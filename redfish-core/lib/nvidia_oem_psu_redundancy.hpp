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
#include "dbus_singleton.hpp"
#include "error_messages.hpp"
#include "http/utility.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utility.hpp"
#include "utils/json_utils.hpp"

namespace redfish
{

namespace nvidia_oem_psu_redundancy
{

inline void afterGetPsuRedundancyProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::urls::url& redfishUri,
    const sdbusplus::message::object_path& dbusPath,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec.value() == EBADR || ec == boost::system::errc::host_unreachable)
    {
        messages::resourceNotFound(asyncResp->res, "PSURedundancy",
                                   dbusPath.filename());
        return;
    }

    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "D-Bus response error on GetPsuRedundancyProperties {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }
    std::string name;
    std::string description;
    std::string redundancySetting;
    std::string id;
    uint64_t maxNumSupported = 0;
    uint64_t minNumNeeded = 0;

    // clang-format off
    bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties,
        "Name", name, "Description", description,
        "RedundancySetting", redundancySetting,
        "MaxNumSupported", maxNumSupported,
        "MinNumNeeded", minNumNeeded,
        "Id", id);
    // clang-format on

    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    asyncResp->res.jsonValue["@odata.id"] = redfishUri;
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaPSURedundancy.v1_0_0.NvidiaPSURedundancy";
    asyncResp->res.jsonValue["Id"] = id;
    asyncResp->res.jsonValue["Name"] = name;
    asyncResp->res.jsonValue["Description"] = description;
    asyncResp->res.jsonValue["RedundancySetting"] = redundancySetting;
    asyncResp->res.jsonValue["MaxNumSupported"] =
        std::to_string(maxNumSupported);
    asyncResp->res.jsonValue["MinNumNeeded"] = std::to_string(minNumNeeded);
}

inline void handlePSURedundancyGetRequest(
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
        "/com/nvidia/state/power_compliance/psu_redundancy");

    boost::urls::url redfishUri = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/PSURedundancy",
        managerId);

    // Get all properties from D-Bus
    std::function<void(const boost::system::error_code&,
                       const dbus::utility::DBusPropertiesMap&)>
        callback = std::bind_front(afterGetPsuRedundancyProperties, asyncResp,
                                   redfishUri, dbusPath);

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, "com.Nvidia.RackPowerCompliance",
        dbusPath, "com.Nvidia.State.PowerCompliance.PSURedundancy",
        std::move(callback));
}

inline void handlePSURedundancyPatchRequest(
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

    std::optional<std::string> redundancySettingStr;

    if (!json_util::readJsonPatch(req, asyncResp->res, "RedundancySetting",
                                  redundancySettingStr))
    {
        return;
    }

    if (redundancySettingStr)
    {
        // Validate the value
        if (*redundancySettingStr != "NoRedundancy" &&
            *redundancySettingStr != "NPlusOne" &&
            *redundancySettingStr != "NPlusN")
        {
            messages::propertyValueNotInList(
                asyncResp->res, *redundancySettingStr, "RedundancySetting");
            return;
        }

        // Set the value via D-Bus
        sdbusplus::message::object_path dbusPath(
            "/com/nvidia/state/power_compliance/psu_redundancy");

        setDbusProperty(asyncResp, "RedundancySetting",
                        "com.Nvidia.RackPowerCompliance", dbusPath,
                        "com.Nvidia.State.PowerCompliance.PSURedundancy",
                        "RedundancySetting", *redundancySettingStr);
    }
}

inline void requestRoutesNvidiaPsuRedundancy(App& app)
{
    /**
     * Define the GET route for PSURedundancy
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/PSURedundancy/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePSURedundancyGetRequest, std::ref(app)));

    /**
     * Define the PATCH route for PSURedundancy
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/PSURedundancy/")
        .privileges(redfish::privileges::patchManager)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handlePSURedundancyPatchRequest, std::ref(app)));
}

} // namespace nvidia_oem_psu_redundancy

} // namespace redfish
