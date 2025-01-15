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

inline static bool checkChassisId(const std::string& path,
                           const std::string& chassisId)
{
    std::string chassisName =
        sdbusplus::message::object_path(path).parent_path().filename();

    return (chassisName == chassisId);
}

inline void handleLeakDetectorPolicyProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& propertiesList)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("DBUS response error: Leak Detect Policy Properties.");
        messages::internalError(asyncResp->res);
        return;
    }

    const bool* shutdownOnLeak = nullptr;
    const double* shutdownDelaySeconds = nullptr;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), propertiesList, "ShutdownOnLeak",
        shutdownOnLeak, "ShutdownDelaySeconds", shutdownDelaySeconds);

    if (!success)
    {
        BMCWEB_LOG_DEBUG("DBUS response error: Unpacking Policy Properties.");
        messages::internalError(asyncResp->res);
        return;
    }

    // Determine the current PolicyEnabled setting. Note that this will be set
    // by each detector based on current configs. We expect the values to be
    // same across all detectors and will keep logic simple here.
    if (shutdownOnLeak != nullptr)
    {
        asyncResp->res.jsonValue["PolicyEnabled"] = *shutdownOnLeak;
    }

    // Indicate the PolicyReactions setting. Note that ReactionDelaySeconds will
    // be set by each detector. We expect that the values are same across all
    // detectors and will keep the logic simple here.
    if (shutdownDelaySeconds != nullptr)
    {
        nlohmann::json::array_t reactionsArray;

        nlohmann::json::object_t commonReactionObject;
        commonReactionObject.emplace("CommonReaction", "HardPowerOff");
        commonReactionObject.emplace("ReactionDelaySeconds",
                                     *shutdownDelaySeconds);

        reactionsArray.emplace_back(std::move(commonReactionObject));
        asyncResp->res.jsonValue["PolicyReactions"] = std::move(reactionsArray);
    }
}

inline void handleLeakDetectorPolicyPathsGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId,
    const dbus::utility::MapperGetSubTreePathsResponse& leakDetectorPaths)
{
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/Oem/Nvidia/Policies/LeakDetectionPolicy",
        chassisId);
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaPolicy.v1_0_0.NvidiaPolicy";
    asyncResp->res.jsonValue["Name"] = "Policy for Leak Detection";
    asyncResp->res.jsonValue["Id"] = "LeakDetectionPolicy";

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

        sdbusplus::asio::getAllProperties(
            *crow::connections::systemBus, entityMangerServiceName,
            leakDetectorPath, voltageLeakDetectorConfigInterface,
            [asyncResp](
                const boost::system::error_code& ec,
                const dbus::utility::DBusPropertiesMap& propertiesList) {
            handleLeakDetectorPolicyProperties(asyncResp, ec, propertiesList);
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

    redfish::nvidia_chassis_utils::getValidLeakDetectionPath(
        asyncResp, chassisId,
        std::bind_front(doLeakDetectionPolicyGet, asyncResp, chassisId));
}

inline void handleLeakDetectorPolicyEnablePatch(
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

        setDbusProperty(asyncResp, "ShutdownOnLeak", entityMangerServiceName,
                        leakDetectorPath, voltageLeakDetectorConfigInterface,
                        "ShutdownOnLeak", policyEnabled);
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
        asyncResp, std::bind_front(handleLeakDetectorPolicyEnablePatch,
                                   asyncResp, chassisId, policyEnabled));
}

inline void handleLeakDetectorReactionDelayPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const double reactionDelaySeconds,
    const dbus::utility::MapperGetSubTreePathsResponse& leakDetectorPaths)
{
    for (const auto& leakDetectorPath : leakDetectorPaths)
    {
        if (!checkChassisId(leakDetectorPath, chassisId))
        {
            continue;
        }

        setDbusProperty(asyncResp, "ShutdownDelaySeconds",
                        entityMangerServiceName, leakDetectorPath,
                        voltageLeakDetectorConfigInterface,
                        "ShutdownDelaySeconds", reactionDelaySeconds);
    }
}

inline void doLeakDetectionReactionDelayPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const double reactionDelaySeconds,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    getLeakDetectorPolicyPaths(
        asyncResp, std::bind_front(handleLeakDetectorReactionDelayPatch,
                                   asyncResp, chassisId, reactionDelaySeconds));
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
    std::optional<double> reactionDelaySeconds;

    if (!json_util::readJsonPatch(req, asyncResp->res, "PolicyEnabled",
                                  policyEnabled, "ReactionDelaySeconds",
                                  reactionDelaySeconds))
    {
        return;
    }

    if (policyEnabled)
    {
        redfish::nvidia_chassis_utils::getValidLeakDetectionPath(
            asyncResp, chassisId,
            std::bind_front(doLeakDetectionPolicyEnabledPatch, asyncResp,
                            chassisId, *policyEnabled));
    }

    if (reactionDelaySeconds)
    {
        redfish::chassis_utils::getValidChassisPath(
            asyncResp, chassisId,
            std::bind_front(doLeakDetectionReactionDelayPatch, asyncResp,
                            chassisId, *reactionDelaySeconds));
    }
}

inline void requestRoutesLeakDetectionPolicy(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Oem/Nvidia/Policies/LeakDetectionPolicy/")
        .privileges(redfish::privileges::privilegeSetLogin)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleLeakDetectionPolicyGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Oem/Nvidia/Policies/LeakDetectionPolicy/")
        .privileges(redfish::privileges::privilegeSetConfigureManager)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleLeakDetectionPolicyPatch, std::ref(app)));
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
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaPolicyCollection.NvidiaPolicyCollection";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/Oem/Nvidia/Policies", chassisId);
    asyncResp->res.jsonValue["Name"] = "Policy Collection";
    asyncResp->res.jsonValue["Description"] =
        "Collection of Policies for Chassis " + chassisId;

    // Collection members
    nlohmann::json& membersArray = asyncResp->res.jsonValue["Members"];
    membersArray = nlohmann::json::array();

    if constexpr (BMCWEB_REDFISH_LEAK_DETECT)
    {
        boost::urls::url leakDetectionPolicyUrl = boost::urls::format(
        "/redfish/v1/Chassis/{}/Oem/Nvidia/Policies/LeakDetectionPolicy",
        chassisId);

        nlohmann::json::object_t leakDetectionPolicyObject;
        leakDetectionPolicyObject.emplace("@odata.id", leakDetectionPolicyUrl);

        membersArray.emplace_back(std::move(leakDetectionPolicyObject));
    }
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

    redfish::nvidia_chassis_utils::getValidLeakDetectionPath(
        asyncResp, chassisId,
        std::bind_front(doPolicyCollection, asyncResp, chassisId));
}

inline void requestPolicyCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/Policies/")
        .privileges(redfish::privileges::privilegeSetLogin)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePolicyCollectionGet, std::ref(app)));
}

} // namespace redfish
