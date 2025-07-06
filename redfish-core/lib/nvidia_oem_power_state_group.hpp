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

#include "nvidia_oem_power_profile.hpp"
#include "nvidia_oem_psc_state.hpp"
#include "nvidia_oem_psu_state.hpp"

namespace redfish
{

namespace nvidia_oem_power_state_group
{

inline void afterGetPowerStateGroupProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec.value() == EBADR || ec == boost::system::errc::host_unreachable ||
        ec == boost::system::errc::io_error)
    {
        messages::resourceNotFound(asyncResp->res, "PowerStateGroup",
                                   BMCWEB_REDFISH_MANAGER_URI_NAME);
        return;
    }

    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "D-Bus response error on GetPowerStateGroupProperties {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    std::string name;
    std::string pscId;
    uint64_t generatedWatts;
    uint64_t numOfPscs;
    uint64_t numOfPsus;
    sdbusplus::message::object_path pscsDbusPath;
    sdbusplus::message::object_path psusDbusPath;

    // clang-format off
    bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties, "Name", name,
        "PscId", pscId, "GeneratedWatts", generatedWatts, "NumberOfPscs",
        numOfPscs, "NumberOfLocalPsus", numOfPsus, "PowerShelfControllers",
        pscsDbusPath, "PowerSupplies", psusDbusPath);
    // clang-format on

    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/PowerStateGroup",
        BMCWEB_REDFISH_MANAGER_URI_NAME);
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaPowerStateGroup.v1_0_0.NvidiaPowerStateGroup";
    asyncResp->res.jsonValue["Id"] = "PowerStateGroup";
    asyncResp->res.jsonValue["Name"] = name;
    asyncResp->res.jsonValue["PscId"] = pscId;
    asyncResp->res.jsonValue["GeneratedWatts"] = generatedWatts;
    asyncResp->res.jsonValue["NumberOfPscs"] = numOfPscs;
    asyncResp->res.jsonValue["NumberOfLocalPsus"] = numOfPsus;

    // Create the "PowerShelfControllers" array
    if (!pscsDbusPath.str.empty())
    {
        asyncResp->res.jsonValue["PowerShelfControllers"]
                                ["@odata.id"] = boost::urls::format(
            "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/PowerStateGroup/PowerShelfControllers",
            BMCWEB_REDFISH_MANAGER_URI_NAME);
    }

    // Create the "PowerSupplies" array
    if (!psusDbusPath.str.empty())
    {
        asyncResp->res
            .jsonValue["PowerSupplies"]["@odata.id"] = boost::urls::format(
            "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/PowerStateGroup/PowerSupplies",
            BMCWEB_REDFISH_MANAGER_URI_NAME);
    }
}

inline void handlePowerStateGroupGetRequest(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "#Manager",
                                   BMCWEB_REDFISH_MANAGER_URI_NAME);
        return;
    }

    sdbusplus::message::object_path dbusPath(
        "/com/nvidia/state/power_compliance/power_state_group");

    // Get all properties from D-Bus
    std::function<void(const boost::system::error_code&,
                       const dbus::utility::DBusPropertiesMap&)>
        callback =
            std::bind_front(afterGetPowerStateGroupProperties, asyncResp);

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, "com.Nvidia.RackPowerCompliance",
        dbusPath, "com.Nvidia.State.PowerCompliance.PowerStateGroup",
        std::move(callback));
}

inline void requestRoutesNvidiaPowerStateGroup(App& app)
{
    /**
     * Define the GET route for PowerStateGroup
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/PowerStateGroup/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePowerStateGroupGetRequest, std::ref(app)));
}

} // namespace nvidia_oem_power_state_group

} // namespace redfish
