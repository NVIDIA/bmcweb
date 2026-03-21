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
#include <http/utility.hpp>
#include <nvidia_messages.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <sdbusplus/message.hpp>
#include <utils/chassis_utils.hpp>
#include <utils/memfd_utils.hpp>
#include <utils/nvidia_async_call_utils.hpp>

#include <format>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

namespace redfish
{
using DOTErrorType = std::tuple<uint16_t, std::string>;
using DOTData = std::vector<uint8_t>;
using DOTGetInfoStruct = std::tuple<uint16_t, uint8_t, uint8_t, DOTData>;
using DOTResultType = std::variant<DOTErrorType, DOTData, DOTGetInfoStruct>;

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
 * @param actionName Name of the DOT action (e.g., "Install")
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
 * @brief Common helper for DOT service discovery
 *
 * Processes the D-Bus discovery response to find the matching DOT service
 * and path for a given component ID. Handles error cases and returns the
 * service and path if found.
 *
 * @param asyncResp Async response object for error reporting
 * @param componentId The trusted component identifier
 * @param actionName Name of the action for logging purposes
 * @param ec Error code from D-Bus discovery operation
 * @param resp D-Bus subtree response containing discovered DOT objects
 * @return Optional pair of (dotService, path) if found, nullopt if error
 */
inline std::optional<std::pair<std::string, std::string>> findDOTServiceAndPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& componentId, const std::string& actionName,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& resp)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DOT {}: GetSubTree error: {}", actionName,
                         ec.message());
        messages::internalError(asyncResp->res);
        return std::nullopt;
    }

    auto matchResult = dot_utils::findMatchingDOTComponent(componentId, resp);
    if (!matchResult)
    {
        BMCWEB_LOG_ERROR("DOT {}: No matching component found for: {}",
                         actionName, componentId);
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentId);
        return std::nullopt;
    }

    const auto& [path, serviceMap] = *matchResult;
    BMCWEB_LOG_DEBUG("DOT {}: Found matching component: {} for componentId: {}",
                     actionName, path, componentId);

    if (serviceMap.empty())
    {
        BMCWEB_LOG_ERROR("DOT {}: No service found for matched path: {}",
                         actionName, path);
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentId);
        return std::nullopt;
    }

    const std::string& dotService = serviceMap[0].first;
    BMCWEB_LOG_DEBUG("DOT {}: Found service '{}' at path '{}'", actionName,
                     dotService, path);

    return std::make_pair(dotService, path);
}

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
 * @brief Helper function for Lock/CAKRotate success handling (returns DOT data)
 *
 * Extracts DOTData from DOTResultType variant and encodes it as base64.
 *
 * @param asyncResp Async response object
 * @param resultPtr Pointer to DOTResultType variant (may be nullptr)
 */
inline void handleDOTDataSuccess(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const DOTResultType* resultPtr)
{
    if (resultPtr == nullptr)
    {
        BMCWEB_LOG_ERROR("DOT operation: Success but no result data");
        messages::internalError(asyncResp->res);
        return;
    }

    const DOTData* dotDataPtr = std::get_if<DOTData>(resultPtr);
    if (dotDataPtr == nullptr)
    {
        BMCWEB_LOG_ERROR("DOT operation: Result data is not DOTData");
        messages::internalError(asyncResp->res);
        return;
    }
    std::string base64Data = crow::utility::base64encode(
        std::string(dotDataPtr->begin(), dotDataPtr->end()));
    asyncResp->res.jsonValue["DOTData"] = base64Data;
    BMCWEB_LOG_DEBUG(
        "DOT operation: Returning DOT data (byte array converted to base64)");
    asyncResp->res.result(boost::beast::http::status::ok);
}

/**
 * @brief Helper function for UnlockChallenge success handling (returns
 * challenge)
 *
 * @param asyncResp Async response object
 * @param resultPtr Pointer to DOTResultType variant (may be nullptr)
 */
inline void handleDOTUnlockChallengeSuccess(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const DOTResultType* resultPtr)
{
    if (resultPtr == nullptr)
    {
        BMCWEB_LOG_ERROR("DOT UnlockChallenge: Success but no result data");
        messages::internalError(asyncResp->res);
        return;
    }

    const std::vector<uint8_t>* challengeBytes =
        std::get_if<std::vector<uint8_t>>(resultPtr);
    if (challengeBytes == nullptr)
    {
        BMCWEB_LOG_ERROR(
            "DOT UnlockChallenge: Result data is not a byte array");
        messages::internalError(asyncResp->res);
        return;
    }
    std::string hexChallenge = bytesToHexString(*challengeBytes);
    asyncResp->res.jsonValue["Challenge"] = hexChallenge;
    asyncResp->res.result(boost::beast::http::status::ok);
}

/**
 * @brief Helper function for GetInfo success handling (returns structured data)
 *
 * @param asyncResp Async response object
 * @param resultPtr Pointer to DOTResultType variant (may be nullptr)
 */
inline void handleDOTGetInfoSuccess(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const DOTResultType* resultPtr)
{
    if (resultPtr == nullptr)
    {
        BMCWEB_LOG_ERROR("DOT GetInfo: Success but no result data");
        messages::internalError(asyncResp->res);
        return;
    }

    const DOTGetInfoStruct* dotInfo = std::get_if<DOTGetInfoStruct>(resultPtr);
    if (dotInfo == nullptr)
    {
        BMCWEB_LOG_ERROR("DOT GetInfo: Result data is not a DotGetInfoStruct");
        messages::internalError(asyncResp->res);
        return;
    }
    const auto& [version, fuseState, transfers, dotData] = *dotInfo;

    asyncResp->res.jsonValue["DOTVersion"] = static_cast<int64_t>(version);
    asyncResp->res.jsonValue["FuseChangeState"] =
        dot_utils::dotFuseChangeStateToString(fuseState);
    asyncResp->res.jsonValue["TransfersRemaining"] =
        static_cast<int64_t>(transfers);
    std::string base64Data = crow::utility::base64encode(
        std::string(dotData.begin(), dotData.end()));
    asyncResp->res.jsonValue["DOTData"] = base64Data;
    asyncResp->res.result(boost::beast::http::status::ok);
}

/**
 * @brief Reads DOTState property from D-Bus and sets it in the response
 *
 * Reads the DOTState property from the com.nvidia.Dot.Action interface
 * and converts it to Redfish format.
 *
 * @param asyncResp Async response object to populate with DOTState
 * @param service D-Bus service name providing the DOT interface
 * @param path D-Bus object path for the DOT component
 */
inline void getDOTState(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& service, const std::string& path)
{
    dbus::utility::getProperty<std::string>(
        service, path, std::string(dot::dotActionIntf), "DOTState",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& dotState) {
            if (ec)
            {
                if (ec == boost::system::errc::host_unreachable)
                {
                    BMCWEB_LOG_DEBUG("DOTState service not available {}", ec);
                    asyncResp->res.jsonValue["DOTState"] = nullptr;
                    return;
                }
                BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            std::optional<std::string> state =
                dot_utils::convertDOTStateToRedfish(dotState);
            if (state)
            {
                asyncResp->res.jsonValue["DOTState"] = *state;
            }
            else
            {
                asyncResp->res.jsonValue["DOTState"] = nullptr;
            }
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

    if (!serviceMap.empty())
    {
        const std::string& dotService = serviceMap[0].first;
        getDOTState(asyncResp, dotService, path);
    }

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

    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.RecoverDOT"]
                            ["@Redfish.ActionInfo"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/RecoverDOTActionInfo",
        chassisId, componentId);
    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.RecoverDOT"]
                            ["target"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/Actions/NvidiaDOT.RecoverDOT",
        chassisId, componentId);

    asyncResp->res.jsonValue["Actions"]["#NvidiaDOT.GetInfo"]
                            ["target"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}/Oem/Nvidia/DOT/Actions/NvidiaDOT.GetInfo",
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
                          {"Name", "VendorMinimumSecurityVersion"},
                          {"Required", false}});
    parameters.push_back({{"DataType", "Number"},
                          {"Name", "OwnerMinimumSecurityVersion"},
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
 * @brief Handles GET request for RecoverDOT ActionInfo
 *
 * Returns ActionInfo resource describing the parameters required for the
 * RecoverDOT action, including DOT data string.
 *
 * @param app Crow application reference
 * @param req HTTP request object
 * @param asyncResp Async response object to populate with ActionInfo data
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleDOTRecoverDOTActionInfo(
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

    handleDOTActionInfo(asyncResp, chassisId, componentId, "RecoverDOT",
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
inline bool parseKeyStructure(
    const std::optional<nlohmann::json>& keyJsonOpt, const std::string& keyName,
    crow::Response& res, std::string& authScheme, std::string& ecdsaKey,
    std::string& lmsKey, const std::string& actionName, bool isRequired = true,
    const std::string& defaultAuthScheme = "Ecdsa")
{
    if (!keyJsonOpt)
    {
        if (isRequired)
        {
            messages::actionParameterMissing(res, actionName, keyName);
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
        messages::actionParameterValueTypeError(res, keyJson.dump(), actionName,
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
            res, actionName, keyName + "/AuthenticationScheme", authScheme);
        return false;
    }

    if (authScheme == "Hybrid" && lmsKey.empty())
    {
        messages::actionParameterMissing(res, actionName, keyName + "/LMSKey");
        return false;
    }
    return true;
}

/**
 * @brief Process DOT service discovery for CAKRotate operation
 *
 * Callback invoked after DBus discovery for DOT services. Validates that the
 * component exists and supports DOT operations, then initiates the CAKRotate
 * operation via D-Bus with the provided CAK key fields and signature fields.
 *
 * @param asyncResp Async response object for error reporting
 * @param componentId The trusted component identifier
 * @param cakKeyAuthScheme CAK key authentication scheme (Ecdsa or Hybrid)
 * @param cakEcdsaKey CAK ECDSA key data
 * @param cakLmsKey CAK LMS key data
 * @param signatureAuthScheme Signature authentication scheme (Ecdsa or Hybrid)
 * @param ecdsaSignature ECDSA signature data
 * @param lmsSignature LMS signature data
 * @param ec Error code from DBus discovery operation
 * @param resp DBus subtree response containing discovered DOT objects
 */
inline void afterDOTCAKRotateServiceDiscovery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& componentId, const std::string& cakKeyAuthEnum,
    const std::string& cakEcdsaKey, const std::string& cakLmsKey,
    const std::string& signatureAuthEnum, const std::string& ecdsaSignature,
    const std::string& lmsSignature, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& resp)
{
    auto servicePath =
        findDOTServiceAndPath(asyncResp, componentId, "CAKRotate", ec, resp);
    if (!servicePath.has_value())
    {
        return;
    }

    const auto& [dotService, path] = *servicePath;

    BMCWEB_LOG_DEBUG(
        "DOT CAKRotate: Calling CAKRotate on service '{}' at path '{}'",
        dotService, path);

    nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<
        DOTResultType>(
        asyncResp, std::chrono::seconds(60), dotService, path,
        std::string(dot::dotActionIntf), "CAKRotate",
        [asyncResp](const std::string& status, const DOTResultType* resultPtr) {
            if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
            {
                BMCWEB_LOG_DEBUG("DOT CAKRotate succeeded");
                handleDOTDataSuccess(asyncResp, resultPtr);
                return;
            }
            handleDOTErrorResult(asyncResp, status, resultPtr,
                                 "NvidiaDOT.CAKRotate");
        },
        cakKeyAuthEnum, cakEcdsaKey, cakLmsKey, signatureAuthEnum,
        ecdsaSignature, lmsSignature);
}

/**
 * @brief Process DOT service discovery for GetInfo operation
 *
 * Callback invoked after DBus discovery for DOT services. Validates that the
 * service exists and contains the required DOT action interface before
 * initiating the GetInfo operation via dot_async.
 *
 * @param asyncResp Async response object to populate
 * @param componentId Component ID from the URI
 * @param ec Error code from getSubTree operation
 * @param subtree D-Bus mapper subtree result containing service information
 */
inline void afterDOTGetInfoServiceDiscovery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& componentId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    auto servicePath =
        findDOTServiceAndPath(asyncResp, componentId, "GetInfo", ec, subtree);
    if (!servicePath.has_value())
    {
        return;
    }

    const auto& [dotService, path] = *servicePath;

    BMCWEB_LOG_DEBUG(
        "DOT GetInfo: Calling GetInfo on service '{}' at path '{}'", dotService,
        path);

    nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<
        DOTResultType>(
        asyncResp, std::chrono::seconds(60), dotService, path,
        std::string(dot::dotActionIntf), "GetInfo",
        [asyncResp](const std::string& status, const DOTResultType* resultPtr) {
            if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
            {
                BMCWEB_LOG_DEBUG("DOT GetInfo succeeded");
                handleDOTGetInfoSuccess(asyncResp, resultPtr);
                return;
            }

            handleDOTErrorResult(asyncResp, status, resultPtr,
                                 "NvidiaDOT.GetInfo");
        });
}

/**
 * @brief Process DOT service discovery for Install operation
 *
 * Callback invoked after DBus discovery for DOT services. Validates that the
 * component exists and supports DOT operations, then initiates the Install
 * operation via D-Bus with the provided CAK and LAK key fields.
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
 * @param ownerMinSvn OEM MIN_SVN value (0-255)
 * @param vendorMinSvn VENDOR MIN_SVN value (0-255)
 * @param ec Error code from DBus discovery operation
 * @param resp DBus subtree response containing discovered DOT objects
 */
inline void afterDOTCAKInstallServiceDiscovery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& componentId, const std::string& cakAuthEnum,
    const std::string& cakEcdsaKey, const std::string& cakLmsKey,
    const std::string& lakAuthEnum, const std::string& lakEcdsaKey,
    const std::string& lakLmsKey, bool lockDisable, uint8_t ownerMinSvn,
    uint8_t vendorMinSvn, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& resp)
{
    auto servicePath =
        findDOTServiceAndPath(asyncResp, componentId, "Install", ec, resp);
    if (!servicePath.has_value())
    {
        return;
    }

    const auto& [dotService, path] = *servicePath;

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
        lakLmsKey, lockDisable, ownerMinSvn, vendorMinSvn);
}

/**
 * @brief Process DOT service discovery for Lock operation
 *
 * Callback invoked after DBus discovery for DOT services. Validates that the
 * component exists and supports DOT operations, then initiates the Lock
 * operation via the DotCommandHandler with the provided key and signature data.
 *
 * @param asyncResp Async response object for error reporting
 * @param componentId The trusted component identifier
 * @param cakAuthScheme CAK authentication scheme (Ecdsa or Hybrid)
 * @param cakEcdsaKey CAK ECDSA key data
 * @param cakLmsKey CAK LMS key data
 * @param lakAuthScheme LAK authentication scheme (Ecdsa or Hybrid)
 * @param lakEcdsaKey LAK ECDSA key data
 * @param lakLmsKey LAK LMS key data
 * @param nonceType Nonce type for unlock challenge
 * @param staticChallenge Static challenge value
 * @param lockSigAuthScheme Lock signature authentication scheme
 * @param ecdsaSignature ECDSA signature data
 * @param lmsSignature LMS signature data
 * @param ec Error code from DBus discovery operation
 * @param resp DBus subtree response containing discovered DOT objects
 */
inline void afterDOTLockServiceDiscovery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& componentId, const std::string& cakAuthEnum,
    const std::string& cakEcdsaKey, const std::string& cakLmsKey,
    const std::string& lakAuthEnum, const std::string& lakEcdsaKey,
    const std::string& lakLmsKey, const std::string& nonceTypeEnum,
    const std::string& staticChallenge, const std::string& lockSigAuthEnum,
    const std::string& ecdsaSignature, const std::string& lmsSignature,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& resp)
{
    auto servicePath =
        findDOTServiceAndPath(asyncResp, componentId, "Lock", ec, resp);
    if (!servicePath.has_value())
    {
        return;
    }

    const auto& [dotService, path] = *servicePath;

    BMCWEB_LOG_DEBUG("DOT Lock: Calling Lock on service '{}' at path '{}'",
                     dotService, path);

    nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<
        DOTResultType>(
        asyncResp, std::chrono::seconds(60), dotService, path,
        std::string(dot::dotActionIntf), "Lock",
        [asyncResp](const std::string& status, const DOTResultType* resultPtr) {
            if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
            {
                BMCWEB_LOG_DEBUG("DOT Lock succeeded");
                handleDOTDataSuccess(asyncResp, resultPtr);
                return;
            }
            handleDOTErrorResult(asyncResp, status, resultPtr,
                                 "NvidiaDOT.Lock");
        },
        cakAuthEnum, cakEcdsaKey, cakLmsKey, lakAuthEnum, lakEcdsaKey,
        lakLmsKey, nonceTypeEnum, staticChallenge, lockSigAuthEnum,
        ecdsaSignature, lmsSignature);
}

/**
 * @brief Process DOT service discovery for UnlockChallenge operation
 *
 * Callback invoked after DBus discovery for DOT services. Validates that the
 * component exists and supports DOT operations, then initiates the
 * UnlockChallenge operation via the DotCommandHandler.
 *
 * @param asyncResp Async response object for error reporting
 * @param componentId The trusted component identifier
 * @param unlockType Unlock type (OwnerUnlock or VendorUnlock)
 * @param ec Error code from DBus discovery operation
 * @param resp DBus subtree response containing discovered DOT objects
 */
inline void afterDOTUnlockChallengeServiceDiscovery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& componentId, const std::string& unlockTypeEnum,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& resp)
{
    auto servicePath = findDOTServiceAndPath(asyncResp, componentId,
                                             "UnlockChallenge", ec, resp);
    if (!servicePath.has_value())
    {
        return;
    }

    const auto& [dotService, path] = *servicePath;

    BMCWEB_LOG_DEBUG(
        "DOT UnlockChallenge: Calling UnlockChallenge on service '{}' at path '{}'",
        dotService, path);

    nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<
        DOTResultType>(
        asyncResp, std::chrono::seconds(60), dotService, path,
        std::string(dot::dotActionIntf), "UnlockChallenge",
        [asyncResp](const std::string& status, const DOTResultType* resultPtr) {
            if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
            {
                BMCWEB_LOG_DEBUG("DOT UnlockChallenge succeeded");
                handleDOTUnlockChallengeSuccess(asyncResp, resultPtr);
                return;
            }
            handleDOTErrorResult(asyncResp, status, resultPtr,
                                 "NvidiaDOT.UnlockChallenge");
        },
        unlockTypeEnum);
}

/**
 * @brief Process DOT service discovery for Unlock operation
 *
 * Callback invoked after DBus discovery for DOT services. Validates that the
 * service exists and contains the required DOT action interface before
 * initiating the Unlock operation via dot_async.
 *
 * @param asyncResp Async response object to populate
 * @param componentId Component ID from the URI
 * @param lakSigAuthScheme LAK signature authentication scheme
 * @param ecdsaSignature ECDSA signature
 * @param lmsSignature LMS signature
 * @param ec Error code from getSubTree operation
 * @param subtree D-Bus mapper subtree result containing service information
 */
inline void afterDOTUnlockServiceDiscovery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& componentId, const std::string& lakSigAuthEnum,
    const std::string& ecdsaSignature, const std::string& lmsSignature,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    auto servicePath =
        findDOTServiceAndPath(asyncResp, componentId, "Unlock", ec, subtree);
    if (!servicePath.has_value())
    {
        return;
    }

    const auto& [dotService, path] = *servicePath;

    BMCWEB_LOG_DEBUG("DOT Unlock: Calling Unlock on service '{}' at path '{}'",
                     dotService, path);

    nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<
        DOTResultType>(
        asyncResp, std::chrono::seconds(60), dotService, path,
        std::string(dot::dotActionIntf), "Unlock",
        [asyncResp](const std::string& status, const DOTResultType* resultPtr) {
            if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
            {
                BMCWEB_LOG_DEBUG("DOT Unlock succeeded");
                messages::success(asyncResp->res);
                return;
            }

            handleDOTErrorResult(asyncResp, status, resultPtr,
                                 "NvidiaDOT.Unlock");
        },
        lakSigAuthEnum, ecdsaSignature, lmsSignature);
}

/**
 * @brief Parse and validate signature structure from JSON request
 *
 * Extracts and validates authentication scheme, ECDSA signature, and LMS
 * signature from a JSON signature structure object. Validates authentication
 * scheme values and ensures required fields are present.
 *
 * @param sigJson JSON object containing signature structure
 * @param sigName Name of the signature (for error messages)
 * @param res Response object for error reporting
 * @param authScheme Output: Authentication scheme (Ecdsa or Hybrid)
 * @param ecdsaSignature Output: ECDSA signature data
 * @param lmsSignature Output: LMS signature data (required for Hybrid scheme)
 * @param actionName Name of the action (for error messages)
 * @return true if parsing succeeded, false otherwise
 */
inline bool parseSignatureStructure(
    nlohmann::json& sigJson, const std::string& sigName, crow::Response& res,
    std::string& authScheme, std::string& ecdsaSignature,
    std::string& lmsSignature, const std::string& actionName)
{
    if (!sigJson.is_object())
    {
        messages::actionParameterValueTypeError(res, sigJson.dump(), actionName,
                                                sigName);
        return false;
    }

    std::optional<std::string> lmsSigOpt;
    if (!redfish::json_util::readJson(
            sigJson, res, "AuthenticationScheme", authScheme, "ECDSASignature",
            ecdsaSignature, "LMSSignature", lmsSigOpt))
    {
        return false;
    }

    lmsSignature = lmsSigOpt.value_or("");
    if (authScheme == "ECDSA")
    {
        authScheme = "Ecdsa";
    }

    if (authScheme != "Ecdsa" && authScheme != "Hybrid")
    {
        messages::actionParameterValueNotInList(
            res, actionName, sigName + "/AuthenticationScheme", authScheme);
        return false;
    }

    if (authScheme == "Hybrid" && lmsSignature.empty())
    {
        messages::actionParameterMissing(res, actionName,
                                         sigName + "/LMSSignature");
        return false;
    }
    return true;
}

/**
 * @brief Process chassis validation and parse CAKRotate action parameters
 *
 * Callback invoked after chassis validation for DOT CAKRotate action. Parses
 * and validates the request JSON body including NewCAKKey (KeyStructure) and
 * LAKSignature (SignatureStructure), extracts individual fields, then initiates
 * DOT service discovery.
 *
 * @param asyncResp Async response object for error reporting
 * @param req HTTP request containing action parameters
 * @param chassisId The chassis identifier
 * @param componentId The trusted component identifier
 * @param validChassisPath Optional path to validated chassis (nullopt if
 * invalid)
 */
inline void afterChassisValidationForDOTCAKRotate(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const crow::Request& req, const std::string& chassisId,
    const std::string& componentId,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("DOT CAKRotate: Invalid chassis: {}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    nlohmann::json newCAKKey;
    nlohmann::json lakSignature;

    if (!json_util::readJsonAction(req, asyncResp->res, "NewCAKKey", newCAKKey,
                                   "LAKSignature", lakSignature))
    {
        return;
    }

    std::string cakKeyAuthScheme;
    std::string cakEcdsaKey;
    std::string cakLmsKey;
    if (!parseKeyStructure(newCAKKey, "NewCAKKey", asyncResp->res,
                           cakKeyAuthScheme, cakEcdsaKey, cakLmsKey,
                           "NvidiaDOT.CAKRotate", true))
    {
        return;
    }

    std::string signatureAuthScheme;
    std::string ecdsaSignature;
    std::string lmsSignature;
    if (!parseSignatureStructure(lakSignature, "LAKSignature", asyncResp->res,
                                 signatureAuthScheme, ecdsaSignature,
                                 lmsSignature, "NvidiaDOT.CAKRotate"))
    {
        return;
    }

    std::string cakKeyAuthEnum =
        dot_utils::convertAuthSchemeToDbusEnum(cakKeyAuthScheme);
    std::string signatureAuthEnum =
        dot_utils::convertAuthSchemeToDbusEnum(signatureAuthScheme);

    if (cakKeyAuthEnum.empty())
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "NvidiaDOT.CAKRotate",
            "NewCAKKey/AuthenticationScheme", cakKeyAuthScheme);
        return;
    }

    if (signatureAuthEnum.empty())
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "NvidiaDOT.CAKRotate",
            "LAKSignature/AuthenticationScheme", signatureAuthScheme);
        return;
    }

    constexpr std::array<std::string_view, 1> interfaces = {dot::dotActionIntf};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project", 0, interfaces,
        std::bind_front(afterDOTCAKRotateServiceDiscovery, asyncResp,
                        componentId, cakKeyAuthEnum, cakEcdsaKey, cakLmsKey,
                        signatureAuthEnum, ecdsaSignature, lmsSignature));
}

/**
 * @brief Process chassis validation for GetInfo action
 *
 * Callback invoked after chassis validation for DOT GetInfo action.
 * Initiates DOT service discovery.
 *
 * @param asyncResp Async response object for error reporting
 * @param chassisId The chassis identifier
 * @param componentId The trusted component identifier
 * @param validChassisPath Optional path to validated chassis (nullopt if
 * invalid)
 */
inline void afterChassisValidationForDOTGetInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("DOT GetInfo: Invalid chassis: {}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    constexpr std::array<std::string_view, 1> interfaces = {dot::dotActionIntf};
    dbus::utility::getSubTree("/xyz/openbmc_project", 0, interfaces,
                              std::bind_front(afterDOTGetInfoServiceDiscovery,
                                              asyncResp, componentId));
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
    std::optional<int64_t> vendorMinSecurityVersionOpt;
    std::optional<int64_t> ownerMinSecurityVersionOpt;

    if (!json_util::readJsonAction(
            req, asyncResp->res, "CAKKey", cakKeyOpt, "LockDisable",
            lockDisableOpt, "LAKKey", lakKeyOpt, "VendorMinimumSecurityVersion",
            vendorMinSecurityVersionOpt, "OwnerMinimumSecurityVersion",
            ownerMinSecurityVersionOpt))
    {
        return;
    }

    if (ownerMinSecurityVersionOpt.has_value() &&
        (ownerMinSecurityVersionOpt.value() < 0 ||
         ownerMinSecurityVersionOpt.value() > 255))
    {
        messages::actionParameterValueOutOfRange(
            asyncResp->res, "Install", "OwnerMinimumSecurityVersion",
            std::to_string(ownerMinSecurityVersionOpt.value()));
        return;
    }

    if (vendorMinSecurityVersionOpt.has_value() &&
        (vendorMinSecurityVersionOpt.value() < 0 ||
         vendorMinSecurityVersionOpt.value() > 255))
    {
        messages::actionParameterValueOutOfRange(
            asyncResp->res, "Install", "VendorMinimumSecurityVersion",
            std::to_string(vendorMinSecurityVersionOpt.value()));
        return;
    }

    uint8_t ownerMinSvn =
        static_cast<uint8_t>(ownerMinSecurityVersionOpt.value_or(0));
    uint8_t vendorMinSvn =
        static_cast<uint8_t>(vendorMinSecurityVersionOpt.value_or(0));

    std::string cakAuthScheme;
    std::string cakEcdsaKey;
    std::string cakLmsKey;
    if (!parseKeyStructure(cakKeyOpt, "CAKKey", asyncResp->res, cakAuthScheme,
                           cakEcdsaKey, cakLmsKey, "Install", true))
    {
        return;
    }

    std::string lakAuthScheme;
    std::string lakEcdsaKey;
    std::string lakLmsKey;
    if (!parseKeyStructure(lakKeyOpt, "LAKKey", asyncResp->res, lakAuthScheme,
                           lakEcdsaKey, lakLmsKey, "Install", false))
    {
        return;
    }

    std::string cakAuthEnum =
        dot_utils::convertAuthSchemeToDbusEnum(cakAuthScheme);
    std::string lakAuthEnum =
        dot_utils::convertAuthSchemeToDbusEnum(lakAuthScheme);

    if (cakAuthEnum.empty())
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "Install", "CAKKey/AuthenticationScheme",
            cakAuthScheme);
        return;
    }

    if (lakAuthEnum.empty())
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "Install", "LAKKey/AuthenticationScheme",
            lakAuthScheme);
        return;
    }

    constexpr std::array<std::string_view, 1> interfaces = {dot::dotActionIntf};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project", 0, interfaces,
        std::bind_front(afterDOTCAKInstallServiceDiscovery, asyncResp,
                        componentId, cakAuthEnum, cakEcdsaKey, cakLmsKey,
                        lakAuthEnum, lakEcdsaKey, lakLmsKey,
                        lockDisableOpt.value_or(false), ownerMinSvn,
                        vendorMinSvn));
}

/**
 * @brief Handle DOT CAKRotate action request
 *
 * Entry point for DOT CAKRotate action endpoint. Validates the request,
 * chassis, and component before processing the CAKRotate operation that
 * rotates the locked CAK without causing a DOT FUSE State change.
 *
 * @param app Crow application reference
 * @param req HTTP request object containing action parameters
 * @param asyncResp Async response object for the operation result
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleDOTCAKRotateAction(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    BMCWEB_LOG_DEBUG(
        "DOT CAKRotate action called - Chassis: '{}', Component: '{}'",
        chassisId, componentId);

    if (componentId.empty())
    {
        BMCWEB_LOG_ERROR("DOT CAKRotate: componentId is empty");
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentId);
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(afterChassisValidationForDOTCAKRotate, asyncResp,
                        std::ref(req), chassisId, componentId));
}

/**
 * @brief Handle DOT GetInfo action request
 *
 * Entry point for DOT GetInfo action endpoint. Validates the request,
 * chassis, and component before processing the GetInfo operation.
 *
 * @param app Crow application reference
 * @param req HTTP request object
 * @param asyncResp Async response object for the operation result
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleDOTGetInfoAction(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    BMCWEB_LOG_DEBUG(
        "DOT GetInfo action called - Chassis: '{}', Component: '{}'", chassisId,
        componentId);

    if (componentId.empty())
    {
        BMCWEB_LOG_ERROR("DOT GetInfo: componentId is empty");
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentId);
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(afterChassisValidationForDOTGetInfo, asyncResp,
                        chassisId, componentId));
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
 * @brief Process chassis validation and parse Lock action parameters
 *
 * Callback invoked after chassis validation for DOT Lock action. Parses
 * and validates the request JSON body including CAK key, LAK key, nonce type,
 * static challenge, and signature, then initiates DOT service discovery.
 *
 * @param asyncResp Async response object for error reporting
 * @param req HTTP request containing action parameters
 * @param chassisId The chassis identifier
 * @param componentId The trusted component identifier
 * @param validChassisPath Optional path to validated chassis (nullopt if
 * invalid)
 */
inline void afterChassisValidationForDOTLock(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const crow::Request& req, const std::string& chassisId,
    const std::string& componentId,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("DOT Lock: Invalid chassis: {}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    nlohmann::json cakKey;
    nlohmann::json lakKey;
    std::string nonceType;
    std::optional<std::string> staticChallengeOpt;
    nlohmann::json lakSignature;

    if (!json_util::readJsonAction(
            req, asyncResp->res, "CAKKey", cakKey, "LAKKey", lakKey,
            "NonceType", nonceType, "StaticChallenge", staticChallengeOpt,
            "LAKSignature", lakSignature))
    {
        return;
    }

    std::string cakAuthScheme;
    std::string cakEcdsaKey;
    std::string cakLmsKey;
    if (!parseKeyStructure(cakKey, "CAKKey", asyncResp->res, cakAuthScheme,
                           cakEcdsaKey, cakLmsKey, "NvidiaDOT.Lock", true))
    {
        return;
    }

    std::string lakAuthScheme;
    std::string lakEcdsaKey;
    std::string lakLmsKey;
    if (!parseKeyStructure(lakKey, "LAKKey", asyncResp->res, lakAuthScheme,
                           lakEcdsaKey, lakLmsKey, "NvidiaDOT.Lock", true))
    {
        return;
    }
    if (nonceType != "DeviceUniqueIdentifier" && nonceType != "RandomNonce" &&
        nonceType != "StaticValue")
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "NvidiaDOT.Lock", "NonceType", nonceType);
        return;
    }

    if (nonceType == "StaticValue" && !staticChallengeOpt)
    {
        messages::actionParameterMissing(asyncResp->res, "NvidiaDOT.Lock",
                                         "StaticChallenge");
        return;
    }

    std::string staticChallenge = staticChallengeOpt.value_or("");

    std::string lockSigAuthScheme;
    std::string ecdsaSignature;
    std::string lmsSignature;
    if (!parseSignatureStructure(lakSignature, "LAKSignature", asyncResp->res,
                                 lockSigAuthScheme, ecdsaSignature,
                                 lmsSignature, "NvidiaDOT.Lock"))
    {
        return;
    }

    std::string cakAuthEnum =
        dot_utils::convertAuthSchemeToDbusEnum(cakAuthScheme);
    std::string lakAuthEnum =
        dot_utils::convertAuthSchemeToDbusEnum(lakAuthScheme);
    std::string lockSigAuthEnum =
        dot_utils::convertAuthSchemeToDbusEnum(lockSigAuthScheme);
    std::string nonceTypeEnum =
        dot_utils::convertNonceTypeToDbusEnum(nonceType);

    if (cakAuthEnum.empty())
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "NvidiaDOT.Lock", "CAKKey/AuthenticationScheme",
            cakAuthScheme);
        return;
    }

    if (lakAuthEnum.empty())
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "NvidiaDOT.Lock", "LAKKey/AuthenticationScheme",
            lakAuthScheme);
        return;
    }

    if (lockSigAuthEnum.empty())
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "NvidiaDOT.Lock",
            "LAKSignature/AuthenticationScheme", lockSigAuthScheme);
        return;
    }

    if (nonceTypeEnum.empty())
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "NvidiaDOT.Lock", "NonceType", nonceType);
        return;
    }

    constexpr std::array<std::string_view, 1> interfaces = {dot::dotActionIntf};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project", 0, interfaces,
        std::bind_front(afterDOTLockServiceDiscovery, asyncResp, componentId,
                        cakAuthEnum, cakEcdsaKey, cakLmsKey, lakAuthEnum,
                        lakEcdsaKey, lakLmsKey, nonceTypeEnum, staticChallenge,
                        lockSigAuthEnum, ecdsaSignature, lmsSignature));
}

/**
 * @brief Handle DOT Lock action request
 *
 * Entry point for DOT Lock action endpoint. Validates the request,
 * chassis, and component before processing the Lock operation that
 * transitions DOT from volatile state to locked state.
 *
 * @param app Crow application reference
 * @param req HTTP request object containing action parameters
 * @param asyncResp Async response object for the operation result
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleDOTLockAction(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    BMCWEB_LOG_DEBUG("DOT Lock action called - Chassis: '{}', Component: '{}'",
                     chassisId, componentId);

    if (componentId.empty())
    {
        BMCWEB_LOG_ERROR("DOT Lock: componentId is empty");
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentId);
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(afterChassisValidationForDOTLock, asyncResp,
                        std::ref(req), chassisId, componentId));
}

/**
 * @brief Process chassis validation and parse UnlockChallenge action parameters
 *
 * Callback invoked after chassis validation for DOT UnlockChallenge action.
 * Parses and validates the request JSON body including unlock type, then
 * initiates DOT service discovery.
 *
 * @param asyncResp Async response object for error reporting
 * @param req HTTP request containing action parameters
 * @param chassisId The chassis identifier
 * @param componentId The trusted component identifier
 * @param validChassisPath Optional path to validated chassis (nullopt if
 * invalid)
 */
inline void afterChassisValidationForDOTUnlockChallenge(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const crow::Request& req, const std::string& chassisId,
    const std::string& componentId,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("DOT UnlockChallenge: Invalid chassis: {}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    std::string unlockType;

    if (!json_util::readJsonAction(req, asyncResp->res, "UnlockType",
                                   unlockType))
    {
        return;
    }
    if (unlockType != "OwnerUnlock" && unlockType != "VendorUnlock")
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "UnlockChallenge", "UnlockType", unlockType);
        return;
    }

    std::string unlockTypeEnum =
        dot_utils::convertUnlockTypeToDbusEnum(unlockType);

    if (unlockTypeEnum.empty())
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "UnlockChallenge", "UnlockType", unlockType);
        return;
    }

    constexpr std::array<std::string_view, 1> interfaces = {dot::dotActionIntf};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project", 0, interfaces,
        std::bind_front(afterDOTUnlockChallengeServiceDiscovery, asyncResp,
                        componentId, unlockTypeEnum));
}

/**
 * @brief Handle DOT UnlockChallenge action request
 *
 * Entry point for DOT UnlockChallenge action endpoint. Validates the request,
 * chassis, and component before processing the UnlockChallenge operation that
 * generates a challenge for subsequent Unlock or Override operations.
 *
 * @param app Crow application reference
 * @param req HTTP request object containing action parameters
 * @param asyncResp Async response object for the operation result
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleDOTUnlockChallengeAction(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    BMCWEB_LOG_DEBUG(
        "DOT UnlockChallenge action called - Chassis: '{}', Component: '{}'",
        chassisId, componentId);

    if (componentId.empty())
    {
        BMCWEB_LOG_ERROR("DOT UnlockChallenge: componentId is empty");
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentId);
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(afterChassisValidationForDOTUnlockChallenge, asyncResp,
                        std::ref(req), chassisId, componentId));
}

/**
 * @brief Process chassis validation and parse Unlock action parameters
 *
 * Callback invoked after chassis validation for DOT Unlock action. Parses
 * and validates the request JSON body including LAK signature, then initiates
 * DOT service discovery.
 *
 * @param asyncResp Async response object for error reporting
 * @param req HTTP request containing action parameters
 * @param chassisId The chassis identifier
 * @param componentId The trusted component identifier
 * @param validChassisPath Optional path to validated chassis (nullopt if
 * invalid)
 */
inline void afterChassisValidationForDOTUnlock(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const crow::Request& req, const std::string& chassisId,
    const std::string& componentId,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("DOT Unlock: Invalid chassis: {}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    nlohmann::json lakSigJson;
    if (!redfish::json_util::readJsonAction(req, asyncResp->res, "LAKSignature",
                                            lakSigJson))
    {
        return;
    }

    std::string lakSigAuthScheme;
    std::string ecdsaSignature;
    std::string lmsSignature;
    if (!parseSignatureStructure(lakSigJson, "LAKSignature", asyncResp->res,
                                 lakSigAuthScheme, ecdsaSignature, lmsSignature,
                                 "NvidiaDOT.Unlock"))
    {
        return;
    }

    std::string lakSigAuthEnum =
        dot_utils::convertAuthSchemeToDbusEnum(lakSigAuthScheme);

    if (lakSigAuthEnum.empty())
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "NvidiaDOT.Unlock",
            "LAKSignature/AuthenticationScheme", lakSigAuthScheme);
        return;
    }

    constexpr std::array<std::string_view, 1> interfaces = {dot::dotActionIntf};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project", 0, interfaces,
        std::bind_front(afterDOTUnlockServiceDiscovery, asyncResp, componentId,
                        lakSigAuthEnum, ecdsaSignature, lmsSignature));
}

/**
 * @brief Handle DOT Unlock action request
 *
 * Entry point for DOT Unlock action endpoint. Validates the request,
 * chassis, and component before processing the Unlock operation that
 * transitions DOT from locked state to volatile state.
 *
 * @param app Crow application reference
 * @param req HTTP request object containing action parameters
 * @param asyncResp Async response object for the operation result
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleDOTUnlockAction(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    BMCWEB_LOG_DEBUG(
        "DOT Unlock action called - Chassis: '{}', Component: '{}'", chassisId,
        componentId);

    if (componentId.empty())
    {
        BMCWEB_LOG_ERROR("DOT Unlock: componentId is empty");
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentId);
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(afterChassisValidationForDOTUnlock, asyncResp,
                        std::ref(req), chassisId, componentId));
}

/**
 * @brief Process DOT service discovery for Disable operation
 *
 * Callback invoked after DBus discovery for DOT services. Validates that the
 * component exists and supports DOT operations, then initiates the Disable
 * operation via D-Bus.
 *
 * @param asyncResp Async response object for error reporting
 * @param componentId The trusted component identifier
 * @param lakAuthEnum LAK authentication scheme enum
 * @param lakEcdsaKey LAK ECDSA key data
 * @param lakLmsKey LAK LMS key data
 * @param nonceTypeEnum Nonce type enum
 * @param staticChallenge Static challenge value
 * @param lakSigAuthEnum LAK signature authentication scheme enum
 * @param ecdsaSignature ECDSA signature data
 * @param lmsSignature LMS signature data
 * @param ec Error code from DBus discovery operation
 * @param resp DBus subtree response containing discovered DOT objects
 */
inline void afterDOTDisableServiceDiscovery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& componentId, const std::string& lakAuthEnum,
    const std::string& lakEcdsaKey, const std::string& lakLmsKey,
    const std::string& nonceTypeEnum, const std::string& staticChallenge,
    const std::string& lakSigAuthEnum, const std::string& ecdsaSignature,
    const std::string& lmsSignature, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& resp)
{
    auto servicePath =
        findDOTServiceAndPath(asyncResp, componentId, "Disable", ec, resp);
    if (!servicePath.has_value())
    {
        return;
    }

    const auto& [dotService, path] = *servicePath;

    BMCWEB_LOG_DEBUG(
        "DOT Disable: Calling Disable on service '{}' at path '{}'", dotService,
        path);

    nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<
        DOTResultType>(
        asyncResp, std::chrono::seconds(10), dotService, path,
        std::string(dot::dotActionIntf), "Disable",
        [asyncResp](const std::string& status, const DOTResultType* resultPtr) {
            if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
            {
                BMCWEB_LOG_DEBUG("DOT Disable succeeded");
                handleDOTDataSuccess(asyncResp, resultPtr);
                return;
            }
            handleDOTErrorResult(asyncResp, status, resultPtr,
                                 "NvidiaDOT.Disable");
        },
        lakAuthEnum, lakEcdsaKey, lakLmsKey, nonceTypeEnum, staticChallenge,
        lakSigAuthEnum, ecdsaSignature, lmsSignature);
}

/**
 * @brief Process chassis validation and parse Disable action parameters
 *
 * Callback invoked after chassis validation for DOT Disable action. Parses
 * and validates the request JSON body including LAK key, nonce type,
 * optional static challenge, and signature, then initiates DOT service
 * discovery.
 *
 * @param asyncResp Async response object for error reporting
 * @param req HTTP request containing action parameters
 * @param chassisId The chassis identifier
 * @param componentId The trusted component identifier
 * @param validChassisPath Optional path to validated chassis (nullopt if
 * invalid)
 */
inline void afterChassisValidationForDOTDisable(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const crow::Request& req, const std::string& chassisId,
    const std::string& componentId,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("DOT Disable: Invalid chassis: {}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    nlohmann::json lakKey;
    std::string nonceType;
    std::optional<std::string> staticChallengeOpt;
    nlohmann::json lakSignature;

    if (!json_util::readJsonAction(
            req, asyncResp->res, "LAKKey", lakKey, "NonceType", nonceType,
            "StaticChallenge", staticChallengeOpt, "LAKSignature",
            lakSignature))
    {
        return;
    }

    std::string lakAuthScheme;
    std::string lakEcdsaKey;
    std::string lakLmsKey;
    if (!parseKeyStructure(lakKey, "LAKKey", asyncResp->res, lakAuthScheme,
                           lakEcdsaKey, lakLmsKey, "NvidiaDOT.Disable", true))
    {
        return;
    }

    if (nonceType != "DeviceUniqueIdentifier" && nonceType != "RandomNonce" &&
        nonceType != "StaticValue")
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "NvidiaDOT.Disable", "NonceType", nonceType);
        return;
    }

    if (nonceType == "StaticValue" && !staticChallengeOpt)
    {
        messages::actionParameterMissing(asyncResp->res, "NvidiaDOT.Disable",
                                         "StaticChallenge");
        return;
    }

    std::string staticChallenge = staticChallengeOpt.value_or("");

    std::string lakSigAuthScheme;
    std::string ecdsaSignature;
    std::string lmsSignature;
    if (!parseSignatureStructure(lakSignature, "LAKSignature", asyncResp->res,
                                 lakSigAuthScheme, ecdsaSignature, lmsSignature,
                                 "NvidiaDOT.Disable"))
    {
        return;
    }

    std::string lakAuthEnum =
        dot_utils::convertAuthSchemeToDbusEnum(lakAuthScheme);
    std::string lakSigAuthEnum =
        dot_utils::convertAuthSchemeToDbusEnum(lakSigAuthScheme);
    std::string nonceTypeEnum =
        dot_utils::convertNonceTypeToDbusEnum(nonceType);

    if (lakAuthEnum.empty())
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "NvidiaDOT.Disable", "LAKKey/AuthenticationScheme",
            lakAuthScheme);
        return;
    }

    if (lakSigAuthEnum.empty())
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "NvidiaDOT.Disable",
            "LAKSignature/AuthenticationScheme", lakSigAuthScheme);
        return;
    }

    if (nonceTypeEnum.empty())
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "NvidiaDOT.Disable", "NonceType", nonceType);
        return;
    }

    constexpr std::array<std::string_view, 1> interfaces = {dot::dotActionIntf};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/DOT", 0, interfaces,
        std::bind_front(afterDOTDisableServiceDiscovery, asyncResp, componentId,
                        lakAuthEnum, lakEcdsaKey, lakLmsKey, nonceTypeEnum,
                        staticChallenge, lakSigAuthEnum, ecdsaSignature,
                        lmsSignature));
}

/**
 * @brief Process DOT service discovery for Override operation
 *
 * Callback invoked after DBus discovery for DOT services. Validates that the
 * service exists and contains the required DOT action interface before
 * initiating the Override operation via D-Bus.
 *
 * @param asyncResp Async response object for error reporting
 * @param componentId The trusted component identifier
 * @param vendorSigAuthEnum Vendor signature authentication scheme enum
 * @param ecdsaSignature ECDSA signature data
 * @param lmsSignature LMS signature data
 * @param ec Error code from DBus discovery operation
 * @param resp DBus subtree response containing discovered DOT objects
 */
inline void afterDOTOverrideServiceDiscovery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& componentId, const std::string& vendorSigAuthEnum,
    const std::string& ecdsaSignature, const std::string& lmsSignature,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& resp)
{
    auto servicePath =
        findDOTServiceAndPath(asyncResp, componentId, "Override", ec, resp);
    if (!servicePath.has_value())
    {
        return;
    }

    const auto& [dotService, path] = *servicePath;

    BMCWEB_LOG_DEBUG(
        "DOT Override: Calling Override on service '{}' at path '{}'",
        dotService, path);

    nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<
        DOTResultType>(
        asyncResp, std::chrono::seconds(10), dotService, path,
        std::string(dot::dotActionIntf), "Override",
        [asyncResp](const std::string& status, const DOTResultType* resultPtr) {
            if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
            {
                BMCWEB_LOG_DEBUG("DOT Override succeeded");
                messages::success(asyncResp->res);
                return;
            }
            handleDOTErrorResult(asyncResp, status, resultPtr,
                                 "NvidiaDOT.Override");
        },
        vendorSigAuthEnum, ecdsaSignature, lmsSignature);
}

/**
 * @brief Process chassis validation and parse Override action parameters
 *
 * Callback invoked after chassis validation for DOT Override action. Parses
 * and validates the request JSON body including vendor signature, then
 * initiates DOT service discovery.
 *
 * @param asyncResp Async response object for error reporting
 * @param req HTTP request containing action parameters
 * @param chassisId The chassis identifier
 * @param componentId The trusted component identifier
 * @param validChassisPath Optional path to validated chassis (nullopt if
 * invalid)
 */
inline void afterChassisValidationForDOTOverride(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const crow::Request& req, const std::string& chassisId,
    const std::string& componentId,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("DOT Override: Invalid chassis: {}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    nlohmann::json vendorSigJson;
    if (!redfish::json_util::readJsonAction(req, asyncResp->res,
                                            "VendorSignature", vendorSigJson))
    {
        return;
    }

    std::string vendorSigAuthScheme;
    std::string ecdsaSignature;
    std::string lmsSignature;
    if (!parseSignatureStructure(vendorSigJson, "VendorSignature",
                                 asyncResp->res, vendorSigAuthScheme,
                                 ecdsaSignature, lmsSignature,
                                 "NvidiaDOT.Override"))
    {
        return;
    }

    std::string vendorSigAuthEnum =
        dot_utils::convertAuthSchemeToDbusEnum(vendorSigAuthScheme);

    if (vendorSigAuthEnum.empty())
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "NvidiaDOT.Override",
            "VendorSignature/AuthenticationScheme", vendorSigAuthScheme);
        return;
    }

    constexpr std::array<std::string_view, 1> interfaces = {dot::dotActionIntf};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/DOT", 0, interfaces,
        std::bind_front(afterDOTOverrideServiceDiscovery, asyncResp,
                        componentId, vendorSigAuthEnum, ecdsaSignature,
                        lmsSignature));
}

/**
 * @brief Process DOT service discovery for Recovery operation
 *
 * Callback invoked after DBus discovery for DOT services. Validates that the
 * service exists and contains the required DOT action interface before
 * initiating the Recovery operation via D-Bus.
 *
 * @param asyncResp Async response object for error reporting
 * @param componentId The trusted component identifier
 * @param dotData Base64-encoded backup DOT data
 * @param ec Error code from DBus discovery operation
 * @param resp DBus subtree response containing discovered DOT objects
 */
inline void afterDOTRecoveryServiceDiscovery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& componentId,
    const std::shared_ptr<MemoryFD>& dotDataMemfd,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& resp)
{
    auto servicePath =
        findDOTServiceAndPath(asyncResp, componentId, "RecoverDOT", ec, resp);
    if (!servicePath.has_value())
    {
        return;
    }

    const auto& [dotService, path] = *servicePath;

    BMCWEB_LOG_DEBUG(
        "DOT Recovery: Calling Recovery on service '{}' at path '{}'",
        dotService, path);

    nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<
        DOTResultType>(
        asyncResp, std::chrono::seconds(10), dotService, path,
        std::string(dot::dotActionIntf), "RecoverDOT",
        [asyncResp](const std::string& status, const DOTResultType* resultPtr) {
            if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
            {
                BMCWEB_LOG_DEBUG("DOT Recovery succeeded");
                messages::success(asyncResp->res);
                return;
            }
            handleDOTErrorResult(asyncResp, status, resultPtr,
                                 "NvidiaDOT.RecoverDOT");
        },
        sdbusplus::message::unix_fd(dotDataMemfd->fd));
}

/**
 * @brief Process chassis validation and parse Recovery action parameters
 *
 * Callback invoked after chassis validation for DOT Recovery action. Parses
 * and validates the request JSON body including DOT data, then initiates
 * DOT service discovery.
 *
 * @param asyncResp Async response object for error reporting
 * @param req HTTP request containing action parameters
 * @param chassisId The chassis identifier
 * @param componentId The trusted component identifier
 * @param validChassisPath Optional path to validated chassis (nullopt if
 * invalid)
 */
inline void afterChassisValidationForDOTRecovery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const crow::Request& req, const std::string& chassisId,
    const std::string& componentId,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("DOT Recovery: Invalid chassis: {}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    std::string dotDataBase64;
    if (!redfish::json_util::readJsonAction(req, asyncResp->res, "DOTData",
                                            dotDataBase64))
    {
        return;
    }

    if (dotDataBase64.empty())
    {
        messages::actionParameterMissing(asyncResp->res, "NvidiaDOT.RecoverDOT",
                                         "DOTData");
        return;
    }

    constexpr size_t dotBlobSize = 1024;
    constexpr size_t maxBase64Size = ((dotBlobSize + 2) / 3) * 4;
    if (dotDataBase64.size() > maxBase64Size)
    {
        BMCWEB_LOG_ERROR(
            "DOT Recovery: DOTData size {} exceeds maximum allowed size {}",
            dotDataBase64.size(), maxBase64Size);
        messages::actionParameterValueFormatError(
            asyncResp->res, nlohmann::json(dotDataBase64), "DOTData",
            "NvidiaDOT.RecoverDOT");
        return;
    }

    std::string dotDataBinary;
    if (!crow::utility::base64Decode(dotDataBase64, dotDataBinary))
    {
        BMCWEB_LOG_ERROR("DOT Recovery: Failed to decode base64 DOT data");
        messages::actionParameterValueFormatError(
            asyncResp->res, nlohmann::json(dotDataBase64), "DOTData",
            "NvidiaDOT.RecoverDOT");
        return;
    }

    std::shared_ptr<MemoryFD> dotDataMemfd;
    try
    {
        dotDataMemfd = std::make_shared<MemoryFD>();
        std::vector<uint8_t> data(dotDataBinary.begin(), dotDataBinary.end());
        dotDataMemfd->write(data);
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR(
            "DOT Recovery: Failed to create memfd for DOT data: {}", e.what());
        messages::internalError(asyncResp->res);
        return;
    }

    constexpr std::array<std::string_view, 1> interfaces = {dot::dotActionIntf};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/DOT", 0, interfaces,
        std::bind_front(afterDOTRecoveryServiceDiscovery, asyncResp,
                        componentId, dotDataMemfd));
}

/**
 * @brief Handle DOT Disable action request
 *
 * Entry point for DOT Disable action endpoint. Validates the request,
 * chassis, and component before processing the Disable operation that
 * disables Device Ownership Transfer (DOT) functionality completely.
 *
 * @param app Crow application reference
 * @param req HTTP request object containing action parameters
 * @param asyncResp Async response object for the operation result
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleDOTDisableAction(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    BMCWEB_LOG_DEBUG(
        "DOT Disable action called - Chassis: '{}', Component: '{}'", chassisId,
        componentId);

    if (componentId.empty())
    {
        BMCWEB_LOG_ERROR("DOT Disable: componentId is empty");
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentId);
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(afterChassisValidationForDOTDisable, asyncResp,
                        std::ref(req), chassisId, componentId));
}

/**
 * @brief Handle DOT Override action request
 *
 * Entry point for DOT Override action endpoint. Validates the request,
 * chassis, and component before processing the Override operation that
 * resets ownership state when valid DOT data can no longer be recovered.
 *
 * @param app Crow application reference
 * @param req HTTP request object containing action parameters
 * @param asyncResp Async response object for the operation result
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleDOTOverrideAction(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    BMCWEB_LOG_DEBUG(
        "DOT Override action called - Chassis: '{}', Component: '{}'",
        chassisId, componentId);

    if (componentId.empty())
    {
        BMCWEB_LOG_ERROR("DOT Override: componentId is empty");
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentId);
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(afterChassisValidationForDOTOverride, asyncResp,
                        std::ref(req), chassisId, componentId));
}

/**
 * @brief Handle DOT Recovery action request
 *
 * Entry point for DOT Recovery action endpoint. Validates the request,
 * chassis, and component before processing the Recovery operation that
 * recovers corrupted DOT data using backup data.
 *
 * @param app Crow application reference
 * @param req HTTP request object containing action parameters
 * @param asyncResp Async response object for the operation result
 * @param chassisId The chassis identifier from the URI
 * @param componentId The trusted component identifier from the URI
 */
inline void handleDOTRecoverDOTAction(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    BMCWEB_LOG_DEBUG(
        "DOT Recovery action called - Chassis: '{}', Component: '{}'",
        chassisId, componentId);

    if (componentId.empty())
    {
        BMCWEB_LOG_ERROR("DOT Recovery: componentId is empty");
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentId);
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(afterChassisValidationForDOTRecovery, asyncResp,
                        std::ref(req), chassisId, componentId));
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
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/RecoverDOTActionInfo")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleDOTRecoverDOTActionInfo, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/Actions/NvidiaDOT.Install")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleDOTInstallAction, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/Actions/NvidiaDOT.Lock")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleDOTLockAction, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/Actions/NvidiaDOT.CAKRotate")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleDOTCAKRotateAction, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/Actions/NvidiaDOT.UnlockChallenge")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleDOTUnlockChallengeAction, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/Actions/NvidiaDOT.Unlock")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleDOTUnlockAction, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/Actions/NvidiaDOT.GetInfo")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleDOTGetInfoAction, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/Actions/NvidiaDOT.Disable")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleDOTDisableAction, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/Actions/NvidiaDOT.Override")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleDOTOverrideAction, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Oem/Nvidia/DOT/Actions/NvidiaDOT.RecoverDOT")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleDOTRecoverDOTAction, std::ref(app)));
}

} // namespace redfish
