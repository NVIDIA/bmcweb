/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION &
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
#include "utils/sw_utils.hpp"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/connection.hpp>

#include <chrono>
#include <iostream>
#include <optional>

#define USING_COM_NVIDIA_INTERFACE false

namespace redfish
{

inline void processNSMCommandResponseData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, int fd,
    uint8_t messageType, uint8_t commandCode, uint8_t completionCode,
    uint16_t reasonCode)
{
    std::vector<uint8_t> responseData;
    std::vector<char> buffer(4096);
    ssize_t bytesRead;

    while ((bytesRead = read(fd, buffer.data(), buffer.size())) > 0)
    {
        responseData.insert(responseData.end(), buffer.begin(),
                            buffer.begin() + bytesRead);
    }

    if (bytesRead == -1)
    {
        BMCWEB_LOG_ERROR("Error reading from FD: {}", strerror(errno));
        messages::internalError(asyncResp->res);
        return;
    }

    asyncResp->res.jsonValue["MessageType"] = messageType;
    asyncResp->res.jsonValue["CommandCode"] = commandCode;
    asyncResp->res.jsonValue["CompletionCode"] = completionCode;
    asyncResp->res.jsonValue["ReasonCode"] = reasonCode;
    asyncResp->res.jsonValue["Data"] = responseData;
    asyncResp->res.result(boost::beast::http::status::ok);
}

#if USING_COM_NVIDIA_INTERFACE
inline void getMatchingFruDeviceObjectPath(
    uint8_t deviceIdentificationId, uint8_t deviceInstanceId,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::function<void(std::string)>&& callback)
{
    crow::connections::systemBus->async_method_call(
        [callback, asyncResp](const boost::system::error_code& ec,
                              const std::string& objectPath) {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "Error calling getObjectPathForNSMDevice. Error: {}",
                ec.message());
            messages::internalError(asyncResp->res);
            callback("");
            return;
        }

        callback(objectPath);
    },
        "xyz.openbmc_project.NSM", "/xyz/openbmc_project/NSM",
        "com.nvidia.NSM.NSMDevice", "getObjectPathForNSMDevice",
        deviceIdentificationId, deviceInstanceId);
}

inline void handleNSMCommandResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath, uint8_t messageType, uint8_t commandCode,
    bool isLongRunning)
{
    BMCWEB_LOG_DEBUG(
        "Handling NSM Command response. objectPath: '{}', messageType: {}, commandCode: {}, isLongRunning: {}",
        objectPath, messageType, commandCode, isLongRunning);

    crow::connections::systemBus->async_method_call(
        [asyncResp, messageType, commandCode, objectPath](
            const boost::system::error_code& ec, const uint8_t completionCode,
            const uint16_t reasonCode, sdbusplus::message::unix_fd responseFd) {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "Failed to get NSM raw command response. Error: {}",
                ec.message());
            messages::internalError(asyncResp->res);
            return;
        }

        int fd = static_cast<int>(responseFd);
        if (fd >= 0)
        {
            boost::asio::dispatch(
                crow::connections::systemBus->get_io_context(),
                [fd, asyncResp, messageType, commandCode, completionCode,
                 reasonCode]() {
                processNSMCommandResponseData(asyncResp, fd, messageType,
                                              commandCode, completionCode,
                                              reasonCode);
            });
        }
    },
        "xyz.openbmc_project.NSM", objectPath.c_str(),
        "com.nvidia.NSM.NSMRawCommand", "GetNSMCommandResponse");
}

inline void
    callSendNSMRawCommand(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          MemoryFileDescriptor& memfd,
                          const std::string& objectPath, uint8_t messageType,
                          uint8_t commandCode, bool isLongRunning)
{
    sdbusplus::message::unix_fd unixFd(memfd.fd);
    crow::connections::systemBus->async_method_call(
        [asyncResp, memfd, messageType, commandCode,
         isLongRunning](const boost::system::error_code& ec,
                        sdbusplus::message::message& reply) mutable {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to send NSM raw command. Error: {}",
                             ec.message());
            messages::internalError(asyncResp->res);
            return;
        }

        sdbusplus::message::object_path commandObjectPath;
        uint8_t completionCode;
        try
        {
            reply.read(commandObjectPath, completionCode);
        }
        catch (const std::exception& e)
        {
            BMCWEB_LOG_ERROR("Error reading NSM command response: {}",
                             e.what());
            messages::internalError(asyncResp->res);
            return;
        }

        handleNSMCommandResponse(asyncResp, std::string(commandObjectPath),
                                 messageType, commandCode, isLongRunning);
    },
        "xyz.openbmc_project.NSM", objectPath.c_str(),
        "com.nvidia.NSM.NSMRawCommand", "SendNSMRawCommand", isLongRunning,
        messageType, commandCode, unixFd);
}

#else  // USING_COM_NVIDIA_INTERFACE
inline void
    getFruDeviceProperty(const std::string& path, const std::string& property,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         std::function<void(std::optional<uint8_t>)>&& callback)
{
    crow::connections::systemBus->async_method_call(
        [callback, asyncResp](const boost::system::error_code& ec,
                              const dbus::utility::DbusVariantType& value) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to fetch FruDevice property. Error: {}",
                             ec.message());
            callback(std::nullopt);
            return;
        }
        callback(std::get<uint8_t>(value)); // Return the property value
    },
        "xyz.openbmc_project.NSM", path.c_str(),
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.FruDevice", property);
}

inline void getMatchingFruDeviceObjectPath(
    uint8_t deviceIdentificationId, uint8_t deviceInstanceId,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::function<void(std::string)>&& callback)
{
    crow::connections::systemBus->async_method_call(
        [deviceIdentificationId, deviceInstanceId, callback, asyncResp](
            const boost::system::error_code& ec,
            const std::map<std::string,
                           std::map<std::string, std::vector<std::string>>>&
                subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Error fetching FruDevice subtree. Error: {}",
                             ec.message());
            messages::internalError(asyncResp->res);
            callback("");
            return;
        }

        BMCWEB_LOG_DEBUG("Found {} FruDevice(s) to check.", subtree.size());

        for (const auto& [objectPath, interfaces] : subtree)
        {
            getFruDeviceProperty(
                objectPath, "DEVICE_TYPE", asyncResp,
                [objectPath, deviceIdentificationId, deviceInstanceId, callback,
                 asyncResp](std::optional<uint8_t> deviceType) mutable {
                if (!deviceType || deviceType.value() != deviceIdentificationId)
                {
                    return;
                }

                getFruDeviceProperty(
                    objectPath, "INSTANCE_NUMBER", asyncResp,
                    [objectPath, deviceInstanceId, callback,
                     asyncResp](std::optional<uint8_t> instanceNumber) mutable {
                    if (!instanceNumber ||
                        instanceNumber.value() != deviceInstanceId)
                    {
                        return;
                    }
                    callback(objectPath);
                    return;
                });
            });
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/FruDevice/", 1, std::vector<std::string>({}));
}

inline void
    checkCommandStatus(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& objectPath, const std::string& status,
                       uint8_t messageType, uint8_t commandCode,
                       std::shared_ptr<sdbusplus::bus::match_t>& commandMatch)
{
    if (status ==
        "xyz.openbmc_project.NSM.NSMRawCommandStatus.SetOperationStatus.CommandInProgress")
    {
        return;
    }
    else if (
        status ==
        "xyz.openbmc_project.NSM.NSMRawCommandStatus.SetOperationStatus.CommandExecutionComplete")
    {
        crow::connections::systemBus->async_method_call(
            [asyncResp, messageType, commandCode, objectPath, &commandMatch](
                const boost::system::error_code& ec,
                const uint8_t completionCode, const uint16_t reasonCode,
                sdbusplus::message::unix_fd responseFd) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Failed to execute NSM raw command. Error: {}",
                                 ec.message());
                messages::internalError(asyncResp->res);
                return;
            }

            int fd = static_cast<int>(responseFd);
            if (fd >= 0)
            {
                boost::asio::dispatch(
                    crow::connections::systemBus->get_io_context(),
                    [fd, asyncResp, messageType, commandCode, completionCode,
                     reasonCode, &commandMatch]() {
                    processNSMCommandResponseData(asyncResp, fd, messageType,
                                                  commandCode, completionCode,
                                                  reasonCode);
                    commandMatch.reset();
                });
            }
        },
            "xyz.openbmc_project.NSM", objectPath.c_str(),
            "xyz.openbmc_project.NSM.NSMRawCommand", "GetNSMCommandResponse");
    }
    else
    {
        BMCWEB_LOG_ERROR("Command failed with status: {}", status);
        messages::internalError(asyncResp->res);
        commandMatch.reset();
    }
}

inline void handleNSMCommandResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath, uint8_t messageType, uint8_t commandCode,
    bool isLongRunning)
{
    BMCWEB_LOG_DEBUG(
        "Handling NSM Command response. objectPath: '{}', messageType: {}, commandCode: {}, isLongRunning: {}",
        objectPath, messageType, commandCode, isLongRunning);

    std::shared_ptr<sdbusplus::bus::match_t> commandMatch;

    // Define the status checker by using the previously refactored method
    auto checkStatus = [asyncResp, commandCode, messageType, objectPath,
                        &commandMatch](const std::string& status) mutable {
        checkCommandStatus(asyncResp, objectPath, status, messageType,
                           commandCode, commandMatch);
    };

    // Setup DBus match for propertiesChanged
    commandMatch = std::make_shared<sdbusplus::bus::match_t>(
        *crow::connections::systemBus,
        sdbusplus::bus::match::rules::propertiesChanged(
            objectPath.c_str(), "xyz.openbmc_project.NSM.NSMRawCommandStatus"),
        [checkStatus, asyncResp](sdbusplus::message_t& msg) mutable {
        std::string iface;
        std::map<std::string, dbus::utility::DbusVariantType> properties;
        msg.read(iface, properties);

        if (iface == "xyz.openbmc_project.NSM.NSMRawCommandStatus")
        {
            auto statusIt = properties.find("Status");
            if (statusIt != properties.end())
            {
                const std::string* status =
                    std::get_if<std::string>(&statusIt->second);
                if (status)
                {
                    checkStatus(*status);
                }
            }
        }
    });

    // Initial DBus call to get the command status
    crow::connections::systemBus->async_method_call(
        [checkStatus,
         asyncResp](const boost::system::error_code& ec,
                    const std::variant<std::string>& statusVar) mutable {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to get command status. Error: {}",
                             ec.message());
            return;
        }
        std::string status = std::get<std::string>(statusVar);
        checkStatus(status);
    },
        "xyz.openbmc_project.NSM", objectPath.c_str(),
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.NSM.NSMRawCommandStatus", "Status");
}

// Call SendNSMRawCommand with DBus
inline void
    callSendNSMRawCommand(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          MemoryFileDescriptor& memfd,
                          const std::string& objectPath, uint8_t messageType,
                          uint8_t commandCode, bool isLongRunning)
{
    sdbusplus::message::unix_fd unixFd(memfd.fd);
    crow::connections::systemBus->async_method_call(
        [asyncResp, memfd, messageType, commandCode,
         isLongRunning](const boost::system::error_code& ec,
                        sdbusplus::message::message& reply) mutable {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to send NSM raw command. Error: {}",
                             ec.message());
            messages::internalError(asyncResp->res);
            return;
        }

        sdbusplus::message::object_path commandObjectPath;
        uint8_t completionCode;
        try
        {
            reply.read(commandObjectPath, completionCode);
        }
        catch (const std::exception& e)
        {
            BMCWEB_LOG_ERROR("Error reading NSM command response: {}",
                             e.what());
            messages::internalError(asyncResp->res);
            return;
        }

        handleNSMCommandResponse(asyncResp, std::string(commandObjectPath),
                                 messageType, commandCode, isLongRunning);
    },
        "xyz.openbmc_project.NSM", objectPath.c_str(),
        "xyz.openbmc_project.NSM.NSMRawCommand", "SendNSMRawCommand",
        messageType, commandCode, unixFd);
}
#endif // USING_COM_NVIDIA_INTERFACE
} // namespace redfish
