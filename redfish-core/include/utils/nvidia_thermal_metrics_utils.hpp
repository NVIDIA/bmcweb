#pragma once
#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "error_messages.hpp"
#include "http_request.hpp"
#include "logging.hpp"
#include "nvidia_dbus_utility.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "sensors.hpp"
#include "str_utility.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_time_utils.hpp"
#include "utils/sensor_utils.hpp"
#include "utils/time_utils.hpp"

#include <asm-generic/errno.h>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace redfish
{
namespace nvidia_thermal_metrics_utils
{
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
        bmcweb::split(split, sensorPath, '/');
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
                const dbus::utility::DbusVariantType& areaValueVariant =
                    sensorAreaValue->second;
                physicalContext = std::get_if<std::string>(&areaValueVariant);
            }
        }
        auto sensorInterfaceProps =
            interfacesDict.find("xyz.openbmc_project.Sensor.Value");
        if (sensorInterfaceProps != interfacesDict.end())
        {
            auto thisValueIt = sensorInterfaceProps->second.find("Value");
            if (thisValueIt != sensorInterfaceProps->second.end())
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
                            const dbus::utility::DbusVariantType&
                                valueVariant1 = sensorDeviceName->second;
                            deviceName =
                                std::get_if<std::string>(&valueVariant1);
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
                            const dbus::utility::DbusVariantType&
                                timeValueVariant = thisElapsedIt->second;
                            const uint64_t* metricUpdatetimestamp =
                                std::get_if<uint64_t>(&timeValueVariant);

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
                                    const boost::system::error_code& ec,
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
                                            const boost::system::error_code&
                                                ec1,
                                            const std::variant<
                                                std::vector<std::string>>&
                                                variantEndpoints) {
                if (ec1)
                {
                    BMCWEB_LOG_DEBUG(
                        "getAllChassisSensors DBUS error on chassis path{}: {}",
                        objectPath, ec1);
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
         metricsType](const boost::system::error_code& ec,
                      ManagedObjectsVectorType& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "getServiceRootManagedObjects for connection:{} error: {}",
                    connection, ec);
                return;
            }
            std::sort(resp.begin(), resp.end(),
                      [](const auto& a, const auto& b) {
                          return a.first.str < b.first.str;
                      });
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
         metricsType](const boost::system::error_code& ec,
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
            std::sort(resp.begin(), resp.end(),
                      [](const auto& a, const auto& b) {
                          return a.first.str < b.first.str;
                      });
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
                          requestTimestamp, metricsType](
                             const boost::system::error_code& ec,
                             const dbus::utility::GetSubTreeType& subtree) {
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
            if (serviceMap.empty())
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

} // namespace nvidia_thermal_metrics_utils
} // namespace redfish
