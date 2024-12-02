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

#include "app.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"

namespace redfish
{
static const std::string& entityMangerServiceName =
    "xyz.openbmc_project.EntityManager";

static constexpr auto voltageLeakDetectorConfigInterface =
    "xyz.openbmc_project.Configuration.VoltageLeakDetector";
static constexpr std::array<std::string_view, 1> leakDetectorConfigInterfaces =
    {voltageLeakDetectorConfigInterface};

inline void handleLeakDetectionPolicyHead(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        [asyncResp,
         chassisId](const std::optional<std::string>& validChassisPath) {
            if (!validChassisPath)
            {
                messages::resourceNotFound(asyncResp->res, "Chassis",
                                           chassisId);
                return;
            }
            asyncResp->res.addHeader(
                boost::beast::http::field::link,
                "</redfish/v1/JsonSchemas/Policy/Policy.json>; rel=describedby");
        });
}

inline bool checkChassisId(const std::string& path,
                           const std::string& chassisId)
{
    std::string chassisName =
        sdbusplus::message::object_path(path).parent_path().filename();

    return (chassisName == chassisId);
}

inline void handleLeakDetectorPolicyEnabled(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code ec, const bool shutdownOnLeak)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("DBUS response error for ShutdownOnLeak");
        messages::internalError(asyncResp->res);
        return;
    }

    if (shutdownOnLeak)
    {
        asyncResp->res.jsonValue["PolicyEnabled"] = true;
    }
}

inline void handleLeakDetectorPolicyPathsGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId,
    const dbus::utility::MapperGetSubTreePathsResponse& leakDetectorPaths)
{
    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/Policy/Policy.json>; rel=describedby");
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/Policies/LeakDetectionPolicy", chassisId);
    asyncResp->res.jsonValue["@odata.type"] = "#Policy.v1_0_0.Policy";
    asyncResp->res.jsonValue["Name"] = "Policy for Leak Detection";
    asyncResp->res.jsonValue["Id"] = "LeakDetectionPolicy";

    asyncResp->res.jsonValue["PolicyEnabled"] = false;

    asyncResp->res.jsonValue["PolicyConditionLogic"] = "AnyOf";

    // PolicyConditions
    nlohmann::json& conditionsArray =
        asyncResp->res.jsonValue["PolicyConditions"];
    conditionsArray = nlohmann::json::array();
    for (const auto& leakDetectorPath : leakDetectorPaths)
    {
        if (!checkChassisId(leakDetectorPath, chassisId))
        {
            continue;
        }

        // Determine the current PolicyEnabled setting. If setting of any one
        // detector was enabled, then we will indicate the policy as enabled
        sdbusplus::asio::getProperty<bool>(
            *crow::connections::systemBus, entityMangerServiceName,
            leakDetectorPath, voltageLeakDetectorConfigInterface,
            "ShutdownOnLeak",
            [asyncResp](const boost::system::error_code ec,
                        const bool shutdownOnLeak) {
                handleLeakDetectorPolicyEnabled(asyncResp, ec, shutdownOnLeak);
            });

        // Add the detector as a member of PolicyCondition
        boost::urls::url propertyURI = boost::urls::format(
            "/redfish/v1/Chassis/{}/ThermalSubsystem/LeakDetection/LeakDetectors/{}/#DetectorState",
            chassisId,
            sdbusplus::message::object_path(leakDetectorPath).filename());

        nlohmann::json::object_t conditionObject;
        conditionObject.emplace("Property", propertyURI);
        conditionObject.emplace("PropertyStringValue", "Critical");

        conditionsArray.emplace_back(std::move(conditionObject));
    }

    // PolicyReactions
    nlohmann::json& reactionsArray =
        asyncResp->res.jsonValue["PolicyReactions"];
    reactionsArray = nlohmann::json::array();

    nlohmann::json::object_t commonReactionObject;
    commonReactionObject.emplace("CommonReaction", "HardPowerOff");

    reactionsArray.emplace_back(std::move(commonReactionObject));

    asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
    asyncResp->res.jsonValue["Status"]["Health"] = "OK";
}

inline void getLeakDetectorPolicyPaths(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::function<void(const dbus::utility::MapperGetSubTreePathsResponse&
                                 leakDetectorPaths)>&& callback)
{
    sdbusplus::message::object_path inventoryPath(
        "/xyz/openbmc_project/inventory");

    dbus::utility::getSubTreePaths(
        inventoryPath, 0, leakDetectorConfigInterfaces,
        [asyncResp, callback = std::move(callback)](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreePathsResponse& subtreePaths) {
            if (ec)
            {
                if (ec.value() != EBADR)
                {
                    BMCWEB_LOG_ERROR(
                        "DBUS response error for getSubTreePaths {}",
                        ec.value());
                    messages::internalError(asyncResp->res);
                }
                return;
            }
            callback(subtreePaths);
        });
}

inline void doLeakDetectionPolicyGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    getLeakDetectorPolicyPaths(asyncResp,
                               std::bind_front(handleLeakDetectorPolicyPathsGet,
                                               asyncResp, chassisId));
}

inline void handleLeakDetectionPolicyGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(doLeakDetectionPolicyGet, asyncResp, chassisId));
}

inline void handleLeakDetectorPolicyPathsPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const bool policyEnabled,
    const dbus::utility::MapperGetSubTreePathsResponse& leakDetectorPaths)
{
    for (const auto& leakDetectorPath : leakDetectorPaths)
    {
        if (!checkChassisId(leakDetectorPath, chassisId))
        {
            continue;
        }

        setDbusProperty(asyncResp, "LeakPolicyReaction",
                        entityMangerServiceName, leakDetectorPath,
                        voltageLeakDetectorConfigInterface, "ShutdownOnLeak",
                        policyEnabled);
    }
}

inline void doLeakDetectionPolicyEnabledPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const bool policyEnabled,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    getLeakDetectorPolicyPaths(
        asyncResp, std::bind_front(handleLeakDetectorPolicyPathsPatch,
                                   asyncResp, chassisId, policyEnabled));
}

inline void handleLeakDetectionPolicyPatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::optional<bool> policyEnabled;

    if (!json_util::readJsonPatch(req, asyncResp->res, "PolicyEnabled",
                                  policyEnabled))
    {
        return;
    }

    if (policyEnabled)
    {
        redfish::chassis_utils::getValidChassisPath(
            asyncResp, chassisId,
            std::bind_front(doLeakDetectionPolicyEnabledPatch, asyncResp,
                            chassisId, *policyEnabled));
    }
}

inline void requestRoutesLeakDetectionPolicy(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Policies/LeakDetectionPolicy/")
        .privileges(redfish::privileges::privilegeSetLogin)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleLeakDetectionPolicyHead, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Policies/LeakDetectionPolicy/")
        .privileges(redfish::privileges::privilegeSetLogin)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleLeakDetectionPolicyGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Policies/LeakDetectionPolicy/")
        .privileges(redfish::privileges::privilegeSetConfigureManager)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleLeakDetectionPolicyPatch, std::ref(app)));
}

inline void handlePolicyCollectionHead(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        [asyncResp,
         chassisId](const std::optional<std::string>& validChassisPath) {
            if (!validChassisPath)
            {
                messages::resourceNotFound(asyncResp->res, "Chassis",
                                           chassisId);
                return;
            }
            asyncResp->res.addHeader(
                boost::beast::http::field::link,
                "</redfish/v1/JsonSchemas/PolicyCollection/PolicyCollection.json>; rel=describedby");
        });
}

inline void
    doPolicyCollection(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& chassisId,
                       const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }
    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/PolicyCollection/PolicyCollection.json>; rel=describedby");
    asyncResp->res.jsonValue["@odata.type"] =
        "#PolicyCollection.PolicyCollection";
    asyncResp->res.jsonValue["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}/Policies", chassisId);
    asyncResp->res.jsonValue["Name"] = "Policy Collection";
    asyncResp->res.jsonValue["Description"] =
        "Collection of Policies for Chassis " + chassisId;

    // Collection members
    nlohmann::json& membersArray = asyncResp->res.jsonValue["Members"];
    membersArray = nlohmann::json::array();

#ifdef BMCWEB_REDFISH_LEAK_DETECT
    boost::urls::url leakDetectionPolicyUrl = boost::urls::format(
        "/redfish/v1/Chassis/{}/Policies/LeakDetectionPolicy", chassisId);

    nlohmann::json::object_t leakDetectionPolicyObject;
    leakDetectionPolicyObject.emplace("@odata.id", leakDetectionPolicyUrl);

    membersArray.emplace_back(std::move(leakDetectionPolicyObject));
#endif
    asyncResp->res.jsonValue["Members@odata.count"] = membersArray.size();
}

inline void handlePolicyCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(doPolicyCollection, asyncResp, chassisId));
}

inline void requestPolicyCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Policies/")
        .privileges(redfish::privileges::privilegeSetLogin)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handlePolicyCollectionHead, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Policies/")
        .privileges(redfish::privileges::privilegeSetLogin)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePolicyCollectionGet, std::ref(app)));
}

} // namespace redfish
