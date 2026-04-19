/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "background_copy.hpp"
#include "commit_image.hpp"
#include "component_integrity.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "debug_token/erase_policy.hpp"
// Nvidia code starts here
#include "generated/enums/resource.hpp"
// Nvidia code ends here
#include "http_utility.hpp"
#include "multipart_parser.hpp"
#include "nvidia_messages.hpp"
#include "ossl_random.hpp"
#include "persistentstorage_util.hpp"
#include "query.hpp"
#include "redfish_aggregator.hpp"
#include "registries/privilege_registry.hpp"
#include "task.hpp"
#include "utility.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/hex_utils.hpp"
#include "utils/sw_utils.hpp"

#include <sys/mman.h>

#include <boost/container/flat_map.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <http_client.hpp>
#include <http_connection.hpp>
#include <registries/oem/nvidia_resource_event_message_registry.hpp>
#include <resource_messages.hpp>
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
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace redfish
{

/* holds compute digest operation state to allow one operation at a time */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static bool computeDigestInProgress = false;
const std::string hashComputeInterface = "com.Nvidia.ComputeHash";
constexpr auto retimerHashMaxTimeSec =
    180; // 2 mins for 2 attempts and 1 addional min as buffer
// Only allow one update at a time
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static bool fwUpdateInProgress = false;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static nlohmann::json preTaskMessages = {};

// allowed firmware image size
constexpr const size_t firmwareImageLimitBytes =
    // NOLINTNEXTLINE(bugprone-implicit-widening-of-multiplication-result)
    BMCWEB_FIRMWARE_IMAGE_LIMIT * 1024 * 1024;

/**
 * @brief A session for asynchronously writing image data to a file.
 *
 * This struct manages the asynchronous writing of image data to a specified
 * file path using Boost.Asio. It handles writing data in chunks and ensures
 * that the file is properly closed upon completion or error.
 */
struct AsyncImageWriteSession :
    public std::enable_shared_from_this<AsyncImageWriteSession>
{
    /**
     * @brief Constructs an AsyncImageWriteSession.
     *
     * @param asyncRespIn A shared pointer to the asynchronous response object.
     * @param streamIn A shared pointer to the Boost.Asio stream descriptor.
     * @param filepathIn The file path where the image data will be written.
     * @param dataRefIn A reference to the string containing the image data.
     * @param sharedReqIn An optional shared pointer to the request object.
     */
    AsyncImageWriteSession(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncRespIn,
        std::shared_ptr<boost::asio::posix::stream_descriptor> streamIn,
        const std::filesystem::path& filepathIn, const std::string& dataRefIn,
        std::shared_ptr<const crow::Request> sharedReqIn = nullptr) :
        asyncResp(asyncRespIn), stream(std::move(streamIn)),
        filepath(filepathIn), dataRef(dataRefIn),
        sharedReq(std::move(sharedReqIn))
    {}

    /**
     * @brief Starts the asynchronous write operation.
     *
     * Initiates the process of writing the image data to the file in chunks.
     */
    void start()
    {
        writeChunk(0);
    }

  private:
    /**
     * @brief Writes a chunk of data to the file.
     *
     * @param offset The current offset in the data to start writing from.
     */
    void writeChunk(std::size_t offset)
    {
        if (offset >= dataRef.size())
        {
            boost::system::error_code ec;
            stream->close(ec);
            BMCWEB_LOG_INFO("Finished writing file to {}", filepath.string());
            return;
        }

        static constexpr std::size_t chunkSize = 8192;
        const std::size_t bytesToWrite =
            std::min(chunkSize, dataRef.size() - offset);

        std::string_view dataRefView{dataRef};
        std::string_view chunk = dataRefView.substr(offset, bytesToWrite);

        auto buffer = boost::asio::buffer(chunk.data(), chunk.size());

        auto self = shared_from_this();
        boost::asio::async_write(
            *stream, buffer,
            [self, offset,
             bytesToWrite](const boost::system::error_code& ec,
                           std::size_t /*bytesTransferred*/) mutable {
                if (!ec)
                {
                    const std::size_t newOffset = offset + bytesToWrite;
                    BMCWEB_LOG_DEBUG("Wrote {} bytes [offset={}] to {}",
                                     bytesToWrite, newOffset,
                                     self->filepath.string());
                    self->writeChunk(newOffset);
                }
                else
                {
                    BMCWEB_LOG_ERROR("Write error on {}: {}",
                                     self->filepath.string(), ec.message());
                    boost::system::error_code closeEc;
                    self->stream->close(closeEc);
                    messages::internalError(self->asyncResp->res);
                }
            });
    }

    std::shared_ptr<bmcweb::AsyncResp> asyncResp;
    std::shared_ptr<boost::asio::posix::stream_descriptor> stream;
    std::filesystem::path filepath;
    const std::string& dataRef;
    std::shared_ptr<const crow::Request> sharedReq;
};

class BMCStatusAsyncResp
{
  public:
    explicit BMCStatusAsyncResp(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncRespIn) :
        asyncResp(asyncRespIn)
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
        if constexpr (!BMCWEB_DISABLE_CONDITIONS_ARRAY)
        {
            asyncResp->res.jsonValue["Status"]["Conditions"] =
                nlohmann::json::array();
        }
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

/**
 * @brief Check the initial activation state of a software update
 *
 * This function checks if a software activation has already failed before
 * the property change monitoring begins. This handles the race condition
 * where PLDM or other update services might have already marked the
 * activation as failed immediately after creating the software object.
 *
 * If the activation state is already "Failed" or "Invalid", the task is
 * immediately marked as failed with appropriate status and messages.
 *
 * @param[in] task    The task object to update if activation has failed
 * @param[in] objPath The D-Bus object path of the software activation
 */
inline void checkInitialActivationState(
    const std::shared_ptr<task::TaskData>& task,
    const sdbusplus::object_path& objPath)
{
    dbus::utility::getDbusObject(
        objPath.str,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Software.Activation"},
        [task, objPath](const boost::system::error_code& ec,
                        const dbus::utility::MapperGetObject& mapperResponse) {
            if (ec || mapperResponse.empty())
            {
                return;
            }

            dbus::utility::getProperty<std::string>(
                mapperResponse.begin()->first, objPath.str,
                "xyz.openbmc_project.Software.Activation", "Activation",
                [task](const boost::system::error_code& ec2,
                       const std::string& activation) {
                    if (!ec2 && (activation.ends_with("Invalid") ||
                                 activation.ends_with("Failed")))
                    {
                        std::string index = std::to_string(task->index);
                        task->state = "Exception";
                        task->status = "Warning";
                        task->messages.emplace_back(
                            messages::taskAborted(index));
                        task->timer.cancel();
                        task->finishTask();
                        fwUpdateInProgress = false;
                    }
                });
        });
}

inline nlohmann::json getUpdateMessage(const std::string& msgId,
                                       std::vector<std::string>& args)
{
    std::string arg1;
    std::string arg2;
    std::string arg3;
    if (!args.empty())
    {
        arg1 = args[0];
    }
    if (args.size() >= 2)
    {
        arg2 = args[1];
    }
    if (args.size() >= 3)
    {
        arg3 = args[2];
    }

    if (msgId == "Update.1.0.TargetDetermined")
    {
        return messages::targetDetermined(arg1, arg2);
    }
    if (msgId == "Update.1.0.AllTargetsDetermined")
    {
        return messages::allTargetsDetermined();
    }
    if (msgId == "Update.1.0.UpdateInProgress")
    {
        return messages::updateInProgress();
    }
    if (msgId == "Update.1.0.TransferringToComponent")
    {
        return messages::transferringToComponent(arg1, arg2);
    }
    if (msgId == "Update.1.0.VerifyingAtComponent")
    {
        return messages::verifyingAtComponent(arg1, arg2);
    }
    if (msgId == "Update.1.0.InstallingOnComponent")
    {
        return messages::installingOnComponent(arg1, arg2);
    }
    if (msgId == "Update.1.0.ApplyingOnComponent")
    {
        return messages::applyingOnComponent(arg1, arg2);
    }
    if (msgId == "Update.1.0.TransferFailed")
    {
        return messages::transferFailed(arg1, arg2);
    }
    if (msgId == "Update.1.0.VerificationFailed")
    {
        return messages::verificationFailed(arg1, arg2);
    }
    if (msgId == "Update.1.0.ApplyFailed")
    {
        return messages::applyFailed(arg1, arg2);
    }
    if (msgId == "Update.1.0.ActivateFailed")
    {
        return messages::activateFailed(arg1, arg2);
    }
    if (msgId == "Update.1.0.AwaitToUpdate")
    {
        return messages::awaitToUpdate(arg1, arg2);
    }
    if (msgId == "Update.1.0.AwaitToActivate")
    {
        return messages::awaitToActivate(arg1, arg2);
    }
    if (msgId == "Update.1.0.UpdateSuccessful")
    {
        return messages::updateSuccessful(arg1, arg2);
    }
    if (msgId == "Update.1.0.OperationTransitionedToJob")
    {
        return messages::operationTransitionedToJob(arg1);
    }
    if (msgId == "ResourceEvent.1.0.ResourceErrorsDetected")
    {
        return messages::resourceErrorsDetectedFormatError(arg1, arg2);
    }
    if (msgId == "NvidiaUpdate.1.0.ComponentUpdateSkipped")
    {
        return messages::componentUpdateSkipped(arg1, arg2);
    }
    if (msgId == "NvidiaUpdate.1.0.RecoveryStarted")
    {
        return messages::recoveryStarted(arg1);
    }
    if (msgId == "NvidiaUpdate.1.0.RecoverySuccessful")
    {
        return messages::recoverySuccessful(arg1);
    }
    if (msgId == "NvidiaUpdate.1.0.FirmwareInRecovery")
    {
        return messages::firmwareInRecovery(arg1);
    }
    if (msgId == "NvidiaUpdate.1.0.FirmwareNotInRecovery")
    {
        return messages::firmwareNotInRecovery(arg1);
    }
    if (msgId == "NvidiaUpdate.1.0.EnterDOTRecovery")
    {
        return messages::enterDOTRecovery(arg1);
    }
    if (msgId == "NvidiaUpdate.1.0.ComponentUpdateTime")
    {
        return messages::componentUpdateTime(arg1, arg2);
    }
    if (msgId == "NvidiaUpdate.1.0.DebugTokenEraseFailed")
    {
        return messages::debugTokenEraseFailed(arg1, arg2);
    }
    if (msgId == "NvidiaUpdate.1.1.DebugTokenEraseSkipped")
    {
        return messages::debugTokenEraseSkipped(arg1);
    }
    if (msgId == "NvidiaResourceEvent.1.0.DeviceDriverErrorsDetected")
    {
        return messages::deviceDriverErrorsDetected(arg1, arg2, arg3);
    }
    if (msgId == "NvidiaResourceEvent.1.0.BmcDriverErrorsDetected")
    {
        return messages::bmcDriverErrorsDetected(arg1, arg2, arg3);
    }
    if (msgId == "ResourceEvent.1.2.ResourceErrorsDetected")
    {
        return messages::resourceErrorsDetectedFormatError(arg1, arg2);
    }
    if (msgId == "NvidiaUpdate.1.0.ActivateSuccessful")
    {
        return messages::activateSuccessful(arg1, arg2);
    }
    if (msgId == "OpenBMC.0.5.ServiceRestart")
    {
        return messages::serviceRestart(arg1);
    }

    return {};
}

inline void handleLogMatchCallback(sdbusplus::message_t& m,
                                   nlohmann::json& messages)
{
    std::vector<std::pair<std::string, dbus::utility::DBusPropertiesMap>>
        interfacesProperties;
    sdbusplus::object_path objPath;
    m.read(objPath, interfacesProperties);
    const std::vector<std::pair<std::string, std::string>>* additionalData =
        nullptr;
    for (auto interface : interfacesProperties)
    {
        if (interface.first == "xyz.openbmc_project.Logging.Entry")
        {
            std::string rfMessage;
            std::string resolution;
            std::string severity;
            std::string messageNamespace;
            std::string deviceName;
            std::string errorId;
            std::vector<std::string> rfArgs;
            for (auto& propertyMap : interface.second)
            {
                if (propertyMap.first == "AdditionalData")
                {
                    additionalData = std::get_if<
                        std::vector<std::pair<std::string, std::string>>>(
                        &propertyMap.second);

                    if (additionalData != nullptr)
                    {
                        redfish::AdditionalData additional(*additionalData);

                        if (additional.contains("REDFISH_MESSAGE_ID"))
                        {
                            rfMessage = additional["REDFISH_MESSAGE_ID"];
                        }
                        if (additional.contains("REDFISH_MESSAGE_ARGS"))
                        {
                            bmcweb::split(rfArgs,
                                          additional["REDFISH_MESSAGE_ARGS"],
                                          ',');
                        }
                        if (additional.contains("namespace"))
                        {
                            messageNamespace = additional["namespace"];
                        }
                        if (additional.contains("DEVICE_NAME"))
                        {
                            deviceName = additional["DEVICE_NAME"];
                        }
                        if (additional.contains("ERROR_ID"))
                        {
                            errorId = additional["ERROR_ID"];
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
                else if (propertyMap.first == "Severity")
                {
                    const std::string* value =
                        std::get_if<std::string>(&propertyMap.second);
                    if (value != nullptr)
                    {
                        severity = translateSeverityDbusToRedfish(*value);
                    }
                }
            }
            /* we need to have found the id, data, this image needs to
               correspond to the image we are working with right now and the
               message should be update related */
            if (additionalData == nullptr || messageNamespace != "FWUpdate")
            {
                // something is invalid
                BMCWEB_LOG_DEBUG("Got invalid log message");
            }
            else
            {
                auto msgObj = getUpdateMessage(rfMessage, rfArgs);
                if (!resolution.empty())
                {
                    msgObj["Resolution"] = resolution;
                }
                if (!severity.empty())
                {
                    msgObj["MessageSeverity"] = severity;
                }
                if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                {
                    // Surface the same OEM identifiers (DEVICE_NAME, ERROR_ID)
                    // that LogServices/EventService expose, on the FW update
                    // task Message[]. Source is the AdditionalData written by
                    // dbus-sensors / phm / pldm.
                    if (!deviceName.empty() || !errorId.empty())
                    {
                        nlohmann::json::object_t nvidia;
                        nvidia["@odata.type"] =
                            "#NvidiaMessage.v1_0_0.NvidiaMessage";
                        if (!deviceName.empty())
                        {
                            nvidia["Device"] = deviceName;
                        }
                        if (!errorId.empty())
                        {
                            nvidia["ErrorId"] = errorId;
                        }
                        msgObj["Oem"]["Nvidia"] = std::move(nvidia);
                    }
                }
                messages.emplace_back(std::move(msgObj));
            }
        }
    }
}

inline void loggingMatchCallback(const std::shared_ptr<task::TaskData>& task,
                                 sdbusplus::message_t& m)
{
    if (task == nullptr)
    {
        return;
    }
    handleLogMatchCallback(m, task->messages);
}

inline void preTaskLoggingHandler(sdbusplus::message_t& m)
{
    handleLogMatchCallback(m, preTaskMessages);
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

inline static void getRelatedItemsDrive(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::object_path& objPath)
{
    // Drive is expected to be under a Chassis
    // Only add Chassis/Drives link, not Storage/Drives link
    std::string driveId = objPath.filename();
    std::string drivePath = objPath.str;

    dbus::utility::getAssociationEndPoints(
        drivePath + "/chassis",
        [asyncResp, driveId](const boost::system::error_code& ec,
                             const dbus::utility::MapperEndPoints& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Error in chassis ID association: {}", ec);
                return;
            }

            if (resp.empty())
            {
                BMCWEB_LOG_DEBUG("No chassis ID association for drive {}",
                                 driveId);
                return;
            }

            // Find the chassisId that contains this driveId
            sdbusplus::object_path chassisPath(resp[0]);
            std::string chassisId = chassisPath.filename();

            // Build Chassis drive link only (no Storage link)
            std::string driveLink = "/redfish/v1/Chassis/";
            driveLink.append(chassisId).append("/Drives/").append(driveId);

            nlohmann::json& relatedItem =
                asyncResp->res.jsonValue["RelatedItem"];
            nlohmann::json& relatedItemCount =
                asyncResp->res.jsonValue["RelatedItem@odata.count"];

            // Add chassis drive link
            if (!relatedItemAlreadyPresent(relatedItem, driveLink))
            {
                relatedItem.push_back({{"@odata.id", driveLink}});
            }

            relatedItemCount = relatedItem.size();
        });
}

inline static void getRelatedItemsStorageController(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const sdbusplus::object_path& objPath)
{
    dbus::utility::async_method_call(
        [aResp, objPath](const boost::system::error_code& ec,
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

                sdbusplus::object_path path(object);

                dbus::utility::getSubTree(
                    object, int32_t(0),
                    std::array<std::string_view, 1>{
                        "xyz.openbmc_project.Inventory."
                        "Item.StorageController"},
                    [aResp, objPath,
                     path](const boost::system::error_code& errCodeController,
                           const dbus::utility::MapperGetSubTreeResponse&
                               subtree) {
                        if (errCodeController || subtree.empty())
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
                                      std::string(
                                          BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                      "/Storage/" + path.filename() +
                                      "#/StorageControllers/" +
                                      std::to_string(i)}});
                            break;
                        }

                        relatedItemCount = relatedItem.size();
                    });
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
    const sdbusplus::object_path& objPath)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath.str + "/chassis",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, objPath](const boost::system::error_code& errorCode,
                             const std::vector<std::string>& data) {
            if (errorCode)
            {
                BMCWEB_LOG_DEBUG("error_code = {}", errorCode);
                BMCWEB_LOG_DEBUG("error msg = {}", errorCode.message());
                return;
            }
            std::string chassisName = "chassis";
            for (const std::string& path : data)
            {
                sdbusplus::object_path myLocalPath(path);
                chassisName = myLocalPath.filename();
            }
            nlohmann::json& relatedItem =
                asyncResp->res.jsonValue["RelatedItem"];
            nlohmann::json& relatedItemCount =
                asyncResp->res.jsonValue["RelatedItem@odata.count"];
            relatedItem.push_back(
                {{"@odata.id",
                  "/redfish/v1/Chassis/" + chassisName +
                      "/PowerSubsystem/PowerSupplies/" + objPath.filename()}});

            relatedItemCount = relatedItem.size();
            asyncResp->res.jsonValue["Description"] = "Power Supply image";
        });
}

inline static void getRelatedItemsPCIeDevice(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::object_path& objPath)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath.str + "/chassis",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, objPath](const boost::system::error_code& errorCode,
                             const std::vector<std::string>& data) {
            if (errorCode)
            {
                BMCWEB_LOG_DEBUG("error_code = {}", errorCode);
                BMCWEB_LOG_DEBUG("error msg = {}", errorCode.message());
                return;
            }
            std::string chassisName = "chassis";
            for (const std::string& path : data)
            {
                sdbusplus::object_path myLocalPath(path);
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
        });
}

inline static void getRelatedItemsSwitch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::object_path& objPath)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath.str + "/fabrics",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, objPath](const boost::system::error_code& errorCode,
                             const std::vector<std::string>& data) {
            if (errorCode)
            {
                BMCWEB_LOG_DEBUG("error_code = {}", errorCode);
                BMCWEB_LOG_DEBUG("error msg = {}", errorCode.message());
                return;
            }
            std::string fabricName = "fabric";
            for (const std::string& path : data)
            {
                sdbusplus::object_path myLocalPath(path);
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
        });
}

inline static void getRelatedItemsNetworkAdapter(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::object_path& objPath)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath.str + "/parent_chassis",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, objPath](const boost::system::error_code& errorCode,
                             const std::vector<std::string>& data) {
            if (errorCode)
            {
                BMCWEB_LOG_ERROR("error_code = {}", errorCode);
                BMCWEB_LOG_ERROR("error msg = {}", errorCode.message());
                return;
            }
            std::string networAdapterChassisName = "Networkadapter";
            if (!data.empty())
            {
                sdbusplus::object_path myLocalPath(data.front());
                networAdapterChassisName = myLocalPath.filename();
            }
            nlohmann::json& relatedItem =
                asyncResp->res.jsonValue["RelatedItem"];
            nlohmann::json& relatedItemCount =
                asyncResp->res.jsonValue["RelatedItem@odata.count"];
            relatedItem.push_back(
                {{"@odata.id",
                  "/redfish/v1/Chassis/" + networAdapterChassisName +
                      "/NetworkAdapters/" + objPath.filename()}});

            relatedItemCount = relatedItem.size();
        });
}

inline static void getRelatedItemsOther(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const sdbusplus::object_path& association)
{
    // Find supported device types.
    dbus::utility::async_method_call(
        [aResp, association](
            const boost::system::error_code& ec,
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
                        std::string itemPath =
                            "/redfish/v1/Chassis/" + association.filename();
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
inline static void getRelatedItemsOthers(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& swId,
    std::string inventoryPathIn = "")
{
    BMCWEB_LOG_DEBUG("getRelatedItemsOthers enter");

    if (inventoryPathIn.empty())
    {
        inventoryPathIn = "/xyz/openbmc_project/software/";
    }

    aResp->res.jsonValue["RelatedItem"] = nlohmann::json::array();
    aResp->res.jsonValue["RelatedItem@odata.count"] = 0;

    dbus::utility::getSubTree(
        inventoryPathIn, 0,
        std::array<std::string_view, 1>{"xyz.openbmc_project.Software.Version"},
        [aResp, swId](
            const boost::system::error_code& ec,
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
                sdbusplus::object_path path(obj.first);
                if (path.filename() != swId)
                {
                    continue;
                }

                if (obj.second.empty())
                {
                    continue;
                }
                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper", path.str + "/inventory",
                    "xyz.openbmc_project.Association", "endpoints",
                    [aResp](const boost::system::error_code& errCodeAssoc,
                            const std::vector<std::string>& resp) {
                        if (errCodeAssoc)
                        {
                            BMCWEB_LOG_DEBUG("error_code = {}, error msg = {}",
                                             errCodeAssoc,
                                             errCodeAssoc.message());
                            return;
                        }

                        for (const std::string& association : resp)
                        {
                            if (association.empty())
                            {
                                continue;
                            }
                            sdbusplus::object_path associationPath(association);

                            getRelatedItemsOther(aResp, associationPath);
                        }
                    });
            }
        });
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
    std::vector<sdbusplus::object_path>& validTargets)
{
    bool hasAnyInvalidOrUnupdateableTarget = false;
    for (const std::string& target : uriTargets)
    {
        std::string componentName = std::filesystem::path(target).filename();
        bool validTarget = false;
        std::string softwarePath =
            "/xyz/openbmc_project/software/" + componentName;

        if (std::ranges::any_of(swInvPaths, [&](const std::string& path) {
                return path.find(softwarePath) != std::string::npos;
            }))
        {
            validTarget = true;

            if (std::ranges::find(updateables, componentName) !=
                updateables.end())
            {
                validTargets.emplace_back(softwarePath);
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
    if (req.body().size() > (firmwareImageLimitBytes))
    {
        if (asyncResp)
        {
            BMCWEB_LOG_ERROR("Large image size: {}", req.body().size());
            messages::payloadTooLarge(asyncResp->res);
        }
        return false;
    }

    // Only allow one FW update at a time
    if (enableFWInProgCheck && fwUpdateInProgress)
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
    return true;
}

inline void extendUpdateServiceGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    asyncResp->res.jsonValue["SoftwareInventory"] = {
        {"@odata.id", "/redfish/v1/UpdateService/SoftwareInventory"}};
    asyncResp->res
        .jsonValue["Actions"]["Oem"]["#NvidiaUpdateService.CommitImage"] = {
        {"target",
         "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.CommitImage"},
        {"@Redfish.ActionInfo",
         "/redfish/v1/UpdateService/Oem/Nvidia/CommitImageActionInfo"}};
    if constexpr (BMCWEB_SCP_UPDATE)
    {
        asyncResp->res.jsonValue["Actions"]["Oem"]
                                ["#NvidiaUpdateService.PublicKeyExchange"] = {
            {"target",
             "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.PublicKeyExchange"}};
        asyncResp->res.jsonValue
            ["Actions"]["Oem"]
            ["#NvidiaUpdateService.RevokeAllRemoteServerPublicKeys"] = {
            {"target",
             "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.RevokeAllRemoteServerPublicKeys"}};
    }

    if constexpr (BMCWEB_REDFISH_POST_TO_OLD_UPDATESERVICE)
    {
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
    }
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
            "#NvidiaUpdateService.v1_4_0.NvidiaUpdateService";
        debug_token::getErasePolicy(
            [asyncResp](const std::optional<bool>& erasePolicy) {
                if (erasePolicy)
                {
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                            ["AutomaticDebugTokenErased"] =
                        *erasePolicy;
                }
            });
    }

    auto getUpdateStatus = std::make_shared<BMCStatusAsyncResp>(asyncResp);
    dbus::utility::async_method_call(
        [asyncResp, getUpdateStatus](
            const boost::system::error_code& errorCode,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) mutable {
            if (errorCode)
            {
                BMCWEB_LOG_ERROR("error_code = {}", errorCode);
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
                BMCWEB_LOG_ERROR("Invalid Object Size {}", objInfo.size());
                if (asyncResp)
                {
                    messages::internalError(asyncResp->res);
                }
                return;
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject",
        "/xyz/openbmc_project/software/pldm",
        std::array<const char*, 1>{"xyz.openbmc_project.Software.Update"});

    dbus::utility::getSubTree(
        "/au/com/codeconstruct/mctp1/networks/1/endpoints/", 0,
        std::array<std::string_view, 1>{"xyz.openbmc_project.MCTP.Endpoint"},
        [getUpdateStatus](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) mutable {
            getUpdateStatus->mctp_serviceStatus = !(ec || subtree.empty());
            return;
        });

    dbus::utility::getProperty<std::string>(
        "xyz.openbmc_project.State.BMC", "/xyz/openbmc_project/state/bmc0",
        "xyz.openbmc_project.State.BMC", "CurrentBMCState",
        [getUpdateStatus](const boost::system::error_code& ec,
                          const std::string& bmcState) mutable {
            if (ec)
            {
                return;
            }

            getUpdateStatus->bmcStateString = bmcState;
            return;
        });

    dbus::utility::getProperty<std::string>(
        "xyz.openbmc_project.State.Host", "/xyz/openbmc_project/state/host0",
        "xyz.openbmc_project.State.Host", "CurrentHostState",
        [getUpdateStatus](const boost::system::error_code& ec,
                          const std::string& hostState) mutable {
            if (ec)
            {
                return;
            }

            getUpdateStatus->hostStateString = hostState;
            return;
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
    dbus::utility::getSubTree(
        "/com/Nvidia/ComputeHash", static_cast<int32_t>(0),
        std::array<std::string_view, 1>{std::string_view(hashComputeInterface)},
        [asyncResp, swId](
            const boost::system::error_code& ec,
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
            for (const auto& obj : subtree)
            {
                sdbusplus::object_path hashPath(obj.first);
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
    dbus::utility::async_method_call(
        [asyncResp, &req, hashComputeObjPath, swId](
            const boost::system::error_code& ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Failed to GetObject for ComputeDigest: {}",
                                 ec);
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
            std::optional<uint64_t> parsedRetimerId =
                stringToUint64(swId.substr(swId.rfind('_') + 1));
            if (!parsedRetimerId)
            {
                BMCWEB_LOG_ERROR("Error while parsing retimer Id from: {}",
                                 swId);
                messages::internalError(asyncResp->res);
                return;
            }
            uint32_t retimerId = static_cast<uint32_t>(*parsedRetimerId);
            // create a task to wait for the hash digest property changed signal
            std::shared_ptr<task::TaskData> task = task::TaskData::createTask(
                [hashComputeObjPath, hashComputeService](
                    const boost::system::error_code& ec1,
                    sdbusplus::message::message& msg,
                    const std::shared_ptr<task::TaskData>& taskData) {
                    if (ec1)
                    {
                        if (ec1 != boost::asio::error::operation_aborted)
                        {
                            // Real error occurred
                            taskData->state = "Exception";
                            taskData->messages.emplace_back(
                                messages::resourceErrorsDetectedFormatError(
                                    "NvidiaSoftwareInventory.ComputeDigest",
                                    ec1.message()));
                            taskData->finishTask();
                        }
                        else
                        {
                            // Handle timeout scenario - matches original
                            // timeoutCallback behavior
                            taskData->state = "Aborted";
                            computeDigestInProgress = false;
                            taskData->messages.emplace_back(
                                messages::taskAborted(
                                    std::to_string(taskData->index)));
                            taskData->finishTask();
                        }
                        computeDigestInProgress = false;
                        return task::completed;
                    }

                    std::string interface;
                    boost::container::flat_map<std::string,
                                               dbus::utility::DbusVariantType>
                        propertiesList;

                    msg.read(interface, propertiesList);
                    if (interface == hashComputeInterface)
                    {
                        auto it = propertiesList.find("Digest");
                        if (it == propertiesList.end())
                        {
                            BMCWEB_LOG_ERROR(
                                "Signal doesn't have Digest value");
                            return !task::completed;
                        }
                        auto* value = std::get_if<std::string>(&(it->second));
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Digest value is not a string");
                            return !task::completed;
                        }

                        if (!(value->empty()))
                        {
                            std::string hashDigestValue = *value;
                            dbus::utility::getProperty<std::string>(
                                hashComputeService, hashComputeObjPath,
                                hashComputeInterface, "Algorithm",
                                [taskData, hashDigestValue](
                                    const boost::system::error_code& ec2,
                                    const std::string& hashAlgoValue) {
                                    if (ec2)
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "DBUS response error for Algorithm");
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
                                            hashAlgoValue;
                                    std::string location =
                                        "Location: /redfish/v1/TaskService/Tasks/" +
                                        std::to_string(taskData->index) +
                                        "/Monitor";
                                    taskData->payload->httpHeaders.emplace_back(
                                        std::move(location));
                                    taskData->taskResponse
                                        .emplace<nlohmann::json>(
                                            std::move(jsonResponse));
                                    taskData->state = "Completed";
                                    taskData->percentComplete = 100;
                                    taskData->messages.emplace_back(
                                        messages::taskCompletedOK(
                                            std::to_string(taskData->index)));
                                    taskData->finishTask();
                                });
                            computeDigestInProgress = false;
                            return task::completed;
                        }

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
                    return !task::completed;
                },
                "type='signal',member='PropertiesChanged',"
                "interface='org.freedesktop.DBus.Properties',"
                "path='" +
                    hashComputeObjPath + "',");
            task->startTimer(std::chrono::seconds(retimerHashMaxTimeSec));
            task->populateResp(asyncResp->res);
            task::Payload payload(req);
            task->payload.emplace(std::move(payload));
            computeDigestInProgress = true;
            dbus::utility::async_method_call(
                [task](const boost::system::error_code& ec3) {
                    if (ec3)
                    {
                        BMCWEB_LOG_ERROR("Failed to ComputeDigest: {}", ec3);
                        task->state = "Aborted";
                        task->messages.emplace_back(
                            messages::resourceErrorsDetectedFormatError(
                                "NvidiaSoftwareInventory.ComputeDigest",
                                ec3.message()));
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
inline void handlePostComputeDigest(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& swId)
{
    dbus::utility::getSubTree(
        "/com/Nvidia/ComputeHash", static_cast<int32_t>(0),
        std::array<std::string_view, 1>{std::string_view(hashComputeInterface)},
        [&req, asyncResp, swId](
            const boost::system::error_code& ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            if (ec)
            {
                messages::resourceNotFound(
                    asyncResp->res, "NvidiaSoftwareInventory.ComputeDigest",
                    swId);
                BMCWEB_LOG_ERROR("Invalid object path: {}", ec);
                return;
            }
            for (const auto& obj : subtree)
            {
                sdbusplus::object_path hashPath(obj.first);
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
        });
}

/**
 * @brief Get allowable value for particular firmware inventory
 * The function gets allowable values from config file
 * /usr/share/bmcweb/fw_mctp_mapping.json.
 * and returns the allowable value if exists in the collection
 *
 * @param[in] inventoryPathIn - firmware inventory path.
 * @returns Pair of boolean value if the allowable value exists
 * and the object of AllowableValue who contains inventory path
 * and assigned to its MCTP EID.
 */
inline std::pair<bool, CommitImageValueEntry> getAllowableValue(
    const std::string_view inventoryPathIn)
{
    std::pair<bool, CommitImageValueEntry> result;

    std::vector<CommitImageValueEntry> allowableValues = getAllowableValues();
    auto it = std::ranges::find(allowableValues, inventoryPathIn,
                                &CommitImageValueEntry::inventoryUri);

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
 * @brief Check whether firmware inventory is allowable
 * The function gets allowable values from config file
 * /usr/share/bmcweb/fw_mctp_mapping.json.
 * and check if the firmware inventory is in this collection
 *
 * @param[in] inventoryPathIn - firmware inventory path.
 * @returns boolean value indicates whether firmware inventory
 * is allowable.
 */
inline bool isInventoryAllowableValue(const std::string_view inventoryPathIn)
{
    bool isAllowable = false;

    std::vector<CommitImageValueEntry> allowableValues = getAllowableValues();
    auto it = std::ranges::find(allowableValues, inventoryPathIn,
                                &CommitImageValueEntry::inventoryUri);

    isAllowable = it != allowableValues.end();

    return isAllowable;
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
    parameterTargets["Required"] = false;
    parameterTargets["DataType"] = "StringArray";
    parameterTargets["AllowableValues"] = nlohmann::json::array();

    nlohmann::json& allowableValues = parameterTargets["AllowableValues"];

    for (const auto& obj : subtree)
    {
        sdbusplus::object_path path(obj.first);
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
}

/**
 * @brief Handles request POST
 * The function triggers Commit Image action
 * for the list of delivered in the body of request
 * firmware inventories
 *
 * @param req Async HTTP request.
 * @param asyncResp Pointer to object holding response data
 * @param[in] subtree  Collection of objectmappers for
 * "/xyz/openbmc_project/software"
 *
 * @return None
 */
inline void handleCommitImagePost(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::optional<std::vector<std::string>> targets;

    if (!json_util::readJsonAction(req, asyncResp->res, "Targets", targets))
    {
        return;
    }

    bool hasTargets = false;

    if (targets && !targets.value().empty())
    {
        hasTargets = true;
    }

    // Pair: first = dbus software object path, second = redfish inventory path
    std::vector<std::pair<std::string, std::string>> softwareObjectPaths = {};
    bool hasInvalidTargets = false;

    if (hasTargets)
    {
        std::vector<std::string> targetsCollection = targets.value();

        for (auto& target : targetsCollection)
        {
            // Validate that the target is a proper Redfish FirmwareInventory
            // path (same style as processUrl() in update_service.hpp)
            boost::system::result<boost::urls::url_view> url =
                boost::urls::parse_origin_form(target);
            if (!url)
            {
                BMCWEB_LOG_ERROR("Invalid target path: '{}'", target);
                boost::urls::url_view targetURL(target);
                messages::resourceMissingAtURI(asyncResp->res, targetURL);
                hasInvalidTargets = true;
                continue;
            }
            std::string firmwareId;
            if (!crow::utility::readUrlSegments(
                    *url, "redfish", "v1", "UpdateService", "FirmwareInventory",
                    std::ref(firmwareId)))
            {
                BMCWEB_LOG_ERROR("Invalid target path: '{}'", target);
                boost::urls::url_view targetURL(target);
                messages::resourceMissingAtURI(asyncResp->res, targetURL);
                hasInvalidTargets = true;
                continue;
            }

            std::string inventoryPathIn =
                "/xyz/openbmc_project/software/" + firmwareId;
            std::pair<bool, CommitImageValueEntry> result =
                getAllowableValue(inventoryPathIn);

            if (result.first)
            {
                softwareObjectPaths.emplace_back(result.second.inventoryUri,
                                                 target);
            }
            else
            {
                BMCWEB_LOG_DEBUG(
                    "Cannot find firmware inventory in allowable values");
                boost::urls::url_view targetURL(target);
                messages::resourceMissingAtURI(asyncResp->res, targetURL);
                hasInvalidTargets = true;
            }
        }
    }

    collectImageCopySoftwarePaths(
        asyncResp,
        [asyncResp, hasTargets, softwareObjectPaths, hasInvalidTargets](
            const std::map<std::string, ChassisInfo>& chassisMap) mutable {
            if (hasTargets)
            {
                for (const auto& [dbusPath, redfishPath] : softwareObjectPaths)
                {
                    bool foundInChassis = false;
                    for (const auto& [chassisName, chassisInfo] : chassisMap)
                    {
                        if (std::ranges::find(chassisInfo.softwarePaths,
                                              dbusPath) !=
                            chassisInfo.softwarePaths.end())
                        {
                            foundInChassis = true;
                            break;
                        }
                    }

                    if (!foundInChassis)
                    {
                        BMCWEB_LOG_ERROR(
                            "Target path {} not found in any chassis",
                            redfishPath);
                        boost::urls::url_view targetURL(redfishPath);
                        messages::resourceMissingAtURI(asyncResp->res,
                                                       targetURL);
                        hasInvalidTargets = true;
                    }
                }
            }

            std::vector<ChassisObjectSoftwarePath> chassisCollection;
            for (const auto& [chassisName, chassisInfo] : chassisMap)
            {
                std::vector<std::string> matchingPaths;
                for (const auto& path : chassisInfo.softwarePaths)
                {
                    if (hasTargets)
                    {
                        auto it = std::ranges::find_if(
                            softwareObjectPaths,
                            [&path](const std::pair<std::string, std::string>&
                                        swPathPair) {
                                return swPathPair.first == path;
                            });
                        if (it != softwareObjectPaths.end())
                        {
                            matchingPaths.push_back(path);
                        }
                    }
                    else
                    {
                        // When no targets specified, only include paths that
                        // are in the allowable values list
                        if (isInventoryAllowableValue(path))
                        {
                            matchingPaths.push_back(path);
                        }
                        else
                        {
                            BMCWEB_LOG_DEBUG(
                                "Skipping path {} - Cannot find "
                                "firmware inventory in allowable values",
                                path);
                        }
                    }
                }

                if (!matchingPaths.empty())
                {
                    ChassisObjectSoftwarePath entry;
                    entry.chassisName = chassisName;
                    entry.chassisDbusPath = chassisInfo.dbusPath;
                    entry.objectPaths = matchingPaths;
                    chassisCollection.push_back(std::move(entry));
                }
            }

            if (chassisCollection.empty())
            {
                if (!hasInvalidTargets)
                {
                    BMCWEB_LOG_ERROR(
                        "No chassis found for commit image operation");
                    messages::internalError(asyncResp->res);
                }
                return;
            }

            // Create aggregation context to track all operations
            auto aggregationCtx =
                std::make_shared<CommitImageAggregationContext>(
                    asyncResp, chassisCollection.size());

            // Initiate commit image operation for each chassis
            for (const auto& chassis : chassisCollection)
            {
                initiateImageCopy(aggregationCtx, chassis);
            }
        });
}

inline void extendSoftwareInventoryGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& objectPath,
    const std::shared_ptr<std::string>& swId)
{
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        fw_util::getFWSlotInformation(asyncResp, objectPath);
        updateOemActionComputeDigest(asyncResp, *swId);
    }
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

inline void handlePublicKeyExchangePost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
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
        messages::createFailedMissingReqProperties(asyncResp->res, emptyprops);
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
    dbus::utility::async_method_call(
        [asyncResp](const boost::system::error_code& ec) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                BMCWEB_LOG_ERROR("error_code = {} error msg = {}", ec,
                                 ec.message());
                return;
            }

            dbus::utility::async_method_call(
                [asyncResp](const boost::system::error_code& ec2,
                            const std::string& selfPublicKeyStr) {
                    if (ec2 || selfPublicKeyStr.empty())
                    {
                        messages::internalError(asyncResp->res);
                        BMCWEB_LOG_ERROR("error_code = {} error msg = {}", ec2,
                                         ec2.message());
                        return;
                    }

                    // Create a JSON object with the additional
                    // information
                    std::string keyMsg =
                        "Please add the following public key info to "
                        "~/.ssh/authorized_keys on the remote server";
                    std::string keyInfo = selfPublicKeyStr + " root@dpu-bmc";

                    asyncResp->res.jsonValue[messages::messageAnnotation] =
                        nlohmann::json::array();
                    asyncResp->res.jsonValue[messages::messageAnnotation]
                        .push_back(extendedInfoSuccessMsg(keyMsg, keyInfo));
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
}

/**
 * @brief POST handler for adding remote server SSH public key
 *
 * @param app
 *
 * @return None
 */
inline void handleRevokeAllRemoteServerPublicKeysPost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
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
    dbus::utility::async_method_call(
        [asyncResp](const boost::system::error_code& ec) {
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
}

/**
 * @brief process the response from satellite BMC.
 *
 * @param[in] prefix the prefix of the url
 * @param[in] asyncResp Pointer to object holding response data
 * @param[in] resp Pointer to object holding response data from satellite
 * BMC
 *
 * @return None
 */
inline void handleSatBMCResponse(
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
        asyncResp->res.copyBody(resp);
        return;
    }

    // The resp will not have a json component
    // We need to create a json from resp's stringResponse
    std::string_view contentType = resp.getHeaderValue("Content-Type");
    if (bmcweb::asciiIEquals(contentType, "application/json") ||
        bmcweb::asciiIEquals(contentType, "application/json; charset=utf-8"))
    {
        nlohmann::json jsonVal =
            nlohmann::json::parse(*resp.body(), nullptr, false);
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

        std::string rfaPrefix = std::string(BMCWEB_REDFISH_AGGREGATION_PREFIX);
        // NOLINTNEXTLINE(modernize-loop-convert) - modifying while iterating
        for (auto it = object->begin(); it != object->end(); ++it)
        {
            // only prefix fix-up on Task response.
            std::string* strValue = it->second.get_ptr<std::string*>();
            if (strValue == nullptr)
            {
                BMCWEB_LOG_CRITICAL("Item is not a string");
                continue;
            }

            if (it->first == "@odata.id")
            {
                std::string file = std::filesystem::path(*strValue).filename();
                std::string path =
                    std::filesystem::path(*strValue).parent_path();
                std::string temp = file;

                file = rfaPrefix;
                file += "_";
                file += temp;
                path += "/";
                // add prefix on odata.id property.
                it->second = path + file;
            }
            else if (it->first == "Id")
            {
                std::string file = std::filesystem::path(*strValue).filename();
                // add prefix on Id property.
                std::string prefixed = rfaPrefix;
                prefixed += "_";
                prefixed += file;
                it->second = prefixed;
            }
        }
        asyncResp->res.result(resp.result());
        asyncResp->res.jsonValue = std::move(jsonVal);
    }
}

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
        BMCWEB_LOG_ERROR("Failed to get satellite configs: {}", ec.message());
        return;
    }
    const auto& sat =
        satelliteInfo.find(std::string(BMCWEB_REDFISH_AGGREGATION_PREFIX));
    if (sat == satelliteInfo.end())
    {
        BMCWEB_LOG_ERROR("satBMC is not found");
        return;
    }

    crow::HttpClient& client = RedfishAggregator::getInstance().getClient();

    std::function<void(crow::Response&)> cb =
        std::bind_front(handleSatBMCResponse, asyncResp);

    std::string data = req.body();
    boost::urls::url url(sat->second);
    url.set_path(req.url().path());
    client.sendDataWithCallback(
        std::move(data), url, ensuressl::VerifyCertificate::Verify,
        req.fields(), boost::beast::http::verb::post, cb);
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

    if (targets && !targets.value().empty())
    {
        hasTargets = true;
    }

    if (hasTargets)
    {
        std::vector<std::string> targetsCollection = targets.value();

        std::string rfaPrefix(BMCWEB_REDFISH_AGGREGATION_PREFIX);
        rfaPrefix += "_";

        bool prefix = false;
        bool noPrefix = false;
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
            RedfishAggregator::getInstance().getSatelliteConfigs(
                std::bind_front(forwardCommitImagePost, std::ref(req),
                                asyncResp));

            // don't pass the request to the local
            return false;
        }
        if (prefix && noPrefix)
        {
            // drop the request with mixed targets.
            boost::urls::url_view targetURL("Target");
            messages::invalidObject(asyncResp->res, targetURL);
            return false;
        }
    }
    else
    {
        RedfishAggregator::getInstance().getSatelliteConfigs(
            std::bind_front(forwardCommitImagePost, std::ref(req), asyncResp));
        // forward the request with empty target.
    }
    return true;
}

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
        nlohmann::json jsonVal =
            nlohmann::json::parse(*resp.body(), nullptr, false);
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
            auto allowValueCb = [asyncResp](nlohmann::json& itemInCb) mutable {
                auto* str = itemInCb.get_ptr<std::string*>();
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
    if (ec)
    {
        BMCWEB_LOG_ERROR("Failed to get satellite configs: {}", ec.message());
        return;
    }
    const auto& sat =
        satelliteInfo.find(std::string(BMCWEB_REDFISH_AGGREGATION_PREFIX));
    if (sat == satelliteInfo.end())
    {
        BMCWEB_LOG_ERROR("satellite BMC is not there.");
        return;
    }

    crow::HttpClient& client = RedfishAggregator::getInstance().getClient();

    std::function<void(crow::Response&)> cb =
        std::bind_front(commitImageActionInfoResp, asyncResp);

    std::string data;
    boost::urls::url url(sat->second);
    url.set_path(req.url().path());

    boost::beast::http::fields headers = req.fields();
    headers.set(boost::beast::http::field::accept, "application/json");

    client.sendDataWithCallback(std::move(data), url,
                                ensuressl::VerifyCertificate::Verify, headers,
                                boost::beast::http::verb::get, cb);
}

inline void handleCommitImageActionInfoGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_2_0.ActionInfo";
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/UpdateService/Oem/Nvidia/CommitImageActionInfo";
    asyncResp->res.jsonValue["Name"] = "CommitImage Action Info";
    asyncResp->res.jsonValue["Id"] = "CommitImageActionInfo";

    // Note that only firmware levels associated with a device
    // are stored under /xyz/openbmc_project/software therefore
    // to ensure only real FirmwareInventory items are returned,
    // this full object path must be used here as input to
    // mapper
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/software", static_cast<int32_t>(0),
        std::array<std::string_view, 1>{"xyz.openbmc_project.Software.Version"},
        [asyncResp{asyncResp}, &req](
            const boost::system::error_code& ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            BMCWEB_LOG_DEBUG("Request URL: {}", req.url());
            updateParametersForCommitImageInfo(asyncResp, subtree);
            if constexpr (BMCWEB_REDFISH_AGGREGATION)
            {
                RedfishAggregator::getInstance().getSatelliteConfigs(
                    std::bind_front(forwardCommitImageActionInfo, std::ref(req),
                                    asyncResp));
            }
        });
}

inline void handleCommitImageActionInfoPost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    BMCWEB_LOG_DEBUG("doPost...");

    if constexpr (BMCWEB_REDFISH_AGGREGATION)
    {
        if (!handleSatBMCCommitImagePost(req, asyncResp))
        {
            return;
        }
    }

    if (fwUpdateInProgress)
    {
        redfish::messages::updateInProgressMsg(
            asyncResp->res,
            "Retry the operation once firmware update operation is complete.");

        // don't copy the image, update already in progress.
        BMCWEB_LOG_ERROR(
            "Cannot execute commit image. Update firmware is in progress.");

        return;
    }
    handleCommitImagePost(req, asyncResp);
}

/**
 * @brief app handler for ComputeDigest action
 *
 * @param[in] app
 */
inline void handleComputeDigestPost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& param)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    BMCWEB_LOG_DEBUG("Enter NvidiaSoftwareInventory.ComputeDigest doPost");
    std::shared_ptr<std::string> swId = std::make_shared<std::string>(param);
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
}

inline void handleUpdateServiceSoftwareInventoryGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& param)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    std::string searchPath = "/xyz/openbmc_project/inventory_software/";
    std::shared_ptr<std::string> swId = std::make_shared<std::string>(param);

    dbus::utility::getSubTree(
        searchPath, static_cast<int32_t>(0),
        std::array<std::string_view, 1>{"xyz.openbmc_project.Software.Version"},
        [asyncResp, swId, searchPath](
            const boost::system::error_code& ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
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
                sdbusplus::object_path objPath(path);
                if (objPath.filename() != *swId)
                {
                    continue;
                }

                if (obj.second.empty())
                {
                    continue;
                }

                asyncResp->res.jsonValue["Id"] = *swId;
                // Nvidia code starts here
                asyncResp->res.jsonValue["Status"]["Health"] =
                    resource::Health::OK;
                // Nvidia code ends here
                if constexpr (!BMCWEB_DISABLE_CONDITIONS_ARRAY)
                {
                    asyncResp->res.jsonValue["Status"]["Conditions"] =
                        nlohmann::json::array();
                }
                dbus::utility::async_method_call(
                    [asyncResp, swId, path, searchPath](
                        const boost::system::error_code& errorCode,
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
                                    asyncResp->res.jsonValue["Version"] =
                                        *version;
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
                        const std::string& mutablePath = searchPath;
                        fw_util::getFwUpdateableStatus(asyncResp, swId,
                                                       mutablePath);
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
        });
}

inline void tryInventoryPatchAfterGetSubTree(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<std::string>& swId, bool writeProtected,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        messages::internalError(asyncResp->res);
        return;
    }
    for (const auto& obj : subtree)
    {
        const std::string& path = obj.first;
        sdbusplus::object_path objPath(path);
        if (objPath.filename() != *swId)
        {
            continue;
        }

        if (obj.second.empty())
        {
            continue;
        }
        fw_util::patchFwWriteProtectedStatus(
            asyncResp, swId, obj.second[0].first, writeProtected);

        return;
    }
    // Couldn't find an object with that name.  return
    // an error
    BMCWEB_LOG_DEBUG("Input swID {} not found!", *swId);
    messages::resourceNotFound(
        asyncResp->res, "SoftwareInventory.v1_4_0.SoftwareInventory", *swId);
}

inline void handleUpdateServiceFirmwareInventoryPatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& param)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    BMCWEB_LOG_DEBUG("doPatch...");
    std::shared_ptr<std::string> swId = std::make_shared<std::string>(param);

    std::optional<bool> writeProtected;
    if (!json_util::readJsonPatch(req, asyncResp->res, "WriteProtected",
                                  writeProtected))
    {
        return;
    }

    if (!writeProtected)
    {
        return;
    }

    static constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Software.Settings"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/software/", 0, interfaces,
        std::bind_front(tryInventoryPatchAfterGetSubTree, asyncResp, swId,
                        *writeProtected));
}

inline void handleUpdateServiceSoftwareInventoryCollectionGet(
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
        "/redfish/v1/UpdateService/SoftwareInventory";
    asyncResp->res.jsonValue["Name"] = "Software Inventory Collection";

    // Note that only firmware levels associated with a device
    // are stored under /xyz/openbmc_project/inventory_software
    // therefore to ensure only real SoftwareInventory items are
    // returned, this full object path must be used here as input to
    // mapper
    const std::array<const std::string_view, 1> iface = {
        "xyz.openbmc_project.Software.Version"};

    redfish::collection_util::getCollectionMembers(
        asyncResp,
        boost::urls::url("/redfish/v1/UpdateService/SoftwareInventory"), iface,
        "/xyz/openbmc_project/inventory_software");
}

inline void handleUpdateServicePatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    BMCWEB_LOG_DEBUG("doPatch...");

    std::optional<bool> erasePolicy;
    if (!json_util::readJsonPatch(req, asyncResp->res,
                                  "Oem/Nvidia/AutomaticDebugTokenErased",
                                  erasePolicy))
    {
        BMCWEB_LOG_ERROR("UpdateService doPatch: Invalid request body");
        return;
    }

    if (erasePolicy)
    {
        debug_token::setErasePolicy(asyncResp, *erasePolicy);
    }
}

inline void addUnsupportedActionParametersMessages(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const nlohmann::json::object_t& actionParametersObject)
{
    asyncResp->res.jsonValue.clear();
    asyncResp->res.result(boost::beast::http::status::bad_request);
    nlohmann::json& extendedInfo =
        asyncResp->res.jsonValue["error"][messages::messageAnnotation];
    for (const auto& [key, _] : actionParametersObject)
    {
        if (key == "Targets" || key == "ForceUpdate" ||
            key == "@Redfish.OperationApplyTime")
        {
            continue;
        }
        if (!extendedInfo.is_array())
        {
            extendedInfo = nlohmann::json::array();
        }
        nlohmann::json message =
            messages::actionParameterNotSupported(key, "UpdateParameters");
        message["Resolution"] =
            "Refer to DMTF Redfish Specification for valid UpdateParameters. "
            "Currently supported  UpdateParameters are Targets, ForceUpdate and "
            "@Redfish.OperationApplyTime.";
        extendedInfo.push_back(std::move(message));
    }
}

inline void requestRoutesNvidiaUpdateService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/SoftwareInventory/<str>/")
        .privileges(redfish::privileges::getSoftwareInventory)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleUpdateServiceSoftwareInventoryGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/SoftwareInventory/")
        .privileges(redfish::privileges::getSoftwareInventory)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleUpdateServiceSoftwareInventoryCollectionGet, std::ref(app)));

    if constexpr (BMCWEB_REDFISH_ALLOW_FW_INVENTORY_PATCH)
    {
        BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/FirmwareInventory/<str>/")
            .privileges(redfish::privileges::patchUpdateService)
            .methods(boost::beast::http::verb::patch)(std::bind_front(
                handleUpdateServiceFirmwareInventoryPatch, std::ref(app)));
    }

    BMCWEB_ROUTE(app, "/redfish/v1/UpdateService/")
        .privileges(redfish::privileges::patchUpdateService)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleUpdateServicePatch, std::ref(app)));

    BMCWEB_ROUTE(app,
                 "/redfish/v1/UpdateService/Oem/Nvidia/CommitImageActionInfo/")
        .privileges(redfish::privileges::getSoftwareInventoryCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleCommitImageActionInfoGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.CommitImage/")
        .privileges(redfish::privileges::postUpdateService)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleCommitImageActionInfoPost, std::ref(app)));

    BMCWEB_ROUTE(
        app, "/redfish/v1/UpdateService/FirmwareInventory/<str>/Actions/Oem/"
             "NvidiaSoftwareInventory.ComputeDigest")
        .privileges(redfish::privileges::postUpdateService)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleComputeDigestPost, std::ref(app)));
    if constexpr (BMCWEB_SCP_UPDATE)
    {
        BMCWEB_ROUTE(
            app,
            "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.RevokeAllRemoteServerPublicKeys/")
            .privileges(redfish::privileges::postUpdateService)
            .methods(boost::beast::http::verb::post)(std::bind_front(
                handleRevokeAllRemoteServerPublicKeysPost, std::ref(app)));

        BMCWEB_ROUTE(
            app,
            "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.PublicKeyExchange/")
            .privileges(redfish::privileges::postUpdateService)
            .methods(boost::beast::http::verb::post)(
                std::bind_front(handlePublicKeyExchangePost, std::ref(app)));
    }
}
} // namespace redfish
