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

#include "dbus_utility.hpp"
#include "task.hpp"
#include "utils/nvidia_chassis_util.hpp"

#include <boost/system/error_code.hpp>

namespace redfish
{
namespace nvidia_system_variable_spi_erase
{

inline void
    afterSpiReadFdFound(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const boost::system::error_code& ec,
                        const sdbusplus::message::unix_fd& fd)
{
    if (ec)
    {
        if (ec == boost::system::errc::host_unreachable)
        {
            BMCWEB_LOG_DEBUG("SPI backend wasn't reachable.  Removed?");
            asyncResp->res.result(boost::beast::http::status::not_found);
            return;
        }
        messages::internalError(asyncResp->res);
        return;
    }

    // Set response headers for binary file download
    asyncResp->res.addHeader("Content-Type", "application/octet-stream");

    // Send raw binary data
    lseek(fd, 0, SEEK_SET);
    asyncResp->res.openFd(dup(fd));
}

inline void getSpiReadData(const std::string& serviceName,
                           const sdbusplus::message::object_path& path,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    BMCWEB_LOG_DEBUG("Getting SPI read data from {} for path {}", serviceName,
                     path.str);
    sdbusplus::asio::getProperty<sdbusplus::message::unix_fd>(
        *crow::connections::systemBus, serviceName, path,
        "com.nvidia.GraceSPIData", "SpiReadFd",
        [asyncResp](const boost::system::error_code& ec,
                    const sdbusplus::message::unix_fd& fd) {
        afterSpiReadFdFound(asyncResp, ec, fd);
    });
}

enum class SpiEventType
{
    SpiRead,
    SpiErase,
};

inline bool onSpiEvent(const boost::system::error_code& ec,
                       sdbusplus::message_t& msg,
                       const std::shared_ptr<task::TaskData>& taskData)
{
    if (ec)
    {
        taskData->messages.emplace_back(messages::internalError());
        return task::completed;
    }

    std::string iface;
    dbus::utility::DBusPropertiesMap propertiesChanged;
    std::vector<std::string> invalidProps;

    msg.read(iface, propertiesChanged, invalidProps);

    if (iface != "xyz.openbmc_project.Common.Progress")
    {
        return !task::completed;
    }

    // We got an update, so extend the timeout
    taskData->extendTimer(std::chrono::seconds(300));

    const std::string* status = nullptr;
    const uint8_t* progress = nullptr;
    if (!sdbusplus::unpackPropertiesNoThrow(
            redfish::dbus_utils::UnpackErrorPrinter(), propertiesChanged,
            "Status", status, "Progress", progress))
    {
        BMCWEB_LOG_ERROR("Failed to unpack properties.  Wrong type?");
        taskData->messages.emplace_back(messages::internalError());
        return !task::completed;
    }

    if (progress != nullptr)
    {
        BMCWEB_LOG_DEBUG("Progress changed to {}", *progress);
        taskData->percentComplete = *progress;
    }

    if (status != nullptr)
    {
        BMCWEB_LOG_DEBUG("Status changed to {}", *status);
        if (*status != "xyz.openbmc_project.Common.Progress.Status.InProgress")
        {
            if (*status == "xyz.openbmc_project.Common.Progress.Status.Aborted")
            {
                std::string index = std::to_string(taskData->index);
                taskData->messages.emplace_back(messages::taskAborted(index));
                taskData->state = "Aborted";
            }
            if (*status == "xyz.openbmc_project.Common.Progress.Status.Failed")
            {
                taskData->messages.emplace_back(messages::internalError());
                taskData->state = "Exception";
            }
            else
            {
                taskData->state = "Completed";
            }

            taskData->percentComplete = 100;
            return task::completed;
        }
    }

    return !task::completed;
}

inline void
    afterSpiEventStarted(SpiEventType spiEventType, task::Payload&& payload,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& serviceName,
                         const sdbusplus::message::object_path& eraseObjPath,
                         const boost::system::error_code& ec)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Failed to start erase task: {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }
    std::string match = sdbusplus::bus::match::rules::propertiesChanged(
        eraseObjPath.str, "xyz.openbmc_project.Common.Progress");

    std::shared_ptr<task::TaskData> task =
        task::TaskData::createTask(onSpiEvent, match);

    if (spiEventType == SpiEventType::SpiRead)
    {
        task::TaskResponseCallback callback =
            [eraseObjPath,
             serviceName](const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            getSpiReadData(serviceName, eraseObjPath, asyncResp);
        };
        task->taskResponse.emplace<task::TaskResponseCallback>(
            std::move(callback));
    }

    task->startTimer(std::chrono::seconds(300));
    task->populateResp(asyncResp->res);
    task->payload.emplace(std::move(payload));
}

inline void afterSpiInterfacesFound(
    SpiEventType spiEventType, task::Payload& payload,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& paths)
{
    // Host unreachable means this wasn't supported and should trigger 404.  All
    // other errors trigger internal error
    if (ec && ec != boost::system::errc::host_unreachable)
    {
        BMCWEB_LOG_ERROR("DBUS response error {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }

    if (paths.empty())
    {
        messages::resourceNotFound(asyncResp->res, "Action",
                                   "NvidiaProcessor.VariableSpiErase");
        return;
    }

    if (paths.size() != 1 || paths.front().second.size() != 1)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    const std::string& service = paths.front().second.front().first;
    const std::string& path = paths.front().first;
    BMCWEB_LOG_DEBUG("Calling spi on service {} path {}", service, path);
    std::string method;
    if (spiEventType == SpiEventType::SpiErase)
    {
        method = "EraseSpi";
    }
    else
    {
        method = "ReadSpi";
    }
    crow::connections::systemBus->async_method_call(
        [asyncResp, payload = std::move(payload), chassisId, spiEventType,
         service](const boost::system::error_code& ec,
                  const sdbusplus::message::object_path& path) mutable {
        afterSpiEventStarted(spiEventType, std::move(payload), asyncResp,
                             service, path, ec);
    },
        service, path, "com.nvidia.GraceSPI", method);
}

inline void handleSystemOemNvidiaVariableSpi(
    crow::App& app, SpiEventType spiEventType, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (!boost::starts_with(chassisId, "HGX_ProcessorModule_"))
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }
    task::Payload payload(req);

    std::array<std::string_view, 1> interfaces{"com.nvidia.GraceSPI"};
    std::string inventoryPath = "/xyz/openbmc_project/inventory/system/" + chassisId;
    dbus::utility::getSubTree(inventoryPath, 0, interfaces,
                              std::bind_front(&afterSpiInterfacesFound,
                                              spiEventType, std::move(payload),
                                              asyncResp, chassisId));
}

} // namespace nvidia_system_variable_spi_erase

/**
 * ChassisProcessorVariableSpiActions derived class for delivering Chassis
 */
inline void requestRoutesChassisOemNvidiaProcessorVariableSpiActions(App& app)
{
    using enum nvidia_system_variable_spi_erase::SpiEventType;
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Actions/Oem/NvidiaProcessor.VariableSpiErase/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            nvidia_system_variable_spi_erase::handleSystemOemNvidiaVariableSpi,
            std::ref(app), SpiErase));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Actions/Oem/NvidiaProcessor.VariableSpiRead/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            nvidia_system_variable_spi_erase::handleSystemOemNvidiaVariableSpi,
            std::ref(app), SpiRead));
}

} // namespace redfish
