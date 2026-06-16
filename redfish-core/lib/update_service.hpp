// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
// SPDX-FileCopyrightText: Copyright 2018 Intel Corporation
#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/resource.hpp"
#include "generated/enums/update_service.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "io_context_singleton.hpp"
#include "logging.hpp"
#include "multipart_parser.hpp"
#include "nvidia_error_messages.hpp"
#include "nvidia_update_service.hpp"
#include "ossl_random.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "resource_messages.hpp"
#include "str_utility.hpp"
#include "task.hpp"
#include "task_messages.hpp"
#include "utility.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/sw_utils.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <boost/asio/error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/result.hpp>
#include <boost/url/format.hpp>
#include <boost/url/parse.hpp>
#include <boost/url/url.hpp>
#include <boost/url/url_view.hpp>
#include <boost/url/url_view_base.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/message/native_types.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace redfish
{

// Match signals added on software path
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::unique_ptr<sdbusplus::bus::match_t> fwUpdateMatcher;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::unique_ptr<sdbusplus::bus::match_t> fwUpdateErrorMatcher;

// Timer for software available
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::unique_ptr<boost::asio::steady_timer> fwAvailableTimer;
// match for logging
constexpr auto fwObjectCreationDefaultTimeout = 40;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::unique_ptr<sdbusplus::bus::match::match> loggingMatch = nullptr;

/* @brief String that indicates the Software Update D-Bus interface */
constexpr const char* updateInterface = "xyz.openbmc_project.Software.Update";

struct MemoryFileDescriptor
{
    int fd = -1;

    explicit MemoryFileDescriptor(const std::string& filename) :
        fd(memfd_create(filename.c_str(), 0))
    {}

    MemoryFileDescriptor(const MemoryFileDescriptor&) = default;
    MemoryFileDescriptor(MemoryFileDescriptor&& other) noexcept : fd(other.fd)
    {
        other.fd = -1;
    }
    MemoryFileDescriptor& operator=(const MemoryFileDescriptor&) = delete;
    MemoryFileDescriptor& operator=(MemoryFileDescriptor&&) = default;

    ~MemoryFileDescriptor()
    {
        if (fd != -1)
        {
            close(fd);
        }
    }

    bool rewind() const
    {
        if (lseek(fd, 0, SEEK_SET) == -1)
        {
            BMCWEB_LOG_ERROR("Failed to seek to beginning of image memfd");
            return false;
        }
        return true;
    }
};

inline void cleanUp()
{
    fwUpdateInProgress = false;
    fwUpdateMatcher = nullptr;
    fwUpdateErrorMatcher = nullptr;
}

inline void activateImage(const std::string& objPath,
                          const std::string& service)
{
    BMCWEB_LOG_DEBUG("Activate image for {} {}", objPath, service);
    dbus::utility::setProperty(
        service, objPath, "xyz.openbmc_project.Software.Activation",
        "RequestedActivation",
        "xyz.openbmc_project.Software.Activation.RequestedActivations.Active",
        [](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("error_code = {}", ec);
                BMCWEB_LOG_DEBUG("error msg = {}", ec.message());
            }
        });
}

inline bool handleCreateTask(const boost::system::error_code& ec2,
                             sdbusplus::message_t& msg,
                             const std::shared_ptr<task::TaskData>& taskData)
{
    if (ec2 or !msg)
    {
        // Callback called with an empty message implying the timer has expired
        fwUpdateInProgress = false;
        return task::completed;
    }

    std::string iface;
    dbus::utility::DBusPropertiesMap values;

    std::string index = std::to_string(taskData->index);
    msg.read(iface, values);

    if (iface == "xyz.openbmc_project.Software.Activation")
    {
        const std::string* state = nullptr;
        for (const auto& property : values)
        {
            if (property.first == "Activation")
            {
                state = std::get_if<std::string>(&property.second);
                if (state == nullptr)
                {
                    taskData->messages.emplace_back(messages::internalError());
                    fwUpdateInProgress = false;
                    return task::completed;
                }
            }
        }

        if (state == nullptr)
        {
            return !task::completed;
        }

        if (state->ends_with("Invalid") || state->ends_with("Failed"))
        {
            taskData->messages.emplace_back(messages::taskAborted(index));
            taskData->finishTask();
            taskData->state = "Exception";
            fwUpdateInProgress = false;
            return task::completed;
        }

        // Nvidia code starts here
        if (state->ends_with("Activating"))
        {
            // set firmware inventory inprogress
            // flag to true during activation.
            // this will ensure no furthur
            // updates allowed during this time
            // from redfish
            fwUpdateInProgress = true;
            return !task::completed;
        }
        // Nvidia code ends here

        if (state->ends_with("Staged"))
        {
            taskData->state = "Stopping";
            taskData->messages.emplace_back(messages::taskPaused(index));

            // its staged, set a long timer to
            // allow them time to complete the
            // update (probably cycle the
            // system) if this expires then
            // task will be canceled
            taskData->extendTimer(std::chrono::hours(5));
            return !task::completed;
        }

        if (state->ends_with("Active"))
        {
            taskData->messages.emplace_back(messages::taskCompletedOK(index));
            taskData->finishTask();
            taskData->state = "Completed";
            fwUpdateInProgress = false;
            return task::completed;
        }
    }
    else if (iface == "xyz.openbmc_project.Software.ActivationProgress")
    {
        const uint8_t* progress = nullptr;
        for (const auto& property : values)
        {
            if (property.first == "Progress")
            {
                progress = std::get_if<uint8_t>(&property.second);
                if (progress == nullptr)
                {
                    taskData->messages.emplace_back(messages::internalError());
                    return task::completed;
                }
            }
        }

        if (progress == nullptr)
        {
            return !task::completed;
        }
        taskData->percentComplete = *progress;
        taskData->messages.emplace_back(
            messages::taskProgressChanged(index, *progress));

        // if we're getting status updates it's
        // still alive, update timer
        taskData->extendTimer(
            std::chrono::minutes(BMCWEB_UPDATE_SERVICE_TASK_TIMEOUT));
    }

    // as firmware update often results in a
    // reboot, the task  may never "complete"
    // unless it is an error

    return !task::completed;
}

inline void createTask(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       task::Payload&& payload,
                       const sdbusplus::message::object_path& objPath)
{
    std::shared_ptr<task::TaskData> task = task::TaskData::createTask(
        std::bind_front(handleCreateTask),
        "type='signal',interface='org.freedesktop.DBus.Properties',"
        "member='PropertiesChanged',path='" +
            objPath.str + "'");

    // Nvidia modified code to use constant
    task->startTimer(std::chrono::minutes(BMCWEB_UPDATE_SERVICE_TASK_TIMEOUT));
    task->populateResp(asyncResp->res);
    task->payload.emplace(std::move(payload));
    // Nvidia code starts here
    loggingMatch = std::make_unique<sdbusplus::bus::match::match>(
        *crow::connections::systemBus,
        "interface='org.freedesktop.DBus.ObjectManager',type='signal',"
        "member='InterfacesAdded',"
        "path='/xyz/openbmc_project/logging'",
        [task](sdbusplus::message_t& msgLog) {
            loggingMatchCallback(task, msgLog);
        });
    if (!preTaskMessages.empty())
    {
        task->messages.insert(task->messages.end(), preTaskMessages.begin(),
                              preTaskMessages.end());
    }
    preTaskMessages = {};

    checkInitialActivationState(task, objPath);
    // Nvidia code ends here
}

// Note that asyncResp can be either a valid pointer or nullptr. If nullptr
// then no asyncResp updates will occur
inline void softwareInterfaceAdded(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    sdbusplus::message_t& m, task::Payload&& payload)
{
    dbus::utility::DBusInterfacesMap interfacesProperties;

    sdbusplus::message::object_path objPath;

    m.read(objPath, interfacesProperties);

    BMCWEB_LOG_DEBUG("obj path = {}", objPath.str);
    for (const auto& interface : interfacesProperties)
    {
        BMCWEB_LOG_DEBUG("interface = {}", interface.first);

        if (interface.first == "xyz.openbmc_project.Software.Activation")
        {
            // Retrieve service and activate
            constexpr std::array<std::string_view, 1> interfaces = {
                "xyz.openbmc_project.Software.Activation"};
            dbus::utility::getDbusObject(
                objPath.str, interfaces,
                [objPath, asyncResp, payload(std::move(payload))](
                    const boost::system::error_code& ec,
                    const std::vector<
                        std::pair<std::string, std::vector<std::string>>>&
                        objInfo) mutable {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG("error_code = {}", ec);
                        BMCWEB_LOG_DEBUG("error msg = {}", ec.message());
                        if (asyncResp)
                        {
                            messages::internalError(asyncResp->res);
                        }
                        cleanUp();
                        return;
                    }
                    // Ensure we only got one service back
                    if (objInfo.size() != 1)
                    {
                        BMCWEB_LOG_ERROR("Invalid Object Size {}",
                                         objInfo.size());
                        if (asyncResp)
                        {
                            messages::internalError(asyncResp->res);
                        }
                        cleanUp();
                        return;
                    }
                    // cancel timer only when
                    // xyz.openbmc_project.Software.Activation interface
                    // is added
                    fwAvailableTimer = nullptr;
                    // Nvidia code starts here
                    sdbusplus::message::object_path objectPath(objPath.str);
                    std::string swID = objectPath.filename();
                    if (swID.empty())
                    {
                        BMCWEB_LOG_ERROR("Software Id is empty");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    // Nvidia code ends here
                    activateImage(objPath.str, objInfo[0].first);
                    if (asyncResp)
                    {
                        createTask(asyncResp, std::move(payload), objPath);
                    }
                    fwUpdateInProgress = false;
                });

            break;
        }
    }
}

// Nvidia modified function arguments to facilitate removal of image
inline void afterAvailbleTimerAsyncWait(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& imagePath, const boost::system::error_code& ec)
{
    cleanUp();
    if (ec == boost::asio::error::operation_aborted)
    {
        // expected, we were canceled before the timer completed.
        return;
    }
    BMCWEB_LOG_ERROR("Timed out waiting for firmware object being created");
    BMCWEB_LOG_ERROR("FW image may has already been uploaded to server");
    if (ec)
    {
        BMCWEB_LOG_ERROR("Async_wait failed{}", ec);
        return;
    }
    if (asyncResp)
    {
        redfish::messages::internalError(asyncResp->res);
    }
    // Nvidia code starts here
    // remove update package to allow next update
    if (!imagePath.empty())
    {
        std::filesystem::remove(imagePath);
    }
    // Nvidia code ends here
}

inline void handleUpdateErrorType(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const std::string& url,
    const std::string& type)
{
    // NOLINTBEGIN(bugprone-branch-clone)
    if (type == "xyz.openbmc_project.Software.Image.Error.UnTarFailure")
    {
        messages::missingOrMalformedPart(asyncResp->res);
    }
    else if (type ==
             "xyz.openbmc_project.Software.Image.Error.ManifestFileFailure")
    {
        messages::missingOrMalformedPart(asyncResp->res);
    }
    else if (type == "xyz.openbmc_project.Software.Image.Error.ImageFailure")
    {
        messages::missingOrMalformedPart(asyncResp->res);
    }
    else if (type == "xyz.openbmc_project.Software.Version.Error.AlreadyExists")
    {
        messages::resourceAlreadyExists(asyncResp->res, "UpdateService",
                                        "Version", "uploaded version");
    }
    else if (type == "xyz.openbmc_project.Software.Image.Error.BusyFailure")
    {
        messages::serviceTemporarilyUnavailable(asyncResp->res, url);
    }
    else if (type == "xyz.openbmc_project.Software.Version.Error.Incompatible")
    {
        messages::internalError(asyncResp->res);
    }
    else if (type ==
             "xyz.openbmc_project.Software.Version.Error.ExpiredAccessKey")
    {
        messages::internalError(asyncResp->res);
    }
    else if (type ==
             "xyz.openbmc_project.Software.Version.Error.InvalidSignature")
    {
        messages::missingOrMalformedPart(asyncResp->res);
    }
    else if (type ==
                 "xyz.openbmc_project.Software.Image.Error.InternalFailure" ||
             type == "xyz.openbmc_project.Software.Version.Error.HostFile")
    {
        BMCWEB_LOG_ERROR("Software Image Error type={}", type);
        messages::internalError(asyncResp->res);
    }
    else
    {
        // Unrelated error types. Ignored
        BMCWEB_LOG_INFO("Non-Software-related Error type={}. Ignored", type);
        return;
    }
    // NOLINTEND(bugprone-branch-clone)
    // Clear the timer
    fwAvailableTimer = nullptr;
}

inline void afterUpdateErrorMatcher(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const std::string& url,
    sdbusplus::message_t& m)
{
    dbus::utility::DBusInterfacesMap interfacesProperties;
    sdbusplus::message::object_path objPath;
    m.read(objPath, interfacesProperties);
    BMCWEB_LOG_DEBUG("obj path = {}", objPath.str);
    for (const std::pair<std::string, dbus::utility::DBusPropertiesMap>&
             interface : interfacesProperties)
    {
        if (interface.first == "xyz.openbmc_project.Logging.Entry")
        {
            for (const std::pair<std::string, dbus::utility::DbusVariantType>&
                     value : interface.second)
            {
                if (value.first != "Message")
                {
                    continue;
                }
                const std::string* type =
                    std::get_if<std::string>(&value.second);
                if (type == nullptr)
                {
                    // if this was our message, timeout will cover it
                    return;
                }
                handleUpdateErrorType(asyncResp, url, *type);
            }
        }
    }
}

// Note that asyncResp can be either a valid pointer or nullptr. If nullptr
// then no asyncResp updates will occur
// Nvidia modified arguments to use constants
inline void monitorForSoftwareAvailable(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const crow::Request& req,
    int timeoutTimeSeconds = fwObjectCreationDefaultTimeout,
    const std::string& imagePath = {})
{
    // Only allow one FW update at a time
    if (fwUpdateInProgress)
    {
        if (asyncResp)
        {
            messages::serviceTemporarilyUnavailable(asyncResp->res, "30");
        }
        return;
    }

    // Request no longer exposes ioService; using shared io_context

    fwAvailableTimer =
        std::make_unique<boost::asio::steady_timer>(getIoContext());

    fwAvailableTimer->expires_after(std::chrono::seconds(timeoutTimeSeconds));

    fwAvailableTimer->async_wait(
        std::bind_front(afterAvailbleTimerAsyncWait, asyncResp, imagePath));

    task::Payload payload(req);
    auto callback = [asyncResp, payload](sdbusplus::message_t& m) mutable {
        BMCWEB_LOG_DEBUG("Match fired");
        softwareInterfaceAdded(asyncResp, m, std::move(payload));
    };

    fwUpdateInProgress = true;

    fwUpdateMatcher = std::make_unique<sdbusplus::bus::match_t>(
        *crow::connections::systemBus,
        "interface='org.freedesktop.DBus.ObjectManager',type='signal',"
        "member='InterfacesAdded',path='/xyz/openbmc_project/software'",
        callback);

    // Nvidia modified bound function to log task messages
    fwUpdateErrorMatcher = std::make_unique<sdbusplus::bus::match_t>(
        *crow::connections::systemBus,
        "interface='org.freedesktop.DBus.ObjectManager',type='signal',"
        "member='InterfacesAdded',"
        "path='/xyz/openbmc_project/logging'",
        preTaskLoggingHandler);
}

inline std::optional<boost::urls::url> parseSimpleUpdateUrl(
    std::string imageURI, std::optional<std::string> transferProtocol,
    crow::Response& res)
{
    if (imageURI.find("://") == std::string::npos)
    {
        if (imageURI.starts_with("/"))
        {
            messages::actionParameterValueTypeError(
                res, imageURI, "ImageURI", "UpdateService.SimpleUpdate");
            return std::nullopt;
        }
        if (!transferProtocol)
        {
            messages::actionParameterValueTypeError(
                res, imageURI, "ImageURI", "UpdateService.SimpleUpdate");
            return std::nullopt;
        }
        // OpenBMC currently only supports HTTPS
        if (*transferProtocol == "HTTPS")
        {
            imageURI = "https://" + imageURI;
        }
        else
        {
            messages::actionParameterNotSupported(res, "TransferProtocol",
                                                  *transferProtocol);
            BMCWEB_LOG_ERROR("Request incorrect protocol parameter: {}",
                             *transferProtocol);
            return std::nullopt;
        }
    }

    boost::system::result<boost::urls::url> url =
        boost::urls::parse_absolute_uri(imageURI);
    if (!url)
    {
        messages::actionParameterValueTypeError(res, imageURI, "ImageURI",
                                                "UpdateService.SimpleUpdate");

        return std::nullopt;
    }
    url->normalize();

    if (url->scheme() == "tftp")
    {
        if (url->encoded_path().size() < 2)
        {
            messages::actionParameterNotSupported(res, "ImageURI",
                                                  url->buffer());
            return std::nullopt;
        }
    }
    else if (url->scheme() == "https")
    {
        // Empty paths default to "/"
        if (url->encoded_path().empty())
        {
            url->set_encoded_path("/");
        }
    }
    else
    {
        messages::actionParameterNotSupported(res, "ImageURI", imageURI);
        return std::nullopt;
    }

    if (url->encoded_path().empty())
    {
        messages::actionParameterValueTypeError(res, imageURI, "ImageURI",
                                                "UpdateService.SimpleUpdate");
        return std::nullopt;
    }

    return *url;
}

inline void doHttpsUpdate(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const boost::urls::url_view_base& url)
{
    messages::actionParameterNotSupported(asyncResp->res, "ImageURI",
                                          url.buffer());
}

inline void handleUpdateServiceSimpleUpdateAction(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::optional<std::string> transferProtocol;
    std::string imageURI;

    BMCWEB_LOG_DEBUG("Enter UpdateService.SimpleUpdate doPost");

    // User can pass in both TransferProtocol and ImageURI parameters or
    // they can pass in just the ImageURI with the transfer protocol
    // embedded within it.
    // 1) TransferProtocol:TFTP ImageURI:1.1.1.1/myfile.bin
    // 2) ImageURI:tftp://1.1.1.1/myfile.bin

    if (!json_util::readJsonAction(              //
            req, asyncResp->res,                 //
            "ImageURI", imageURI,                //
            "TransferProtocol", transferProtocol //
            ))
    {
        BMCWEB_LOG_DEBUG("Missing TransferProtocol or ImageURI parameter");
        return;
    }

    std::optional<boost::urls::url> url =
        parseSimpleUpdateUrl(imageURI, transferProtocol, asyncResp->res);
    if (!url)
    {
        return;
    }
    if (url->scheme() == "https")
    {
        doHttpsUpdate(asyncResp, *url);
    }
    else
    {
        messages::actionParameterNotSupported(asyncResp->res, "ImageURI",
                                              url->buffer());
        return;
    }

    BMCWEB_LOG_DEBUG("Exit UpdateService.SimpleUpdate doPost");
}

inline void uploadImageFile(crow::Response& res, std::string_view body)
{
    std::filesystem::path filepath("/tmp/images/" + bmcweb::getRandomUUID());

    BMCWEB_LOG_DEBUG("Writing file to {}", filepath.string());
    std::ofstream out(filepath, std::ofstream::out | std::ofstream::binary |
                                    std::ofstream::trunc);
    // set the permission of the file to 640
    std::filesystem::perms permission =
        std::filesystem::perms::owner_read | std::filesystem::perms::group_read;
    std::filesystem::permissions(filepath, permission);
    out << body;

    if (out.bad())
    {
        messages::internalError(res);
        cleanUp();
    }
}

// Convert the Request Apply Time to the D-Bus value
inline bool convertApplyTime(crow::Response& res, const std::string& applyTime,
                             std::string& applyTimeNewVal)
{
    if (applyTime == "Immediate")
    {
        applyTimeNewVal =
            "xyz.openbmc_project.Software.ApplyTime.RequestedApplyTimes.Immediate";
    }
    else if (applyTime == "OnReset")
    {
        applyTimeNewVal =
            "xyz.openbmc_project.Software.ApplyTime.RequestedApplyTimes.OnReset";
    }
    else
    {
        BMCWEB_LOG_WARNING(
            "ApplyTime value {} is not in the list of acceptable values",
            applyTime);
        messages::propertyValueNotInList(res, applyTime, "ApplyTime");
        return false;
    }
    return true;
}

inline void setApplyTime(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& applyTime)
{
    std::string applyTimeNewVal;
    if (!convertApplyTime(asyncResp->res, applyTime, applyTimeNewVal))
    {
        return;
    }

    setDbusProperty(
        asyncResp, "ApplyTime", "xyz.openbmc_project.Settings",
        sdbusplus::message::object_path("/xyz/openbmc_project/software/apply_time"),
        "xyz.openbmc_project.Software.ApplyTime", "RequestedApplyTime",
        applyTimeNewVal);
}

struct MultiPartUpdate
{
    std::string uploadData;
    std::optional<std::string> updateFileContentType;
    struct UpdateParameters
    {
        std::optional<std::string> applyTime;
        std::optional<std::vector<std::string>> targets;
        std::optional<bool> forceUpdate;
    } params;
};

inline std::optional<std::string> processUrl(
    boost::system::result<boost::urls::url_view>& url)
{
    if (!url)
    {
        return std::nullopt;
    }
    if (crow::utility::readUrlSegments(*url, "redfish", "v1", "Managers",
                                       BMCWEB_REDFISH_MANAGER_URI_NAME))
    {
        return std::make_optional(std::string(BMCWEB_REDFISH_MANAGER_URI_NAME));
    }
    if constexpr (!BMCWEB_REDFISH_UPDATESERVICE_USE_DBUS)
    {
        return std::nullopt;
    }
    std::string firmwareId;
    if (!crow::utility::readUrlSegments(*url, "redfish", "v1", "UpdateService",
                                        "FirmwareInventory",
                                        std::ref(firmwareId)))
    {
        return std::nullopt;
    }

    return std::make_optional(firmwareId);
}

inline std::optional<std::string> parseFormPartName(
    const boost::beast::http::fields::const_iterator& contentDisposition)
{
    size_t semicolonPos = contentDisposition->value().find(';');
    if (semicolonPos == std::string::npos)
    {
        return std::nullopt;
    }

    for (const auto& param : boost::beast::http::param_list{
             contentDisposition->value().substr(semicolonPos)})
    {
        if (param.first == "name" && !param.second.empty())
        {
            return std::string(param.second);
        }
    }
    return std::nullopt;
}

inline std::optional<MultiPartUpdate::UpdateParameters> processUpdateParameters(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::string_view content)
{
    MultiPartUpdate::UpdateParameters multiRet;
    std::optional<nlohmann::json> jsonContent =
        parseStringAsJson(std::string(content));
    if (!jsonContent)
    {
        // Nvidia code starts here
        messages::unrecognizedRequestBody(asyncResp->res);
        // Nvidia code ends here
        return std::nullopt;
    }
    nlohmann::json::object_t* obj =
        jsonContent->get_ptr<nlohmann::json::object_t*>();
    if (obj == nullptr)
    {
        messages::propertyValueTypeError(asyncResp->res, content,
                                         "UpdateParameters");
        return std::nullopt;
    }

    if (!json_util::readJsonObject(                            //
            *obj, asyncResp->res,                              //
            "@Redfish.OperationApplyTime", multiRet.applyTime, //
            "Targets", multiRet.targets,                       //
            "ForceUpdate", multiRet.forceUpdate                //
            ))
    {
        addUnsupportedActionParametersMessages(asyncResp, *obj);
        return std::nullopt;
    }

    if constexpr (BMCWEB_ENABLE_UNUSED_UPSTREAM_CODE)
    {
        if (multiRet.targets)
        {
            if (multiRet.targets->size() > 1)
            {
                messages::propertyValueFormatError(
                    asyncResp->res, *multiRet.targets, "Targets");
                return std::nullopt;
            }

            for (auto& target : *multiRet.targets)
            {
                boost::system::result<boost::urls::url_view> url =
                    boost::urls::parse_origin_form(target);
                auto res = processUrl(url);
                if (!res.has_value())
                {
                    messages::propertyValueFormatError(asyncResp->res, target,
                                                       "Targets");
                    return std::nullopt;
                }
                target = res.value();
            }
        }
    }

    return multiRet;
}

inline std::optional<MultiPartUpdate> extractMultipartUpdateParameters(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, crow::Request& req)
{
    MultiPartUpdate multiRet;
    for (FormPart& formpart : req.multipart())
    {
        boost::beast::http::fields::const_iterator it =
            formpart.fields.find("Content-Disposition");
        if (it == formpart.fields.end())
        {
            BMCWEB_LOG_ERROR("Couldn't find Content-Disposition");
            return std::nullopt;
        }
        BMCWEB_LOG_INFO("Parsing value {}", it->value());

        auto formFieldNameOpt = parseFormPartName(it);
        if (!formFieldNameOpt.has_value())
        {
            continue;
        }

        const std::string& formFieldName = formFieldNameOpt.value();

        if (formFieldName == "UpdateParameters")
        {
            std::optional<MultiPartUpdate::UpdateParameters> params =
                processUpdateParameters(asyncResp, formpart.content);
            if (!params)
            {
                return std::nullopt;
            }
            multiRet.params = std::move(*params);
        }
        else if (formFieldName == "UpdateFile")
        {
            multiRet.uploadData = std::move(formpart.content);
        }
    }

    if (multiRet.uploadData.empty())
    {
        BMCWEB_LOG_ERROR("Upload data is NULL");
        messages::propertyMissing(asyncResp->res, "UpdateFile");
        return std::nullopt;
    }

    return multiRet;
}

inline void handleStartUpdate(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, task::Payload payload,
    const std::string& objectPath, const boost::system::error_code& ec,
    const sdbusplus::message::object_path& retPath)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("error_code = {}", ec);
        BMCWEB_LOG_ERROR("error msg = {}", ec.message());
        messages::internalError(asyncResp->res);
        // Nvidia code begin
        fwUpdateInProgress = false;
        // Nvidia code end
        return;
    }

    BMCWEB_LOG_INFO("Call to StartUpdate on {} Success, retPath = {}",
                    objectPath, retPath.str);
    createTask(asyncResp, std::move(payload), retPath);
}

inline void startUpdate(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, task::Payload payload,
    const std::shared_ptr<MemoryFileDescriptor>& memfd,
    const std::string& applyTime, bool forceUpdate,
    const std::vector<sdbusplus::message::object_path>& targets)
{
    // PLDM UA is the only service implementing StartUpdate
    const std::string serviceName = "xyz.openbmc_project.PLDM";
    const std::string objectPath = "/xyz/openbmc_project/software/pldm";

    // Nvidia modified function call to support force update
    dbus::utility::async_method_call(
        asyncResp,
        [asyncResp, payload = std::move(payload), memfd,
         objectPath](const boost::system::error_code& ec1,
                     const sdbusplus::message::object_path& retPath) mutable {
            handleStartUpdate(asyncResp, std::move(payload), objectPath, ec1,
                              retPath);
        },
        // Nvidia modified: added forceUpdate and targets parameters
        serviceName, objectPath, updateInterface, "StartUpdate",
        sdbusplus::message::unix_fd(memfd->fd), applyTime, forceUpdate,
        targets);
}

inline void getSwInfo(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      task::Payload payload, const MemoryFileDescriptor& memfd,
                      const std::string& applyTime, const std::string& target,
                      const boost::system::error_code& ec,
                      const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    using SwInfoMap =
        std::unordered_map<std::string,
                           std::pair<sdbusplus::message::object_path, std::string>>;
    SwInfoMap swInfoMap;

    if (ec)
    {
        BMCWEB_LOG_ERROR("error_code = {}", ec);
        BMCWEB_LOG_ERROR("error msg = {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }
    BMCWEB_LOG_DEBUG("Found {} software version paths", subtree.size());

    for (const auto& entry : subtree)
    {
        sdbusplus::message::object_path path(entry.first);
        std::string swId = path.filename();
        swInfoMap.emplace(swId, make_pair(path, entry.second[0].first));
    }

    auto swEntry = swInfoMap.find(target);
    if (swEntry == swInfoMap.end())
    {
        BMCWEB_LOG_WARNING("No valid DBus path for Target URI {}", target);
        messages::propertyValueFormatError(asyncResp->res, target, "Targets");
        return;
    }

    BMCWEB_LOG_DEBUG("Found software version path {} serviceName {}",
                     swEntry->second.first.str, swEntry->second.second);

    // Nvidia modified code to prevent compilation issues in unused code
    (void)memfd;
    startUpdate(asyncResp, std::move(payload), {}, applyTime, true, {});
    // Nvidia modified code end
}

inline void handleBMCUpdate(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, task::Payload payload,
    const MemoryFileDescriptor& memfd, const std::string& applyTime,
    const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& functionalSoftware)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("error_code = {}", ec);
        BMCWEB_LOG_ERROR("error msg = {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }
    if (functionalSoftware.size() != 1)
    {
        BMCWEB_LOG_ERROR("Found {} functional software endpoints",
                         functionalSoftware.size());
        messages::internalError(asyncResp->res);
        return;
    }

    // Nvidia modified code to prevent compilation issues in unused code
    (void)memfd;
    startUpdate(asyncResp, std::move(payload), {}, applyTime, {}, {});
    // Nvidia modified code end
}
// un used upstream code starts here
inline void processUpdateRequest(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    task::Payload&& payload, crow::Request& req,
    const std::string& dbusApplyTime, bool forceUpdate,
    const std::vector<std::string>& uriTargets)
{
    auto memfd = std::make_shared<MemoryFileDescriptor>("update-image");
    if (memfd->fd == -1)
    {
        BMCWEB_LOG_ERROR("Failed to create image memfd");
        messages::internalError(asyncResp->res);
        fwUpdateInProgress = false;
        return;
    }

    // Nvidia code starts here
    bool foundUpdateFile = false;
    for (FormPart& formpart : req.multipart())
    {
        boost::beast::http::fields::const_iterator it =
            formpart.fields.find("Content-Disposition");
        if (it == formpart.fields.end())
        {
            continue;
        }
        auto formFieldNameOpt = parseFormPartName(it);
        if (!formFieldNameOpt || *formFieldNameOpt != "UpdateFile")
        {
            continue;
        }

        std::span<const char> remaining(formpart.content.data(),
                                        formpart.content.size());
        while (!remaining.empty())
        {
            ssize_t written =
                write(memfd->fd, remaining.data(), remaining.size());
            if (written < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                BMCWEB_LOG_ERROR("Failed to write UpdateFile to memfd");
                messages::internalError(asyncResp->res);
                fwUpdateInProgress = false;
                return;
            }
            remaining = remaining.subspan(static_cast<size_t>(written));
        }
        foundUpdateFile = true;
        break;
    }

    if (!foundUpdateFile)
    {
        messages::propertyMissing(asyncResp->res, "UpdateFile");
        fwUpdateInProgress = false;
        return;
    }

    off_t imageSize = lseek(memfd->fd, 0, SEEK_END);
    if (imageSize == 0)
    {
        messages::invalidUpload(asyncResp->res,
                                "/redfish/v1/UpdateService/update-multipart",
                                "Uploaded image file is empty");
        fwUpdateInProgress = false;
        return;
    }
    // Nvidia code ends here

    if (!memfd->rewind())
    {
        messages::internalError(asyncResp->res);
        fwUpdateInProgress = false;
        return;
    }

    if constexpr (BMCWEB_ENABLE_UNUSED_UPSTREAM_CODE)
    {
        // Nvidia modified code to prevent compilation issues in unused code
        std::vector<std::string> targets;
        std::string applyTime;
        // Use a separate payload instance for upstream-only branches to avoid
        // consuming the primary 'payload' rvalue used later below.
        task::Payload payloadForUpstream(req);
        // Nvidia modified code end
        if (!targets.empty() && targets[0] == BMCWEB_REDFISH_MANAGER_URI_NAME)
        {
            dbus::utility::getAssociationEndPoints(
                "/xyz/openbmc_project/software/bmc/updateable",
                [asyncResp, payload = std::move(payloadForUpstream),
                 memfd = std::move(memfd), applyTime](
                    const boost::system::error_code& ec,
                    const dbus::utility::MapperEndPoints& objectPaths) mutable {
                    handleBMCUpdate(asyncResp, std::move(payload), *memfd,
                                    applyTime, ec, objectPaths);
                });
        }
        else
        {
            constexpr std::array<std::string_view, 1> interfaces = {
                "xyz.openbmc_project.Software.Version"};
            dbus::utility::getSubTree(
                "/xyz/openbmc_project/software", 1, interfaces,
                [asyncResp, payload = std::move(payloadForUpstream),
                 memfd = std::move(memfd), applyTime,
                 targets](const boost::system::error_code& ec,
                          const dbus::utility::MapperGetSubTreeResponse&
                              subtree) mutable {
                    getSwInfo(asyncResp, std::move(payload), *memfd, applyTime,
                              targets[0], ec, subtree);
                });
        }
    }

    // Nvidia code starts here
    if (!uriTargets.empty())
    {
        dbus::utility::getSubTreePaths(
            "/xyz/openbmc_project/software", 0,
            std::array<std::string_view, 1>{
                "xyz.openbmc_project.Software.Version"},
            [asyncResp, payload = std::move(payload), memfd, dbusApplyTime,
             forceUpdate,
             uriTargets](const boost::system::error_code& ec,
                         const std::vector<std::string>& swInvPaths) mutable {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("Failed to get software inventory: {}",
                                     ec);
                    messages::internalError(asyncResp->res);
                    fwUpdateInProgress = false;
                    return;
                }

                std::vector<sdbusplus::message::object_path> validTargets;
                std::vector<std::string> updateableFw;
                updateableFw.reserve(swInvPaths.size());
                for (const auto& path : swInvPaths)
                {
                    std::string fwId = std::filesystem::path(path).filename();
                    updateableFw.push_back(fwId);
                }

                if (areTargetsInvalidOrUnupdatable(uriTargets, updateableFw,
                                                   swInvPaths, validTargets))
                {
                    BMCWEB_LOG_ERROR("Invalid targets provided");
                    messages::invalidObject(asyncResp->res,
                                            boost::urls::url_view("Targets"));
                    fwUpdateInProgress = false;
                    return;
                }

                startUpdate(asyncResp, std::move(payload), memfd, dbusApplyTime,
                            forceUpdate, validTargets);
            });
    }
    else
    {
        std::vector<sdbusplus::message::object_path> emptyTargets{};
        startUpdate(asyncResp, std::move(payload), memfd, dbusApplyTime,
                    forceUpdate, emptyTargets);
    }
    // Nvidia code ends here
}

inline bool parseContentDisposition(const boost::beast::http::fields& fields,
                                    std::string_view formFieldName)
{
    auto dispositionIt = fields.find("Content-Disposition");
    if (dispositionIt == fields.end())
    {
        return false;
    }

    auto formFieldNameOpt = parseFormPartName(dispositionIt);
    if (!formFieldNameOpt.has_value())
    {
        return false;
    }
    if (formFieldNameOpt.value() != formFieldName)
    {
        return false;
    }
    return true;
}

inline bool parseContentType(const boost::beast::http::fields& fields)
{
    auto dispositionIt = fields.find("Content-Type");
    if (dispositionIt == fields.end())
    {
        return false;
    }

    if (!isJsonContentType(dispositionIt->value()))
    {
        return false;
    }
    return true;
}

// Upstream unused code
inline void doHTTPUpdate(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         crow::Request& req)
{
    if constexpr (BMCWEB_REDFISH_UPDATESERVICE_USE_DBUS)
    {
        task::Payload payload(req);
        // HTTP push only supports BMC updates (with ApplyTime as immediate) for
        // backwards compatibility. Specific component updates will be handled
        // through Multipart form HTTP push.
        std::vector<std::string> targets;
        targets.emplace_back(BMCWEB_REDFISH_MANAGER_URI_NAME);

        // Nvidia modified code to prevent compilation issues in unused code
        processUpdateRequest(
            asyncResp, std::move(payload), req,
            "xyz.openbmc_project.Software.ApplyTime.RequestedApplyTimes.Immediate",
            {}, {});
        // Nvidia modified code end
    }
    else
    {
        // Nvidia modified code to prevent compilation issues in unused code
        // Setup callback for when new software detected
        monitorForSoftwareAvailable(asyncResp, req, {},
                                    "/redfish/v1/UpdateService");
        // Nvidia modified code end

        uploadImageFile(asyncResp->res, req.body());
    }
}

inline void handleUpdateServicePost(
    App& app, crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    std::string_view contentType = req.getHeaderValue("Content-Type");

    BMCWEB_LOG_DEBUG("doPost: contentType={}", contentType);

    // Make sure that content type is application/octet-stream
    if (bmcweb::asciiIEquals(contentType, "application/octet-stream"))
    {
        doHTTPUpdate(asyncResp, req);
    }
    else
    {
        BMCWEB_LOG_DEBUG("Bad content type specified:{}", contentType);
        asyncResp->res.result(boost::beast::http::status::bad_request);
        messages::addMessageToErrorJson(
            asyncResp->res.jsonValue,
            messages::headerValueInvalid(contentType, "Content-Type",
                                         "application/octet-stream"));
    }
}

inline void handleUpdateServiceGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] =
        "#UpdateService.v1_11_1.UpdateService";
    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/UpdateService";
    asyncResp->res.jsonValue["Id"] = "UpdateService";
    asyncResp->res.jsonValue["Description"] = "Service for Software Update";
    asyncResp->res.jsonValue["Name"] = "Update Service";

    if constexpr (BMCWEB_ENABLE_UNUSED_UPSTREAM_CODE)
    {
        asyncResp->res.jsonValue["HttpPushUri"] =
            "/redfish/v1/UpdateService/update";
        asyncResp->res.jsonValue["HttpPushUriOptions"]["HttpPushUriApplyTime"]
                                ["ApplyTime"] =
            update_service::ApplyTime::Immediate;
    }
    asyncResp->res.jsonValue["MultipartHttpPushUri"] =
        "/redfish/v1/UpdateService/update-multipart";

    nlohmann::json::array_t supportedApplyTimes;
    supportedApplyTimes.emplace_back("OnReset");
    supportedApplyTimes.emplace_back("Immediate");
    asyncResp->res
        .jsonValue["MultipartHttpPushUri@Redfish.OperationApplyTimeSupport"]
                  ["@odata.type"] =
        "#Settings.v1_3_3.OperationApplyTimeSupport";
    asyncResp->res
        .jsonValue["MultipartHttpPushUri@Redfish.OperationApplyTimeSupport"]
                  ["SupportedValues"] = std::move(supportedApplyTimes);

    // UpdateService cannot be disabled
    asyncResp->res.jsonValue["ServiceEnabled"] = true;
    asyncResp->res.jsonValue["FirmwareInventory"]["@odata.id"] =
        "/redfish/v1/UpdateService/FirmwareInventory";
    // Get the MaxImageSizeBytes
    // Nvidia code starts here
    asyncResp->res.jsonValue["MaxImageSizeBytes"] = firmwareImageLimitBytes;
    // Nvidia code ends here

    extendUpdateServiceGet(asyncResp);

    if constexpr (BMCWEB_REDFISH_ALLOW_SIMPLE_UPDATE)
    {
        // Update Actions object.
        nlohmann::json& updateSvcSimpleUpdate =
            asyncResp->res.jsonValue["Actions"]["#UpdateService.SimpleUpdate"];
        updateSvcSimpleUpdate["target"] =
            "/redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate";

        nlohmann::json::array_t allowed;
        allowed.emplace_back(update_service::TransferProtocolType::HTTPS);
        updateSvcSimpleUpdate["TransferProtocol@Redfish.AllowableValues"] =
            std::move(allowed);
    }
}

inline void handleUpdateServiceFirmwareInventoryCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] =
        "#SoftwareInventoryCollection.SoftwareInventoryCollection";
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/UpdateService/FirmwareInventory";
    asyncResp->res.jsonValue["Name"] = "Software Inventory Collection";
    const std::array<const std::string_view, 3> iface = {
        "xyz.openbmc_project.Software.Version",
        "xyz.openbmc_project.State.Decorator.Health",
        "xyz.openbmc_project.State.Decorator.OperationalStatus"};

    redfish::collection_util::getCollectionMembers(
        asyncResp,
        boost::urls::url("/redfish/v1/UpdateService/FirmwareInventory"), iface,
        "/xyz/openbmc_project/software");
}

inline void addRelatedItem(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const boost::urls::url& url)
{
    nlohmann::json& relatedItem = asyncResp->res.jsonValue["RelatedItem"];
    nlohmann::json::object_t item;
    item["@odata.id"] = url;
    relatedItem.emplace_back(std::move(item));
    asyncResp->res.jsonValue["RelatedItem@odata.count"] = relatedItem.size();
}

/* Fill related item links (i.e. bmc, bios) in for inventory */
inline static void getRelatedItems(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& swId, const std::string& purpose)
{
    // Nvidia getRelatedItems start
    if (purpose == sw_util::otherPurpose || purpose == sw_util::bmcPurpose)
    {
        getRelatedItemsOthers(asyncResp, swId);
    }
    // Nvidia getRelatedItems end
    else if (purpose == sw_util::biosPurpose)
    {
        auto url = boost::urls::format("/redfish/v1/Systems/{}/Bios",
                                       BMCWEB_REDFISH_SYSTEM_URI_NAME);

        addRelatedItem(asyncResp, url);
    }
    else
    {
        BMCWEB_LOG_DEBUG("Unknown software purpose {}", purpose);
    }
}

inline void getSoftwareVersionCallback(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& swId, const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& propertiesList)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("D-Bus error {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }
    const std::string* swInvPurpose = nullptr;
    const std::string* version = nullptr;
    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), propertiesList, "Purpose",
        swInvPurpose, "Version", version);
    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }
    if (swInvPurpose == nullptr)
    {
        BMCWEB_LOG_DEBUG("Can't find property \"Purpose\"!");
        messages::internalError(asyncResp->res);
        return;
    }
    BMCWEB_LOG_DEBUG("swInvPurpose = {}", *swInvPurpose);
    if (version == nullptr)
    {
        BMCWEB_LOG_DEBUG("Can't find property \"Version\"!");
        messages::internalError(asyncResp->res);
        return;
    }
    asyncResp->res.jsonValue["Version"] = *version;
    asyncResp->res.jsonValue["Id"] = swId;
    // swInvPurpose is of format:
    // xyz.openbmc_project.Software.Version.VersionPurpose.ABC
    // Translate this to "ABC image"
    size_t endDesc = swInvPurpose->rfind('.');
    if (endDesc == std::string::npos)
    {
        messages::internalError(asyncResp->res);
        return;
    }
    endDesc++;
    if (endDesc >= swInvPurpose->size())
    {
        messages::internalError(asyncResp->res);
        return;
    }
    std::string formatDesc = swInvPurpose->substr(endDesc);
    asyncResp->res.jsonValue["Description"] = formatDesc + " image";
    getRelatedItems(asyncResp, swId, *swInvPurpose);
}

inline void getSoftwareVersion(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path,
    const std::string& swId)
{
    dbus::utility::getAllProperties(
        service, path, "xyz.openbmc_project.Software.Version",
        std::bind_front(getSoftwareVersionCallback, asyncResp, swId));
}

inline void handleUpdateServiceFirmwareInventoryGetCallback(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<std::string>& swId,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    BMCWEB_LOG_DEBUG("doGet callback...");
    if (ec)
    {
        BMCWEB_LOG_ERROR("D-Bus error {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }
    // Ensure we find our input swId, otherwise return an error
    bool foundVersionObject = false;
    bool foundStatusObject = false;
    [[maybe_unused]] bool found = false;
    for (const std::pair<
             std::string,
             std::vector<std::pair<std::string, std::vector<std::string>>>>&
             obj : subtree)
    {
        sdbusplus::message::object_path path(obj.first);
        std::string id = path.filename();
        if (id.empty())
        {
            BMCWEB_LOG_DEBUG("Failed to find software id in {}", obj.first);
            continue;
        }
        if (id != *swId)
        {
            continue;
        }
        if (obj.second.empty())
        {
            continue;
        }
        if constexpr (BMCWEB_ENABLE_UNUSED_UPSTREAM_CODE)
        {
            found = true;
            sw_util::getSwStatus(asyncResp, swId, obj.second[0].first);
            sw_util::getSwMinimumVersion(asyncResp, swId, obj.second[0].first);
            getSoftwareVersion(asyncResp, obj.second[0].first, obj.first,
                               *swId);
        }
        // Nvidia FirmwareInventoryGet start
        foundVersionObject = true;

        std::string settingService{};
        std::string versionService{};
        std::string statusService{};
        for (const auto& [service, interface] : obj.second)
        {
            if (std::ranges::find(interface,
                                  "xyz.openbmc_project.Software.Settings") !=
                interface.end())
            {
                settingService = service;
            }
            if (std::ranges::find(interface,
                                  "xyz.openbmc_project.Software.Version") !=
                interface.end())
            {
                versionService = service;
            }
            if (std::ranges::find(
                    interface, "xyz.openbmc_project.State.Decorator.Health") !=
                    interface.end() and
                std::ranges::find(
                    interface,
                    "xyz.openbmc_project.State.Decorator.OperationalStatus") !=
                    interface.end())
            {
                statusService = service;
                foundStatusObject = true;
            }
        }

        if (versionService.empty() and statusService.empty())
        {
            BMCWEB_LOG_ERROR(
                "Firmware Inventory: Software.Version interface is missing for swId: {}",
                *swId);
            messages::internalError(asyncResp->res);
            return;
        }

        fw_util::getFwStatus(asyncResp, swId, obj.second[0].first);
        // The settingService is used for populating
        // WriteProtected property. This property is optional
        // and not implemented on all devices.
        if (!settingService.empty())
        {
            fw_util::getFwWriteProtectedStatus(asyncResp, obj.first,
                                               settingService);
        }
        asyncResp->res.jsonValue["Id"] = *swId;

        if (!versionService.empty())
        {
            dbus::utility::getAllProperties(
                versionService, obj.first, "",
                [asyncResp,
                 swId](const boost::system::error_code& errorCode,
                       const dbus::utility::DBusPropertiesMap& propertiesList) {
                    if (errorCode)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    auto it = std::ranges::find_if(
                        propertiesList, [](const auto& property) {
                            return property.first == "Purpose";
                        });
                    if (it == propertiesList.end())
                    {
                        BMCWEB_LOG_ERROR("Can't find property \"Version\"!");
                        messages::propertyMissing(asyncResp->res, "Purpose");
                        return;
                    }
                    const std::string* swInvPurpose =
                        std::get_if<std::string>(&it->second);
                    if (swInvPurpose == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "wrong types for property\"Purpose\"!");
                        messages::propertyValueTypeError(asyncResp->res, "",
                                                         "Purpose");
                        return;
                    }

                    BMCWEB_LOG_DEBUG("swInvPurpose = {}", *swInvPurpose);
                    it = std::ranges::find_if(
                        propertiesList, [](const auto& property) {
                            return property.first == "Version";
                        });
                    if (it == propertiesList.end())
                    {
                        BMCWEB_LOG_ERROR("Can't find property \"Version\"!");
                        messages::propertyMissing(asyncResp->res, "Version");
                        return;
                    }

                    BMCWEB_LOG_DEBUG("Version found!");

                    const std::string* version =
                        std::get_if<std::string>(&it->second);

                    if (version == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Can't find property \"Version\"!");

                        messages::propertyValueTypeError(asyncResp->res, "",
                                                         "Version");
                        return;
                    }

                    it = std::ranges::find_if(
                        propertiesList, [](const auto& property) {
                            return property.first == "Manufacturer";
                        });
                    if (it != propertiesList.end())
                    {
                        const std::string* manufacturer =
                            std::get_if<std::string>(&it->second);

                        if (manufacturer == nullptr)
                        {
                            BMCWEB_LOG_ERROR(
                                "Can't find property \"Manufacturer\"!");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res.jsonValue["Manufacturer"] =
                            *manufacturer;
                    }

                    it = std::ranges::find_if(
                        propertiesList, [](const auto& property) {
                            return property.first == "SoftwareId";
                        });
                    if (it != propertiesList.end())
                    {
                        const std::string* softwareId =
                            std::get_if<std::string>(&it->second);

                        if (softwareId == nullptr)
                        {
                            BMCWEB_LOG_ERROR(
                                "Can't find property \"softwareId\"!");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        if (!softwareId->empty())
                        {
                            asyncResp->res.jsonValue["SoftwareId"] =
                                *softwareId;
                        }
                    }

                    asyncResp->res.jsonValue["Version"] = *version;

                    // swInvPurpose is of format:
                    // xyz.openbmc_project.Software.Version.VersionPurpose.ABC
                    // Translate this to "ABC image"
                    size_t endDesc = swInvPurpose->rfind('.');
                    if (endDesc == std::string::npos)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    endDesc++;
                    if (endDesc >= swInvPurpose->size())
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    std::string formatDesc = swInvPurpose->substr(endDesc);
                    it = std::ranges::find_if(
                        propertiesList, [](const auto& property) {
                            return property.first == "Description";
                        });
                    asyncResp->res.jsonValue["Description"] =
                        formatDesc + " image";

                    if (it != propertiesList.end())
                    {
                        const std::string* description =
                            std::get_if<std::string>(&it->second);
                        if (description != nullptr && !description->empty())
                        {
                            asyncResp->res.jsonValue["Description"] =
                                *description;
                        }
                    }
                    getRelatedItems(asyncResp, *swId, *swInvPurpose);

                    it = std::ranges::find_if(
                        propertiesList, [](const auto& property) {
                            return property.first == "PrettyName";
                        });
                    if (it != propertiesList.end())
                    {
                        const std::string* foundName =
                            std::get_if<std::string>(&it->second);
                        if (foundName != nullptr && !foundName->empty())
                        {
                            asyncResp->res.jsonValue["Name"] = *foundName;
                        }
                    }
                });
        }

        asyncResp->res.jsonValue["Status"]["Health"] = "OK";
        if constexpr (!BMCWEB_DISABLE_HEALTH_ROLLUP)
        {
            asyncResp->res.jsonValue["Status"]["HealthRollup"] = "OK";
        }
        if constexpr (!BMCWEB_DISABLE_CONDITIONS_ARRAY)
        {
            asyncResp->res.jsonValue["Status"]["Conditions"] =
                nlohmann::json::array();
        }

        if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
        {
            redfish::fw_util::getFWSlotInformation(asyncResp, obj.first);
        }
        if (!statusService.empty())
        {
            fw_util::getFwRecoveryStatus(asyncResp, swId, statusService);
        }
        else
        {
            asyncResp->res.jsonValue["Status"]["Health"] = "OK";
        }
        // Nvidia FirmwareInventoryGet end
    }

    if (!foundVersionObject and !foundStatusObject)
    {
        BMCWEB_LOG_WARNING("Input swID {} not found!", *swId);
        messages::resourceMissingAtURI(
            asyncResp->res,
            boost::urls::format(
                "/redfish/v1/UpdateService/FirmwareInventory/{}", *swId));
        return;
    }
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/UpdateService/FirmwareInventory/{}", *swId);

    // Nvidia code start
    if (foundVersionObject)
    {
        asyncResp->res.jsonValue["Updateable"] = false;
        fw_util::getFwUpdateableStatus(asyncResp, swId);
    }
    // Nvidia code end

    asyncResp->res.jsonValue["@odata.type"] =
        "#SoftwareInventory.v1_4_0.SoftwareInventory";
    asyncResp->res.jsonValue["Name"] = "Software Inventory";
    if constexpr (BMCWEB_ENABLE_UNUSED_UPSTREAM_CODE)
    {
        asyncResp->res.jsonValue["Status"]["HealthRollup"] =
            resource::Health::OK;
        asyncResp->res.jsonValue["Updateable"] = false;
        sw_util::getSwUpdatableStatus(asyncResp, swId);
    }

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        updateOemActionComputeDigest(asyncResp, *swId);
    }
}

inline void handleUpdateServiceFirmwareInventoryGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& param)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    std::shared_ptr<std::string> swId = std::make_shared<std::string>(param);

    // Nvidia FirmwareInventory Get start
    constexpr std::array<std::string_view, 4> interfaces = {
        "xyz.openbmc_project.Software.Version",
        "xyz.openbmc_project.Software.Settings",
        "xyz.openbmc_project.State.Decorator.Health",
        "xyz.openbmc_project.State.Decorator.OperationalStatus"};
    // Nvidia FirmwareInventory Get end

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/software/", 0, interfaces,
        std::bind_front(handleUpdateServiceFirmwareInventoryGetCallback,
                        asyncResp, swId));
}

inline void requestRoutesUpdateService(App& app)
{
    if constexpr (BMCWEB_REDFISH_ALLOW_SIMPLE_UPDATE)
    {
        BMCWEB_ROUTE(
            app,
            "/redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate/")
            .privileges(redfish::privileges::postUpdateService)
            .methods(boost::beast::http::verb::post)(std::bind_front(
                handleUpdateServiceSimpleUpdateAction, std::ref(app)));
    }
    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/FirmwareInventory/<str>/")
        .privileges(redfish::privileges::getSoftwareInventory)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleUpdateServiceFirmwareInventoryGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/")
        .privileges(redfish::privileges::getUpdateService)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleUpdateServiceGet, std::ref(app)));

    if constexpr (BMCWEB_ENABLE_UNUSED_UPSTREAM_CODE)
    {
        BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/update/")
            .privileges(redfish::privileges::postUpdateService)
            .methods(boost::beast::http::verb::post)(
                std::bind_front(handleUpdateServicePost, std::ref(app)));
    }
    /* Nvidia removed
    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/update-multipart/")
        .privileges(redfish::privileges::postUpdateService)
        .streamInput()
        .methods(boost::beast::http::verb::post)(
            handleUpdateServiceMultipartUpdatePostHeaders);
    */

    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/FirmwareInventory/")
        .privileges(redfish::privileges::getSoftwareInventoryCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleUpdateServiceFirmwareInventoryCollectionGet, std::ref(app)));
}

} // namespace redfish
