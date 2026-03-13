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
#include "utils/dbus_fd_download_utils.hpp"
#include "utils/nvidia_chassis_util.hpp"

#include <boost/system/error_code.hpp>

namespace redfish
{
namespace nvidia_oem_chassis_spi
{

enum class SpiEventType
{
    SpiRead,
    SpiErase,
};

inline void afterSpiEventStarted(
    SpiEventType spiEventType, task::Payload&& payload,
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
        task::TaskData::createTask(dbus_fd_utils::handleTaskMessage, match);

    if (spiEventType == SpiEventType::SpiRead)
    {
        task::TaskResponseCallback callback =
            [eraseObjPath,
             serviceName](const std::shared_ptr<bmcweb::AsyncResp>& aResp) {
                dbus_fd_utils::getDbusResultFd(serviceName, eraseObjPath,
                                               "com.nvidia.GraceSPIData",
                                               "SpiReadFd", aResp);
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
                                   "NvidiaChassis.VariableSpiErase");
        return;
    }

    std::string objectPath;
    std::string service;
    int objPathCount = 0;
    for (const auto& [path, serviceMap] : paths)
    {
        if (path.find(chassisId) != std::string::npos)
        {
            objPathCount++;
            objectPath = path;
            service = serviceMap.front().first;
        }
    }
    if (objPathCount != 1)
    {
        BMCWEB_LOG_ERROR(
            "Multiple SPI interface object paths {} found for chassisId: {}",
            objPathCount, chassisId);
        return;
    }
    if (objectPath.empty() || service.empty())
    {
        BMCWEB_LOG_ERROR(
            "SPI interface object path {} or service {} not found for chassisId: {}",
            objectPath, service, chassisId);
        return;
    }

    std::string method;
    if (spiEventType == SpiEventType::SpiErase)
    {
        method = "EraseSpi";
    }
    else
    {
        method = "ReadSpi";
    }
    sdbusplus::message::object_path path(objectPath);
    crow::connections::systemBus->async_method_call(
        [asyncResp, payload = std::move(payload), chassisId, spiEventType,
         service](const boost::system::error_code& ec2,
                  const sdbusplus::message::object_path& objPath) mutable {
            afterSpiEventStarted(spiEventType, std::move(payload), asyncResp,
                                 service, objPath, ec2);
        },
        service, path, "com.nvidia.GraceSPI", method);
}

inline void handleChassisOemNvidiaVariableSpi(
    crow::App& app, SpiEventType spiEventType, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    task::Payload payload(req);

    std::array<std::string_view, 1> interfaces{"com.nvidia.GraceSPI"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        std::bind_front(&afterSpiInterfacesFound, spiEventType,
                        std::move(payload), asyncResp, chassisId));
}

} // namespace nvidia_oem_chassis_spi

/**
 * ChassisProcessorVariableSpiActions derived class for delivering Chassis
 */
inline void requestRoutesChassisOemNvidiaProcessorVariableSpiActions(App& app)
{
    using enum nvidia_oem_chassis_spi::SpiEventType;
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Actions/Oem/NvidiaChassis.VariableSpiErase/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            nvidia_oem_chassis_spi::handleChassisOemNvidiaVariableSpi,
            std::ref(app), SpiErase));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Actions/Oem/NvidiaChassis.VariableSpiRead/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            nvidia_oem_chassis_spi::handleChassisOemNvidiaVariableSpi,
            std::ref(app), SpiRead));
}

} // namespace redfish
