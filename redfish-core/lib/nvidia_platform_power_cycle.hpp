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

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/action_info.hpp"
#include "http_request.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_platform_power_cycle_utils.hpp"

#include <boost/beast/http/verb.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace redfish::nvidia_platform_power_cycle
{

inline bool supportsAuxPowerCycle(const Capabilities& capabilities)
{
    return supports(capabilities, auxPowerCycle) ||
           supports(capabilities, auxPowerCycleForce);
}

enum class AuxPowerCycleBackend
{
    legacy,
    platform,
    unsupported,
};

inline AuxPowerCycleBackend resolveAuxPowerCycleBackend(
    const std::optional<Capabilities>& capabilities,
    std::string_view requestedType)
{
    if (!capabilities)
    {
        return AuxPowerCycleBackend::legacy;
    }
    if (!supports(*capabilities, requestedType))
    {
        return AuxPowerCycleBackend::unsupported;
    }
    return AuxPowerCycleBackend::platform;
}

inline void populateAuxPowerResetAction(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    nlohmann::json& action =
        asyncResp->res
            .jsonValue["Actions"]["Oem"]["#NvidiaChassis.AuxPowerReset"];
    action["target"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/Actions/Oem/NvidiaChassis.AuxPowerReset",
        chassisId);
    action["@Redfish.ActionInfo"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/Oem/Nvidia/AuxPowerResetActionInfo", chassisId);
}

inline void afterGetAuxPowerCapabilitiesForChassis(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    const std::optional<Capabilities>& capabilities)
{
    if (ec)
    {
        return;
    }

    if (!capabilities)
    {
        populateAuxPowerResetAction(asyncResp, chassisId);
        return;
    }

    if (!supportsAuxPowerCycle(*capabilities))
    {
        return;
    }

    populateAuxPowerResetAction(asyncResp, chassisId);
}

inline void addAuxPowerResetAction(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (chassisId != BMCWEB_PLATFORM_CHASSIS_NAME)
    {
        return;
    }
    getSupportedPowerCycleTypes(
        0, std::bind_front(afterGetAuxPowerCapabilitiesForChassis, asyncResp,
                           chassisId));
}

inline Capabilities getLegacyAuxPowerCapabilities()
{
    return {"", {std::string(auxPowerCycle), std::string(auxPowerCycleForce)}};
}

inline void populateAuxPowerResetActionInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const Capabilities& capabilities)
{
    nlohmann::json::array_t allowableValues;
    if (supports(capabilities, auxPowerCycle))
    {
        allowableValues.emplace_back("AuxPowerCycle");
    }
    if (supports(capabilities, auxPowerCycleForce))
    {
        allowableValues.emplace_back("AuxPowerCycleForce");
    }

    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_2_0.ActionInfo";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/Oem/Nvidia/AuxPowerResetActionInfo", chassisId);
    asyncResp->res.jsonValue["Name"] = "Auxiliary Power Reset Action Info";
    asyncResp->res.jsonValue["Id"] = "AuxPowerResetActionInfo";

    nlohmann::json::object_t parameter;
    parameter["Name"] = "ResetType";
    parameter["Required"] = true;
    parameter["DataType"] = action_info::ParameterTypes::String;
    parameter["AllowableValues"] = std::move(allowableValues);
    asyncResp->res.jsonValue["Parameters"] =
        nlohmann::json::array({std::move(parameter)});
}

inline void afterGetAuxPowerResetActionInfoCapabilities(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    const std::optional<Capabilities>& capabilities)
{
    if (ec)
    {
        messages::internalError(asyncResp->res);
        return;
    }
    if (!capabilities)
    {
        populateAuxPowerResetActionInfo(asyncResp, chassisId,
                                        getLegacyAuxPowerCapabilities());
        return;
    }
    if (!supportsAuxPowerCycle(*capabilities))
    {
        messages::resourceNotFound(asyncResp->res, "ActionInfo",
                                   "AuxPowerResetActionInfo");
        return;
    }
    populateAuxPowerResetActionInfo(asyncResp, chassisId, *capabilities);
}

inline void handleAuxPowerResetActionInfoGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (chassisId != BMCWEB_PLATFORM_CHASSIS_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }
    getSupportedPowerCycleTypes(
        0, std::bind_front(afterGetAuxPowerResetActionInfoCapabilities,
                           asyncResp, chassisId));
}

inline void afterStartLegacyAuxPowerCycle(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("D-Bus response error {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }
    messages::success(asyncResp->res);
}

inline void afterGetLegacyAuxPowerHostState(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, const std::string& hostState)
{
    if (ec)
    {
        if (ec == boost::system::errc::host_unreachable)
        {
            BMCWEB_LOG_DEBUG("Service not available {}", ec);
            return;
        }
        messages::internalError(asyncResp->res);
        return;
    }
    if (hostState != "xyz.openbmc_project.State.Host.HostState.Off")
    {
        messages::chassisPowerStateOffRequired(asyncResp->res, "0");
        return;
    }
    dbus::utility::async_method_call(
        [asyncResp](const boost::system::error_code& startEc) {
            afterStartLegacyAuxPowerCycle(asyncResp, startEc);
        },
        "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", "StartUnit",
        "nvidia-aux-power.service", "replace");
}

inline void requestLegacyAuxPowerCycle(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, bool force)
{
    if (force)
    {
        dbus::utility::async_method_call(
            [asyncResp](const boost::system::error_code& startEc) {
                afterStartLegacyAuxPowerCycle(asyncResp, startEc);
            },
            "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
            "org.freedesktop.systemd1.Manager", "StartUnit",
            "nvidia-aux-power-force.service", "replace");
        return;
    }

    dbus::utility::getProperty<std::string>(
        "xyz.openbmc_project.State.Host", "/xyz/openbmc_project/state/host0",
        "xyz.openbmc_project.State.Host", "CurrentHostState",
        std::bind_front(afterGetLegacyAuxPowerHostState, asyncResp));
}

inline void afterGetAuxPowerCapabilitiesForRequest(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, bool force,
    const std::string& resetType, const boost::system::error_code& ec,
    const std::optional<Capabilities>& capabilities)
{
    if (ec)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    const std::string_view requestedType =
        force ? auxPowerCycleForce : auxPowerCycle;

    switch (resolveAuxPowerCycleBackend(capabilities, requestedType))
    {
        case AuxPowerCycleBackend::legacy:
            requestLegacyAuxPowerCycle(asyncResp, force);
            return;
        case AuxPowerCycleBackend::unsupported:
            messages::actionParameterValueNotInList(
                asyncResp->res, resetType, "ResetType",
                "NvidiaChassis.AuxPowerReset");
            return;
        case AuxPowerCycleBackend::platform:
            break;
        default:
            BMCWEB_LOG_ERROR("Unexpected AUX power-cycle backend");
            messages::internalError(asyncResp->res);
            return;
    }

    if (!capabilities.has_value())
    {
        BMCWEB_LOG_ERROR(
            "Platform AUX power-cycle backend has no capabilities");
        messages::internalError(asyncResp->res);
        return;
    }

    requestPowerCycleWithCapabilities(asyncResp, 0, capabilities.value(),
                                      requestedType, resetType,
                                      "NvidiaChassis.AuxPowerReset");
}

inline void handleAuxPowerResetActionPost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (chassisId != BMCWEB_PLATFORM_CHASSIS_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    std::string resetType;
    if (!json_util::readJsonAction(req, asyncResp->res, "ResetType", resetType))
    {
        return;
    }
    if (resetType != "AuxPowerCycle" && resetType != "AuxPowerCycleForce")
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, resetType, "ResetType",
            "NvidiaChassis.AuxPowerReset");
        return;
    }

    const bool force = resetType == "AuxPowerCycleForce";
    getSupportedPowerCycleTypes(
        0, std::bind_front(afterGetAuxPowerCapabilitiesForRequest, asyncResp,
                           force, resetType));
}

inline void requestRoutesPlatformPowerCycle(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Actions/Oem/NvidiaChassis.AuxPowerReset/")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleAuxPowerResetActionPost, std::ref(app)));

    BMCWEB_ROUTE(
        app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/AuxPowerResetActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleAuxPowerResetActionInfoGet, std::ref(app)));
}

} // namespace redfish::nvidia_platform_power_cycle
