// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "app.hpp"
#include "async_resp.hpp"
#include "error_messages.hpp"
#include "http_request.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "str_utility.hpp"
#include "utils/chassis_utils.hpp"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/url/format.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/environment_util.hpp>
#include <utils/json_utils.hpp>
#include <utils/processor_utils.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace redfish
{

inline void handleEnvironmentMetricsHead(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    auto respHandler = [asyncResp, chassisId](
                           const std::optional<std::string>& validChassisPath) {
        if (!validChassisPath)
        {
            messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
            return;
        }

        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/EnvironmentMetrics/EnvironmentMetrics.json>; rel=describedby");
    };

    redfish::chassis_utils::getValidChassisPath(asyncResp, chassisId,
                                                std::move(respHandler));
}

inline void handleEnvironmentMetricsGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    auto respHandler = [asyncResp, chassisId](
                           const std::optional<std::string>& validChassisPath) {
        if (!validChassisPath)
        {
            messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
            return;
        }

        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/EnvironmentMetrics/EnvironmentMetrics.json>; rel=describedby");
        asyncResp->res.jsonValue["@odata.type"] =
            "#EnvironmentMetrics.v1_3_0.EnvironmentMetrics";
        asyncResp->res.jsonValue["Name"] = "Chassis Environment Metrics";
        asyncResp->res.jsonValue["Id"] = "EnvironmentMetrics";
        asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
            "/redfish/v1/Chassis/{}/EnvironmentMetrics", chassisId);

        // Nvidia Added Code Start
        if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
        {
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaEnvironmentMetrics.v1_2_0.NvidiaEnvironmentMetrics";
        }
        redfish::nvidia_env_utils::getfanSpeedsPercent(asyncResp, chassisId);
        redfish::nvidia_env_utils::getPowerWattsEnergyJoules(
            asyncResp, chassisId, *validChassisPath);

        // TODO: Remove interfaces from here
        const std::array<std::string_view, 2> interfaces = {
            "xyz.openbmc_project.Inventory.Item.Board",
            "xyz.openbmc_project.Inventory.Item.Chassis"};
        redfish::nvidia_env_utils::getPowerAndControlData(asyncResp, chassisId,
                                                          interfaces);
        // Nvidia Added Code End
    };

    redfish::chassis_utils::getValidChassisPath(asyncResp, chassisId,
                                                std::move(respHandler));
}

// Nvidia Added Code Start
inline void handleEnvironmentMetricsPatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    redfish::nvidia_env_utils::handleEnvironmentMetricsPatchBody(
        req, asyncResp, chassisId);
}
// Nvidia Added Code End

inline void requestRoutesEnvironmentMetrics(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/EnvironmentMetrics/")
        .privileges(redfish::privileges::headEnvironmentMetrics)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleEnvironmentMetricsHead, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/EnvironmentMetrics/")
        .privileges(redfish::privileges::getEnvironmentMetrics)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleEnvironmentMetricsGet, std::ref(app)));

    // Nvidia Added Code Start
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/EnvironmentMetrics/")
        .privileges(redfish::privileges::patchChassis)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleEnvironmentMetricsPatch, std::ref(app)));
    // Nvidia Added Code End
}

} // namespace redfish
