#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "dbus_utility.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "thermal_metrics.hpp"
#include "utils/collection.hpp"
#include "utils/metric_report_utils.hpp"
#include "utils/telemetry_utils.hpp"
#include "utils/time_utils.hpp"

#include <boost/url/format.hpp>
#include <sdbusplus/asio/property.hpp>

#include <array>
#include <chrono>
#include <string_view>

#ifdef BMCWEB_ENABLE_SHMEM_PLATFORM_METRICS
#include "shmem_utils.hpp"
#endif

namespace redfish
{

namespace telemetry
{
constexpr const char* metricReportDefinitionUriStr =
    "/redfish/v1/TelemetryService/MetricReportDefinitions";
constexpr const char* metricReportUri =
    "/redfish/v1/TelemetryService/MetricReports";

using Readings = std::vector<std::tuple<std::string, double, uint64_t>>;
using TimestampReadings = std::tuple<uint64_t, Readings>;

inline nlohmann::json toMetricValues(const Readings& readings)
{
    nlohmann::json metricValues = nlohmann::json::array_t();

    for (const auto& [metadata, sensorValue, timestamp] : readings)
    {
        nlohmann::json::object_t metricReport;
        metricReport["MetricProperty"] = metadata;
        metricReport["MetricValue"] = std::to_string(sensorValue);
        metricReport["Timestamp"] =
            redfish::time_utils::getDateTimeUintMs(timestamp);
        metricValues.emplace_back(std::move(metricReport));
    }

    return metricValues;
}

inline bool fillReport(nlohmann::json& json, const std::string& id,
                       const TimestampReadings& timestampReadings)
{
    json["@odata.type"] = "#MetricReport.v1_4_2.MetricReport";
    json["@odata.id"] = boost::urls::format(
        "/redfish/v1/TelemetryService/MetricReports/{}", id);
    json["Id"] = id;
    json["Name"] = id;
    json["MetricReportDefinition"]["@odata.id"] = boost::urls::format(
        "/redfish/v1/TelemetryService/MetricReportDefinitions/{}", id);

    const auto& [timestamp, readings] = timestampReadings;
    json["Timestamp"] = redfish::time_utils::getDateTimeUintMs(timestamp);
    json["MetricValues"] = toMetricValues(readings);
    return true;
}
} // namespace telemetry

inline void
    addMetricReportMembers(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](boost::system::error_code ec,
                    const std::vector<std::string>& metricPaths) mutable {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error: {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }
        nlohmann::json& addMembers = asyncResp->res.jsonValue["Members"];

        for (const std::string& object : metricPaths)
        {
            // Get the metric object
            std::string metricReportUriPath =
                "/redfish/v1/TelemetryService/MetricReports/";
            if (boost::ends_with(object, "platformmetrics"))
            {
                std::string uripath = metricReportUriPath;
                uripath += PLATFORMMETRICSID;
                if (!containsJsonObject(addMembers, "@odata.id", uripath))
                {
                    addMembers.push_back({{"@odata.id", uripath}});
                }
            }
            else if (boost::ends_with(object, "memory"))
            {
                std::string memoryMetricId = PLATFORMDEVICEPREFIX
                    "MemoryMetrics";
                memoryMetricId += "_0";
                std::string uripath = metricReportUriPath + memoryMetricId;
                addMembers.push_back({{"@odata.id", uripath}});
            }
            else if (boost::ends_with(object, "processors"))
            {
                std::string processorMetricId = PLATFORMDEVICEPREFIX
                    "ProcessorMetrics";
                processorMetricId += "_0";
                std::string uripath = metricReportUriPath + processorMetricId;
                addMembers.push_back({{"@odata.id", uripath}});

                std::string processorGpmMetricId = PLATFORMDEVICEPREFIX
                    "ProcessorGPMMetrics";
                processorGpmMetricId += "_0";
                std::string uripathGpm = metricReportUriPath +
                                         processorGpmMetricId;
                addMembers.push_back({{"@odata.id", uripathGpm}});

                std::string processorPortMetricId = PLATFORMDEVICEPREFIX
                    "ProcessorPortMetrics";
                processorPortMetricId += "_0";
                uripath = metricReportUriPath + processorPortMetricId;
                addMembers.push_back({{"@odata.id", uripath}});

                std::string processorPortGpmMetricId = PLATFORMDEVICEPREFIX
                    "ProcessorPortGPMMetrics";
                processorPortGpmMetricId += "_0";
                uripath = metricReportUriPath + processorPortGpmMetricId;
                addMembers.push_back({{"@odata.id", uripath}});
            }
            else if (boost::ends_with(object, "Switches"))
            {
                std::string switchMetricId = PLATFORMDEVICEPREFIX
                    "NVSwitchMetrics";
                switchMetricId += "_0";
                std::string uripath = metricReportUriPath + switchMetricId;
                addMembers.push_back({{"@odata.id", uripath}});

                std::string switchPortMetricId = PLATFORMDEVICEPREFIX
                    "NVSwitchPortMetrics";
                switchPortMetricId += "_0";
                uripath = metricReportUriPath + switchPortMetricId;
                addMembers.push_back({{"@odata.id", uripath}});
            }
        }
        asyncResp->res.jsonValue["Members@odata.count"] = addMembers.size();
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 1>{"xyz.openbmc_project.Sensor.Aggregation"});
}

inline void requestRoutesMetricReportCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/TelemetryService/MetricReports/")
        .privileges(redfish::privileges::getMetricReportCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
<<<<<<< HEAD
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        asyncResp->res.jsonValue["@odata.type"] =
            "#MetricReportCollection.MetricReportCollection";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/TelemetryService/MetricReports";
        asyncResp->res.jsonValue["Name"] = "Metric Report Collection";
#ifdef BMCWEB_ENABLE_PLATFORM_METRICS
#ifdef BMCWEB_ENABLE_SHMEM_PLATFORM_METRICS
        redfish::shmem::getShmemMetricsReportCollection(asyncResp,
                                                        "MetricReports");
#else
        addMetricReportMembers(asyncResp);
#endif
        return;
#endif
        constexpr std::array<std::string_view, 1> interfaces{
            telemetry::reportInterface};
        collection_util::getCollectionMembers(
            asyncResp,
            boost::urls::url("/redfish/v1/TelemetryService/MetricReports"),
            interfaces,
            "/xyz/openbmc_project/Telemetry/Reports/TelemetryService");
    });
=======
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                asyncResp->res.jsonValue["@odata.type"] =
                    "#MetricReportCollection.MetricReportCollection";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/TelemetryService/MetricReports";
                asyncResp->res.jsonValue["Name"] = "Metric Report Collection";
                constexpr std::array<std::string_view, 1> interfaces{
                    telemetry::reportInterface};
                collection_util::getCollectionMembers(
                    asyncResp,
                    boost::urls::url(
                        "/redfish/v1/TelemetryService/MetricReports"),
                    interfaces,
                    "/xyz/openbmc_project/Telemetry/Reports/TelemetryService");
            });
>>>>>>> origin/master
}
#ifdef BMCWEB_ENABLE_PLATFORM_METRICS
inline void getSensorMap(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& serviceName,
                         const std::string& objectPath,
                         const uint32_t& staleSensorUpperLimit,
                         const uint64_t& requestTimestamp)
{
    using sensorMap = std::map<
        std::string,
        std::tuple<std::variant<std::string, int, int16_t, int64_t, uint16_t,
                                uint32_t, uint64_t, double, bool>,
                   uint64_t, sdbusplus::message::object_path>>;

    sdbusplus::asio::getProperty<sensorMap>(
        *crow::connections::systemBus, serviceName, objectPath,
        "xyz.openbmc_project.Sensor.Aggregation", "SensorMetrics",
        [asyncResp, staleSensorUpperLimit,
         requestTimestamp](const boost::system::error_code ec,
                           const sensorMap& sensorMetrics) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error");
            messages::internalError(asyncResp->res);
            return;
        }
        nlohmann::json& resArray = asyncResp->res.jsonValue["MetricValues"];

        for (const auto& i : sensorMetrics)
        {
            nlohmann::json thisMetric = nlohmann::json::object();
            std::string sensorName = i.first;
            auto data = i.second;
            auto var = std::get<0>(data);
            const double* reading = std::get_if<double>(&var);
            thisMetric["MetricValue"] = std::to_string(*reading);
            // sensorUpdatetimeSteadyClock is in ms
            const uint64_t sensorUpdatetimeSteadyClock = std::get<1>(data);
            /*
            the complex code here converts sensorUpdatetimeSteadyClock
            from std::chrono::steady_clock to std::chrono::system_clock
            */
            const uint64_t sensorUpdatetimeSystemClock =
                static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count()) -
                static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count()) +
                sensorUpdatetimeSteadyClock;
            thisMetric["Timestamp"] = redfish::time_utils::getDateTimeUintMs(
                sensorUpdatetimeSystemClock);
            sdbusplus::message::object_path chassisPath = std::get<2>(data);
            std::string sensorUri = "/redfish/v1/Chassis/";
            sensorUri += chassisPath.filename();
            sensorUri += "/Sensors/";
            sensorUri += sensorName;
            thisMetric["MetricProperty"] = sensorUri;
            thisMetric["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaMetricReport.v1_0_0.NvidiaMetricReport";
            thisMetric["Oem"]["Nvidia"]["MetricValueStale"] = true;
            if (requestTimestamp != 0 && thisMetric["MetricValue"] != "nan")
            {
                // Note: requestTimestamp, sensorUpdatetimeSteadyClock uses
                // steadyclock to calculate time
                int64_t freshness = static_cast<int64_t>(
                    requestTimestamp - sensorUpdatetimeSteadyClock);

                if (freshness <= staleSensorUpperLimit)
                {
                    thisMetric["Oem"]["Nvidia"]["MetricValueStale"] = false;
                }
            }
            resArray.push_back(thisMetric);
        }
    });
}

inline void getPlatforMetricsFromSensorMap(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath, const std::string& serviceName,
    const std::string& metricId, const uint64_t& requestTimestamp = 0)
{
    asyncResp->res.jsonValue["@odata.type"] =
        "#MetricReport.v1_4_2.MetricReport";
    std::string metricUri = "/redfish/v1/TelemetryService/MetricReports/";
    metricUri += metricId;
    asyncResp->res.jsonValue["@odata.id"] = metricUri;
    asyncResp->res.jsonValue["Id"] = metricId;
    asyncResp->res.jsonValue["Name"] = metricId;
    std::string metricDefinitionUri = telemetry::metricReportDefinitionUriStr;
    metricDefinitionUri += "/";
    metricDefinitionUri += metricId;

    asyncResp->res.jsonValue["MetricReportDefinition"]["@odata.id"] =
        metricDefinitionUri;
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
        "#NvidiaMetricReport.v1_0_0.NvidiaMetricReport";
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["SensingIntervalMilliseconds"] =
        pmSensingInterval;
    asyncResp->res.jsonValue["MetricValues"] = nlohmann::json::array();
    sdbusplus::asio::getProperty<uint32_t>(
        *crow::connections::systemBus, serviceName, objectPath,
        "xyz.openbmc_project.Sensor.Aggregation", "StaleSensorUpperLimitms",
        [asyncResp, objectPath, serviceName,
         requestTimestamp](const boost::system::error_code ec,
                           const uint32_t& staleSensorUpperLimit) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error");
            messages::internalError(asyncResp->res);
            return;
        }
        getSensorMap(asyncResp, serviceName, objectPath, staleSensorUpperLimit,
                     requestTimestamp);
    });
}

inline void
    getPlatformMetrics(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& chassisId,
                       const uint64_t& requestTimestamp = 0)
{
    const std::array<const char*, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Chassis"};
    auto respHandler = [asyncResp, requestTimestamp, chassisId](
                           const boost::system::error_code ec,
                           const std::vector<std::string>& chassisPaths) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("getPlatformMetrics respHandler DBUS error: {}",
                             ec);
            messages::internalError(asyncResp->res);
            return;
        }

        for (const std::string& chassisPath : chassisPaths)
        {
            sdbusplus::message::object_path path(chassisPath);
            const std::string& chassisName = path.filename();
            if (chassisName.empty())
            {
                BMCWEB_LOG_ERROR("Failed to find '/' in {}", chassisPath);
                continue;
            }
            if (chassisName != chassisId)
            {
                continue;
            }
            asyncResp->res.jsonValue["@odata.type"] =
                "#MetricReport.v1_4_2.MetricReport";
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/TelemetryService/MetricReports/" PLATFORMMETRICSID;
            asyncResp->res.jsonValue["Id"] = PLATFORMMETRICSID;
            asyncResp->res.jsonValue["Name"] = PLATFORMMETRICSID;
            asyncResp->res.jsonValue["MetricReportDefinition"]["@odata.id"] =
                telemetry::metricReportDefinitionUriStr +
                std::string("/" PLATFORMMETRICSID);
            asyncResp->res.jsonValue["MetricValues"] = nlohmann::json::array();
            // Identify sensor services for sensor readings
            processSensorServices(asyncResp, chassisPath, "all",
                                  pmSensingInterval, requestTimestamp);
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

// This function populate the metric report for devices but not excludes the
// subdevices. Eg : All metric for gpu memory or processor
inline void getAggregatedDeviceMetrics(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& deviceType, const std::string& deviceName,
    const std::string& devicePath,
    const dbus::utility::DBusInterfacesMap& portInterfacesProperties)
{
    if (deviceType != "MemoryMetrics" && deviceType != "ProcessorMetrics" &&
        deviceType != "NVSwitchMetrics" && deviceType != "ProcessorGpmMetrics")
    {
        return;
    }

    nlohmann::json& resArray = asyncResp->res.jsonValue["MetricValues"];
    auto timestampIterator = std::find_if(
        portInterfacesProperties.begin(), portInterfacesProperties.end(),
        [](const auto& i) { return i.first == "oem.nvidia.Timestamp"; });
    if (timestampIterator != portInterfacesProperties.end())
    {
        for (const auto& interface : portInterfacesProperties)
        {
            std::string ifaceName = std::string(interface.first);
            std::string keyName = getKeyNameonTimeStampIface(ifaceName);
            // GPM Processor Metrics Hosted on GPM Metrics Inerface
            if (((deviceType == "ProcessorGpmMetrics") &&
                 ((keyName != "GPMMetrics") && (keyName != "NVLinkMetrics"))) ||
                ((deviceType != "ProcessorGpmMetrics") &&
                 ((keyName == "GPMMetrics") || (keyName == "NVLinkMetrics"))))
            {
                continue;
            }
            std::string subDeviceName = "";
            auto timeStampMap = timestampIterator->second;
            auto timestampPropertiesIterator = std::find_if(
                timeStampMap.begin(), timeStampMap.end(),
                [keyName](const auto& i) { return i.first == keyName; });
            if (timestampPropertiesIterator != timeStampMap.end())
            {
                for (const auto& property : interface.second)
                {
                    auto timeStampPropertyValue =
                        timestampPropertiesIterator->second;
                    std::string propName = std::string(property.first);
                    std::map<std::string, uint64_t>* a =
                        std::get_if<std::map<std::string, uint64_t>>(
                            &timeStampPropertyValue);
                    if (a != nullptr)
                    {
                        auto value = property.second;
                        auto t = (*a)[propName];
                        getMetricValue(deviceType, deviceName, subDeviceName,
                                       devicePath, propName, ifaceName, value,
                                       t, resArray);
                    }
                }
            }
        }
    }
}

// This function populate the metric report for sub devices. Eg All nvlinks of
// all processors or switches
inline void getAggregatedSubDeviceMetrics(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& deviceType, const std::string& deviceName,
    const std::string& subDeviceName, const std::string& devicePath,
    const dbus::utility::DBusInterfacesMap& portInterfacesProperties)
{
    if (deviceType != "ProcessorPortMetrics" &&
        deviceType != "NVSwitchPortMetrics" &&
        deviceType != "ProcessorPortGpmMetrics")
    {
        return;
    }
    nlohmann::json& resArray = asyncResp->res.jsonValue["MetricValues"];
    auto timestampIterator = std::find_if(
        portInterfacesProperties.begin(), portInterfacesProperties.end(),
        [](const auto& i) { return i.first == "oem.nvidia.Timestamp"; });
    if (timestampIterator != portInterfacesProperties.end())
    {
        for (const auto& interface : portInterfacesProperties)
        {
            std::string ifaceName = std::string(interface.first);
            std::string keyName = getKeyNameonTimeStampIface(ifaceName);

            // GPM Processor Metrics Hosted on GPM Metrics Inerface
            if (((deviceType == "ProcessorPortGpmMetrics") &&
                 (keyName != "NVLinkMetrics")) ||
                ((deviceType != "ProcessorPortGpmMetrics") &&
                 (keyName == "NVLinkMetrics")))
            {
                continue;
            }

            auto timeStampMap = timestampIterator->second;
            auto timestampPropertiesIterator = std::find_if(
                timeStampMap.begin(), timeStampMap.end(),
                [keyName](const auto& i) { return i.first == keyName; });
            if (timestampPropertiesIterator != timeStampMap.end())
            {
                for (const auto& property : interface.second)
                {
                    auto timeStampPropertyValue =
                        timestampPropertiesIterator->second;
                    std::string propName = std::string(property.first);
                    std::map<std::string, uint64_t>* a =
                        std::get_if<std::map<std::string, uint64_t>>(
                            &timeStampPropertyValue);
                    if (a != nullptr)
                    {
                        auto value = property.second;
                        auto t = (*a)[propName];
                        getMetricValue(deviceType, deviceName, subDeviceName,
                                       devicePath, propName, ifaceName, value,
                                       t, resArray);
                    }
                }
            }
        }
    }
}

inline void getManagedObjectForMetrics(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath, const std::string& serviceName,
    const std::string& metricId, const std::string& metricfname,
    std::vector<std::string>& supportedMetricIds)
{
    BMCWEB_LOG_DEBUG("{}", metricId);
    std::string deviceType;

    std::string memoryMetrics = PLATFORMDEVICEPREFIX "MemoryMetrics";
    memoryMetrics += "_0";

    std::string processorMetrics = PLATFORMDEVICEPREFIX "ProcessorMetrics";
    processorMetrics += "_0";

    std::string processorGpmMetrics = PLATFORMDEVICEPREFIX
        "ProcessorGPMMetrics";
    processorGpmMetrics += "_0";

    std::string processorPortMetrics = PLATFORMDEVICEPREFIX
        "ProcessorPortMetrics";
    processorPortMetrics += "_0";

    std::string processorPortGpmMetrics = PLATFORMDEVICEPREFIX
        "ProcessorPortGPMMetrics";
    processorPortGpmMetrics += "_0";

    std::string nvswitchMetrics = PLATFORMDEVICEPREFIX "NVSwitchMetrics";
    nvswitchMetrics += "_0";

    std::string nvswitchPortMetrics = PLATFORMDEVICEPREFIX
        "NVSwitchPortMetrics";
    nvswitchPortMetrics += "_0";

    if (metricId == memoryMetrics && metricfname == "memory")
    {
        deviceType = "MemoryMetrics";
    }
    else if (metricId == processorPortMetrics && metricfname == "processors")
    {
        deviceType = "ProcessorPortMetrics";
    }
    else if (metricId == processorMetrics && metricfname == "processors")
    {
        deviceType = "ProcessorMetrics";
    }
    else if (metricId == nvswitchPortMetrics && metricfname == "Switches")
    {
        deviceType = "NVSwitchPortMetrics";
    }
    else if (metricId == nvswitchMetrics && metricfname == "Switches")
    {
        deviceType = "NVSwitchMetrics";
    }
    else if (metricId == processorGpmMetrics && metricfname == "processors")
    {
        deviceType = "ProcessorGpmMetrics";
    }
    else if (metricId == processorPortGpmMetrics && metricfname == "processors")
    {
        deviceType = "ProcessorPortGpmMetrics";
    }
    else
    {
        return;
    }
    supportedMetricIds.emplace_back(metricId);
    asyncResp->res.jsonValue["@odata.type"] =
        "#MetricReport.v1_4_2.MetricReport";
    std::string metricUri = "/redfish/v1/TelemetryService/MetricReports/";
    metricUri += metricId;
    asyncResp->res.jsonValue["@odata.id"] = metricUri;
    asyncResp->res.jsonValue["Id"] = metricId;
    asyncResp->res.jsonValue["Name"] = metricId;
    std::string metricDefinitionUri = telemetry::metricReportDefinitionUriStr;
    metricDefinitionUri += "/";
    metricDefinitionUri += metricId;

    asyncResp->res.jsonValue["MetricReportDefinition"]["@odata.id"] =
        metricDefinitionUri;
    crow::connections::systemBus->async_method_call(
        [asyncResp,
         deviceType](const boost::system::error_code ec,
                     const dbus::utility::ManagedObjectType& objects) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }
        if (deviceType == "MemoryMetrics" || deviceType == "NVSwitchMetrics" ||
            deviceType == "ProcessorMetrics" ||
            deviceType == "ProcessorGpmMetrics")
        {
            for (const auto& object : objects)
            {
                const std::string parentName =
                    object.first.parent_path().filename();
                const std::string devicePath = std::string(object.first);
                if (parentName == "processors" || parentName == "memory" ||
                    parentName == "Switches")
                {
                    const std::string deviceName =
                        std::string(object.first.filename());
                    getAggregatedDeviceMetrics(asyncResp, deviceType,
                                               deviceName, devicePath,
                                               object.second);
                }
            }
        }
        else if (deviceType == "NVSwitchPortMetrics" ||
                 deviceType == "ProcessorPortMetrics" ||
                 deviceType == "ProcessorPortGpmMetrics")
        {
            for (const auto& object : objects)
            {
                const std::string parentName =
                    object.first.parent_path().filename();
                const std::string devicePath = std::string(object.first);
                if (parentName == "Ports")
                {
                    const std::string subDeviceName =
                        std::string(object.first.filename());
                    const std::string deviceName =
                        object.first.parent_path().parent_path().filename();
                    getAggregatedSubDeviceMetrics(asyncResp, deviceType,
                                                  deviceName, subDeviceName,
                                                  devicePath, object.second);
                }
            }
        }
        else
        {
            return;
        }
    },
        serviceName, objPath, "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects");
}

inline bool
    isMetricIdSupported(const std::string& requestedMetricId,
                        const std::vector<std::string>& supportedMetricIds)
{
    bool supported = true;
    // If metricId not found in supportedMetricId list
    if (std::find(supportedMetricIds.begin(), supportedMetricIds.end(),
                  requestedMetricId) == supportedMetricIds.end())
    {
        supported = false;
    }
    return supported;
}

inline void
    getPlatforMetrics(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& metricId,
                      const uint64_t& requestTimestamp = 0)
{
    using MapperServiceMap =
        std::vector<std::pair<std::string, std::vector<std::string>>>;

    // Map of object paths to MapperServiceMaps
    using MapperGetSubTreeResponse =
        std::vector<std::pair<std::string, MapperServiceMap>>;
    crow::connections::systemBus->async_method_call(
        [asyncResp, metricId,
         requestTimestamp](boost::system::error_code ec,
                           const MapperGetSubTreeResponse& subtree) mutable {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error: {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }
        // list of metric Ids supported
        std::vector<std::string> supportedMetricIds;
        for (const auto& [path, serviceMap] : subtree)
        {
            const std::string objectPath = path;
            sdbusplus::message::object_path objPath(objectPath);
            const std::string metricfname = objPath.filename();
            for (const auto& [conName, interfaceList] : serviceMap)
            {
                const std::string serviceName = conName;
                if (metricId == PLATFORMMETRICSID)
                {
                    if (metricfname == "platformmetrics")
                    {
                        supportedMetricIds.emplace_back(PLATFORMMETRICSID);
                        getPlatforMetricsFromSensorMap(asyncResp, objectPath,
                                                       serviceName, metricId,
                                                       requestTimestamp);
                    }
                }
                else if (metricfname == "memory" ||
                         metricfname == "processors" ||
                         metricfname == "Switches")
                {
                    getManagedObjectForMetrics(asyncResp, objectPath,
                                               serviceName, metricId,
                                               metricfname, supportedMetricIds);
                }
            }
        }
        if (!isMetricIdSupported(metricId, supportedMetricIds))
        {
            messages::resourceNotFound(asyncResp->res, "MetricReport",
                                       metricId);
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 1>{"xyz.openbmc_project.Sensor.Aggregation"});
}

#endif

inline void requestRoutesMetricReport(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/TelemetryService/MetricReports/<str>/")
        .privileges(redfish::privileges::getMetricReport)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& id) {
<<<<<<< HEAD
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
#ifdef BMCWEB_ENABLE_PLATFORM_METRICS
        const uint64_t requestTimestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        BMCWEB_LOG_DEBUG("Request submitted at {}", requestTimestamp);
#ifdef BMCWEB_ENABLE_SHMEM_PLATFORM_METRICS
        redfish::shmem::getShmemPlatformMetrics(asyncResp, id,
                                                requestTimestamp);
#else
        getPlatforMetrics(asyncResp, id, requestTimestamp);
#endif
        return;
#else
        const std::string reportPath = telemetry::getDbusReportPath(id);
        crow::connections::systemBus->async_method_call(
            [asyncResp, id, reportPath](const boost::system::error_code& ec) {
            if (ec.value() == EBADR ||
                ec == boost::system::errc::host_unreachable)
            {
                messages::resourceNotFound(asyncResp->res, "MetricReport", id);
                return;
            }
            if (ec)
            {
                BMCWEB_LOG_ERROR("respHandler DBus error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }

            sdbusplus::asio::getProperty<telemetry::TimestampReadings>(
                *crow::connections::systemBus, telemetry::service, reportPath,
                telemetry::reportInterface, "Readings",
                [asyncResp, id](const boost::system::error_code& ec2,
                                const telemetry::TimestampReadings& ret) {
                if (ec2)
=======
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
>>>>>>> origin/master
                {
                    return;
                }
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
                            BMCWEB_LOG_ERROR("respHandler DBus error {}", ec);
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        sdbusplus::asio::getProperty<
                            telemetry::TimestampReadings>(
                            *crow::connections::systemBus, telemetry::service,
                            reportPath, telemetry::reportInterface, "Readings",
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
<<<<<<< HEAD
        },
            telemetry::service, reportPath, telemetry::reportInterface,
            "Update");
#endif
    });
=======
>>>>>>> origin/master
}
} // namespace redfish
