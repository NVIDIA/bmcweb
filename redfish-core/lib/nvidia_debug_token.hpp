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

#include "debug_token/nsm_async.hpp"
#include "nvidia_messages.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/json_utils.hpp"

#include <memory>

namespace redfish
{

namespace debug_token
{

inline void getChassisDebugToken(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
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
                BMCWEB_LOG_DEBUG("GetSubTreePaths error: {}", ec);
                return;
            }
            if (resp.empty())
            {
                return;
            }
            for (const auto& path : resp)
            {
                auto pathChassis =
                    std::filesystem::path(path).filename().string();
                if (chassisId == pathChassis)
                {
                    auto resultCallback = [asyncResp, chassisId](
                                              const nsm_async::single_op::
                                                  Result& result) {
                        const auto& [state, output] = result;
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
                    nsm_async::single_op::Handler::startOperation(
                        chassisId,
                        nsm_async::single_op::Operation::GetTokenStatus,
                        tokenType, resultCallback);
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

    std::string resUri =
        std::format("/redfish/v1/Chassis/{}/Oem/Nvidia/DebugToken", chassisId);
    auto resultCallback = [asyncResp, chassisId,
                           resUri](const nsm_async::single_op::Result& result) {
        const auto& [state, output] = result;
        if (state == EndpointState::DebugTokenUnsupported)
        {
            messages::debugTokenUnsupported(asyncResp->res, chassisId);
            return;
        }
        const auto* tokenStatus = std::get_if<NsmTokenStatus>(&output);
        if (!tokenStatus)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        auto& resJson = asyncResp->res.jsonValue;
        nsmTokenStatusToJson(*tokenStatus, resJson);
        resJson["@odata.type"] = "#NvidiaDebugToken.v1_1_0.NvidiaDebugToken";
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
         resultCallback](const std::optional<std::string>& valid) {
            if (!valid.has_value())
            {
                messages::resourceNotFound(asyncResp->res, "Chassis",
                                           chassisId);
                return;
            }
            std::string tokenType = "CRCS";
            nsm_async::single_op::Handler::startOperation(
                chassisId, nsm_async::single_op::Operation::GetTokenStatus,
                tokenType, resultCallback);
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
            const std::optional<std::string>& valid) {
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
            const std::optional<std::string>& valid) {
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
    const crow::Request& /*unused*/,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, nsm_async::single_op::Argument& arg,
    nsm_async::single_op::ResultCallback& cb)
{
    arg = std::monostate();
    cb = [asyncResp, chassisId](const nsm_async::single_op::Result& result) {
        const auto& [state, output] = result;
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
    const std::string& chassisId, nsm_async::single_op::Argument& arg,
    nsm_async::single_op::ResultCallback& cb)
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
    cb = [asyncResp, chassisId](const nsm_async::single_op::Result& result) {
        const auto& [state, output] = result;
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
        const auto* request = std::get_if<std::vector<uint8_t>>(&output);
        if (!request)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        std::string binaryData(request->begin(), request->end());
        asyncResp->res.jsonValue["Token"] =
            crow::utility::base64encode(binaryData);
    };
}

inline void handleInstallToken(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, nsm_async::single_op::Argument& arg,
    nsm_async::single_op::ResultCallback& cb)
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
    cb = [asyncResp, chassisId](const nsm_async::single_op::Result& result) {
        const auto& [state, output] = result;
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

template <nsm_async::single_op::Operation operationType>
inline void handleTargetedTokenOp(
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
        [req, asyncResp, chassisId](const std::optional<std::string>& valid) {
            if (!valid.has_value())
            {
                messages::resourceNotFound(asyncResp->res, "Chassis",
                                           chassisId);
                return;
            }
            nsm_async::single_op::Argument arg;
            nsm_async::single_op::ResultCallback cb;
            if (operationType == nsm_async::single_op::Operation::DisableTokens)
            {
                handleDisableTokens(req, asyncResp, chassisId, arg, cb);
            }
            if (operationType ==
                nsm_async::single_op::Operation::GenerateTokenRequest)
            {
                handleGenerateTokenRequest(req, asyncResp, chassisId, arg, cb);
            }
            if (operationType == nsm_async::single_op::Operation::InstallToken)
            {
                handleInstallToken(req, asyncResp, chassisId, arg, cb);
            }
            if (cb)
            {
                nsm_async::single_op::Handler::startOperation(
                    chassisId, operationType, arg, cb);
            }
        });
}

} // namespace debug_token

inline void requestRoutesChassisDebugToken(App& app)
{
    using namespace debug_token;
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/DebugToken/")
        .privileges(redfish::privileges::getChassis)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleDebugTokenResourceInfo, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/DebugToken"
                      "/GenerateTokenActionInfo/")
        .privileges(redfish::privileges::getChassis)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleGenerateTokenActionInfo, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/DebugToken"
                      "/InstallTokenActionInfo/")
        .privileges(redfish::privileges::getChassis)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleInstallTokenActionInfo, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/DebugToken"
                      "/Actions/NvidiaDebugToken.DisableToken")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleTargetedTokenOp<
                                nsm_async::single_op::Operation::DisableTokens>,
                            std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/DebugToken"
                      "/Actions/NvidiaDebugToken.GenerateToken")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleTargetedTokenOp<
                nsm_async::single_op::Operation::GenerateTokenRequest>,
            std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/DebugToken"
                      "/Actions/NvidiaDebugToken.InstallToken")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleTargetedTokenOp<
                                nsm_async::single_op::Operation::InstallToken>,
                            std::ref(app)));
}

} // namespace redfish
