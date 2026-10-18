// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "duplicatable_file_handle.hpp"
#include "error_messages.hpp"
#include "generated/enums/action_info.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "http_utility.hpp"
#include "io_context_singleton.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/assembly_utils.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/memfd_utils.hpp"

#include <asm-generic/errno.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

#include <boost/asio/error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <boost/url/url.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/exception.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/message/native_types.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace redfish
{
namespace assembly_binary_data
{

constexpr const char* binaryDataInterface =
    "xyz.openbmc_project.Inventory.BinaryData";

constexpr std::array<std::string_view, 1> binaryDataInterfaces = {
    binaryDataInterface};

constexpr const char* progressInterface = "xyz.openbmc_project.Common.Progress";

// How long a request waits for the item data to be updated.
constexpr std::chrono::seconds progressTimeout{20};

// When to retry a request the implementing service is not ready for.
constexpr std::string_view retryAfterSeconds = "10";

// NVIDIA OEM until DMTF publishes Assembly.ResetToDefaults.
constexpr std::string_view resetToDefaultsAction =
    "NvidiaAssembly.ResetToDefaults";

/** @brief A BinaryData object and the service implementing it. */
struct BinaryDataObject
{
    std::string path;
    std::string service;
};

/** @brief An assembly member with BinaryData, as listed in Assemblies. */
struct BinaryDataMember
{
    std::string memberId;
    BinaryDataObject binaryData;
};

using BinaryDataObjectCallback =
    std::function<void(const std::optional<BinaryDataObject>& object)>;

using ResolveCallback = std::function<void(const BinaryDataObject& object)>;

// Called with whether any assembly has BinaryData, and the resettable members.
using ResettableMembersCallback = std::function<void(
    bool anyBinaryData, const std::vector<BinaryDataMember>& resettable)>;

/**
 * @brief Gets the URL of the ResetToDefaults action of a chassis.
 * @param[in] chassisId - Chassis that contains the assemblies.
 * @return The action target.
 */
inline boost::urls::url getResetToDefaultsTargetUrl(
    const std::string& chassisId)
{
    return boost::urls::format("/redfish/v1/Chassis/{}/Assembly/Actions/Oem/{}",
                               chassisId, resetToDefaultsAction);
}

/**
 * @brief Gets the URL of the ResetToDefaults ActionInfo of a chassis.
 * @param[in] chassisId - Chassis that contains the assemblies.
 * @return The ActionInfo URL.
 */
inline boost::urls::url getResetToDefaultsActionInfoUrl(
    const std::string& chassisId)
{
    return boost::urls::format(
        "/redfish/v1/Chassis/{}/Assembly/Oem/Nvidia/ResetToDefaultsActionInfo",
        chassisId);
}

/**
 * @brief Gets the D-Bus error name of a failed method call.
 * @param[in] msg - Reply message of the call.
 * @return The error name, empty if there is none.
 */
inline std::string_view getDbusErrorName(const sdbusplus::message_t& msg)
{
    const sd_bus_error* dbusError = msg.get_error();
    if (dbusError == nullptr || dbusError->name == nullptr)
    {
        return {};
    }
    return dbusError->name;
}

/**
 * @brief Maps a failed BinaryData method call to a Redfish error.
 * @param[in,out] res - Response to fill in.
 * @param[in] objectPath - BinaryData object the call was made on.
 * @param[in] ec - Error code of the call.
 * @param[in] dbusErrorName - D-Bus error name of the call.
 * @param[in] action - Action that made the call, empty for HTTP methods.
 */
inline void handleBinaryDataError(
    crow::Response& res, const std::string& objectPath,
    const boost::system::error_code& ec, std::string_view dbusErrorName,
    std::string_view action)
{
    if (dbusErrorName == "xyz.openbmc_project.Common.Error.InvalidArgument")
    {
        BMCWEB_LOG_WARNING("{} rejected the image: {}", objectPath, ec);
        messages::unrecognizedRequestBody(res);
        return;
    }
    if (dbusErrorName == "xyz.openbmc_project.Common.Error.NotAllowed")
    {
        BMCWEB_LOG_WARNING("Operation not allowed on {}: {}", objectPath, ec);
        if (action.empty())
        {
            messages::operationNotAllowed(res);
        }
        else
        {
            messages::actionNotSupported(res, action);
        }
        return;
    }
    // The implementing service is busy, restarting, or not answering.
    if (dbusErrorName == "xyz.openbmc_project.Common.Error.Unavailable" ||
        dbusErrorName == "org.freedesktop.DBus.Error.ServiceUnknown" ||
        dbusErrorName == "org.freedesktop.DBus.Error.NameHasNoOwner" ||
        dbusErrorName == "org.freedesktop.DBus.Error.NoReply" ||
        dbusErrorName == "org.freedesktop.DBus.Error.Timeout")
    {
        BMCWEB_LOG_ERROR("{} temporarily unavailable: {}, {}", objectPath, ec,
                         dbusErrorName);
        messages::serviceTemporarilyUnavailable(res, retryAfterSeconds);
        return;
    }
    // Includes Common.Device.Error.ReadFailure and WriteFailure.
    BMCWEB_LOG_ERROR("Call on {} failed: {}, {}", objectPath, ec,
                     dbusErrorName);
    messages::internalError(res);
}

/**
 * @brief Picks the BinaryData object from an association lookup.
 * @param[in] ec - Error code of the lookup.
 * @param[in] subtree - Response of the lookup.
 * @return The first BinaryData object, none if the assembly has none.
 */
inline std::optional<BinaryDataObject> findBinaryDataObject(
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    // The mapper fails the lookup when the association does not exist.
    if (ec || subtree.empty() || subtree.front().second.empty())
    {
        return std::nullopt;
    }
    return BinaryDataObject{subtree.front().first,
                            subtree.front().second.front().first};
}

/**
 * @brief Calls back with the BinaryData object found by an association
 * lookup.
 * @param[in] callback - Called with the object, none if there is none.
 * @param[in] ec - Error code of the lookup.
 * @param[in] subtree - Response of the lookup.
 */
inline void afterGetBinaryDataSubTree(
    const BinaryDataObjectCallback& callback,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    callback(findBinaryDataObject(ec, subtree));
}

/**
 * @brief Gets the BinaryData object linked to an assembly by its described_by
 * association.
 * @param[in] assemblyPath - Assembly object path.
 * @param[in] callback - Called with the object, none if there is none.
 */
inline void getBinaryDataObject(const std::string& assemblyPath,
                                BinaryDataObjectCallback&& callback)
{
    dbus::utility::getAssociatedSubTree(
        sdbusplus::object_path(assemblyPath) / "described_by",
        sdbusplus::object_path("/"), 0, binaryDataInterfaces,
        std::bind_front(afterGetBinaryDataSubTree, std::move(callback)));
}

/**
 * @brief Fills BinaryDataURI on an assembly member that has BinaryData, and
 * the ResetToDefaults action on the resource.
 * @param[in,out] asyncResp - Response holding the Assemblies array.
 * @param[in] chassisId - Chassis that contains the assembly.
 * @param[in] assemblyPath - Assembly object path.
 * @param[in] assemblyJsonPtr - The member in the Assemblies array.
 * @param[in] object - BinaryData object of the assembly, none if it has none.
 */
inline void afterGetBinaryDataObject(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& assemblyPath,
    const nlohmann::json::json_pointer& assemblyJsonPtr,
    const std::optional<BinaryDataObject>& object)
{
    if (!object)
    {
        return;
    }
    asyncResp->res.jsonValue[assemblyJsonPtr]["BinaryDataURI"] =
        boost::urls::format("/redfish/v1/Chassis/{}/Assembly/{}/BinaryData",
                            chassisId,
                            sdbusplus::object_path(assemblyPath).filename());
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        // The ActionInfo tells which members can actually be reset.
        nlohmann::json& action =
            asyncResp->res.jsonValue["Actions"]["Oem"]
                                    ["#" + std::string(resetToDefaultsAction)];
        action["target"] = getResetToDefaultsTargetUrl(chassisId);
        action["@Redfish.ActionInfo"] =
            getResetToDefaultsActionInfoUrl(chassisId);
    }
}

/**
 * @brief Adds BinaryDataURI to an assembly member that has BinaryData, and
 * the ResetToDefaults action to the resource.
 * @param[in,out] asyncResp - Response holding the Assemblies array.
 * @param[in] chassisId - Chassis that contains the assembly.
 * @param[in] assemblyPath - Assembly object path.
 * @param[in] assemblyJsonPtr - The member in the Assemblies array.
 */
inline void addAssemblyBinaryData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& assemblyPath,
    const nlohmann::json::json_pointer& assemblyJsonPtr)
{
    getBinaryDataObject(
        assemblyPath,
        std::bind_front(afterGetBinaryDataObject, asyncResp, chassisId,
                        assemblyPath, assemblyJsonPtr));
}

/**
 * @brief Calls back with the BinaryData object of a resolved assembly, or
 * fails the response if it has none.
 * @param[in,out] asyncResp - Response for errors.
 * @param[in] assemblyLeaf - Leaf name from the URI.
 * @param[in] callback - Called with the BinaryData object.
 * @param[in] object - BinaryData object of the assembly, none if it has none.
 */
inline void afterGetBinaryDataForResolve(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& assemblyLeaf, const ResolveCallback& callback,
    const std::optional<BinaryDataObject>& object)
{
    if (!object)
    {
        BMCWEB_LOG_WARNING("Assembly {} has no BinaryData", assemblyLeaf);
        messages::resourceNotFound(asyncResp->res, "Assembly", assemblyLeaf);
        return;
    }
    callback(*object);
}

/**
 * @brief Continues the request if the chassis exists.
 * @param[in,out] asyncResp - Response for errors.
 * @param[in] chassisId - Chassis from the URI.
 * @param[in] callback - Called if the chassis exists.
 * @param[in] chassisPath - Object path of the chassis, none if it is unknown.
 */
inline void afterGetValidChassis(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::function<void()>& callback,
    const std::optional<std::string>& chassisPath)
{
    if (!chassisPath)
    {
        BMCWEB_LOG_WARNING("Chassis {} not found", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }
    callback();
}

/**
 * @brief Finds the assembly whose leaf name is used in a BinaryData URI, and
 * looks up its BinaryData object.
 * @param[in,out] asyncResp - Response for errors.
 * @param[in] chassisId - Chassis that contains the assembly.
 * @param[in] assemblyLeaf - Leaf name from the URI.
 * @param[in] callback - Called with the BinaryData object.
 * @param[in] assemblyList - Assembly paths of the chassis.
 */
inline void afterGetAssembliesForResolve(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& assemblyLeaf,
    const ResolveCallback& callback, const boost::system::error_code& /*ec*/,
    const std::vector<std::string>& assemblyList)
{
    // getChassisAssembly reports its errors itself.
    const auto hasLeaf = [&assemblyLeaf](const std::string& path) {
        return sdbusplus::object_path(path).filename() == assemblyLeaf;
    };
    auto it = std::ranges::find_if(assemblyList, hasLeaf);
    if (it == assemblyList.end())
    {
        BMCWEB_LOG_WARNING("Assembly {} not found in chassis {}", assemblyLeaf,
                           chassisId);
        messages::resourceNotFound(asyncResp->res, "Assembly", assemblyLeaf);
        return;
    }
    // Never act on an ambiguous URI: it could target the wrong assembly.
    if (std::ranges::count_if(assemblyList, hasLeaf) > 1)
    {
        BMCWEB_LOG_ERROR("Assembly {} is not unique in chassis {}",
                         assemblyLeaf, chassisId);
        messages::internalError(asyncResp->res);
        return;
    }
    getBinaryDataObject(*it,
                        std::bind_front(afterGetBinaryDataForResolve, asyncResp,
                                        assemblyLeaf, callback));
}

/**
 * @brief Resolves an assembly by the leaf name used in its BinaryData URI and
 * finds its BinaryData object.
 * @param[in,out] asyncResp - Response for errors.
 * @param[in] chassisId - Chassis that contains the assembly.
 * @param[in] assemblyLeaf - Leaf name from the URI.
 * @param[in] callback - Called with the BinaryData object.
 */
inline void resolveBinaryData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& assemblyLeaf,
    ResolveCallback&& callback)
{
    chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(afterGetValidChassis, asyncResp, chassisId,
                        [asyncResp, chassisId, assemblyLeaf,
                         callback = std::move(callback)]() {
                            assembly_utils::getChassisAssembly(
                                asyncResp, chassisId,
                                std::bind_front(afterGetAssembliesForResolve,
                                                asyncResp, chassisId,
                                                assemblyLeaf, callback));
                        }));
}

/**
 * @brief Streams the image returned by GetData as the response body.
 * @param[in,out] asyncResp - Response to stream the image into.
 * @param[in] objectPath - BinaryData object that was read.
 * @param[in] ec - Error code of the call.
 * @param[in] msg - Reply message of the call.
 * @param[in] unixfd - Descriptor of the image.
 */
inline void afterGetData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath, const boost::system::error_code& ec,
    const sdbusplus::message_t& msg, const sdbusplus::message::unix_fd& unixfd)
{
    if (ec)
    {
        handleBinaryDataError(asyncResp->res, objectPath, ec,
                              getDbusErrorName(msg), "");
        return;
    }
    // The message owns the received descriptor.
    int fd = dup(unixfd);
    if (fd < 0)
    {
        BMCWEB_LOG_ERROR("Failed to duplicate the image descriptor");
        messages::internalError(asyncResp->res);
        return;
    }
    if (lseek(fd, 0, SEEK_SET) < 0)
    {
        BMCWEB_LOG_ERROR("Failed to rewind the image descriptor: {}", errno);
        close(fd);
        messages::internalError(asyncResp->res);
        return;
    }
    if (!asyncResp->res.openFd(DuplicatableFileHandle(fd)))
    {
        BMCWEB_LOG_ERROR("Failed to open the image descriptor");
        messages::internalError(asyncResp->res);
        return;
    }
    asyncResp->res.addHeader(boost::beast::http::field::content_type,
                             "application/octet-stream");
    asyncResp->res.addHeader("Content-Transfer-Encoding", "Binary");
}

/**
 * @brief Returns the image of a BinaryData object.
 * @param[in,out] asyncResp - Response to stream the image into.
 * @param[in] object - BinaryData object to read.
 */
inline void readBinaryData(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const BinaryDataObject& object)
{
    dbus::utility::async_method_call(
        asyncResp,
        [asyncResp,
         path = object.path](const boost::system::error_code& ec,
                             const sdbusplus::message_t& msg,
                             const sdbusplus::message::unix_fd& unixfd) {
            afterGetData(asyncResp, path, ec, msg, unixfd);
        },
        object.service, object.path, binaryDataInterface, "GetData");
}

/**
 * @brief Handles GET of an assembly's BinaryData URI.
 * @param[in] app - Application the route belongs to.
 * @param[in] req - The request.
 * @param[in,out] asyncResp - Response to fill in.
 * @param[in] chassisId - Chassis from the URI.
 * @param[in] assemblyLeaf - Assembly leaf name from the URI.
 */
inline void handleAssemblyBinaryDataGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& assemblyLeaf)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (!http_helpers::isContentTypeAllowed(
            req.getHeaderValue("Accept"),
            http_helpers::ContentType::OctetStream, true))
    {
        asyncResp->res.result(boost::beast::http::status::not_acceptable);
        return;
    }
    resolveBinaryData(asyncResp, chassisId, assemblyLeaf,
                      std::bind_front(readBinaryData, asyncResp));
}

/**
 * @brief Fills the response of a completed PUT.
 * @param[in,out] res - Response to fill in.
 */
inline void setNoContent(crow::Response& res)
{
    res.result(boost::beast::http::status::no_content);
}

/**
 * @brief Fills the response of a completed action.
 * @param[in,out] res - Response to fill in.
 */
inline void setSuccess(crow::Response& res)
{
    messages::success(res);
}

/**
 * @brief Waits for a BinaryData request to complete.
 *
 * SetData and ResetToDefaults return once the request is accepted; the
 * Progress Status of the object then reports whether the item data was
 * updated. The response is held until Status leaves InProgress, or until the
 * wait times out.
 *
 * The timer keeps the wait alive until the response is decided. The match
 * holds only a weak reference, since the wait owns the match.
 */
class ProgressWait : public std::enable_shared_from_this<ProgressWait>
{
  public:
    using CompletedCallback = std::function<void(crow::Response&)>;

    /**
     * @brief Constructs the wait.
     * @param[in] asyncRespIn - Response held until the request completes.
     * @param[in] objectIn - BinaryData object the request was made on.
     * @param[in] onCompletedIn - Fills the response if the request completed.
     */
    ProgressWait(const std::shared_ptr<bmcweb::AsyncResp>& asyncRespIn,
                 const BinaryDataObject& objectIn,
                 CompletedCallback onCompletedIn) :
        asyncResp(asyncRespIn), object(objectIn),
        onCompleted(std::move(onCompletedIn)), timer(getIoContext())
    {}

    /**
     * @brief Starts waiting.
     *
     * The match is set before Status is read, so a change between the two is
     * not missed.
     */
    void run()
    {
        match = std::make_unique<sdbusplus::bus::match_t>(
            *crow::connections::systemBus,
            sdbusplus::bus::match::rules::propertiesChanged(object.path,
                                                            progressInterface) +
                sdbusplus::bus::match::rules::sender(object.service),
            std::bind_front(onPropertiesChanged, weak_from_this()));
        timer.expires_after(progressTimeout);
        timer.async_wait(
            std::bind_front(&ProgressWait::onTimeout, shared_from_this()));
        dbus::utility::getProperty<std::string>(
            object.service, object.path, progressInterface, "Status",
            std::bind_front(&ProgressWait::afterGetStatus, shared_from_this()));
    }

  private:
    /**
     * @brief Handles a change of the Progress properties.
     * @param[in] weakSelf - The wait, if it still exists.
     * @param[in] msg - The PropertiesChanged signal.
     */
    static void onPropertiesChanged(const std::weak_ptr<ProgressWait>& weakSelf,
                                    sdbusplus::message_t& msg)
    {
        std::shared_ptr<ProgressWait> self = weakSelf.lock();
        if (!self || self->done)
        {
            return;
        }
        std::string interface;
        dbus::utility::DBusPropertiesMap values;
        try
        {
            msg.read(interface, values);
        }
        catch (const sdbusplus::exception_t& e)
        {
            BMCWEB_LOG_ERROR("Failed to read PropertiesChanged signal: {}",
                             e.what());
            return;
        }
        const std::string* status = nullptr;
        if (sdbusplus::unpackPropertiesNoThrow(dbus_utils::UnpackErrorPrinter(),
                                               values, "Status", status) &&
            status != nullptr)
        {
            self->checkStatus(*status);
        }
    }

    /**
     * @brief Handles the read of Status.
     * @param[in] ec - Error code of the read.
     * @param[in] status - Value of Status.
     */
    void afterGetStatus(const boost::system::error_code& ec,
                        const std::string& status)
    {
        if (done)
        {
            return;
        }
        if (ec)
        {
            // The match or the timer completes the request.
            BMCWEB_LOG_ERROR("Failed to read the Status of {}: {}", object.path,
                             ec);
            return;
        }
        checkStatus(status);
    }

    /**
     * @brief Completes the response once Status leaves InProgress.
     * @param[in] status - Value of Status.
     */
    void checkStatus(const std::string& status)
    {
        if (status ==
            "xyz.openbmc_project.Common.Progress.OperationStatus.InProgress")
        {
            return;
        }
        finish();
        if (status ==
            "xyz.openbmc_project.Common.Progress.OperationStatus.Completed")
        {
            BMCWEB_LOG_DEBUG("Update of {} completed", object.path);
            onCompleted(asyncResp->res);
            return;
        }
        BMCWEB_LOG_ERROR("Update of {} ended with {}", object.path, status);
        messages::operationFailed(asyncResp->res);
    }

    /**
     * @brief Fails the response if the request did not complete in time.
     * @param[in] ec - Error code of the timer.
     */
    void onTimeout(const boost::system::error_code& ec)
    {
        if (done || ec == boost::asio::error::operation_aborted)
        {
            return;
        }
        finish();
        BMCWEB_LOG_ERROR("Update of {} did not complete in time", object.path);
        messages::operationTimeout(asyncResp->res);
    }

    /**
     * @brief Stops waiting; the response is sent once the wait is released.
     */
    void finish()
    {
        done = true;
        match.reset();
        timer.cancel();
    }

    std::shared_ptr<bmcweb::AsyncResp> asyncResp;
    BinaryDataObject object;
    CompletedCallback onCompleted;
    std::unique_ptr<sdbusplus::bus::match_t> match;
    boost::asio::steady_timer timer;
    // Set once the response is decided; later events are ignored.
    bool done = false;
};

/**
 * @brief Waits for an accepted BinaryData request to complete, or fails the
 * response if it was not accepted.
 * @param[in,out] asyncResp - Response held until the request completes.
 * @param[in] object - BinaryData object the request was made on.
 * @param[in] action - Action that made the request, empty for PUT.
 * @param[in] onCompleted - Fills the response if the request completed.
 * @param[in] ec - Error code of the call.
 * @param[in] msg - Reply message of the call.
 */
inline void afterBinaryDataRequest(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const BinaryDataObject& object, std::string_view action,
    const ProgressWait::CompletedCallback& onCompleted,
    const boost::system::error_code& ec, const sdbusplus::message_t& msg)
{
    if (ec)
    {
        handleBinaryDataError(asyncResp->res, object.path, ec,
                              getDbusErrorName(msg), action);
        return;
    }
    std::make_shared<ProgressWait>(asyncResp, object, onCompleted)->run();
}

/**
 * @brief Replaces the image of a BinaryData object.
 * @param[in,out] asyncResp - Response held until the request completes.
 * @param[in] image - Memory file holding the request body.
 * @param[in] object - BinaryData object to write.
 */
inline void writeBinaryData(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::shared_ptr<MemoryFD>& image,
                            const BinaryDataObject& object)
{
    dbus::utility::async_method_call(
        asyncResp,
        [asyncResp, object](const boost::system::error_code& ec,
                            const sdbusplus::message_t& msg) {
            afterBinaryDataRequest(asyncResp, object, "", setNoContent, ec,
                                   msg);
        },
        object.service, object.path, binaryDataInterface, "SetData",
        sdbusplus::message::unix_fd(image->fd));
}

/**
 * @brief Handles PUT of an assembly's BinaryData URI.
 * @param[in] app - Application the route belongs to.
 * @param[in] req - The request.
 * @param[in,out] asyncResp - Response to fill in.
 * @param[in] chassisId - Chassis from the URI.
 * @param[in] assemblyLeaf - Assembly leaf name from the URI.
 */
inline void handleAssemblyBinaryDataPut(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& assemblyLeaf)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    std::shared_ptr<MemoryFD> image;
    try
    {
        image = std::make_shared<MemoryFD>();
        image->writeString(req.body());
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("Failed to write the image to a memfd: {}", e.what());
        messages::internalError(asyncResp->res);
        return;
    }
    // The receiver shares the file offset, so it must start at the image.
    if (!image->rewind())
    {
        messages::internalError(asyncResp->res);
        return;
    }
    resolveBinaryData(asyncResp, chassisId, assemblyLeaf,
                      std::bind_front(writeBinaryData, asyncResp, image));
}

/**
 * @brief Scans the assemblies of a chassis for resettable BinaryData.
 *
 * The assemblies are checked one at a time, in the order of the Assemblies
 * array. For each one, the scan looks up its BinaryData object; if it has
 * one, it reads the assembly's MemberId as the Assembly resource does, then
 * Resettable. Once all are checked, the callback gets the result. An assembly
 * whose lookup fails is logged and left out.
 */
class ResettableScan : public std::enable_shared_from_this<ResettableScan>
{
  public:
    /**
     * @brief Constructs the scan.
     * @param[in] assembliesIn - Assembly paths, in the order of the
     * Assemblies array.
     * @param[in] callbackIn - Called with the result of the scan.
     */
    ResettableScan(std::vector<std::string> assembliesIn,
                   ResettableMembersCallback callbackIn) :
        assemblies(std::move(assembliesIn)), callback(std::move(callbackIn))
    {}

    /** @brief Starts the scan. */
    void run()
    {
        checkAssembly(0);
    }

  private:
    /**
     * @brief Checks the assembly at a position, or calls back once all are
     * checked.
     * @param[in] index - Position of the assembly in the Assemblies array.
     */
    void checkAssembly(std::size_t index)
    {
        if (index == assemblies.size())
        {
            callback(anyBinaryData, resettable);
            return;
        }
        getBinaryDataObject(
            assemblies[index],
            std::bind_front(&ResettableScan::afterBinaryDataObject,
                            shared_from_this(), index));
    }

    /**
     * @brief Looks up the service of an assembly that has BinaryData, or moves
     * on to the next assembly.
     * @param[in] index - Position of the assembly in the Assemblies array.
     * @param[in] object - BinaryData object of the assembly, none if it has
     * none.
     */
    void afterBinaryDataObject(std::size_t index,
                               const std::optional<BinaryDataObject>& object)
    {
        if (!object)
        {
            assemblyChecked(index, std::nullopt);
            return;
        }
        anyBinaryData = true;
        dbus::utility::getDbusObject(
            assemblies[index], assemblyInterfaces,
            std::bind_front(&ResettableScan::afterAssemblyObject,
                            shared_from_this(), index, *object));
    }

    /**
     * @brief Reads all properties of an assembly that has BinaryData.
     * @param[in] index - Position of the assembly in the Assemblies array.
     * @param[in] binaryData - BinaryData object of the assembly.
     * @param[in] ec - Error code of the lookup.
     * @param[in] assemblyObject - Services implementing the assembly.
     */
    void afterAssemblyObject(
        std::size_t index, const BinaryDataObject& binaryData,
        const boost::system::error_code& ec,
        const dbus::utility::MapperGetObject& assemblyObject)
    {
        if (ec)
        {
            // No MemberId is known for it, so it cannot be offered.
            skip(index, ec);
            return;
        }
        assembly_utils::getAllAssemblyProperties(
            assemblies[index], assemblyObject,
            std::bind_front(&ResettableScan::afterAssemblyProperties,
                            shared_from_this(), index, binaryData));
    }

    /**
     * @brief Derives the MemberId of an assembly from its properties, then
     * reads Resettable.
     * @param[in] index - Position of the assembly in the Assemblies array.
     * @param[in] binaryData - BinaryData object of the assembly.
     * @param[in] ec - Error code of the read.
     * @param[in] properties - Properties of the assembly.
     */
    void afterAssemblyProperties(
        std::size_t index, const BinaryDataObject& binaryData,
        const boost::system::error_code& ec,
        const dbus::utility::DBusPropertiesMap& properties)
    {
        // Unread properties leave the position as the MemberId, as in the
        // Assembly resource.
        const dbus::utility::DBusPropertiesMap noProperties;
        getResettable(index, binaryData,
                      assembly_utils::getAssemblyMemberId(
                          index, ec ? noProperties : properties));
    }

    /**
     * @brief Reads whether the BinaryData of an assembly is resettable.
     * @param[in] index - Position of the assembly in the Assemblies array.
     * @param[in] binaryData - BinaryData object of the assembly.
     * @param[in] memberId - MemberId of the assembly.
     */
    void getResettable(std::size_t index, const BinaryDataObject& binaryData,
                       std::string memberId)
    {
        dbus::utility::getProperty<bool>(
            binaryData.service, binaryData.path, binaryDataInterface,
            "Resettable",
            std::bind_front(&ResettableScan::afterResettable,
                            shared_from_this(), index,
                            BinaryDataMember{std::move(memberId), binaryData}));
    }

    /**
     * @brief Records an assembly whose BinaryData is resettable.
     * @param[in] index - Position of the assembly in the Assemblies array.
     * @param[in] member - The assembly as an Assemblies member.
     * @param[in] ec - Error code of the read.
     * @param[in] isResettable - Value of Resettable.
     */
    void afterResettable(std::size_t index, const BinaryDataMember& member,
                         const boost::system::error_code& ec, bool isResettable)
    {
        // An absent BinaryData object or property is not resettable.
        if (ec && ec.value() != EBADR)
        {
            skip(index, ec);
            return;
        }
        assemblyChecked(index, (!ec && isResettable) ? std::optional(member)
                                                     : std::nullopt);
    }

    /**
     * @brief Ends the check of an assembly: records it if resettable, and
     * moves on to the next one. Every check ends here.
     * @param[in] index - Position of the assembly in the Assemblies array.
     * @param[in] member - The assembly, if its BinaryData is resettable.
     */
    void assemblyChecked(std::size_t index,
                         const std::optional<BinaryDataMember>& member)
    {
        if (member)
        {
            resettable.emplace_back(*member);
        }
        checkAssembly(index + 1);
    }

    /**
     * @brief Logs a failed lookup and moves on without the assembly, so that
     * the other assemblies can still be reset.
     * @param[in] index - Position of the assembly in the Assemblies array.
     * @param[in] ec - Error code of the lookup.
     */
    void skip(std::size_t index, const boost::system::error_code& ec)
    {
        BMCWEB_LOG_WARNING("Skipping assembly {}: D-Bus error {}",
                           assemblies[index], ec);
        assemblyChecked(index, std::nullopt);
    }

    std::vector<std::string> assemblies;
    ResettableMembersCallback callback;
    bool anyBinaryData = false;
    std::vector<BinaryDataMember> resettable;
};

/**
 * @brief Starts the scan of the assemblies of a chassis.
 * @param[in] callback - Called with the result of the scan.
 * @param[in] assemblyList - Assembly paths of the chassis.
 */
inline void afterGetAssembliesForScan(
    const ResettableMembersCallback& callback,
    const boost::system::error_code& /*ec*/,
    const std::vector<std::string>& assemblyList)
{
    // getChassisAssembly reports its errors itself.
    std::make_shared<ResettableScan>(assemblyList, callback)->run();
}

/**
 * @brief Lists the resettable BinaryData members of a chassis, with the
 * MemberIds the Assembly resource shows for them.
 * @param[in,out] asyncResp - Response for errors.
 * @param[in] chassisId - Chassis whose assemblies are listed.
 * @param[in] callback - Called with whether any assembly has BinaryData, and
 * the resettable members.
 */
inline void getResettableMembers(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, ResettableMembersCallback&& callback)
{
    chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(
            afterGetValidChassis, asyncResp, chassisId,
            [asyncResp, chassisId, callback = std::move(callback)]() {
                assembly_utils::getChassisAssembly(
                    asyncResp, chassisId,
                    std::bind_front(afterGetAssembliesForScan, callback));
            }));
}

/**
 * @brief Fills the ResetToDefaults ActionInfo once the resettable members
 * are known.
 * @param[in,out] asyncResp - Response to fill in.
 * @param[in] chassisId - Chassis whose assemblies were scanned.
 * @param[in] anyBinaryData - Whether any assembly has BinaryData.
 * @param[in] members - The resettable members.
 */
inline void afterGetMembersForActionInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, bool anyBinaryData,
    const std::vector<BinaryDataMember>& members)
{
    // The action, and so its ActionInfo, exists only with some BinaryData.
    if (!anyBinaryData)
    {
        BMCWEB_LOG_WARNING("No assembly with BinaryData in chassis {}",
                           chassisId);
        messages::resourceNotFound(asyncResp->res, "ActionInfo",
                                   "ResetToDefaultsActionInfo");
        return;
    }
    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/ActionInfo/ActionInfo.json>; rel=describedby");
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_1_2.ActionInfo";
    asyncResp->res.jsonValue["@odata.id"] =
        getResetToDefaultsActionInfoUrl(chassisId);
    asyncResp->res.jsonValue["Name"] = "ResetToDefaults Action Info";
    asyncResp->res.jsonValue["Id"] = "ResetToDefaultsActionInfo";

    nlohmann::json::array_t parameters;
    nlohmann::json::object_t parameter;
    parameter["Name"] = "MemberId";
    parameter["Required"] = true;
    parameter["DataType"] = action_info::ParameterTypes::String;
    nlohmann::json::array_t allowed;
    for (const BinaryDataMember& member : members)
    {
        allowed.emplace_back(member.memberId);
    }
    parameter["AllowableValues"] = std::move(allowed);
    parameters.emplace_back(std::move(parameter));
    asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
}

/**
 * @brief Handles GET of the ResetToDefaults ActionInfo.
 * @param[in] app - Application the route belongs to.
 * @param[in] req - The request.
 * @param[in,out] asyncResp - Response to fill in.
 * @param[in] chassisId - Chassis from the URI.
 */
inline void handleAssemblyResetToDefaultsActionInfoGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    getResettableMembers(
        asyncResp, chassisId,
        std::bind_front(afterGetMembersForActionInfo, asyncResp, chassisId));
}

/**
 * @brief Resets the member selected by the action's MemberId.
 * @param[in,out] asyncResp - Response held until the reset completes.
 * @param[in] memberId - MemberId from the request.
 * @param[in] members - The resettable members.
 */
inline void afterGetMembersForReset(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& memberId, bool /*anyBinaryData*/,
    const std::vector<BinaryDataMember>& members)
{
    if (members.empty())
    {
        BMCWEB_LOG_WARNING("No resettable assembly for MemberId {}", memberId);
        messages::actionNotSupported(asyncResp->res, resetToDefaultsAction);
        return;
    }
    const auto hasMemberId = [&memberId](const BinaryDataMember& member) {
        return member.memberId == memberId;
    };
    auto it = std::ranges::find_if(members, hasMemberId);
    if (it == members.end())
    {
        BMCWEB_LOG_WARNING("MemberId {} is not resettable", memberId);
        messages::actionParameterValueNotInList(
            asyncResp->res, memberId, "MemberId", resetToDefaultsAction);
        return;
    }
    // Never reset on an ambiguous MemberId: it could target the wrong assembly.
    if (std::ranges::count_if(members, hasMemberId) > 1)
    {
        BMCWEB_LOG_ERROR("MemberId {} is not unique", memberId);
        messages::internalError(asyncResp->res);
        return;
    }
    const BinaryDataObject& object = it->binaryData;
    dbus::utility::async_method_call(
        asyncResp,
        [asyncResp, object](const boost::system::error_code& ec,
                            const sdbusplus::message_t& msg) {
            afterBinaryDataRequest(asyncResp, object, resetToDefaultsAction,
                                   setSuccess, ec, msg);
        },
        object.service, object.path, binaryDataInterface, "ResetToDefaults");
}

/**
 * @brief Handles POST of the ResetToDefaults action.
 * @param[in] app - Application the route belongs to.
 * @param[in] req - The request.
 * @param[in,out] asyncResp - Response to fill in.
 * @param[in] chassisId - Chassis from the URI.
 */
inline void handleAssemblyResetToDefaultsPost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    std::string memberId;
    if (!json_util::readJsonAction(req, asyncResp->res, "MemberId", memberId))
    {
        return;
    }
    getResettableMembers(
        asyncResp, chassisId,
        std::bind_front(afterGetMembersForReset, asyncResp, memberId));
}

/**
 * @brief Registers the BinaryData and ResetToDefaults routes of the Assembly
 * resource.
 * @param[in] app - Application to register the routes with.
 */
inline void requestRoutesAssemblyBinaryData(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Assembly/<str>/BinaryData/")
        .privileges(redfish::privileges::getAssembly)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleAssemblyBinaryDataGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Assembly/<str>/BinaryData/")
        .privileges(redfish::privileges::putAssembly)
        .methods(boost::beast::http::verb::put)(
            std::bind_front(handleAssemblyBinaryDataPut, std::ref(app)));

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Assembly/Actions/Oem/"
                          "NvidiaAssembly.ResetToDefaults/")
            .privileges(redfish::privileges::postAssembly)
            .methods(boost::beast::http::verb::post)(std::bind_front(
                handleAssemblyResetToDefaultsPost, std::ref(app)));

        BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Assembly/Oem/Nvidia/"
                          "ResetToDefaultsActionInfo/")
            .privileges(redfish::privileges::getActionInfo)
            .methods(boost::beast::http::verb::get)(std::bind_front(
                handleAssemblyResetToDefaultsActionInfoGet, std::ref(app)));
    }
}

} // namespace assembly_binary_data
} // namespace redfish
