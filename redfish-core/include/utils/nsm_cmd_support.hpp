/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved. SPDX-License-Identifier: Apache-2.0
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

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/connection.hpp>

#include <chrono>

namespace redfish
{
static std::shared_ptr<boost::asio::steady_timer> rawCmdTimer;
static std::shared_ptr<sdbusplus::bus::match_t> rawCommandStatusMatch;

static inline void clearTimerAndMatch()
{
    rawCommandStatusMatch = nullptr;
    rawCmdTimer.reset();
    rawCmdTimer = nullptr;
}

static inline void
    handleCommandError(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& message)
{
    BMCWEB_LOG_ERROR("{}", message);
    clearTimerAndMatch();
    messages::internalError(asyncResp->res);
}

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
        handleCommandError(asyncResp, "Error reading from FD.");
        return;
    }

    asyncResp->res.jsonValue["MessageType"] = messageType;
    asyncResp->res.jsonValue["CommandCode"] = commandCode;
    asyncResp->res.jsonValue["CompletionCode"] = completionCode;
    asyncResp->res.jsonValue["ReasonCode"] = reasonCode;
    asyncResp->res.jsonValue["Data"] = responseData;
    asyncResp->res.result(boost::beast::http::status::ok);
}

inline void
    fetchCommandResponse(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& objectPath, uint8_t messageType,
                         uint8_t commandCode)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, messageType, commandCode](
            const boost::system::error_code& ec, const uint8_t completionCode,
            const uint16_t reasonCode, sdbusplus::message::unix_fd responseFd) {
        if (ec)
        {
            handleCommandError(asyncResp,
                               "Failed to get NSM command response.");
            return;
        }

        int fd = static_cast<int>(responseFd);
        if (fd >= 0)
        {
            processNSMCommandResponseData(asyncResp, fd, messageType,
                                          commandCode, completionCode,
                                          reasonCode);
        }
        clearTimerAndMatch();
    },
        "xyz.openbmc_project.NSM", objectPath.c_str(),
        "xyz.openbmc_project.NSM.NSMRawCommand", "GetNSMCommandResponse");
}

static inline void
    handleStatusChange(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& objectPath, const std::string& status,
                       uint8_t messageType, uint8_t commandCode)
{
    if (status ==
        "xyz.openbmc_project.NSM.NSMRawCommandStatus.SetOperationStatus.CommandInProgress")
    {
        return;
    }

    if (status ==
        "xyz.openbmc_project.NSM.NSMRawCommandStatus.SetOperationStatus.CommandExecutionComplete")
    {
        fetchCommandResponse(asyncResp, objectPath, messageType, commandCode);
    }
    else
    {
        handleCommandError(asyncResp, "Command failed with status: " + status);
    }
}

inline void handleNSMCommandResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath, uint8_t messageType, uint8_t commandCode)
{
    rawCmdTimer = std::make_shared<boost::asio::steady_timer>(
        crow::connections::systemBus->get_io_context());

    auto checkStatus = [asyncResp, messageType, commandCode,
                        objectPath](const std::string& status) {
        handleStatusChange(asyncResp, objectPath, status, messageType,
                           commandCode);
    };

    rawCommandStatusMatch = std::make_shared<sdbusplus::bus::match_t>(
        *crow::connections::systemBus,
        sdbusplus::bus::match::rules::propertiesChanged(
            objectPath.c_str(), "xyz.openbmc_project.NSM.NSMRawCommandStatus"),
        [checkStatus](sdbusplus::message_t& msg) {
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

    rawCmdTimer->expires_after(std::chrono::seconds(10));
    rawCmdTimer->async_wait([asyncResp, objectPath, messageType, commandCode](
                                const boost::system::error_code& ec) mutable {
        if (ec == boost::asio::error::operation_aborted)
        {
            return;
        }

        if (ec)
        {
            handleCommandError(asyncResp, "Timer error: " + ec.message());
            return;
        }

        crow::connections::systemBus->async_method_call(
            [asyncResp, objectPath, messageType,
             commandCode](const boost::system::error_code& ec,
                          const std::variant<std::string>& statusVar) mutable {
            if (ec)
            {
                handleCommandError(asyncResp, "Final status check failed.");
                return;
            }

            std::string status = std::get<std::string>(statusVar);
            handleStatusChange(asyncResp, objectPath, status, messageType,
                               commandCode);
        },
            "xyz.openbmc_project.NSM", objectPath.c_str(),
            "org.freedesktop.DBus.Properties", "Get",
            "xyz.openbmc_project.NSM.NSMRawCommandStatus", "Status");
    });

    crow::connections::systemBus->async_method_call(
        [checkStatus](const boost::system::error_code& ec,
                      const std::variant<std::string>& statusVar) mutable {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Initial status check failed: {}", ec.message());
            return;
        }

        std::string status = std::get<std::string>(statusVar);
        checkStatus(status);
    },
        "xyz.openbmc_project.NSM", objectPath.c_str(),
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.NSM.NSMRawCommandStatus", "Status");
}

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
    callSendNSMRawCommand(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          MemoryFileDescriptor& memfd,
                          const std::string& objectPath, uint8_t messageType,
                          uint8_t commandCode,
                          [[maybe_unused]] bool isLongRunning)
{
    sdbusplus::message::unix_fd unixFd(memfd.fd);

    crow::connections::systemBus->async_method_call(
        [asyncResp, messageType,
         commandCode](const boost::system::error_code& ec,
                      sdbusplus::message::message& reply) mutable {
        if (ec)
        {
            handleCommandError(asyncResp, "Failed to send NSM raw command.");
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
            handleCommandError(asyncResp,
                       "Error reading NSM command response: " + std::string(e.what()));
            return;
        }

        handleNSMCommandResponse(asyncResp, std::string(commandObjectPath),
                                 messageType, commandCode);
    },
        "xyz.openbmc_project.NSM", objectPath.c_str(),
        "xyz.openbmc_project.NSM.NSMRawCommand", "SendNSMRawCommand",
        messageType, commandCode, unixFd);
}

} // namespace redfish
