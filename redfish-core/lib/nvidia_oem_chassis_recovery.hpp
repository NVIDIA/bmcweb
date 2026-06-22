/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
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
#include "utils/json_utils.hpp"

#include <boost/system/error_code.hpp>

namespace redfish
{
namespace nvidia_oem_chassis_recovery
{

inline void afterSetRecoveryModeInterfacesFound(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& paths)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("getSubTree failed for chassisId {}: {}", chassisId,
                         ec.message());
        messages::internalError(asyncResp->res);
        return;
    }

    if (paths.empty())
    {
        messages::resourceNotFound(asyncResp->res, "Action",
                                   "NvidiaChassis.SetRecoveryMode");
        return;
    }

    int objPathCount = 0;
    std::string objectPath;
    std::string service;
    for (const auto& [path, serviceMap] : paths)
    {
        if (std::filesystem::path(path).filename() == chassisId)
        {
            objPathCount++;
            objectPath = path;
            service = serviceMap.front().first;
        }
    }

    if (objectPath.empty())
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    if (objPathCount > 1)
    {
        BMCWEB_LOG_ERROR(
            "Multiple SetRecoveryMode interface object paths ({}) found for chassisId: {}",
            objPathCount, chassisId);
        messages::internalError(asyncResp->res);
        return;
    }

    sdbusplus::object_path path(objectPath);
    dbus::utility::async_method_call(
        [asyncResp](const boost::system::error_code& ec2) {
            if (ec2)
            {
                BMCWEB_LOG_ERROR(
                    "Failed to call SetRecoveryMode D-Bus method: {}",
                    ec2.message());
                messages::internalError(asyncResp->res);
                return;
            }
            messages::success(asyncResp->res);
        },
        service, path, "com.nvidia.SetRecoveryMode", "SetRecoveryMode");
}

inline void handleChassisOemNvidiaSetRecoveryMode(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    // SetRecoveryMode accepts only empty body or empty JSON object {}; fail if
    // any parameters or other JSON is given.
    if (!json_util::requireEmptyOrEmptyJsonObject(
            asyncResp->res, req.body(), "NvidiaChassis.SetRecoveryMode"))
    {
        return;
    }

    std::array<std::string_view, 1> interfaces{"com.nvidia.SetRecoveryMode"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        std::bind_front(afterSetRecoveryModeInterfacesFound, asyncResp,
                        chassisId));
}

} // namespace nvidia_oem_chassis_recovery

/**
 * ChassisRecoveryActions derived class for delivering Chassis
 */
inline void requestRoutesChassisOemNvidiaRecoveryActions(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Actions/Oem/NvidiaChassis.SetRecoveryMode/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            nvidia_oem_chassis_recovery::handleChassisOemNvidiaSetRecoveryMode,
            std::ref(app)));
}

} // namespace redfish
