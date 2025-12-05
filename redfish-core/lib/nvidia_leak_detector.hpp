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
#include "generated/enums/leak_detector.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
namespace redfish
{
static constexpr auto leakDetectorStateInterface =
    "xyz.openbmc_project.State.LeakDetector";
static constexpr auto leakDetectorOpStatusInterface =
    "xyz.openbmc_project.State.Decorator.OperationalStatus";
static constexpr auto leakDetectorInventoryInterface =
    "xyz.openbmc_project.Inventory.Item.LeakDetector";
static constexpr auto leakDetectorPolicyConfigInterface =
    "xyz.openbmc_project.Configuration.LeakDetectionPolicy";

constexpr std::array<std::string_view, 1> leakDetectorInventoryInterfaces = {
    leakDetectorInventoryInterface};

constexpr std::array<std::string_view, 2> leakDetectorStateInterfaces = {
    leakDetectorStateInterface, leakDetectorOpStatusInterface};

// Struct to hold leak detector policy properties
struct LeakDetectorPolicyProperties
{
    std::optional<std::string> criticalReactionType;
    std::optional<std::string> warningReactionType;
    std::optional<double> reactionDelaySeconds;
};

inline std::string getDetectorState(const std::string& detectorState)
{
    if (detectorState ==
        "xyz.openbmc_project.State.LeakDetector.DetectorStateEnum.OK")
    {
        return "OK";
    }
    if (detectorState ==
        "xyz.openbmc_project.State.LeakDetector.DetectorStateEnum.Warning")
    {
        return "Warning";
    }
    if (detectorState ==
        "xyz.openbmc_project.State.LeakDetector.DetectorStateEnum.Critical")
    {
        return "Critical";
    }
    // Unknown or others
    return "";
}

inline std::string getOperationalStatus(const std::string& operationalStatus)
{
    if (operationalStatus ==
        "xyz.openbmc_project.State.Decorator.OperationalStatus.StateType.None")
    {
        return "None";
    }
    if (operationalStatus ==
        "xyz.openbmc_project.State.Decorator.OperationalStatus.StateType.Absent")
    {
        return "Absent";
    }
    if (operationalStatus ==
        "xyz.openbmc_project.State.Decorator.OperationalStatus.StateType.Deferring")
    {
        return "Deferring";
    }
    if (operationalStatus ==
        "xyz.openbmc_project.State.Decorator.OperationalStatus.StateType.Degraded")
    {
        return "Degraded";
    }
    if (operationalStatus ==
        "xyz.openbmc_project.State.Decorator.OperationalStatus.StateType.Disabled")
    {
        return "Disabled";
    }
    if (operationalStatus ==
        "xyz.openbmc_project.State.Decorator.OperationalStatus.StateType.Enabled")
    {
        return "Enabled";
    }
    if (operationalStatus ==
        "xyz.openbmc_project.State.Decorator.OperationalStatus.StateType.StandbyOffline")
    {
        return "StandbyOffline";
    }
    if (operationalStatus ==
        "xyz.openbmc_project.State.Decorator.OperationalStatus.StateType.Starting")
    {
        return "Starting";
    }
    if (operationalStatus ==
        "xyz.openbmc_project.State.Decorator.OperationalStatus.StateType.UnavailableOffline")
    {
        return "UnavailableOffline";
    }
    if (operationalStatus ==
        "xyz.openbmc_project.State.Decorator.OperationalStatus.StateType.Updating")
    {
        return "Updating";
    }
    if (operationalStatus ==
        "xyz.openbmc_project.State.Decorator.OperationalStatus.StateType.Fault")
    {
        return "Fault";
    }
    // Unknown or others
    return "";
}

inline std::string getLeakDetectorType(const std::string& leakDetectorType)
{
    if (leakDetectorType ==
        "xyz.openbmc_project.Inventory.Item.LeakDetector.LeakDetectorTypeEnum.FloatSwitch")
    {
        return "FloatSwitch";
    }
    if (leakDetectorType ==
        "xyz.openbmc_project.Inventory.Item.LeakDetector.LeakDetectorTypeEnum.Moisture")
    {
        return "Moisture";
    }
    // Unknown or others
    return "";
}

inline leak_detector::ReactionType translateReactionTypeString(
    const std::string& reactionType)
{
    if (reactionType == "None")
    {
        return leak_detector::ReactionType::None;
    }
    if (reactionType == "ForceOff")
    {
        return leak_detector::ReactionType::ForceOff;
    }
    if (reactionType == "GracefulShutdown")
    {
        return leak_detector::ReactionType::GracefulShutdown;
    }
    return leak_detector::ReactionType::Invalid;
}

inline void getValidLeakDetectorPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& leakDetectorId,
    const std::function<void(const std::string& leakDetectorPath,
                             const std::string& service)>& callback)
{
    sdbusplus::message::object_path inventoryPath(
        "/xyz/openbmc_project/inventory/leakdetectors/");
    sdbusplus::message::object_path leakDetectorPath =
        inventoryPath / leakDetectorId;

    dbus::utility::getDbusObject(
        leakDetectorPath, leakDetectorInventoryInterfaces,
        [leakDetectorPath, leakDetectorId, asyncResp,
         callback](const boost::system::error_code& ec,
                   const dbus::utility::MapperGetObject& object) {
            if (ec || object.empty())
            {
                BMCWEB_LOG_ERROR("DBUS response error on getDbusObject {}",
                                 ec.value());
                messages::internalError(asyncResp->res);
                return;
            }

            callback(leakDetectorPath, object.begin()->first);
        });
}

inline void afterGetLeakDetectorName(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& leakDetectorId,
    const std::function<void(const std::string& leakDetectorConfigPath,
                             const std::string& service,
                             const std::string& configInterface)>& callback,
    const std::string& path, const std::string& service,
    const boost::system::error_code& ec, const std::string& leakDetectorName)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "Failed to get LeakDetectorName property from path {}: {}", path,
            ec.message());
        messages::internalError(asyncResp->res);
        return;
    }
    if (leakDetectorName == leakDetectorId)
    {
        callback(path, service, leakDetectorPolicyConfigInterface);
    }
}

inline void afterGetSubTreeLeakDetectorPolicy(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& leakDetectorId,
    const std::function<void(const std::string& leakDetectorConfigPath,
                             const std::string& service,
                             const std::string& configInterface)>& callback,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error on getSubTree {}", ec.value());
        messages::internalError(asyncResp->res);
        return;
    }

    if (subtree.empty())
    {
        BMCWEB_LOG_DEBUG("No leak detector config paths found in subtree");
        messages::internalError(asyncResp->res);
        return;
    }

    for (const auto& [path, serviceMap] : subtree)
    {
        if (serviceMap.empty())
        {
            BMCWEB_LOG_ERROR(
                "No services found for path {} in subtree response for "
                "leak detector policy",
                path);
            messages::internalError(asyncResp->res);
            return;
        }

        const std::string& service = serviceMap.front().first;

        // Query LeakDetectorName property
        sdbusplus::asio::getProperty<std::string>(
            *crow::connections::systemBus, service, path,
            leakDetectorPolicyConfigInterface, "LeakDetectorName",
            std::bind_front(afterGetLeakDetectorName, asyncResp, leakDetectorId,
                            callback, path, service));
    }
}

inline void getValidLeakDetectorPolicyPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& leakDetectorId,
    std::function<void(const std::string& leakDetectorConfigPath,
                       const std::string& service,
                       const std::string& configInterface)>&& callback)
{
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0,
        std::array<std::string_view, 1>{leakDetectorPolicyConfigInterface},
        [asyncResp, leakDetectorId, callback{std::move(callback)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
            afterGetSubTreeLeakDetectorPolicy(asyncResp, leakDetectorId,
                                              callback, ec, subtree);
        });
}

inline void addLeakDetectorCommonProperties(crow::Response& resp,
                                            const std::string& chassisId,
                                            const std::string& leakDetectorId)
{
    resp.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/LeakDetector/LeakDetector.json>; rel=describedby");
    resp.jsonValue["@odata.type"] = "#LeakDetector.v1_4_0.LeakDetector";
    resp.jsonValue["Id"] = leakDetectorId;
    resp.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/ThermalSubsystem/LeakDetection/LeakDetectors/{}",
        chassisId, leakDetectorId);
    resp.jsonValue["Status"]["State"] = "Enabled";
    resp.jsonValue["Status"]["Health"] = "OK";

    std::string leakDetectorName(leakDetectorId);
    std::replace(leakDetectorName.begin(), leakDetectorName.end(), '_', ' ');
    resp.jsonValue["Name"] = std::move(leakDetectorName);
}

inline void afterDetectorStatePropertyGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& propertiesList)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("DBUS response error for State {}", ec.value());
            messages::internalError(asyncResp->res);
        }
        return;
    }

    const std::string* detectorState = nullptr;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), propertiesList, "DetectorState",
        detectorState);

    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    if (detectorState != nullptr)
    {
        std::string mappedState = getDetectorState(*detectorState);
        asyncResp->res.jsonValue["DetectorState"] = mappedState;
        asyncResp->res.jsonValue["Status"]["Health"] = mappedState;
    }
}

inline void afterDetectorStatusPropertyGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& propertiesList)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("DBUS response error for State {}", ec.value());
            messages::internalError(asyncResp->res);
        }
        return;
    }

    const std::string* detectorOpStatus = nullptr;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), propertiesList, "State",
        detectorOpStatus);

    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    if (detectorOpStatus != nullptr)
    {
        std::string mappedStatus = getOperationalStatus(*detectorOpStatus);
        asyncResp->res.jsonValue["Status"]["State"] = mappedStatus;
    }
}

inline void getLeakDetectorState(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& leakDetectorPath, const std::string& service)
{
    dbus::utility::getAssociatedSubTreePaths(
        leakDetectorPath + "/leak_detecting",
        sdbusplus::message::object_path("/xyz/openbmc_project/state"), 0,
        leakDetectorStateInterfaces,
        [asyncResp, service](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreePathsResponse& subtreePaths) {
            if (ec)
            {
                if (ec.value() != EBADR)
                {
                    BMCWEB_LOG_ERROR(
                        "DBUS response error for getAssociatedSubTreePaths {}",
                        ec.value());
                    messages::internalError(asyncResp->res);
                }
                return;
            }

            if (subtreePaths.size() != 1)
            {
                BMCWEB_LOG_ERROR(
                    "Unexpected number of paths returned by getSubTree: {}",
                    subtreePaths.size());
                messages::internalError(asyncResp->res);
                return;
            }

            sdbusplus::asio::getAllProperties(
                *crow::connections::systemBus, service, subtreePaths.front(),
                leakDetectorStateInterface,
                [asyncResp](
                    const boost::system::error_code& ec1,
                    const dbus::utility::DBusPropertiesMap& propertiesList) {
                    afterDetectorStatePropertyGet(asyncResp, ec1,
                                                  propertiesList);
                });

            sdbusplus::asio::getAllProperties(
                *crow::connections::systemBus, service, subtreePaths.front(),
                leakDetectorOpStatusInterface,
                [asyncResp](
                    const boost::system::error_code& ec1,
                    const dbus::utility::DBusPropertiesMap& propertiesList) {
                    afterDetectorStatusPropertyGet(asyncResp, ec1,
                                                   propertiesList);
                });
        });
}

inline void getLeakDetectorItem(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& leakDetectorPath, const std::string& service)
{
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, leakDetectorPath,
        leakDetectorInventoryInterface,
        [asyncResp, leakDetectorPath](
            const boost::system::error_code& ec,
            const dbus::utility::DBusPropertiesMap& propertiesList) {
            if (ec.value() == EBADR)
            {
                messages::resourceNotFound(
                    asyncResp->res, "LeakDetector",
                    sdbusplus::message::object_path(leakDetectorPath)
                        .filename());
                return;
            }
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error for State {}",
                                 ec.value());
                messages::internalError(asyncResp->res);
                return;
            }

            const std::string* leakDetectorType = nullptr;

            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), propertiesList,
                "LeakDetectorType", leakDetectorType);

            if (!success)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            if (leakDetectorType != nullptr)
            {
                std::string mappedType = getLeakDetectorType(*leakDetectorType);
                asyncResp->res.jsonValue["LeakDetectorType"] = mappedType;
            }
        });
}

inline void afterLeakDetectorPolicyProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& propertiesList)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("DBUS response error for State {}", ec.value());
            messages::internalError(asyncResp->res);
        }
        return;
    }

    const std::string* criticalReactionType = nullptr;
    const std::string* warningReactionType = nullptr;
    const double* reactionDelaySeconds = nullptr;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), propertiesList,
        "CriticalReactionType", criticalReactionType, "WarningReactionType",
        warningReactionType, "ReactionDelaySeconds", reactionDelaySeconds);

    if (!success)
    {
        BMCWEB_LOG_DEBUG("DBUS response error: Unpacking Policy Properties.");
        messages::internalError(asyncResp->res);
        return;
    }

    if (criticalReactionType != nullptr)
    {
        leak_detector::ReactionType reactionType =
            translateReactionTypeString(*criticalReactionType);

        if (reactionType != leak_detector::ReactionType::Invalid)
        {
            asyncResp->res.jsonValue["CriticalReactionType"] = reactionType;
        }
        else
        {
            BMCWEB_LOG_WARNING("Critical reaction type value is invalid: {}",
                               *criticalReactionType);
        }
    }

    if (warningReactionType != nullptr)
    {
        leak_detector::ReactionType reactionType =
            translateReactionTypeString(*warningReactionType);

        if (reactionType != leak_detector::ReactionType::Invalid)
        {
            asyncResp->res.jsonValue["WarningReactionType"] = reactionType;
        }
        else
        {
            BMCWEB_LOG_WARNING("Warning reaction type value is invalid: {}",
                               *warningReactionType);
        }
    }

    if (reactionDelaySeconds != nullptr)
    {
        if (std::isfinite(*reactionDelaySeconds))
        {
            asyncResp->res.jsonValue["ReactionDelaySeconds"] =
                static_cast<int64_t>(*reactionDelaySeconds);
        }
        else
        {
            BMCWEB_LOG_WARNING("Reaction delay value is invalid");
        }
    }
}

inline void afterGetValidLeakDetectorPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& leakDetectorId,
    const std::string& leakDetectorPath, const std::string& service)
{
    addLeakDetectorCommonProperties(asyncResp->res, chassisId, leakDetectorId);
    getLeakDetectorState(asyncResp, leakDetectorPath, service);
    getLeakDetectorItem(asyncResp, leakDetectorPath, service);
}

inline void afterGetValidLeakDetectorPolicyPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& leakDetectorConfigPath, const std::string& service,
    const std::string& configInterface)
{
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, leakDetectorConfigPath,
        configInterface,
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::DBusPropertiesMap& propertiesList) {
            afterLeakDetectorPolicyProperties(asyncResp, ec, propertiesList);
        });
}

inline void doLeakDetectorGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& leakDetectorId,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    getValidLeakDetectorPath(
        asyncResp, leakDetectorId,
        std::bind_front(afterGetValidLeakDetectorPath, asyncResp, chassisId,
                        leakDetectorId));

    getValidLeakDetectorPolicyPath(
        asyncResp, leakDetectorId,
        std::bind_front(afterGetValidLeakDetectorPolicyPath, asyncResp));
}

inline void handleLeakDetectorGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& leakDetectorId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(doLeakDetectorGet, asyncResp, chassisId,
                        leakDetectorId));
}

inline void handleLeakDetectorHead(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& leakDetectorId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        [asyncResp, chassisId,
         leakDetectorId](const std::optional<std::string>& validChassisPath) {
            if (!validChassisPath)
            {
                messages::resourceNotFound(asyncResp->res, "Chassis",
                                           chassisId);
                return;
            }
            getValidLeakDetectorPath(
                asyncResp, leakDetectorId,
                [asyncResp](const std::string&, const std::string&) {
                    asyncResp->res.addHeader(
                        boost::beast::http::field::link,
                        "</redfish/v1/JsonSchemas/LeakDetector/LeakDetector.json>; rel=describedby");
                });
        });
}

inline void doLeakDetectorCollection(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
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
        "</redfish/v1/JsonSchemas/LeakDetectorCollection/LeakDetectorCollection.json>; rel=describedby");
    asyncResp->res.jsonValue["@odata.type"] =
        "#LeakDetectorCollection.LeakDetectorCollection";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/ThermalSubsystem/LeakDetection/LeakDetectors",
        chassisId);
    asyncResp->res.jsonValue["Name"] = "Leak Detector Collection";
    asyncResp->res.jsonValue["Description"] =
        "Collection of Leak Detectors for Chassis " + chassisId;

    boost::urls::url collectionUrl = boost::urls::format(
        "/redfish/v1/Chassis/{}/ThermalSubsystem/LeakDetection/LeakDetectors",
        chassisId);
    collection_util::getCollectionMembersByAssociation(
        asyncResp, std::string(collectionUrl.data(), collectionUrl.size()),
        *validChassisPath + "/contained_by", {leakDetectorInventoryInterface});
}

inline void handleLeakDetectorCollectionGet(
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
        std::bind_front(doLeakDetectorCollection, asyncResp, chassisId));
}

inline void handleLeakDetectorCollectionHead(
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
                "</redfish/v1/JsonSchemas/LeakDetectorCollection/LeakDetectorCollection.json>; rel=describedby");
        });
}

inline void doLeakDetectorPolicyPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const LeakDetectorPolicyProperties& policyProperties,
    const std::string& leakDetectorConfigPath, const std::string& service,
    const std::string& configInterface)
{
    sdbusplus::message::object_path path(leakDetectorConfigPath);

    if (policyProperties.criticalReactionType)
    {
        setDbusProperty(asyncResp, "CriticalReactionType", service, path,
                        configInterface, "CriticalReactionType",
                        *policyProperties.criticalReactionType);
    }

    if (policyProperties.warningReactionType)
    {
        setDbusProperty(asyncResp, "WarningReactionType", service, path,
                        configInterface, "WarningReactionType",
                        *policyProperties.warningReactionType);
    }

    if (policyProperties.reactionDelaySeconds)
    {
        setDbusProperty(asyncResp, "ReactionDelaySeconds", service, path,
                        configInterface, "ReactionDelaySeconds",
                        *policyProperties.reactionDelaySeconds);
    }
}

inline void handleLeakDetectorPatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& /*chassisId*/, const std::string& leakDetectorId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    LeakDetectorPolicyProperties policyProperties;

    if (!json_util::readJsonPatch(
            req, asyncResp->res, "CriticalReactionType",
            policyProperties.criticalReactionType, "WarningReactionType",
            policyProperties.warningReactionType, "ReactionDelaySeconds",
            policyProperties.reactionDelaySeconds))
    {
        return;
    }

    // Validate criticalReactionType if provided
    if (policyProperties.criticalReactionType)
    {
        leak_detector::ReactionType reactionType =
            translateReactionTypeString(*policyProperties.criticalReactionType);

        if (reactionType == leak_detector::ReactionType::Invalid)
        {
            messages::propertyValueNotInList(
                asyncResp->res, *policyProperties.criticalReactionType,
                "CriticalReactionType");
            return;
        }
    }

    // Validate warningReactionType if provided
    if (policyProperties.warningReactionType)
    {
        leak_detector::ReactionType reactionType =
            translateReactionTypeString(*policyProperties.warningReactionType);

        if (reactionType == leak_detector::ReactionType::Invalid)
        {
            messages::propertyValueNotInList(
                asyncResp->res, *policyProperties.warningReactionType,
                "WarningReactionType");
            return;
        }
    }

    // Only call getValidLeakDetectorPolicyPath if there's something to patch
    if (policyProperties.criticalReactionType ||
        policyProperties.warningReactionType ||
        policyProperties.reactionDelaySeconds)
    {
        getValidLeakDetectorPolicyPath(
            asyncResp, leakDetectorId,
            std::bind_front(doLeakDetectorPolicyPatch, asyncResp,
                            std::move(policyProperties)));
    }
}

inline void requestRoutesLeakDetector(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/ThermalSubsystem/LeakDetection/LeakDetectors/")
        .privileges(redfish::privileges::headLeakDetectorCollection)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleLeakDetectorCollectionHead, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/ThermalSubsystem/LeakDetection/LeakDetectors/")
        .privileges(redfish::privileges::getLeakDetectorCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleLeakDetectorCollectionGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/ThermalSubsystem/LeakDetection/LeakDetectors/<str>/")
        .privileges(redfish::privileges::headLeakDetector)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleLeakDetectorHead, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/ThermalSubsystem/LeakDetection/LeakDetectors/<str>/")
        .privileges(redfish::privileges::getLeakDetector)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleLeakDetectorGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/ThermalSubsystem/LeakDetection/LeakDetectors/<str>/")
        .privileges(redfish::privileges::patchLeakDetector)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleLeakDetectorPatch, std::ref(app)));
}

} // namespace redfish
