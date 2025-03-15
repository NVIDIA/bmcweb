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
#include "dbus_utility.hpp"
#include "generated/enums/nvidia_power_compliance_manager.hpp"
#include "generated/enums/nvidia_power_policy.hpp"
#include "nvidia_oem_power_policy.hpp"
#include "redfish_util.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/collection.hpp"
#include "utils/time_utils.hpp"

#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <format>
#include <functional>
#include <string_view>
#include <unordered_map>

namespace redfish
{

namespace nvidia_oem_managers_pmc
{
inline void afterInvokePowerComplianceManagerAction(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec)
{
    if (ec.value() == EBADR || ec == boost::system::errc::host_unreachable ||
        ec == boost::system::errc::io_error)
    {
        messages::resourceNotFound(asyncResp->res, "PowerComplianceManager",
                                   BMCWEB_REDFISH_MANAGER_URI_NAME);
        return;
    }

    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "Error occurred in Action for NvidiaPowerComplianceManager {}: {}",
            BMCWEB_REDFISH_MANAGER_URI_NAME, ec.message());
        messages::internalError(asyncResp->res);
        return;
    }
    messages::success(asyncResp->res);
}

inline void afterGetPowerComplianceManagerProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    [[maybe_unused]] const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec.value() == EBADR || ec == boost::system::errc::host_unreachable)
    {
        messages::resourceNotFound(asyncResp->res, "PowerComplianceManager",
                                   BMCWEB_REDFISH_MANAGER_URI_NAME);
        return;
    }

    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "Power interface was not found. This is not an error {}", ec);
        return;
    }

    // Add the @odata.id property to the PowerCompliance object in the Manager
    // response
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["PowerCompliance"]["@odata.id"] =
        boost::urls::format(
            "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance",
            BMCWEB_REDFISH_MANAGER_URI_NAME);
}

inline void processPowerComplianceManagerGetRequest(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::function<void(const boost::system::error_code&,
                       const dbus::utility::DBusPropertiesMap&)>
        callback = std::bind_front(afterGetPowerComplianceManagerProperties,
                                   asyncResp);
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, "com.Nvidia.RackPowerCompliance",
        "/com/nvidia/state/power_compliance",
        "com.Nvidia.State.PowerCompliance", std::move(callback));
}

inline void
    deassertPowerBrake(App& app, const crow::Request& req,
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

    std::function<void(const boost::system::error_code&)> callback =
        std::bind_front(&afterInvokePowerComplianceManagerAction, asyncResp);

    crow::connections::systemBus->async_method_call(
        callback, "com.Nvidia.RackPowerCompliance",
        "/com/nvidia/state/power_compliance",
        "com.Nvidia.State.PowerCompliance", "DeassertPowerBrake");
}

inline void
    assertPowerBrake(App& app, const crow::Request& req,
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

    std::function<void(const boost::system::error_code&)> callback =
        std::bind_front(&afterInvokePowerComplianceManagerAction, asyncResp);

    crow::connections::systemBus->async_method_call(
        callback, "com.Nvidia.RackPowerCompliance",
        "/com/nvidia/state/power_compliance",
        "com.Nvidia.State.PowerCompliance", "AssertPowerBrake");
}

inline void afterGetPowerComplianceProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "PowerComplianceManager",
                                   BMCWEB_REDFISH_MANAGER_URI_NAME);
        return;
    }

    if (ec.value() == EBADR || ec == boost::system::errc::host_unreachable)
    {
        messages::resourceNotFound(asyncResp->res, "PowerComplianceManager",
                                   BMCWEB_REDFISH_MANAGER_URI_NAME);
        return;
    }

    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "Power interface was not found. This is not an error {}", ec);
        return;
    }

    sdbusplus::message::object_path managedEntityGroupsDbusPath;
    sdbusplus::message::object_path powerDomainsDbusPath;
    sdbusplus::message::object_path powerPoliciesDbusPath;
    sdbusplus::message::object_path powerStateGroupDbusPath;

    // clang-format off
        bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), properties,
            "ManagedEntityGroups", managedEntityGroupsDbusPath,
            "PowerDomains", powerDomainsDbusPath,
            "PowerPolicies", powerPoliciesDbusPath,
            "PowerStateGroup", powerStateGroupDbusPath);
    // clang-format on

    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    nlohmann::json& jOut = asyncResp->res.jsonValue;

    // Set the resource properties in the exact order specified
    jOut["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance",
        BMCWEB_REDFISH_MANAGER_URI_NAME);
    jOut["@odata.type"] =
        "#NvidiaPowerComplianceManager.v1_1_0.NvidiaPowerComplianceManager";
    jOut["Id"] = "PowerCompliance";
    jOut["Name"] = "Rack Power Compliance";

    if (!managedEntityGroupsDbusPath.str.empty())
    {
        jOut["ManagedEntityGroups"]["@odata.id"] = boost::urls::format(
            "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/ManagedEntityGroups",
            BMCWEB_REDFISH_MANAGER_URI_NAME);
    }
    // Add PowerDomains
    if (!powerDomainsDbusPath.str.empty())
    {
        jOut["PowerDomains"]["@odata.id"] = boost::urls::format(
            "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/PowerDomains",
            BMCWEB_REDFISH_MANAGER_URI_NAME);
    }

    if (!powerStateGroupDbusPath.str.empty())
    {
        jOut["PowerStateGroup"]["@odata.id"] = boost::urls::format(
            "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/PowerStateGroup",
            BMCWEB_REDFISH_MANAGER_URI_NAME);
    }

    // Add ACLossPolicy
    jOut["ACLossPolicy"]["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/ACLossPolicy",
        BMCWEB_REDFISH_MANAGER_URI_NAME);

    // Add PSUCompliancePolicy
    jOut["PSUCompliancePolicy"]["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/PSUCompliancePolicy",
        BMCWEB_REDFISH_MANAGER_URI_NAME);

    // Add ManagerType last
    jOut["ManagerType"] =
        nvidia_power_compliance_manager::NvidiaManagerType::PowerManager;

    // Process PowerPolicies
    if (!powerPoliciesDbusPath.str.empty())
    {
        constexpr std::array<std::string_view, 1> interfaces = {
            "com.Nvidia.State.PowerCompliance.PowerPolicy"};
        dbus::utility::getSubTreePaths(
            powerPoliciesDbusPath, 0, interfaces,
            std::bind_front(
                nvidia_oem_power_policy::processGetTopLevelPowerPolicies,
                asyncResp));
    }

    // Add PowerBrake actions to the PowerCompliance resource
    nlohmann::json& actions = jOut["Actions"];
    actions["#NvidiaPowerComplianceManager.AssertPowerBrake"]["target"] =
        boost::urls::format(
            "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/Actions/NvidiaPowerComplianceManager.AssertPowerBrake",
            BMCWEB_REDFISH_MANAGER_URI_NAME);

    actions["#NvidiaPowerComplianceManager.DeassertPowerBrake"]["target"] =
        boost::urls::format(
            "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/Actions/NvidiaPowerComplianceManager.DeassertPowerBrake",
            BMCWEB_REDFISH_MANAGER_URI_NAME);
}

inline void handlePowerComplianceGetRequest(
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

    // Get all properties from D-Bus
    std::function<void(const boost::system::error_code&,
                       const dbus::utility::DBusPropertiesMap&)>
        callback = std::bind_front(afterGetPowerComplianceProperties, asyncResp,
                                   managerId);

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, "com.Nvidia.RackPowerCompliance",
        "/com/nvidia/state/power_compliance",
        "com.Nvidia.State.PowerCompliance", std::move(callback));
}

inline void requestRoutesNvidiaPowerCompliance(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePowerComplianceGetRequest, std::ref(app)));
}

inline void requestRoutesNvidiaPowerComplianceManagerActions(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/Actions/NvidiaPowerComplianceManager.AssertPowerBrake")
        .privileges(redfish::privileges::postManager)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(assertPowerBrake, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/Actions/NvidiaPowerComplianceManager.DeassertPowerBrake")
        .privileges(redfish::privileges::postManager)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(deassertPowerBrake, std::ref(app)));
}

} // namespace nvidia_oem_managers_pmc

} // namespace redfish
