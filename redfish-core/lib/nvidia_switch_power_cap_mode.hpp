/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION &
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

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_fabric_utils.hpp"

#include <boost/beast/http/verb.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace redfish
{
namespace nvidia
{

inline void afterSwitchPowerCappingModeObjectGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& switchUri, const std::string& service,
    const std::string& objectPath,
    const dbus::utility::MapperGetObject& /*object*/)
{
    redfish::nvidia_fabric_utils::updateSwitchPowerCappingModeData(
        asyncResp, service, objectPath, switchUri);
}

inline void afterSwitchPowerCappingModeGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId,
    const std::string& switchObjectPath,
    const dbus::utility::MapperServiceMap& /*serviceMap*/)
{
    const std::string switchUri =
        "/redfish/v1/Fabrics/" + fabricId + "/Switches/" + switchId;
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaSwitchPowerCapMode.v1_0_0.NvidiaSwitchPowerCapMode";
    asyncResp->res.jsonValue["@odata.id"] =
        switchUri + "/Oem/Nvidia/PowerCappingMode";
    asyncResp->res.jsonValue["Id"] = "PowerCappingMode";
    asyncResp->res.jsonValue["Name"] = switchId + " Power Capping Mode";
    redfish::nvidia_fabric_utils::getSwitchPowerCappingModeObject(
        asyncResp, switchId, switchObjectPath,
        std::bind_front(afterSwitchPowerCappingModeObjectGet, asyncResp,
                        switchUri));
}

inline void handleSwitchPowerCappingModeGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    redfish::nvidia_fabric_utils::getSwitchObject(
        asyncResp, fabricId, switchId, afterSwitchPowerCappingModeGet);
}

inline void afterSwitchPowerCappingModeSettingsObjectGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objectPath,
    const dbus::utility::MapperGetObject& /*object*/)
{
    redfish::nvidia_fabric_utils::updateSwitchPowerCappingModeSettingsData(
        asyncResp, service, objectPath);
}

inline void afterSwitchPowerCappingModeSettingsGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId,
    const std::string& switchObjectPath,
    const dbus::utility::MapperServiceMap& /*serviceMap*/)
{
    const std::string settingsUri =
        "/redfish/v1/Fabrics/" + fabricId + "/Switches/" + switchId +
        "/Oem/Nvidia/PowerCappingMode/Settings";
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaSwitchPowerCapMode.v1_0_0.NvidiaSwitchPowerCapMode";
    asyncResp->res.jsonValue["@odata.id"] = settingsUri;
    asyncResp->res.jsonValue["Id"] = "Settings";
    asyncResp->res.jsonValue["Name"] =
        switchId + " Power Capping Mode Pending Settings";
    redfish::nvidia_fabric_utils::getSwitchPowerCappingModeObject(
        asyncResp, switchId, switchObjectPath,
        std::bind_front(afterSwitchPowerCappingModeSettingsObjectGet,
                        asyncResp));
}

inline void handleSwitchPowerCappingModeSettingsGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    redfish::nvidia_fabric_utils::getSwitchObject(
        asyncResp, fabricId, switchId, afterSwitchPowerCappingModeSettingsGet);
}

inline void afterSwitchPowerCappingModeObjectPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& powerCapMode, const std::string& /*service*/,
    const std::string& objectPath, const dbus::utility::MapperGetObject& object)
{
    redfish::nvidia_fabric_utils::patchSwitchPowerCappingMode(
        asyncResp, powerCapMode, objectPath, object);
}

inline void afterSwitchPowerCappingModePatch(
    const std::string& powerCapMode,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& /*fabricId*/, const std::string& switchId,
    const std::string& switchObjectPath,
    const dbus::utility::MapperServiceMap& /*serviceMap*/)
{
    redfish::nvidia_fabric_utils::getSwitchPowerCappingModeObject(
        asyncResp, switchId, switchObjectPath,
        std::bind_front(afterSwitchPowerCappingModeObjectPatch, asyncResp,
                        powerCapMode));
}

inline void handleSwitchPowerCappingModeSettingsPatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    std::optional<std::string> powerCapMode;
    std::optional<nlohmann::json> settingsApplyTime;
    if (!redfish::json_util::readJsonPatch(
            req, asyncResp->res, "PowerCapMode", powerCapMode,
            "@Redfish.SettingsApplyTime", settingsApplyTime))
    {
        return;
    }
    if (settingsApplyTime)
    {
        std::optional<std::string> applyTime;
        if (!redfish::json_util::readJson(*settingsApplyTime, asyncResp->res,
                                          "ApplyTime", applyTime))
        {
            return;
        }
        if (!applyTime || *applyTime != "OnReset")
        {
            messages::propertyValueNotInList(
                asyncResp->res, applyTime.value_or(""), "ApplyTime");
            return;
        }
    }
    if (!powerCapMode)
    {
        messages::propertyMissing(asyncResp->res, "PowerCapMode");
        return;
    }
    if (*powerCapMode != "Enabled" && *powerCapMode != "Disabled")
    {
        messages::propertyValueNotInList(asyncResp->res, *powerCapMode,
                                         "PowerCapMode");
        return;
    }
    redfish::nvidia_fabric_utils::getSwitchObject(
        asyncResp, fabricId, switchId,
        std::bind_front(afterSwitchPowerCappingModePatch, *powerCapMode));
}

inline void handleSwitchPowerCappingModeResetToDefaults(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    nlohmann::json actionParams = nlohmann::json::object();
    if (!redfish::json_util::processJsonFromRequest(asyncResp->res, req,
                                                    actionParams))
    {
        return;
    }
    if (!actionParams.is_object())
    {
        messages::actionParameterValueTypeError(
            asyncResp->res, actionParams, "request body", "ResetToDefaults");
        return;
    }
    if (!actionParams.empty())
    {
        messages::actionParameterUnknown(asyncResp->res, "ResetToDefaults",
                                         actionParams.begin().key());
        return;
    }
    redfish::nvidia_fabric_utils::getSwitchObject(
        asyncResp, fabricId, switchId,
        std::bind_front(afterSwitchPowerCappingModePatch, "Default"));
}

inline void requestRoutesSwitchPowerCappingMode(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/PowerCappingMode/")
        .privileges(redfish::privileges::getSwitch)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleSwitchPowerCappingModeGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/PowerCappingMode/Settings/")
        .privileges(redfish::privileges::getSwitch)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleSwitchPowerCappingModeSettingsGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/PowerCappingMode/Settings/")
        .privileges(redfish::privileges::patchSwitch)
        .methods(boost::beast::http::verb::patch)(std::bind_front(
            handleSwitchPowerCappingModeSettingsPatch, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/"
                      "PowerCappingMode/Actions/"
                      "NvidiaSwitchPowerCapMode.ResetToDefaults/")
        .privileges(redfish::privileges::patchSwitch)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleSwitchPowerCappingModeResetToDefaults, std::ref(app)));
}

} // namespace nvidia

using nvidia::requestRoutesSwitchPowerCappingMode;

} // namespace redfish
