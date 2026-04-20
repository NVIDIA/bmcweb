// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http_request.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "shmem_utils.hpp"
#include "telemetry_readings.hpp"
#include "thermal_metrics.hpp"
#include "utils/collection.hpp"
#include "utils/metric_report_utils.hpp"
#include "utils/nvidia_thermal_metrics_utils.hpp"
#include "utils/nvidia_time_utils.hpp"
#include "utils/telemetry_utils.hpp"

#include <asm-generic/errno.h>

#include <boost/beast/http/verb.hpp>
#include <boost/url/format.hpp>
#include <boost/url/url.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/property.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace redfish
{

inline void requestRoutesMetricReportCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/TelemetryService/MetricReports/")
        .privileges(redfish::privileges::getMetricReportCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                asyncResp->res.jsonValue["@odata.type"] =
                    "#MetricReportCollection.MetricReportCollection";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/TelemetryService/MetricReports";
                asyncResp->res.jsonValue["Name"] = "Metric Report Collection";

                // Nvidia code starts here
                if constexpr (BMCWEB_SHMEM_PLATFORM_METRICS)
                {
                    redfish::shmem::getShmemMetricsReportCollection(
                        asyncResp, "MetricReports");
                    return;
                }
                // Nvidia code ends here

                constexpr std::array<std::string_view, 1> interfaces{
                    telemetry::reportInterface};
                collection_util::getCollectionMembers(
                    asyncResp,
                    boost::urls::url(
                        "/redfish/v1/TelemetryService/MetricReports"),
                    interfaces,
                    "/xyz/openbmc_project/Telemetry/Reports/TelemetryService");
            });
}

inline void requestRoutesMetricReport(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/TelemetryService/MetricReports/<str>/")
        .privileges(redfish::privileges::getMetricReport)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& id) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                // Nvidia code starts here
                if constexpr (BMCWEB_SHMEM_PLATFORM_METRICS)
                {
                    const uint64_t requestTimestamp = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count());
                    BMCWEB_LOG_DEBUG("Request submitted at {}",
                                     requestTimestamp);
                    redfish::shmem::getShmemPlatformMetrics(asyncResp, id,
                                                            requestTimestamp);
                    return;
                }
                // Nvidia code ends here

                const std::string reportPath = telemetry::getDbusReportPath(id);
                dbus::utility::async_method_call(
                    asyncResp,
                    [asyncResp, id,
                     reportPath](const boost::system::error_code& ec) {
                        if (ec.value() == EBADR ||
                            ec == boost::system::errc::host_unreachable)
                        {
                            messages::resourceNotFound(asyncResp->res,
                                                       "MetricReport", id);
                            return;
                        }
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR("respHandler DBus error {}", ec);
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        dbus::utility::getProperty<
                            telemetry::TimestampReadings>(
                            telemetry::service, reportPath,
                            telemetry::reportInterface, "Readings",
                            [asyncResp,
                             id](const boost::system::error_code& ec2,
                                 const telemetry::TimestampReadings& ret) {
                                if (ec2)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "respHandler DBus error {}", ec2);
                                    messages::internalError(asyncResp->res);
                                    return;
                                }

                                telemetry::fillReport(asyncResp->res.jsonValue,
                                                      id, ret);
                            });
                    },
                    telemetry::service, reportPath, telemetry::reportInterface,
                    "Update");
            });
}
} // namespace redfish
