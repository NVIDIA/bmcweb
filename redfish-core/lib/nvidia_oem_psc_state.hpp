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

namespace redfish
{

namespace nvidia_oem_psc_state
{

inline void afterGetPscStateProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const std::string& id,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec.value() == EBADR || ec == boost::system::errc::host_unreachable ||
        ec == boost::system::errc::io_error)
    {
        messages::resourceNotFound(asyncResp->res, "PscState", id);
        return;
    }

    if (ec)
    {
        BMCWEB_LOG_ERROR("D-Bus response error on GetPscStateProperties {}",
                         ec);
        messages::internalError(asyncResp->res);
        return;
    }

    std::string name;
    std::string pscId;
    uint64_t numOfOperationalPsus;
    bool powerBrakeAssert;
    uint64_t msSinceLastHeartbeat;
    std::string status;

    // clang-format off
    bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties, "Name", name,
        "PscId", pscId, "NumOfOperationalPsus", numOfOperationalPsus,
        "PowerBrakeAssert", powerBrakeAssert, "MillisecondsSinceLastHeartbeat",
        msSinceLastHeartbeat, "Status", status);
    // clang-format on

    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia_PowerCompliance/PowerStateGroup/PowerShelfControllers/{}",
        BMCWEB_REDFISH_MANAGER_URI_NAME, id);
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaPscState.v1_0_0.NvidiaPscState";
    asyncResp->res.jsonValue["Id"] = id;
    asyncResp->res.jsonValue["Name"] = name;
    asyncResp->res.jsonValue["PscId"] = pscId;
    asyncResp->res.jsonValue["NumOfOperationalPsus"] = numOfOperationalPsus;
    asyncResp->res.jsonValue["PowerBrakeAssert"] = powerBrakeAssert;
    asyncResp->res.jsonValue["MillisecondsSinceLastHeartbeat"] =
        msSinceLastHeartbeat;
    asyncResp->res.jsonValue["Status"] = status;
}

inline void handlePscStateGetRequest(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& id)
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

    // Dynamically construct the D-Bus paths based on the id
    sdbusplus::message::object_path dbusPath(
        "/com/nvidia/state/power_compliance/power_state_group/power_shelf_controller");
    dbusPath /= id;

    // Get all properties from D-Bus
    std::function<void(const boost::system::error_code&,
                       const dbus::utility::DBusPropertiesMap&)>
        callback = std::bind_front(afterGetPscStateProperties, asyncResp, id);

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, "com.Nvidia.RackPowerCompliance",
        dbusPath, "com.Nvidia.State.PowerCompliance.PscState",
        std::move(callback));
}

/**
 * @brief Handles GET request for PscStateCollection which reads from
 * PscStateCollection DBus objects under PowerDomains
 * @param app - crow application
 * @param req - crow request
 * @param asyncResp - response object
 * @param managerId - id of Manager
 * @return None
 */
inline void handlePscStateCollectionGetRequest(
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

    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaPscStateCollection.NvidiaPscStateCollection";
    asyncResp->res.jsonValue["Name"] = "PowerShelf Controller State Collection";

    sdbusplus::message::object_path dbusPath(
        "/com/nvidia/state/power_compliance/power_state_group/power_shelf_controller");

    nvidia_oem_power_profile::handlePowerProfileCollectionGetRequest(
        app, dbusPath, "com.Nvidia.State.PowerCompliance.PscState",
        boost::urls::format(
            "/redfish/v1/Managers/{}/Oem/Nvidia_PowerCompliance/PowerStateGroup/PowerShelfControllers",
            BMCWEB_REDFISH_MANAGER_URI_NAME),
        req, asyncResp, managerId);
}

inline void requestRoutesNvidiaPscState(App& app)
{
    /**
     * Define the GET route for PscState
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia_PowerCompliance/PowerStateGroup/PowerShelfControllers/<str>/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePscStateGetRequest, std::ref(app)));

    /**
     * Define the GET route for PscStateCollection
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia_PowerCompliance/PowerStateGroup/PowerShelfControllers/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePscStateCollectionGetRequest, std::ref(app)));
}

} // namespace nvidia_oem_psc_state

} // namespace redfish
