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

namespace nvidia_oem_psu_state
{

inline void afterGetPsuStateProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const std::string& id,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec.value() == EBADR || ec == boost::system::errc::host_unreachable ||
        ec == boost::system::errc::io_error)
    {
        messages::resourceNotFound(asyncResp->res, "PsuState", id);
        return;
    }

    if (ec)
    {
        BMCWEB_LOG_ERROR("D-Bus response error on GetPsuStateProperties {}",
                         ec);
        messages::internalError(asyncResp->res);
        return;
    }

    std::string name;
    std::string psuId;
    bool presence;
    bool input1Active;
    bool input2Active;

    // clang-format off
    bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties, "Name", name,
        "PsuId", psuId, "Presence", presence, "Input1Active", input1Active,
        "Input2Active", input2Active);
    // clang-format on

    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia_PowerCompliance/PowerStateGroup/PowerSupplies/{}",
        BMCWEB_REDFISH_MANAGER_URI_NAME, id);
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaPsuState.v1_0_0.NvidiaPsuState";
    asyncResp->res.jsonValue["Id"] = id;
    asyncResp->res.jsonValue["Name"] = name;
    asyncResp->res.jsonValue["PsuId"] = psuId;
    asyncResp->res.jsonValue["Presence"] = presence;
    asyncResp->res.jsonValue["Input1Active"] = input1Active;
    asyncResp->res.jsonValue["Input2Active"] = input2Active;
}

inline void handlePsuStateGetRequest(
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
        "/com/nvidia/state/power_compliance/power_state_group/power_supply");
    dbusPath /= id;

    // Get all properties from D-Bus
    std::function<void(const boost::system::error_code&,
                       const dbus::utility::DBusPropertiesMap&)>
        callback = std::bind_front(afterGetPsuStateProperties, asyncResp, id);

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, "com.Nvidia.RackPowerCompliance",
        dbusPath, "com.Nvidia.State.PowerCompliance.PsuState",
        std::move(callback));
}

/**
 * @brief Handles GET request for PsuStateCollection which reads from
 * PsuStateCollection DBus objects under PowerDomains
 * @param app - crow application
 * @param req - crow request
 * @param asyncResp - response object
 * @param managerId - id of Manager
 * @return None
 */
inline void handlePsuStateCollectionGetRequest(
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
        "#NvidiaPsuStateCollection.NvidiaPsuStateCollection";
    asyncResp->res.jsonValue["Name"] = "Power Supply State Collection";

    sdbusplus::message::object_path dbusPath(
        "/com/nvidia/state/power_compliance/power_state_group/power_supply");

    nvidia_oem_power_profile::handlePowerProfileCollectionGetRequest(
        app, dbusPath, "com.Nvidia.State.PowerCompliance.PsuState",
        boost::urls::format(
            "/redfish/v1/Managers/{}/Oem/Nvidia_PowerCompliance/PowerStateGroup/PowerSupplies",
            BMCWEB_REDFISH_MANAGER_URI_NAME),
        req, asyncResp, managerId);
}

inline void requestRoutesNvidiaPsuState(App& app)
{
    /**
     * Define the GET route for PsuState
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia_PowerCompliance/PowerStateGroup/PowerSupplies/<str>/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePsuStateGetRequest, std::ref(app)));

    /**
     * Define the GET route for PsuStateCollection
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia_PowerCompliance/PowerStateGroup/PowerSupplies/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePsuStateCollectionGetRequest, std::ref(app)));
}

} // namespace nvidia_oem_psu_state

} // namespace redfish
