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

inline bool onSpiEraseEvent(const boost::system::error_code& ec,
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
        taskData->messages.emplace_back(messages::internalError());
        return !task::completed;
    }

    if (progress != nullptr)
    {
        taskData->percentComplete = *progress;
    }

    if (status != nullptr)
    {
        BMCWEB_LOG_DEBUG("Status changed to {}", *status);
        if (*status != "xyz.openbmc_project.Common.Progress.Status.InProgress")
        {
            taskData->state = "Completed";
            return task::completed;
        }
    }

    return !task::completed;
}

inline void
    afterSpiEraseStarted(task::Payload&& payload,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const sdbusplus::message::object_path& eraseObjPath,
                         const boost::system::error_code& ec)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Fail to start erase task: {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }
    std::string match = sdbusplus::bus::match::rules::propertiesChanged(
        eraseObjPath.str, "xyz.openbmc_project.Common.Progress");

    std::shared_ptr<task::TaskData> task =
        task::TaskData::createTask(onSpiEraseEvent, match);

    task->startTimer(std::chrono::seconds(300));
    task->populateResp(asyncResp->res);
    task->payload.emplace(std::move(payload));
}

inline void afterSpiInterfacesFound(
    task::Payload& payload, const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
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

    crow::connections::systemBus->async_method_call(
        [asyncResp, payload = std::move(payload),
         chassisId](const boost::system::error_code& ec,
                    const sdbusplus::message::object_path& path) mutable {
        afterSpiEraseStarted(std::move(payload), asyncResp, path, ec);
    },
        service, path, "com.nvidia.SPI.SPI", "EraseSpi");
}

inline void handleSystemOemNvidiaVariableSpiErase(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (chassisId != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "System", chassisId);
        return;
    }
    task::Payload payload(req);

    std::array<std::string_view, 1> interfaces{"com.nvidia.SPI.SPI"};
    dbus::utility::getSubTree("/xyz/openbmc_project/inventory", 0, interfaces,
                              std::bind_front(&afterSpiInterfacesFound,
                                              std::move(payload), asyncResp,
                                              chassisId));
}

} // namespace nvidia_system_variable_spi_erase

/**
 * ChassisProcessorVariableSpiActions derived class for delivering Chassis
 */
inline void requestRoutesSystemOemNvidiaProcessorVariableSpiActions(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Actions/Oem/NvidiaProcessor.VariableSpiErase/")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(nvidia_system_variable_spi_erase::
                                handleSystemOemNvidiaVariableSpiErase,
                            std::ref(app)));
}

} // namespace redfish
