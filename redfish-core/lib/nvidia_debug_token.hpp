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

#include "debug_token/targeted_operation.hpp"
#include "nvidia_messages.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/json_utils.hpp"

#include <map>
#include <memory>

namespace redfish
{

namespace debug_token
{

static std::map<std::string, std::unique_ptr<TargetedOperationHandler>>
    tokenOpMap;

inline void
    getChassisDebugToken(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& chassisId)
{
    constexpr std::array<std::string_view, 1> interfaces = {debugTokenIntf};
    dbus::utility::getSubTreePaths(
        std::string(debugTokenBasePath), 0, interfaces,
        [asyncResp,
         chassisId](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreePathsResponse& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("GetSubTreePaths error: {}", ec);
                return;
            }
            if (resp.size() == 0)
            {
                return;
            }
            for (const auto& path : resp)
            {
                auto pathChassis =
                    std::filesystem::path(path).filename().string();
                if (chassisId == pathChassis)
                {
                    int timeout;
                    auto currentOp = tokenOpMap.find(chassisId);
                    if (currentOp != tokenOpMap.end() &&
                        !currentOp->second->finished(timeout))
                    {
                        return;
                    }
                    auto resultCallback = [asyncResp,
                                           chassisId](EndpointState state,
                                                      TargetedOperationResult) {
                        if (state != EndpointState::DebugTokenUnsupported)
                        {
                            auto& oemNvidia =
                                asyncResp->res.jsonValue["Oem"]["Nvidia"];
                            oemNvidia["@odata.type"] =
                                "#NvidiaChassis.v1_3_0.NvidiaChassis";
                            oemNvidia["DebugToken"]["@odata.id"] =
                                boost::urls::format(
                                    "/redfish/v1/Chassis/{}/Oem/Nvidia/DebugToken",
                                    chassisId);
                        }
                    };
                    std::string tokenType = "CRCS";
                    auto op = std::make_unique<TargetedOperationHandler>(
                        chassisId, TargetedOperation::GetTokenStatus,
                        resultCallback, tokenType);
                    if (currentOp != tokenOpMap.end())
                    {
                        currentOp->second = std::move(op);
                    }
                    else
                    {
                        tokenOpMap.insert(
                            std::make_pair(chassisId, std::move(op)));
                    }
                }
            }
        });
}

inline void handleDebugTokenResourceInfo(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    using namespace std::string_literals;
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    auto resultCallback =
        [asyncResp, chassisId, resUri{std::string(req.url().buffer())}](
            EndpointState state, TargetedOperationResult result) {
            if (state == EndpointState::DebugTokenUnsupported)
            {
                messages::debugTokenUnsupported(asyncResp->res, chassisId);
                return;
            }
            NsmTokenStatus* tokenStatus = std::get_if<NsmTokenStatus>(&result);
            if (!tokenStatus)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            auto& resJson = asyncResp->res.jsonValue;
            nsmTokenStatusToJson(*tokenStatus, resJson);
            resJson["@odata.type"] =
                "#NvidiaDebugToken.v1_0_0.NvidiaDebugToken";
            resJson["@odata.id"] = resUri;
            resJson["Id"] = "DebugToken";
            resJson["Name"] = chassisId + " Debug Token Resource"s;

            auto& actions = resJson["Actions"];
            auto& generateAction = actions["#NvidiaDebugToken.GenerateToken"];
            generateAction["target"] =
                resUri + "/Actions/NvidiaDebugToken.GenerateToken"s;
            generateAction["@Redfish.ActionInfo"] =
                resUri + "/GenerateTokenActionInfo"s;
            auto& installAction = actions["#NvidiaDebugToken.InstallToken"];
            installAction["target"] =
                resUri + "/Actions/NvidiaDebugToken.InstallToken"s;
            installAction["@Redfish.ActionInfo"] =
                resUri + "/InstallTokenActionInfo"s;
            auto& disableAction = actions["#NvidiaDebugToken.DisableToken"];
            disableAction["target"] =
                resUri + "/Actions/NvidiaDebugToken.DisableToken"s;
        };
    chassis_utils::getValidChassisID(
        asyncResp, chassisId,
        [asyncResp, chassisId,
         resultCallback](std::optional<std::string> valid) {
            if (!valid.has_value())
            {
                messages::resourceNotFound(asyncResp->res, "Chassis",
                                           chassisId);
                return;
            }
            int timeout = 0;
            auto currentOp = tokenOpMap.find(chassisId);
            if (currentOp != tokenOpMap.end() &&
                !currentOp->second->finished(timeout))
            {
                messages::serviceTemporarilyUnavailable(
                    asyncResp->res, std::to_string(timeout));
                return;
            }
            std::string tokenType = "CRCS";
            auto op = std::make_unique<TargetedOperationHandler>(
                chassisId, TargetedOperation::GetTokenStatus, resultCallback,
                tokenType);
            if (currentOp != tokenOpMap.end())
            {
                currentOp->second = std::move(op);
            }
            else
            {
                tokenOpMap.insert(std::make_pair(chassisId, std::move(op)));
            }
        });
}

inline void handleGenerateTokenActionInfo(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    chassis_utils::getValidChassisID(
        asyncResp, chassisId,
        [asyncResp, chassisId, resUri{std::string(req.url().buffer())}](
            std::optional<std::string> valid) {
            if (!valid.has_value())
            {
                messages::resourceNotFound(asyncResp->res, "Chassis",
                                           chassisId);
                return;
            }
            asyncResp->res.jsonValue["@odata.type"] =
                "#ActionInfo.v1_2_0.ActionInfo";
            asyncResp->res.jsonValue["@odata.id"] = resUri;
            asyncResp->res.jsonValue["Id"] = "GenerateTokenActionInfo";
            asyncResp->res.jsonValue["Name"] = "GenerateToken Action Info";

            nlohmann::json::array_t parameters;
            nlohmann::json::object_t parameter;
            parameter["Name"] = "TokenType";
            parameter["Required"] = true;
            parameter["DataType"] = "String";
            nlohmann::json::array_t allowed;
            allowed.emplace_back("CRCS");
            parameter["AllowableValues"] = std::move(allowed);
            parameters.emplace_back(std::move(parameter));

            asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
        });
}

inline void handleInstallTokenActionInfo(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    chassis_utils::getValidChassisID(
        asyncResp, chassisId,
        [asyncResp, chassisId, resUri{std::string(req.url().buffer())}](
            std::optional<std::string> valid) {
            if (!valid.has_value())
            {
                messages::resourceNotFound(asyncResp->res, "Chassis",
                                           chassisId);
                return;
            }
            asyncResp->res.jsonValue["@odata.type"] =
                "#ActionInfo.v1_2_0.ActionInfo";
            asyncResp->res.jsonValue["@odata.id"] = resUri;
            asyncResp->res.jsonValue["Id"] = "InstallTokenActionInfo";
            asyncResp->res.jsonValue["Name"] = "InstallToken Action Info";

            nlohmann::json::array_t parameters;
            nlohmann::json::object_t parameter;
            parameter["Name"] = "TokenData";
            parameter["Required"] = true;
            parameter["DataType"] = "String";
            parameters.emplace_back(std::move(parameter));

            asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
        });
}

inline void handleDisableTokens(
    const crow::Request&, const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, TargetedOperationArgument& arg,
    TargetedOperationResultCallback& cb)
{
    arg = std::monostate();
    cb = [asyncResp, chassisId](EndpointState state, TargetedOperationResult) {
        if (state == EndpointState::DebugTokenUnsupported)
        {
            messages::debugTokenUnsupported(asyncResp->res, chassisId);
            return;
        }
        if (state != EndpointState::Error)
        {
            messages::success(asyncResp->res);
            return;
        }
        messages::internalError(asyncResp->res);
    };
}

inline void handleGenerateTokenRequest(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, TargetedOperationArgument& arg,
    TargetedOperationResultCallback& cb)
{
    std::string tokenType;
    if (!redfish::json_util::readJsonAction(req, asyncResp->res, "TokenType",
                                            tokenType))
    {
        return;
    }
    if (tokenType != "CRCS")
    {
        messages::actionParameterValueNotInList(asyncResp->res, tokenType,
                                                "TokenType", "GenerateToken");
        return;
    }
    arg = tokenType;
    cb = [asyncResp,
          chassisId](EndpointState state, TargetedOperationResult result) {
        if (state == EndpointState::TokenInstalled)
        {
            messages::debugTokenAlreadyInstalled(asyncResp->res, chassisId);
            return;
        }
        if (state == EndpointState::DebugTokenUnsupported)
        {
            messages::debugTokenUnsupported(asyncResp->res, chassisId);
            return;
        }
        std::vector<uint8_t>* request =
            std::get_if<std::vector<uint8_t>>(&result);
        if (!request)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        std::string_view binaryData(
            reinterpret_cast<const char*>(request->data()), request->size());
        asyncResp->res.jsonValue["Token"] =
            crow::utility::base64encode(binaryData);
    };
}

inline void handleInstallToken(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, TargetedOperationArgument& arg,
    TargetedOperationResultCallback& cb)
{
    std::string tokenData;
    if (!redfish::json_util::readJsonAction(req, asyncResp->res, "TokenData",
                                            tokenData))
    {
        return;
    }
    std::string binaryData;
    if (!crow::utility::base64Decode(tokenData, binaryData))
    {
        messages::actionParameterValueFormatError(asyncResp->res, tokenData,
                                                  "TokenData", "InstallToken");
        return;
    }
    arg = std::vector<uint8_t>(binaryData.begin(), binaryData.end());
    cb = [asyncResp, chassisId](EndpointState state, TargetedOperationResult) {
        if (state == EndpointState::DebugTokenUnsupported)
        {
            messages::debugTokenUnsupported(asyncResp->res, chassisId);
            return;
        }
        if (state != EndpointState::Error)
        {
            messages::success(asyncResp->res);
            return;
        }
        messages::internalError(asyncResp->res);
    };
}

template <TargetedOperation operationType>
inline void
    handleTargetedTokenOp(App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    chassis_utils::getValidChassisID(
        asyncResp, chassisId,
        [req, asyncResp, chassisId](std::optional<std::string> valid) {
            if (!valid.has_value())
            {
                messages::resourceNotFound(asyncResp->res, "Chassis",
                                           chassisId);
                return;
            }
            int timeout = 0;
            auto currentOp = tokenOpMap.find(chassisId);
            if (currentOp != tokenOpMap.end() &&
                !currentOp->second->finished(timeout))
            {
                messages::serviceTemporarilyUnavailable(
                    asyncResp->res, std::to_string(timeout));
                return;
            }
            TargetedOperationArgument arg;
            TargetedOperationResultCallback cb;
            if (operationType == TargetedOperation::DisableTokens)
            {
                handleDisableTokens(req, asyncResp, chassisId, arg, cb);
            }
            if (operationType == TargetedOperation::GenerateTokenRequest)
            {
                handleGenerateTokenRequest(req, asyncResp, chassisId, arg, cb);
            }
            if (operationType == TargetedOperation::InstallToken)
            {
                handleInstallToken(req, asyncResp, chassisId, arg, cb);
            }
            if (cb)
            {
                auto op = std::make_unique<TargetedOperationHandler>(
                    chassisId, operationType, std::move(cb), arg);
                if (currentOp != tokenOpMap.end())
                {
                    currentOp->second = std::move(op);
                }
                else
                {
                    tokenOpMap.insert(std::make_pair(chassisId, std::move(op)));
                }
            }
        });
}

} // namespace debug_token

inline void requestRoutesChassisDebugToken(App& app)
{
    using namespace debug_token;
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/DebugToken")
        .privileges(redfish::privileges::getChassis)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleDebugTokenResourceInfo, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/DebugToken"
                      "/GenerateTokenActionInfo")
        .privileges(redfish::privileges::getChassis)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleGenerateTokenActionInfo, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/DebugToken"
                      "/InstallTokenActionInfo")
        .privileges(redfish::privileges::getChassis)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleInstallTokenActionInfo, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/DebugToken"
                      "/Actions/NvidiaDebugToken.DisableToken")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleTargetedTokenOp<TargetedOperation::DisableTokens>,
            std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/DebugToken"
                      "/Actions/NvidiaDebugToken.GenerateToken")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleTargetedTokenOp<TargetedOperation::GenerateTokenRequest>,
            std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/DebugToken"
                      "/Actions/NvidiaDebugToken.InstallToken")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleTargetedTokenOp<TargetedOperation::InstallToken>,
            std::ref(app)));
}

} // namespace redfish
