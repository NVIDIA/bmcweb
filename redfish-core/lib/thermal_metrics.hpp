#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "dbus_utility.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "sensors.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/sensor_utils.hpp"
#include "utils/time_utils.hpp"

#include <boost/system/error_code.hpp>
#include <utils/chassis_utils.hpp>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace redfish
{
using GetSubTreeType = std::vector<
    std::pair<std::string,
              std::vector<std::pair<std::string, std::vector<std::string>>>>>;

inline void processSensorsValue(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::vector<std::string>& sensorPaths, const std::string& chassisId,
    const ManagedObjectsVectorType& managedObjectsResp,
    const std::string& metricsType, const uint64_t& sensingInterval = 0,
    const uint64_t& requestTimestamp = 0)
{
    // Get sensor reading from managed object
    for (const std::string& sensorPath : sensorPaths)
    {
        auto sensorElem = std::find_if(
            managedObjectsResp.begin(), managedObjectsResp.end(),
            [sensorPath](const std::pair<
                         sdbusplus::message::object_path,
                         boost::container::flat_map<
                             std::string,
                             boost::container::flat_map<
                                 std::string, dbus::utility::DbusVariantType>>>&
                             element) { return element.first == sensorPath; });
        if (sensorElem == std::end(managedObjectsResp))
        {
            // Sensor not found continue
            BMCWEB_LOG_DEBUG("Sensor not found sensorPath{}", sensorPath);
            continue;
        }
        std::vector<std::string> split;
        // Reserve space for
        // /xyz/openbmc_project/sensors/<name>/<subname>
        split.reserve(6);
        boost::algorithm::split(split, sensorPath, boost::is_any_of("/"));
        if (split.size() < 6)
        {
            BMCWEB_LOG_ERROR("Got path that isn't long enough {}", sensorPath);
            continue;
        }
        // These indexes aren't intuitive, as boost::split puts an empty
        // string at the beginning
        const std::string& sensorType = split[4];
        const std::string& sensorName = split[5];
        BMCWEB_LOG_DEBUG("sensorName {} sensorType {}", sensorName, sensorType);

        if ((metricsType == "thermal") && (sensorType != "temperature"))
        {
            BMCWEB_LOG_DEBUG("Skip non thermal sensor type:{}", sensorType);
            continue;
        }

        // Process sensor reading
        auto interfacesDict = sensorElem->second;
        auto sensorAreadProperties =
            interfacesDict.find("xyz.openbmc_project.Inventory.Decorator.Area");
        const std::string* physicalContext = nullptr;
        if (sensorAreadProperties != interfacesDict.end())
        {
            auto sensorAreaValue =
                sensorAreadProperties->second.find("PhysicalContext");
            if (sensorAreaValue != sensorAreadProperties->second.end())
            {
                const dbus::utility::DbusVariantType& valueVariant =
                    sensorAreaValue->second;
                physicalContext = std::get_if<std::string>(&valueVariant);
            }
        }
        auto interfaceProperties =
            interfacesDict.find("xyz.openbmc_project.Sensor.Value");
        if (interfaceProperties != interfacesDict.end())
        {
            auto thisValueIt = interfaceProperties->second.find("Value");
            if (thisValueIt != interfaceProperties->second.end())
            {
                const dbus::utility::DbusVariantType& valueVariant =
                    thisValueIt->second;
                const double* doubleValue = std::get_if<double>(&valueVariant);
                double reading = static_cast<double>(NAN);
                if (doubleValue != nullptr)
                {
                    reading = *doubleValue;
                }
                if (metricsType == "thermal")
                {
                    nlohmann::json& resArray =
                        asyncResp->res.jsonValue["TemperatureReadingsCelsius"];
                    nlohmann::json objectJson = nlohmann::json::object();
                    objectJson["@odata.id"] =
                        std::string("/redfish/v1/Chassis/")
                            .append(chassisId)
                            .append("/Sensors/")
                            .append(sensorName);
                    objectJson["DataSourceUri"] =
                        std::string("/redfish/v1/Chassis/")
                            .append(chassisId)
                            .append("/Sensors/")
                            .append(sensorName);
                    objectJson["DeviceName"] = sensorName;

                    auto sensorDeviceNamedProperties = interfacesDict.find(
                        "xyz.openbmc_project.Inventory.Item");
                    const std::string* deviceName = nullptr;
                    if (sensorDeviceNamedProperties != interfacesDict.end())
                    {
                        auto sensorDeviceName =
                            sensorDeviceNamedProperties->second.find(
                                "PrettyName");
                        if (sensorDeviceName !=
                            sensorDeviceNamedProperties->second.end())
                        {
                            const dbus::utility::DbusVariantType& valueVariant =
                                sensorDeviceName->second;
                            deviceName =
                                std::get_if<std::string>(&valueVariant);
                            objectJson["DeviceName"] = *deviceName;
                        }
                    }

                    if (std::isnan(reading))
                    {
                        objectJson["Reading"] = nullptr;
                    }
                    else
                    {
                        objectJson["Reading"] = reading;
                    }

                    if (physicalContext != nullptr)
                    {
                        objectJson["PhysicalContext"] =
                            redfish::dbus_utils::toPhysicalContext(
                                *physicalContext);
                    }
                    resArray.push_back(objectJson);
                }
                else
                {
                    nlohmann::json& resArray =
                        asyncResp->res.jsonValue["MetricValues"];
                    std::string sensorUri = "/redfish/v1/Chassis/";
                    sensorUri += chassisId;
                    sensorUri += "/Sensors/";
                    sensorUri += sensorName;

                    nlohmann::json thisMetric = nlohmann::json::object();
                    thisMetric["MetricProperty"] = sensorUri;
                    thisMetric["MetricValue"] = std::to_string(reading);
                    BMCWEB_LOG_DEBUG("Reading:{}", reading);
                    auto interfaceProperties = interfacesDict.find(
                        "xyz.openbmc_project.Time.EpochTime");
                    if (interfaceProperties != interfacesDict.end())
                    {
                        auto thisElapsedIt =
                            interfaceProperties->second.find("Elapsed");
                        if (thisElapsedIt != interfaceProperties->second.end())
                        {
                            const dbus::utility::DbusVariantType& valueVariant =
                                thisElapsedIt->second;
                            const uint64_t* metricUpdatetimestamp =
                                std::get_if<uint64_t>(&valueVariant);

                            if (metricUpdatetimestamp != nullptr)
                            {
                                thisMetric["Timestamp"] =
                                    redfish::time_utils::getDateTimeUintMs(
                                        *metricUpdatetimestamp);
                            }
                            else
                            {
                                BMCWEB_LOG_DEBUG("Unable to read Elapsed");
                                thisMetric["Timestamp"] = "nan";
                            }
                            if (sensingInterval != 0)
                            {
                                thisMetric["Oem"]["Nvidia"]
                                          ["SensingIntervalMilliseconds"] =
                                              std::to_string(sensingInterval);
                            }
                            // assume stale by default
                            thisMetric["Oem"]["Nvidia"]["MetricValueStale"] =
                                true;
                            // Compute stalenes only when sensingInterval
                            // and metricUpdatetimestamp are not null
                            if (sensingInterval != 0 &&
                                metricUpdatetimestamp != nullptr &&
                                !std::isnan(reading))
                            {
                                // difference between requestTimestamp and
                                // lastUpdateTime stamp should be within
                                // BMCWEB_STALESENSOR_UPPER_LIMIT_MILISECOND for
                                // fresh metric
                                BMCWEB_LOG_DEBUG(
                                    "Stalesensor upper limit is:{}",
                                    BMCWEB_STALESENSOR_UPPER_LIMIT_MILISECOND);
                                if (requestTimestamp - *metricUpdatetimestamp <=
                                    BMCWEB_STALESENSOR_UPPER_LIMIT_MILISECOND)
                                {
                                    thisMetric["Oem"]["Nvidia"]
                                              ["MetricValueStale"] = false;
                                }
                            }
                        }
                    }
                    resArray.push_back(thisMetric);
                }
            }
        }
    }
}

inline void processChassisSensors(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const ManagedObjectsVectorType& managedObjectsResp,
    const std::string& chassisPath, const std::string& metricsType,
    const uint64_t& sensingInterval = 0, const uint64_t& requestTimestamp = 0)
{
    auto getAllChassisHandler = [asyncResp, chassisPath, managedObjectsResp,
                                 sensingInterval, requestTimestamp,
                                 metricsType](
                                    const boost::system::error_code ec,
                                    std::variant<std::vector<std::string>>&
                                        chassisLinks) {
        std::vector<std::string> chassisPaths;
        if (ec)
        {
            // no chassis links = no failures
        }
        // Add parent chassis to the list
        chassisPaths.emplace_back(chassisPath);
        // Add underneath chassis paths
        std::vector<std::string>* chassisData =
            std::get_if<std::vector<std::string>>(&chassisLinks);
        if (chassisData != nullptr)
        {
            for (const std::string& path : *chassisData)
            {
                chassisPaths.emplace_back(path);
            }
        }
        // Sort the chassis for sensors paths
        std::sort(chassisPaths.begin(), chassisPaths.end());

        // Process all sensors to all chassis
        for (const std::string& objectPath : chassisPaths)
        {
            // Get the chassis path for respective sensors
            sdbusplus::message::object_path path(objectPath);
            const std::string& chassisId = path.filename();

            auto getAllChassisSensors = [asyncResp, chassisId,
                                         managedObjectsResp, sensingInterval,
                                         requestTimestamp, objectPath,
                                         metricsType](
                                            const boost::system::error_code ec,
                                            const std::variant<
                                                std::vector<std::string>>&
                                                variantEndpoints) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG(
                        "getAllChassisSensors DBUS error on chassis path{}: {}",
                        objectPath, ec);
                    return;
                }
                const std::vector<std::string>* sensorPaths =
                    std::get_if<std::vector<std::string>>(&(variantEndpoints));
                if (sensorPaths == nullptr)
                {
                    BMCWEB_LOG_ERROR("getAllChassisSensors empty sensors list");
                    messages::internalError(asyncResp->res);
                    return;
                }
                // Process sensor readings
                processSensorsValue(asyncResp, *sensorPaths, chassisId,
                                    managedObjectsResp, metricsType,
                                    sensingInterval, requestTimestamp);
            };
            crow::connections::systemBus->async_method_call(
                getAllChassisSensors, "xyz.openbmc_project.ObjectMapper",
                objectPath + "/all_sensors", "org.freedesktop.DBus.Properties",
                "Get", "xyz.openbmc_project.Association", "endpoints");
        }
    };
    // Get all chassis
    crow::connections::systemBus->async_method_call(
        getAllChassisHandler, "xyz.openbmc_project.ObjectMapper",
        chassisPath + "/all_chassis", "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getServiceRootManagedObjects(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connection, const std::string& chassisPath,
    const std::string& metricsType, const uint64_t& sensingInterval = 0,
    const uint64_t& requestTimestamp = 0)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, connection, chassisPath, sensingInterval, requestTimestamp,
         metricsType](const boost::system::error_code ec,
                      ManagedObjectsVectorType& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "getServiceRootManagedObjects for connection:{} error: {}",
                    connection, ec);
                return;
            }
            std::sort(resp.begin(), resp.end());
            processChassisSensors(asyncResp, resp, chassisPath, metricsType,
                                  sensingInterval, requestTimestamp);
        },
        connection, "/", "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects");
}

inline void getServiceManagedObjects(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connection, const std::string& chassisPath,
    const std::string& metricsType, const uint64_t& sensingInterval = 0,
    const uint64_t& requestTimestamp = 0)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, connection, chassisPath, sensingInterval, requestTimestamp,
         metricsType](const boost::system::error_code ec,
                      ManagedObjectsVectorType& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG(
                    "GetManagedObjects is not at sensor path for connection:{}",
                    connection);
                // Check managed objects on service root
                getServiceRootManagedObjects(asyncResp, connection, chassisPath,
                                             metricsType, sensingInterval,
                                             requestTimestamp);
                return;
            }
            std::sort(resp.begin(), resp.end());
            processChassisSensors(asyncResp, resp, chassisPath, metricsType,
                                  sensingInterval, requestTimestamp);
        },
        connection, "/xyz/openbmc_project/sensors",
        "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
}

inline void processSensorServices(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisPath, const std::string& metricsType,
    const uint64_t& sensingInterval = 0, const uint64_t& requestTimestamp = 0)
{
    // Sensor interface implemented by sensor services
    const std::array<const char*, 1> sensorInterface = {
        "xyz.openbmc_project.Sensor.Value"};

    // Get all sensors on the system
    auto getAllSensors = [asyncResp, chassisPath, sensingInterval,
                          requestTimestamp,
                          metricsType](const boost::system::error_code ec,
                                       const GetSubTreeType& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "processSensorServices: Error in getting DBUS sensors: {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }
        if (subtree.empty())
        {
            BMCWEB_LOG_ERROR("processSensorServices: Empty sensors subtree");
            messages::internalError(asyncResp->res);
            return;
        }

        // Identify unique services for GetManagedObjects
        std::set<std::string> sensorServices;
        for (const auto& [objectPath, serviceMap] : subtree)
        {
            if (serviceMap.size() < 1)
            {
                BMCWEB_LOG_DEBUG("Got 0 service names for sensorpath:{}",
                                 objectPath);
                continue;
            }
            sensorServices.insert(serviceMap[0].first);
        }
        // Collect all GetManagedObjects for services
        for (const std::string& connection : sensorServices)
        {
            getServiceManagedObjects(asyncResp, connection, chassisPath,
                                     metricsType, sensingInterval,
                                     requestTimestamp);
        }
    };
    crow::connections::systemBus->async_method_call(
        getAllSensors, "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/sensors", 2, sensorInterface);
}

inline void afterGetTemperatureValue(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& path,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& valuesDict)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("DBUS response error for getAllProperties {}",
                             ec.value());
            messages::internalError(asyncResp->res);
        }
        return;
    }

    nlohmann::json item = nlohmann::json::object();

    /* Don't return an error for a failure to fill in properties from any of
     * the sensors in the list. Just skip it.
     */
    if (sensor_utils::objectExcerptToJson(
            path, chassisId, sensor_utils::ChassisSubNode::thermalMetricsNode,
            "temperature", valuesDict, item))
    {
        nlohmann::json& temperatureReadings =
            asyncResp->res.jsonValue["TemperatureReadingsCelsius"];
        nlohmann::json::array_t* temperatureArray =
            temperatureReadings.get_ptr<nlohmann::json::array_t*>();
        if (temperatureArray == nullptr)
        {
            BMCWEB_LOG_ERROR("Missing TemperatureReadingsCelsius Json array");
            messages::internalError(asyncResp->res);
            return;
        }

        temperatureArray->emplace_back(std::move(item));
        asyncResp->res.jsonValue["TemperatureReadingsCelsius@odata.count"] =
            temperatureArray->size();

        json_util::sortJsonArrayByKey(*temperatureArray, "DataSourceUri");
    }
}

inline void handleTemperatureReadingsCelsius(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    const sensor_utils::SensorServicePathList& sensorsServiceAndPath)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("DBUS response error for getAssociatedSubTree {}",
                             ec.value());
            messages::internalError(asyncResp->res);
        }
        return;
    }

    asyncResp->res.jsonValue["TemperatureReadingsCelsius"] =
        nlohmann::json::array_t();
    asyncResp->res.jsonValue["TemperatureReadingsCelsius@odata.count"] = 0;

    for (const auto& [service, sensorPath] : sensorsServiceAndPath)
    {
        sdbusplus::asio::getAllProperties(
            *crow::connections::systemBus, service, sensorPath,
            "xyz.openbmc_project.Sensor.Value",
            [asyncResp, chassisId,
             sensorPath](const boost::system::error_code& ec1,
                         const dbus::utility::DBusPropertiesMap& properties) {
                afterGetTemperatureValue(asyncResp, chassisId, sensorPath, ec1,
                                         properties);
            });
    }
}

inline void getTemperatureReadingsCelsius(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& validChassisPath, const std::string& chassisId)
{
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Sensor.Value"};

    sensor_utils::getAllSensorObjects(
        validChassisPath, "/xyz/openbmc_project/sensors/temperature",
        interfaces, 1,
        std::bind_front(handleTemperatureReadingsCelsius, asyncResp,
                        chassisId));
}

inline void
    doThermalMetrics(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& chassisId,
                     const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/ThermalMetrics/ThermalMetrics.json>; rel=describedby");
    asyncResp->res.jsonValue["@odata.type"] =
        "#ThermalMetrics.v1_0_1.ThermalMetrics";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/ThermalSubsystem/ThermalMetrics", chassisId);
    asyncResp->res.jsonValue["Id"] = "ThermalMetrics";
    asyncResp->res.jsonValue["Name"] = "Thermal Metrics";

    getTemperatureReadingsCelsius(asyncResp, *validChassisPath, chassisId);
}

inline void handleThermalMetricsHead(
    App& app, const crow::Request& req,
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
                messages::resourceNotFound(asyncResp->res, "Chassis",
                                           chassisId);
                return;
            }
            asyncResp->res.addHeader(
                boost::beast::http::field::link,
                "</redfish/v1/JsonSchemas/ThermalMetrics/ThermalMetrics.json>; rel=describedby");
        });
}

inline void
    handleThermalMetricsGet(App& app, const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(doThermalMetrics, asyncResp, chassisId));
}

inline void requestRoutesThermalMetrics(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Chassis/<str>/ThermalSubsystem/ThermalMetrics/")
        .privileges(redfish::privileges::headThermalMetrics)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleThermalMetricsHead, std::ref(app)));

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Chassis/<str>/ThermalSubsystem/ThermalMetrics/")
        .privileges({{"Login"}})
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& param) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            const std::string& chassisId = param;
            // Identify chassis
            const std::array<const char*, 1> interface = {
                "xyz.openbmc_project.Inventory.Item.Chassis"};

            auto respHandler = [asyncResp,
                                chassisId](const boost::system::error_code ec,
                                           const std::vector<std::string>&
                                               chassisPaths) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "thermal metrics respHandler DBUS error: {}", ec);
                    messages::internalError(asyncResp->res);
                    return;
                }
                // Identify the chassis
                for (const std::string& chassisPath : chassisPaths)
                {
                    sdbusplus::message::object_path path(chassisPath);
                    const std::string& chassisName = path.filename();
                    if (chassisName.empty())
                    {
                        BMCWEB_LOG_ERROR("Failed to find '/' in {}",
                                         chassisPath);
                        continue;
                    }
                    if (chassisName != chassisId)
                    {
                        continue;
                    }
                    // Process response

                    asyncResp->res.addHeader(
                        boost::beast::http::field::link,
                        "</redfish/v1/JsonSchemas/ThermalMetrics/ThermalMetrics.json>; rel=describedby");
                    asyncResp->res.jsonValue["@odata.type"] =
                        "#ThermalMetrics.v1_0_0.ThermalMetrics";
                    asyncResp->res.jsonValue["@odata.id"] =
                        "/redfish/v1/Chassis/" + chassisId +
                        "/ThermalSubsystem/ThermalMetrics";
                    asyncResp->res.jsonValue["Id"] = "ThermalMetrics";
                    asyncResp->res.jsonValue["Name"] =
                        "Chassis Thermal Metrics";
                    asyncResp->res.jsonValue["TemperatureReadingsCelsius"] =
                        nlohmann::json::array();

                    // Identify sensor services for sensor readings
                    processSensorServices(asyncResp, chassisPath, "thermal");
                    return;
                }
                messages::resourceNotFound(asyncResp->res, "Chassis",
                                           chassisId);
            };
            // Get the Chassis Collection
            crow::connections::systemBus->async_method_call(
                respHandler, "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                "/xyz/openbmc_project/inventory", 0, interface);
        });
}
} // namespace redfish
