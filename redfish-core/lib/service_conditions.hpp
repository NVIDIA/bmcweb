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

#include "logging.hpp"

#include <app.hpp>
#include <registries/privilege_registry.hpp>
#include <utils/conditions_utils.hpp>
namespace redfish
{

inline void requestRoutesServiceConditions(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/ServiceConditions/")
        .privileges(redfish::privileges::privilegeSetLogin)
        .methods(boost::beast::http::verb::get)(
            []([[maybe_unused]] const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                asyncResp->res.jsonValue = {
                    {"@odata.type",
                     "#ServiceConditions.v1_0_0.ServiceConditions"},
                    {"@odata.id", "/redfish/v1/ServiceConditions"},
                    {"Id", "ServiceConditions"},
                    {"Name", "Redfish Service Conditions"}};
                asyncResp->res.jsonValue["Oem"] = nlohmann::json::object();
                if constexpr (!BMCWEB_DISABLE_HEALTH_ROLLUP)
                {
                    asyncResp->res.jsonValue["HealthRollup"] = "OK";
                }
                if constexpr (!BMCWEB_DISABLE_CONDITIONS_ARRAY)
                {
                    redfish::conditions_utils::populateServiceConditions(
                        asyncResp, std::string(""));
                }
            });
}

} // namespace redfish
