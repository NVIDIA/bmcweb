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

#include <app.hpp>
#include <boost/url/format.hpp>
#include <nvidia_messages.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <sdbusplus/message.hpp>
#include <utils/chassis_utils.hpp>

#include <format>
#include <string>

namespace redfish
{
// DOT Action Interface
constexpr const std::string_view dotActionIntf = "com.nvidia.Dot.Action";

/**
 * @brief Sets up ActionInfo JSON response for DOT operations
 *
 * Populates the response with ActionInfo metadata including OData type,
 * OData ID, resource ID, and resource name for a specific DOT action.
 *
 * @param asyncResp Async response object to populate with ActionInfo
 * @param chassisId The chassis identifier
 * @param componentId The trusted component identifier
 * @param actionName Name of the DOT action (e.g., "Install", "Lock")
 */
inline void setupActionInfo(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisId,
                            const std::string& componentId,
                            const std::string& actionName)
{
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_2_0.ActionInfo";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/{}ActionInfo",
        chassisId, componentId, actionName);
    asyncResp->res.jsonValue["Id"] = std::format("{}ActionInfo", actionName);
    asyncResp->res.jsonValue["Name"] =
        std::format("{} Oem Nvidia DOT {} ActionInfo", componentId, actionName);
}

/**
 * @brief Find matching D-Bus object for a component ID
 *
 * Searches through discovered D-Bus objects and finds one that matches
 * the component ID pattern, handling both prefixed and non-prefixed names.
 *
 * @param componentId The component ID (e.g., "IRoT_CPU_0")
 * @param resp The D-Bus discovery response containing object paths
 * @return Optional pair of (path, serviceMap) if found, nullopt otherwise
 */
inline std::optional<std::pair<
    std::string, std::vector<std::pair<std::string, std::vector<std::string>>>>>
    findMatchingDOTComponent(
        const std::string& componentId,
        const dbus::utility::MapperGetSubTreeResponse& resp)
{
    constexpr std::string_view platformPrefix(PLATFORMDEVICEPREFIX);

    for (const auto& [path, serviceMap] : resp)
    {
        sdbusplus::message::object_path objPath(path);
        std::string dbusComponentName = objPath.filename();

        if (dbusComponentName == componentId)
        {
            return std::make_pair(path, serviceMap);
        }

        std::string componentWithPrefix =
            std::string(platformPrefix) + componentId;
        if (dbusComponentName == componentWithPrefix)
        {
            return std::make_pair(path, serviceMap);
        }

        if (dbusComponentName.starts_with(platformPrefix))
        {
            std::string dbusWithoutPrefix =
                dbusComponentName.substr(platformPrefix.size());
            if (dbusWithoutPrefix == componentId)
            {
                return std::make_pair(path, serviceMap);
            }
        }
    }

    return std::nullopt;
}

/**
 * @brief Validates DOT component after D-Bus discovery
 *
 * Callback invoked after D-Bus subtree discovery to validate that the
 * requested component supports DOT operations. If valid, invokes the
 * success callback.
 *
 * @param asyncResp Async response object for error reporting
 * @param chassisId The chassis identifier
 * @param componentId The trusted component identifier
 * @param onSuccess Callback to invoke if component validation succeeds
 * @param ec Error code from D-Bus discovery operation
 * @param subtree D-Bus subtree response containing discovered objects
 */
inline void afterDOTComponentValidation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId,
    const std::function<void()>& onSuccess, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("No DOT support for component: {} in chassis: {}",
                         componentId, chassisId);
        messages::resourceNotFound(asyncResp->res, "DOT", componentId);
        return;
    }

    auto matchResult = findMatchingDOTComponent(componentId, subtree);
    if (!matchResult)
    {
        BMCWEB_LOG_DEBUG("DOT not available for component: {} in chassis: {}",
                         componentId, chassisId);
        messages::resourceNotFound(asyncResp->res, "DOT", componentId);
        return;
    }

    BMCWEB_LOG_DEBUG("Found DOT support for component: {} at path: {}",
                     componentId, matchResult->first);
    onSuccess();
}

/**
 * @brief Validates chassis and initiates DOT component discovery
 *
 * Callback invoked after chassis validation. If chassis is valid, initiates
 * D-Bus discovery to find DOT-capable components.
 *
 * @param asyncResp Async response object for error reporting
 * @param chassisId The chassis identifier
 * @param componentId The trusted component identifier
 * @param onSuccess Callback to invoke if DOT component is found and validated
 * @param validChassisPath Optional path to validated chassis (nullopt if
 * invalid)
 */
inline void afterChassisValidation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId,
    std::function<void()> onSuccess,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_DEBUG("DOT validation: Invalid chassis: {}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    constexpr std::array<std::string_view, 1> interfaces = {dotActionIntf};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project", 0, interfaces,
        std::bind_front(afterDOTComponentValidation, asyncResp, chassisId,
                        componentId, std::move(onSuccess)));
}

/**
 * @brief Validates chassis and DOT component existence
 *
 * Initiates validation chain to ensure the specified chassis exists and
 * the component supports DOT operations. Invokes success callback if both
 * validations pass.
 *
 * @param asyncResp Async response object for error reporting
 * @param chassisId The chassis identifier to validate
 * @param componentId The trusted component identifier to validate
 * @param onSuccess Callback to invoke if validations succeed
 */
inline void validateChassisAndDOTComponent(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId,
    std::function<void()> onSuccess)
{
    if (componentId.empty())
    {
        BMCWEB_LOG_ERROR("DOT validation: componentId is empty");
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentId);
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(afterChassisValidation, asyncResp, chassisId,
                        componentId, std::move(onSuccess)));
}

/**
 * @brief Handles DOT ActionInfo request
 *
 * Validates chassis and component, then populates the response with
 * ActionInfo metadata including parameter definitions for the specified
 * DOT action.
 *
 * @param asyncResp Async response object to populate with ActionInfo
 * @param chassisId The chassis identifier
 * @param componentId The trusted component identifier
 * @param actionName Name of the DOT action
 * @param parameters Array of parameter definitions for the action
 */
inline void handleDOTActionInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId,
    const std::string& actionName, nlohmann::json::array_t&& parameters)
{
    validateChassisAndDOTComponent(
        asyncResp, chassisId, componentId,
        [asyncResp, chassisId, componentId, actionName,
         parameters = std::move(parameters)]() mutable {
            setupActionInfo(asyncResp, chassisId, componentId, actionName);
            asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
        });
}

/**
 * @brief Processes DOT resource discovery results
 *
 * Callback invoked after D-Bus discovery for DOT resources. Validates that
 * the component exists and supports DOT, then populates the response with
 * complete DOT resource information including all available actions.
 *
 * @param asyncResp Async response object to populate with DOT resource data
 * @param chassisId The chassis identifier
 * @param componentId The trusted component identifier
 * @param ec Error code from D-Bus discovery operation
 * @param subtree D-Bus subtree response containing discovered DOT objects
 */
inline void afterDOTResourceDiscovery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG(
            "No DOT support found for component: {} in chassis: {}",
            componentId, chassisId);
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentId);
        return;
    }

    auto matchResult = findMatchingDOTComponent(componentId, subtree);
    if (!matchResult)
    {
        BMCWEB_LOG_DEBUG("DOT not available for component: {} in chassis: {}",
                         componentId, chassisId);
        messages::resourceNotFound(asyncResp->res, "DOT", componentId);
        return;
    }

    const auto& [path, serviceMap] = *matchResult;
    BMCWEB_LOG_DEBUG("Found DOT support for component: {} at path: {}",
                     componentId, path);

    asyncResp->res.jsonValue["@odata.type"] = "#NvidiaDOT.v1_0_0.NvidiaDOT";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT", chassisId,
        componentId);
    asyncResp->res.jsonValue["Id"] = "DOT";
    asyncResp->res.jsonValue["Name"] = std::format(
        "{} TrustedComponents {} Oem Nvidia DOT", chassisId, componentId);

    asyncResp->res.jsonValue["DOTState"] = "Uninitialized";

    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.Install"]
                            ["@Redfish.ActionInfo"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/InstallActionInfo",
        chassisId, componentId);
    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.Install"]
                            ["target"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/Actions/NvidiaDOT.Install",
        chassisId, componentId);

    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.Lock"]
                            ["@Redfish.ActionInfo"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/LockActionInfo",
        chassisId, componentId);
    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.Lock"]
                            ["target"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/Actions/NvidiaDOT.Lock",
        chassisId, componentId);

    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.Disable"]
                            ["@Redfish.ActionInfo"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/DisableActionInfo",
        chassisId, componentId);
    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.Disable"]
                            ["target"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/Actions/NvidiaDOT.Disable",
        chassisId, componentId);

    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.Unlock"]
                            ["@Redfish.ActionInfo"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/UnlockActionInfo",
        chassisId, componentId);
    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.Unlock"]
                            ["target"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/Actions/NvidiaDOT.Unlock",
        chassisId, componentId);

    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.CAKRotate"]
                            ["@Redfish.ActionInfo"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/CAKRotateActionInfo",
        chassisId, componentId);
    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.CAKRotate"]
                            ["target"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/Actions/NvidiaDOT.CAKRotate",
        chassisId, componentId);

    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.Override"]
                            ["@Redfish.ActionInfo"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/OverrideActionInfo",
        chassisId, componentId);
    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.Override"]
                            ["target"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/Actions/NvidiaDOT.Override",
        chassisId, componentId);

    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.UnlockChallenge"]
                            ["@Redfish.ActionInfo"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/UnlockChallengeActionInfo",
        chassisId, componentId);
    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.UnlockChallenge"]
                            ["target"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/Actions/NvidiaDOT.UnlockChallenge",
        chassisId, componentId);

    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.Recovery"]
                            ["@Redfish.ActionInfo"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/RecoveryActionInfo",
        chassisId, componentId);
    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.Recovery"]
                            ["target"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/Actions/NvidiaDOT.Recovery",
        chassisId, componentId);

    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.GetInfo"]
                            ["target"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/Actions/NvidiaDOT.GetInfo",
        chassisId, componentId);

    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.CAKBypass"]
                            ["target"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/Actions/NvidiaDOT.CAKBypass",
        chassisId, componentId);
}

/**
 * @brief Validates chassis and initiates DOT resource discovery
 *
 * Callback invoked after chassis validation. If chassis is valid, initiates
 * D-Bus subtree discovery for DOT operations and invokes the provided callback
 * with the discovery results.
 *
 * @param asyncResp Async response object for error reporting
 * @param chassisId The chassis identifier
 * @param componentId The trusted component identifier (unused in this function)
 * @param callback Callback to invoke with D-Bus discovery results
 * @param validChassisPath Optional path to validated chassis (nullopt if
 * invalid)
 */
inline void afterChassisValidationWithDOTDiscovery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId,
    [[maybe_unused]] const std::string& componentId,
    std::function<void(const boost::system::error_code&,
                       const dbus::utility::MapperGetSubTreeResponse&)>
        callback,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("DOT: Invalid chassis path: {}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    constexpr std::array<std::string_view, 1> interfaces = {dotActionIntf};

    dbus::utility::getSubTree("/xyz/openbmc_project", 0, interfaces,
                              std::move(callback));
}

/**
 * @brief Handles GET request for Nvidia DOT resource
 *
 * Entry point for retrieving DOT (Device Owner Transfer) resource information
 * for a specific trusted component. Validates the request, chassis, and
 * component before returning the complete DOT resource representation.
 *
 * @param app Crow application reference
 * @param req HTTP request object
 * @param asyncResp Async response object to populate with DOT resource data
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleNvidiaOemTrustedComponentsDOT(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    BMCWEB_LOG_DEBUG("DOT GET resource called - Chassis: '{}', Component: '{}'",
                     chassisId, componentId);

    if (componentId.empty())
    {
        BMCWEB_LOG_ERROR("DOT GET: componentId is empty");
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentId);
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(afterChassisValidationWithDOTDiscovery, asyncResp,
                        chassisId, componentId,
                        std::bind_front(afterDOTResourceDiscovery, asyncResp,
                                        chassisId, componentId)));
}

/**
 * @brief Handles GET request for DOT Install ActionInfo
 *
 * Returns ActionInfo resource describing the parameters required for the
 * DOT Install action, including CAK key, optional LAK key, lock disable flag,
 * and minimum security version.
 *
 * @param app Crow application reference
 * @param req HTTP request object
 * @param asyncResp Async response object to populate with ActionInfo data
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleDOTInstallActionInfo(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    nlohmann::json::array_t parameters;
    parameters.push_back({{"DataType", "Object"},
                          {"ObjectDataType", "#NvidiaDOT.v1_0_0.KeyStructure"},
                          {"Name", "CAKKey"},
                          {"Required", true}});
    parameters.push_back({{"DataType", "Object"},
                          {"ObjectDataType", "#NvidiaDOT.v1_0_0.KeyStructure"},
                          {"Name", "LAKKey"},
                          {"Required", false}});
    parameters.push_back({{"DataType", "Boolean"},
                          {"Name", "LockDisable"},
                          {"Required", false}});
    parameters.push_back({{"DataType", "Number"},
                          {"Name", "MinimumSecurityVersion"},
                          {"Required", false}});

    handleDOTActionInfo(asyncResp, chassisId, componentId, "Install",
                        std::move(parameters));
}

/**
 * @brief Handles GET request for DOT Lock ActionInfo
 *
 * Returns ActionInfo resource describing the parameters required for the
 * DOT Lock action, including CAK key, LAK key, nonce type, optional static
 * challenge, and LAK signature.
 *
 * @param app Crow application reference
 * @param req HTTP request object
 * @param asyncResp Async response object to populate with ActionInfo data
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleDOTLockActionInfo(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    nlohmann::json::array_t parameters;
    parameters.push_back({{"DataType", "Object"},
                          {"ObjectDataType", "#NvidiaDOT.v1_0_0.KeyStructure"},
                          {"Name", "CAKKey"},
                          {"Required", true}});
    parameters.push_back({{"DataType", "Object"},
                          {"ObjectDataType", "#NvidiaDOT.v1_0_0.KeyStructure"},
                          {"Name", "LAKKey"},
                          {"Required", true}});
    parameters.push_back(
        {{"AllowableValues",
          nlohmann::json::array_t{"DeviceUniqueIdentifier", "RandomNonce",
                                  "StaticValue"}},
         {"Name", "NonceType"},
         {"Required", true}});
    parameters.push_back({{"DataType", "String"},
                          {"Name", "StaticChallenge"},
                          {"Required", false}});
    parameters.push_back(
        {{"DataType", "Object"},
         {"ObjectDataType", "#NvidiaDOT.v1_0_0.SignatureStructure"},
         {"Name", "LAKSignature"},
         {"Required", true}});

    handleDOTActionInfo(asyncResp, chassisId, componentId, "Lock",
                        std::move(parameters));
}

/**
 * @brief Handles GET request for DOT Disable ActionInfo
 *
 * Returns ActionInfo resource describing the parameters required for the
 * DOT Disable action, including LAK key, nonce type, optional static
 * challenge, and LAK signature.
 *
 * @param app Crow application reference
 * @param req HTTP request object
 * @param asyncResp Async response object to populate with ActionInfo data
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleDOTDisableActionInfo(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    nlohmann::json::array_t parameters;
    parameters.push_back({{"DataType", "Object"},
                          {"ObjectDataType", "#NvidiaDOT.v1_0_0.KeyStructure"},
                          {"Name", "LAKKey"},
                          {"Required", true}});
    parameters.push_back(
        {{"AllowableValues",
          nlohmann::json::array_t{"DeviceUniqueIdentifier", "RandomNonce",
                                  "StaticValue"}},
         {"Name", "NonceType"},
         {"Required", true}});
    parameters.push_back({{"DataType", "String"},
                          {"Name", "StaticChallenge"},
                          {"Required", false}});
    parameters.push_back(
        {{"DataType", "Object"},
         {"ObjectDataType", "#NvidiaDOT.v1_0_0.SignatureStructure"},
         {"Name", "LAKSignature"},
         {"Required", true}});

    handleDOTActionInfo(asyncResp, chassisId, componentId, "Disable",
                        std::move(parameters));
}

/**
 * @brief Handles GET request for DOT Unlock ActionInfo
 *
 * Returns ActionInfo resource describing the parameters required for the
 * DOT Unlock action, including LAK key and LAK signature.
 *
 * @param app Crow application reference
 * @param req HTTP request object
 * @param asyncResp Async response object to populate with ActionInfo data
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleDOTUnlockActionInfo(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    nlohmann::json::array_t parameters;
    parameters.push_back({{"DataType", "Object"},
                          {"ObjectDataType", "#NvidiaDOT.v1_0_0.KeyStructure"},
                          {"Name", "LAKKey"},
                          {"Required", true}});
    parameters.push_back(
        {{"DataType", "Object"},
         {"ObjectDataType", "#NvidiaDOT.v1_0_0.SignatureStructure"},
         {"Name", "LAKSignature"},
         {"Required", true}});

    handleDOTActionInfo(asyncResp, chassisId, componentId, "Unlock",
                        std::move(parameters));
}

/**
 * @brief Handles GET request for DOT CAKRotate ActionInfo
 *
 * Returns ActionInfo resource describing the parameters required for the
 * DOT CAKRotate action, including new CAK key and LAK signature.
 *
 * @param app Crow application reference
 * @param req HTTP request object
 * @param asyncResp Async response object to populate with ActionInfo data
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleDOTCAKRotateActionInfo(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    nlohmann::json::array_t parameters;
    parameters.push_back({{"DataType", "Object"},
                          {"ObjectDataType", "#NvidiaDOT.v1_0_0.KeyStructure"},
                          {"Name", "NewCAKKey"},
                          {"Required", true}});
    parameters.push_back(
        {{"DataType", "Object"},
         {"ObjectDataType", "#NvidiaDOT.v1_0_0.SignatureStructure"},
         {"Name", "LAKSignature"},
         {"Required", true}});

    handleDOTActionInfo(asyncResp, chassisId, componentId, "CAKRotate",
                        std::move(parameters));
}

/**
 * @brief Handles GET request for DOT Override ActionInfo
 *
 * Returns ActionInfo resource describing the parameters required for the
 * DOT Override action, including vendor signature.
 *
 * @param app Crow application reference
 * @param req HTTP request object
 * @param asyncResp Async response object to populate with ActionInfo data
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleDOTOverrideActionInfo(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    nlohmann::json::array_t parameters;
    parameters.push_back(
        {{"DataType", "Object"},
         {"ObjectDataType", "#NvidiaDOT.v1_0_0.SignatureStructure"},
         {"Name", "VendorSignature"},
         {"Required", true}});

    handleDOTActionInfo(asyncResp, chassisId, componentId, "Override",
                        std::move(parameters));
}

/**
 * @brief Handles GET request for DOT UnlockChallenge ActionInfo
 *
 * Returns ActionInfo resource describing the parameters required for the
 * DOT UnlockChallenge action, including unlock type (OwnerUnlock or
 * VendorUnlock).
 *
 * @param app Crow application reference
 * @param req HTTP request object
 * @param asyncResp Async response object to populate with ActionInfo data
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleDOTUnlockChallengeActionInfo(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    nlohmann::json::array_t parameters;
    parameters.push_back(
        {{"AllowableValues",
          nlohmann::json::array_t{"OwnerUnlock", "VendorUnlock"}},
         {"Name", "UnlockType"},
         {"Required", true}});

    handleDOTActionInfo(asyncResp, chassisId, componentId, "UnlockChallenge",
                        std::move(parameters));
}

/**
 * @brief Handles GET request for DOT Recovery ActionInfo
 *
 * Returns ActionInfo resource describing the parameters required for the
 * DOT Recovery action, including DOT data string.
 *
 * @param app Crow application reference
 * @param req HTTP request object
 * @param asyncResp Async response object to populate with ActionInfo data
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleDOTRecoveryActionInfo(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    nlohmann::json::array_t parameters;
    parameters.push_back(
        {{"DataType", "String"}, {"Name", "DOTData"}, {"Required", true}});

    handleDOTActionInfo(asyncResp, chassisId, componentId, "Recovery",
                        std::move(parameters));
}

/**
 * @brief Registers Redfish routes for Nvidia OEM Trusted Components DOT
 * endpoints
 *
 * Configures all HTTP routes for DOT (Device Owner Transfer) operations and
 * ActionInfo resources under the Nvidia OEM extension for TrustedComponents.
 *
 * @param app Crow application to register routes with
 */
inline void requestRoutesNvidiaOemDOT(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT")
        .privileges(redfish::privileges::getChassis)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleNvidiaOemTrustedComponentsDOT, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/InstallActionInfo")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleDOTInstallActionInfo, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/LockActionInfo")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleDOTLockActionInfo, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/DisableActionInfo")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleDOTDisableActionInfo, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/UnlockActionInfo")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleDOTUnlockActionInfo, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/CAKRotateActionInfo")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleDOTCAKRotateActionInfo, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/OverrideActionInfo")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleDOTOverrideActionInfo, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/UnlockChallengeActionInfo")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleDOTUnlockChallengeActionInfo, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/RecoveryActionInfo")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleDOTRecoveryActionInfo, std::ref(app)));
}

} // namespace redfish
