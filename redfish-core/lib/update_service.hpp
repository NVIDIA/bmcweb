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

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
#include "persistentstorage_util.hpp"
#endif
#include <http_client.hpp>
#include <http_connection.hpp>
#ifdef BMCWEB_ENABLE_REDFISH_AGGREGATION
#include "redfish_aggregator.hpp"
#endif

#include <app.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/container/flat_map.hpp>
#include <dbus_utility.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <update_messages.hpp>
#include <utils/conditions_utils.hpp>
#include <utils/dbus_log_utils.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/fw_utils.hpp>
#include <utils/sw_utils.hpp>

namespace redfish
{

// Match signals added on software path
static std::unique_ptr<sdbusplus::bus::match_t> fwUpdateMatcher;
// Only allow one update at a time
static bool fwUpdateInProgress = false;
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
// Allow staging, deleting or initializing firmware 
// when staging of another firmware is not in a progress
static bool fwImageIsStaging = false;
// max staged updates per hour
constexpr auto maxStagedUpdatesPerHour = 16;
// rate limit duration for staging update. current rate limit is 16 updates per
// one hour.
constexpr auto stageRateLimitDurationInSeconds = 3600;
// staged update counter for rate limit
static uint8_t stagedUpdateCount = 0;
// staged update time stamp for rate limit
static std::chrono::time_point<std::chrono::steady_clock> stagedUpdateTimeStamp;
// Timer for staging software available
static std::unique_ptr<boost::asio::steady_timer> fwStageAvailableTimer;
#endif
// Timer for software available
static std::unique_ptr<boost::asio::steady_timer> fwAvailableTimer;
// match for logging
constexpr auto fwObjectCreationDefaultTimeout = 40;
static std::unique_ptr<sdbusplus::bus::match::match> loggingMatch = nullptr;

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
#define SUPPORTED_RETIMERS 8
/* holds compute digest operation state to allow one operation at a time */
static bool computeDigestInProgress = false;
const std::string hashComputeInterface = "com.Nvidia.ComputeHash";
constexpr auto retimerHashMaxTimeSec =
    180; // 2 mins for 2 attempts and 1 addional min as buffer
#endif
const std::string firmwarePrefix = "redfish/v1/UpdateService/FirmwareInventory/";

inline static void cleanUp()
{
    fwUpdateInProgress = false;
    fwUpdateMatcher = nullptr;
}

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
inline static void cleanUpStageObjects()
{
    fwImageIsStaging = false;
}
#endif

inline static void activateImage(const std::string& objPath,
                                 const std::string& service)
{
    BMCWEB_LOG_DEBUG << "Activate image for " << objPath << " " << service;
    crow::connections::systemBus->async_method_call(
        [](const boost::system::error_code errorCode) {
            if (errorCode)
            {
                BMCWEB_LOG_DEBUG << "error_code = " << errorCode;
                BMCWEB_LOG_DEBUG << "error msg = " << errorCode.message();
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Software.Activation", "RequestedActivation",
        dbus::utility::DbusVariantType(
            "xyz.openbmc_project.Software.Activation.RequestedActivations.Active"));
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
                BMCWEB_LOG_DEBUG << "Got invalid log message";
            }
            else
            {
                auto message =
                    redfish::messages::getUpdateMessage(rfMessage, rfArgs);
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
                    BMCWEB_LOG_ERROR << "Unknown message ID: " << rfMessage;
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

// Note that asyncResp can be either a valid pointer or nullptr. If nullptr
// then no asyncResp updates will occur
static void
    softwareInterfaceAdded(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           sdbusplus::message_t& m, task::Payload&& payload)
{
    dbus::utility::DBusInteracesMap interfacesProperties;

    sdbusplus::message::object_path objPath;

    m.read(objPath, interfacesProperties);

    BMCWEB_LOG_DEBUG << "obj path = " << objPath.str;
    for (auto& interface : interfacesProperties)
    {
        BMCWEB_LOG_DEBUG << "interface = " << interface.first;

        if (interface.first == "xyz.openbmc_project.Software.Activation")
        {
            // Retrieve service and activate
            crow::connections::systemBus->async_method_call(
                [objPath, asyncResp, payload(std::move(payload))](
                    const boost::system::error_code errorCode,
                    const std::vector<
                        std::pair<std::string, std::vector<std::string>>>&
                        objInfo) mutable {
                    if (errorCode)
                    {
                        BMCWEB_LOG_ERROR << "error_code = " << errorCode;
                        BMCWEB_LOG_ERROR << "error msg = "
                                         << errorCode.message();
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
                        BMCWEB_LOG_ERROR << "Invalid Object Size "
                                         << objInfo.size();
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
                        BMCWEB_LOG_ERROR << "Software Id is empty";
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    if (asyncResp)
                    {
                        auto messageCallback =
                            [swID, objPath](const std::string_view state,
                                            [[maybe_unused]] size_t index) {
                                nlohmann::json message{};
                                if (state == "Started")
                                {
                                    message = messages::taskStarted(
                                        std::to_string(index));
                                }
                                else if (state == "Aborted")
                                {
                                    fwUpdateInProgress = false;
                                    message = messages::taskAborted(
                                        std::to_string(index));
                                }
                                else if (state == "Completed")
                                {
                                    message = messages::taskCompletedOK(
                                        std::to_string(index));
                                    // fwupdate status is set in task callback
                                }
                                else
                                {
                                    BMCWEB_LOG_INFO << "State not good";
                                }
                                return message;
                            };
                        std::shared_ptr<task::TaskData> task =
                            task::TaskData::createTask(
                                [](boost::system::error_code ec,
                                   sdbusplus::message_t& msg,
                                   const std::shared_ptr<task::TaskData>&
                                       taskData) {
                                    if (ec)
                                    {
                                        return task::completed;
                                    }

                                    std::string iface;
                                    boost::container::flat_map<
                                        std::string,
                                        dbus::utility::DbusVariantType>
                                        values;

                                    std::string index =
                                        std::to_string(taskData->index);
                                    msg.read(iface, values);

                                    if (iface ==
                                        "xyz.openbmc_project.Software.Activation")
                                    {
                                        auto findActivation =
                                            values.find("Activation");
                                        if (findActivation == values.end())
                                        {
                                            return !task::completed;
                                        }
                                        std::string* state =
                                            std::get_if<std::string>(
                                                &(findActivation->second));
                                        if (state == nullptr)
                                        {
                                            taskData->messages.emplace_back(
                                                messages::internalError());
                                            fwUpdateInProgress = false;
                                            return task::completed;
                                        }

                                        if (boost::ends_with(*state,
                                                             "Invalid") ||
                                            boost::ends_with(*state, "Failed"))
                                        {
                                            taskData->state = "Exception";
                                            taskData->messages.emplace_back(
                                                messages::taskAborted(index));
                                            fwUpdateInProgress = false;
                                            return task::completed;
                                        }

                                        if (boost::ends_with(*state, "Staged"))
                                        {

                                            taskData->state = "Stopping";
                                            taskData->messages.emplace_back(
                                                messages::taskPaused(index));
                                            // its staged, set a long timer to
                                            // allow them time to complete the
                                            // update (probably cycle the
                                            // system) if this expires then
                                            // task will be cancelled
                                            taskData->extendTimer(
                                                std::chrono::hours(5));
                                            fwUpdateInProgress = true;
                                            return !task::completed;
                                        }

                                        if (boost::ends_with(*state,
                                                             "Activating"))
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
                                            taskData->messages.emplace_back(
                                                messages::taskCompletedOK(
                                                    index));
                                            fwUpdateInProgress = false;
                                            return task::completed;
                                        }
                                    }
                                    else if (
                                        iface ==
                                        "xyz.openbmc_project.Software.ActivationProgress")
                                    {
                                        auto findProgress =
                                            values.find("Progress");
                                        if (findProgress == values.end())
                                        {
                                            return !task::completed;
                                        }
                                        uint8_t* progress =
                                            std::get_if<uint8_t>(
                                                &(findProgress->second));

                                        if (progress == nullptr)
                                        {
                                            taskData->messages.emplace_back(
                                                messages::internalError());
                                            return task::completed;
                                        }
                                        taskData->percentComplete =
                                            static_cast<int>(*progress);

                                        taskData->messages.emplace_back(
                                            messages::taskProgressChanged(
                                                index, static_cast<size_t>(
                                                           *progress)));

                                        // if we're getting status updates it's
                                        // still alive, update timer
                                        taskData->extendTimer(
                                            std::chrono::minutes(
                                                updateServiceTaskTimeout));
                                    }

                                    // as firmware update often results in a
                                    // reboot, the task  may never "complete"
                                    // unless it is an error

                                    return !task::completed;
                                },
                                "type='signal',interface='org.freedesktop.DBus.Properties',"
                                "member='PropertiesChanged',path='" +
                                    objPath.str + "'",
                                messageCallback);
                        task->startTimer(
                            std::chrono::minutes(updateServiceTaskTimeout));
                        task->populateResp(asyncResp->res);
                        task->payload.emplace(std::move(payload));
                        loggingMatch = std::make_unique<
                            sdbusplus::bus::match::match>(
                            *crow::connections::systemBus,
                            "interface='org.freedesktop.DBus.ObjectManager',type='signal',"
                            "member='InterfacesAdded',"
                            "path='/xyz/openbmc_project/logging'",
                            [task](sdbusplus::message_t& msgLog) {
                                loggingMatchCallback(task, msgLog);
                            });
                        if (preTaskMessages.size() > 0)
                        {
                            task->messages.emplace_back(preTaskMessages);
                        }
                        preTaskMessages = {};
                    }
                    fwUpdateInProgress = false;
                    activateImage(objPath.str, objInfo[0].first);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetObject", objPath.str,
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Software.Activation"});
            break;
        }
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
    fwAvailableTimer =
        std::make_unique<boost::asio::steady_timer>(*req.ioService);

    fwAvailableTimer->expires_after(std::chrono::seconds(timeoutTimeSeconds));

    fwAvailableTimer->async_wait(
        [asyncResp, imagePath](const boost::system::error_code& ec) {
            cleanUp();
            if (ec == boost::asio::error::operation_aborted)
            {
                // expected, we were canceled before the timer completed.
                return;
            }
            BMCWEB_LOG_ERROR
                << "Timed out waiting for firmware object being created";
            BMCWEB_LOG_ERROR
                << "FW image may has already been uploaded to server";
            if (ec)
            {
                BMCWEB_LOG_ERROR << "Async_wait failed" << ec;
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
        });

    task::Payload payload(req);
    auto callback = [asyncResp, payload](sdbusplus::message_t& m) mutable {
        BMCWEB_LOG_DEBUG << "Match fired";
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

                BMCWEB_LOG_DEBUG << "Enter UpdateService.SimpleUpdate doPost";

                // User can pass in both TransferProtocol and ImageURI
                // parameters or they can pass in just the ImageURI with the
                // transfer protocol embedded within it. 1)
                // TransferProtocol:TFTP ImageURI:1.1.1.1/myfile.bin 2)
                // ImageURI:tftp://1.1.1.1/myfile.bin

                if (!json_util::readJsonAction(
                        req, asyncResp->res, "TransferProtocol",
                        transferProtocol, "ImageURI", imageURI,
                        "Targets", targets, "Username", username)
                        && imageURI.empty())
                {
                    messages::createFailedMissingReqProperties(
                        asyncResp->res, "ImageURI");
                    BMCWEB_LOG_DEBUG << "Missing ImageURI";
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
                        BMCWEB_LOG_ERROR
                            << "ImageURI missing transfer protocol: "
                            << imageURI;
                        return;
                    }

                    transferProtocol = imageURI.substr(0, separator);
                    // Ensure protocol is upper case for a common comparison
                    // path below
                    boost::to_upper(*transferProtocol);
                    BMCWEB_LOG_DEBUG << "Encoded transfer protocol "
                                     << *transferProtocol;

                    // Adjust imageURI to not have the protocol on it for
                    // parsing below ex. tftp://1.1.1.1/myfile.bin
                    // -> 1.1.1.1/myfile.bin
                    imageURI = imageURI.substr(separator + 3);
                    BMCWEB_LOG_DEBUG << "Adjusted imageUri " << imageURI;
                }

                std::vector<std::string> supportedProtocols;
#ifdef BMCWEB_INSECURE_ENABLE_REDFISH_FW_TFTP_UPDATE
                supportedProtocols.push_back("TFTP");
#endif
#ifdef BMCWEB_ENABLE_REDFISH_FW_SCP_UPDATE
                supportedProtocols.push_back("SCP");
#endif
                // OpenBMC currently only supports TFTP and SCP
                if (std::find(supportedProtocols.begin(), supportedProtocols.end(), *transferProtocol)
                    == supportedProtocols.end())
                {
                    messages::actionParameterNotSupported(
                        asyncResp->res, "TransferProtocol",
                        "UpdateService.SimpleUpdate");
                    BMCWEB_LOG_ERROR << "Request incorrect protocol parameter: "
                                     << *transferProtocol;
                    return;
                }

                if ((*transferProtocol == "SCP") && (!targets))
                {
                    messages::createFailedMissingReqProperties(
                        asyncResp->res, "Targets");
                    BMCWEB_LOG_DEBUG << "Missing Target URI";
                    return;
                }

                if ((*transferProtocol == "SCP") && (!username))
                {
                    messages::createFailedMissingReqProperties(
                        asyncResp->res, "Username");
                    BMCWEB_LOG_DEBUG << "Missing Username";
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
                    BMCWEB_LOG_ERROR << "Invalid ImageURI: " << imageURI;
                    return;
                }

                std::string server = imageURI.substr(0, separator);
                std::string fwFile = imageURI.substr(separator + 1);
                BMCWEB_LOG_DEBUG
                    << "Server: " << server + " File: " << fwFile;

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
                        [](const boost::system::error_code ec) {
                            if (ec)
                            {
                                // messages::internalError(asyncResp->res);
                                cleanUp();
                                BMCWEB_LOG_DEBUG << "error_code = " << ec;
                                BMCWEB_LOG_DEBUG << "error msg = " << ec.message();
                            }
                            else
                            {
                                BMCWEB_LOG_DEBUG
                                    << "Call to DownloaViaTFTP Success";
                            }
                        },
                        "xyz.openbmc_project.Software.Download",
                        "/xyz/openbmc_project/software",
                        "xyz.openbmc_project.Common.TFTP", "DownloadViaTFTP",
                        fwFile, server);
                }
                else if (*transferProtocol == "SCP")
                {
                    // Take the first target as only one target is supported
                    std::string targetURI = targets.value()[0];
                    if (targetURI.find(firmwarePrefix) == std::string::npos)
                    {
                        // Find the last occurrence of the directory separator character
                        messages::actionParameterNotSupported(
                                    asyncResp->res, "Targets",
                                    "UpdateService.SimpleUpdate");
                        BMCWEB_LOG_ERROR << "Invalid TargetURI: " << targetURI;
                        return;
                    }
                    std::string objName = "/xyz/openbmc_project/software/" + 
                                          targetURI.substr(firmwarePrefix.length());

                    // Search for the version object related to the given target URI
                    crow::connections::systemBus->async_method_call(
                        [req, asyncResp, objName, fwFile, server, username](
                            const boost::system::error_code ec,
                            const std::vector<std::pair<
                                std::string, std::vector<std::string>>>&
                                objInfo) {
                            if (ec)
                            {
                                messages::actionParameterNotSupported(
                                    asyncResp->res, "Targets",
                                    "UpdateService.SimpleUpdate");
                                    BMCWEB_LOG_ERROR << "Request incorrect target URI parameter: "
                                     << objName;
                                BMCWEB_LOG_ERROR << "error_code = " << ec << " " <<
                                                    "error msg = " << ec.message();
                                return;
                            }
                            // Ensure we only got one service back
                            if (objInfo.size() != 1)
                            {
                                messages::internalError(asyncResp->res);
                                BMCWEB_LOG_ERROR << "Invalid Object Size "
                                                 << objInfo.size();
                                return;
                            }

                            // Read the version object's FilePath property which holds
                            // the local path used for the update procedure
                            crow::connections::systemBus->async_method_call(
                                [req, asyncResp, fwFile, server, username](
                                    const boost::system::error_code ecPath,
                                    const std::variant<std::string>& property) {
                                    if (ecPath)
                                    {
                                        messages::actionParameterNotSupported(
                                            asyncResp->res, "Targets",
                                            "UpdateService.SimpleUpdate");
                                        BMCWEB_LOG_ERROR << "Failed to read the path property of Target";
                                        BMCWEB_LOG_ERROR << "error_code = " << ecPath << " " <<
                                                            "error msg = " << ecPath.message();
                                        return;
                                    }

                                    const std::string* targetPath = std::get_if<std::string>(&property);
                                    if (targetPath == nullptr)
                                    {
                                        messages::actionParameterNotSupported(
                                            asyncResp->res, "Targets",
                                            "UpdateService.SimpleUpdate");
                                        BMCWEB_LOG_ERROR << "Null value returned for path";
                                        return;
                                    }

                                    // Check if local path exists
                                    if (!fs::exists(*targetPath))
                                    {
                                        messages::resourceNotFound(asyncResp->res, "Targets", *targetPath);
                                        BMCWEB_LOG_ERROR << "Path does not exist";
                                        return;
                                    }

                                    // Setup callback for when new software detected
                                    // Give SCP 10 minutes to detect new software
                                    monitorForSoftwareAvailable(asyncResp, req, 600);

                                    // Call SCP service. As key-based authentication is used,
                                    // user password is not necessary
                                    crow::connections::systemBus->async_method_call(
                                        [asyncResp](const boost::system::error_code ecSCP) {
                                            if (ecSCP)
                                            {
                                                messages::internalError(asyncResp->res);
                                                BMCWEB_LOG_ERROR << "error_code = " << ecSCP << " " <<
                                                                    "error msg = " << ecSCP.message();
                                            }
                                            else
                                            {
                                                BMCWEB_LOG_DEBUG << "Call to DownloadViaSCP Success";
                                            }
                                        },
                                        "xyz.openbmc_project.Software.Download",
                                        "/xyz/openbmc_project/software",
                                        "xyz.openbmc_project.Common.SCP", "DownloadViaSCP",
                                        server, *username, fwFile, *targetPath);
                                },
                                objInfo[0].first, objName,
                                "org.freedesktop.DBus.Properties", "Get",
                                "xyz.openbmc_project.Common.FilePath",
                                "Path");
                        },
                        "xyz.openbmc_project.ObjectMapper",
                        "/xyz/openbmc_project/object_mapper",
                        "xyz.openbmc_project.ObjectMapper", "GetObject", objName,
                        std::array<const char*, 2>{
                            "xyz.openbmc_project.Software.Version",
                            "xyz.openbmc_project.Common.FilePath"});
                }

                BMCWEB_LOG_DEBUG << "Exit UpdateService.SimpleUpdate doPost";
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
inline void uploadImageFile(const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::string filepath(
        updateServiceImageLocation +
        boost::uuids::to_string(boost::uuids::random_generator()()));

    monitorForSoftwareAvailable(asyncResp, req, fwObjectCreationDefaultTimeout,
                                filepath);

    BMCWEB_LOG_DEBUG << "Uploaded image is written to " << filepath;
    std::ofstream out(filepath, std::ofstream::out | std::ofstream::binary |
                                    std::ofstream::trunc);
    // set the permission of the file to 640
    std::filesystem::perms permission =
        std::filesystem::perms::owner_read | std::filesystem::perms::group_read;
    std::filesystem::permissions(filepath, permission);

    MultipartParser parser;
    ParserError ec = parser.parse(req);
    if (ec != ParserError::PARSER_SUCCESS)
    {
        // handle error
        BMCWEB_LOG_ERROR << "MIME parse failed, ec : " << static_cast<int>(ec);
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

        for (auto const& param :
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
                    BMCWEB_LOG_ERROR << "Error writing to file: " << filepath
                                     << ", Error code: " << out.rdstate();
                    messages::internalError(asyncResp->res);
                    cleanUp();
                }
            }
        }
    }

    if (!hasUpdateFile)
    {
        BMCWEB_LOG_ERROR << "File with firmware image is missing.";
        messages::propertyMissing(asyncResp->res, "UpdateFile");
    }
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
        std::string softwarePath = "/xyz/openbmc_project/software/" + componentName;

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
                BMCWEB_LOG_ERROR << "Unupdatable Target: " << target << "\n";
            }
        }

        if (!validTarget)
        {
            hasAnyInvalidOrUnupdateableTarget = true;
            BMCWEB_LOG_ERROR << "Invalid Target: " << target << "\n";
        }
    }

    return hasAnyInvalidOrUnupdateableTarget;
}

/**
 * @brief Handle update policy
 *
 * @param[in] errorCode Error code
 * @param[in] objInfo Service object
 * @param[in] req  HTTP request
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] targets  List of valid components delivered in HTTPRequest
 *
 * @return None
 */
inline void validateUpdatePolicyCallback(
    const boost::system::error_code errorCode,
    const MapperServiceMap& objInfo,
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::vector<sdbusplus::message::object_path>& targets)
{
    if (errorCode)
    {
        BMCWEB_LOG_ERROR << "validateUpdatePolicyCallback:error_code = "
                         << errorCode;
        BMCWEB_LOG_ERROR << "validateUpdatePolicyCallback:error msg = "
                         << errorCode.message();
        if (asyncResp)
        {
            messages::internalError(asyncResp->res);
        }
        return;
    }
    // Ensure we only got one service back
    if (objInfo.size() != 1)
    {
        BMCWEB_LOG_ERROR
            << "More than one service support xyz.openbmc_project.Software.UpdatePolicy. Object Size "
            << objInfo.size();
        if (asyncResp)
        {
            messages::internalError(asyncResp->res);
        }
        return;
    }

    crow::connections::systemBus->async_method_call(
        [req, asyncResp](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "error_code = " << ec;
                messages::internalError(asyncResp->res);
            }

            uploadImageFile(req, asyncResp);
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
 *
 * @return None
 */
inline void areTargetsUpdateableCallback(
    const boost::system::error_code& ec,
    const std::vector<std::string>& objPaths, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::vector<std::string>& uriTargets,
    const std::vector<std::string>& swInvPaths)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG << "areTargetsUpdateableCallback:error_code = " << ec;
        BMCWEB_LOG_DEBUG << "areTargetsUpdateableCallback:error msg =  "
                         << ec.message();

        BMCWEB_LOG_ERROR << "Targeted devices not updateable";

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
        [req, asyncResp, targets](const boost::system::error_code errorCode,
                                  const MapperServiceMap& objInfo) mutable {
            validateUpdatePolicyCallback(errorCode, objInfo, req, asyncResp,
                                         targets);
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
 *
 * @return None
 */
inline void
    areTargetsUpdateable(const crow::Request& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::vector<std::string>& uriTargets)
{
    crow::connections::systemBus->async_method_call(
        [req, asyncResp,
         uriTargets](const boost::system::error_code ec,
                     const std::vector<std::string>& swInvPaths) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "D-Bus responses error: " << ec;
                messages::internalError(asyncResp->res);
                return;
            }

            sdbusplus::asio::getProperty<std::vector<std::string>>(
                *crow::connections::systemBus,
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/software/updateable",
                "xyz.openbmc_project.Association", "endpoints",
                [req, asyncResp, uriTargets,
                 swInvPaths](const boost::system::error_code ec,
                             const std::vector<std::string>& objPaths) {
                    areTargetsUpdateableCallback(ec, objPaths, req, asyncResp,
                                                 uriTargets,
                                                 swInvPaths);
                });
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/software/", static_cast<int32_t>(0),
        std::array<std::string, 1>{"xyz.openbmc_project.Software.Version"});
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
 * @param[out] hasFile return true when 'UpdateFile' is added to HTTPRequest
 *
 * @return It returns true when parsing of the multipart update form is
 * successfully completed.
 */
inline bool
    parseMultipartForm(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const MultipartParser& parser, bool& hasUpdateParameters,
                       std::optional<std::vector<std::string>>& targets,
                       std::optional<std::string>& applyTime, 
                       std::optional<bool>& forceUpdate, bool& hasFile)
{
    hasUpdateParameters = false;
    hasFile = false;
    for (const FormPart& formpart : parser.mime_fields)
    {
        boost::beast::http::fields::const_iterator it =
            formpart.fields.find("Content-Disposition");
        if (it == formpart.fields.end())
        {
            BMCWEB_LOG_ERROR << "Couldn't find Content-Disposition";
            messages::propertyMissing(asyncResp->res, "Content-Disposition");
            return false;
        }
        BMCWEB_LOG_INFO << "Parsing value " << it->value();

        // The construction parameters of param_list must start with `;`
        size_t index = it->value().find(';');
        if (index == std::string::npos)
        {
            continue;
        }

        for (auto const& param :
             boost::beast::http::param_list{it->value().substr(index)})
        {
            if (param.first != "name" || param.second.empty())
            {
                continue;
            }

            if (param.second == "UpdateParameters")
            {
                hasUpdateParameters = true;

                try
                {
                    nlohmann::json content =
                        nlohmann::json::parse(formpart.content);

                    json_util::readJson(content, asyncResp->res, "Targets",
                                        targets, "@Redfish.OperationApplyTime",
                                        applyTime, "ForceUpdate", forceUpdate);
                }
                catch (const std::exception& e)
                {
                    BMCWEB_LOG_ERROR
                        << "Unable to parse JSON. Check the format of the request body."
                        << "Exception caught: " << e.what();
                    messages::unrecognizedRequestBody(asyncResp->res);

                    return false;
                }
            }
            else if (param.second == "UpdateFile")
            {
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
 *
 * @return It returns true when the form section 'UpdateParameters' contains the
 * required parameters.
 */
inline bool validateUpdateParametersFormData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    bool hasUpdateParameters, std::optional<std::string>& applyTime)
{
    if (!hasUpdateParameters)
    {
        BMCWEB_LOG_INFO << "UpdateParameters parameter is missing";

        messages::actionParameterMissing(asyncResp->res, "update-multipart",
                                         "UpdateParameters");

        return false;
    }

    if (applyTime)
    {
        std::string allowedApplyTime = "Immediate";
        if (allowedApplyTime != *applyTime)
        {
            BMCWEB_LOG_INFO
                << "ApplyTime value is not in the list of acceptable values";

            messages::propertyValueNotInList(asyncResp->res, *applyTime,
                                             "@Redfish.OperationApplyTime");

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
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const bool hasFile)
{
    if (!hasFile)
    {
        BMCWEB_LOG_ERROR << "Upload data is NULL";
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
inline boost::system::error_code aggregationPostRetryHandler(unsigned int respCode)
{
    // Allow all response codes because we want to surface any satellite
    // issue to the client
    BMCWEB_LOG_DEBUG << "Received " << respCode
                     << " response of the firmware update from satellite";
    return boost::system::errc::make_error_code(boost::system::errc::success);
}

inline crow::ConnectionPolicy getPostAggregationPolicy()
{
    return {.maxRetryAttempts = 1,
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
void handleSatBMCResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, crow::Response& resp)
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
        asyncResp->res.body() = resp.body();
        return;
    }

    // The resp will not have a json component
    // We need to create a json from resp's stringResponse
    std::string_view contentType = resp.getHeaderValue("Content-Type");
    if (boost::iequals(contentType, "application/json") ||
        boost::iequals(contentType, "application/json; charset=utf-8"))
    {
        nlohmann::json jsonVal =
            nlohmann::json::parse(resp.body(), nullptr, false);
        if (jsonVal.is_discarded())
        {
            BMCWEB_LOG_ERROR << "Error parsing satellite response as JSON";

            // Notify the user if doing so won't overwrite a valid response
            if (asyncResp->res.resultInt() != static_cast<unsigned>(boost::beast::http::status::ok))
            {
                messages::operationFailed(asyncResp->res);
            }
            return;
        }
        BMCWEB_LOG_DEBUG << "Successfully parsed satellite response";
        nlohmann::json::object_t* object =
            jsonVal.get_ptr<nlohmann::json::object_t*>();
        if (object == nullptr)
        {
            BMCWEB_LOG_ERROR << "Parsed JSON was not an object?";
            return;
        }

        std::string rfaPrefix = redfishAggregationPrefix;
        for (std::pair<const std::string, nlohmann::json>& prop : *object)
        {
            // only prefix fix-up on Task response.
            std::string* strValue = prop.second.get_ptr<std::string*>();
            if (strValue == nullptr)
            {
                BMCWEB_LOG_CRITICAL << "Field wasn't a string????";
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
    const crow::Request& req,
    const bool updateAll,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code &ec,
    const std::unordered_map<std::string, boost::urls::url>& satelliteInfo)
{
    // Something went wrong while querying dbus
    if (ec)
    {
        BMCWEB_LOG_ERROR << "Dbus query error for satellite BMC.";
        messages::internalError(asyncResp->res);
        return;
    }

    const auto& sat = satelliteInfo.find(redfishAggregationPrefix);
    if (sat == satelliteInfo.end())
    {
        BMCWEB_LOG_ERROR << "satellite BMC is not there.";
        return;
    }

    crow::HttpClient client( 
        std::make_shared<crow::ConnectionPolicy>(getPostAggregationPolicy()));

    MultipartParser parser;
    ParserError err = parser.parse(req);
    if (err != ParserError::PARSER_SUCCESS)
    {
        // handle error
        BMCWEB_LOG_ERROR << "MIME parse failed, ec : " << static_cast<int>(err);
        messages::internalError(asyncResp->res);
        return;
    }

    std::function<void(crow::Response&)> cb =
        std::bind_front(handleSatBMCResponse, asyncResp);

    bool hasUpdateFile = false;
    std::string data;
    std::string_view boundary(parser.boundary);
    for (FormPart& formpart : parser.mime_fields)
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

        for (auto const& param :
             boost::beast::http::param_list{it->value().substr(index)})
        {
            if (param.first != "name" || param.second.empty())
            {
                continue;
            }

            if (param.second == "UpdateFile")
            {
                data += "Content-Type: application/octet-stream\r\n\r\n";
                data += formpart.content;
                data += "\r\n";
                hasUpdateFile = true;
            }
            else if (param.second == "UpdateParameters")
            {
                data += "Content-Type: application/json\r\n\r\n";
                nlohmann::json content =
                    nlohmann::json::parse(formpart.content, nullptr, false);
                if (content.is_discarded())
                {
                    BMCWEB_LOG_INFO << "UpdateParameters parse error:"
                                    << formpart.content;
                    continue;
                }
                std::optional<std::vector<std::string>> targets;
                std::optional<bool> forceUpdate;

                json_util::readJson(content, asyncResp->res, "Targets", targets,
                                    "ForceUpdate", forceUpdate);

                nlohmann::json paramJson = nlohmann::json::object();

                const std::string urlPrefix = redfishAggregationPrefix;
                // individual components update
                if (targets && updateAll == false)
                {
                    paramJson["Targets"] = nlohmann::json::array();

                    for (auto & uri: *targets) 
                    {
                        // the special handling for Oberon System.
                        // we don't remove the prefix if the resource's prefix
                        // from FirmwareInventory is the same with RFA prefix. 
#ifdef RAF_PREFIX_REMOVAL
                        // remove prefix before the update request is forwarded.
                        std::string file = std::filesystem::path(uri).filename();
                        size_t pos = uri.find(urlPrefix + "_");
                        if (pos != std::string::npos)
                        {
                            uri.erase(pos, urlPrefix.size() + 1);
                        }
#endif
                        BMCWEB_LOG_DEBUG << "uri in Targets: "<< uri;
                        paramJson["Targets"].push_back(uri);
                    }
                }
                if (forceUpdate)
                {
                    paramJson["ForceUpdate"] = *forceUpdate;
                }
                data += paramJson.dump();
                data += "\r\n";
                BMCWEB_LOG_DEBUG <<"form data:" <<data;
            }
        }
    }

    if (!hasUpdateFile)
    {
        BMCWEB_LOG_ERROR << "File with firmware image is missing.";
        messages::propertyMissing(asyncResp->res, "UpdateFile");
    }
    else
    {
        data += boundary.substr(2);
        data += "--\r\n";

        std::string targetURI(req.target());

        client.sendDataWithCallback(
            data, std::string(sat->second.host()),
            sat->second.port_number(), targetURI, false /*useSSL*/, req.fields,
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
                BMCWEB_LOG_ERROR << "error_code = " << errorCode;
                BMCWEB_LOG_ERROR << "error msg = " << errorCode.message();
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
                BMCWEB_LOG_ERROR
                    << "Expected exactly one service implementing xyz.openbmc_project.Software.UpdatePolicy, but found "
                    << objInfo.size() << " services.";
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
                        BMCWEB_LOG_ERROR << "error_code = " << errCodePolicy;
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    if (callback)
                    {
                        callback();
                    }
                },
                objInfo[0].first, objpath, "org.freedesktop.DBus.Properties",
                "Set", "xyz.openbmc_project.Software.UpdatePolicy",
                "ForceUpdate", dbus::utility::DbusVariantType(forceUpdate));
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
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const MultipartParser& parser)
{
    std::optional<std::string> applyTime;
    std::optional<bool> forceUpdate;
    std::optional<std::vector<std::string>> targets;
    bool hasUpdateParameters = false;
    bool hasFile = false;

    if (!parseMultipartForm(asyncResp, parser, hasUpdateParameters, targets,
                            applyTime, forceUpdate, hasFile))
    {
        return;
    }

    if (!validateUpdateParametersFormData(asyncResp, hasUpdateParameters,
                                          applyTime))
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
                BMCWEB_LOG_DEBUG << "Couldn't parse URI from resource "
                                    << uri;
                return;
            }

            boost::urls::url_view thisUrl = *parsed;

            //this is the Chassis resource from satellite BMC for all component
            //firmware update.
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
                BMCWEB_LOG_DEBUG << "forward image" << uriTargets[0];

                RedfishAggregator::getSatelliteConfigs(
                    std::bind_front(forwardImage, req, updateAll, asyncResp));
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
            BMCWEB_LOG_ERROR << "Update already in progress.";
        }
        return ;
    }
#endif

    setForceUpdate(asyncResp, "/xyz/openbmc_project/software",
                   forceUpdate.value_or(false), [&req, asyncResp, uriTargets]() {
                       areTargetsUpdateable(req, asyncResp, uriTargets);
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
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (req.body.size() > firmwareImageLimitBytes)
    {
        if (asyncResp)
        {
            BMCWEB_LOG_ERROR << "Large image size: " << req.body.size();
            std::string resolution =
                "Firmware package size is greater than allowed "
                "size. Make sure package size is less than "
                "UpdateService.MaxImageSizeBytes property and "
                "retry the firmware update operation.";
            messages::payloadTooLarge(asyncResp->res, resolution);
        }
        return false;
    }

#ifndef BMCWEB_ENABLE_REDFISH_AGGREGATION
    // Only allow one FW update at a time
    if (fwUpdateInProgress != false)
    {
        if (asyncResp)
        {
            // don't copy the image, update already in progress.
            std::string resolution =
                "Another update is in progress. Retry"
                " the update operation once it is complete.";
            redfish::messages::updateInProgressMsg(asyncResp->res, resolution);
            BMCWEB_LOG_ERROR << "Update already in progress.";
        }
        return false;
    }
#endif

    std::error_code spaceInfoError;
    const std::filesystem::space_info spaceInfo =
        std::filesystem::space(updateServiceImageLocation, spaceInfoError);
    if (!spaceInfoError)
    {
        if (spaceInfo.free < req.body.size())
        {
            BMCWEB_LOG_ERROR
                << "Insufficient storage space. Required: " << req.body.size()
                << " Available: " << spaceInfo.free;
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
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    BMCWEB_LOG_DEBUG
        << "Execute HTTP POST method '/redfish/v1/UpdateService/update-multipart/'";

    if (!preCheckMultipartUpdateServiceReq(req, asyncResp))
    {
        return;
    }

    MultipartParser parser;
    ParserError ec = parser.parse(req);
    if (ec == ParserError::ERROR_BOUNDARY_FORMAT)
    {
        BMCWEB_LOG_ERROR << "The request has unsupported media type";
        messages::unsupportedMediaType(asyncResp->res);

        return;
    }
    if (ec != ParserError::PARSER_SUCCESS)
    {
        // handle error
        BMCWEB_LOG_ERROR << "MIME parse failed, ec : " << static_cast<int>(ec);
        messages::internalError(asyncResp->res);
        return;
    }
    processMultipartFormData(req, asyncResp, parser);
}

inline void
    handleUpdateServicePost(App& app, const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    BMCWEB_LOG_DEBUG
        << "Execute HTTP POST method '/redfish/v1/UpdateService/update/'";

    if (!preCheckMultipartUpdateServiceReq(req, asyncResp))
    {
        return;
    }

    std::string filepath(
        updateServiceImageLocation +
        boost::uuids::to_string(boost::uuids::random_generator()()));
    monitorForSoftwareAvailable(asyncResp, req, fwObjectCreationDefaultTimeout,
                                filepath);

    BMCWEB_LOG_DEBUG << "Writing file to " << filepath;
    std::ofstream out(filepath, std::ofstream::out | std::ofstream::binary |
                                    std::ofstream::trunc);
    out << req.body;
    out.close();
    BMCWEB_LOG_DEBUG << "file upload complete!!";

}

class BMCStatusAsyncResp
{
  public:
    BMCStatusAsyncResp(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) :
        asyncResp(asyncResp) {}

    ~BMCStatusAsyncResp()
    {
        if (bmcStateString == "xyz.openbmc_project.State.BMC.BMCState.Ready"
            && hostStateString != "xyz.openbmc_project.State.Host.HostState.TransitioningToRunning"
            && hostStateString != "xyz.openbmc_project.State.Host.HostState.TransitioningToOff"
            && pldm_serviceStatus && mctp_serviceStatus)
        {
            asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
        }
        else
        {
            asyncResp->res.jsonValue["Status"]["State"] = "UnavailableOffline";
        }
        asyncResp->res.jsonValue["Status"]["Conditions"] = nlohmann::json::array();
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
        .methods(boost::beast::http::verb::get)([&app](const crow::Request& req,
                                                       const std::shared_ptr<
                                                           bmcweb::AsyncResp>&
                                                           asyncResp) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            asyncResp->res.jsonValue["@odata.type"] =
                "#UpdateService.v1_8_0.UpdateService";
            asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/UpdateService";
            asyncResp->res.jsonValue["Id"] = "UpdateService";
            asyncResp->res.jsonValue["Description"] =
                "Service for Software Update";
            asyncResp->res.jsonValue["Name"] = "Update Service";

#ifdef BMCWEB_ENABLE_REDFISH_UPDATESERVICE_OLD_POST_URL
            // See note about later on in this file about why this is neccesary
            // This is "Wrong" per the standard, but is done temporarily to
            // avoid noise in failing tests as people transition to having this
            // option disabled
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
            asyncResp->res.jsonValue["MaxImageSizeBytes"] =
                firmwareImageLimitBytes;
            asyncResp->res.jsonValue["Actions"]["Oem"]["Nvidia"]
                                    ["#NvidiaUpdateService.CommitImage"] = {
                {"target",
                 "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.CommitImage"},
                {"@Redfish.ActionInfo",
                 "/redfish/v1/UpdateService/Oem/Nvidia/CommitImageActionInfo"}};
            asyncResp->res.jsonValue["Actions"]["Oem"]["Nvidia"]
                                    ["#NvidiaUpdateService.PublicKeyExchange"] = {
                {"target",
                 "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.PublicKeyExchange"}};
            asyncResp->res.jsonValue["Actions"]["Oem"]["Nvidia"]
                                    ["#NvidiaUpdateService.RevokeAllRemoteServerPublicKeys"] = {
                {"target",
                 "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.RevokeAllRemoteServerPublicKeys"}};

#if defined(BMCWEB_INSECURE_ENABLE_REDFISH_FW_TFTP_UPDATE) || defined(BMCWEB_ENABLE_REDFISH_FW_SCP_UPDATE)
            // Update Actions object.
            nlohmann::json& updateSvcSimpleUpdate =
                asyncResp->res
                    .jsonValue["Actions"]["#UpdateService.SimpleUpdate"];
            updateSvcSimpleUpdate["target"] =
                "/redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate";
            updateSvcSimpleUpdate["TransferProtocol@Redfish.AllowableValues"] = {};
#ifdef BMCWEB_INSECURE_ENABLE_REDFISH_FW_TFTP_UPDATE
            updateSvcSimpleUpdate["TransferProtocol@Redfish.AllowableValues"] += "TFTP";
#endif
#ifdef BMCWEB_ENABLE_REDFISH_FW_SCP_UPDATE
            updateSvcSimpleUpdate["TransferProtocol@Redfish.AllowableValues"] += "SCP";
#endif
#endif
            // Get the current ApplyTime value
            sdbusplus::asio::getProperty<std::string>(
                *crow::connections::systemBus, "xyz.openbmc_project.Settings",
                "/xyz/openbmc_project/software/apply_time",
                "xyz.openbmc_project.Software.ApplyTime", "RequestedApplyTime",
                [asyncResp](const boost::system::error_code ec,
                            const std::string& applyTime) {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG << "DBUS response error " << ec;
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    // Store the ApplyTime Value
                    if (applyTime == "xyz.openbmc_project.Software.ApplyTime."
                                     "RequestedApplyTimes.Immediate")
                    {
                        asyncResp->res
                            .jsonValue["HttpPushUriOptions"]
                                      ["HttpPushUriApplyTime"]["ApplyTime"] =
                            "Immediate";
                    }
                    else if (applyTime ==
                             "xyz.openbmc_project.Software.ApplyTime."
                             "RequestedApplyTimes.OnReset")
                    {
                        asyncResp->res
                            .jsonValue["HttpPushUriOptions"]
                                      ["HttpPushUriApplyTime"]["ApplyTime"] =
                            "OnReset";
                    }
                });

            auto getUpdateStatus = std::make_shared<BMCStatusAsyncResp>(asyncResp);
            crow::connections::systemBus->async_method_call(
                [asyncResp, getUpdateStatus](
                    const boost::system::error_code errorCode,
                    const std::vector<
                        std::pair<std::string, std::vector<std::string>>>&
                        objInfo) mutable {
                    if (errorCode)
                    {
                        BMCWEB_LOG_ERROR << "error_code = " << errorCode;
                        BMCWEB_LOG_ERROR << "error msg = "
                                         << errorCode.message();
                        if (asyncResp)
                        {
                            messages::internalError(asyncResp->res);
                        }
                        getUpdateStatus->pldm_serviceStatus  = false;
                        return;
                    }
                    getUpdateStatus->pldm_serviceStatus  = true;

                    // Ensure we only got one service back
                    if (objInfo.size() != 1)
                    {
                        BMCWEB_LOG_ERROR << "Invalid Object Size "
                                         << objInfo.size();
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
                                BMCWEB_LOG_ERROR << "error_code = " << ec;
                                BMCWEB_LOG_ERROR << "error msg = "
                                                 << ec.message();
                                messages::internalError(asyncResp->res);
                                return;
                            }

                            for (auto& propertyMap : resp)
                            {
                                if (propertyMap.first == "Targets")
                                {
                                    auto targets = std::get_if<std::vector<
                                        sdbusplus::message::object_path>>(
                                        &propertyMap.second);
                                    if (targets)
                                    {
                                        std::vector<std::string> pushURITargets;
                                        for (auto& target : *targets)
                                        {
                                            std::string firmwareId =
                                                target.filename();
                                            if (firmwareId.empty())
                                            {
                                                BMCWEB_LOG_ERROR
                                                    << "Unable to parse firmware ID";
                                                messages::internalError(
                                                    asyncResp->res);
                                                return;
                                            }
                                            pushURITargets.push_back(
                                                "/redfish/v1/UpdateService/FirmwareInventory/" +
                                                firmwareId);
                                        }
                                        asyncResp->res
                                            .jsonValue["HttpPushUriTargets"] =
                                            pushURITargets;
                                    }
                                }
                                else if (propertyMap.first == "ForceUpdate")
                                {
                                    auto forceUpdate =
                                        std::get_if<bool>(&propertyMap.second);
                                    if (forceUpdate)
                                    {
                                        asyncResp->res
                                            .jsonValue["HttpPushUriOptions"]
                                                      ["ForceUpdate"] =
                                            *forceUpdate;
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
                            const MapperGetSubTreeResponse& subtree) mutable {
                    if (ec || !subtree.size())
                    {
                        getUpdateStatus->mctp_serviceStatus  = false;
                    } else {
                        getUpdateStatus->mctp_serviceStatus = true;
                    }
                    return;
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/mctp/0", 0,
                std::array<const char*, 1>{
                    "xyz.openbmc_project.MCTP.Endpoint"});

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
                "/xyz/openbmc_project/state/host0", "xyz.openbmc_project.State.Host",
                "CurrentHostState",
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
        .methods(
            boost::beast::http::verb::patch)([&app](const crow::Request& req,
                                                    const std::shared_ptr<
                                                        bmcweb::AsyncResp>&
                                                        asyncResp) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            BMCWEB_LOG_DEBUG << "doPatch...";

            std::optional<nlohmann::json> pushUriOptions;
            std::optional<std::vector<std::string>> imgTargets;
            if (!json_util::readJsonPatch(req, asyncResp->res,
                                          "HttpPushUriOptions", pushUriOptions,
                                          "HttpPushUriTargets", imgTargets))
            {
                BMCWEB_LOG_ERROR
                    << "UpdateService doPatch: Invalid request body";
                return;
            }

            if (pushUriOptions)
            {
                std::optional<bool> forceUpdate;
                std::optional<nlohmann::json> pushUriApplyTime;
                if (!json_util::readJson(
                        *pushUriOptions, asyncResp->res, "HttpPushUriApplyTime",
                        pushUriApplyTime, "ForceUpdate", forceUpdate))
                {
                    return;
                }

                if (pushUriApplyTime)
                {
                    std::optional<std::string> applyTime;
                    if (!json_util::readJson(*pushUriApplyTime, asyncResp->res,
                                             "ApplyTime", applyTime))
                    {
                        return;
                    }

                    if (applyTime)
                    {
                        std::string applyTimeNewVal;
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
                            BMCWEB_LOG_INFO
                                << "ApplyTime value is not in the list of acceptable values";
                            messages::propertyValueNotInList(
                                asyncResp->res, *applyTime, "ApplyTime");
                            return;
                        }

                        // Set the requested image apply time value
                        crow::connections::systemBus->async_method_call(
                            [asyncResp](const boost::system::error_code ec) {
                                if (ec)
                                {
                                    BMCWEB_LOG_ERROR
                                        << "D-Bus responses error: " << ec;
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                messages::success(asyncResp->res);
                            },
                            "xyz.openbmc_project.Settings",
                            "/xyz/openbmc_project/software/apply_time",
                            "org.freedesktop.DBus.Properties", "Set",
                            "xyz.openbmc_project.Software.ApplyTime",
                            "RequestedApplyTime",
                            dbus::utility::DbusVariantType{applyTimeNewVal});
                    }
                }

                if (forceUpdate)
                {
                    setForceUpdate(asyncResp, "/xyz/openbmc_project/software",
                                   forceUpdate.value_or(false));
                }
            }

            if (imgTargets)
            {
                crow::connections::systemBus->async_method_call(
                    [asyncResp, uriTargets{*imgTargets}](
                        const boost::system::error_code ec,
                        const std::vector<std::string>& swInvPaths) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR << "D-Bus responses error: " << ec;
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
                                    if ((idPos == std::string::npos))
                                    {
                                        continue;
                                    }
                                    std::string swId = path.substr(idPos);
                                    if (swId == objPath)
                                    {
                                        sdbusplus::message::object_path objpath(
                                            path);
                                        httpPushUriTargets.emplace_back(
                                            objpath);
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
                                BMCWEB_LOG_ERROR
                                    << "Targetted Device not Found!!";
                                messages::invalidObject(
                                    asyncResp->res,
                                    crow::utility::urlFromPieces(
                                        "HttpPushUriTargets"));
                                return;
                            }
                            // return HTTP200 - Success with errors
                            // when there is partial valid targets
                            if (invalidTargets.size() > 0)
                            {
                                for (const std::string& invalidTarget :
                                     invalidTargets)
                                {
                                    BMCWEB_LOG_ERROR
                                        << "Invalid HttpPushUriTarget: "
                                        << invalidTarget << "\n";
                                    messages::propertyValueFormatError(
                                        asyncResp->res, invalidTarget,
                                        "HttpPushUriTargets");
                                }
                                asyncResp->res.result(
                                    boost::beast::http::status::ok);
                            }
                            // else all targets are valid
                        }
                        crow::connections::systemBus->async_method_call(
                            [asyncResp, httpPushUriTargets](
                                const boost::system::error_code errorCode,
                                const std::vector<std::pair<
                                    std::string, std::vector<std::string>>>&
                                    objInfo) mutable {
                                if (errorCode)
                                {
                                    BMCWEB_LOG_ERROR << "error_code = "
                                                     << errorCode;
                                    BMCWEB_LOG_ERROR << "error msg = "
                                                     << errorCode.message();
                                    if (asyncResp)
                                    {
                                        messages::internalError(asyncResp->res);
                                    }
                                    return;
                                }
                                // Ensure we only got one service back
                                if (objInfo.size() != 1)
                                {
                                    BMCWEB_LOG_ERROR << "Invalid Object Size "
                                                     << objInfo.size();
                                    if (asyncResp)
                                    {
                                        messages::internalError(asyncResp->res);
                                    }
                                    return;
                                }

                                crow::connections::systemBus->async_method_call(
                                    [asyncResp](const boost::system::error_code
                                                    errCodePolicy) {
                                        if (errCodePolicy)
                                        {
                                            BMCWEB_LOG_ERROR << "error_code = "
                                                             << errCodePolicy;
                                            messages::internalError(
                                                asyncResp->res);
                                        }
                                        messages::success(asyncResp->res);
                                    },
                                    objInfo[0].first,
                                    "/xyz/openbmc_project/software",
                                    "org.freedesktop.DBus.Properties", "Set",
                                    "xyz.openbmc_project.Software.UpdatePolicy",
                                    "Targets",
                                    dbus::utility::DbusVariantType(
                                        httpPushUriTargets));
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
        .methods(
            boost::beast::http::verb::
                post)([&app](
                          const crow::Request& req,
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
            std::bind_front(handleMultipartUpdateServicePost, std::ref(app)));
}

inline void requestRoutesSoftwareInventoryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/FirmwareInventory/")
        .privileges(redfish::privileges::getSoftwareInventoryCollection)
        .methods(boost::beast::http::verb::get)([&app](const crow::Request& req,
                                                       const std::shared_ptr<
                                                           bmcweb::AsyncResp>&
                                                           asyncResp) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            asyncResp->res.jsonValue["@odata.type"] =
                "#SoftwareInventoryCollection.SoftwareInventoryCollection";
            asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/UpdateService/FirmwareInventory";
            asyncResp->res.jsonValue["Name"] = "Software Inventory Collection";

            crow::connections::systemBus->async_method_call(
                [asyncResp](
                    const boost::system::error_code ec,
                    const std::vector<
                        std::pair<std::string,
                                  std::vector<std::pair<
                                      std::string, std::vector<std::string>>>>>&
                        subtree) {
                    if (ec == boost::system::errc::io_error)
                    {
                        asyncResp->res.jsonValue["Members"] =
                            nlohmann::json::array();
                        asyncResp->res.jsonValue["Members@odata.count"] = 0;
                        return;
                    }
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Members"] =
                        nlohmann::json::array();
                    asyncResp->res.jsonValue["Members@odata.count"] = 0;

                    for (auto& obj : subtree)
                    {
                        sdbusplus::message::object_path path(obj.first);
                        std::string swId = path.filename();
                        if (swId.empty())
                        {
                            messages::internalError(asyncResp->res);
                            BMCWEB_LOG_DEBUG << "Can't parse firmware ID!!";
                            return;
                        }

                        nlohmann::json& members =
                            asyncResp->res.jsonValue["Members"];
                        members.push_back(
                            {{"@odata.id",
                                "/redfish/v1/UpdateService/FirmwareInventory/" +
                                  swId}});
                        asyncResp->res.jsonValue["Members@odata.count"] =
                            members.size();
                    }
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
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Software.Version"});
        });
}

inline void requestRoutesInventorySoftwareCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/SoftwareInventory/")
        .privileges(redfish::privileges::getSoftwareInventoryCollection)
        .methods(boost::beast::http::verb::get)([&app](const crow::Request& req,
                                                       const std::shared_ptr<
                                                           bmcweb::AsyncResp>&
                                                           asyncResp) {
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
                    const std::vector<
                        std::pair<std::string,
                                  std::vector<std::pair<
                                      std::string, std::vector<std::string>>>>>&
                        subtree) {
                    if (ec == boost::system::errc::io_error)
                    {
                        asyncResp->res.jsonValue["Members"] =
                            nlohmann::json::array();
                        asyncResp->res.jsonValue["Members@odata.count"] = 0;
                        return;
                    }
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Members"] =
                        nlohmann::json::array();
                    asyncResp->res.jsonValue["Members@odata.count"] = 0;

                    for (auto& obj : subtree)
                    {
                        sdbusplus::message::object_path path(obj.first);
                        std::string swId = path.filename();
                        if (swId.empty())
                        {
                            messages::internalError(asyncResp->res);
                            BMCWEB_LOG_DEBUG << "Can't parse software ID!!";
                            return;
                        }

                        nlohmann::json& members =
                            asyncResp->res.jsonValue["Members"];
                        members.push_back(
                            {{"@odata.id",
                              "/redfish/v1/UpdateService/SoftwareInventory/" +
                                  swId}});
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
                "/xyz/openbmc_project/inventory_software",
                static_cast<int32_t>(0),
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Software.Version"});
        });
}

inline static bool validSubpath([[maybe_unused]] const std::string& objPath,
                                [[maybe_unused]] const std::string& objectPath)
{
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
                BMCWEB_LOG_DEBUG << "DBUS response error";
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
                                   "Systems/" PLATFORMSYSTEMID "/"
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
                BMCWEB_LOG_DEBUG << "DBUS response error";
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
                           const dbus::utility::MapperGetSubTreeResponse&
                               subtree) {
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
                                  "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                  "/Storage/" +
                                      path.filename() +
                                      "#/StorageControllers/" +
                                      std::to_string(i)}});
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
                BMCWEB_LOG_DEBUG << "error_code = " << errorCode;
                BMCWEB_LOG_DEBUG << "error msg = " << errorCode.message();
                return;
            }
            std::string chassisName = "chassis";
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_ERROR << "Invalid Object ";
                return;
            }
            for (const std::string& path : *data)
            {
                sdbusplus::message::object_path myLocalPath(path);
                chassisName = myLocalPath.filename();
            }
            nlohmann::json& relatedItem =
                asyncResp->res.jsonValue["RelatedItem"];
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
                BMCWEB_LOG_DEBUG << "error_code = " << errorCode;
                BMCWEB_LOG_DEBUG << "error msg = " << errorCode.message();
                return;
            }
            std::string chassisName = "chassis";
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_ERROR << "Invalid Object ";
                return;
            }
            for (const std::string& path : *data)
            {
                sdbusplus::message::object_path myLocalPath(path);
                chassisName = myLocalPath.filename();
            }
            nlohmann::json& relatedItem =
                asyncResp->res.jsonValue["RelatedItem"];
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
                BMCWEB_LOG_DEBUG << "error_code = " << errorCode;
                BMCWEB_LOG_DEBUG << "error msg = " << errorCode.message();
                return;
            }
            std::string fabricName = "fabric";
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_ERROR << "Invalid Object ";
                return;
            }
            for (const std::string& path : *data)
            {
                sdbusplus::message::object_path myLocalPath(path);
                fabricName = myLocalPath.filename();
            }
            nlohmann::json& relatedItem =
                asyncResp->res.jsonValue["RelatedItem"];
            nlohmann::json& relatedItemCount =
                asyncResp->res.jsonValue["RelatedItem@odata.count"];
            relatedItem.push_back(
                {{"@odata.id", "/redfish/v1/Fabrics/" + fabricName +
                                   "/Switches/" + objPath.filename()}});

            relatedItemCount = relatedItem.size();
        },
        "xyz.openbmc_project.ObjectMapper", objPath.str + "/fabrics",
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
                BMCWEB_LOG_ERROR << "error_code = " << ec
                                 << ", error msg = " << ec.message();
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
                              "/redfish/v1/Systems/" PLATFORMSYSTEMID
                              "/Processors/" +
                                  association.filename()}});
                    }

                    if (interfaces == "xyz.openbmc_project.Inventory."
                                      "Item.Board" ||
                        interfaces == "xyz.openbmc_project.Inventory."
                                      "Item.Chassis")
                    {
                        relatedItem.push_back(
                            {{"@odata.id", "/redfish/v1/Chassis/" +
                                               association.filename()}});
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
                }
            }

            relatedItemCount = relatedItem.size();
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", association.str,
        std::array<const char*, 9>{
            "xyz.openbmc_project.Inventory.Item.PowerSupply",
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "xyz.openbmc_project.Inventory.Item.PCIeDevice",
            "xyz.openbmc_project.Inventory.Item.Switch",
            "xyz.openbmc_project.Inventory.Item.Cpu",
            "xyz.openbmc_project.Inventory.Item.Drive",
            "xyz.openbmc_project.Inventory.Item.Board",
            "xyz.openbmc_project.Inventory.Item.Chassis",
            "xyz.openbmc_project.Inventory.Item.StorageController"});
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

    BMCWEB_LOG_DEBUG << "getRelatedItemsOthers enter";

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

            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
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
                            BMCWEB_LOG_ERROR
                                << "error_code = " << errCodeAssoc
                                << ", error msg = " << errCodeAssoc.message();
                            return;
                        }

                        std::vector<std::string>* associations =
                            std::get_if<std::vector<std::string>>(&resp);
                        if ((associations == nullptr) ||
                            (associations->empty()))
                        {
                            BMCWEB_LOG_ERROR
                                << "Zero association for the software";
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
    getRelatedItems(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                    const std::string& swId, const std::string& purpose)
{
    if (purpose == fw_util::biosPurpose)
    {
        nlohmann::json& relatedItem = aResp->res.jsonValue["RelatedItem"];
        relatedItem.push_back(
            {{"@odata.id", "/redfish/v1/Systems/" PLATFORMSYSTEMID "/Bios"}});
        aResp->res.jsonValue["Members@odata.count"] = relatedItem.size();
    }
    else if (purpose == fw_util::otherPurpose || purpose == fw_util::bmcPurpose)
    {
        getRelatedItemsOthers(aResp, swId);
    }
    else
    {
        BMCWEB_LOG_ERROR << "Unknown software purpose " << purpose;
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
         swId](const boost::system::error_code errorCode,
               const dbus::utility::DBusPropertiesMap& propertiesList) {
            if (errorCode)
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
                BMCWEB_LOG_DEBUG << "Can't find property \"Purpose\"!";
                messages::internalError(asyncResp->res);
                return;
            }

            BMCWEB_LOG_DEBUG << "swInvPurpose = " << *swInvPurpose;

            if (version == nullptr)
            {
                BMCWEB_LOG_DEBUG << "Can't find property \"Version\"!";

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

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES

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
                BMCWEB_LOG_ERROR << "Failed to GetObject for ComputeDigest: "
                                 << ec;
                messages::internalError(asyncResp->res);
                return;
            }
            // Ensure we only got one service back
            if (objInfo.size() != 1)
            {
                BMCWEB_LOG_ERROR << "Invalid Object Size " << objInfo.size();
                messages::internalError(asyncResp->res);
                return;
            }
            const std::string hashComputeService = objInfo[0].first;
            unsigned retimerId;
            try
            {
                retimerId = std::stoul(swId.substr(swId.rfind("_") + 1));
            }
            catch (const std::exception& e)
            {
                BMCWEB_LOG_ERROR << "Error while parsing retimer Id: "
                                 << e.what();
                messages::internalError(asyncResp->res);
                return;
            }
            // callback to reset hash compute state for timeout scenario
            auto timeoutCallback = [](const std::string_view state,
                                      size_t index) {
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
                    boost::system::error_code ec,
                    sdbusplus::message::message& msg,
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
                            BMCWEB_LOG_ERROR
                                << "Signal doesn't have Digest value";
                            return !task::completed;
                        }
                        auto value = std::get_if<std::string>(&(it->second));
                        if (!value)
                        {
                            BMCWEB_LOG_ERROR
                                << "Digest value is not a string";
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
                                        BMCWEB_LOG_ERROR
                                            << "DBUS response error for Algorithm";
                                        taskData->state = "Exception";
                                        taskData->messages.emplace_back(
                                            messages::taskAborted(
                                                std::to_string(
                                                    taskData->index)));
                                        return;
                                    }
                                    const std::string* hashAlgoValue =
                                        std::get_if<std::string>(&property);
                                    if (hashAlgoValue == nullptr)
                                    {
                                        BMCWEB_LOG_ERROR
                                            << "Null value returned for Algorithm";
                                        taskData->state = "Exception";
                                        taskData->messages.emplace_back(
                                            messages::taskAborted(
                                                std::to_string(
                                                    taskData->index)));
                                        return;
                                    }

                                    nlohmann::json jsonResponse;
                                    jsonResponse["FirmwareDigest"] =
                                        hashDigestValue;
                                    jsonResponse
                                        ["FirmwareDigestHashingAlgorithm"] =
                                            *hashAlgoValue;
                                    taskData->taskResponse.emplace(
                                        jsonResponse);
                                    std::string location =
                                        "Location: /redfish/v1/TaskService/Tasks/" +
                                        std::to_string(taskData->index) +
                                        "/Monitor";
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
                            BMCWEB_LOG_ERROR
                                << "GetHash failed. Digest is empty.";
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
                        BMCWEB_LOG_ERROR << "Failed to ComputeDigest: " << ec;
                        task->state = "Aborted";
                        task->messages.emplace_back(
                            messages::resourceErrorsDetectedFormatError(
                                "NvidiaSoftwareInventory.ComputeDigest",
                                ec.message()));
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
                    asyncResp->res, "NvidiaSoftwareInventory.ComputeDigest",
                    swId);
                BMCWEB_LOG_ERROR << "Invalid object path: " << ec;
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
        .methods(
            boost::beast::http::verb::
                post)([&app](
                          const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& param) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            BMCWEB_LOG_DEBUG
                << "Enter NvidiaSoftwareInventory.ComputeDigest doPost";
            std::shared_ptr<std::string> swId =
                std::make_shared<std::string>(param);
            // skip input parameter validation

            // 1. Firmware update and retimer hash cannot run in parallel
            if (fwUpdateInProgress)
            {
                redfish::messages::updateInProgressMsg(
                    asyncResp->res,
                    "Retry the operation once firmware update operation is complete.");
                BMCWEB_LOG_ERROR
                    << "Cannot execute ComputeDigest. Update firmware is in progress.";

                return;
            }
            // 2. Only one compute hash allowed at a time due to FPGA limitation
            if (computeDigestInProgress)
            {
                redfish::messages::resourceErrorsDetectedFormatError(
                    asyncResp->res, "NvidiaSoftwareInventory.ComputeDigest",
                    "Another ComputeDigest operation is in progress");
                BMCWEB_LOG_ERROR << "Cannot execute ComputeDigest."
                                 << " Another ComputeDigest is in progress.";
                return;
            }
            handlePostComputeDigest(req, asyncResp, *swId);
            BMCWEB_LOG_DEBUG << "Exit NvidiaUpdateService.ComputeDigest doPost";
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
                        .jsonValue["Actions"]["Oem"]["Nvidia"]
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
#endif

inline void requestRoutesSoftwareInventory(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/FirmwareInventory/<str>/")
        .privileges(redfish::privileges::getSoftwareInventory)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& param) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            std::shared_ptr<std::string> swId =
                std::make_shared<std::string>(param);

            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/UpdateService/FirmwareInventory/" + *swId;

            crow::connections::systemBus->async_method_call(
                [asyncResp, swId](
                    const boost::system::error_code ec,
                    const std::vector<
                        std::pair<std::string,
                                  std::vector<std::pair<
                                      std::string, std::vector<std::string>>>>>&
                        subtree) {
                    BMCWEB_LOG_DEBUG << "doGet callback...";
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    // Ensure we find our input swId, otherwise return an
                    // error
                    bool found = false;
                    for (const auto& [path, serviceMap] : subtree)
                    {
                        sdbusplus::message::object_path objPath(path);
                        if (boost::equals(objPath.filename(), *swId) != true)
                        {
                            continue;
                        }

                        if (serviceMap.size() < 1)
                        {
                            continue;
                        }

                        found = true;
                        std::string settingService;
                        std::string versionService;
                        for (const auto& [service, interface] : serviceMap)
                        {
                            if (std::ranges::find(interface, "xyz.openbmc_project.Software.Settings") !=
                                interface.end())
                            {
                                settingService = service;
                            }
                            if (std::ranges::find(interface, "xyz.openbmc_project.Software.Version") !=
                                interface.end())
                            {
                                versionService = service;
                            }
                        }

                        if (versionService.empty())
                        {
                            BMCWEB_LOG_ERROR
                                << "Firmware Inventory: Software.Version interface is missing for swId: "
                                << *swId;
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        // The settingService is used for populating
                        // WriteProtected property. This property is optional
                        // and not implemented on all devices.
                        if (!settingService.empty())
                        {
                            fw_util::getFwWriteProtectedStatus(asyncResp, swId,
                                                               settingService);
                        }
                        fw_util::getFwStatus(asyncResp, swId, versionService);

                        crow::connections::systemBus->async_method_call(
                            [asyncResp,
                             swId](const boost::system::error_code errorCode,
                                   const boost::container::flat_map<
                                       std::string,
                                       dbus::utility::DbusVariantType>&
                                       propertiesList) {
                                if (errorCode)
                                {
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                boost::container::flat_map<
                                    std::string,
                                    dbus::utility::DbusVariantType>::
                                    const_iterator it =
                                        propertiesList.find("Purpose");
                                if (it == propertiesList.end())
                                {
                                    BMCWEB_LOG_DEBUG
                                        << "Can't find property \"Version\"!";
                                    messages::propertyMissing(asyncResp->res,
                                                              "Purpose");
                                    return;
                                }
                                const std::string* swInvPurpose =
                                    std::get_if<std::string>(&it->second);
                                if (swInvPurpose == nullptr)
                                {
                                    BMCWEB_LOG_DEBUG
                                        << "wrong types for property\"Purpose\"!";
                                    messages::propertyValueTypeError(
                                        asyncResp->res, "", "Purpose");
                                    return;
                                }

                                BMCWEB_LOG_DEBUG << "swInvPurpose = "
                                                 << *swInvPurpose;
                                it = propertiesList.find("Version");
                                if (it == propertiesList.end())
                                {
                                    BMCWEB_LOG_DEBUG
                                        << "Can't find property \"Version\"!";
                                    messages::propertyMissing(asyncResp->res,
                                                              "Version");
                                    return;
                                }

                                BMCWEB_LOG_DEBUG << "Version found!";

                                const std::string* version =
                                    std::get_if<std::string>(&it->second);

                                if (version == nullptr)
                                {
                                    BMCWEB_LOG_DEBUG
                                        << "Can't find property \"Version\"!";

                                    messages::propertyValueTypeError(
                                        asyncResp->res, "", "Version");
                                    return;
                                }


                                it = propertiesList.find("Manufacturer");
                                if (it != propertiesList.end())
                                {
                                    const std::string* manufacturer =
                                        std::get_if<std::string>(
                                            &it->second);

                                    if (manufacturer == nullptr)
                                    {
                                        BMCWEB_LOG_ERROR
                                            << "Can't find property \"Manufacturer\"!";
                                        messages::internalError(
                                            asyncResp->res);
                                        return;
                                    }
                                    asyncResp->res
                                        .jsonValue["Manufacturer"] =
                                        *manufacturer;
                                }


                                it = propertiesList.find("SoftwareId");
                                if (it != propertiesList.end())
                                {
                                    const std::string* softwareId =
                                        std::get_if<std::string>(&it->second);

                                    if (softwareId == nullptr)
                                    {
                                        BMCWEB_LOG_ERROR
                                            << "Can't find property \"softwareId\"!";
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }
                                    asyncResp->res.jsonValue["SoftwareId"] =
                                        *softwareId;
                                }

                                asyncResp->res.jsonValue["Version"] = *version;
                                asyncResp->res.jsonValue["Id"] = *swId;

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

                                std::string formatDesc =
                                    swInvPurpose->substr(endDesc);
                                asyncResp->res.jsonValue["Description"] =
                                    formatDesc + " image";
                                getRelatedItems(asyncResp, *swId,
                                                *swInvPurpose);
                            },
                            versionService, path,
                            "org.freedesktop.DBus.Properties", "GetAll","");

                        asyncResp->res.jsonValue["Status"]["Health"] = "OK";
                        asyncResp->res.jsonValue["Status"]["HealthRollup"] =
                            "OK";
                        asyncResp->res.jsonValue["Status"]["Conditions"] =
                            nlohmann::json::array();
                    }
                    if (!found)
                    {
                        BMCWEB_LOG_ERROR
                            << "Input swID " + *swId + " not found!";
                        messages::resourceMissingAtURI(
                            asyncResp->res,
                            crow::utility::urlFromPieces(
                                "redfish", "v1", "UpdateService",
                                "FirmwareInventory", *swId));
                        return;
                    }
                    asyncResp->res.jsonValue["@odata.type"] =
                        "#SoftwareInventory.v1_4_0.SoftwareInventory";
                    asyncResp->res.jsonValue["Name"] = "Software Inventory";

                    asyncResp->res.jsonValue["Updateable"] = false;
                    fw_util::getFwUpdateableStatus(asyncResp, swId);
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
                    updateOemActionComputeDigest(asyncResp, *swId);
#endif
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree", "/xyz/openbmc_project/software/",
                static_cast<int32_t>(0),
                std::array<const char*, 2>{
                    "xyz.openbmc_project.Software.Version", "xyz.openbmc_project.Software.Settings"});
        });
}

inline void requestRoutesInventorySoftware(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/SoftwareInventory/<str>/")
        .privileges(redfish::privileges::getSoftwareInventory)
        .methods(
            boost::beast::http::verb::get)([](const crow::Request&,
                                              const std::shared_ptr<
                                                  bmcweb::AsyncResp>& asyncResp,
                                              const std::string& param) {
            std::string searchPath = "/xyz/openbmc_project/inventory_software/";
            std::shared_ptr<std::string> swId =
                std::make_shared<std::string>(param);

            crow::connections::systemBus->async_method_call(
                [asyncResp, swId, searchPath](
                    const boost::system::error_code ec,
                    const std::vector<
                        std::pair<std::string,
                                  std::vector<std::pair<
                                      std::string, std::vector<std::string>>>>>&
                        subtree) {
                    BMCWEB_LOG_DEBUG << "doGet callback...";
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    // Ensure we find our input swId, otherwise return an
                    // error
                    for (const std::pair<
                             std::string,
                             std::vector<std::pair<
                                 std::string, std::vector<std::string>>>>& obj :
                         subtree)
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
                        asyncResp->res.jsonValue["Status"]["HealthRollup"] =
                            "OK";
                        asyncResp->res.jsonValue["Status"]["Conditions"] =
                            nlohmann::json::array();

                        crow::connections::systemBus->async_method_call(
                            [asyncResp, swId, path, searchPath](
                                const boost::system::error_code errorCode,
                                const boost::container::flat_map<
                                    std::string,
                                    dbus::utility::DbusVariantType>&
                                    propertiesList) {
                                if (errorCode)
                                {
                                    BMCWEB_LOG_DEBUG << "properties not found ";
                                    messages::internalError(asyncResp->res);
                                    return;
                                }

                                for (const auto& property : propertiesList)
                                {
                                    if (property.first == "Manufacturer")
                                    {
                                        const std::string* manufacturer =
                                            std::get_if<std::string>(
                                                &property.second);
                                        if (manufacturer != nullptr)
                                        {
                                            asyncResp->res
                                                .jsonValue["Manufacturer"] =
                                                *manufacturer;
                                        }
                                    }
                                    else if (property.first == "Version")
                                    {
                                        const std::string* version =
                                            std::get_if<std::string>(
                                                &property.second);
                                        if (version != nullptr)
                                        {
                                            asyncResp->res
                                                .jsonValue["Version"] =
                                                *version;
                                        }
                                    }
                                    else if (property.first == "Functional")
                                    {
                                        const bool* swInvFunctional =
                                            std::get_if<bool>(&property.second);
                                        if (swInvFunctional != nullptr)
                                        {
                                            BMCWEB_LOG_DEBUG
                                                << " Functinal "
                                                << *swInvFunctional;
                                            if (*swInvFunctional)
                                            {
                                                asyncResp->res
                                                    .jsonValue["Status"]
                                                              ["State"] =
                                                    "Enabled";
                                            }
                                            else
                                            {
                                                asyncResp->res
                                                    .jsonValue["Status"]
                                                              ["State"] =
                                                    "Disabled";
                                            }
                                        }
                                    }
                                }
                                getRelatedItemsOthers(asyncResp, *swId,
                                                      searchPath);
                                fw_util::getFwUpdateableStatus(asyncResp, swId,
                                                               searchPath);
                            },
                            obj.second[0].first, obj.first,
                            "org.freedesktop.DBus.Properties", "GetAll", "");
                        asyncResp->res.jsonValue["@odata.id"] =
                            "/redfish/v1/UpdateService/SoftwareInventory/" +
                            *swId;
                        asyncResp->res.jsonValue["@odata.type"] =
                            "#SoftwareInventory.v1_4_0.SoftwareInventory";
                        asyncResp->res.jsonValue["Name"] = "Software Inventory";
                        return;
                    }
                    // Couldn't find an object with that name.  return an error
                    BMCWEB_LOG_DEBUG << "Input swID " + *swId + " not found!";
                    messages::resourceNotFound(
                        asyncResp->res,
                        "SoftwareInventory.v1_4_0.SoftwareInventory", *swId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree", searchPath,
                static_cast<int32_t>(0),
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Software.Version"});
        });

    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/FirmwareInventory/<str>/")
        .privileges(redfish::privileges::patchUpdateService)
        .methods(
            boost::beast::http::verb::
                patch)([](const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& param) {
            BMCWEB_LOG_DEBUG << "doPatch...";
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
                        const std::vector<std::pair<
                            std::string,
                            std::vector<std::pair<std::string,
                                                  std::vector<std::string>>>>>&
                            subtree) {
                        if (ec)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        for (const std::pair<
                                 std::string,
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
                            fw_util::patchFwWriteProtectedStatus(
                                asyncResp, swId, obj.second[0].first,
                                *writeProtected);

                            return;
                        }
                        // Couldn't find an object with that name.  return an
                        // error
                        BMCWEB_LOG_DEBUG
                            << "Input swID " + *swId + " not found!";
                        messages::resourceNotFound(
                            asyncResp->res,
                            "SoftwareInventory.v1_4_0.SoftwareInventory",
                            *swId);
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTree", "/xyz/openbmc_project/software/",
                    static_cast<int32_t>(0),
                    std::array<const char*, 1>{
                        "xyz.openbmc_project.Software.Version"});
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
    parameterTargets["Required"] = "false";
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
            BMCWEB_LOG_DEBUG << "Cannot parse firmware ID";
            return;
        }

        if (isInventoryAllowableValue(obj.first))
        {
            allowableValues.push_back(
                "/redfish/v1/UpdateService/FirmwareInventory/" + fwId);
        }
    }

    parameters.push_back(parameterTargets);
}

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

    if (!json_util::readJsonAction(req, asyncResp->res,
        "Targets", targets))
    {
        return;
    }

    bool hasTargets = false;

    if (targets && targets.value().empty() == false)
    {
        hasTargets = true;
    }

    if (hasTargets)
    {
        std::vector<std::string> targetsCollection = targets.value();

        for (auto& target : targetsCollection)
        {
            sdbusplus::message::object_path objectPath(target);
            std::string inventoryPath =
                "/xyz/openbmc_project/software/" + objectPath.filename();
            std::pair<bool, CommitImageValueEntry> result =
                getAllowableValue(inventoryPath);
            if (result.first == true)
            {
                uint32_t eid = result.second.mctpEndpointId;

                initBackgroundCopy(req, asyncResp, eid, result.second.inventoryUri);
            }
            else
            {
                BMCWEB_LOG_DEBUG
                    << "Cannot find firmware inventory in allowable values";
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
                uint32_t eid = result.second.mctpEndpointId;

                initBackgroundCopy(req, asyncResp, eid, result.second.inventoryUri);
            }
        }
    }
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
        .methods(
            boost::beast::http::verb::
                get)([](const crow::Request&,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            asyncResp->res.jsonValue["@odata.type"] =
                "#ActionInfo.v1_2_0.ActionInfo";
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/UpdateService/Oem/Nvidia/CommitImageActionInfo";
            asyncResp->res.jsonValue["Name"] = "CommitImage Action Info";
            asyncResp->res.jsonValue["Id"] = "CommitImageActionInfo";

            crow::connections::systemBus->async_method_call(
                [asyncResp{asyncResp}](
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

                    updateParametersForCommitImageInfo(asyncResp, subtree);
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
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Software.Version"});
        });

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.CommitImage")
        .privileges(redfish::privileges::postUpdateService)
        .methods(
            boost::beast::http::verb::
                post)([](const crow::Request& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            BMCWEB_LOG_DEBUG << "doPost...";

            if (fwUpdateInProgress == true)
            {
                redfish::messages::updateInProgressMsg(
                    asyncResp->res,
                    "Retry the operation once firmware update operation is complete.");

                // don't copy the image, update already in progress.
                BMCWEB_LOG_ERROR
                    << "Cannot execute commit image. Update firmware is in progress.";

                return;
            }

            crow::connections::systemBus->async_method_call(
                [req, asyncResp{asyncResp}](
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
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Software.Version"});
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
    return nlohmann::json{
        {"@odata.type", "#Message.v1_1_1.Message"},
        {"Message", msg},
        {"MessageArgs", {arg}}};
}

inline void requestRoutesUpdateServicePublicKeyExchange(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.PublicKeyExchange")
        .privileges(redfish::privileges::postUpdateService)
        .methods(
            boost::beast::http::verb::
                post)([&app](const crow::Request& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            std::string remoteServerIP;
            std::string remoteServerKeyString; // "<type> <key>"

            BMCWEB_LOG_DEBUG << "Enter UpdateService.PublicKeyExchange doPost";

            if (!json_util::readJsonAction(
                    req, asyncResp->res, "RemoteServerIP", remoteServerIP,
                    "RemoteServerKeyString", remoteServerKeyString)
                    && (remoteServerIP.empty()||remoteServerKeyString.empty()))
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
                messages::createFailedMissingReqProperties(
                        asyncResp->res, emptyprops);
                BMCWEB_LOG_DEBUG << "Missing " << emptyprops;
                return;
            }

            BMCWEB_LOG_DEBUG << "RemoteServerIP: " << remoteServerIP +
            " RemoteServerKeyString: " << remoteServerKeyString;

            // Verify remoteServerKeyString matches the pattern "<type> <key>"
            std::string remoteServerKeyStringPattern = R"(\S+\s+\S+)";
            std::regex pattern(remoteServerKeyStringPattern);
            if (!std::regex_match(remoteServerKeyString, pattern))
            {
                // Invalid format, return an error message
                messages::actionParameterValueTypeError(
                                        asyncResp->res,
                                        remoteServerKeyString,
                                        "RemoteServerKeyString",
                                        "UpdateService.PublicKeyExchange");
                BMCWEB_LOG_DEBUG << "Invalid RemoteServerKeyString format";
                return;
            }

            // Call SCP service
            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        BMCWEB_LOG_ERROR << "error_code = " << ec << " " <<
                                            "error msg = " << ec.message();
                        return;
                    }

                    crow::connections::systemBus->async_method_call(
                        [asyncResp](const boost::system::error_code ec,
                                    const std::string& selfPublicKeyStr) {
                            if (ec || selfPublicKeyStr.empty())
                            {
                                messages::internalError(asyncResp->res);
                                BMCWEB_LOG_ERROR << "error_code = " << ec << " " <<
                                                    "error msg = " << ec.message();
                                return;
                            }

                            // Create a JSON object with the additional information
                            std::string keyMsg =
                            "Please add the following public key info to "
                            "~/.ssh/authorized_keys on the remote server";
                            std::string keyInfo = selfPublicKeyStr + " root@dpu-bmc";

                            asyncResp->res.jsonValue[messages::messageAnnotation] = nlohmann::json::array();
                            asyncResp->res.jsonValue[messages::messageAnnotation].push_back(
                                extendedInfoSuccessMsg(keyMsg,keyInfo));
                            messages::success(asyncResp->res);
                            BMCWEB_LOG_DEBUG
                            << "Call to PublicKeyExchange succeeded " + selfPublicKeyStr;
                    },
                    "xyz.openbmc_project.Software.Download",
                    "/xyz/openbmc_project/software",
                    "xyz.openbmc_project.Common.SCP", "GenerateSelfKeyPair");
                },
                "xyz.openbmc_project.Software.Download",
                "/xyz/openbmc_project/software",
                "xyz.openbmc_project.Common.SCP", "AddRemoteServerPublicKey",
                remoteServerIP, remoteServerKeyString);
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
        "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.RevokeAllRemoteServerPublicKeys")
        .privileges(redfish::privileges::postUpdateService)
        .methods(
            boost::beast::http::verb::
                post)([&app](const crow::Request& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            std::string remoteServerIP;

            BMCWEB_LOG_DEBUG << "Enter UpdateService.RevokeAllRemoteServerPublicKeys doPost";

            if (!json_util::readJsonAction(
                    req, asyncResp->res, "RemoteServerIP", remoteServerIP)
                    && remoteServerIP.empty())
            {
                messages::createFailedMissingReqProperties(
                        asyncResp->res, "RemoteServerIP");
                BMCWEB_LOG_DEBUG << "Missing RemoteServerIP";
                return;
            }

            BMCWEB_LOG_DEBUG << "RemoteServerIP: " << remoteServerIP;

            // Call SCP service
            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        BMCWEB_LOG_ERROR << "error_code = " << ec << " " <<
                                            "error msg = " << ec.message();
                    }
                    else
                    {
                        messages::success(asyncResp->res);
                        BMCWEB_LOG_DEBUG
                            << "Call to RevokeAllRemoteServerPublicKeys succeeded";
                    }
                },
                "xyz.openbmc_project.Software.Download",
                "/xyz/openbmc_project/software",
                "xyz.openbmc_project.Common.SCP", "RevokeAllRemoteServerPublicKeys",
                remoteServerIP);
        });   
}

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
/**
 * @brief Create task for tracking firmware package staging.
 *
 * @param[in] asyncResp - http response
 * @param[in] req - http request
 * @param[in] timeoutTimeSeconds - the timeout duration for the staging task in
 * seconds, default is set to fwObjectCreationDefaultTimeout.
 *
 * @return None
 */
static void createStageFirmwarePackageTask(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const crow::Request& req,
    int timeoutTimeSeconds = fwObjectCreationDefaultTimeout,
    const std::string& imagePath = {})
{
    auto messageHandlerCallback = [imagePath](const std::string_view state,
                                              size_t index) {
        nlohmann::json message{};
        if (state == "Started")
        {
            message = messages::taskStarted(std::to_string(index));
        }
        else if (state == "Aborted" || state == "Exception")
        {
            cleanUpStageObjects();

            if (!imagePath.empty())
            {
                std::filesystem::remove(imagePath);
            }

            message = messages::taskAborted(std::to_string(index));
        }
        return message;
    };

    std::shared_ptr<task::TaskData> task = task::TaskData::createTask(
        [](boost::system::error_code ec, sdbusplus::message::message& msg,
           const std::shared_ptr<task::TaskData>& taskData) {
            if (ec)
            {
                taskData->state = "Exception";
                taskData->messages.emplace_back(
                    messages::resourceErrorsDetectedFormatError(
                        "Stage firmware package task", ec.message()));
                taskData->finishTask();

                return task::completed;
            }

            sdbusplus::message::object_path path;
            dbus::utility::DBusInteracesMap interfaces;
            msg.read(path, interfaces);

            if (std::find_if(
                    interfaces.begin(), interfaces.end(), [](const auto& i) {
                        return i.first ==
                               "xyz.openbmc_project.Software.PackageInformation";
                    }) != interfaces.end())
            {
                std::string stagedFirmwarePackageURI(
                    "/redfish/v1/UpdateService/Oem/Nvidia/PersistentStorage/FirmwarePackages/0");

                nlohmann::json jsonResponse;
                jsonResponse["StagedFirmwarePackageURI"] =
                    stagedFirmwarePackageURI;

                taskData->taskResponse.emplace(jsonResponse);
                std::string location =
                    "Location: /redfish/v1/TaskService/Tasks/" +
                    std::to_string(taskData->index) + "/Monitor";
                taskData->payload->httpHeaders.emplace_back(
                    std::move(location));
                taskData->state = "Completed";
                taskData->percentComplete = 100;
                taskData->messages.emplace_back(
                    messages::taskCompletedOK(std::to_string(taskData->index)));
                taskData->timer.cancel();
                taskData->finishTask();
                cleanUpStageObjects();
                return task::completed;
            }
            return !task::completed;
        },
        "type='signal',member='InterfacesAdded',"
        "interface='org.freedesktop.DBus.ObjectManager',"
        "path='/'",
        messageHandlerCallback);
    task->startTimer(std::chrono::seconds(timeoutTimeSeconds));
    task->populateResp(asyncResp->res);
    task->payload.emplace(req);
}

/**
 * @brief Add dbus watcher to wait till staged firmware object is created
 * and upload firmware package
 *
 * @param asyncResp
 * @param req
 *
 * @return None
 */
inline void addDBusWatchAndUploadPackage(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const crow::Request& req)
{
    std::string filepath(
        updateServiceStageLocation +
        boost::uuids::to_string(boost::uuids::random_generator()()));

    try
    {
        createStageFirmwarePackageTask(
            asyncResp, req, fwObjectCreationDefaultTimeout, filepath);

        fwImageIsStaging = true;
        BMCWEB_LOG_DEBUG << "Writing file to " << filepath;
        std::ofstream out(filepath, std::ofstream::out | std::ofstream::binary |
                                        std::ofstream::trunc);
        out << req.body;
        out.close();
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR << "Error while uploading firmware: " << e.what();
        messages::internalError(asyncResp->res);
        cleanUpStageObjects();
        fwStageAvailableTimer = nullptr;
        return;
    }
}

/**
 * @brief Stage firmware package and fill dbus tree
 *
 * @param asyncResp
 * @param req
 *
 * @return None
 */
inline void
    stageFirmwarePackage(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const crow::Request& req)
{
    crow::connections::systemBus->async_method_call(
        [req, asyncResp](const boost::system::error_code ec,
                         const crow::openbmc_mapper::GetSubTreeType& subtree) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            bool found = false;
            std::string foundService;
            std::string foundPath;

            for (const auto& obj : subtree)
            {
                if (obj.second.size() < 1)
                {
                    break;
                }

                foundService = obj.second[0].first;
                foundPath = obj.first;

                found = true;
                break;
            }

            if (found)
            {
                auto respHandler = [req, asyncResp](
                                       const boost::system::error_code ec) {
                    BMCWEB_LOG_DEBUG << "doDelete callback: Done";
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR << "doDelete respHandler got error "
                                         << ec.message();
                        asyncResp->res.result(
                            boost::beast::http::status::internal_server_error);
                        return;
                    }

                    addDBusWatchAndUploadPackage(asyncResp, req);
                };

                crow::connections::systemBus->async_method_call(
                    respHandler, foundService, foundPath,
                    "xyz.openbmc_project.Object.Delete", "Delete");
            }
            else
            {
                addDBusWatchAndUploadPackage(asyncResp, req);
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/software", static_cast<int32_t>(0),
        std::array<const char*, 1>{
            "xyz.openbmc_project.Software.PackageInformation"});
}

/**
 * @brief handle stage location errors and map to redfish errors
 *
 * @param[in] req
 * @param[in] asyncResp
 */
inline void handleStageLocationErrors(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::string getCommand = "/sbin/fw_printenv";
    auto emmcStatusCallback =
        []([[maybe_unused]] const crow::Request& req,
           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
           const std::string& stdOut,
           [[maybe_unused]] const std::string& stdErr,
           [[maybe_unused]] const boost::system::error_code& ec,
           [[maybe_unused]] int errorCode) -> void {
        if (stdOut.find("emmc=enable") != std::string::npos)
        {
            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec,
                            const std::variant<int32_t>& property) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR
                            << "DBUS response error getting service status: "
                            << ec.message();
                        redfish::messages::internalError(asyncResp->res);
                        return;
                    }
                    const int32_t* serviceStatus =
                        std::get_if<int32_t>(&property);
                    if (serviceStatus == nullptr)
                    {
                        BMCWEB_LOG_ERROR << "Invalid service exit status code";
                        redfish::messages::internalError(asyncResp->res);
                        return;
                    }
                    std::optional<ErrorMapping> errorMapping =
                        getEMMCErrorMessageFromExitCode(*serviceStatus);
                    if (errorMapping)
                    {
                        BMCWEB_LOG_ERROR
                            << "stage-firmware-package Error Message: "
                            << (*errorMapping).first;
                        redfish::messages::resourceErrorsDetectedFormatError(
                            asyncResp->res,
                            "/redfish/v1/UpdateService/Oem/Nvidia/PersistentStorage/stage-firmware-package",
                            (*errorMapping).first, (*errorMapping).second);
                    }
                    else
                    {
                        redfish::messages::internalError(asyncResp->res);
                    }
                },
                "org.freedesktop.systemd1",
                "/org/freedesktop/systemd1/unit/nvidia_2demmc_2dpartition_2eservice",
                "org.freedesktop.DBus.Properties", "Get",
                "org.freedesktop.systemd1.Service", "ExecMainStatus");
        }
        else
        {
            BMCWEB_LOG_INFO << "PersistentStorage is not enabled.";
            messages::serviceDisabled(
                asyncResp->res,
                "/redfish/v1/Managers/" PLATFORMBMCID);
        }
        return;
    };
    PersistentStorageUtil persistentStorageUtil;
    persistentStorageUtil.executeEnvCommand(req, asyncResp, getCommand,
                                            std::move(emmcStatusCallback));
}

/**
 * @brief POST handler for staging firmware package
 *
 * @param app
 * @param req
 * @param asyncResp
 *
 * @return None
 */
inline void handleUpdateServiceStageFirmwarePackagePost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    BMCWEB_LOG_DEBUG << "doPost...";

    if (req.body.size() == 0)
    {
        if (asyncResp)
        {
            BMCWEB_LOG_ERROR << "Image size equals 0.";
            messages::unrecognizedRequestBody(asyncResp->res);
        }
        return;
    }

    if (req.body.size() > firmwareImageLimitBytes)
    {
        if (asyncResp)
        {
            BMCWEB_LOG_ERROR << "Large image size: " << req.body.size();
            std::string resolution =
                "Firmware package size is greater than allowed "
                "size. Make sure package size is less than "
                "UpdateService.MaxImageSizeBytes property and "
                "retry the firmware update operation.";
            messages::payloadTooLarge(asyncResp->res, resolution);
        }
        return;
    }

    // staging is not allowed when firmware update is in progress.
    if (fwUpdateInProgress != false)
    {
        if (asyncResp)
        {
            // don't copy the image, update already in progress.
            std::string resolution = "Another update is in progress. Retry"
                                     " the operation once it is complete.";
            redfish::messages::updateInProgressMsg(asyncResp->res, resolution);
            BMCWEB_LOG_ERROR << "Update already in progress.";
        }
        return;
    }

    // Only allow one FW staging at a time
    if (fwImageIsStaging == true)
    {
        if (asyncResp)
        {
            redfish::messages::resourceErrorsDetectedFormatError(
                asyncResp->res,
                "/redfish/v1/UpdateService/Oem/Nvidia/PersistentStorage/stage-firmware-package",
                "Another staging is in progress.");
            BMCWEB_LOG_ERROR << "Another staging is in progress.";
        }
        return;
    }

    // add rate limit by limiting number of updates per hour
    if (stagedUpdateCount == 0)
    {
        stagedUpdateTimeStamp = std::chrono::steady_clock::now();
        stagedUpdateCount += 1;
    }
    else if (stagedUpdateCount >= maxStagedUpdatesPerHour)
    {
        std::chrono::time_point<std::chrono::steady_clock> currentTime =
            std::chrono::steady_clock::now();
        auto elapsedTime = currentTime - stagedUpdateTimeStamp;
        if (elapsedTime <=
            std::chrono::seconds(stageRateLimitDurationInSeconds))
        {
            BMCWEB_LOG_ERROR << "Staging update ratelimit error. Duration: "
                             << elapsedTime.count();
            redfish::messages::resourceErrorsDetectedFormatError(
                asyncResp->res, "StageFirmwarePackage",
                "Staging Update Ratelimit Error");
            return;
        }

        BMCWEB_LOG_INFO << "Resetting the staging update counter. Duration: "
                        << elapsedTime.count();
        stagedUpdateTimeStamp = currentTime;
        stagedUpdateCount = 1; // reset the counter
    }
    else
    {
        stagedUpdateCount += 1;
    }

    bool stageLocationExists =
        std::filesystem::exists(updateServiceStageLocation);

    if (!stageLocationExists)
    {
        if (asyncResp)
        {
            BMCWEB_LOG_ERROR << "Stage location does not exist. "
                             << updateServiceStageLocation;
            handleStageLocationErrors(req, asyncResp);
        }
        return;
    }

    std::error_code spaceInfoError;
    const std::filesystem::space_info spaceInfo =
        std::filesystem::space(updateServiceStageLocation, spaceInfoError);
    if (spaceInfoError)
    {
        messages::internalError(asyncResp->res);
        return;
    }
    else
    {
        if (spaceInfo.free < req.body.size())
        {
            BMCWEB_LOG_ERROR
                << "Insufficient storage space. Required: " << req.body.size()
                << " Available: " << spaceInfo.free;
            std::string resolution =
                "Reset the baseboard and retry the operation.";
            messages::insufficientStorage(asyncResp->res, resolution);
            return;
        }
    }

    stageFirmwarePackage(asyncResp, req);
    BMCWEB_LOG_DEBUG << "stage firmware package complete";
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

    BMCWEB_LOG_DEBUG << "Unknown status: " << status;
    return "Unknown";
}

/**
 * @brief Fill properties related to StoragePackageInformation in the response
 *
 * @param asyncResp
 * @param service
 * @param path
 *
 * @return None
 */
inline void getPropertiesPersistentStoragePackageInformation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path)
{
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.Software.PackageInformation",
        [asyncResp](const boost::system::error_code errorCode,
                    const dbus::utility::DBusPropertiesMap& propertiesList) {
            if (errorCode)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            const std::string* packageVersion = nullptr;
            const std::string* packageVerificationStatus = nullptr;

            bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), propertiesList,
                "PackageVersion", packageVersion, "VerificationStatus",
                packageVerificationStatus);

            if (!success)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            if (packageVersion == nullptr)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            if (packageVerificationStatus == nullptr)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            asyncResp->res.jsonValue["FirmwarePackageVersion"] =
                *packageVersion;
            asyncResp->res.jsonValue["VerificationStatus"] =
                getPackageVerificationStatus(*packageVerificationStatus);
        });
}

/**
 * @brief Fill properties related to ComputeHash in the response
 *
 * @param asyncResp
 * @param service
 * @param path
 *
 * @return None
 */
inline void getPropertiesPersistentStorageComputeHash(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path)
{
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, path, "com.nvidia.ComputeHash",
        [asyncResp](const boost::system::error_code errorCode,
                    const dbus::utility::DBusPropertiesMap& propertiesList) {
            if (errorCode)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            const std::string* algorithm = nullptr;
            const std::string* digest = nullptr;

            bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), propertiesList, "Algorithm",
                algorithm, "Digest", digest);

            if (!success)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            if (algorithm == nullptr)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            if (digest == nullptr)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            asyncResp->res.jsonValue["FirmwarePackageDigestHashAlgorithm"] =
                *algorithm;
            asyncResp->res.jsonValue["FirmwarePackageDigest"] = *digest;
        });
}

/**
 * @brief Fill property StagedTimestamp in the response
 *
 * @param asyncResp
 * @param service
 * @param path
 *
 * @return None
 */
inline void getPropertiesPersistentStorageEpochTime(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path)
{
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, path,
        "xyz.openbmc_project.Time.EpochTime",
        [asyncResp](const boost::system::error_code errorCode,
                    const dbus::utility::DBusPropertiesMap& propertiesList) {
            if (errorCode)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            const uint64_t* elapsed = nullptr;

            bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), propertiesList, "Elapsed",
                elapsed);

            if (!success)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            if (elapsed == nullptr)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            uint64_t elapsedTimeStamp = *elapsed / 1000;

            asyncResp->res.jsonValue["StagedTimestamp"] =
                redfish::time_utils::getDateTimeUint(elapsedTimeStamp);
        });
}

/**
 * @brief GET handler to present list of staged firmware packages
 *
 * @param app
 * @param req
 * @param asyncResp
 *
 * @return None
 */
inline void handleUpdateServicePersistentStorageFwPackagesListGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaFirmwarePackageCollection.NvidiaFirmwarePackageCollection";
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/UpdateService/Oem/Nvidia/PersistentStorage/FirmwarePackages";
    asyncResp->res.jsonValue["Name"] = "Firmware Package Collection";

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const crow::openbmc_mapper::GetSubTreeType& subtree) {
            BMCWEB_LOG_DEBUG << "doGet callback...";
            if (ec)
            {
                asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
                asyncResp->res.jsonValue["Members@odata.count"] = 0;
                return;
            }

            bool found = false;

            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                     obj : subtree)
            {
                if (obj.second.size() < 1)
                {
                    break;
                }

                asyncResp->res.jsonValue["Members"] = nlohmann::json::array();

                nlohmann::json& members = asyncResp->res.jsonValue["Members"];

                members.push_back(
                    {{"@odata.id",
                      "/redfish/v1/UpdateService/Oem/Nvidia/PersistentStorage/FirmwarePackages/0"}});
                asyncResp->res.jsonValue["Members@odata.count"] =
                    members.size();

                found = true;

                break;
            }

            if (!found)
            {
                asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
                asyncResp->res.jsonValue["Members@odata.count"] = 0;
                return;
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/software/staged", static_cast<int32_t>(0),
        std::array<const char*, 1>{
            "xyz.openbmc_project.Software.PackageInformation"});
}

/**
 * @brief GET handler to present details of staged firmware package
 *
 * @param app
 * @param req
 * @param asyncResp
 * @param strParam
 *
 * @return None
 */
inline void handleUpdateServicePersistentStorageFwPackageGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& strParam)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (fwImageIsStaging == true)
    {
        if (asyncResp)
        {
            std::string resolution = "Staging is in progress. Retry"
                                     " the action once it is complete.";
            redfish::messages::updateInProgressMsg(asyncResp->res, resolution);
            BMCWEB_LOG_ERROR << "Staging is in progress.";
        }
        return;
    }

    // current implementation assume that the collection of FwPackages has only
    // one item
    if (strParam != "0")
    {
        messages::resourceNotFound(asyncResp->res, "FirmwarePackages",
                                   strParam);
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaFirmwarePackage.v1_0_0.NvidiaFirmwarePackage";
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/UpdateService/Oem/Nvidia/PersistentStorage/FirmwarePackages/0";
    asyncResp->res.jsonValue["Id"] = 0;
    asyncResp->res.jsonValue["Name"] = "Firmware Package 0 Resource";

    crow::connections::systemBus->async_method_call(
        [asyncResp,
         &strParam](const boost::system::error_code ec,
                    const crow::openbmc_mapper::GetSubTreeType& subtree) {
            BMCWEB_LOG_DEBUG << "doGet callback...";
            if (ec)
            {
                messages::resourceNotFound(asyncResp->res, "FirmwarePackages",
                                           "0");
                return;
            }
            // Ensure we find package information, otherwise return an
            // error
            bool found = false;

            for (const auto& obj : subtree)
            {
                if (obj.second.size() < 1)
                {
                    break;
                }

                found = true;

                getPropertiesPersistentStoragePackageInformation(
                    asyncResp, obj.second[0].first, obj.first);
                getPropertiesPersistentStorageComputeHash(
                    asyncResp, obj.second[0].first, obj.first);
                getPropertiesPersistentStorageEpochTime(
                    asyncResp, obj.second[0].first, obj.first);
            }
            if (!found)
            {
                BMCWEB_LOG_ERROR << "PackageInformation not found!";
                messages::resourceNotFound(asyncResp->res, "FirmwarePackages",
                                           "0");
                return;
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/software/staged", static_cast<int32_t>(0),
        std::array<const char*, 1>{
            "xyz.openbmc_project.Software.PackageInformation"});
}

/**
 * @brief Initiate staged firmware package.
 * The function create relevant task to monitor status of the process
 *
 * @param asyncResp
 * @param service
 * @param path
 * @param payload
 *
 * @return None
 */
inline void initiateStagedFirmwareUpdate(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path,
    task::Payload&& payload)
{
    auto messageCallback = [](const std::string_view state,
                              [[maybe_unused]] size_t index) {
        nlohmann::json message{};
        if (state == "Started")
        {
            message = messages::taskStarted(std::to_string(index));
        }
        else if (state == "Aborted")
        {
            fwUpdateInProgress = false;
            message = messages::taskAborted(std::to_string(index));
        }
        else if (state == "Completed")
        {
            message = messages::taskCompletedOK(std::to_string(index));
            // fwupdate status is set in task callback
        }
        else
        {
            BMCWEB_LOG_INFO << "State not good";
        }
        return message;
    };

    std::shared_ptr<task::TaskData> task = task::TaskData::createTask(
        [](boost::system::error_code ec, sdbusplus::message_t& msg,
           const std::shared_ptr<task::TaskData>& taskData) {
            if (ec)
            {
                return task::completed;
            }

            std::string iface;
            boost::container::flat_map<std::string,
                                       dbus::utility::DbusVariantType>
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
                    taskData->messages.emplace_back(
                        messages::taskAborted(index));
                    fwUpdateInProgress = false;
                    return task::completed;
                }

                if (boost::ends_with(*state, "Staged"))
                {

                    taskData->state = "Stopping";
                    taskData->messages.emplace_back(
                        messages::taskPaused(index));
                    // its staged, set a long timer to
                    // allow them time to complete the
                    // update (probably cycle the
                    // system) if this expires then
                    // task will be cancelled
                    taskData->extendTimer(std::chrono::hours(5));
                    fwUpdateInProgress = true;
                    return !task::completed;
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
                    taskData->messages.emplace_back(
                        messages::taskCompletedOK(index));
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
                uint8_t* progress =
                    std::get_if<uint8_t>(&(findProgress->second));

                if (progress == nullptr)
                {
                    taskData->messages.emplace_back(messages::internalError());
                    return task::completed;
                }
                taskData->percentComplete = static_cast<int>(*progress);

                taskData->messages.emplace_back(messages::taskProgressChanged(
                    index, static_cast<size_t>(*progress)));

                // if we're getting status updates it's
                // still alive, update timer
                taskData->extendTimer(
                    std::chrono::minutes(updateServiceTaskTimeout));
            }

            return !task::completed;
        },
        "type='signal',interface='org.freedesktop.DBus.Properties',"
        "member='PropertiesChanged',path='" +
            path + "'",
        messageCallback);

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
        task->messages.emplace_back(preTaskMessages);
    }
    preTaskMessages = {};

    fwAvailableTimer = nullptr;
    activateImage(path, service);
}

/**
 * @brief Initiates a staged firmware update for specified targets.
 *
 * This function asynchronously initiates a staged firmware update for the
 * specified targets. It validates the provided target URIs, checks for their
 * existence, and sets the update policy accordingly.
 *
 * @param[in] asyncResp Pointer to the object holding the response data.
 * @param[in] req The Crow HTTP request object.
 * @param[out] foundService Output parameter to store the found service name.
 * @param[out] foundPath Output parameter to store the found software path.
 * @param[in] targets Optional parameter containing a list of target URIs for
 * the firmware update. If not provided, the firmware update is initiated for
 * all available targets.
 *
 * @return None
 */
inline void setTargetsInitiateFirmwarePackage(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const crow::Request& req, std::string& foundService, std::string& foundPath,
    const std::optional<std::vector<std::string>>& targets)
{

    if (targets)
    {
        crow::connections::systemBus->async_method_call(
            [req, asyncResp, foundService, foundPath,
             uriTargets{*targets}](const boost::system::error_code ec,
                                   const std::vector<std::string>& swInvPaths) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR << "D-Bus responses error: " << ec;
                    messages::internalError(asyncResp->res);
                    return;
                }

                std::vector<sdbusplus::message::object_path> httpUriTargets =
                    {};
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
                            if ((idPos == std::string::npos))
                            {
                                continue;
                            }
                            std::string swId = path.substr(idPos);
                            if (swId == objPath)
                            {
                                sdbusplus::message::object_path objpath(path);
                                httpUriTargets.emplace_back(objpath);
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
                        BMCWEB_LOG_ERROR << "Targetted Device not Found!!";
                        messages::invalidObject(
                            asyncResp->res,
                            crow::utility::urlFromPieces("Targets"));
                        return;
                    }
                    // return HTTP200 - Success with errors
                    // when there is partial valid targets
                    if (invalidTargets.size() > 0)
                    {
                        for (const std::string& invalidTarget : invalidTargets)
                        {
                            BMCWEB_LOG_ERROR
                                << "Invalid UriTarget: " << invalidTarget
                                << "\n";
                            messages::propertyValueFormatError(
                                asyncResp->res, invalidTarget, "Targets");
                        }
                        asyncResp->res.result(boost::beast::http::status::ok);
                    }
                    // else all targets are valid
                }
                crow::connections::systemBus->async_method_call(
                    [req, asyncResp, foundService, foundPath, httpUriTargets](
                        const boost::system::error_code errorCode,
                        const std::vector<
                            std::pair<std::string, std::vector<std::string>>>&
                            objInfo) mutable {
                        if (errorCode)
                        {
                            BMCWEB_LOG_ERROR << "error_code = " << errorCode;
                            BMCWEB_LOG_ERROR << "error msg = "
                                             << errorCode.message();
                            if (asyncResp)
                            {
                                messages::internalError(asyncResp->res);
                            }
                            return;
                        }
                        // Ensure we only got one service back
                        if (objInfo.size() != 1)
                        {
                            BMCWEB_LOG_ERROR << "Invalid Object Size "
                                             << objInfo.size();
                            if (asyncResp)
                            {
                                messages::internalError(asyncResp->res);
                            }
                            return;
                        }

                        crow::connections::systemBus->async_method_call(
                            [req, asyncResp, foundService, foundPath](
                                const boost::system::error_code errCodePolicy) {
                                if (errCodePolicy)
                                {
                                    BMCWEB_LOG_ERROR << "error_code = "
                                                     << errCodePolicy;
                                    messages::internalError(asyncResp->res);
                                }

                                task::Payload payload(req);
                                initiateStagedFirmwareUpdate(
                                    asyncResp, foundService, foundPath,
                                    std::move(payload));

                                fwUpdateInProgress = true;
                            },
                            objInfo[0].first, foundPath,
                            "org.freedesktop.DBus.Properties", "Set",
                            "xyz.openbmc_project.Software.UpdatePolicy",
                            "Targets",
                            dbus::utility::DbusVariantType(httpUriTargets));
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
            std::array<std::string, 1>{"xyz.openbmc_project.Software.Version"});
    }
    else
    {
        task::Payload payload(req);

        initiateStagedFirmwareUpdate(asyncResp, foundService, foundPath,
                                     std::move(payload));

        fwUpdateInProgress = true;
    }
}

/**
 * @brief Stage firmware package and fill dbus tree
 *
 * @param[in] asyncResp Pointer to the object holding the response data.
 * @param[in] req The Crow HTTP request object.
 * @param[in] forceUpdate The boolean value to set for the ForceUpdate flag.
 * @param[in] targets The parameter containing a list of target URIs for
 * the firmware update.
 *
 * @return None
 */
inline void initiateFirmwarePackage(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const crow::Request& req, const std::optional<bool>& forceUpdate,
    const std::optional<std::vector<std::string>>& targets)
{
    crow::connections::systemBus->async_method_call(
        [req, asyncResp, targets,
         forceUpdate](const boost::system::error_code ec,
                      const crow::openbmc_mapper::GetSubTreeType& subtree) {
            BMCWEB_LOG_DEBUG << "doGet callback...";
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error code = " << ec;
                BMCWEB_LOG_DEBUG << "DBUS response error msg = "
                                 << ec.message();
                messages::internalError(asyncResp->res);
                return;
            }

            bool found = false;
            std::string foundService;
            std::string foundPath;

            for (const auto& obj : subtree)
            {
                if (obj.second.size() < 1)
                {
                    break;
                }

                found = true;
                foundService = obj.second[0].first;
                foundPath = obj.first;

                break;
            }

            if (found)
            {

                fwAvailableTimer =
                    std::make_unique<boost::asio::steady_timer>(*req.ioService);

                fwAvailableTimer->expires_after(
                    std::chrono::seconds(fwObjectCreationDefaultTimeout));

                fwAvailableTimer->async_wait([asyncResp](const boost::system::
                                                             error_code& ec) {
                    cleanUp();
                    if (ec == boost::asio::error::operation_aborted)
                    {
                        // expected, we were canceled before the timer
                        // completed.
                        return;
                    }
                    BMCWEB_LOG_ERROR
                        << "Timed out waiting for firmware object being created";

                    if (ec)
                    {
                        BMCWEB_LOG_ERROR << "Async_wait failed" << ec;
                        return;
                    }
                    if (asyncResp)
                    {
                        redfish::messages::internalError(asyncResp->res);
                    }
                });

                setForceUpdate(
                    asyncResp, foundPath, forceUpdate.value_or(false),
                    [asyncResp, req, foundService, foundPath,
                     targets]() mutable {
                        setTargetsInitiateFirmwarePackage(
                            asyncResp, req, foundService, foundPath, targets);
                    });
            }
            else
            {
                BMCWEB_LOG_ERROR << "PackageInformation not found!";
                messages::resourceNotFound(asyncResp->res, "FirmwarePackages",
                                           "0");
                return;
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/software/staged", static_cast<int32_t>(0),
        std::array<const char*, 1>{
            "xyz.openbmc_project.Software.PackageInformation"});
}

/**
 * @brief POST handler for initiating firmware package
 *
 * @param app
 * @param req
 * @param asyncResp
 *
 * @return None
 */
inline void handleUpdateServiceInitiateFirmwarePackagePost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    BMCWEB_LOG_DEBUG << "doPost...";

    std::optional<std::string> firmwarePackageURI;
    std::optional<std::vector<std::string>> targets;
    std::optional<bool> forceUpdate;

    if (!json_util::readJsonPatch(
            req, asyncResp->res, "StagedFirmwarePackageURI", firmwarePackageURI,
            "Targets", targets, "ForceUpdate", forceUpdate))
    {
        BMCWEB_LOG_ERROR << "UpdateService doPatch: Invalid request body";
        return;
    }

    if (firmwarePackageURI)
    {
        if (firmwarePackageURI !=
            "/redfish/v1/UpdateService/Oem/Nvidia/PersistentStorage/FirmwarePackages/0")
        {
            BMCWEB_LOG_ERROR << "Invalid StagedFirmwarePackageURI:  "
                             << firmwarePackageURI.value();
            messages::propertyValueIncorrect(asyncResp->res,
                                             "StagedFirmwarePackageURI",
                                             firmwarePackageURI.value());
            return;
        }
    }
    else
    {
        BMCWEB_LOG_ERROR << "StagedFirmwarePackageURI is empty.";
        messages::propertyValueIncorrect(asyncResp->res, "StagedFirmwarePackageURI",
                                         "empty");
        return;
    }

    if (fwUpdateInProgress != false)
    {
        if (asyncResp)
        {
            std::string resolution = "Update is in progress. Retry"
                                     " the action once it is complete.";
            redfish::messages::updateInProgressMsg(asyncResp->res, resolution);
            BMCWEB_LOG_ERROR << "Update is in progress.";
        }
        return;
    }

    if (fwImageIsStaging == true)
    {
        if (asyncResp)
        {
            std::string resolution = "Staging is in progress. Retry"
                                     " the action once it is complete.";
            redfish::messages::updateInProgressMsg(asyncResp->res, resolution);
            BMCWEB_LOG_ERROR << "Staging is in progress.";
        }
        return;
    }

    crow::connections::systemBus->async_method_call(
        [req, asyncResp, forceUpdate, uriTargets{*targets}](
            const boost::system::error_code ec,
            const crow::openbmc_mapper::GetSubTreeType& subtree) {
            BMCWEB_LOG_DEBUG << "doGet callback...";
            if (ec)
            {
                messages::resourceNotFound(asyncResp->res, "FirmwarePackages",
                                           "0");
                return;
            }
            // Ensure we find package information, otherwise return an
            // error
            bool found = false;

            for (const auto& obj : subtree)
            {
                if (obj.second.size() < 1)
                {
                    break;
                }

                found = true;
                initiateFirmwarePackage(asyncResp, req, forceUpdate,
                                        uriTargets);
            }
            if (!found)
            {
                BMCWEB_LOG_ERROR << "PackageInformation not found!";
                messages::resourceNotFound(asyncResp->res, "FirmwarePackages",
                                           "0");
                return;
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/software/staged", static_cast<int32_t>(0),
        std::array<const char*, 1>{
            "xyz.openbmc_project.Software.PackageInformation"});
}

/**
 * @brief Update parameters for GET Method InitiateFirmwareUpdateActionInfo
 *
 * @param[in] asyncResp Shared pointer to the response message
 * @param[in] subtree  Collection of objectmappers for
 * "/xyz/openbmc_project/software"
 *
 * @return None
 */
inline void updateParametersForInitiateActionInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const crow::openbmc_mapper::GetSubTreeType& subtree)
{
    sdbusplus::asio::getProperty<std::vector<std::string>>(
        *crow::connections::systemBus, "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/software/updateable",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, subtree](const boost::system::error_code ec,
                             const std::vector<std::string>& objPaths) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << " error_code = " << ec
                                 << " error msg =  " << ec.message();
                // System can exist with no updateable firmware,
                // so don't throw error here.
                return;
            }
            asyncResp->res.jsonValue["Parameters"] = nlohmann::json::array();
            nlohmann::json& parameters = asyncResp->res.jsonValue["Parameters"];

            nlohmann::json parameterPackageURI;
            parameterPackageURI["Name"] = "StagedFirmwarePackageURI";
            parameterPackageURI["Required"] = "true";
            parameterPackageURI["DataType"] = "String";

            parameters.push_back(parameterPackageURI);

            nlohmann::json parameterTargets;
            parameterTargets["Name"] = "Targets";
            parameterTargets["Required"] = "false";
            parameterTargets["DataType"] = "StringArray";
            parameterTargets["AllowableValues"] = nlohmann::json::array();
            nlohmann::json& allowableValues =
                parameterTargets["AllowableValues"];
            std::string inventoryPath = "/xyz/openbmc_project/software/";
            for (auto& obj : subtree)
            {
                sdbusplus::message::object_path path(obj.first);
                std::string fwId = path.filename();
                std::string reqFwObjPath = inventoryPath + fwId;
                if (fwId.empty())
                {
                    messages::internalError(asyncResp->res);
                    BMCWEB_LOG_DEBUG << "Cannot parse firmware ID";
                    return;
                }
                if (std::find(objPaths.begin(), objPaths.end(), reqFwObjPath) !=
                    objPaths.end())
                {
                    BMCWEB_LOG_ERROR << "Alowable Value" << fwId;
                    allowableValues.push_back(
                        "/redfish/v1/UpdateService/FirmwareInventory/" + fwId);
                }
            }
            parameters.push_back(parameterTargets);

            nlohmann::json parameterForceUpdate;
            parameterForceUpdate["Name"] = "ForceUpdate";
            parameterForceUpdate["Required"] = "false";
            parameterForceUpdate["DataType"] = "Boolean";

            parameters.push_back(parameterForceUpdate);
        });
}

/**
 * @brief DELETE handler for deleting firmware package
 *
 * @param app
 * @param req
 * @param asyncResp
 * @param strParam
 *
 * @return None
 */
inline void handleUpdateServiceDeleteFirmwarePackage(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& strParam)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    // current implementation assume that the collection of
    // FwPackages has only one item
    if (strParam != "0")
    {
        messages::resourceNotFound(asyncResp->res, "FirmwarePackages",
                                   strParam);
        return;
    }

    if (fwUpdateInProgress != false)
    {
        if (asyncResp)
        {
            std::string resolution = "Update is in progress. Retry"
                                     " the action once it is complete.";
            redfish::messages::updateInProgressMsg(asyncResp->res, resolution);
            BMCWEB_LOG_ERROR << "Update is in progress.";
        }
        return;
    }

    if (fwImageIsStaging == true)
    {
        if (asyncResp)
        {
            std::string resolution = "Staging is in progress. Retry"
                                     " the action once it is complete.";
            redfish::messages::updateInProgressMsg(asyncResp->res, resolution);
            BMCWEB_LOG_ERROR << "Staging is in progress.";
        }
        return;
    }

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const crow::openbmc_mapper::GetSubTreeType& subtree) {
            BMCWEB_LOG_DEBUG << "doDelete callback...";
            if (ec)
            {
                messages::resourceNotFound(asyncResp->res, "FirmwarePackages",
                                           "0");
                return;
            }

            // Ensure we find package information, otherwise return
            // an error
            bool found = false;
            std::string foundService;
            std::string foundPath;

            for (const auto& obj : subtree)
            {
                if (obj.second.size() < 1)
                {
                    break;
                }

                foundService = obj.second[0].first;
                foundPath = obj.first;

                found = true;
                break;
            }

            if (found)
            {
                auto respHandler = [asyncResp](
                                       const boost::system::error_code ec) {
                    BMCWEB_LOG_DEBUG << "doDelete callback: Done";
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR << "doDelete respHandler got error "
                                         << ec.message();
                        asyncResp->res.result(
                            boost::beast::http::status::internal_server_error);
                        return;
                    }

                    asyncResp->res.result(boost::beast::http::status::ok);
                };

                crow::connections::systemBus->async_method_call(
                    respHandler, foundService, foundPath,
                    "xyz.openbmc_project.Object.Delete", "Delete");
            }
            else
            {
                BMCWEB_LOG_ERROR << "PackageInformation not found!";
                messages::resourceNotFound(asyncResp->res, "FirmwarePackages",
                                           "0");
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/software/staged", static_cast<int32_t>(0),
        std::array<const char*, 1>{
            "xyz.openbmc_project.Software.PackageInformation"});
}

/**
 * @brief Register Web Api endpoints for Split Update Firmware Package
 * functionality
 *
 * @param app
 *
 * @return None
 */
inline void requestRoutesSplitUpdateService(App& app)
{

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/UpdateService/Oem/Nvidia/PersistentStorage/stage-firmware-package/")
        .privileges(redfish::privileges::postUpdateService)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleUpdateServiceStageFirmwarePackagePost, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/UpdateService/Oem/Nvidia/PersistentStorage/FirmwarePackages/")
        .privileges(redfish::privileges::getUpdateService)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleUpdateServicePersistentStorageFwPackagesListGet,
            std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/UpdateService/Oem/Nvidia/PersistentStorage/FirmwarePackages/<str>/")
        .privileges(redfish::privileges::getUpdateService)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleUpdateServicePersistentStorageFwPackageGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/UpdateService/Oem/Nvidia/PersistentStorage/Actions/NvidiaPersistentStorage.InitiateFirmwareUpdate/")
        .privileges(redfish::privileges::postUpdateService)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleUpdateServiceInitiateFirmwarePackagePost, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/UpdateService/Oem/Nvidia/PersistentStorage/InitiateFirmwareUpdateActionInfo/")
        .privileges(redfish::privileges::getUpdateService)
        .methods(
            boost::beast::http::verb::
                get)([](const crow::Request&,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            asyncResp->res.jsonValue["@odata.type"] =
                "#ActionInfo.v1_2_0.ActionInfo";
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/UpdateService/Oem/Nvidia/PersistentStorage/InitiateFirmwareUpdateActionInfo";
            asyncResp->res.jsonValue["Name"] =
                "InitiateFirmwareUpdate Action Info";
            asyncResp->res.jsonValue["Id"] = "InitiateFirmwareUpdateActionInfo";

            crow::connections::systemBus->async_method_call(
                [asyncResp{asyncResp}](
                    const boost::system::error_code ec,
                    const std::vector<
                        std::pair<std::string,
                                  std::vector<std::pair<
                                      std::string, std::vector<std::string>>>>>&
                        subtree) {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG << "DBUS response error code = " << ec;
                        BMCWEB_LOG_DEBUG << "DBUS response error msg = "
                                         << ec.message();
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    updateParametersForInitiateActionInfo(asyncResp, subtree);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/software", static_cast<int32_t>(0),
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Software.Version"});
        });

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/UpdateService/Oem/Nvidia/PersistentStorage/FirmwarePackages/<str>/")
        .privileges(redfish::privileges::deleteUpdateService)
        .methods(boost::beast::http::verb::delete_)(std::bind_front(
            handleUpdateServiceDeleteFirmwarePackage, std::ref(app)));
}

#endif

} // namespace redfish
