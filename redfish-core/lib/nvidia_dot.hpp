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
#include <dot/base.hpp>
#include <dot/dot_dbus_utils.hpp>
#include <dot/dot_utils.hpp>
#include <nvidia_messages.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <sdbusplus/message.hpp>
#include <utils/chassis_utils.hpp>
#include <utils/nvidia_async_call_utils.hpp>

#include <format>
#include <functional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

namespace redfish
{
using DOTErrorType = std::tuple<uint16_t, std::string>;
using DOTResultType = std::variant<std::monostate, DOTErrorType>;

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

    auto matchResult =
        dot_utils::findMatchingDOTComponent(componentId, subtree);
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

    constexpr std::array<std::string_view, 1> interfaces = {dot::dotActionIntf};

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

    auto matchResult =
        dot_utils::findMatchingDOTComponent(componentId, subtree);
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

    constexpr std::array<std::string_view, 1> interfaces = {dot::dotActionIntf};

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
 * @brief Parse and validate key structure from JSON request
 *
 * Extracts and validates authentication scheme, ECDSA key, and LMS key from
 * a JSON key structure object. Validates authentication scheme values and
 * ensures required fields are present.
 *
 * @param keyJsonOpt Optional JSON object containing key structure
 * @param keyName Name of the key (for error messages)
 * @param res Response object for error reporting
 * @param authScheme Output: Authentication scheme (Ecdsa or Hybrid)
 * @param ecdsaKey Output: ECDSA key data
 * @param lmsKey Output: LMS key data (required for Hybrid scheme)
 * @param isRequired Whether the key structure is required
 * @param defaultAuthScheme Default authentication scheme if not provided
 * @return true if parsing succeeded, false otherwise
 */
inline bool parseKeyStructure(const std::optional<nlohmann::json>& keyJsonOpt,
                              const std::string& keyName, crow::Response& res,
                              std::string& authScheme, std::string& ecdsaKey,
                              std::string& lmsKey, bool isRequired = true,
                              const std::string& defaultAuthScheme = "Ecdsa")
{
    if (!keyJsonOpt)
    {
        if (isRequired)
        {
            messages::actionParameterMissing(res, "Action", keyName);
            return false;
        }
        authScheme = defaultAuthScheme;
        ecdsaKey = "";
        lmsKey = "";
        return true;
    }

    const nlohmann::json& keyJson = *keyJsonOpt;

    if (!keyJson.is_object())
    {
        messages::actionParameterValueTypeError(res, keyJson.dump(), "Action",
                                                keyName);
        return false;
    }

    nlohmann::json keyJsonCopy = keyJson;

    std::optional<std::string> lmsKeyOpt;
    if (!redfish::json_util::readJson(keyJsonCopy, res, "AuthenticationScheme",
                                      authScheme, "ECDSAKey", ecdsaKey,
                                      "LMSKey", lmsKeyOpt))
    {
        return false;
    }

    lmsKey = lmsKeyOpt.value_or("");
    if (authScheme == "ECDSA" || authScheme == "ecdsa")
    {
        authScheme = "Ecdsa";
    }

    if (authScheme != "Ecdsa" && authScheme != "Hybrid")
    {
        messages::actionParameterValueNotInList(
            res, "Action", "AuthenticationScheme", authScheme);
        return false;
    }

    if (authScheme == "Hybrid" && lmsKey.empty())
    {
        messages::actionParameterMissing(res, "Action", keyName + "/LMSKey");
        return false;
    }
    return true;
}

/**
 * @brief Handle DOT operation error result
 *
 * Generic error handler that processes DOT async operation error results,
 * mapping operation status values to appropriate HTTP error responses.
 * Extracts error information from the variant and handles InvalidArgument,
 * Unavailable, UnsupportedRequest, and generic error cases with appropriate
 * error messages.
 *
 * @param asyncResp Async response object to populate with error result
 * @param status Operation status string from async operation
 * @param resultPtr Pointer to DOTResultType variant (may be nullptr)
 * @param actionName Name of the DOT action (e.g., "Install", "CAKBypass")
 */
inline void handleDOTErrorResult(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& status, const DOTResultType* resultPtr,
    const std::string& actionName)
{
    std::string errorMsg;
    if (resultPtr != nullptr)
    {
        if (const auto* errPtr = std::get_if<DOTErrorType>(resultPtr))
        {
            const auto& [errorCode, errorMessage] = *errPtr;
            errorMsg = errorMessage;
        }
        else
        {
            BMCWEB_LOG_ERROR(
                "DOT {} failed with status {} but error data format is unexpected",
                actionName, status);
            messages::internalError(asyncResp->res);
            return;
        }
    }
    if (status == nvidia_async_operation_utils::asyncStatusValueInvalidArgument)
    {
        BMCWEB_LOG_ERROR("DOT {} invalid argument: {}", actionName, errorMsg);
        messages::actionParameterValueError(
            asyncResp->res, nlohmann::json(errorMsg), actionName);
        return;
    }
    if (status == nvidia_async_operation_utils::asyncStatusValueUnavailable)
    {
        BMCWEB_LOG_ERROR("DOT {} service unavailable: {}", actionName,
                         errorMsg);
        messages::serviceTemporarilyUnavailable(asyncResp->res, "60");
        return;
    }
    if (status ==
        nvidia_async_operation_utils::asyncStatusValueUnsupportedRequest)
    {
        BMCWEB_LOG_ERROR("DOT {} unsupported by device: {}", actionName,
                         errorMsg);
        messages::actionNotSupported(asyncResp->res, actionName);
        return;
    }
    if (errorMsg.empty())
    {
        BMCWEB_LOG_ERROR("DOT {} failed with status {} but no error data",
                         actionName, status);
        messages::internalError(asyncResp->res);
        return;
    }
    BMCWEB_LOG_ERROR("DOT {} failed: {}", actionName, errorMsg);
    messages::dotActionResponseError(asyncResp->res, errorMsg);
}

/**
 * @brief Process DOT service discovery for Install operation
 *
 * Callback invoked after DBus discovery for DOT services. Validates that the
 * component exists and supports DOT operations, then initiates the CAK Install
 * operation via D-Bus with the provided key data.
 *
 * @param asyncResp Async response object for error reporting
 * @param componentId The trusted component identifier
 * @param cakAuthScheme CAK authentication scheme (Ecdsa or Hybrid)
 * @param cakEcdsaKey CAK ECDSA key data
 * @param cakLmsKey CAK LMS key data
 * @param lakAuthScheme LAK authentication scheme (Ecdsa or Hybrid)
 * @param lakEcdsaKey LAK ECDSA key data
 * @param lakLmsKey LAK LMS key data
 * @param lockDisable Lock disable flag
 * @param minSvn Minimum security version number
 * @param ec Error code from DBus discovery operation
 * @param resp DBus subtree response containing discovered DOT objects
 */
inline void afterDOTCAKInstallServiceDiscovery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& componentId, const std::string& cakAuthScheme,
    const std::string& cakEcdsaKey, const std::string& cakLmsKey,
    const std::string& lakAuthScheme, const std::string& lakEcdsaKey,
    const std::string& lakLmsKey, bool lockDisable, uint32_t minSvn,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& resp)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("GetSubTree error: {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }

    auto matchResult = dot_utils::findMatchingDOTComponent(componentId, resp);
    if (!matchResult)
    {
        BMCWEB_LOG_ERROR("No matching DOT component found for: {}",
                         componentId);
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentId);
        return;
    }

    const auto& [path, serviceMap] = *matchResult;
    BMCWEB_LOG_DEBUG("Found matching DOT component: {} for componentId: {}",
                     path, componentId);

    if (serviceMap.empty())
    {
        BMCWEB_LOG_ERROR("No service for DOT path: {}", path);
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentId);
        return;
    }

    const std::string& dotService = serviceMap[0].first;
    BMCWEB_LOG_DEBUG("Found DOT service: {} at path: {}", dotService, path);

    std::string cakAuthEnum =
        dot_utils::convertAuthSchemeToDbusEnum(cakAuthScheme);
    std::string lakAuthEnum =
        dot_utils::convertAuthSchemeToDbusEnum(lakAuthScheme);

    if (cakAuthEnum.empty() || lakAuthEnum.empty())
    {
        BMCWEB_LOG_ERROR("DOT Install: Invalid authentication scheme");
        messages::actionParameterValueError(
            asyncResp->res, nlohmann::json("Invalid authentication scheme"),
            "Install");
        return;
    }

    nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<
        DOTResultType>(
        asyncResp, std::chrono::seconds(60), dotService, path,
        std::string(dot::dotActionIntf), "DotCAKInstall",
        [asyncResp](const std::string& status, const DOTResultType* resultPtr) {
            if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
            {
                BMCWEB_LOG_DEBUG("DOT Install succeeded");
                messages::success(asyncResp->res);
                return;
            }

            handleDOTErrorResult(asyncResp, status, resultPtr, "Install");
        },
        cakAuthEnum, cakEcdsaKey, cakLmsKey, lakAuthEnum, lakEcdsaKey,
        lakLmsKey, lockDisable, minSvn);
}

/**
 * @brief Process chassis validation and parse Install action parameters
 *
 * Callback invoked after chassis validation for DOT Install action. Parses
 * and validates the request JSON body including CAK key, optional LAK key,
 * lock disable flag, and minimum security version, then initiates DOT
 * service discovery.
 *
 * @param asyncResp Async response object for error reporting
 * @param req HTTP request containing action parameters
 * @param chassisId The chassis identifier
 * @param componentId The trusted component identifier
 * @param validChassisPath Optional path to validated chassis (nullopt if
 * invalid)
 */
inline void afterChassisValidationForDOTInstall(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const crow::Request& req, const std::string& chassisId,
    const std::string& componentId,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("DOT Install: Invalid chassis: {}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    std::optional<nlohmann::json> cakKeyOpt;
    std::optional<bool> lockDisableOpt;
    std::optional<nlohmann::json> lakKeyOpt;
    std::optional<int64_t> minSecurityVersionOpt;

    if (!json_util::readJsonAction(
            req, asyncResp->res, "CAKKey", cakKeyOpt, "LockDisable",
            lockDisableOpt, "LAKKey", lakKeyOpt, "MinimumSecurityVersion",
            minSecurityVersionOpt))
    {
        return;
    }

    std::string cakAuthScheme;
    std::string cakEcdsaKey;
    std::string cakLmsKey;
    if (!parseKeyStructure(cakKeyOpt, "CAKKey", asyncResp->res, cakAuthScheme,
                           cakEcdsaKey, cakLmsKey, true))
    {
        return;
    }

    std::string lakAuthScheme;
    std::string lakEcdsaKey;
    std::string lakLmsKey;
    if (!parseKeyStructure(lakKeyOpt, "LAKKey", asyncResp->res, lakAuthScheme,
                           lakEcdsaKey, lakLmsKey, false))
    {
        return;
    }

    constexpr std::array<std::string_view, 1> interfaces = {dot::dotActionIntf};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project", 0, interfaces,
        std::bind_front(
            afterDOTCAKInstallServiceDiscovery, asyncResp, componentId,
            cakAuthScheme, cakEcdsaKey, cakLmsKey, lakAuthScheme, lakEcdsaKey,
            lakLmsKey, lockDisableOpt.value_or(false),
            static_cast<uint32_t>(minSecurityVersionOpt.value_or(0))));
}

/**
 * @brief Handle DOT Install action request
 *
 * Entry point for DOT Install action endpoint. Validates the request,
 * chassis, and component before processing the Install operation that
 * provisions Component Authentication Key (CAK) and optionally Lock
 * Authentication Key (LAK).
 *
 * @param app Crow application reference
 * @param req HTTP request object containing action parameters
 * @param asyncResp Async response object for the operation result
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleDOTInstallAction(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    BMCWEB_LOG_DEBUG(
        "DOT Install action called - Chassis: '{}', Component: '{}'", chassisId,
        componentId);

    if (componentId.empty())
    {
        BMCWEB_LOG_ERROR("DOT Install: componentId is empty");
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentId);
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(afterChassisValidationForDOTInstall, asyncResp,
                        std::ref(req), chassisId, componentId));
}

/**
 * @brief Process DOT service discovery for CAK Bypass operation
 *
 * Callback invoked after DBus discovery for DOT services. Validates that the
 * component exists and supports DOT operations, then initiates the CAK Bypass
 * operation via D-Bus.
 *
 * @param asyncResp Async response object for error reporting
 * @param componentId The trusted component identifier
 * @param ec Error code from DBus discovery operation
 * @param resp DBus subtree response containing discovered DOT objects
 */
inline void afterDOTCAKBypassServiceDiscovery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& componentId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& resp)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DOT CAKBypass GetSubTree error: {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }

    BMCWEB_LOG_DEBUG(
        "DOT CAKBypass service discovery: componentId='{}', found {} paths",
        componentId, resp.size());

    auto matchResult = dot_utils::findMatchingDOTComponent(componentId, resp);
    if (!matchResult)
    {
        BMCWEB_LOG_ERROR("DOT CAKBypass: No matching component found for: {}",
                         componentId);
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentId);
        return;
    }

    const auto& [path, serviceMap] = *matchResult;
    BMCWEB_LOG_DEBUG(
        "DOT CAKBypass found matching component: {} for componentId: {}", path,
        componentId);

    if (serviceMap.empty())
    {
        BMCWEB_LOG_ERROR("DOT CAKBypass: No service found for matched path: {}",
                         path);
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentId);
        return;
    }

    const std::string& dotService = serviceMap[0].first;
    BMCWEB_LOG_DEBUG(
        "DOT CAKBypass: Calling Bypass on service '{}' at path '{}'",
        dotService, path);

    nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<
        DOTResultType>(
        asyncResp, std::chrono::seconds(60), dotService, path,
        std::string(dot::dotActionIntf), "Bypass",
        [asyncResp](const std::string& status, const DOTResultType* resultPtr) {
            if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
            {
                BMCWEB_LOG_DEBUG("DOT CAKBypass succeeded");
                messages::success(asyncResp->res);
                return;
            }

            handleDOTErrorResult(asyncResp, status, resultPtr, "CAKBypass");
        });
}

/**
 * @brief Handle DOT CAK Bypass action request
 *
 * Entry point for DOT CAK Bypass action endpoint. Validates the request,
 * chassis, and component before initiating the bypass operation that skips
 * CAK authentication requirements.
 *
 * @param app Crow application reference
 * @param req HTTP request object
 * @param asyncResp Async response object for the operation result
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleDOTCAKBypassAction(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    BMCWEB_LOG_DEBUG(
        "DOT CAKBypass action called - Chassis: '{}', Component: '{}'",
        chassisId, componentId);

    if (componentId.empty())
    {
        BMCWEB_LOG_ERROR("DOT CAKBypass: componentId is empty");
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentId);
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(afterChassisValidationWithDOTDiscovery, asyncResp,
                        chassisId, componentId,
                        std::bind_front(afterDOTCAKBypassServiceDiscovery,
                                        asyncResp, componentId)));
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

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/Actions/NvidiaDOT.Install")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleDOTInstallAction, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/Actions/NvidiaDOT.CAKBypass")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleDOTCAKBypassAction, std::ref(app)));
}

} // namespace redfish
