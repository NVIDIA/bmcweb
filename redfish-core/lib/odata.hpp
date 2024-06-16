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
#include "error_messages.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utility.hpp"

#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <ranges>
#include <string>
#include <string_view>

namespace redfish
{

inline void redfishOdataGet(const crow::Request& /*req*/,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    nlohmann::json::object_t obj;
    obj["@odata.context"] = "/redfish/v1/$metadata";
    nlohmann::json::array_t value;
    for (std::string_view service :
         {"$metadata", "odata", "JsonSchemas", "Service", "ServiceRoot",
          "Systems", "Chassis", "Managers", "SessionService", "AccountService",
          "UpdateService"})
    {
        nlohmann::json::object_t serviceObj;
        serviceObj["kind"] = "Singleton";
        serviceObj["name"] = "$metadata";
        boost::urls::url url = boost::urls::format("/redfish/v1/{}", service);
        if (service == "Service")
        {
            url = boost::urls::url("/redfish/v1");
        }
        serviceObj["url"] = url;
        value.emplace_back(std::move(serviceObj));
    }

    obj["value"] = std::move(value);

    asyncResp->res.jsonValue = std::move(obj);
}

inline void requestRoutesOdata(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/odata/")
        .methods(boost::beast::http::verb::get)(redfishOdataGet);
}

} // namespace redfish
