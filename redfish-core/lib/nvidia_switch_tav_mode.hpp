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

inline void afterSwitchTAVModeObjectGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& switchUri, const std::string& service,
    const std::string& objectPath,
    const dbus::utility::MapperGetObject& /*object*/)
{
    redfish::nvidia_fabric_utils::updateSwitchTAVModeData(
        asyncResp, service, objectPath, switchUri);
}

inline void afterSwitchTAVModeGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId,
    const std::string& switchObjectPath,
    const dbus::utility::MapperServiceMap& /*serviceMap*/)
{
    const std::string switchUri =
        "/redfish/v1/Fabrics/" + fabricId + "/Switches/" + switchId;
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaSwitchTAVMode.v1_0_0.NvidiaSwitchTAVMode";
    asyncResp->res.jsonValue["@odata.id"] = switchUri + "/Oem/Nvidia/TAVMode";
    asyncResp->res.jsonValue["Id"] = "TAVMode";
    asyncResp->res.jsonValue["Name"] = switchId + " TAV Mode";
    redfish::nvidia_fabric_utils::getSwitchTAVModeObject(
        asyncResp, switchId, switchObjectPath,
        std::bind_front(afterSwitchTAVModeObjectGet, asyncResp, switchUri));
}

inline void handleSwitchTAVModeGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    redfish::nvidia_fabric_utils::getSwitchObject(asyncResp, fabricId, switchId,
                                                  afterSwitchTAVModeGet);
}

inline void afterSwitchTAVModeSettingsObjectGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objectPath,
    const dbus::utility::MapperGetObject& /*object*/)
{
    redfish::nvidia_fabric_utils::updateSwitchTAVModeSettingsData(
        asyncResp, service, objectPath);
}

inline void afterSwitchTAVModeSettingsGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId,
    const std::string& switchObjectPath,
    const dbus::utility::MapperServiceMap& /*serviceMap*/)
{
    const std::string settingsUri =
        "/redfish/v1/Fabrics/" + fabricId + "/Switches/" + switchId +
        "/Oem/Nvidia/TAVMode/Settings";
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaSwitchTAVMode.v1_0_0.NvidiaSwitchTAVMode";
    asyncResp->res.jsonValue["@odata.id"] = settingsUri;
    asyncResp->res.jsonValue["Id"] = "Settings";
    asyncResp->res.jsonValue["Name"] = switchId + " TAV Mode Pending Settings";
    redfish::nvidia_fabric_utils::getSwitchTAVModeObject(
        asyncResp, switchId, switchObjectPath,
        std::bind_front(afterSwitchTAVModeSettingsObjectGet, asyncResp));
}

inline void handleSwitchTAVModeSettingsGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    redfish::nvidia_fabric_utils::getSwitchObject(
        asyncResp, fabricId, switchId, afterSwitchTAVModeSettingsGet);
}

inline void afterSwitchTAVModeObjectPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& tavMode, const std::string& /*service*/,
    const std::string& objectPath, const dbus::utility::MapperGetObject& object)
{
    redfish::nvidia_fabric_utils::patchSwitchTAVMode(asyncResp, tavMode,
                                                     objectPath, object);
}

inline void afterSwitchTAVModePatch(
    const std::string& tavMode,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& /*fabricId*/, const std::string& switchId,
    const std::string& switchObjectPath,
    const dbus::utility::MapperServiceMap& /*serviceMap*/)
{
    redfish::nvidia_fabric_utils::getSwitchTAVModeObject(
        asyncResp, switchId, switchObjectPath,
        std::bind_front(afterSwitchTAVModeObjectPatch, asyncResp, tavMode));
}

inline void handleSwitchTAVModeSettingsPatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    std::optional<std::string> tavMode;
    std::optional<nlohmann::json> settingsApplyTime;
    if (!redfish::json_util::readJsonPatch(
            req, asyncResp->res, "TAVMode", tavMode,
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
    if (!tavMode)
    {
        messages::propertyMissing(asyncResp->res, "TAVMode");
        return;
    }
    if (*tavMode != "Enabled" && *tavMode != "Disabled")
    {
        messages::propertyValueNotInList(asyncResp->res, *tavMode, "TAVMode");
        return;
    }
    BMCWEB_LOG_DEBUG("TAVMode PATCH requested for {}/{}: {}", fabricId,
                     switchId, *tavMode);
    redfish::nvidia_fabric_utils::getSwitchObject(
        asyncResp, fabricId, switchId,
        std::bind_front(afterSwitchTAVModePatch, *tavMode));
}

inline void handleSwitchTAVModeResetToDefaults(
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
    BMCWEB_LOG_DEBUG("TAVMode ResetToDefaults requested for {}/{}", fabricId,
                     switchId);
    redfish::nvidia_fabric_utils::getSwitchObject(
        asyncResp, fabricId, switchId,
        std::bind_front(afterSwitchTAVModePatch, "Default"));
}

inline void requestRoutesSwitchTAVMode(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/TAVMode/")
        .privileges(redfish::privileges::getSwitch)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleSwitchTAVModeGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/TAVMode/Settings/")
        .privileges(redfish::privileges::getSwitch)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleSwitchTAVModeSettingsGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/TAVMode/Settings/")
        .privileges(redfish::privileges::patchSwitch)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleSwitchTAVModeSettingsPatch, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/"
                      "TAVMode/Actions/"
                      "NvidiaSwitchTAVMode.ResetToDefaults/")
        .privileges(redfish::privileges::patchSwitch)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleSwitchTAVModeResetToDefaults, std::ref(app)));
}

} // namespace nvidia

using nvidia::requestRoutesSwitchTAVMode;

} // namespace redfish
