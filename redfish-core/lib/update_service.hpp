/*
// Copyright (c) 2018 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
#pragma once

#include "bmcweb_config.h"

#include "background_copy.hpp"
#include "commit_image.hpp"
#include "debug_token/erase_policy.hpp"
#include "persistentstorage_util.hpp"

#include <http_client.hpp>
#include <http_connection.hpp>
#ifdef BMCWEB_ENABLE_REDFISH_AGGREGATION
#include "redfish_aggregator.hpp"
#endif

#include "app.hpp"
#include "component_integrity.hpp"
#include "dbus_utility.hpp"
#include "multipart_parser.hpp"
#include "ossl_random.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "task.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/sw_utils.hpp"

#include <sys/mman.h>

#include <boost/algorithm/string.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <update_messages.hpp>
#include <utils/conditions_utils.hpp>
#include <utils/dbus_log_utils.hpp>
#include <utils/fw_utils.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace redfish
{

// Match signals added on software path
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::unique_ptr<sdbusplus::bus::match_t> fwUpdateMatcher;
// Only allow one update at a time
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static bool fwUpdateInProgress = false;
// Timer for software available
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::unique_ptr<boost::asio::steady_timer> fwAvailableTimer;
// match for logging
constexpr auto fwObjectCreationDefaultTimeout = 40;
static std::unique_ptr<sdbusplus::bus::match::match> loggingMatch = nullptr;

#define SUPPORTED_RETIMERS 8
/* holds compute digest operation state to allow one operation at a time */
static bool computeDigestInProgress = false;
const std::string hashComputeInterface = "com.Nvidia.ComputeHash";
constexpr auto retimerHashMaxTimeSec =
    180; // 2 mins for 2 attempts and 1 addional min as buffer
const std::string firmwarePrefix =
    "redfish/v1/UpdateService/FirmwareInventory/";

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
}

inline void activateImage(const std::string& objPath,
                          const std::string& service)
{
    BMCWEB_LOG_DEBUG("Activate image for {} {}", objPath, service);
    sdbusplus::asio::setProperty(
        *crow::connections::systemBus, service, objPath,
        "xyz.openbmc_project.Software.Activation", "RequestedActivation",
        "xyz.openbmc_project.Software.Activation.RequestedActivations.Active",
        [](const boost::system::error_code& ec) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("error_code = {}", ec);
            BMCWEB_LOG_DEBUG("error msg = {}", ec.message());
        }
    });
}

static void handleLogMatchCallback(sdbusplus::message_t& m,
                                   nlohmann::json& messages)
{
    std::vector<std::pair<std::string, dbus::utility::DBusPropertiesMap>>
        interfacesProperties;
    sdbusplus::message::object_path objPath;
    m.read(objPath, interfacesProperties);
    for (auto interface : interfacesProperties)
    {
        if (interface.first == "xyz.openbmc_project.Logging.Entry")
        {
            std::string rfMessage = "";
            std::string resolution = "";
            std::string messageNamespace = "";
            std::vector<std::string> rfArgs;
            const std::vector<std::string>* vData = nullptr;
            for (auto& propertyMap : interface.second)
            {
                if (propertyMap.first == "AdditionalData")
                {
                    vData = std::get_if<std::vector<std::string>>(
                        &propertyMap.second);

                    for (auto& kv : *vData)
                    {
                        std::vector<std::string> fields;
                        boost::split(fields, kv, boost::is_any_of("="));
                        if (fields[0] == "REDFISH_MESSAGE_ID")
                        {
                            rfMessage = fields[1];
                        }
                        else if (fields[0] == "REDFISH_MESSAGE_ARGS")
                        {
                            boost::split(rfArgs, fields[1],
                                         boost::is_any_of(","));
                        }
                        else if (fields[0] == "namespace")
                        {
                            messageNamespace = fields[1];
                        }
                    }
                }
                else if (propertyMap.first == "Resolution")
                {
                    const std::string* value =
                        std::get_if<std::string>(&propertyMap.second);
                    if (value != nullptr)
                    {
                        resolution = *value;
                    }
                }
            }
            /* we need to have found the id, data, this image needs to
               correspond to the image we are working with right now and the
               message should be update related */
            if (vData == nullptr || messageNamespace != "FWUpdate")
            {
                // something is invalid
                BMCWEB_LOG_DEBUG("Got invalid log message");
            }
            else
            {
                auto message = redfish::messages::getUpdateMessage(rfMessage,
                                                                   rfArgs);
                if (message.find("Message") != message.end())
                {
                    if (resolution != "")
                    {
                        message["Resolution"] = resolution;
                    }
                    messages.emplace_back(message);
                }
                else
                {
                    BMCWEB_LOG_ERROR("Unknown message ID: {}", rfMessage);
                }
            }
        }
    }
}

static void loggingMatchCallback(const std::shared_ptr<task::TaskData>& task,
                                 sdbusplus::message_t& m)
{
    if (task == nullptr)
    {
        return;
    }
    handleLogMatchCallback(m, task->messages);
}

static nlohmann::json preTaskMessages = {};
static void preTaskLoggingHandler(sdbusplus::message_t& m)
{
    handleLogMatchCallback(m, preTaskMessages);
}

inline bool handleCreateTask(const boost::system::error_code& ec2,
                             sdbusplus::message_t& msg,
                             const std::shared_ptr<task::TaskData>& taskData)
{
    if (ec2)
    {
        return task::completed;
    }

    std::string iface;
    boost::container::flat_map<std::string, dbus::utility::DbusVariantType>
        values;

    std::string index = std::to_string(taskData->index);
    msg.read(iface, values);

    if (iface == "xyz.openbmc_project.Software.Activation")
    {
        auto findActivation = values.find("Activation");
        if (findActivation == values.end())
        {
            return !task::completed;
        }
        std::string* state =
            std::get_if<std::string>(&(findActivation->second));
        if (state == nullptr)
        {
            taskData->messages.emplace_back(messages::internalError());
            fwUpdateInProgress = false;
            return task::completed;
        }

        if (boost::ends_with(*state, "Invalid") ||
            boost::ends_with(*state, "Failed"))
        {
            taskData->state = "Exception";
            taskData->messages.emplace_back(messages::taskAborted(index));
            fwUpdateInProgress = false;
            return task::completed;
        }

        if (boost::ends_with(*state, "Activating"))
        {
            // set firmware inventory inprogress
            // flag to true during activation.
            // this will ensure no furthur
            // updates allowed during this time
            // from redfish
            fwUpdateInProgress = true;
            return !task::completed;
        }

        if (boost::ends_with(*state, "Active"))
        {
            taskData->state = "Completed";
            taskData->messages.emplace_back(messages::taskCompletedOK(index));
            fwUpdateInProgress = false;
            return task::completed;
        }
    }
    else if (iface == "xyz.openbmc_project.Software.ActivationProgress")
    {
        auto findProgress = values.find("Progress");
        if (findProgress == values.end())
        {
            return !task::completed;
        }
        uint8_t* progress = std::get_if<uint8_t>(&(findProgress->second));

        if (progress == nullptr)
        {
            taskData->messages.emplace_back(messages::internalError());
            return task::completed;
        }
        taskData->percentComplete = static_cast<int>(*progress);

        taskData->messages.emplace_back(
            messages::taskProgressChanged(index, *progress));

        // if we're getting status updates it's
        // still alive, update timer
        taskData->extendTimer(std::chrono::minutes(updateServiceTaskTimeout));
    }

    // as firmware update often results in a
    // reboot, the task  may never "complete"
    // unless it is an error

    return !task::completed;
}

/**
 * @brief Retrieve the task message in JSON format for a given task state and
 * index.
 *
 * This function overrides the base function to handle firmware update state
 * management. It is designed to manage the "Aborted" state and reset the global
 * fwUpdateInProgress flag to false.
 *
 * @param state A string representing the task state
 * @param index The index to identify the specific task message
 *
 * @return nlohmann::json The task message corresponding to the given state and
 * index
 */
inline nlohmann::json getTaskMessage(const std::string_view state, size_t index)
{
    if (state == "Aborted")
    {
        fwUpdateInProgress = false;
    }

    return redfish::task::getMessage(state, index);
}

inline void createTask(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       task::Payload&& payload,
                       const sdbusplus::message::object_path& objPath)
{
    std::shared_ptr<task::TaskData> task = task::TaskData::createTask(
        std::bind_front(handleCreateTask),
        "type='signal',interface='org.freedesktop.DBus.Properties',"
        "member='PropertiesChanged',path='" +
            objPath.str + "'",
        std::bind_front(getTaskMessage));

    task->startTimer(std::chrono::minutes(updateServiceTaskTimeout));
    task->populateResp(asyncResp->res);
    task->payload.emplace(std::move(payload));
    loggingMatch = std::make_unique<sdbusplus::bus::match::match>(
        *crow::connections::systemBus,
        "interface='org.freedesktop.DBus.ObjectManager',type='signal',"
        "member='InterfacesAdded',"
        "path='/xyz/openbmc_project/logging'",
        [task](sdbusplus::message_t& msgLog) {
        loggingMatchCallback(task, msgLog);
    });
    if (preTaskMessages.size() > 0)
    {
        task->messages.insert(task->messages.end(), preTaskMessages.begin(),
                              preTaskMessages.end());
    }
    preTaskMessages = {};
}

// Note that asyncResp can be either a valid pointer or nullptr. If nullptr
// then no asyncResp updates will occur
static void
    softwareInterfaceAdded(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
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
                    BMCWEB_LOG_ERROR("error_code = {}", ec);
                    BMCWEB_LOG_ERROR("error msg = {}", ec.message());
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
                    BMCWEB_LOG_ERROR("Invalid Object Size {}", objInfo.size());
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
                sdbusplus::message::object_path objectPath(objPath.str);
                std::string swID = objectPath.filename();
                if (swID.empty())
                {
                    BMCWEB_LOG_ERROR("Software Id is empty");
                    messages::internalError(asyncResp->res);
                    return;
                }

                if (asyncResp)
                {
                    createTask(asyncResp, std::move(payload), objPath);
                }
                activateImage(objPath.str, objInfo[0].first);
            });
            break;
        }
    }
}

inline void afterAvailbleTimerAsyncWait(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& imagePath, const boost::system::error_code& ec)
{
    fwUpdateMatcher = nullptr;
    if (ec == boost::asio::error::operation_aborted)
    {
        // expected, we were canceled before the timer completed.
        return;
    }
    fwUpdateInProgress = false;
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
    // remove update package to allow next update
    if (!imagePath.empty())
    {
        std::filesystem::remove(imagePath);
    }
}

// Note that asyncResp can be either a valid pointer or nullptr. If nullptr
// then no asyncResp updates will occur
static void monitorForSoftwareAvailable(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const crow::Request& req,
    int timeoutTimeSeconds = fwObjectCreationDefaultTimeout,
    const std::string& imagePath = {})
{
    if (req.ioService == nullptr)
    {
        messages::internalError(asyncResp->res);
        return;
    }
    fwAvailableTimer =
        std::make_unique<boost::asio::steady_timer>(*req.ioService);

    fwAvailableTimer->expires_after(std::chrono::seconds(timeoutTimeSeconds));

    fwAvailableTimer->async_wait(
        std::bind_front(afterAvailbleTimerAsyncWait, asyncResp, imagePath));

    task::Payload payload(req);
    auto callback = [asyncResp, payload](sdbusplus::message_t& m) mutable {
        BMCWEB_LOG_DEBUG("Match fired");
        softwareInterfaceAdded(asyncResp, m, std::move(payload));
    };

    fwUpdateInProgress = true;

    fwUpdateMatcher = std::make_unique<sdbusplus::bus::match::match>(
        *crow::connections::systemBus,
        "interface='org.freedesktop.DBus.ObjectManager',type='signal',"
        "member='InterfacesAdded',path='/'",
        callback);

    loggingMatch = std::make_unique<sdbusplus::bus::match::match>(
        *crow::connections::systemBus,
        "interface='org.freedesktop.DBus.ObjectManager',type='signal',"
        "member='InterfacesAdded',"
        "path='/xyz/openbmc_project/logging'",
        preTaskLoggingHandler);
}

struct TftpUrl
{
    std::string fwFile;
    std::string tftpServer;
};

inline std::optional<boost::urls::url>
    parseSimpleUpdateUrl(std::string imageURI,
                         std::optional<std::string> transferProtocol,
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
        // OpenBMC currently only supports TFTP or HTTPS
        if (*transferProtocol == "TFTP")
        {
            imageURI = "tftp://" + imageURI;
        }
        else if (*transferProtocol == "HTTPS")
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

struct SimpleUpdateParams
{
    std::string remoteServerIP;
    std::string fwImagePath;
    std::string transferProtocol;
    bool forceUpdate;
    std::optional<std::string> username;
};

inline void
    downloadViaSCP(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::shared_ptr<const SimpleUpdateParams>& params,
                   const std::string& targetPath)
{
    BMCWEB_LOG_DEBUG("Downloading from {}:{} to {} using {} protocol...",
                     params->remoteServerIP, params->fwImagePath, targetPath,
                     params->transferProtocol);
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
        if (ec)
        {
            messages::internalError(asyncResp->res);
            BMCWEB_LOG_ERROR("error_code = {}, error msg = {}", ec,
                             ec.message());
        }
        else
        {
            BMCWEB_LOG_DEBUG("Call to DownloadViaSCP Success");
        }
    },
        "xyz.openbmc_project.Software.Download",
        "/xyz/openbmc_project/software", "xyz.openbmc_project.Common.SCP",
        "DownloadViaSCP", params->remoteServerIP, (params->username).value(),
        params->fwImagePath, targetPath);
}

inline void
    downloadViaHTTP(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                    const std::shared_ptr<const SimpleUpdateParams>& params,
                    const std::string& targetPath)
{
    BMCWEB_LOG_DEBUG("Downloading from {}:{} to {} using {} protocol...",
                     params->remoteServerIP, params->fwImagePath, targetPath,
                     params->transferProtocol);
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
        if (ec)
        {
            messages::internalError(asyncResp->res);
            BMCWEB_LOG_ERROR("error_code = {}, error msg = {}", ec,
                             ec.message());
        }
        else
        {
            BMCWEB_LOG_DEBUG("Call to DownloadViaHTTP Success");
        }
    },
        "xyz.openbmc_project.Software.Download",
        "/xyz/openbmc_project/software", "xyz.openbmc_project.Common.HTTP",
        "DownloadViaHTTP", params->remoteServerIP,
        (params->transferProtocol == "HTTPS"), params->fwImagePath, targetPath);
}

inline void mountTargetPath(const std::string& serviceName,
                            const std::string& objPath)
{
    // For update target path that needs mouting, proxy interface should be used
    // Here we try to mount the proxy interface
    // For target path that does not need mounting, catch exception and continue
    try
    {
        auto method = crow::connections::systemBus->new_method_call(
            serviceName.data(), objPath.data(),
            "xyz.openbmc_project.VirtualMedia.Proxy", "Mount");
        crow::connections::systemBus->call_noreply(method);
        BMCWEB_LOG_DEBUG("Mounting device");
    }
    catch (const sdbusplus::exception::SdBusError& ex)
    {
        if (std::string_view("org.freedesktop.DBus.Error.UnknownMethod") !=
            std::string_view(ex.name()))
        {
            BMCWEB_LOG_ERROR("Mounting error");
        }
        else
        {
            // This is a normal case for target path that doesn't need
            // any mounting
            BMCWEB_LOG_DEBUG("Continue without mounting");
        }
    }
    catch (...)
    {
        BMCWEB_LOG_ERROR("Mounting error");
    }
}

inline void downloadFirmwareImageToTarget(
    const std::shared_ptr<const crow::Request>& request,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<const SimpleUpdateParams>& params,
    const std::string& service, const std::string& objPath)
{
    mountTargetPath(service, objPath);
    BMCWEB_LOG_DEBUG(
        "Getting value of Path property for service {} and object path {}...",
        service, objPath);
    crow::connections::systemBus->async_method_call(
        [request, asyncResp,
         params](const boost::system::error_code ec,
                 const std::variant<std::string>& property) {
        if (ec)
        {
            messages::actionParameterNotSupported(asyncResp->res, "Targets",
                                                  "UpdateService.SimpleUpdate");
            BMCWEB_LOG_ERROR("Failed to read the path property of Target");
            BMCWEB_LOG_ERROR("error_code = {}, error msg = {}", ec,
                             ec.message());
            return;
        }

        const std::string* targetPath = std::get_if<std::string>(&property);

        if (targetPath == nullptr)
        {
            messages::actionParameterNotSupported(asyncResp->res, "Targets",
                                                  "UpdateService.SimpleUpdate");
            BMCWEB_LOG_ERROR("Null value returned for path");
            return;
        }

        BMCWEB_LOG_DEBUG("Path property: {}", *targetPath);

        // Check if local path exists
        if (!fs::exists(*targetPath))
        {
            messages::resourceNotFound(asyncResp->res, "Targets", *targetPath);
            BMCWEB_LOG_ERROR("Path does not exist");
            return;
        }

        // Setup callback for when new software detected
        // Give SCP 10 minutes to detect new software
        monitorForSoftwareAvailable(asyncResp, *request, 600);

        if (params->transferProtocol == "SCP")
        {
            downloadViaSCP(asyncResp, params, *targetPath);
        }
        else if ((params->transferProtocol == "HTTP") ||
                 (params->transferProtocol == "HTTPS"))
        {
            downloadViaHTTP(asyncResp, params, *targetPath);
        }
    },
        service, objPath, "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Common.FilePath", "Path");
}

inline void setUpdaterForceUpdateProperty(
    const std::shared_ptr<const crow::Request>& request,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<const SimpleUpdateParams>& params,
    const std::string& serviceName, const std::string& serviceObjectPath,
    const std::string& fwItemObjPath)
{
    BMCWEB_LOG_DEBUG(
        "Setting ForceUpdate property for service {} and object path {} to {}",
        serviceName, serviceObjectPath,
        (params->forceUpdate ? "true" : "false"));
    crow::connections::systemBus->async_method_call(
        [request, asyncResp, params, serviceName,
         fwItemObjPath](const boost::system::error_code ec) {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "Failed to set ForceUpdate property. Aborting update as "
                "value of ForceUpdate property can't be guaranteed.");
            BMCWEB_LOG_ERROR("error_code = {}", ec);
            BMCWEB_LOG_ERROR("error_msg = {}", ec.message());
            if (asyncResp)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }
        BMCWEB_LOG_DEBUG("ForceUpdate property successfully set to {}.",
                         (params->forceUpdate ? "true" : "false"));
        // begin downloading the firmware image to the target path
        downloadFirmwareImageToTarget(request, asyncResp, params, serviceName,
                                      fwItemObjPath);
    },
        serviceName, serviceObjectPath, "org.freedesktop.DBus.Properties",
        "Set", "xyz.openbmc_project.Software.UpdatePolicy", "ForceUpdate",
        dbus::utility::DbusVariantType(params->forceUpdate));
}

inline void findObjectPathAssociatedWithService(
    const std::shared_ptr<const crow::Request>& request,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<const SimpleUpdateParams>& params,
    const std::string& serviceName, const std::string& fwItemObjPath,
    const char* rootPath = "/xyz/openbmc_project")
{
    BMCWEB_LOG_DEBUG(
        "Searching for object paths associated with the service {} in the "
        "sub-tree {}...",
        serviceName, rootPath);
    crow::connections::systemBus->async_method_call(
        [request, asyncResp, params, serviceName, fwItemObjPath,
         rootPath](const boost::system::error_code ec,
                   const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("error_code = {}", ec);
            BMCWEB_LOG_ERROR("error_msg = {}", ec.message());
            if (asyncResp)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }
        else if (subtree.empty())
        {
            BMCWEB_LOG_DEBUG(
                "Could not find any services implementing "
                "xyz.openbmc_project.Software.UpdatePolicy associated with the "
                "object path {}. Proceeding with update as though the "
                "ForceUpdate property is set to false.",
                rootPath);
            // Begin downloading the firmware image to the target path
            downloadFirmwareImageToTarget(request, asyncResp, params,
                                          serviceName, fwItemObjPath);
            return;
        }
        // iterate through the object paths in the subtree until one is found
        // with an associated service name matching the input service.
        std::optional<std::string> serviceObjPath{};
        for (const auto& pathServiceMapPair : subtree)
        {
            const auto& currObjPath = pathServiceMapPair.first;
            for (const auto& serviceInterfacesMap : pathServiceMapPair.second)
            {
                const auto& currServiceName = serviceInterfacesMap.first;
                if (currServiceName == serviceName)
                {
                    if (!serviceObjPath.has_value())
                    {
                        serviceObjPath.emplace(currObjPath);
                        break;
                    }
                }
            }
            // break external for-loop if object path is found
            if (serviceObjPath.has_value())
            {
                break;
            }
        }
        if (serviceObjPath.has_value())
        {
            BMCWEB_LOG_DEBUG("Found object path {}.", serviceObjPath.value());
            // use the service and object path found to set the ForceUpdate
            // property
            setUpdaterForceUpdateProperty(request, asyncResp, params,
                                          serviceName, serviceObjPath.value(),
                                          fwItemObjPath);
        }
        else
        {
            // If there is no object implementing
            // xyz.openbmc_project.Software.UpdatePolicy associated with
            // the service under the sub-tree root, then that service does
            // not implement a force-update policy, and the download should
            // continue.
            BMCWEB_LOG_DEBUG(
                "Could not find any a service {} implementing "
                "xyz.openbmc_project.Software.UpdatePolicy and "
                "associated with the object path {}. Proceeding with "
                "update as though the ForceUpdate property is set to "
                "false.",
                serviceName, rootPath);
            // Begin downloading the firmware image to the target path
            downloadFirmwareImageToTarget(request, asyncResp, params,
                                          serviceName, fwItemObjPath);
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", rootPath, 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Software.UpdatePolicy"});
}

inline void findAssociatedUpdaterService(
    const std::shared_ptr<const crow::Request>& request,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<const SimpleUpdateParams>& params,
    const std::string& fwItemObjPath)
{
    BMCWEB_LOG_DEBUG("Searching for updater service associated with {}...",
                     fwItemObjPath);
    crow::connections::systemBus->async_method_call(
        [request, asyncResp, params,
         fwItemObjPath](const boost::system::error_code ec,
                        const MapperServiceMap& objInfo) {
        if (ec)
        {
            messages::actionParameterNotSupported(asyncResp->res, "Targets",
                                                  "UpdateService.SimpleUpdate");
            BMCWEB_LOG_ERROR("Request incorrect target URI parameter: {}",
                             fwItemObjPath);
            BMCWEB_LOG_ERROR("error_code = {}, error msg = {}", ec,
                             ec.message());
            return;
        }
        // Ensure we only got one service back
        if (objInfo.size() != 1)
        {
            messages::internalError(asyncResp->res);
            BMCWEB_LOG_ERROR("Invalid Object Size {}", objInfo.size());
            return;
        }
        const std::string& serviceName = objInfo[0].first;
        BMCWEB_LOG_DEBUG("Found service {}", serviceName);
        // The ForceUpdate property of the
        // xyz.openbmc_project.Software.UpdatePolicy dbus interface should
        // be explicitly set to true or false, according to the value of
        // ForceUpdate option in the Redfish command.
        findObjectPathAssociatedWithService(request, asyncResp, params,
                                            serviceName, fwItemObjPath);
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", fwItemObjPath,
        std::array<const char*, 2>{"xyz.openbmc_project.Software.Version",
                                   "xyz.openbmc_project.Common.FilePath"});
}

inline bool isProtocolScpOrHttp(const std::string& protocol)
{
    return protocol == "SCP" || protocol == "HTTP" || protocol == "HTTPS";
}

/**
 * UpdateServiceActionsSimpleUpdate class supports handle POST method for
 * SimpleUpdate action.
 */
inline void requestRoutesUpdateServiceActionsSimpleUpdate(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate/")
        .privileges(redfish::privileges::postUpdateService)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        std::string imageURI;
        std::optional<std::string> transferProtocol;
        std::optional<std::vector<std::string>> targets;
        std::optional<std::string> username;
        std::optional<bool> forceUpdate;

        BMCWEB_LOG_DEBUG("Enter UpdateService.SimpleUpdate doPost");

        // User can pass in both TransferProtocol and ImageURI
        // parameters or they can pass in just the ImageURI with the
        // transfer protocol embedded within it. 1)
        // TransferProtocol:TFTP ImageURI:1.1.1.1/myfile.bin 2)
        // ImageURI:tftp://1.1.1.1/myfile.bin

        bool success = json_util::readJsonAction(
            req, asyncResp->res, "TransferProtocol", transferProtocol,
            "ImageURI", imageURI, "Targets", targets, "Username", username,
            "ForceUpdate", forceUpdate);
        if (!success && imageURI.empty())
        {
            messages::createFailedMissingReqProperties(asyncResp->res,
                                                       "ImageURI");
            BMCWEB_LOG_DEBUG("Missing ImageURI");
            return;
        }

        if (!transferProtocol)
        {
            // Must be option 2
            // Verify ImageURI has transfer protocol in it
            size_t separator = imageURI.find(':');
            if ((separator == std::string::npos) ||
                ((separator + 1) > imageURI.size()))
            {
                messages::actionParameterValueTypeError(
                    asyncResp->res, imageURI, "ImageURI",
                    "UpdateService.SimpleUpdate");
                BMCWEB_LOG_ERROR("ImageURI missing transfer protocol: {}",
                                 imageURI);
                return;
            }

            transferProtocol = imageURI.substr(0, separator);
            // Ensure protocol is upper case for a common comparison
            // path below
            boost::to_upper(*transferProtocol);
            BMCWEB_LOG_DEBUG("Encoded transfer protocol {}", *transferProtocol);

            // Adjust imageURI to not have the protocol on it for
            // parsing below ex. tftp://1.1.1.1/myfile.bin
            // -> 1.1.1.1/myfile.bin
            imageURI = imageURI.substr(separator + 3);
            BMCWEB_LOG_DEBUG("Adjusted imageUri {}", imageURI);
        }

        std::vector<std::string> supportedProtocols;
        if constexpr (BMCWEB_INSECURE_PUSH_STYLE_NOTIFICATION)
        {
            supportedProtocols.push_back("TFTP");
        }
#ifdef BMCWEB_ENABLE_REDFISH_FW_SCP_UPDATE
        supportedProtocols.push_back("SCP");
#endif
#ifdef BMCWEB_ENABLE_REDFISH_FW_HTTP_HTTPS_UPDATE
        supportedProtocols.push_back("HTTP");
        supportedProtocols.push_back("HTTPS");
#endif

        auto searchProtocol = std::find(supportedProtocols.begin(),
                                        supportedProtocols.end(),
                                        *transferProtocol);
        if (searchProtocol == supportedProtocols.end())
        {
            messages::actionParameterNotSupported(asyncResp->res,
                                                  "TransferProtocol",
                                                  "UpdateService.SimpleUpdate");
            BMCWEB_LOG_ERROR("Request incorrect protocol parameter: {}",
                             *transferProtocol);
            return;
        }

        if (isProtocolScpOrHttp(*transferProtocol))
        {
            if (!targets.has_value())
            {
                messages::createFailedMissingReqProperties(asyncResp->res,
                                                           "Targets");
                BMCWEB_LOG_DEBUG("Missing Target URI");
                return;
            }
            else if (targets->empty())
            {
                messages::propertyValueIncorrect(asyncResp->res, "Targets",
                                                 targets.value());
                BMCWEB_LOG_DEBUG("Invalid Targets parameter: []");
                return;
            }
        }

        if ((*transferProtocol == "SCP") && (!username))
        {
            messages::createFailedMissingReqProperties(asyncResp->res,
                                                       "Username");
            BMCWEB_LOG_DEBUG("Missing Username");
            return;
        }

        // Format should be <IP or Hostname>/<file> for imageURI
        size_t separator = imageURI.find('/');
        if ((separator == std::string::npos) ||
            ((separator + 1) > imageURI.size()))
        {
            messages::actionParameterValueTypeError(
                asyncResp->res, imageURI, "ImageURI",
                "UpdateService.SimpleUpdate");
            BMCWEB_LOG_ERROR("Invalid ImageURI: {}", imageURI);
            return;
        }

        std::string server = imageURI.substr(0, separator);
        std::string fwFile = imageURI.substr(separator + 1);
        BMCWEB_LOG_DEBUG("Server: {} File: {} Protocol: {}", server, fwFile,
                         *transferProtocol);

        // Allow only one operation at a time
        if (fwUpdateInProgress != false)
        {
            if (asyncResp)
            {
                std::string resolution =
                    "Another update is in progress. Retry"
                    " the update operation once it is complete.";
                messages::updateInProgressMsg(asyncResp->res, resolution);
            }
            return;
        }

        if (*transferProtocol == "TFTP")
        {
            // Setup callback for when new software detected
            // Give TFTP 10 minutes to detect new software
            monitorForSoftwareAvailable(asyncResp, req, 600);

            // TFTP can take up to 10 minutes depending on image size and
            // connection speed. Return to caller as soon as the TFTP
            // operation has been started. The callback above will ensure
            // the activate is started once the download has completed
            messages::success(asyncResp->res);

            // Call TFTP service
            crow::connections::systemBus->async_method_call(
                [](const boost::system::error_code& ec) {
                if (ec)
                {
                    // messages::internalError(asyncResp->res);
                    cleanUp();
                    BMCWEB_LOG_DEBUG("error_code = {}", ec);
                    BMCWEB_LOG_DEBUG("error msg = {}", ec.message());
                }
                else
                {
                    BMCWEB_LOG_DEBUG("Call to DownloaViaTFTP Success");
                }
            },
                "xyz.openbmc_project.Software.Download",
                "/xyz/openbmc_project/software",
                "xyz.openbmc_project.Common.TFTP", "DownloadViaTFTP", fwFile,
                server);
        }
        else if (isProtocolScpOrHttp(*transferProtocol))
        {
            // Take the first target as only one target is supported
            std::string targetURI = targets.value()[0];
            if (targetURI.find(firmwarePrefix) == std::string::npos)
            {
                // Find the last occurrence of the directory separator character
                messages::actionParameterNotSupported(
                    asyncResp->res, "Targets", "UpdateService.SimpleUpdate");
                BMCWEB_LOG_ERROR("Invalid TargetURI: {}", targetURI);
                return;
            }
            std::string objName = "/xyz/openbmc_project/software/" +
                                  targetURI.substr(firmwarePrefix.length());
            BMCWEB_LOG_INFO("Object path: {}", objName);
            // pack SimpleUpdate parameters in shared pointer to allow safe
            // usage during the sequence of asynchronous method calls.
            // The value of forceUpdate is false by default.
            const auto params = std::make_shared<const SimpleUpdateParams>(
                server, fwFile, *transferProtocol, forceUpdate.value_or(false),
                username);
            // wrap crow::Request in a shared pointer to pass securely to
            // asynchronous method calls.
            const auto sharedReq = std::make_shared<const crow::Request>(req);
            // Search for the version object related to the given target URI
            findAssociatedUpdaterService(sharedReq, asyncResp, params, objName);
        }
        BMCWEB_LOG_DEBUG("Exit UpdateService.SimpleUpdate doPost");
    });
}

/**
 * @brief Upload firmware image
 *
 * @param[in] req  HTTP request.
 * @param[in] asyncResp Pointer to object holding response data
 *
 * @return None
 */
inline void uploadImageFile(const std::shared_ptr<const crow::Request>& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::filesystem::path filepath(updateServiceImageLocation +
                                   bmcweb::getRandomUUID());

    monitorForSoftwareAvailable(asyncResp, *req, fwObjectCreationDefaultTimeout,
                                filepath);

    BMCWEB_LOG_DEBUG("Writing file to {}", filepath.string());
    std::ofstream out(filepath, std::ofstream::out | std::ofstream::binary |
                                    std::ofstream::trunc);
    // set the permission of the file to 640
    std::filesystem::perms permission = std::filesystem::perms::owner_read |
                                        std::filesystem::perms::group_read;
    std::filesystem::permissions(filepath, permission);

    MultipartParser parser;
    ParserError ec = parser.parse(*req);
    if (ec != ParserError::PARSER_SUCCESS)
    {
        // handle error
        BMCWEB_LOG_ERROR("MIME parse failed, ec : {}", static_cast<int>(ec));
        messages::internalError(asyncResp->res);
        return;
    }

    bool hasUpdateFile = false;

    for (const FormPart& formpart : parser.mime_fields)
    {
        boost::beast::http::fields::const_iterator it =
            formpart.fields.find("Content-Disposition");

        size_t index = it->value().find(';');
        if (index == std::string::npos)
        {
            continue;
        }

        for (const auto& param :
             boost::beast::http::param_list{it->value().substr(index)})
        {
            if (param.first != "name" || param.second.empty())
            {
                continue;
            }

            if (param.second == "UpdateFile")
            {
                hasUpdateFile = true;
                out << formpart.content;

                if (out.bad())
                {
                    BMCWEB_LOG_ERROR("Error writing to file: {}",
                                     filepath.string());
                    messages::internalError(asyncResp->res);
                    cleanUp();
                }
            }
        }
    }

    if (!hasUpdateFile)
    {
        BMCWEB_LOG_ERROR("File with firmware image is missing.");
        messages::propertyMissing(asyncResp->res, "UpdateFile");
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

/**
 * @brief Check if the list of targets contains invalid and unupdateable
 * targets. The function returns a list of valid targets in the parameter
 * 'validTargets'
 *
 * @param[in] uriTargets  List of components delivered in HTTPRequest
 * @param[in] updateables List of all unupdateable components in the system
 * @param[in] swInvPaths  List of software inventory paths
 * @param[out] validTargets  List of valid components delivered in HTTPRequest
 *
 * @return It returns true when a list of delivered components contains invalid
 * or unupdateable components
 */
inline bool areTargetsInvalidOrUnupdatable(
    const std::vector<std::string>& uriTargets,
    const std::vector<std::string>& updateables,
    const std::vector<std::string>& swInvPaths,
    std::vector<sdbusplus::message::object_path>& validTargets)
{
    bool hasAnyInvalidOrUnupdateableTarget = false;
    for (const std::string& target : uriTargets)
    {
        std::string componentName = std::filesystem::path(target).filename();
        bool validTarget = false;
        std::string softwarePath = "/xyz/openbmc_project/software/" +
                                   componentName;

        if (std::any_of(swInvPaths.begin(), swInvPaths.end(),
                        [&](const std::string& path) {
            return path.find(softwarePath) != std::string::npos;
        }))
        {
            validTarget = true;

            if (std::find(updateables.begin(), updateables.end(),
                          componentName) != updateables.end())
            {
                validTargets.emplace_back(
                    sdbusplus::message::object_path(softwarePath));
            }
            else
            {
                hasAnyInvalidOrUnupdateableTarget = true;
                BMCWEB_LOG_ERROR("Unupdatable Target: {}", target);
            }
        }

        if (!validTarget)
        {
            hasAnyInvalidOrUnupdateableTarget = true;
            BMCWEB_LOG_ERROR("Invalid Target: {}", target);
        }
    }

    return hasAnyInvalidOrUnupdateableTarget;
}

/**
 * @brief Sets the OEM Firmware UpdateOption in the UpdatePolicy.
 *
 * @param asyncResp Shared pointer to the response object.
 * @param oemUpdateOption The update option ("StageOnly" or "StageAndActivate").
 * @param callback Optional callback after setting the property.
 */
inline void
    setOemUpdateOption(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& oemUpdateOption,
                       const std::function<void()>& callback = {})
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, oemUpdateOption, callback](
            const boost::system::error_code errorCode,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) mutable {
        if (errorCode)
        {
            BMCWEB_LOG_ERROR("error_code = {}", errorCode);
            BMCWEB_LOG_ERROR("error msg = {}", errorCode.message());
            if (asyncResp)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }
        // Check if only one service implements
        // xyz.openbmc_project.Software.UpdatePolicy
        if (objInfo.size() != 1)
        {
            BMCWEB_LOG_ERROR(
                "Expected exactly one service implementing UpdatePolicy interface, but found {} services.",
                objInfo.size());
            if (asyncResp)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }

        std::string oemUpdateOptionNewVal;
        if (oemUpdateOption == "StageOnly")
        {
            oemUpdateOptionNewVal =
                "xyz.openbmc_project.Software.UpdatePolicy.UpdateOptionSupport.StageOnly";
        }
        else if (oemUpdateOption == "StageAndActivate")
        {
            oemUpdateOptionNewVal =
                "xyz.openbmc_project.Software.UpdatePolicy.UpdateOptionSupport.StageAndActivate";
        }
        sdbusplus::asio::setProperty(
            *crow::connections::systemBus, objInfo[0].first,
            "/xyz/openbmc_project/software",
            "xyz.openbmc_project.Software.UpdatePolicy", "UpdateOption",
            oemUpdateOptionNewVal,
            [asyncResp, oemUpdateOption,
             callback](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("error_code = {}", ec);
                BMCWEB_LOG_ERROR("error msg = {}", ec.message());
                messages::internalError(asyncResp->res);
            }
            if (callback)
            {
                callback();
            }
        });
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject",
        "/xyz/openbmc_project/software",
        std::array<const char*, 1>{
            "xyz.openbmc_project.Software.UpdatePolicy"});
}

/**
 * @brief Handle update policy
 *
 * @param[in] errorCode Error code
 * @param[in] objInfo Service object
 * @param[in] req  HTTP request
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] targets  List of valid components delivered in HTTPRequest
 * @param[in] oemUpdateOption OEM-specific update option.
 *
 * @return None
 */
inline void validateUpdatePolicyCallback(
    const boost::system::error_code errorCode,
    const dbus::utility::MapperServiceMap& objInfo,
    const std::shared_ptr<const crow::Request>& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::vector<sdbusplus::message::object_path>& targets,
    const std::optional<std::string>& oemUpdateOption)
{
    if (errorCode)
    {
        BMCWEB_LOG_ERROR("validateUpdatePolicyCallback:error_code = {}",
                         errorCode);
        BMCWEB_LOG_ERROR("validateUpdatePolicyCallback:error msg = {}",
                         errorCode.message());
        if (asyncResp)
        {
            messages::internalError(asyncResp->res);
        }
        return;
    }
    // Ensure we only got one service back
    if (objInfo.size() != 1)
    {
        BMCWEB_LOG_ERROR(
            "More than one service support xyz.openbmc_project.Software.UpdatePolicy. Object Size {}",
            objInfo.size());
        if (asyncResp)
        {
            messages::internalError(asyncResp->res);
        }
        return;
    }

    crow::connections::systemBus->async_method_call(
        [req, asyncResp, objInfo,
         oemUpdateOption](const boost::system::error_code ec) mutable {
        if (ec)
        {
            BMCWEB_LOG_ERROR("error_code = {}", ec);
            messages::internalError(asyncResp->res);
        }
        setOemUpdateOption(
            asyncResp, oemUpdateOption.value_or("StageAndActivate"),
            [req, asyncResp]() { uploadImageFile(req, asyncResp); });
    },
        objInfo[0].first, "/xyz/openbmc_project/software",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Software.UpdatePolicy", "Targets",
        dbus::utility::DbusVariantType(targets));
}

/**
 * @brief Handle check updateable devices
 *
 * @param[in] ec Error code
 * @param[in] objPaths Object paths
 * @param[in] req  HTTP request
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] uriTargets List of valid components delivered in HTTPRequest
 * @param[in] swInvPaths List of software inventory paths
 * @param[in] oemUpdateOption OEM-specific update option.
 *
 * @return None
 */
inline void areTargetsUpdateableCallback(
    const boost::system::error_code& ec,
    const std::vector<std::string>& objPaths,
    const std::shared_ptr<const crow::Request>& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::vector<std::string>& uriTargets,
    const std::vector<std::string>& swInvPaths,
    const std::optional<std::string>& oemUpdateOption)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("areTargetsUpdateableCallback:error_code = {}", ec);
        BMCWEB_LOG_DEBUG("areTargetsUpdateableCallback:error msg =  {}",
                         ec.message());

        BMCWEB_LOG_ERROR("Targeted devices not updateable");

        boost::urls::url_view targetURL("Target");
        messages::invalidObject(asyncResp->res, targetURL);
        return;
    }

    std::vector<std::string> updateableFw;
    for (const auto& reqFwObjPath : swInvPaths)
    {
        if (std::find(objPaths.begin(), objPaths.end(), reqFwObjPath) !=
            objPaths.end())
        {
            std::string compName =
                std::filesystem::path(reqFwObjPath).filename();
            updateableFw.push_back(compName);
        }
    }

    std::vector<sdbusplus::message::object_path> targets = {};
    // validate TargetUris if entries are present
    if (uriTargets.size() != 0)
    {
        if (areTargetsInvalidOrUnupdatable(uriTargets, updateableFw, swInvPaths,
                                           targets))
        {
            boost::urls::url_view targetURL("Target");
            messages::invalidObject(asyncResp->res, targetURL);
            return;
        }

        // else all targets are valid
    }

    crow::connections::systemBus->async_method_call(
        [req, asyncResp, targets, oemUpdateOption](
            const boost::system::error_code errorCode,
            const dbus::utility::MapperServiceMap& objInfo) mutable {
        validateUpdatePolicyCallback(errorCode, objInfo, req, asyncResp,
                                     targets, oemUpdateOption);
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject",
        "/xyz/openbmc_project/software",
        std::array<const char*, 1>{
            "xyz.openbmc_project.Software.UpdatePolicy"});
}

/**
 * @brief Perfom a check to determine if the targets are updateable
 *
 * @param[in] req  HTTP request
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] uriTargets  List of valid components delivered in HTTPRequest
 * @param[in] oemUpdateOption OEM-specific update option.
 *
 * @return None
 */
inline void
    areTargetsUpdateable(const std::shared_ptr<const crow::Request>& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::vector<std::string>& uriTargets,
                         const std::optional<std::string>& oemUpdateOption)
{
    crow::connections::systemBus->async_method_call(
        [req, asyncResp, uriTargets,
         oemUpdateOption](const boost::system::error_code ec,
                          const std::vector<std::string>& swInvPaths) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("D-Bus responses error: {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }

        sdbusplus::asio::getProperty<std::vector<std::string>>(
            *crow::connections::systemBus, "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/software/updateable",
            "xyz.openbmc_project.Association", "endpoints",
            [req, asyncResp, uriTargets, swInvPaths,
             oemUpdateOption](const boost::system::error_code ec,
                              const std::vector<std::string>& objPaths) {
            areTargetsUpdateableCallback(ec, objPaths, req, asyncResp,
                                         uriTargets, swInvPaths,
                                         oemUpdateOption);
        });
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/software/", static_cast<int32_t>(0),
        std::array<std::string, 1>{"xyz.openbmc_project.Software.Version"});
}

inline std::optional<std::string>
    processUrl(boost::system::result<boost::urls::url_view>& url)
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

inline void
    handleStartUpdate(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      task::Payload payload, const std::string& objectPath,
                      const boost::system::error_code& ec,
                      const sdbusplus::message::object_path& retPath)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("error_code = {}", ec);
        BMCWEB_LOG_ERROR("error msg = {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }

    BMCWEB_LOG_INFO("Call to StartUpdate Success, retPath = {}", retPath.str);
    createTask(asyncResp, std::move(payload), objectPath);
}

inline void startUpdate(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        task::Payload payload,
                        const MemoryFileDescriptor& memfd,
                        const std::string& applyTime,
                        const std::string& objectPath,
                        const std::string& serviceName)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, payload = std::move(payload),
         objectPath](const boost::system::error_code& ec1,
                     const sdbusplus::message::object_path& retPath) mutable {
        handleStartUpdate(asyncResp, std::move(payload), objectPath, ec1,
                          retPath);
    },
        serviceName, objectPath, "xyz.openbmc_project.Software.Update",
        "StartUpdate", sdbusplus::message::unix_fd(memfd.fd), applyTime);
}

inline void getAssociatedUpdateInterface(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, task::Payload payload,
    const MemoryFileDescriptor& memfd, const std::string& applyTime,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("error_code = {}", ec);
        BMCWEB_LOG_ERROR("error msg = {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }
    BMCWEB_LOG_DEBUG("Found {} startUpdate subtree paths", subtree.size());

    if (subtree.size() > 1)
    {
        BMCWEB_LOG_ERROR("Found more than one startUpdate subtree paths");
        messages::internalError(asyncResp->res);
        return;
    }

    auto objectPath = subtree[0].first;
    auto serviceName = subtree[0].second[0].first;

    BMCWEB_LOG_DEBUG("Found objectPath {} serviceName {}", objectPath,
                     serviceName);
    startUpdate(asyncResp, std::move(payload), memfd, applyTime, objectPath,
                serviceName);
}

inline void
    getSwInfo(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
              task::Payload payload, MemoryFileDescriptor memfd,
              const std::string& applyTime, const std::string& target,
              const boost::system::error_code& ec,
              const dbus::utility::MapperGetSubTreePathsResponse& subtree)
{
    using SwInfoMap =
        std::unordered_map<std::string, sdbusplus::message::object_path>;
    SwInfoMap swInfoMap;

    if (ec)
    {
        BMCWEB_LOG_ERROR("error_code = {}", ec);
        BMCWEB_LOG_ERROR("error msg = {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }
    BMCWEB_LOG_DEBUG("Found {} software version paths", subtree.size());

    for (const auto& objectPath : subtree)
    {
        sdbusplus::message::object_path path(objectPath);
        std::string swId = path.filename();
        swInfoMap.emplace(swId, path);
    }

    auto swEntry = swInfoMap.find(target);
    if (swEntry == swInfoMap.end())
    {
        BMCWEB_LOG_WARNING("No valid DBus path for Target URI {}", target);
        messages::propertyValueFormatError(asyncResp->res, target, "Targets");
        return;
    }

    BMCWEB_LOG_DEBUG("Found software version path {}", swEntry->second.str);

    sdbusplus::message::object_path swObjectPath = swEntry->second /
                                                   "software_version";
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Software.Update"};
    dbus::utility::getAssociatedSubTree(
        swObjectPath,
        sdbusplus::message::object_path("/xyz/openbmc_project/software"), 0,
        interfaces,
        [asyncResp, payload = std::move(payload), memfd = std::move(memfd),
         applyTime](
            const boost::system::error_code& ec1,
            const dbus::utility::MapperGetSubTreeResponse& subtree1) mutable {
        getAssociatedUpdateInterface(asyncResp, std::move(payload), memfd,
                                     applyTime, ec1, subtree1);
    });
}

inline void
    processUpdateRequest(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         task::Payload&& payload, std::string_view body,
                         const std::string& applyTime,
                         std::vector<std::string>& targets)
{
    MemoryFileDescriptor memfd("update-image");
    if (memfd.fd == -1)
    {
        BMCWEB_LOG_ERROR("Failed to create image memfd");
        messages::internalError(asyncResp->res);
        return;
    }
    if (write(memfd.fd, body.data(), body.length()) !=
        static_cast<ssize_t>(body.length()))
    {
        BMCWEB_LOG_ERROR("Failed to write to image memfd");
        messages::internalError(asyncResp->res);
        return;
    }
    if (!memfd.rewind())
    {
        messages::internalError(asyncResp->res);
        return;
    }

    if (!targets.empty() && targets[0] == BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        startUpdate(asyncResp, std::move(payload), memfd, applyTime,
                    "/xyz/openbmc_project/software/bmc",
                    "xyz.openbmc_project.Software.Manager");
    }
    else
    {
        constexpr std::array<std::string_view, 1> interfaces = {
            "xyz.openbmc_project.Software.Version"};
        dbus::utility::getSubTreePaths(
            "/xyz/openbmc_project/software", 1, interfaces,
            [asyncResp, payload = std::move(payload), memfd = std::move(memfd),
             applyTime,
             targets](const boost::system::error_code& ec,
                      const dbus::utility::MapperGetSubTreePathsResponse&
                          subtree) mutable {
            getSwInfo(asyncResp, std::move(payload), std::move(memfd),
                      applyTime, targets[0], ec, subtree);
        });
    }
}

/**
 * @brief Parse multipart update form
 *
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] parser  Multipart Parser
 * @param[out] hasUpdateParameters return true when 'UpdateParameters' is added
 * to HTTPRequest
 * @param[out] targets List of delivered targets in HTTPRequest
 * @param[out] applyTime Operation Apply Time
 * @param[out] forceUpdate return true when force update policy should be set
 * @param[out] oemUpdateOption Optional OEM-specific update option.
 * @param[out] hasFile return true when 'UpdateFile' is added to HTTPRequest
 *
 * @return It returns true when parsing of the multipart update form is
 * successfully completed.
 */
inline bool parseMultipartForm(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const MultipartParser& parser, bool& hasUpdateParameters,
    std::optional<std::vector<std::string>>& targets,
    std::optional<std::string>& applyTime, std::optional<bool>& forceUpdate,
    [[maybe_unused]] std::optional<std::string>& oemUpdateOption, bool& hasFile)
{
    hasUpdateParameters = false;
    hasFile = false;
    for (const FormPart& formpart : parser.mime_fields)
    {
        boost::beast::http::fields::const_iterator it =
            formpart.fields.find("Content-Disposition");
        if (it == formpart.fields.end())
        {
            BMCWEB_LOG_ERROR("Couldn't find Content-Disposition");
            messages::propertyMissing(asyncResp->res, "Content-Disposition");
            return false;
        }
        BMCWEB_LOG_INFO("Parsing value {}", it->value());

        // The construction parameters of param_list must start with `;`
        size_t index = it->value().find(';');
        if (index == std::string::npos)
        {
            continue;
        }

        for (const auto& param :
             boost::beast::http::param_list{it->value().substr(index)})
        {
            if (param.first != "name" || param.second.empty())
            {
                continue;
            }

            if (param.second == "UpdateParameters")
            {
                hasUpdateParameters = true;
                nlohmann::json content = nlohmann::json::parse(formpart.content,
                                                               nullptr, false);
                if (content.is_discarded())
                {
                    BMCWEB_LOG_INFO("UpdateParameters parse error:{}",
                                    formpart.content);
                    messages::unrecognizedRequestBody(asyncResp->res);

                    return false;
                }

                try
                {
#ifdef BMCWEB_ENABLE_NVIDIA_UPDATE_STAGING
                    std::optional<nlohmann::json> oemObject;
                    json_util::readJson(content, asyncResp->res, "Targets",
                                        targets, "@Redfish.OperationApplyTime",
                                        applyTime, "ForceUpdate", forceUpdate,
                                        "Oem", oemObject);

                    if (oemObject)
                    {
                        std::optional<nlohmann::json> oemNvidiaObject;
                        if (json_util::readJson(*oemObject, asyncResp->res,
                                                "Nvidia", oemNvidiaObject))
                        {
                            json_util::readJson(*oemNvidiaObject,
                                                asyncResp->res, "UpdateOption",
                                                oemUpdateOption);
                        }
                    }
#else
                    json_util::readJson(content, asyncResp->res, "Targets",
                                        targets, "@Redfish.OperationApplyTime",
                                        applyTime, "ForceUpdate", forceUpdate);
#endif
                }
                catch (const std::exception& e)
                {
                    BMCWEB_LOG_ERROR(
                        "Unable to parse JSON. Check the format of the request body. Exception caught: {}",
                        e.what());
                    messages::unrecognizedRequestBody(asyncResp->res);

                    return false;
                }
            }
            else if (param.second == "UpdateFile")
            {
                boost::beast::http::fields::const_iterator contentTypeIt =
                    formpart.fields.find("Content-Type");
                if (contentTypeIt == formpart.fields.end() ||
                    contentTypeIt->value() != "application/octet-stream")
                {
                    BMCWEB_LOG_ERROR(
                        "UpdateFile parameter must be of type 'application/octet-stream'");
                    messages::unsupportedMediaType(asyncResp->res);
                    return false;
                }
                hasFile = true;
            }
        }
    }

    return true;
}

/**
 * @brief Check multipart update form UpdateParameters
 *
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] hasUpdateParameters true when 'UpdateParameters' is added
 * to HTTPRequest
 * @param[in] applyTime Operation Apply Time
 * @param[in] oemUpdateOption OEM-specific update option.
 *
 * @return It returns true when the form section 'UpdateParameters' contains the
 * required parameters.
 */
inline bool validateUpdateParametersFormData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    bool hasUpdateParameters, std::optional<std::string>& applyTime,
    std::optional<std::string>& oemUpdateOption)
{
    if (!hasUpdateParameters)
    {
        BMCWEB_LOG_INFO("UpdateParameters parameter is missing");

        messages::actionParameterMissing(asyncResp->res, "update-multipart",
                                         "UpdateParameters");

        return false;
    }

    if (applyTime)
    {
        std::string allowedApplyTime = "Immediate";
        if (allowedApplyTime != *applyTime)
        {
            BMCWEB_LOG_INFO(
                "ApplyTime value is not in the list of acceptable values");

            messages::propertyValueNotInList(asyncResp->res, *applyTime,
                                             "@Redfish.OperationApplyTime");

            return false;
        }
    }
    if (oemUpdateOption)
    {
        if (oemUpdateOption != "StageOnly" and
            oemUpdateOption != "StageAndActivate")
        {
            BMCWEB_LOG_ERROR(
                "Update option value {} is not in the list of acceptable values",
                *oemUpdateOption);
            messages::propertyValueNotInList(asyncResp->res, *oemUpdateOption,
                                             "UpdateOption");
            return false;
        }
    }

    return true;
}

/**
 * @brief Check multipart update form UpdateFile
 *
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] hasFile true when 'UpdateFile' is added to HTTPRequest
 *
 * @return It returns true when the form section 'UpdateFile' contains the
 * required parameters.
 */
inline bool validateUpdateFileFormData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const bool hasFile)
{
    if (!hasFile)
    {
        BMCWEB_LOG_ERROR("Upload data is NULL");
        messages::propertyMissing(asyncResp->res, "UpdateFile");
        return false;
    }

    return true;
}

#ifdef BMCWEB_ENABLE_REDFISH_AGGREGATION
/**
 * @brief retry handler of the aggregation post request.
 *
 * @param[in] respCode HTTP response status code
 *
 * @return None
 */
inline boost::system::error_code
    aggregationPostRetryHandler(unsigned int respCode)
{
    // Allow all response codes because we want to surface any satellite
    // issue to the client
    BMCWEB_LOG_DEBUG(
        "Received {} response of the firmware update from satellite", respCode);
    return boost::system::errc::make_error_code(boost::system::errc::success);
}

inline crow::ConnectionPolicy getPostAggregationPolicy()
{
    return {.maxRetryAttempts = 0,
            .requestByteLimit = firmwareImageLimitBytes,
            .maxConnections = 20,
            .retryPolicyAction = "TerminateAfterRetries",
            .retryIntervalSecs = std::chrono::seconds(0),
            .invalidResp = aggregationPostRetryHandler};
}

/**
 * @brief process the response from satellite BMC.
 *
 * @param[in] prefix the prefix of the url
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] resp Pointer to object holding response data from satellite BMC
 *
 * @return None
 */
void handleSatBMCResponse(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          crow::Response& resp)
{
    // 429 and 502 mean we didn't actually send the request so don't
    // overwrite the response headers in that case
    if ((resp.result() == boost::beast::http::status::too_many_requests) ||
        (resp.result() == boost::beast::http::status::bad_gateway))
    {
        asyncResp->res.result(resp.result());
        return;
    }

    if (resp.resultInt() !=
        static_cast<unsigned>(boost::beast::http::status::accepted))
    {
        asyncResp->res.result(resp.result());
        asyncResp->res.copyBody(resp);
        return;
    }

    // The resp will not have a json component
    // We need to create a json from resp's stringResponse
    std::string_view contentType = resp.getHeaderValue("Content-Type");
    if (bmcweb::asciiIEquals(contentType, "application/json") ||
        bmcweb::asciiIEquals(contentType, "application/json; charset=utf-8"))
    {
        auto jsonVal = nlohmann::json::parse(*resp.body(), nullptr, false);
        if (jsonVal.is_discarded())
        {
            BMCWEB_LOG_ERROR("Error parsing satellite response as JSON");

            // Notify the user if doing so won't overwrite a valid response
            if (asyncResp->res.resultInt() !=
                static_cast<unsigned>(boost::beast::http::status::ok))
            {
                messages::operationFailed(asyncResp->res);
            }
            return;
        }
        BMCWEB_LOG_DEBUG("Successfully parsed satellite response");
        auto* object = jsonVal.get_ptr<nlohmann::json::object_t*>();
        if (object == nullptr)
        {
            BMCWEB_LOG_ERROR("Parsed JSON was not an object?");
            return;
        }

        std::string rfaPrefix = redfishAggregationPrefix;
        for (std::pair<const std::string, nlohmann::json>& prop : *object)
        {
            // only prefix fix-up on Task response.
            std::string* strValue = prop.second.get_ptr<std::string*>();
            if (strValue == nullptr)
            {
                BMCWEB_LOG_CRITICAL("Item is not a string");
                continue;
            }
            if (prop.first == "@odata.id")
            {
                std::string file = std::filesystem::path(*strValue).filename();
                std::string path =
                    std::filesystem::path(*strValue).parent_path();

                file = rfaPrefix + "_" + file;
                path += "/";
                // add prefix on odata.id property.
                prop.second = path + file;
            }
            if (prop.first == "Id")
            {
                std::string file = std::filesystem::path(*strValue).filename();
                // add prefix on Id property.
                prop.second = rfaPrefix + "_" + file;
            }
            else
            {
                continue;
            }
        }
        asyncResp->res.result(resp.result());
        asyncResp->res.jsonValue = std::move(jsonVal);
    }
}

/**
 * @brief Forward firmware image to the satellite BMC
 *
 * @param[in] req  HTTP request.
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] ec the error code returned by Dbus call.
 * @param[in] satelliteInfo the map containing the satellite controllers
 *
 * @return None
 */
inline void forwardImage(
    crow::Request& req, const MultipartParser& parser, const bool updateAll,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const std::unordered_map<std::string, boost::urls::url>& satelliteInfo)
{
    // Something went wrong while querying dbus
    if (ec)
    {
        BMCWEB_LOG_ERROR("Dbus query error for satellite BMC.");
        messages::internalError(asyncResp->res);
        return;
    }

    const auto& sat = satelliteInfo.find(redfishAggregationPrefix);
    if (sat == satelliteInfo.end())
    {
        BMCWEB_LOG_ERROR("satellite BMC is not there.");
        return;
    }

    crow::HttpClient client(
        *req.ioService,
        std::make_shared<crow::ConnectionPolicy>(getPostAggregationPolicy()));

    std::function<void(crow::Response&)> cb =
        std::bind_front(handleSatBMCResponse, asyncResp);

    bool hasUpdateFile = false;
    std::string data;
    std::string_view boundary(parser.boundary);
    for (const FormPart& formpart : parser.mime_fields)
    {
        boost::beast::http::fields::const_iterator it =
            formpart.fields.find("Content-Disposition");

        size_t index = it->value().find(';');
        if (index == std::string::npos)
        {
            continue;
        }
        // skip \r\n and get the boundary
        data += boundary.substr(2);
        data += "\r\n";
        data += "Content-Disposition:";
        data += formpart.fields.at("Content-Disposition");
        data += "\r\n";

        for (const auto& param :
             boost::beast::http::param_list{it->value().substr(index)})
        {
            if (param.first != "name" || param.second.empty())
            {
                continue;
            }

            if (param.second == "UpdateFile")
            {
                data += "Content-Type: application/octet-stream\r\n\r\n";
                data += std::move(formpart.content);
                data += "\r\n";
                hasUpdateFile = true;
            }
            else if (param.second == "UpdateParameters")
            {
                data += "Content-Type: application/json\r\n\r\n";
                nlohmann::json content = nlohmann::json::parse(formpart.content,
                                                               nullptr, false);
                if (content.is_discarded())
                {
                    BMCWEB_LOG_INFO("UpdateParameters parse error:{}",
                                    formpart.content);
                    continue;
                }
                std::optional<std::vector<std::string>> targets;
                std::optional<bool> forceUpdate;
                std::optional<nlohmann::json> oemObject;

                json_util::readJson(content, asyncResp->res, "Targets", targets,
                                    "ForceUpdate", forceUpdate, "Oem",
                                    oemObject);

                nlohmann::json paramJson = nlohmann::json::object();

                const std::string urlPrefix = redfishAggregationPrefix;
                // individual components update
                if (targets && updateAll == false)
                {
                    paramJson["Targets"] = nlohmann::json::array();

                    for (auto& uri : *targets)
                    {
                        // the special handling for Gb200Nvl System.
                        // we don't remove the prefix if the resource's prefix
                        // from FirmwareInventory is the same with RFA prefix.
#ifdef RAF_PREFIX_REMOVAL
                        // remove prefix before the update request is forwarded.
                        std::string file =
                            std::filesystem::path(uri).filename();
                        size_t pos = uri.find(urlPrefix + "_");
                        if (pos != std::string::npos)
                        {
                            uri.erase(pos, urlPrefix.size() + 1);
                        }
#endif
                        BMCWEB_LOG_DEBUG("uri in Targets: {}", uri);
                        paramJson["Targets"].push_back(uri);
                    }
                }
                if (forceUpdate)
                {
                    paramJson["ForceUpdate"] = *forceUpdate;
                }
                if (oemObject)
                {
                    paramJson["Oem"] = oemObject;
                }
                data += paramJson.dump();
                data += "\r\n";
                BMCWEB_LOG_DEBUG("form data: {}", data);
            }
        }
    }

    if (!hasUpdateFile)
    {
        BMCWEB_LOG_ERROR("File with firmware image is missing.");
        messages::propertyMissing(asyncResp->res, "UpdateFile");
    }
    else
    {
        data += boundary.substr(2);
        data += "--\r\n";

        boost::urls::url url(sat->second);
        url.set_path(req.url().path());
        // Remove headers not handled for RFA firmware upgrade flow
        if (!req.getHeaderValue("Expect").empty())
        {
            BMCWEB_LOG_INFO("Removed Expect header from the request");
            req.clearHeader(boost::beast::http::field::expect);
        }
        BMCWEB_LOG_INFO("Expect header value {}", req.getHeaderValue("Expect"));
        client.sendDataWithCallback(std::move(data), url, req.fields(),
                                    boost::beast::http::verb::post, cb);
    }
}
#endif

/**
 * @brief Sets the ForceUpdate flag in the update policy.
 *
 * This function asynchronously updates the ForceUpdate flag in the software
 * update policy.
 *
 * @param[in] asyncResp - Pointer to the object holding the response data.
 * @param[in] objpath - D-Bus object path for the UpdatePolicy.
 * @param[in] forceUpdate - The boolean value to set for the ForceUpdate flag.
 * @param[in] callback - A callback function to be called after the ForceUpdate
 * update policy is changed. This is an optional parameter with a default value
 * of an empty function.
 *
 * @return None
 */
inline void setForceUpdate(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& objpath, const bool forceUpdate,
                           const std::function<void()>& callback = {})
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, forceUpdate, objpath, callback](
            const boost::system::error_code errorCode,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) mutable {
        if (errorCode)
        {
            BMCWEB_LOG_ERROR("error_code = {}", errorCode);
            BMCWEB_LOG_ERROR("error msg = {}", errorCode.message());
            if (asyncResp)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }
        // Check if only one service implements
        // xyz.openbmc_project.Software.UpdatePolicy
        if (objInfo.size() != 1)
        {
            BMCWEB_LOG_ERROR(
                "Expected exactly one service implementing xyz.openbmc_project.Software.UpdatePolicy, but found {} services.",
                objInfo.size());
            if (asyncResp)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }
        crow::connections::systemBus->async_method_call(
            [asyncResp,
             callback](const boost::system::error_code errCodePolicy) {
            if (errCodePolicy)
            {
                BMCWEB_LOG_ERROR("error_code = {}", errCodePolicy);
                messages::internalError(asyncResp->res);
                return;
            }

            if (callback)
            {
                callback();
            }
        },
            objInfo[0].first, objpath, "org.freedesktop.DBus.Properties", "Set",
            "xyz.openbmc_project.Software.UpdatePolicy", "ForceUpdate",
            dbus::utility::DbusVariantType(forceUpdate));
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject",
        "/xyz/openbmc_project/software",
        std::array<const char*, 1>{
            "xyz.openbmc_project.Software.UpdatePolicy"});
}

/**
 * @brief Process multipart form data
 *
 * @param[in] req  HTTP request
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] parser  MultipartParser
 *
 * @return None
 */
inline void processMultipartFormData(
    crow::Request& req, const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const MultipartParser& parser)
{
    std::optional<std::string> applyTime;
    std::optional<bool> forceUpdate;
    std::optional<std::vector<std::string>> targets;
    std::optional<std::string> oemUpdateOption;
    bool hasUpdateParameters = false;
    bool hasFile = false;

    if (!parseMultipartForm(asyncResp, parser, hasUpdateParameters, targets,
                            applyTime, forceUpdate, oemUpdateOption, hasFile))
    {
        return;
    }

    if (!validateUpdateParametersFormData(asyncResp, hasUpdateParameters,
                                          applyTime, oemUpdateOption))
    {
        return;
    }

    if (!validateUpdateFileFormData(asyncResp, hasFile))
    {
        return;
    }

    std::vector<std::string> uriTargets{*targets};
#ifdef BMCWEB_ENABLE_REDFISH_AGGREGATION
    bool updateAll = false;
    uint8_t count = 0;
    std::string rfaPrefix = redfishAggregationPrefix;
    if (uriTargets.size() > 0)
    {
        for (const auto& uri : uriTargets)
        {
            std::string file = std::filesystem::path(uri).filename();
            std::string prefix = rfaPrefix + "_";
            if (file.starts_with(prefix))
            {
                count++;
            }

            auto parsed = boost::urls::parse_relative_ref(uri);
            if (!parsed)
            {
                BMCWEB_LOG_DEBUG("Couldn't parse URI from resource ", uri);
                return;
            }

            boost::urls::url_view thisUrl = *parsed;

            // this is the Chassis resource from satellite BMC for all component
            // firmware update.
            if (crow::utility::readUrlSegments(thisUrl, "redfish", "v1",
                                               "Chassis", rfaHmcUpdateTarget))
            {
                updateAll = true;
            }
        }
        // There is one URI at least for satellite BMC.
        if (count > 0)
        {
            // further check if there is mixed targets and some are not
            // for satellite BMC.
            if (count != uriTargets.size())
            {
                boost::urls::url_view targetURL("Target");
                messages::invalidObject(asyncResp->res, targetURL);
            }
            else
            {
                // All URIs in Target has the prepended prefix
                BMCWEB_LOG_DEBUG("forward image {}", uriTargets[0]);

                // clear up the body buffer of the request to save memory
                req.clearBody();
                RedfishAggregator::getSatelliteConfigs(std::bind_front(
                    forwardImage, req, parser, updateAll, asyncResp));
            }
            return;
        }
    }
    // the update request is for BMC so only allow one FW update at a time
    if (fwUpdateInProgress != false)
    {
        if (asyncResp)
        {
            // don't copy the image, update already in progress.
            std::string resolution =
                "Another update is in progress. Retry"
                " the update operation once it is complete.";
            redfish::messages::updateInProgressMsg(asyncResp->res, resolution);
            BMCWEB_LOG_ERROR("Update already in progress.");
        }
        return;
    }
#endif

    auto sharedReq = std::make_shared<const crow::Request>(req);

    setForceUpdate(asyncResp, "/xyz/openbmc_project/software",
                   forceUpdate.value_or(false),
                   [sharedReq, asyncResp, uriTargets, oemUpdateOption]() {
        areTargetsUpdateable(sharedReq, asyncResp, uriTargets, oemUpdateOption);
    });
}

/**
 * @brief Check whether an update can be processed.
 *
 * @param[in] req  HTTP request
 * @param[in] asyncResp Pointer to object holding response data
 *
 * @return Returns true when the firmware can be applied.
 */
inline bool preCheckMultipartUpdateServiceReq(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    bool enableFWInProgCheck)
{
    if (req.body().size() > firmwareImageLimitBytes)
    {
        if (asyncResp)
        {
            BMCWEB_LOG_ERROR("Large image size: {}", req.body().size());
            std::string resolution =
                "Firmware package size is greater than allowed "
                "size. Make sure package size is less than "
                "UpdateService.MaxImageSizeBytes property and "
                "retry the firmware update operation.";
            messages::payloadTooLarge(asyncResp->res, resolution);
        }
        return false;
    }

    // Only allow one FW update at a time
    if (enableFWInProgCheck && fwUpdateInProgress != false)
    {
        if (asyncResp)
        {
            // don't copy the image, update already in progress.
            std::string resolution =
                "Another update is in progress. Retry"
                " the update operation once it is complete.";
            redfish::messages::updateInProgressMsg(asyncResp->res, resolution);
            BMCWEB_LOG_ERROR("Update already in progress.");
        }
        return false;
    }

    std::error_code spaceInfoError;
    const std::filesystem::space_info spaceInfo =
        std::filesystem::space(updateServiceImageLocation, spaceInfoError);
    if (!spaceInfoError)
    {
        if (spaceInfo.free < req.body().size())
        {
            BMCWEB_LOG_ERROR(
                "Insufficient storage space. Required: {} Available: {}",
                req.body().size(), spaceInfo.free);
            std::string resolution =
                "Reset the baseboard and retry the operation.";
            messages::insufficientStorage(asyncResp->res, resolution);
            return false;
        }
    }

    return true;
}

/**
 * @brief POST handler for Multipart Update Service
 *
 * @param[in] app App
 * @param[in] req  HTTP request
 * @param[in] asyncResp  Pointer to object holding response data
 *
 * @return None
 */
inline void handleMultipartUpdateServicePost(
    App& app, crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    BMCWEB_LOG_DEBUG(
        "Execute HTTP POST method '/redfish/v1/UpdateService/update-multipart/'");

    bool enableFWInProgCheck = true;
#ifdef BMCWEB_ENABLE_REDFISH_AGGREGATION
    // This is the flag to check BMC firmware update.
    // Parse the multipart payload and then learn satBMC or BMC firmware update
    // So UpdateInProgress will be checking at the later stage.
    enableFWInProgCheck = false;
#endif
    if (!preCheckMultipartUpdateServiceReq(req, asyncResp, enableFWInProgCheck))
    {
        return;
    }

    MultipartParser parser;
    ParserError ec = parser.parse(req);
    if (ec == ParserError::ERROR_BOUNDARY_FORMAT)
    {
        BMCWEB_LOG_ERROR("The request has unsupported media type");
        messages::unsupportedMediaType(asyncResp->res);

        return;
    }
    if (ec != ParserError::PARSER_SUCCESS)
    {
        // handle error
        BMCWEB_LOG_ERROR("MIME parse failed, ec : {}", static_cast<int>(ec));
        messages::internalError(asyncResp->res);
        return;
    }
    processMultipartFormData(req, asyncResp, parser);
}

inline void doHTTPUpdate(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const crow::Request& req)
{
    if constexpr (BMCWEB_REDFISH_UPDATESERVICE_USE_DBUS)
    {
        task::Payload payload(req);
        // HTTP push only supports BMC updates (with ApplyTime as immediate) for
        // backwards compatibility. Specific component updates will be handled
        // through Multipart form HTTP push.
        std::vector<std::string> targets;
        targets.emplace_back(BMCWEB_REDFISH_MANAGER_URI_NAME);

        processUpdateRequest(
            asyncResp, std::move(payload), req.body(),
            "xyz.openbmc_project.Software.ApplyTime.RequestedApplyTimes.Immediate",
            targets);
    }
    else
    {
        auto sharedReq = std::make_shared<const crow::Request>(req);

        setForceUpdate(asyncResp, "/xyz/openbmc_project/software", true,
                       [asyncResp, sharedReq]() mutable {
            setOemUpdateOption(asyncResp, "StageAndActivate",
                               [asyncResp, sharedReq]() mutable {
                std::string filepath(updateServiceImageLocation +
                                     boost::uuids::to_string(
                                         boost::uuids::random_generator()()));

                monitorForSoftwareAvailable(asyncResp, *sharedReq,
                                            fwObjectCreationDefaultTimeout,
                                            filepath);

                BMCWEB_LOG_DEBUG("Writing file to {}", filepath);
                std::ofstream out(filepath, std::ofstream::out |
                                                std::ofstream::binary |
                                                std::ofstream::trunc);
                out << sharedReq->body();
                out.close();
                BMCWEB_LOG_DEBUG("file upload complete!!");
            });
        });
    }
}

inline void
    handleUpdateServicePost(App& app, const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    BMCWEB_LOG_DEBUG(
        "Execute HTTP POST method '/redfish/v1/UpdateService/update/'");

    if (!preCheckMultipartUpdateServiceReq(req, asyncResp, true))
    {
        return;
    }
    doHTTPUpdate(asyncResp, req);
}

class BMCStatusAsyncResp
{
  public:
    BMCStatusAsyncResp(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) :
        asyncResp(asyncResp)
    {}

    ~BMCStatusAsyncResp()
    {
        if (bmcStateString == "xyz.openbmc_project.State.BMC.BMCState.Ready" &&
            hostStateString !=
                "xyz.openbmc_project.State.Host.HostState.TransitioningToRunning" &&
            hostStateString !=
                "xyz.openbmc_project.State.Host.HostState.TransitioningToOff" &&
            pldm_serviceStatus && mctp_serviceStatus)
        {
            asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
        }
        else
        {
            asyncResp->res.jsonValue["Status"]["State"] = "UnavailableOffline";
        }
#ifndef BMCWEB_DISABLE_CONDITIONS_ARRAY
        asyncResp->res.jsonValue["Status"]["Conditions"] =
            nlohmann::json::array();
#endif // BMCWEB_DISABLE_CONDITIONS_ARRAY
    }

    BMCStatusAsyncResp(const BMCStatusAsyncResp&) = delete;
    BMCStatusAsyncResp(BMCStatusAsyncResp&&) = delete;
    BMCStatusAsyncResp& operator=(const BMCStatusAsyncResp&) = delete;
    BMCStatusAsyncResp& operator=(BMCStatusAsyncResp&&) = delete;

    const std::shared_ptr<bmcweb::AsyncResp> asyncResp;
    bool pldm_serviceStatus = false;
    bool mctp_serviceStatus = false;
    std::string bmcStateString;
    std::string hostStateString;
};

inline void requestRoutesUpdateService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/")
        .privileges(redfish::privileges::getUpdateService)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        asyncResp->res.jsonValue["@odata.type"] =
            "#UpdateService.v1_11_0.UpdateService";
        asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/UpdateService";
        asyncResp->res.jsonValue["Id"] = "UpdateService";
        asyncResp->res.jsonValue["Description"] = "Service for Software Update";
        asyncResp->res.jsonValue["Name"] = "Update Service";

#ifdef BMCWEB_ENABLE_REDFISH_UPDATESERVICE_OLD_POST_URL
        // See note about later on in this file about why this is neccesary
        // This is "Wrong" per the standard, but is done temporarily to
        // avoid noise in failing tests as people transition to having this
        // option disabled
        if (!asyncResp->res.getHeaderValue("Allow").empty())
        {
            asyncResp->res.clearHeader(boost::beast::http::field::allow);
        }
        asyncResp->res.addHeader(boost::beast::http::field::allow,
                                 "GET, PATCH, HEAD");
#endif

        asyncResp->res.jsonValue["HttpPushUri"] =
            "/redfish/v1/UpdateService/update";

        // UpdateService cannot be disabled
        asyncResp->res.jsonValue["ServiceEnabled"] = true;

        asyncResp->res.jsonValue["MultipartHttpPushUri"] =
            "/redfish/v1/UpdateService/update-multipart";

        const nlohmann::json operationApplyTimeSupportedValues = {"Immediate"};

        asyncResp->res.jsonValue
            ["MultipartHttpPushUri@Redfish.OperationApplyTimeSupport"] = {
            {"@odata.type", "#Settings.v1_3_3.OperationApplyTimeSupport"},
            {"SupportedValues", operationApplyTimeSupportedValues}};

        asyncResp->res.jsonValue["FirmwareInventory"]["@odata.id"] =
            "/redfish/v1/UpdateService/FirmwareInventory";
        asyncResp->res.jsonValue["SoftwareInventory"] = {
            {"@odata.id", "/redfish/v1/UpdateService/SoftwareInventory"}};
        // Get the MaxImageSizeBytes
        asyncResp->res.jsonValue["MaxImageSizeBytes"] = firmwareImageLimitBytes;
        asyncResp->res
            .jsonValue["Actions"]["Oem"]["#NvidiaUpdateService.CommitImage"] = {
            {"target",
             "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.CommitImage"},
            {"@Redfish.ActionInfo",
             "/redfish/v1/UpdateService/Oem/Nvidia/CommitImageActionInfo"}};
        asyncResp->res.jsonValue["Actions"]["Oem"]["Nvidia"]
                                ["#NvidiaUpdateService.PublicKeyExchange"] = {
            {"target",
             "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.PublicKeyExchange"}};
        asyncResp->res.jsonValue
            ["Actions"]["Oem"]["Nvidia"]
            ["#NvidiaUpdateService.RevokeAllRemoteServerPublicKeys"] = {
            {"target",
             "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.RevokeAllRemoteServerPublicKeys"}};

        if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
        {
            asyncResp->res.jsonValue["Oem"]["Nvidia"] = {
                {"@odata.type",
                 "#NvidiaUpdateService.v1_3_0.NvidiaUpdateService"},
                {"MultipartHttpPushUriOptions",
                 {{"UpdateOptionSupport",
#ifdef BMCWEB_ENABLE_NVIDIA_UPDATE_STAGING
                   {"StageAndActivate", "StageOnly"}}}
#else
                   {"StageAndActivate"}}}
#endif
                }
            };
            debug_token::getErasePolicy(asyncResp);
        }

#if defined(BMCWEB_INSECURE_ENABLE_REDFISH_FW_TFTP_UPDATE) ||                  \
    defined(BMCWEB_ENABLE_REDFISH_FW_SCP_UPDATE) ||                            \
    defined(BMCWEB_ENABLE_REDFISH_FW_HTTP_HTTPS_UPDATE)
        // Update Actions object.
        nlohmann::json& updateSvcSimpleUpdate =
            asyncResp->res.jsonValue["Actions"]["#UpdateService.SimpleUpdate"];
        updateSvcSimpleUpdate["target"] =
            "/redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate";
        updateSvcSimpleUpdate["TransferProtocol@Redfish.AllowableValues"] = {};

        if constexpr (BMCWEB_INSECURE_PUSH_STYLE_NOTIFICATION)
        {
            updateSvcSimpleUpdate["TransferProtocol@Redfish.AllowableValues"] +=
                "TFTP";
        }

#ifdef BMCWEB_ENABLE_REDFISH_FW_SCP_UPDATE
        updateSvcSimpleUpdate["TransferProtocol@Redfish.AllowableValues"] +=
            "SCP";
#endif
#ifdef BMCWEB_ENABLE_REDFISH_FW_HTTP_HTTPS_UPDATE
        updateSvcSimpleUpdate["TransferProtocol@Redfish.AllowableValues"] +=
            "HTTP";
        updateSvcSimpleUpdate["TransferProtocol@Redfish.AllowableValues"] +=
            "HTTPS";
#endif
#endif
        asyncResp->res.jsonValue["HttpPushUriOptions"]["HttpPushUriApplyTime"]
                                ["ApplyTime"] = "Immediate";

        auto getUpdateStatus = std::make_shared<BMCStatusAsyncResp>(asyncResp);
        crow::connections::systemBus->async_method_call(
            [asyncResp, getUpdateStatus](
                const boost::system::error_code errorCode,
                const std::vector<std::pair<
                    std::string, std::vector<std::string>>>& objInfo) mutable {
            if (errorCode)
            {
                BMCWEB_LOG_ERROR("error_code = {}", errorCode);
                BMCWEB_LOG_ERROR("error msg = ", errorCode.message());
                if (asyncResp)
                {
                    messages::internalError(asyncResp->res);
                }
                getUpdateStatus->pldm_serviceStatus = false;
                return;
            }
            getUpdateStatus->pldm_serviceStatus = true;

            // Ensure we only got one service back
            if (objInfo.size() != 1)
            {
                BMCWEB_LOG_ERROR("Invalid Object Size ", objInfo.size());
                if (asyncResp)
                {
                    messages::internalError(asyncResp->res);
                }
                return;
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec,
                            GetManagedPropertyType& resp) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("error_code = {}", ec);
                    BMCWEB_LOG_ERROR("error msg = ", ec.message());
                    messages::internalError(asyncResp->res);
                    return;
                }

                for (auto& propertyMap : resp)
                {
                    if (propertyMap.first == "Targets")
                    {
                        auto targets = std::get_if<
                            std::vector<sdbusplus::message::object_path>>(
                            &propertyMap.second);
                        if (targets)
                        {
                            std::vector<std::string> pushURITargets;
                            for (auto& target : *targets)
                            {
                                std::string firmwareId = target.filename();
                                if (firmwareId.empty())
                                {
                                    BMCWEB_LOG_ERROR(
                                        "Unable to parse firmware ID");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                pushURITargets.push_back(
                                    "/redfish/v1/UpdateService/FirmwareInventory/" +
                                    firmwareId);
                            }
                            asyncResp->res.jsonValue["HttpPushUriTargets"] =
                                pushURITargets;
                        }
                    }
                }
                return;
            },
                objInfo[0].first, "/xyz/openbmc_project/software",
                "org.freedesktop.DBus.Properties", "GetAll",
                "xyz.openbmc_project.Software.UpdatePolicy");
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetObject",
            "/xyz/openbmc_project/software",
            std::array<const char*, 1>{
                "xyz.openbmc_project.Software.UpdatePolicy"});

        crow::connections::systemBus->async_method_call(
            [getUpdateStatus](boost::system::error_code ec,
                              const dbus::utility::MapperGetSubTreeResponse&
                                  subtree) mutable {
            if (ec || !subtree.size())
            {
                getUpdateStatus->mctp_serviceStatus = false;
            }
            else
            {
                getUpdateStatus->mctp_serviceStatus = true;
            }
            return;
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/mctp/0", 0,
            std::array<const char*, 1>{"xyz.openbmc_project.MCTP.Endpoint"});

        sdbusplus::asio::getProperty<std::string>(
            *crow::connections::systemBus, "xyz.openbmc_project.State.BMC",
            "/xyz/openbmc_project/state/bmc0", "xyz.openbmc_project.State.BMC",
            "CurrentBMCState",
            [getUpdateStatus](const boost::system::error_code ec,
                              const std::string& bmcState) mutable {
            if (ec)
            {
                return;
            }

            getUpdateStatus->bmcStateString = bmcState;
            return;
        });

        sdbusplus::asio::getProperty<std::string>(
            *crow::connections::systemBus, "xyz.openbmc_project.State.Host",
            "/xyz/openbmc_project/state/host0",
            "xyz.openbmc_project.State.Host", "CurrentHostState",
            [getUpdateStatus](const boost::system::error_code ec,
                              const std::string& hostState) mutable {
            if (ec)
            {
                return;
            }

            getUpdateStatus->hostStateString = hostState;
            return;
        });
    });
    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/")
        .privileges(redfish::privileges::patchUpdateService)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        BMCWEB_LOG_DEBUG("doPatch...");

        std::optional<nlohmann::json> pushUriOptions;
        std::optional<std::vector<std::string>> imgTargets;
        std::optional<bool> erasePolicy;
        if (!json_util::readJsonPatch(
                req, asyncResp->res, "HttpPushUriTargets", imgTargets,
                "Oem/Nvidia/AutomaticDebugTokenErased", erasePolicy))
        {
            BMCWEB_LOG_ERROR("UpdateService doPatch: Invalid request body");
            return;
        }

        if (erasePolicy)
        {
            debug_token::setErasePolicy(asyncResp, *erasePolicy);
        }

        if (imgTargets)
        {
            crow::connections::systemBus->async_method_call(
                [asyncResp, uriTargets{*imgTargets}](
                    const boost::system::error_code ec,
                    const std::vector<std::string>& swInvPaths) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("D-Bus responses error: {}", ec);
                    messages::internalError(asyncResp->res);
                    return;
                }

                std::vector<sdbusplus::message::object_path>
                    httpPushUriTargets = {};
                // validate TargetUris if entries are present
                if (uriTargets.size() != 0)
                {
                    std::vector<std::string> invalidTargets;
                    for (const std::string& target : uriTargets)
                    {
                        std::string compName =
                            std::filesystem::path(target).filename();
                        bool validTarget = false;
                        std::string objPath = "software/" + compName;
                        for (const std::string& path : swInvPaths)
                        {
                            std::size_t idPos = path.rfind(objPath);
                            if (idPos == std::string::npos)
                            {
                                continue;
                            }
                            std::string swId = path.substr(idPos);
                            if (swId == objPath)
                            {
                                sdbusplus::message::object_path objpath(path);
                                httpPushUriTargets.emplace_back(objpath);
                                validTarget = true;
                                break;
                            }
                        }
                        if (!validTarget)
                        {
                            invalidTargets.emplace_back(target);
                        }
                    }
                    // return HTTP400 - Bad request
                    // when none of the target filters are valid
                    if (invalidTargets.size() == uriTargets.size())
                    {
                        BMCWEB_LOG_ERROR("Targetted Device not Found!!");
                        messages::invalidObject(
                            asyncResp->res,
                            boost::urls::format("HttpPushUriTargets"));
                        return;
                    }
                    // return HTTP200 - Success with errors
                    // when there is partial valid targets
                    if (invalidTargets.size() > 0)
                    {
                        for (const std::string& invalidTarget : invalidTargets)
                        {
                            BMCWEB_LOG_ERROR("Invalid HttpPushUriTarget: {}",
                                             invalidTarget);
                            messages::propertyValueFormatError(
                                asyncResp->res, invalidTarget,
                                "HttpPushUriTargets");
                        }
                        asyncResp->res.result(boost::beast::http::status::ok);
                    }
                    // else all targets are valid
                }
                crow::connections::systemBus->async_method_call(
                    [asyncResp, httpPushUriTargets](
                        const boost::system::error_code errorCode,
                        const std::vector<
                            std::pair<std::string, std::vector<std::string>>>&
                            objInfo) mutable {
                    if (errorCode)
                    {
                        BMCWEB_LOG_ERROR("error_code = {}", errorCode);
                        BMCWEB_LOG_ERROR("error msg = {}", errorCode.message());
                        if (asyncResp)
                        {
                            messages::internalError(asyncResp->res);
                        }
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
                        return;
                    }

                    crow::connections::systemBus->async_method_call(
                        [asyncResp](
                            const boost::system::error_code errCodePolicy) {
                        if (errCodePolicy)
                        {
                            BMCWEB_LOG_ERROR("error_code = {}", errCodePolicy);
                            messages::internalError(asyncResp->res);
                        }
                        messages::success(asyncResp->res);
                    },
                        objInfo[0].first, "/xyz/openbmc_project/software",
                        "org.freedesktop.DBus.Properties", "Set",
                        "xyz.openbmc_project.Software.UpdatePolicy", "Targets",
                        dbus::utility::DbusVariantType(httpPushUriTargets));
                },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetObject",
                    "/xyz/openbmc_project/software",
                    std::array<const char*, 1>{
                        "xyz.openbmc_project.Software.UpdatePolicy"});
            },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                "/xyz/openbmc_project/software/", static_cast<int32_t>(0),
                std::array<std::string, 1>{
                    "xyz.openbmc_project.Software.Version"});
        }
    } // namespace redfish
        );
// The "old" behavior of the update service URI causes redfish-service validator
// failures when the Allow header is supported, given that in the spec,
// UpdateService does not allow POST.  in openbmc, we unfortunately reused that
// resource as our HttpPushUri as well.  A number of services, including the
// openbmc tests, and documentation have hardcoded that erroneous API, instead
// of relying on HttpPushUri as the spec requires.  This option will exist
// temporarily to allow the old behavior until Q4 2022, at which time it will be
// removed.
#ifdef BMCWEB_ENABLE_REDFISH_UPDATESERVICE_OLD_POST_URL
    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/")
        .privileges(redfish::privileges::postUpdateService)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        asyncResp->res.addHeader(
            boost::beast::http::field::warning,
            "299 - \"POST to /redfish/v1/UpdateService is deprecated. Use "
            "the value contained within HttpPushUri.\"");
        handleUpdateServicePost(app, req, asyncResp);
    });
#endif
    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/update/")
        .privileges(redfish::privileges::postUpdateService)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleUpdateServicePost, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/update-multipart/")
        .privileges(redfish::privileges::postUpdateService)
        .methods(boost::beast::http::verb::post)(
            [&app](
                crow::Request& req,
                const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) mutable {
        handleMultipartUpdateServicePost(app, req, asyncResp);
    });
} // namespace redfish

inline void requestRoutesSoftwareInventoryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/FirmwareInventory/")
        .privileges(redfish::privileges::getSoftwareInventoryCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        asyncResp->res.jsonValue["@odata.type"] =
            "#SoftwareInventoryCollection.SoftwareInventoryCollection";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/UpdateService/FirmwareInventory";
        asyncResp->res.jsonValue["Name"] = "Software Inventory Collection";

        crow::connections::systemBus->async_method_call(
            [asyncResp](
                const boost::system::error_code ec,
                const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("D-Bus response error on GetSubTree {}", ec);
                return;
            }

            if (subtree.empty())
            {
                return;
            }

            std::vector<std::string> pathNames;
            for (const auto& [object, serviceMap] : subtree)
            {
                sdbusplus::message::object_path path(object);
                std::string leaf = path.filename();
                if (leaf.empty())
                {
                    continue;
                }
                pathNames.push_back(leaf);
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp,
                 pathNames](const boost::system::error_code ec,
                            const dbus::utility::MapperGetSubTreeResponse&
                                subtree) mutable {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("D-Bus response error on GetSubTree {}",
                                     ec);
                    return;
                }

                for (const auto& [object, serviceMap] : subtree)
                {
                    sdbusplus::message::object_path path(object);
                    std::string leaf = path.filename();
                    if (leaf.empty() or
                        std::ranges::find(pathNames, leaf) != pathNames.end())
                    {
                        continue;
                    }
                    pathNames.push_back(leaf);
                }

                std::ranges::sort(pathNames, AlphanumLess<std::string>());
                nlohmann::json& members = asyncResp->res.jsonValue["Members"];
                members = nlohmann::json::array();

                for (const std::string& leaf : pathNames)
                {
                    boost::urls::url url = boost::urls::url(
                        "/redfish/v1/UpdateService/FirmwareInventory");
                    crow::utility::appendUrlPieces(url, leaf);
                    nlohmann::json::object_t member;
                    member["@odata.id"] = std::move(url);
                    members.emplace_back(std::move(member));
                }
                asyncResp->res.jsonValue["Members@odata.count"] =
                    members.size();
            },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/software/", int32_t(0),
                std::array<const char*, 2>{
                    "xyz.openbmc_project.State.Decorator.Health",
                    "xyz.openbmc_project.State.Decorator.OperationalStatus"});
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/software/", int32_t(0),
            std::array<const char*, 1>{"xyz.openbmc_project.Software.Version"});
    });
}

inline void requestRoutesInventorySoftwareCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/SoftwareInventory/")
        .privileges(redfish::privileges::getSoftwareInventoryCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        asyncResp->res.jsonValue["@odata.type"] =
            "#SoftwareInventoryCollection.SoftwareInventoryCollection";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/UpdateService/SoftwareInventory";
        asyncResp->res.jsonValue["Name"] = "Software Inventory Collection";

        crow::connections::systemBus->async_method_call(
            [asyncResp](
                const boost::system::error_code ec,
                const std::vector<std::pair<
                    std::string, std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>>&
                    subtree) {
            if (ec == boost::system::errc::io_error)
            {
                asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
                asyncResp->res.jsonValue["Members@odata.count"] = 0;
                return;
            }
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
            asyncResp->res.jsonValue["Members@odata.count"] = 0;

            for (auto& obj : subtree)
            {
                sdbusplus::message::object_path path(obj.first);
                std::string swId = path.filename();
                if (swId.empty())
                {
                    messages::internalError(asyncResp->res);
                    BMCWEB_LOG_DEBUG("Can't parse software ID!!");
                    return;
                }

                nlohmann::json& members = asyncResp->res.jsonValue["Members"];
                members.push_back(
                    {{"@odata.id",
                      "/redfish/v1/UpdateService/SoftwareInventory/" + swId}});
                asyncResp->res.jsonValue["Members@odata.count"] =
                    members.size();
            }
        },
            // Note that only firmware levels associated with a device
            // are stored under /xyz/openbmc_project/inventory_software
            // therefore to ensure only real SoftwareInventory items are
            // returned, this full object path must be used here as input to
            // mapper
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/inventory_software", static_cast<int32_t>(0),
            std::array<const char*, 1>{"xyz.openbmc_project.Software.Version"});
    });
}

inline static bool validSubpath([[maybe_unused]] const std::string& objPath,
                                [[maybe_unused]] const std::string& objectPath)
{
    return false;
}

inline static bool relatedItemAlreadyPresent(const nlohmann::json& relatedItem,
                                             const std::string& itemPath)
{
    for (const auto& obj : relatedItem)
    {
        if (obj.contains("@odata.id") && obj["@odata.id"] == itemPath)
        {
            return true;
        }
    }
    return false;
}

inline static void
    getRelatedItemsDrive(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const sdbusplus::message::object_path& objPath)
{
    // Drive is expected to be under a Chassis
    crow::connections::systemBus->async_method_call(
        [aResp, objPath](const boost::system::error_code ec,
                         const std::vector<std::string>& objects) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error");
            return;
        }

        nlohmann::json& relatedItem = aResp->res.jsonValue["RelatedItem"];
        nlohmann::json& relatedItemCount =
            aResp->res.jsonValue["RelatedItem@odata.count"];

        for (const auto& object : objects)
        {
            if (!validSubpath(objPath.str, object))
            {
                continue;
            }

            sdbusplus::message::object_path path(object);
            relatedItem.push_back(
                {{"@odata.id", "/redfish/v1/"
                               "Systems/" +
                                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                   "/"
                                   "Storage/" +
                                   path.filename() + "/Drives/" +
                                   objPath.filename()}});
            break;
        }
        relatedItemCount = relatedItem.size();
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/inventory", 0,
        std::array<std::string, 1>{
            "xyz.openbmc_project.Inventory.Item.Storage"});
}

inline static void getRelatedItemsStorageController(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const sdbusplus::message::object_path& objPath)
{
    crow::connections::systemBus->async_method_call(
        [aResp, objPath](const boost::system::error_code ec,
                         const std::vector<std::string>& objects) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error");
            return;
        }

        for (const auto& object : objects)
        {
            if (!validSubpath(objPath.str, object))
            {
                continue;
            }

            sdbusplus::message::object_path path(object);

            crow::connections::systemBus->async_method_call(
                [aResp, objPath,
                 path](const boost::system::error_code errCodeController,
                       const dbus::utility::MapperGetSubTreeResponse& subtree) {
                if (errCodeController || !subtree.size())
                {
                    return;
                }
                nlohmann::json& relatedItem =
                    aResp->res.jsonValue["RelatedItem"];
                nlohmann::json& relatedItemCount =
                    aResp->res.jsonValue["RelatedItem@odata.count"];

                for (size_t i = 0; i < subtree.size(); ++i)
                {
                    if (subtree[i].first != objPath.str)
                    {
                        continue;
                    }

                    relatedItem.push_back(
                        {{"@odata.id",
                          "/redfish/v1/Systems/" +
                              std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                              "/Storage/" + path.filename() +
                              "#/StorageControllers/" + std::to_string(i)}});
                    break;
                }

                relatedItemCount = relatedItem.size();
            },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree", object,
                int32_t(0),
                std::array<const char*, 1>{"xyz.openbmc_project.Inventory."
                                           "Item.StorageController"});
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Item.Storage"});
}

inline static void getRelatedItemsPowerSupply(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::object_path& objPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath](const boost::system::error_code& errorCode,
                             std::variant<std::vector<std::string>>& resp) {
        if (errorCode)
        {
            BMCWEB_LOG_DEBUG("error_code = {}", errorCode);
            BMCWEB_LOG_DEBUG("error msg = {}", errorCode.message());
            return;
        }
        std::string chassisName = "chassis";
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            BMCWEB_LOG_ERROR("Invalid Object.");
            return;
        }
        for (const std::string& path : *data)
        {
            sdbusplus::message::object_path myLocalPath(path);
            chassisName = myLocalPath.filename();
        }
        nlohmann::json& relatedItem = asyncResp->res.jsonValue["RelatedItem"];
        nlohmann::json& relatedItemCount =
            asyncResp->res.jsonValue["RelatedItem@odata.count"];
        relatedItem.push_back(
            {{"@odata.id", "/redfish/v1/Chassis/" + chassisName +
                               "/PowerSubsystem/PowerSupplies/" +
                               objPath.filename()}});

        relatedItemCount = relatedItem.size();
        asyncResp->res.jsonValue["Description"] = "Power Supply image";
    },
        "xyz.openbmc_project.ObjectMapper", objPath.str + "/chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline static void getRelatedItemsPCIeDevice(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::object_path& objPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath](const boost::system::error_code& errorCode,
                             std::variant<std::vector<std::string>>& resp) {
        if (errorCode)
        {
            BMCWEB_LOG_DEBUG("error_code = {}", errorCode);
            BMCWEB_LOG_DEBUG("error msg = {}", errorCode.message());
            return;
        }
        std::string chassisName = "chassis";
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            BMCWEB_LOG_ERROR("Invalid Object.");
            return;
        }
        for (const std::string& path : *data)
        {
            sdbusplus::message::object_path myLocalPath(path);
            chassisName = myLocalPath.filename();
        }
        nlohmann::json& relatedItem = asyncResp->res.jsonValue["RelatedItem"];
        nlohmann::json& relatedItemCount =
            asyncResp->res.jsonValue["RelatedItem@odata.count"];
        relatedItem.push_back(
            {{"@odata.id", "/redfish/v1/Chassis/" + chassisName +
                               "/PCIeDevices/" + objPath.filename()}});

        relatedItemCount = relatedItem.size();
    },
        "xyz.openbmc_project.ObjectMapper", objPath.str + "/chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline static void
    getRelatedItemsSwitch(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const sdbusplus::message::object_path& objPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath](const boost::system::error_code& errorCode,
                             std::variant<std::vector<std::string>>& resp) {
        if (errorCode)
        {
            BMCWEB_LOG_DEBUG("error_code = {}", errorCode);
            BMCWEB_LOG_DEBUG("error msg = {}", errorCode.message());
            return;
        }
        std::string fabricName = "fabric";
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            BMCWEB_LOG_ERROR("Invalid Object.");
            return;
        }
        for (const std::string& path : *data)
        {
            sdbusplus::message::object_path myLocalPath(path);
            fabricName = myLocalPath.filename();
        }
        nlohmann::json& relatedItem = asyncResp->res.jsonValue["RelatedItem"];
        nlohmann::json& relatedItemCount =
            asyncResp->res.jsonValue["RelatedItem@odata.count"];
        relatedItem.push_back(
            {{"@odata.id", "/redfish/v1/Fabrics/" + fabricName + "/Switches/" +
                               objPath.filename()}});

        relatedItemCount = relatedItem.size();
    },
        "xyz.openbmc_project.ObjectMapper", objPath.str + "/fabrics",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline static void getRelatedItemsNetworkAdapter(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::object_path& objPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath](const boost::system::error_code& errorCode,
                             std::variant<std::vector<std::string>>& resp) {
        if (errorCode)
        {
            BMCWEB_LOG_ERROR("error_code = {}", errorCode);
            BMCWEB_LOG_ERROR("error msg = {}", errorCode.message());
            return;
        }
        std::string networAdapterChassisName = "Networkadapter";
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            BMCWEB_LOG_ERROR("Invalid Object.");
            return;
        }
        if (!data->empty())
        {
            sdbusplus::message::object_path myLocalPath(data->front());
            networAdapterChassisName = myLocalPath.filename();
        }
        nlohmann::json& relatedItem = asyncResp->res.jsonValue["RelatedItem"];
        nlohmann::json& relatedItemCount =
            asyncResp->res.jsonValue["RelatedItem@odata.count"];
        relatedItem.push_back(
            {{"@odata.id", "/redfish/v1/Chassis/" + networAdapterChassisName +
                               "/NetworkAdapters/" + objPath.filename()}});

        relatedItemCount = relatedItem.size();
    },
        "xyz.openbmc_project.ObjectMapper", objPath.str + "/parent_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline static void
    getRelatedItemsOther(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const sdbusplus::message::object_path& association)
{
    // Find supported device types.
    crow::connections::systemBus->async_method_call(
        [aResp, association](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objects) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("error_code = {}, error msg = {}", ec,
                             ec.message());
            return;
        }
        if (objects.empty())
        {
            return;
        }

        nlohmann::json& relatedItem = aResp->res.jsonValue["RelatedItem"];
        nlohmann::json& relatedItemCount =
            aResp->res.jsonValue["RelatedItem@odata.count"];

        for (const auto& object : objects)
        {
            for (const auto& interfaces : object.second)
            {
                if (interfaces == "xyz.openbmc_project.Inventory."
                                  "Item.Drive")
                {
                    getRelatedItemsDrive(aResp, association);
                }

                if (interfaces == "xyz.openbmc_project.Inventory."
                                  "Item.PCIeDevice")
                {
                    getRelatedItemsPCIeDevice(aResp, association);
                }

                if (interfaces == "xyz.openbmc_project."
                                  "Inventory."
                                  "Item.Accelerator" ||
                    interfaces == "xyz.openbmc_project."
                                  "Inventory.Item.Cpu")
                {
                    relatedItem.push_back(
                        {{"@odata.id",
                          "/redfish/v1/Systems/" +
                              std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                              "/Processors/" + association.filename()}});
                }

                if (interfaces == "xyz.openbmc_project.Inventory."
                                  "Item.Board" ||
                    interfaces == "xyz.openbmc_project.Inventory."
                                  "Item.Chassis")
                {
                    std::string itemPath = "/redfish/v1/Chassis/" +
                                           association.filename();
                    if (!relatedItemAlreadyPresent(relatedItem, itemPath))
                    {
                        relatedItem.push_back({{"@odata.id", itemPath}});
                    }
                }

                if (interfaces == "xyz.openbmc_project.Inventory."
                                  "Item.StorageController")
                {
                    getRelatedItemsStorageController(aResp, association);
                }
                if (interfaces == "xyz.openbmc_project.Inventory."
                                  "Item.PowerSupply")
                {
                    getRelatedItemsPowerSupply(aResp, association);
                }

                if (interfaces == "xyz.openbmc_project.Inventory."
                                  "Item.Switch")
                {
                    getRelatedItemsSwitch(aResp, association);
                }

                if (interfaces == "xyz.openbmc_project.Inventory."
                                  "Item.NetworkInterface")
                {
                    getRelatedItemsNetworkAdapter(aResp, association);
                }
            }
        }

        relatedItemCount = relatedItem.size();
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", association.str,
        std::array<const char*, 10>{
            "xyz.openbmc_project.Inventory.Item.PowerSupply",
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "xyz.openbmc_project.Inventory.Item.PCIeDevice",
            "xyz.openbmc_project.Inventory.Item.Switch",
            "xyz.openbmc_project.Inventory.Item.Cpu",
            "xyz.openbmc_project.Inventory.Item.Drive",
            "xyz.openbmc_project.Inventory.Item.Board",
            "xyz.openbmc_project.Inventory.Item.Chassis",
            "xyz.openbmc_project.Inventory.Item.StorageController",
            "xyz.openbmc_project.Inventory.Item.NetworkInterface"});
}

/*
    Fill related item links for Software with other purposes.
    Use other purpose for device level softwares.
*/
inline static void
    getRelatedItemsOthers(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                          const std::string& swId,
                          std::string inventoryPath = "")
{
    BMCWEB_LOG_DEBUG("getRelatedItemsOthers enter");

    if (inventoryPath.empty())
    {
        inventoryPath = "/xyz/openbmc_project/software/";
    }

    aResp->res.jsonValue["RelatedItem"] = nlohmann::json::array();
    aResp->res.jsonValue["RelatedItem@odata.count"] = 0;

    crow::connections::systemBus->async_method_call(
        [aResp, swId](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
        if (ec)
        {
            messages::internalError(aResp->res);
            return;
        }

        for (const std::pair<
                 std::string,
                 std::vector<std::pair<std::string, std::vector<std::string>>>>&
                 obj : subtree)
        {
            sdbusplus::message::object_path path(obj.first);
            if (path.filename() != swId)
            {
                continue;
            }

            if (obj.second.size() < 1)
            {
                continue;
            }
            crow::connections::systemBus->async_method_call(
                [aResp](const boost::system::error_code errCodeAssoc,
                        std::variant<std::vector<std::string>>& resp) {
                if (errCodeAssoc)
                {
                    BMCWEB_LOG_ERROR("error_code = {}, error msg = {}",
                                     errCodeAssoc, errCodeAssoc.message());
                    return;
                }

                std::vector<std::string>* associations =
                    std::get_if<std::vector<std::string>>(&resp);
                if ((associations == nullptr) || (associations->empty()))
                {
                    BMCWEB_LOG_ERROR("Zero association for the software");
                    return;
                }

                for (const std::string& association : *associations)
                {
                    if (association.empty())
                    {
                        continue;
                    }
                    sdbusplus::message::object_path associationPath(
                        association);

                    getRelatedItemsOther(aResp, associationPath);
                }
            },
                "xyz.openbmc_project.ObjectMapper", path.str + "/inventory",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Association", "endpoints");
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", inventoryPath, 0,
        std::array<const char*, 1>{"xyz.openbmc_project.Software.Version"});
}

/* Fill related item links (i.e. bmc, bios) in for inventory */
inline static void
    getRelatedItems(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                    const std::string& swId, const std::string& purpose)
{
    if (purpose == fw_util::biosPurpose)
    {
        nlohmann::json& relatedItem = asyncResp->res.jsonValue["RelatedItem"];
        relatedItem.push_back(
            {{"@odata.id", "/redfish/v1/Systems/" +
                               std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                               "/Bios"}});
        asyncResp->res.jsonValue["Members@odata.count"] = relatedItem.size();
    }
    else if (purpose == fw_util::otherPurpose || purpose == fw_util::bmcPurpose)
    {
        getRelatedItemsOthers(asyncResp, swId);
    }
    else
    {
        BMCWEB_LOG_DEBUG("Unknown software purpose {}", purpose);
    }
}

inline void
    getSoftwareVersion(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& service, const std::string& path,
                       const std::string& swId)
{
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.Software.Version",
        [asyncResp,
         swId](const boost::system::error_code& ec,
               const dbus::utility::DBusPropertiesMap& propertiesList) {
        if (ec)
        {
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
    });
}

/**
 * @brief compute digest method handler invoke retimer hash computation
 *
 * @param[in] req - http request
 * @param[in] asyncResp - http response
 * @param[in] hashComputeObjPath - hash object path
 * @param[in] swId - software id
 */
inline void computeDigest(const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& hashComputeObjPath,
                          const std::string& swId)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, req, hashComputeObjPath, swId](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to GetObject for ComputeDigest: {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }
        // Ensure we only got one service back
        if (objInfo.size() != 1)
        {
            BMCWEB_LOG_ERROR("Invalid Object Size {}", objInfo.size());
            messages::internalError(asyncResp->res);
            return;
        }
        const std::string hashComputeService = objInfo[0].first;
        unsigned retimerId;
        try
        {
            // TODO this needs moved to from_chars
            retimerId = static_cast<unsigned>(
                std::stoul(swId.substr(swId.rfind("_") + 1)));
        }
        catch (const std::exception& e)
        {
            BMCWEB_LOG_ERROR("Error while parsing retimer Id: {}", e.what());
            messages::internalError(asyncResp->res);
            return;
        }
        // callback to reset hash compute state for timeout scenario
        auto timeoutCallback = [](const std::string_view state, size_t index) {
            nlohmann::json message{};
            if (state == "Started")
            {
                message = messages::taskStarted(std::to_string(index));
            }
            else if (state == "Aborted")
            {
                computeDigestInProgress = false;
                message = messages::taskAborted(std::to_string(index));
            }
            return message;
        };
        // create a task to wait for the hash digest property changed signal
        std::shared_ptr<task::TaskData> task = task::TaskData::createTask(
            [hashComputeObjPath, hashComputeService](
                boost::system::error_code ec, sdbusplus::message::message& msg,
                const std::shared_ptr<task::TaskData>& taskData) {
            if (ec)
            {
                if (ec != boost::asio::error::operation_aborted)
                {
                    taskData->state = "Aborted";
                    taskData->messages.emplace_back(
                        messages::resourceErrorsDetectedFormatError(
                            "NvidiaSoftwareInventory.ComputeDigest",
                            ec.message()));
                    taskData->finishTask();
                }
                computeDigestInProgress = false;
                return task::completed;
            }

            std::string interface;
            std::map<std::string, dbus::utility::DbusVariantType> props;

            msg.read(interface, props);
            if (interface == hashComputeInterface)
            {
                auto it = props.find("Digest");
                if (it == props.end())
                {
                    BMCWEB_LOG_ERROR("Signal doesn't have Digest value");
                    return !task::completed;
                }
                auto value = std::get_if<std::string>(&(it->second));
                if (!value)
                {
                    BMCWEB_LOG_ERROR("Digest value is not a string");
                    return !task::completed;
                }

                if (!(value->empty()))
                {
                    std::string hashDigestValue = *value;
                    crow::connections::systemBus->async_method_call(
                        [taskData, hashDigestValue](
                            const boost::system::error_code ec,
                            const std::variant<std::string>& property) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR(
                                "DBUS response error for Algorithm");
                            taskData->state = "Exception";
                            taskData->messages.emplace_back(
                                messages::taskAborted(
                                    std::to_string(taskData->index)));
                            return;
                        }
                        const std::string* hashAlgoValue =
                            std::get_if<std::string>(&property);
                        if (hashAlgoValue == nullptr)
                        {
                            BMCWEB_LOG_ERROR(
                                "Null value returned for Algorithm");
                            taskData->state = "Exception";
                            taskData->messages.emplace_back(
                                messages::taskAborted(
                                    std::to_string(taskData->index)));
                            return;
                        }

                        nlohmann::json jsonResponse;
                        jsonResponse["FirmwareDigest"] = hashDigestValue;
                        jsonResponse["FirmwareDigestHashingAlgorithm"] =
                            *hashAlgoValue;
                        taskData->taskResponse.emplace(jsonResponse);
                        std::string location =
                            "Location: /redfish/v1/TaskService/Tasks/" +
                            std::to_string(taskData->index) + "/Monitor";
                        taskData->payload->httpHeaders.emplace_back(
                            std::move(location));
                        taskData->state = "Completed";
                        taskData->percentComplete = 100;
                        taskData->messages.emplace_back(
                            messages::taskCompletedOK(
                                std::to_string(taskData->index)));
                        taskData->finishTask();
                    },
                        hashComputeService, hashComputeObjPath,
                        "org.freedesktop.DBus.Properties", "Get",
                        hashComputeInterface, "Algorithm");
                    computeDigestInProgress = false;
                    return task::completed;
                }
                else
                {
                    BMCWEB_LOG_ERROR("GetHash failed. Digest is empty.");
                    taskData->state = "Exception";
                    taskData->messages.emplace_back(
                        messages::resourceErrorsDetectedFormatError(
                            "NvidiaSoftwareInventory.ComputeDigest",
                            "Hash Computation Failed"));
                    taskData->finishTask();
                    computeDigestInProgress = false;
                    return task::completed;
                }
            }
            return !task::completed;
        },
            "type='signal',member='PropertiesChanged',"
            "interface='org.freedesktop.DBus.Properties',"
            "path='" +
                hashComputeObjPath + "',",
            timeoutCallback);
        task->startTimer(std::chrono::seconds(retimerHashMaxTimeSec));
        task->populateResp(asyncResp->res);
        task->payload.emplace(req);
        computeDigestInProgress = true;
        crow::connections::systemBus->async_method_call(
            [task](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Failed to ComputeDigest: {}", ec);
                task->state = "Aborted";
                task->messages.emplace_back(
                    messages::resourceErrorsDetectedFormatError(
                        "NvidiaSoftwareInventory.ComputeDigest", ec.message()));
                task->finishTask();
                computeDigestInProgress = false;
                return;
            }
        },
            hashComputeService, hashComputeObjPath, hashComputeInterface,
            "GetHash", retimerId);
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", hashComputeObjPath,
        std::array<const char*, 1>{hashComputeInterface.c_str()});
}

/**
 * @brief post handler for compute digest method
 *
 * @param req
 * @param asyncResp
 * @param swId
 */
inline void
    handlePostComputeDigest(const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& swId)
{
    crow::connections::systemBus->async_method_call(
        [req, asyncResp, swId](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
        if (ec)
        {
            messages::resourceNotFound(
                asyncResp->res, "NvidiaSoftwareInventory.ComputeDigest", swId);
            BMCWEB_LOG_ERROR("Invalid object path: {}", ec);
            return;
        }
        for (auto& obj : subtree)
        {
            sdbusplus::message::object_path hashPath(obj.first);
            std::string hashId = hashPath.filename();
            if (hashId == swId)
            {
                computeDigest(req, asyncResp, hashPath, swId);
                return;
            }
        }
        messages::resourceNotFound(
            asyncResp->res, "NvidiaSoftwareInventory.ComputeDigest", swId);
        return;
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/com/Nvidia/ComputeHash", static_cast<int32_t>(0),
        std::array<const char*, 1>{hashComputeInterface.c_str()});
}

/**
 * @brief app handler for ComputeDigest action
 *
 * @param[in] app
 */
inline void requestRoutesComputeDigestPost(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/UpdateService/FirmwareInventory/<str>/Actions/Oem/"
             "NvidiaSoftwareInventory.ComputeDigest")
        .privileges(redfish::privileges::postUpdateService)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& param) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        BMCWEB_LOG_DEBUG("Enter NvidiaSoftwareInventory.ComputeDigest doPost");
        std::shared_ptr<std::string> swId =
            std::make_shared<std::string>(param);
        // skip input parameter validation

        // 1. Firmware update and retimer hash cannot run in parallel
        if (fwUpdateInProgress)
        {
            redfish::messages::updateInProgressMsg(
                asyncResp->res,
                "Retry the operation once firmware update operation is complete.");
            BMCWEB_LOG_ERROR(
                "Cannot execute ComputeDigest. Update firmware is in progress.");

            return;
        }
        // 2. Only one compute hash allowed at a time due to FPGA limitation
        if (computeDigestInProgress)
        {
            redfish::messages::resourceErrorsDetectedFormatError(
                asyncResp->res, "NvidiaSoftwareInventory.ComputeDigest",
                "Another ComputeDigest operation is in progress");
            BMCWEB_LOG_ERROR(
                "Cannot execute ComputeDigest. Another ComputeDigest is in progress.");
            return;
        }
        handlePostComputeDigest(req, asyncResp, *swId);
        BMCWEB_LOG_DEBUG("Exit NvidiaUpdateService.ComputeDigest doPost");
    });
}

/**
 * @brief update oem action with ComputeDigest for devices which supports hash
 * compute
 *
 * @param[in] asyncResp
 * @param[in] swId
 */
inline void updateOemActionComputeDigest(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& swId)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, swId](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
        if (ec)
        {
            // hash compute interface is not applicable, ignore for the
            // device
            return;
        }
        for (auto& obj : subtree)
        {
            sdbusplus::message::object_path hashPath(obj.first);
            std::string hashId = hashPath.filename();
            if (hashId == swId)
            {
                std::string computeDigestTarget =
                    "/redfish/v1/UpdateService/FirmwareInventory/" + swId +
                    "/Actions/Oem/NvidiaSoftwareInventory.ComputeDigest";
                asyncResp->res
                    .jsonValue["Actions"]["Oem"]
                              ["#NvidiaSoftwareInventory.ComputeDigest"] = {
                    {"target", computeDigestTarget}};
                break;
            }
        }
        return;
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/com/Nvidia/ComputeHash", static_cast<int32_t>(0),
        std::array<const char*, 1>{hashComputeInterface.c_str()});
}

inline void requestRoutesSoftwareInventory(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/FirmwareInventory/<str>/")
        .privileges(redfish::privileges::getSoftwareInventory)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& param) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::shared_ptr<std::string> swId =
            std::make_shared<std::string>(param);

        asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
            "/redfish/v1/UpdateService/FirmwareInventory/{}", *swId);

        constexpr std::array<std::string_view, 4> interfaces = {
            "xyz.openbmc_project.Software.Version",
            "xyz.openbmc_project.Software.Settings",
            "xyz.openbmc_project.State.Decorator.Health",
            "xyz.openbmc_project.State.Decorator.OperationalStatus"};
        dbus::utility::getSubTree(
            "/xyz/openbmc_project/software/", 0, interfaces,
            [asyncResp, swId](
                const boost::system::error_code& ec,
                const std::vector<std::pair<
                    std::string, std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>>&
                    subtree) {
            BMCWEB_LOG_DEBUG("doGet callback...");
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            // Ensure we find our input swId, otherwise return an
            // error
            bool foundVersionObject = false;
            bool foundStatusObject = false;
            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                     obj : subtree)
            {
                sdbusplus::message::object_path objPath(obj.first);
                if (boost::equals(objPath.filename(), *swId) != true)
                {
                    continue;
                }

                if (obj.second.size() < 1)
                {
                    continue;
                }

                foundVersionObject = true;

                std::string settingService{};
                std::string versionService{};
                std::string statusService{};
                for (const auto& [service, interface] : obj.second)
                {
                    if (std::ranges::find(
                            interface,
                            "xyz.openbmc_project.Software.Settings") !=
                        interface.end())
                    {
                        settingService = service;
                    }
                    if (std::ranges::find(
                            interface,
                            "xyz.openbmc_project.Software.Version") !=
                        interface.end())
                    {
                        versionService = service;
                    }
                    if (std::ranges::find(
                            interface,
                            "xyz.openbmc_project.State.Decorator.Health") !=
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
                    fw_util::getFwWriteProtectedStatus(asyncResp, swId,
                                                       settingService);
                }
                asyncResp->res.jsonValue["Id"] = *swId;

                if (!versionService.empty())
                {
                    crow::connections::systemBus->async_method_call(
                        [asyncResp,
                         swId](const boost::system::error_code errorCode,
                               const boost::container::flat_map<
                                   std::string, dbus::utility::DbusVariantType>&
                                   propertiesList) {
                        if (errorCode)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        boost::container::flat_map<
                            std::string,
                            dbus::utility::DbusVariantType>::const_iterator it =
                            propertiesList.find("Purpose");
                        if (it == propertiesList.end())
                        {
                            BMCWEB_LOG_DEBUG(
                                "Can't find property \"Version\"!");
                            messages::propertyMissing(asyncResp->res,
                                                      "Purpose");
                            return;
                        }
                        const std::string* swInvPurpose =
                            std::get_if<std::string>(&it->second);
                        if (swInvPurpose == nullptr)
                        {
                            BMCWEB_LOG_DEBUG(
                                "wrong types for property\"Purpose\"!");
                            messages::propertyValueTypeError(asyncResp->res, "",
                                                             "Purpose");
                            return;
                        }

                        BMCWEB_LOG_DEBUG("swInvPurpose = {}", *swInvPurpose);
                        it = propertiesList.find("Version");
                        if (it == propertiesList.end())
                        {
                            BMCWEB_LOG_DEBUG(
                                "Can't find property \"Version\"!");
                            messages::propertyMissing(asyncResp->res,
                                                      "Version");
                            return;
                        }

                        BMCWEB_LOG_DEBUG("Version found!");

                        const std::string* version =
                            std::get_if<std::string>(&it->second);

                        if (version == nullptr)
                        {
                            BMCWEB_LOG_DEBUG(
                                "Can't find property \"Version\"!");

                            messages::propertyValueTypeError(asyncResp->res, "",
                                                             "Version");
                            return;
                        }

                        it = propertiesList.find("Manufacturer");
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

                        it = propertiesList.find("SoftwareId");
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
                        it = propertiesList.find("Description");
                        asyncResp->res.jsonValue["Description"] = formatDesc +
                                                                  " image";

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

                        it = propertiesList.find("PrettyName");
                        if (it != propertiesList.end())
                        {
                            const std::string* foundName =
                                std::get_if<std::string>(&it->second);
                            if (foundName != nullptr && !foundName->empty())
                            {
                                asyncResp->res.jsonValue["Name"] = *foundName;
                            }
                        }
                    },
                        versionService, obj.first,
                        "org.freedesktop.DBus.Properties", "GetAll", "");
                }

                asyncResp->res.jsonValue["Status"]["Health"] = "OK";
#ifndef BMCWEB_DISABLE_HEALTH_ROLLUP
                asyncResp->res.jsonValue["Status"]["HealthRollup"] = "OK";
#endif // BMCWEB_DISABLE_HEALTH_ROLLUP
#ifndef BMCWEB_DISABLE_CONDITIONS_ARRAY
                asyncResp->res.jsonValue["Status"]["Conditions"] =
                    nlohmann::json::array();
#endif // BMCWEB_DISABLE_CONDITIONS_ARRAY

                if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                {
#ifdef BMCWEB_ENABLE_NVIDIA_UPDATE_STAGING
                    redfish::fw_util::getFWSlotInformation(asyncResp,
                                                           obj.first);
#endif
                }
                if (!statusService.empty())
                {
                    fw_util::getFwRecoveryStatus(asyncResp, swId,
                                                 statusService);
                }
                else
                {
                    asyncResp->res.jsonValue["Status"]["Health"] = "OK";
                }
            }

            if (!foundVersionObject and !foundStatusObject)
            {
                BMCWEB_LOG_ERROR("Input swID {} not found!", *swId);
                messages::resourceMissingAtURI(
                    asyncResp->res,
                    boost::urls::format(
                        "/redfish/v1/UpdateService/FirmwareInventory/{}",
                        *swId));
                return;
            }

            if (foundVersionObject)
            {
                asyncResp->res.jsonValue["Updateable"] = false;
                fw_util::getFwUpdateableStatus(asyncResp, swId);
            }

            asyncResp->res.jsonValue["@odata.type"] =
                "#SoftwareInventory.v1_4_0.SoftwareInventory";
            asyncResp->res.jsonValue["Name"] = "Software Inventory";

            if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
            {
                updateOemActionComputeDigest(asyncResp, *swId);
            }
        });
    });
}

inline void requestRoutesInventorySoftware(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/SoftwareInventory/<str>/")
        .privileges(redfish::privileges::getSoftwareInventory)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& param) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::string searchPath = "/xyz/openbmc_project/inventory_software/";
        std::shared_ptr<std::string> swId =
            std::make_shared<std::string>(param);

        crow::connections::systemBus->async_method_call(
            [asyncResp, swId, searchPath](
                const boost::system::error_code ec,
                const std::vector<std::pair<
                    std::string, std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>>&
                    subtree) {
            BMCWEB_LOG_DEBUG("doGet callback...");
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            // Ensure we find our input swId, otherwise return an
            // error
            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                     obj : subtree)
            {
                const std::string& path = obj.first;
                sdbusplus::message::object_path objPath(path);
                if (objPath.filename() != *swId)
                {
                    continue;
                }

                if (obj.second.size() < 1)
                {
                    continue;
                }

                asyncResp->res.jsonValue["Id"] = *swId;
                asyncResp->res.jsonValue["Status"]["Health"] = "OK";
#ifndef BMCWEB_DISABLE_HEALTH_ROLLUP
                asyncResp->res.jsonValue["Status"]["HealthRollup"] = "OK";
#endif // BMCWEB_DISABLE_HEALTH_ROLLUP
#ifndef BMCWEB_DISABLE_CONDITIONS_ARRAY
                asyncResp->res.jsonValue["Status"]["Conditions"] =
                    nlohmann::json::array();
#endif // BMCWEB_DISABLE_CONDITIONS_ARRAY
                crow::connections::systemBus->async_method_call(
                    [asyncResp, swId, path, searchPath](
                        const boost::system::error_code errorCode,
                        const boost::container::flat_map<
                            std::string, dbus::utility::DbusVariantType>&
                            propertiesList) {
                    if (errorCode)
                    {
                        BMCWEB_LOG_DEBUG("properties not found ");
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (const auto& property : propertiesList)
                    {
                        if (property.first == "Manufacturer")
                        {
                            const std::string* manufacturer =
                                std::get_if<std::string>(&property.second);
                            if (manufacturer != nullptr)
                            {
                                asyncResp->res.jsonValue["Manufacturer"] =
                                    *manufacturer;
                            }
                        }
                        else if (property.first == "Version")
                        {
                            const std::string* version =
                                std::get_if<std::string>(&property.second);
                            if (version != nullptr)
                            {
                                asyncResp->res.jsonValue["Version"] = *version;
                            }
                        }
                        else if (property.first == "Functional")
                        {
                            const bool* swInvFunctional =
                                std::get_if<bool>(&property.second);
                            if (swInvFunctional != nullptr)
                            {
                                BMCWEB_LOG_DEBUG(" Functinal {}",
                                                 *swInvFunctional);
                                if (*swInvFunctional)
                                {
                                    asyncResp->res
                                        .jsonValue["Status"]["State"] =
                                        "Enabled";
                                }
                                else
                                {
                                    asyncResp->res
                                        .jsonValue["Status"]["State"] =
                                        "Disabled";
                                }
                            }
                        }
                    }
                    getRelatedItemsOthers(asyncResp, *swId, searchPath);
                    fw_util::getFwUpdateableStatus(asyncResp, swId, searchPath);
                },
                    obj.second[0].first, obj.first,
                    "org.freedesktop.DBus.Properties", "GetAll", "");
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/UpdateService/SoftwareInventory/" + *swId;
                asyncResp->res.jsonValue["@odata.type"] =
                    "#SoftwareInventory.v1_4_0.SoftwareInventory";
                asyncResp->res.jsonValue["Name"] = "Software Inventory";
                return;
            }
            // Couldn't find an object with that name.  return an error
            BMCWEB_LOG_DEBUG("Input swID {} not found!", *swId);
            messages::resourceNotFound(
                asyncResp->res, "SoftwareInventory.v1_4_0.SoftwareInventory",
                *swId);
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree", searchPath,
            static_cast<int32_t>(0),
            std::array<const char*, 1>{"xyz.openbmc_project.Software.Version"});
    });

    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/FirmwareInventory/<str>/")
        .privileges(redfish::privileges::patchUpdateService)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& param) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        BMCWEB_LOG_DEBUG("doPatch...");
        std::shared_ptr<std::string> swId =
            std::make_shared<std::string>(param);

        std::optional<bool> writeProtected;
        if (!json_util::readJsonPatch(req, asyncResp->res, "WriteProtected",
                                      writeProtected))
        {
            return;
        }

        if (writeProtected)
        {
            crow::connections::systemBus->async_method_call(
                [asyncResp, swId, writeProtected](
                    const boost::system::error_code ec,
                    const std::vector<
                        std::pair<std::string,
                                  std::vector<std::pair<
                                      std::string, std::vector<std::string>>>>>&
                        subtree) {
                if (ec)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                for (const std::pair<
                         std::string,
                         std::vector<
                             std::pair<std::string, std::vector<std::string>>>>&
                         obj : subtree)
                {
                    const std::string& path = obj.first;
                    sdbusplus::message::object_path objPath(path);
                    if (objPath.filename() != *swId)
                    {
                        continue;
                    }

                    if (obj.second.size() < 1)
                    {
                        continue;
                    }
                    fw_util::patchFwWriteProtectedStatus(
                        asyncResp, swId, obj.second[0].first, *writeProtected);

                    return;
                }
                // Couldn't find an object with that name.  return an
                // error
                BMCWEB_LOG_DEBUG("Input swID {} not found!", *swId);
                messages::resourceNotFound(
                    asyncResp->res,
                    "SoftwareInventory.v1_4_0.SoftwareInventory", *swId);
            },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/software/", static_cast<int32_t>(0),
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Software.Settings"});
        }
    });
}

/**
 * @brief Check whether firmware inventory is allowable
 * The function gets allowable values from config file
 * /usr/share/bmcweb/fw_mctp_mapping.json.
 * and check if the firmware inventory is in this collection
 *
 * @param[in] inventoryPath - firmware inventory path.
 * @returns boolean value indicates whether firmware inventory
 * is allowable.
 */
inline bool isInventoryAllowableValue(const std::string_view inventoryPath)
{
    bool isAllowable = false;

    std::vector<CommitImageValueEntry> allowableValues = getAllowableValues();
    std::vector<CommitImageValueEntry>::iterator it =
        find(allowableValues.begin(), allowableValues.end(),
             static_cast<std::string>(inventoryPath));

    if (it != allowableValues.end())
    {
        isAllowable = true;
    }
    else
    {
        isAllowable = false;
    }

    return isAllowable;
}

#ifdef BMCWEB_ENABLE_REDFISH_AGGREGATION
/**
 * @brief  callback handler of JSON array object
 * the common function to get the JSON array object, espeically for
 * the response of CommitImageActionInfo from satBMC.
 *
 * @param[in] object JSON object
 * @param[in] name JSON name
 * @param[in] cb  The callback function
 *
 * @return None
 */
inline void getArrayObject(nlohmann::json::object_t* object,
                           const std::string_view name,
                           const std::function<void(nlohmann::json&)>& cb)
{
    for (std::pair<const std::string, nlohmann::json>& item : *object)
    {
        if (item.first != name)
        {
            continue;
        }
        auto* array = item.second.get_ptr<nlohmann::json::array_t*>();
        if (array == nullptr)
        {
            continue;
        }
        for (nlohmann::json& elm : *array)
        {
            cb(elm);
        }
    }
}

/**
 * @brief The response handler of CommitImageActionInfo from satBMC
 * aggregate the allowable values from the response of CommitImageActionInfo
 * if the response is successful.
 *
 * @param[in] asyncResp Shared pointer to the response message
 * @param[in] resp  HTTP response of satBMC
 *
 * @return None
 */
inline void commitImageActionInfoResp(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, crow::Response& resp)
{
    // Failed to get ActionInfo because of the error response
    // just return without any further processing for the aggregation.
    if ((resp.result() == boost::beast::http::status::too_many_requests) ||
        (resp.result() == boost::beast::http::status::bad_gateway))
    {
        return;
    }

    // The resp will not have a json component
    // We need to create a json from resp's stringResponse
    std::string_view contentType = resp.getHeaderValue("Content-Type");
    if (bmcweb::asciiIEquals(contentType, "application/json") ||
        bmcweb::asciiIEquals(contentType, "application/json; charset=utf-8"))
    {
        nlohmann::json jsonVal = nlohmann::json::parse(*resp.body(), nullptr,
                                                       false);
        if (jsonVal.is_discarded())
        {
            return;
        }
        nlohmann::json::object_t* object =
            jsonVal.get_ptr<nlohmann::json::object_t*>();
        if (object == nullptr)
        {
            BMCWEB_LOG_ERROR("Parsed JSON was not an object?");
            return;
        }

        auto cb = [asyncResp](nlohmann::json& item) mutable {
            auto allowValueCb = [asyncResp](nlohmann::json& item) mutable {
                auto* str = item.get_ptr<std::string*>();
                if (str == nullptr)
                {
                    BMCWEB_LOG_CRITICAL("Item is not a string");
                    return;
                }
                nlohmann::json& allowableValues =
                    asyncResp->res
                        .jsonValue["Parameters"][0]["AllowableValues"];

                allowableValues.push_back(*str);
            };

            auto* nestedObject = item.get_ptr<nlohmann::json::object_t*>();
            if (nestedObject == nullptr)
            {
                BMCWEB_LOG_CRITICAL("Nested object is null");
                return;
            }
            getArrayObject(nestedObject, std::string("AllowableValues"),
                           allowValueCb);
        };
        getArrayObject(object, std::string("Parameters"), cb);
    }
}

/**
 * @brief forward Commit Image Action Info request to satBMC.
 * the function will send the request to satBMC to get the CommitImageActionInfo
 * if the satellie BMC is available.
 *
 * @param[in] req  HTTP request
 * @param[in] asyncResp Shared pointer to the response message
 * @param[in] ec Error code
 * @param[in] satelliteInfo satellite BMC information
 *
 * @return None
 */
inline void forwardCommitImageActionInfo(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const std::unordered_map<std::string, boost::urls::url>& satelliteInfo)
{
    // Something went wrong while querying dbus
    if (ec)
    {
        BMCWEB_LOG_ERROR("Dbus query error for satellite BMC.");
        messages::internalError(asyncResp->res);
        return;
    }

    const auto& sat = satelliteInfo.find(redfishAggregationPrefix);
    if (sat == satelliteInfo.end())
    {
        BMCWEB_LOG_ERROR("satellite BMC is not there.");
        return;
    }

    crow::HttpClient client(
        *req.ioService,
        std::make_shared<crow::ConnectionPolicy>(getPostAggregationPolicy()));

    std::function<void(crow::Response&)> cb =
        std::bind_front(commitImageActionInfoResp, asyncResp);

    std::string data;
    boost::urls::url url(sat->second);
    url.set_path(req.url().path());

    client.sendDataWithCallback(std::move(data), url, req.fields(),
                                boost::beast::http::verb::get, cb);
}
#endif

/**
 * @brief Get allowable value for particular firmware inventory
 * The function gets allowable values from config file
 * /usr/share/bmcweb/fw_mctp_mapping.json.
 * and returns the allowable value if exists in the collection
 *
 * @param[in] inventoryPath - firmware inventory path.
 * @returns Pair of boolean value if the allowable value exists
 * and the object of AllowableValue who contains inventory path
 * and assigned to its MCTP EID.
 */
inline std::pair<bool, CommitImageValueEntry>
    getAllowableValue(const std::string_view inventoryPath)
{
    std::pair<bool, CommitImageValueEntry> result;

    std::vector<CommitImageValueEntry> allowableValues = getAllowableValues();
    std::vector<CommitImageValueEntry>::iterator it =
        find(allowableValues.begin(), allowableValues.end(),
             static_cast<std::string>(inventoryPath));

    if (it != allowableValues.end())
    {
        result.second = *it;
        result.first = true;
    }
    else
    {
        result.first = false;
    }

    return result;
}

/**
 * @brief Update parameters for GET Method CommitImageInfo
 *
 * @param[in] asyncResp Shared pointer to the response message
 * @param[in] subtree  Collection of objectmappers for
 * "/xyz/openbmc_project/software"
 *
 * @return None
 */
inline void updateParametersForCommitImageInfo(
    [[maybe_unused]] const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::vector<std::pair<
        std::string,
        std::vector<std::pair<std::string, std::vector<std::string>>>>>&
        subtree)
{
    asyncResp->res.jsonValue["Parameters"] = nlohmann::json::array();
    nlohmann::json& parameters = asyncResp->res.jsonValue["Parameters"];

    nlohmann::json parameterTargets;
    parameterTargets["Name"] = "Targets";
    parameterTargets["Required"] = false;
    parameterTargets["DataType"] = "StringArray";
    parameterTargets["AllowableValues"] = nlohmann::json::array();

    nlohmann::json& allowableValues = parameterTargets["AllowableValues"];

    for (auto& obj : subtree)
    {
        sdbusplus::message::object_path path(obj.first);
        std::string fwId = path.filename();
        if (fwId.empty())
        {
            messages::internalError(asyncResp->res);
            BMCWEB_LOG_DEBUG("Cannot parse firmware ID");
            return;
        }

        if (isInventoryAllowableValue(obj.first))
        {
            allowableValues.push_back(
                "/redfish/v1/UpdateService/FirmwareInventory/" + fwId);
        }
    }

    parameters.push_back(parameterTargets);

#ifdef BMCWEB_ENABLE_REDFISH_AGGREGATION
    RedfishAggregator::getSatelliteConfigs(
        std::bind_front(forwardCommitImageActionInfo, req, asyncResp));
#endif
}

#ifdef BMCWEB_ENABLE_REDFISH_AGGREGATION
/**
 * @brief forward Commit Image Post Request to satBMC.
 *
 *
 * @param[in] req  HTTP request
 * @param[in] asyncResp Shared pointer to the response message
 * @param[in] ec Error code
 * @param[in] satelliteInfo satellite BMC information
 *
 * @return None
 */
inline void forwardCommitImagePost(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const std::unordered_map<std::string, boost::urls::url>& satelliteInfo)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Dbus query error for satellite BMC.");
        messages::internalError(asyncResp->res);
        return;
    }

    const auto& sat = satelliteInfo.find(redfishAggregationPrefix);
    if (sat == satelliteInfo.end())
    {
        BMCWEB_LOG_ERROR("satBMC is not found");
        return;
    }

    crow::HttpClient client(
        *req.ioService,
        std::make_shared<crow::ConnectionPolicy>(getPostAggregationPolicy()));

    std::function<void(crow::Response&)> cb =
        std::bind_front(handleSatBMCResponse, asyncResp);

    std::string data = req.body();
    boost::urls::url url(sat->second);
    url.set_path(req.url().path());

    client.sendDataWithCallback(std::move(data), url, req.fields(),
                                boost::beast::http::verb::post, cb);
}

/**
 * @brief the response handler of CommitImage Post
 * the function will examine the targets of the request and send out
 * the request to the satellite BMC if the remote targets are present.
 *
 * @param[in] req  HTTP request
 * @param[in] asyncResp Shared pointer to the response message
 *
 * @return return true to pass request to the local. otherwise, don't pass.
 */

inline bool handleSatBMCCommitImagePost(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::optional<std::vector<std::string>> targets;

    if (!json_util::readJsonAction(req, asyncResp->res, "Targets", targets))
    {
        messages::createFailedMissingReqProperties(asyncResp->res, "Targets");
        BMCWEB_LOG_ERROR("Missing Targets of OemCommitImage API");
        return false;
    }

    bool hasTargets = false;

    if (targets && targets.value().empty() == false)
    {
        hasTargets = true;
    }

    if (hasTargets)
    {
        std::vector<std::string> targetsCollection = targets.value();

        std::string rfaPrefix = redfishAggregationPrefix;
        rfaPrefix += "_";

        bool prefix = false, noPrefix = false;
        for (auto& target : targetsCollection)
        {
            std::string file = std::filesystem::path(target).filename();
            if (file.starts_with(rfaPrefix))
            {
                prefix = true;
            }
            else
            {
                noPrefix = true;
            }
        }

        if (prefix && !noPrefix)
        {
            // targets with the prefix included only.
            RedfishAggregator::getSatelliteConfigs(
                std::bind_front(forwardCommitImagePost, req, asyncResp));

            // don't pass the request to the local
            return false;
        }
        else if (prefix && noPrefix)
        {
            // drop the request with mixed targets.
            boost::urls::url_view targetURL("Target");
            messages::invalidObject(asyncResp->res, targetURL);
            return false;
        }
    }
    else
    {
        RedfishAggregator::getSatelliteConfigs(
            std::bind_front(forwardCommitImagePost, req, asyncResp));
        // forward the request with empty target.
    }
    return true;
}
#endif

/**
 * @brief Handles request POST
 * The function triggers Commit Image action
 * for the list of delivered in the body of request
 * firmware inventories
 *
 * @param resp Async HTTP response.
 * @param asyncResp Pointer to object holding response data
 * @param[in] subtree  Collection of objectmappers for
 * "/xyz/openbmc_project/software"
 *
 * @return None
 */
inline void handleCommitImagePost(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::vector<std::pair<
        std::string,
        std::vector<std::pair<std::string, std::vector<std::string>>>>>&
        subtree)
{
    std::optional<std::vector<std::string>> targets;

    if (!json_util::readJsonAction(req, asyncResp->res, "Targets", targets))
    {
        return;
    }

    bool hasTargets = false;

    if (targets && targets.value().empty() == false)
    {
        hasTargets = true;
    }

    UuidToUriMap targetUuidInventoryUriMap = {};

    if (hasTargets)
    {
        std::vector<std::string> targetsCollection = targets.value();

        for (auto& target : targetsCollection)
        {
            sdbusplus::message::object_path objectPath(target);
            std::string inventoryPath = "/xyz/openbmc_project/software/" +
                                        objectPath.filename();
            std::pair<bool, CommitImageValueEntry> result =
                getAllowableValue(inventoryPath);
            if (result.first == true)
            {
                targetUuidInventoryUriMap[result.second.uuid] =
                    result.second.inventoryUri;
            }
            else
            {
                BMCWEB_LOG_DEBUG(
                    "Cannot find firmware inventory in allowable values");
                boost::urls::url_view targetURL(target);
                messages::resourceMissingAtURI(asyncResp->res, targetURL);
            }
        }
    }
    else
    {
        for (auto& obj : subtree)
        {
            std::pair<bool, CommitImageValueEntry> result =
                getAllowableValue(obj.first);

            if (result.first == true)
            {
                targetUuidInventoryUriMap[result.second.uuid] =
                    result.second.inventoryUri;
            }
        }
    }

    auto initBackgroundCopyCallback =
        [req, asyncResp]([[maybe_unused]] const UUID uuid, const EID eid,
                         const URI inventoryUri) mutable {
        BMCWEB_LOG_DEBUG("Run CommitImage operation for EID {}, UUID {}", eid,
                         uuid);
        initBackgroundCopy(req, asyncResp, eid, inventoryUri);
    };

    auto errorCallback =
        [req, asyncResp]([[maybe_unused]] const std::string desc,
                         [[maybe_unused]] const std::string errMsg) mutable {
        BMCWEB_LOG_ERROR("The CommitImage operation failed: {}, {}", desc,
                         errMsg);
        messages::internalError(asyncResp->res);
    };

    retrieveEidFromMctpServices(targetUuidInventoryUriMap,
                                initBackgroundCopyCallback, errorCallback);
}

/**
 * @brief Register Web Api endpoints for Commit Image functionality
 *
 * @return None
 */
inline void requestRoutesUpdateServiceCommitImage(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/UpdateService/Oem/Nvidia/CommitImageActionInfo/")
        .privileges(redfish::privileges::getSoftwareInventoryCollection)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        asyncResp->res.jsonValue["@odata.type"] =
            "#ActionInfo.v1_2_0.ActionInfo";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/UpdateService/Oem/Nvidia/CommitImageActionInfo";
        asyncResp->res.jsonValue["Name"] = "CommitImage Action Info";
        asyncResp->res.jsonValue["Id"] = "CommitImageActionInfo";

        crow::connections::systemBus->async_method_call(
            [asyncResp{asyncResp}, req](
                const boost::system::error_code ec,
                const std::vector<std::pair<
                    std::string, std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>>&
                    subtree) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            updateParametersForCommitImageInfo(req, asyncResp, subtree);
        },
            // Note that only firmware levels associated with a device
            // are stored under /xyz/openbmc_project/software therefore
            // to ensure only real FirmwareInventory items are returned,
            // this full object path must be used here as input to
            // mapper
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/software", static_cast<int32_t>(0),
            std::array<const char*, 1>{"xyz.openbmc_project.Software.Version"});
    });

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.CommitImage/")
        .privileges(redfish::privileges::postUpdateService)
        .methods(boost::beast::http::verb::post)(
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        BMCWEB_LOG_DEBUG("doPost...");

#ifdef BMCWEB_ENABLE_REDFISH_AGGREGATION
        if (!handleSatBMCCommitImagePost(req, asyncResp))
        {
            return;
        }
#endif

        if (fwUpdateInProgress == true)
        {
            redfish::messages::updateInProgressMsg(
                asyncResp->res,
                "Retry the operation once firmware update operation is complete.");

            // don't copy the image, update already in progress.
            BMCWEB_LOG_ERROR(
                "Cannot execute commit image. Update firmware is in progress.");

            return;
        }

        crow::connections::systemBus->async_method_call(
            [req, asyncResp{asyncResp}](
                const boost::system::error_code ec,
                const std::vector<std::pair<
                    std::string, std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>>&
                    subtree) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            handleCommitImagePost(req, asyncResp, subtree);
        },
            // Note that only firmware levels associated with a device
            // are stored under /xyz/openbmc_project/software therefore
            // to ensure only real FirmwareInventory items are returned,
            // this full object path must be used here as input to
            // mapper
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/software", static_cast<int32_t>(0),
            std::array<const char*, 1>{"xyz.openbmc_project.Software.Version"});
    });
}

/**
 * @brief POST handler for SSH public key exchange - user and remote server
 * authentication.
 *
 * @param app
 *
 * @return None
 */
inline nlohmann::json extendedInfoSuccessMsg(const std::string& msg,
                                             const std::string& arg)
{
    return nlohmann::json{{"@odata.type", "#Message.v1_1_1.Message"},
                          {"Message", msg},
                          {"MessageArgs", {arg}}};
}

inline void requestRoutesUpdateServicePublicKeyExchange(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.PublicKeyExchange/")
        .privileges(redfish::privileges::postUpdateService)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        std::string remoteServerIP;
        std::string remoteServerKeyString; // "<type> <key>"

        BMCWEB_LOG_DEBUG("Enter UpdateService.PublicKeyExchange doPost");

        if (!json_util::readJsonAction(req, asyncResp->res, "RemoteServerIP",
                                       remoteServerIP, "RemoteServerKeyString",
                                       remoteServerKeyString) &&
            (remoteServerIP.empty() || remoteServerKeyString.empty()))
        {
            std::string emptyprops;
            if (remoteServerIP.empty())
            {
                emptyprops += "RemoteServerIP ";
            }
            if (remoteServerKeyString.empty())
            {
                emptyprops += "RemoteServerKeyString ";
            }
            messages::createFailedMissingReqProperties(asyncResp->res,
                                                       emptyprops);
            BMCWEB_LOG_DEBUG("Missing {}", emptyprops);
            return;
        }

        BMCWEB_LOG_DEBUG("RemoteServerIP: {} RemoteServerKeyString: {}",
                         remoteServerIP, remoteServerKeyString);

        // Verify remoteServerKeyString matches the pattern "<type> <key>"
        std::string remoteServerKeyStringPattern = R"(\S+\s+\S+)";
        std::regex pattern(remoteServerKeyStringPattern);
        if (!std::regex_match(remoteServerKeyString, pattern))
        {
            // Invalid format, return an error message
            messages::actionParameterValueTypeError(
                asyncResp->res, remoteServerKeyString, "RemoteServerKeyString",
                "UpdateService.PublicKeyExchange");
            BMCWEB_LOG_DEBUG("Invalid RemoteServerKeyString format");
            return;
        }

        // Call SCP service
        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code ec) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                BMCWEB_LOG_ERROR("error_code = {} error msg = {}", ec,
                                 ec.message());
                return;
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec,
                            const std::string& selfPublicKeyStr) {
                if (ec || selfPublicKeyStr.empty())
                {
                    messages::internalError(asyncResp->res);
                    BMCWEB_LOG_ERROR("error_code = {} error msg = {}", ec,
                                     ec.message());
                    return;
                }

                // Create a JSON object with the additional information
                std::string keyMsg =
                    "Please add the following public key info to "
                    "~/.ssh/authorized_keys on the remote server";
                std::string keyInfo = selfPublicKeyStr + " root@dpu-bmc";

                asyncResp->res.jsonValue[messages::messageAnnotation] =
                    nlohmann::json::array();
                asyncResp->res.jsonValue[messages::messageAnnotation].push_back(
                    extendedInfoSuccessMsg(keyMsg, keyInfo));
                messages::success(asyncResp->res);
                BMCWEB_LOG_DEBUG("Call to PublicKeyExchange succeeded {}",
                                 selfPublicKeyStr);
            },
                "xyz.openbmc_project.Software.Download",
                "/xyz/openbmc_project/software",
                "xyz.openbmc_project.Common.SCP", "GenerateSelfKeyPair");
        },
            "xyz.openbmc_project.Software.Download",
            "/xyz/openbmc_project/software", "xyz.openbmc_project.Common.SCP",
            "AddRemoteServerPublicKey", remoteServerIP, remoteServerKeyString);
    });
}

/**
 * @brief POST handler for adding remote server SSH public key
 *
 * @param app
 *
 * @return None
 */
inline void requestRoutesUpdateServiceRevokeAllRemoteServerPublicKeys(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.RevokeAllRemoteServerPublicKeys/")
        .privileges(redfish::privileges::postUpdateService)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        std::string remoteServerIP;

        BMCWEB_LOG_DEBUG(
            "Enter UpdateService.RevokeAllRemoteServerPublicKeys doPost");

        if (!json_util::readJsonAction(req, asyncResp->res, "RemoteServerIP",
                                       remoteServerIP) &&
            remoteServerIP.empty())
        {
            messages::createFailedMissingReqProperties(asyncResp->res,
                                                       "RemoteServerIP");
            BMCWEB_LOG_DEBUG("Missing RemoteServerIP");
            return;
        }

        BMCWEB_LOG_DEBUG("RemoteServerIP: {}", remoteServerIP);

        // Call SCP service
        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code ec) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                BMCWEB_LOG_ERROR("error_code = {} error msg = {}", ec,
                                 ec.message());
            }
            else
            {
                messages::success(asyncResp->res);
                BMCWEB_LOG_DEBUG(
                    "Call to RevokeAllRemoteServerPublicKeys succeeded");
            }
        },
            "xyz.openbmc_project.Software.Download",
            "/xyz/openbmc_project/software", "xyz.openbmc_project.Common.SCP",
            "RevokeAllRemoteServerPublicKeys", remoteServerIP);
    });
}

/**
 * @brief Translate VerificationStatus to Redfish state
 *
 * This function will return the corresponding Redfish state
 *
 * @param[i]   status  The OpenBMC software state
 *
 * @return The corresponding Redfish state
 */
inline std::string getPackageVerificationStatus(const std::string& status)
{
    if (status ==
        "xyz.openbmc_project.Software.PackageInformation.PackageVerificationStatus.Invalid")
    {
        return "Failed";
    }
    if (status ==
        "xyz.openbmc_project.Software.PackageInformation.PackageVerificationStatus.Valid")
    {
        return "Success";
    }

    BMCWEB_LOG_DEBUG("Unknown status: {}", status);
    return "Unknown";
}

} // namespace redfish
