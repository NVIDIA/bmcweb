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
#include "redfish_util.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_time_utils.hpp"
#include "utils/time_utils.hpp"

namespace redfish
{

namespace nvidia_oem_power_policy
{

inline void processGetTopLevelPowerPolicies(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& paths)
{
    if (ec.value() == EBADR || ec == boost::system::errc::host_unreachable)
    {
        messages::resourceNotFound(asyncResp->res, "PowerPolicy",
                                   BMCWEB_REDFISH_MANAGER_URI_NAME);
        return;
    }

    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "Process Power Compliance Manager Get Managed Objects error {}",
            ec);
        messages::internalError(asyncResp->res);
        return;
    }

    for (const auto& objectPath : paths)
    {
        sdbusplus::message::object_path path(objectPath);

        std::string filename = path.filename();
        if (filename.empty())
        {
            continue;
        }

        asyncResp->res.jsonValue[filename]["@odata.id"] = boost::urls::format(
            "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/{}",
            BMCWEB_REDFISH_MANAGER_URI_NAME, filename);
    }
}

inline void afterGetPowerPolicyProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::urls::url& redfishUri,
    const sdbusplus::message::object_path& dbusPath,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec.value() == EBADR || ec == boost::system::errc::host_unreachable)
    {
        messages::resourceNotFound(asyncResp->res, "PowerPolicy",
                                   dbusPath.filename());
        return;
    }

    if (ec)
    {
        BMCWEB_LOG_ERROR("D-Bus response error on GetPowerPolicyProperties {}",
                         ec);
        messages::internalError(asyncResp->res);
        return;
    }

    bool autoDeassertPowerBrake = false;
    std::string dwellTime;
    uint64_t maxThreshold = 0;
    uint64_t minThreshold = 0;
    std::string name;
    std::string policyActions;
    std::string type;
    std::string unit;

    // clang-format off
    bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties, "AutoDeassertPowerBrake",
        autoDeassertPowerBrake, "DwellTime", dwellTime, "Max", maxThreshold, "Min",
        minThreshold, "Name", name, "PolicyActions", policyActions, "Type",
        type, "Unit", unit);
    // clang-format on

    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    asyncResp->res.jsonValue["@odata.id"] = redfishUri;
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaPowerPolicy.v1_0_0.NvidiaPowerPolicy";
    asyncResp->res.jsonValue["Id"] = dbusPath.filename();
    asyncResp->res.jsonValue["Name"] = name;
    asyncResp->res.jsonValue["Min"] = minThreshold;
    asyncResp->res.jsonValue["Max"] = maxThreshold;
    asyncResp->res.jsonValue["Unit"] = unit;
    asyncResp->res.jsonValue["Type"] = type;
    asyncResp->res.jsonValue["DwellTime"] = dwellTime;
    asyncResp->res.jsonValue["AutoDeassertPowerBrake"] = autoDeassertPowerBrake;
    asyncResp->res.jsonValue["PolicyActions"] = policyActions;
}

/**
 * @brief Handles GET request for PowerPolicy that is part of a PowerDomain
 * which reads from PowerDomain and PowerPolicy DBus objects
 * @param app - crow application
 * @param req - crow request
 * @param asyncResp - response object
 * @param managerId - id of Manager
 * @param powerDomainId - id of the PowerDomain
 * @param powerPolicyId - id of the PowerPolicy
 * @return None
 */
inline void handlePowerPolicyGetRequest(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& powerDomainId,
    const std::string& powerPolicyId)
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
        "/com/nvidia/state/power_compliance/power_domain");
    dbusPath /= powerDomainId;
    dbusPath /= "power_policy";
    dbusPath /= powerPolicyId;

    boost::urls::url redfishUri = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/PowerDomains/{}/PowerPolicies/{}",
        managerId, powerDomainId, powerPolicyId);

    // Get all properties from D-Bus
    std::function<void(const boost::system::error_code&,
                       const dbus::utility::DBusPropertiesMap&)>
        callback = std::bind_front(afterGetPowerPolicyProperties, asyncResp,
                                   redfishUri, dbusPath);

    dbus::utility::getAllProperties(
        "com.Nvidia.RackPowerCompliance", dbusPath,
        "com.Nvidia.State.PowerCompliance.PowerPolicy", std::move(callback));
}

/**
 * @brief Handles GET request for top-level PowerPolicy under
 * /redfish/v1/Managers/{ManagerId}/Oem/Nvidia/PowerCompliance extension which
 * reads from top-level PowerPolicy DBus objects.
 * @param app - crow application
 * @param powerPolicyId - id of top-level PowerPolicy
 * @param req - crow request
 * @param asyncResp - response object
 * @param managerId - id of Manager
 * @return None
 */
inline void handlePowerPolicyTopLevelGetRequest(
    App& app, const std::string& powerPolicyId, const crow::Request& req,
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
        "/com/nvidia/state/power_compliance/power_policy");
    dbusPath /= powerPolicyId;

    boost::urls::url redfishUri = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/{}", managerId,
        powerPolicyId);

    // Get all properties from D-Bus
    std::function<void(const boost::system::error_code&,
                       const dbus::utility::DBusPropertiesMap&)>
        callback = std::bind_front(afterGetPowerPolicyProperties, asyncResp,
                                   redfishUri, dbusPath);

    dbus::utility::getAllProperties(
        "com.Nvidia.RackPowerCompliance", dbusPath,
        "com.Nvidia.State.PowerCompliance.PowerPolicy", std::move(callback));
}

/**
 * @brief Handles GET request for PowerPolicyCollection which reads from
 * PowerPolicyCollection DBus objects under PowerDomains
 * @param app - crow application
 * @param req - crow request
 * @param asyncResp - response object
 * @param managerId - id of Manager
 * @param powerDomainId - id of the PowerDomain containing the
 * PowerPolicyCollection
 * @return None
 */
inline void handlePowerPolicyCollectionGetRequest(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& powerDomainId)
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
        "#NvidiaPowerPolicyCollection.NvidiaPowerPolicyCollection";
    asyncResp->res.jsonValue["Name"] = "Power Policies Collection";

    // Dynamically construct the D-Bus paths based on the powerPolicyId
    sdbusplus::message::object_path dbusPath(
        "/com/nvidia/state/power_compliance/power_domain");
    dbusPath /= powerDomainId;
    dbusPath /= "power_policy";

    nvidia_oem_power_profile::handlePowerProfileCollectionGetRequest(
        app, dbusPath, "com.Nvidia.State.PowerCompliance.PowerPolicy",
        boost::urls::format(
            "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/PowerDomains/{}/PowerPolicies",
            BMCWEB_REDFISH_MANAGER_URI_NAME, powerDomainId),
        req, asyncResp, managerId);
}

/**
 * @brief Handles PATCH request for PowerPolicy which writes into PowerPolicy
 * DBus objects.
 * @param app - crow application
 * @param req - crow request
 * @param asyncResp - response object
 * @param managerId - id of Manager
 * @param powerDomainId - id of the PowerDomain containing the PowerPolicy
 * @return None
 */
inline void handlePowerPolicyPatchRequest(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& powerDomainId,
    const std::string& powerPolicyId)
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

    // Dynamically construct the D-Bus paths based on the powerDomainId
    sdbusplus::message::object_path dbusPath(
        "/com/nvidia/state/power_compliance/power_domain");
    dbusPath /= powerDomainId;
    dbusPath /= "power_policy";
    dbusPath /= powerPolicyId;

    std::optional<std::string> newDwellTime;
    std::optional<bool> newAutoDeassertPowerBrake;
    std::optional<uint64_t> newMin;
    std::optional<uint64_t> newMax;
    std::optional<std::string> newType;
    std::optional<std::string> newPolicyActions;

    if (!json_util::readJsonPatch(
            req, asyncResp->res, "AutoDeassertPowerBrake",
            newAutoDeassertPowerBrake, "DwellTime", newDwellTime, "Min", newMin,
            "Max", newMax, "PolicyActions", newPolicyActions, "Type", newType))
    {
        return;
    }

    if (newAutoDeassertPowerBrake)
    {
        setDbusProperty(asyncResp, "AutoDeassertPowerBrake",
                        "com.Nvidia.RackPowerCompliance", dbusPath,
                        "com.Nvidia.State.PowerCompliance.PowerPolicy",
                        "AutoDeassertPowerBrake", *newAutoDeassertPowerBrake);
    }

    if (newDwellTime)
    {
        std::optional<std::chrono::milliseconds> dwellTime =
            time_utils::fromDurationString(*newDwellTime);
        if (!dwellTime)
        {
            messages::propertyValueIncorrect(asyncResp->res, "DwellTime",
                                             newDwellTime.value());
            return;
        }

        setDbusProperty(asyncResp, "DwellTime",
                        "com.Nvidia.RackPowerCompliance", dbusPath,
                        "com.Nvidia.State.PowerCompliance.PowerPolicy",
                        "DwellTime", *newDwellTime);
    }

    if (newMin)
    {
        setDbusProperty(
            asyncResp, "Min", "com.Nvidia.RackPowerCompliance", dbusPath,
            "com.Nvidia.State.PowerCompliance.PowerPolicy", "Min", *newMin);
    }

    if (newMax)
    {
        setDbusProperty(
            asyncResp, "Max", "com.Nvidia.RackPowerCompliance", dbusPath,
            "com.Nvidia.State.PowerCompliance.PowerPolicy", "Max", *newMax);
    }

    if (newType)
    {
        setDbusProperty(
            asyncResp, "Type", "com.Nvidia.RackPowerCompliance", dbusPath,
            "com.Nvidia.State.PowerCompliance.PowerPolicy", "Type", *newType);
    }

    if (newPolicyActions)
    {
        setDbusProperty(asyncResp, "PolicyActions",
                        "com.Nvidia.RackPowerCompliance", dbusPath,
                        "com.Nvidia.State.PowerCompliance.PowerPolicy",
                        "PolicyActions", *newPolicyActions);
    }
}

/**
 * @brief Handles PATCH request for top-level PowerPolicy which writes into
 * PowerPolicy DBus objects.
 * @param app - crow application
 * @param powerPolicyId - id of top-level PowerPolicy
 * @param req - crow request
 * @param asyncResp - response object
 * @param managerId - id of Manager
 * @return None
 */
inline void handlePowerPolicyTopLevelPatchRequest(
    App& app, const std::string& powerPolicyId, const crow::Request& req,
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

    // Dynamically construct the D-Bus paths based on the powerPolicyId
    sdbusplus::message::object_path dbusPath(
        "/com/nvidia/state/power_compliance/power_policy");
    dbusPath /= powerPolicyId;

    std::optional<std::string> newPolicyActions;

    if (!json_util::readJsonPatch(req, asyncResp->res, "PolicyActions",
                                  newPolicyActions))
    {
        return;
    }

    if (newPolicyActions)
    {
        setDbusProperty(asyncResp, "PolicyActions",
                        "com.Nvidia.RackPowerCompliance", dbusPath,
                        "com.Nvidia.State.PowerCompliance.PowerPolicy",
                        "PolicyActions", *newPolicyActions);
    }
}

inline void requestRoutesNvidiaPowerPolicy(App& app)
{
    /**
     * Define the GET route for PowerPolicy
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/PowerDomains/<str>/PowerPolicies/<str>/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePowerPolicyGetRequest, std::ref(app)));

    /**
     * Define the GET route for PowerPolicyCollection
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/PowerDomains/<str>/PowerPolicies/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handlePowerPolicyCollectionGetRequest, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/ACLossPolicy/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePowerPolicyTopLevelGetRequest, std::ref(app),
                            "ACLossPolicy"));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/PSUCompliancePolicy/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePowerPolicyTopLevelGetRequest, std::ref(app),
                            "PSUCompliancePolicy"));

    /**
     * Define the PATCH route for ACLossPolicy
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/ACLossPolicy/")
        .privileges(redfish::privileges::patchManager)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handlePowerPolicyTopLevelPatchRequest,
                            std::ref(app), "ACLossPolicy"));
    /**
     * Define the PATCH route for PSUCompliancePolicy
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/PSUCompliancePolicy/")
        .privileges(redfish::privileges::patchManager)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handlePowerPolicyTopLevelPatchRequest,
                            std::ref(app), "PSUCompliancePolicy"));
    /**
     * Define the PATCH route for PowerPolicy
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/PowerDomains/<str>/PowerPolicies/<str>/")
        .privileges(redfish::privileges::patchManager)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handlePowerPolicyPatchRequest, std::ref(app)));
}

} // namespace nvidia_oem_power_policy

} // namespace redfish
