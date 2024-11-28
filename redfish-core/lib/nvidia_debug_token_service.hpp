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
#include "debug_token/erase_policy.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"

namespace redfish
{

inline void handleDebugTokenServiceGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    using namespace debug_token;
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    std::string uri =
        std::format("/redfish/v1/Systems/{}/Oem/Nvidia/DebugTokenService",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME);
    asyncResp->res.jsonValue["@odata.id"] = uri;
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaDebugTokenService.v1_0_0.NvidiaDebugTokenService";
    asyncResp->res.jsonValue["Id"] = "DebugTokenService";
    asyncResp->res.jsonValue["Name"] = "DebugTokenService";
    asyncResp->res.jsonValue["Description"] = "NVIDIA Debug Token Service";

    auto policyCallback = [asyncResp](std::string policy) {
        asyncResp->res.jsonValue["ErasePolicy"] = policy;
    };
    getErasePolicy(asyncResp, policyCallback);
}

inline void handleDebugTokenServicePatch(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    std::optional<std::string> debugTokenErasePolicy;
    if (!json_util::readJsonPatch(req, asyncResp->res, "ErasePolicy",
                                  debugTokenErasePolicy))
    {
        return;
    }
    if (debugTokenErasePolicy)
    {
        debug_token::setErasePolicy(asyncResp, *debugTokenErasePolicy);
    }
}

inline void requestRoutesDebugTokenService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Oem/Nvidia/DebugTokenService/")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleDebugTokenServiceGet, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Oem/Nvidia/DebugTokenService/")
        .privileges(redfish::privileges::patchComputerSystem)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleDebugTokenServicePatch, std::ref(app)));
}

} // namespace redfish
