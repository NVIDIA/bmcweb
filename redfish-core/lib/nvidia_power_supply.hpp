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
#include "async_resp.hpp"
#include "error_messages.hpp"
#include "http_request.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/nvidia_power_supply_utils.hpp"

#include <memory>
#include <string>

namespace redfish
{

inline void handlePowerSupplyMetricsGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& powerSupplyId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(
            redfish::nvidia_power_supply_utils::doPowerSupplyMetricsGet,
            asyncResp, chassisId, powerSupplyId));
}

inline void requestRoutesPowerSupplyMetrics(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/PowerSubsystem/PowerSupplies/<str>/Metrics/")
        .privileges(redfish::privileges::getPowerSupplyMetrics)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePowerSupplyMetricsGet, std::ref(app)));
}

} // namespace redfish
