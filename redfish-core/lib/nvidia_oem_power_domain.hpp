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

#include "nvidia_oem_power_policy.hpp"
#include "nvidia_oem_power_profile.hpp"

namespace redfish
{

namespace nvidia_oem_power_domain
{

inline void afterGetPowerDomainProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::object_path& dbusPath,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec.value() == EBADR || ec == boost::system::errc::host_unreachable)
    {
        messages::resourceNotFound(asyncResp->res, "PowerDomain",
                                   dbusPath.filename());
        return;
    }

    if (ec)
    {
        BMCWEB_LOG_ERROR("D-Bus response error on GetPowerDomainProperties {}",
                         ec);
        messages::internalError(asyncResp->res);
        return;
    }

    std::string name;
    std::string sensorReadingType;
    std::string sensorImpl;
    std::string type;
    std::string unit;
    sdbusplus::message::object_path powerPoliciesDbusPath;
    uint64_t value = 0;

    // clang-format off
    bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties,
        "Name", name, "PowerPolicies", powerPoliciesDbusPath,
        "SensorImpl", sensorImpl, "SensorReadingType", sensorReadingType,
        "Type", type, "Unit", unit, "Value", value);
    // clang-format on

    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/PowerDomains/{}",
        BMCWEB_REDFISH_MANAGER_URI_NAME, dbusPath.filename());
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaPowerDomain.v1_0_0.NvidiaPowerDomain";
    asyncResp->res.jsonValue["Id"] = dbusPath.filename();
    asyncResp->res.jsonValue["Name"] = name;
    asyncResp->res.jsonValue["Value"] = value;
    asyncResp->res.jsonValue["Type"] = type;
    asyncResp->res.jsonValue["Unit"] = unit;
    asyncResp->res.jsonValue["SensorReadingType"] = sensorReadingType;
    asyncResp->res.jsonValue["SensorImpl"] = sensorImpl;

    if (!powerPoliciesDbusPath.str.empty())
    {
        asyncResp->res
            .jsonValue["PowerPolicies"]["@odata.id"] = boost::urls::format(
            "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/PowerDomains/{}/PowerPolicies",
            BMCWEB_REDFISH_MANAGER_URI_NAME, dbusPath.filename());
    }
}

/**
 * @brief Handles GET request for PowerDomain which reads from PowerDomain DBus
 * objects
 * @param app - crow application
 * @param req - crow request
 * @param asyncResp - response object
 * @param managerId - id of Manager
 * @param powerDomainId - id of the PowerDomain
 * @return None
 */
inline void handlePowerDomainGetRequest(
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

    // Dynamically construct the D-Bus paths based on the powerDomainId
    sdbusplus::message::object_path dbusPath(
        "/com/nvidia/state/power_compliance/power_domain");
    dbusPath /= powerDomainId;

    // Get all properties from D-Bus
    std::function<void(const boost::system::error_code&,
                       const dbus::utility::DBusPropertiesMap&)>
        callback =
            std::bind_front(afterGetPowerDomainProperties, asyncResp, dbusPath);

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, "com.Nvidia.RackPowerCompliance",
        dbusPath, "com.Nvidia.State.PowerCompliance.PowerDomain",
        std::move(callback));
}

/**
 * @brief Handles PATCH request for PowerDomain which writes to PowerDomain DBus
 * objects
 * @param app - crow application
 * @param req - crow request
 * @param asyncResp - response object
 * @param managerId - id of Manager
 * @param powerDomainId - id of the PowerDomain
 * @return None
 */
inline void handlePowerDomainPatchRequest(
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

    // Dynamically construct the D-Bus paths based on the powerDomainId
    sdbusplus::message::object_path dbusPath(
        "/com/nvidia/state/power_compliance/power_domain");
    dbusPath /= powerDomainId;

    std::optional<uint64_t> newValue;
    std::optional<std::string> newType;

    if (!json_util::readJsonPatch(req, asyncResp->res, "Value", newValue,
                                  "Type", newType))
    {
        return;
    }

    if (newValue)
    {
        setDbusProperty(
            asyncResp, "Value", "com.Nvidia.RackPowerCompliance", dbusPath,
            "com.Nvidia.State.PowerCompliance.PowerDomain", "Value", *newValue);
    }

    if (newType)
    {
        setDbusProperty(
            asyncResp, "Type", "com.Nvidia.RackPowerCompliance", dbusPath,
            "com.Nvidia.State.PowerCompliance.PowerDomain", "Type", *newType);
    }
}

/**
 * @brief Handles GET request for PowerDomainCollection which reads from
 * PowerDomainCollection DBus object
 * @param app - crow application
 * @param req - crow request
 * @param asyncResp - response object
 * @param managerId - id of Manager
 * @return None
 */
inline void handlePowerDomainCollectionGetRequest(
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
        "#NvidiaPowerDomainCollection.NvidiaPowerDomainCollection";
    asyncResp->res.jsonValue["Name"] = "Power Domain Collection";

    sdbusplus::message::object_path dbusPath(
        "/com/nvidia/state/power_compliance/power_domain");

    nvidia_oem_power_profile::handlePowerProfileCollectionGetRequest(
        app, dbusPath, "com.Nvidia.State.PowerCompliance.PowerDomain",
        boost::urls::format(
            "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/PowerDomains",
            BMCWEB_REDFISH_MANAGER_URI_NAME),
        req, asyncResp, managerId);
}

inline void requestRoutesNvidiaPowerDomain(App& app)
{
    /**
     * Define the GET route for PowerDomain
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/PowerDomains/<str>/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePowerDomainGetRequest, std::ref(app)));

    /**
     * Define the GET route for PowerDomainCollection
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/PowerDomains/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handlePowerDomainCollectionGetRequest, std::ref(app)));

    /**
     * Define the PATCH route for PowerDomain
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/PowerDomains/<str>/")
        .privileges(redfish::privileges::patchManager)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handlePowerDomainPatchRequest, std::ref(app)));
}

} // namespace nvidia_oem_power_domain

} // namespace redfish
