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

inline void afterSwitchUPhyRecoveryModeObjectGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& switchUri, const std::string& service,
    const std::string& objectPath,
    const dbus::utility::MapperGetObject& /*object*/)
{
    redfish::nvidia_fabric_utils::updateSwitchUPhyRecoveryModeData(
        asyncResp, service, objectPath, switchUri);
}

inline void afterSwitchUPhyRecoveryModeGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId,
    const std::string& switchObjectPath,
    const dbus::utility::MapperServiceMap& /*serviceMap*/)
{
    const std::string switchUri =
        "/redfish/v1/Fabrics/" + fabricId + "/Switches/" + switchId;
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaSwitchUPhyRecoveryMode.v1_0_0.NvidiaSwitchUPhyRecoveryMode";
    asyncResp->res.jsonValue["@odata.id"] =
        switchUri + "/Oem/Nvidia/UPhyRecoveryMode";
    asyncResp->res.jsonValue["Id"] = "UPhyRecoveryMode";
    asyncResp->res.jsonValue["Name"] = switchId + " UPhy Recovery Mode";
    redfish::nvidia_fabric_utils::getSwitchUPhyRecoveryModeObject(
        asyncResp, switchId, switchObjectPath,
        std::bind_front(afterSwitchUPhyRecoveryModeObjectGet, asyncResp,
                        switchUri));
}

inline void handleSwitchUPhyRecoveryModeGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    redfish::nvidia_fabric_utils::getSwitchObject(
        asyncResp, fabricId, switchId, afterSwitchUPhyRecoveryModeGet);
}

// ---------------------------------------------------------------------------
// Settings resource GET handlers
// ---------------------------------------------------------------------------

inline void afterSwitchUPhyRecoveryModeSettingsObjectGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objectPath,
    const dbus::utility::MapperGetObject& /*object*/)
{
    redfish::nvidia_fabric_utils::updateSwitchUPhyRecoveryModeSettingsData(
        asyncResp, service, objectPath);
}

inline void afterSwitchUPhyRecoveryModeSettingsGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId,
    const std::string& switchObjectPath,
    const dbus::utility::MapperServiceMap& /*serviceMap*/)
{
    const std::string settingsUri =
        "/redfish/v1/Fabrics/" + fabricId + "/Switches/" + switchId +
        "/Oem/Nvidia/UPhyRecoveryMode/Settings";
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaSwitchUPhyRecoveryMode.v1_0_0.NvidiaSwitchUPhyRecoveryMode";
    asyncResp->res.jsonValue["@odata.id"] = settingsUri;
    asyncResp->res.jsonValue["Id"] = "Settings";
    asyncResp->res.jsonValue["Name"] =
        switchId + " UPhy Recovery Mode Pending Settings";
    redfish::nvidia_fabric_utils::getSwitchUPhyRecoveryModeObject(
        asyncResp, switchId, switchObjectPath,
        std::bind_front(afterSwitchUPhyRecoveryModeSettingsObjectGet,
                        asyncResp));
}

inline void handleSwitchUPhyRecoveryModeSettingsGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    redfish::nvidia_fabric_utils::getSwitchObject(
        asyncResp, fabricId, switchId, afterSwitchUPhyRecoveryModeSettingsGet);
}

// ---------------------------------------------------------------------------
// Settings resource PATCH handler
// ---------------------------------------------------------------------------

inline void afterSwitchUPhyRecoveryModeObjectPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& uphyRecoveryMode, const std::string& /*service*/,
    const std::string& objectPath, const dbus::utility::MapperGetObject& object)
{
    redfish::nvidia_fabric_utils::patchSwitchUPhyRecoveryMode(
        asyncResp, uphyRecoveryMode, objectPath, object);
}

inline void afterSwitchUPhyRecoveryModePatch(
    const std::string& uphyRecoveryMode,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& /*fabricId*/, const std::string& switchId,
    const std::string& switchObjectPath,
    const dbus::utility::MapperServiceMap& /*serviceMap*/)
{
    redfish::nvidia_fabric_utils::getSwitchUPhyRecoveryModeObject(
        asyncResp, switchId, switchObjectPath,
        std::bind_front(afterSwitchUPhyRecoveryModeObjectPatch, asyncResp,
                        uphyRecoveryMode));
}

inline void handleSwitchUPhyRecoveryModeSettingsPatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    std::optional<std::string> uphyRecoveryMode;
    if (!redfish::json_util::readJsonPatch(
            req, asyncResp->res, "UPhyRecoveryMode", uphyRecoveryMode))
    {
        return;
    }
    if (!uphyRecoveryMode)
    {
        // UPhyRecoveryMode is nullable and not Redfish.Required on the
        // Settings resource — nothing to patch if the client omitted it.
        return;
    }
    // Reject Default — only Enabled and Disabled are valid PATCH values.
    if (*uphyRecoveryMode != "Enabled" && *uphyRecoveryMode != "Disabled")
    {
        messages::propertyValueNotInList(asyncResp->res, *uphyRecoveryMode,
                                         "UPhyRecoveryMode");
        return;
    }
    redfish::nvidia_fabric_utils::getSwitchObject(
        asyncResp, fabricId, switchId,
        std::bind_front(afterSwitchUPhyRecoveryModePatch, *uphyRecoveryMode));
}

// ---------------------------------------------------------------------------
// ResetToDefaults action POST handler
// ---------------------------------------------------------------------------

inline void handleSwitchUPhyRecoveryModeResetToDefaults(
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
        std::bind_front(afterSwitchUPhyRecoveryModePatch, "Default"));
}

// ---------------------------------------------------------------------------
// Route registration
// ---------------------------------------------------------------------------

inline void requestRoutesSwitchUPhyRecoveryMode(App& app)
{
    // Active resource — GET only; PATCH returns 405 via route absence.
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/UPhyRecoveryMode/")
        .privileges(redfish::privileges::getSwitch)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleSwitchUPhyRecoveryModeGet, std::ref(app)));

    // Settings resource — GET and PATCH.
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/UPhyRecoveryMode/Settings/")
        .privileges(redfish::privileges::getSwitch)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleSwitchUPhyRecoveryModeSettingsGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/UPhyRecoveryMode/Settings/")
        .privileges(redfish::privileges::patchSwitch)
        .methods(boost::beast::http::verb::patch)(std::bind_front(
            handleSwitchUPhyRecoveryModeSettingsPatch, std::ref(app)));

    // ResetToDefaults action.
    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/"
                      "UPhyRecoveryMode/Actions/"
                      "NvidiaSwitchUPhyRecoveryMode.ResetToDefaults/")
        .privileges(redfish::privileges::patchSwitch)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleSwitchUPhyRecoveryModeResetToDefaults, std::ref(app)));
}

} // namespace nvidia

using nvidia::requestRoutesSwitchUPhyRecoveryMode;

} // namespace redfish
