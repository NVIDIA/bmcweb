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
inline void doLeakDetection(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisId,
                            const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_WARNING("Not a valid chassis ID{}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }
    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/LeakDetection/LeakDetection.json>; rel=describedby");
    asyncResp->res.jsonValue["@odata.type"] =
        "#LeakDetection.v1_0_0.LeakDetection";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/ThermalSubsystem/LeakDetection", chassisId);

    asyncResp->res.jsonValue["Name"] = "Leak Detection Systems";
    asyncResp->res.jsonValue["Id"] = "LeakDetection";

    asyncResp->res
        .jsonValue["LeakDetectors"]["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/ThermalSubsystem/LeakDetection/LeakDetectors",
        chassisId);

    asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
    asyncResp->res.jsonValue["Status"]["Health"] = "OK";
#ifndef BMCWEB_DISABLE_HEALTH_ROLLUP
    asyncResp->res.jsonValue["Status"]["HealthRollup"] = "OK";
#endif // BMCWEB_DISABLE_HEALTH_ROLLUP
}

inline void
    handleLeakDetectionGet(App& app, const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(doLeakDetection, asyncResp, chassisId));
}

inline void
    handleLeakDetectionHead(App& app, const crow::Request& req,
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
            messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
            return;
        }
        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/LeakDetection/LeakDetection.json>; rel=describedby");
    });
}

inline void requestRoutesLeakDetection(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Chassis/<str>/ThermalSubsystem/LeakDetection/")
        .privileges(redfish::privileges::headLeakDetection)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleLeakDetectionHead, std::ref(app)));

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Chassis/<str>/ThermalSubsystem/LeakDetection/")
        .privileges(redfish::privileges::getLeakDetection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleLeakDetectionGet, std::ref(app)));
}

} // namespace redfish
