#pragma once

#include "thermal_metrics.hpp"
#include "utils/collection.hpp"
#include "utils/telemetry_utils.hpp"

#include <app.hpp>
#include <registries/privilege_registry.hpp>

namespace redfish
{

namespace telemetry
{

using Readings =
    std::vector<std::tuple<std::string, std::string, double, uint64_t>>;
using TimestampReadings = std::tuple<uint64_t, Readings>;

inline nlohmann::json toMetricValues(const Readings& readings)
{
    nlohmann::json metricValues = nlohmann::json::array_t();

    for (auto& [id, metadata, sensorValue, timestamp] : readings)
    {
        metricValues.push_back({
            {"MetricId", id},
            {"MetricProperty", metadata},
            {"MetricValue", std::to_string(sensorValue)},
            {"Timestamp", crow::utility::getDateTimeUint(timestamp)},
        });
    }

    return metricValues;
}

inline bool fillReport(nlohmann::json& json, const std::string& id,
                       const std::variant<TimestampReadings>& var)
{
    json["@odata.type"] = "#MetricReport.v1_3_0.MetricReport";
    json["@odata.id"] = telemetry::metricReportUri + std::string("/") + id;
    json["Id"] = id;
    json["Name"] = id;
    json["MetricReportDefinition"]["@odata.id"] =
        telemetry::metricReportDefinitionUri + std::string("/") + id;

    const TimestampReadings* timestampReadings =
        std::get_if<TimestampReadings>(&var);
    if (!timestampReadings)
    {
        BMCWEB_LOG_ERROR << "Property type mismatch or property is missing";
        return false;
    }

    const auto& [timestamp, readings] = *timestampReadings;
    json["Timestamp"] = crow::utility::getDateTimeUint(timestamp);
    json["MetricValues"] = toMetricValues(readings);
    return true;
}
} // namespace telemetry

inline void requestRoutesMetricReportCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/TelemetryService/MetricReports/")
        .privileges(redfish::privileges::getMetricReportCollection)
        .methods(
            boost::beast::http::verb::
                get)([](const crow::Request&,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            asyncResp->res.jsonValue["@odata.type"] =
                "#MetricReportCollection.MetricReportCollection";
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/TelemetryService/MetricReports";
            asyncResp->res.jsonValue["Name"] = "Metric Report Collection";
#ifdef BMCWEB_ENABLE_PLATFORM_METRICS
            nlohmann::json& addMembers = asyncResp->res.jsonValue["Members"];
            addMembers.push_back(
                {{"@odata.id",
                  "/redfish/v1/TelemetryService/MetricReports/PlatformMetrics"}});
            asyncResp->res.jsonValue["Members@odata.count"] = addMembers.size();
            return;
#endif
            const std::vector<const char*> interfaces{
                telemetry::reportInterface};
            collection_util::getCollectionMembers(
                asyncResp, telemetry::metricReportUri, interfaces,
                "/xyz/openbmc_project/Telemetry/Reports/TelemetryService");
        });
}

#ifdef BMCWEB_ENABLE_PLATFORM_METRICS
inline void
    getPlatformMetrics(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& chassisId)
{
    const std::array<const char*, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Chassis"};
    auto respHandler = [asyncResp, chassisId](
                           const boost::system::error_code ec,
                           const std::vector<std::string>& chassisPaths) {
        if (ec)
        {
            BMCWEB_LOG_ERROR << "getPlatformMetrics respHandler DBUS error: "
                             << ec;
            messages::internalError(asyncResp->res);
            return;
        }

        for (const std::string& chassisPath : chassisPaths)
        {
            sdbusplus::message::object_path path(chassisPath);
            const std::string& chassisName = path.filename();
            if (chassisName.empty())
            {
                BMCWEB_LOG_ERROR << "Failed to find '/' in " << chassisPath;
                continue;
            }
            if (chassisName != chassisId)
            {
                continue;
            }
            asyncResp->res.jsonValue["@odata.type"] =
                "#MetricReport.v1_3_0.MetricReport";
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/TelemetryService/MetricReports/PlatformMetrics";
            asyncResp->res.jsonValue["Id"] = "PlatformMetrics";
            asyncResp->res.jsonValue["Name"] = "PlatformMetrics";
            asyncResp->res.jsonValue["MetricValues"] = nlohmann::json::array();
            // Identify sensor services for sensor readings
            processSensorServices(asyncResp, chassisPath, "all");
            return;
        }
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
    };
    // Get the Chassis Collection
    crow::connections::systemBus->async_method_call(
        respHandler, "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/inventory", 0, interfaces);
}

inline void requestRoutesPlatformMetricReport(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/TelemetryService/MetricReports/PlatformMetrics/")
        .privileges(redfish::privileges::getMetricReport)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                getPlatformMetrics(asyncResp, "Baseboard");
            });
}
#endif

inline void requestRoutesMetricReport(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/TelemetryService/MetricReports/<str>/")
        .privileges(redfish::privileges::getMetricReport)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& id) {
                const std::string reportPath = telemetry::getDbusReportPath(id);
                crow::connections::systemBus->async_method_call(
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
                            BMCWEB_LOG_ERROR << "respHandler DBus error " << ec;
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        crow::connections::systemBus->async_method_call(
                            [asyncResp,
                             id](const boost::system::error_code ec,
                                 const std::variant<
                                     telemetry::TimestampReadings>& ret) {
                                if (ec)
                                {
                                    BMCWEB_LOG_ERROR
                                        << "respHandler DBus error " << ec;
                                    messages::internalError(asyncResp->res);
                                    return;
                                }

                                if (!telemetry::fillReport(
                                        asyncResp->res.jsonValue, id, ret))
                                {
                                    messages::internalError(asyncResp->res);
                                }
                            },
                            telemetry::service, reportPath,
                            "org.freedesktop.DBus.Properties", "Get",
                            telemetry::reportInterface, "Readings");
                    },
                    telemetry::service, reportPath, telemetry::reportInterface,
                    "Update");
            });
}
} // namespace redfish
