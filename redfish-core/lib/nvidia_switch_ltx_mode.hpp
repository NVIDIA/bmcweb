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

// ---------------------------------------------------------------------------
// Active resource GET handlers
// ---------------------------------------------------------------------------

inline void afterSwitchLTXModeObjectGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& switchUri, const std::string& service,
    const std::string& objectPath,
    const dbus::utility::MapperGetObject& /*object*/)
{
    redfish::nvidia_fabric_utils::updateSwitchLTXModeData(
        asyncResp, service, objectPath, switchUri);
}

inline void afterSwitchLTXModeGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId,
    const std::string& switchObjectPath,
    const dbus::utility::MapperServiceMap& /*serviceMap*/)
{
    const std::string switchUri =
        "/redfish/v1/Fabrics/" + fabricId + "/Switches/" + switchId;
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaSwitchLTXMode.v1_0_0.NvidiaSwitchLTXMode";
    asyncResp->res.jsonValue["@odata.id"] = switchUri + "/Oem/Nvidia/LTXMode";
    asyncResp->res.jsonValue["Id"] = "LTXMode";
    asyncResp->res.jsonValue["Name"] = switchId + " LTX Mode";
    redfish::nvidia_fabric_utils::getSwitchLTXModeObject(
        asyncResp, switchId, switchObjectPath,
        std::bind_front(afterSwitchLTXModeObjectGet, asyncResp, switchUri));
}

inline void handleSwitchLTXModeGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    redfish::nvidia_fabric_utils::getSwitchObject(asyncResp, fabricId, switchId,
                                                  afterSwitchLTXModeGet);
}

// ---------------------------------------------------------------------------
// Settings resource GET handlers
// ---------------------------------------------------------------------------

inline void afterSwitchLTXModeSettingsObjectGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objectPath,
    const dbus::utility::MapperGetObject& /*object*/)
{
    redfish::nvidia_fabric_utils::updateSwitchLTXModeSettingsData(
        asyncResp, service, objectPath);
}

inline void afterSwitchLTXModeSettingsGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId,
    const std::string& switchObjectPath,
    const dbus::utility::MapperServiceMap& /*serviceMap*/)
{
    const std::string settingsUri =
        "/redfish/v1/Fabrics/" + fabricId + "/Switches/" + switchId +
        "/Oem/Nvidia/LTXMode/Settings";
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaSwitchLTXMode.v1_0_0.NvidiaSwitchLTXMode";
    asyncResp->res.jsonValue["@odata.id"] = settingsUri;
    asyncResp->res.jsonValue["Id"] = "Settings";
    asyncResp->res.jsonValue["Name"] = switchId + " LTX Mode Pending Settings";
    redfish::nvidia_fabric_utils::getSwitchLTXModeObject(
        asyncResp, switchId, switchObjectPath,
        std::bind_front(afterSwitchLTXModeSettingsObjectGet, asyncResp));
}

inline void handleSwitchLTXModeSettingsGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    redfish::nvidia_fabric_utils::getSwitchObject(
        asyncResp, fabricId, switchId, afterSwitchLTXModeSettingsGet);
}

// ---------------------------------------------------------------------------
// Settings resource PATCH handler
// ---------------------------------------------------------------------------

inline void afterSwitchLTXModeObjectPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& ltxMode, const std::string& /*service*/,
    const std::string& objectPath, const dbus::utility::MapperGetObject& object)
{
    redfish::nvidia_fabric_utils::patchSwitchLTXMode(asyncResp, ltxMode,
                                                     objectPath, object);
}

inline void afterSwitchLTXModePatch(
    const std::string& ltxMode,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& /*fabricId*/, const std::string& switchId,
    const std::string& switchObjectPath,
    const dbus::utility::MapperServiceMap& /*serviceMap*/)
{
    redfish::nvidia_fabric_utils::getSwitchLTXModeObject(
        asyncResp, switchId, switchObjectPath,
        std::bind_front(afterSwitchLTXModeObjectPatch, asyncResp, ltxMode));
}

inline void handleSwitchLTXModeSettingsPatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    std::optional<std::string> ltxMode;
    if (!redfish::json_util::readJsonPatch(req, asyncResp->res, "LTXMode",
                                           ltxMode))
    {
        return;
    }
    if (!ltxMode)
    {
        // LTXMode is nullable and not Redfish.Required on the Settings
        // resource — nothing to patch if the client omitted it.
        return;
    }
    // Reject Default — only Enabled and Disabled are valid PATCH values.
    if (*ltxMode != "Enabled" && *ltxMode != "Disabled")
    {
        messages::propertyValueNotInList(asyncResp->res, *ltxMode, "LTXMode");
        return;
    }
    redfish::nvidia_fabric_utils::getSwitchObject(
        asyncResp, fabricId, switchId,
        std::bind_front(afterSwitchLTXModePatch, *ltxMode));
}

// ---------------------------------------------------------------------------
// ResetToDefaults action POST handler
// ---------------------------------------------------------------------------

inline void handleSwitchLTXModeResetToDefaults(
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
    // Write PendingMode = Default to trigger the firmware default via nsmd.
    redfish::nvidia_fabric_utils::getSwitchObject(
        asyncResp, fabricId, switchId,
        std::bind_front(afterSwitchLTXModePatch, "Default"));
}

// ---------------------------------------------------------------------------
// Route registration
// ---------------------------------------------------------------------------

inline void requestRoutesSwitchLTXMode(App& app)
{
    // Active resource — GET only; PATCH returns 405 via route absence.
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/LTXMode/")
        .privileges(redfish::privileges::getSwitch)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleSwitchLTXModeGet, std::ref(app)));

    // Settings resource — GET and PATCH.
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/LTXMode/Settings/")
        .privileges(redfish::privileges::getSwitch)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleSwitchLTXModeSettingsGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/LTXMode/Settings/")
        .privileges(redfish::privileges::patchSwitch)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleSwitchLTXModeSettingsPatch, std::ref(app)));

    // ResetToDefaults action.
    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/"
                      "LTXMode/Actions/"
                      "NvidiaSwitchLTXMode.ResetToDefaults/")
        .privileges(redfish::privileges::patchSwitch)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleSwitchLTXModeResetToDefaults, std::ref(app)));
}

} // namespace nvidia

using nvidia::requestRoutesSwitchLTXMode;

} // namespace redfish
