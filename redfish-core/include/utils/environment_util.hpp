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
#include "chassis_utils.hpp"
#include "dbus_utility.hpp"
#include "str_utility.hpp"
#include "utils/nvidia_async_call_utils.hpp"
#include "utils/nvidia_async_set_callbacks.hpp"

#include <boost/container/flat_map.hpp>
#include <boost/url/format.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace redfish
{
namespace nvidia_env_utils
{
using SetPointProperties =
    std::vector<std::pair<std::string, dbus::utility::DbusVariantType>>;

// Map of service name to list of interfaces
using MapperServiceMap =
    std::vector<std::pair<std::string, std::vector<std::string>>>;

/**
 * Handle the PATCH operation of the Edpp Scale limit property. Do basic
 * validation of the input data, and then set the D-Bus property.
 *
 * @param[in,out]   resp                Async HTTP response.
 * @param[in]       processorId         prcoessor Id.
 * @param[in]       setPoint            New property value to apply.
 * @param[in]       patchEdppSetPoint   Path of CPU object to modify.
 * @param[in]       serviceMap          Service map for CPU object.
 */
inline void patchEdppSetPoint(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                              const std::string& processorId,
                              const size_t setPoint, const bool persistency,
                              const std::string& cpuObjectPath,
                              const MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::find(interfaceList.begin(), interfaceList.end(),
                      "com.nvidia.Edpp") != interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        messages::internalError(resp->res);
        return;
    }

    dbus::utility::getDbusObject(
        cpuObjectPath,
        std::array<std::string_view, 1>{
            nvidia_async_operation_utils::setAsyncInterfaceName},
        [resp, setPoint, persistency, processorId, cpuObjectPath,
         service =
             *inventoryService](const boost::system::error_code& ec,
                                const dbus::utility::MapperGetObject& object) {
            if (!ec)
            {
                for (const auto& [serv, _] : object)
                {
                    if (serv != service)
                    {
                        continue;
                    }

                    nvidia_async_operation_utils::
                        doGenericSetAsyncAndGatherResult(
                            resp, std::chrono::seconds(60), service,
                            cpuObjectPath, "com.nvidia.Edpp", "SetPoint",
                            std::variant<std::tuple<bool, uint32_t>>(
                                std::make_tuple(
                                    persistency,
                                    static_cast<uint32_t>(setPoint))),
                            nvidia_async_operation_utils::
                                PatchEdppSetPointCallback{resp});

                    return;
                }
            }

            std::tuple<size_t, bool> reqSetPoint;
            reqSetPoint = std::make_tuple(setPoint, persistency);

            // Set the property, with handler to check error responses
            dbus::utility::async_method_call(
                [resp, processorId,
                 setPoint](boost::system::error_code& ec1,
                           sdbusplus::message::message& msg) {
                    if (!ec1)
                    {
                        BMCWEB_LOG_DEBUG("Set point property succeeded");
                        return;
                    }

                    BMCWEB_LOG_ERROR(
                        "Processor ID: {} set point property failed: {}",
                        processorId, ec1);
                    // Read and convert dbus error message to redfish error
                    const sd_bus_error* dbusError = msg.get_error();
                    if (dbusError == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Internal error for patch EDPp Setpoint");
                        messages::internalError(resp->res);
                        return;
                    }
                    if (strcmp(
                            dbusError->name,
                            "xyz.openbmc_project.Common.Error.InvalidArgument") ==
                        0)
                    {
                        // Invalid value
                        BMCWEB_LOG_ERROR("Invalid value for EDPp Setpoint");
                        messages::propertyValueIncorrect(
                            resp->res, "setPoint", std::to_string(setPoint));
                    }
                    else if (
                        strcmp(
                            dbusError->name,
                            "xyz.openbmc_project.Common.Error.Unavailable") ==
                        0)
                    {
                        std::string errBusy = "0x50A";
                        std::string errBusyResolution =
                            "SMBPBI Command failed with error busy, please try after 60 seconds";
                        // busy error
                        BMCWEB_LOG_ERROR(
                            "Command failed with error busy, for patch EDPp Setpoint");
                        messages::asyncError(resp->res, errBusy,
                                             errBusyResolution);
                    }
                    else if (strcmp(
                                 dbusError->name,
                                 "xyz.openbmc_project.Common.Error.Timeout") ==
                             0)
                    {
                        std::string errTimeout = "0x600";
                        std::string errTimeoutResolution =
                            "Settings may/maynot have applied, please check get response before patching";
                        // timeout error
                        BMCWEB_LOG_ERROR(
                            "Timeout error for patch EDPp Setpoint");
                        messages::asyncError(resp->res, errTimeout,
                                             errTimeoutResolution);
                    }
                    else if (strcmp(dbusError->name,
                                    "xyz.openbmc_project.Common."
                                    "Device.Error.WriteFailure") == 0)
                    {
                        // Service failed to change the config
                        BMCWEB_LOG_ERROR(
                            "Write Operation failed for patch EDPp Setpoint");
                        messages::operationFailed(resp->res);
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR(
                            "Unknown error for patch EDPp Setpoint");
                        messages::internalError(resp->res);
                    }
                },
                service, cpuObjectPath, "org.freedesktop.DBus.Properties",
                "Set", "com.nvidia.Edpp", "SetPoint",
                std::variant<std::tuple<size_t, bool>>(reqSetPoint));
        });
}

inline void getPowerMode(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& connectionName,
                         const std::string& objPath)
{
    dbus::utility::async_method_call(
        [asyncResp, connectionName,
         objPath](const boost::system::error_code& ec,
                  const std::vector<
                      std::pair<std::string, std::variant<std::string>>>&
                      propertiesList) {
            if (ec || propertiesList.empty())
            {
                BMCWEB_LOG_DEBUG("DBUS response error for "
                                 "Chassis properties");
                return;
            }
            for (const std::pair<std::string, std::variant<std::string>>&
                     property : propertiesList)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "PowerMode")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for type");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    std::string oemPowerMode =
                        redfish::chassis_utils::getPowerModeType(*value);
                    if (oemPowerMode.empty())
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["PowerMode"] =
                        oemPowerMode;
                }
            }
        },
        connectionName, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Control.Power.Mode");
}

inline void getClearPowerCap(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& resourceId, const std::string& objPath)
{
    const std::array<const char*, 2> clearPowerCapInterfaces = {
        "com.nvidia.Common.ClearPowerCap",
        "com.nvidia.Common.ClearPowerCapAsync"};

    dbus::utility::async_method_call(
        [asyncResp, resourceId, objPath](
            const boost::system::error_code& ec,
            [[maybe_unused]] const std::vector<
                std::pair<std::string, std::vector<std::string>>>& objInfo) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("ObjectMapper::GetObject call failed: {}", ec);
                return;
            }

            asyncResp->res
                .jsonValue["Actions"]["Oem"]
                          ["#NvidiaEnvironmentMetrics.ClearOOBSetPoint"] = {
                {"target",
                 "/redfish/v1/Chassis/" + resourceId +
                     "/EnvironmentMetrics/Actions/Oem/NvidiaEnvironmentMetrics.ClearOOBSetPoint"}};
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
        clearPowerCapInterfaces);
}

inline void getPowerWattsBySensorName(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& sensorName)
{
    const std::string& totalPowerPath =
        "/xyz/openbmc_project/sensors/power/" + sensorName;
    // Add total power sensor to associated chassis only
    dbus::utility::async_method_call(
        [asyncResp, chassisID, sensorName,
         totalPowerPath](const boost::system::error_code& ec,
                         std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no endpoints = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            // Check chassisId for endpoint
            for (const std::string& endpointPath : *data)
            {
                sdbusplus::message::object_path objPath(endpointPath);
                const std::string& endpointId = objPath.filename();
                if (endpointId != chassisID)
                {
                    continue;
                }
                const std::array<const char*, 1> totalPowerInterfaces = {
                    "xyz.openbmc_project.Sensor.Value"};
                // Process sensor reading
                dbus::utility::async_method_call(
                    [asyncResp, chassisID, sensorName, totalPowerPath](
                        const boost::system::error_code& ec1,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& object) {
                        if (ec1)
                        {
                            BMCWEB_LOG_DEBUG("DBUS response error");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        for (const auto& tempObject : object)
                        {
                            const std::string& connectionName =
                                tempObject.first;
                            dbus::utility::async_method_call(
                                [asyncResp, sensorName, chassisID](
                                    const boost::system::error_code& innerError,
                                    const std::variant<double>& value) {
                                    if (innerError)
                                    {
                                        BMCWEB_LOG_DEBUG(
                                            "Can't get Power Watts!");
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }

                                    const double* attributeValue =
                                        std::get_if<double>(&value);
                                    if (attributeValue == nullptr)
                                    {
                                        // illegal property
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }
                                    std::string tempPath =
                                        "/redfish/v1/Chassis/" + chassisID +
                                        "/Sensors/";
                                    asyncResp->res.jsonValue["PowerWatts"] = {
                                        {"Reading", *attributeValue},
                                        {"DataSourceUri",
                                         tempPath + sensorName}};
                                    // look for the correct moduel power sensor
                                    // by the pattern
                                    // ProcessorModule_{instance_id}_Power{instance_id}
                                    std::size_t found =
                                        chassisID.find_last_of('_');
                                    std::string name = "ProcessorModule_";
                                    if (found != std::string::npos)
                                    {
                                        std::string index =
                                            chassisID.substr(found + 1, 1);
                                        name += index + "_Power";
                                    }
                                    /// Reading is the same in PowerWatt and
                                    /// PowerLimitWatts objects for module.
                                    if (sensorName.find(name) !=
                                        std::string::npos)
                                    {
                                        asyncResp->res
                                            .jsonValue["PowerLimitWatts"]
                                                      ["Reading"] =
                                            *attributeValue;
                                    }
                                },
                                connectionName, totalPowerPath,
                                "org.freedesktop.DBus.Properties", "Get",
                                "xyz.openbmc_project.Sensor.Value", "Value");
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetObject",
                    totalPowerPath, totalPowerInterfaces);
            }
        },
        "xyz.openbmc_project.ObjectMapper", totalPowerPath + "/chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline double joulesToKwh(const double& joules)
{
    const double jtoKwhFactor = 2.77777778e-7;
    return jtoKwhFactor * joules;
}

inline void getEnergyJoulesBySensorName(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& sensorName)
{
    const std::string& sensorPath =
        "/xyz/openbmc_project/sensors/energy/" + sensorName;
    // Add total power sensor to associated chassis only
    dbus::utility::async_method_call(
        [asyncResp, chassisID, sensorName,
         sensorPath](const boost::system::error_code& ec,
                     std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no endpoints = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            // Check chassisId for endpoint
            for (const std::string& endpointPath : *data)
            {
                sdbusplus::message::object_path objPath(endpointPath);
                const std::string& endpointId = objPath.filename();
                if (endpointId != chassisID)
                {
                    continue;
                }
                const std::array<const char*, 1> energyJoulesInterfaces = {
                    "xyz.openbmc_project.Sensor.Value"};
                // Process sensor reading
                dbus::utility::async_method_call(
                    [asyncResp, chassisID, sensorName, sensorPath](
                        const boost::system::error_code& ec1,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& object) {
                        if (ec1)
                        {
                            BMCWEB_LOG_DEBUG("DBUS response error");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        for (const auto& tempObject : object)
                        {
                            const std::string& connectionName =
                                tempObject.first;
                            dbus::utility::async_method_call(
                                [asyncResp, sensorName, chassisID](
                                    const boost::system::error_code& innerError,
                                    const std::variant<double>& value) {
                                    if (innerError)
                                    {
                                        BMCWEB_LOG_DEBUG(
                                            "Can't get Energy Joules!");
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }

                                    const double* attributeValue =
                                        std::get_if<double>(&value);
                                    if (attributeValue == nullptr)
                                    {
                                        // illegal property
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }
                                    std::string tempPath =
                                        "/redfish/v1/Chassis/" + chassisID +
                                        "/Sensors/";
                                    asyncResp->res.jsonValue["EnergykWh"] = {
                                        {"Reading",
                                         joulesToKwh(*attributeValue)},
                                    };
                                    asyncResp->res.jsonValue["EnergyJoules"] = {
                                        {"Reading", *attributeValue},
                                        {"DataSourceUri",
                                         tempPath + sensorName}};
                                },
                                connectionName, sensorPath,
                                "org.freedesktop.DBus.Properties", "Get",
                                "xyz.openbmc_project.Sensor.Value", "Value");
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetObject", sensorPath,
                    energyJoulesInterfaces);
            }
        },
        "xyz.openbmc_project.ObjectMapper", sensorPath + "/chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getPowerWattsEnergyJoules(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& chassisPath)
{
    dbus::utility::async_method_call(
        [asyncResp, chassisID,
         chassisPath](const boost::system::error_code& ec,
                      std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no endpoints = no failures
            }

            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }

            // Check chassisId for endpoint
            for (const std::string& endpoint : *data)
            {
                std::string powerSensorName = chassisID + "_Power";

                // find power sensor
                if (endpoint.find("/power/") != std::string::npos &&
                    ((endpoint.find(powerSensorName) != std::string::npos) ||
                     (endpoint.find(BMCWEB_PLATFORM_TOTAL_POWER_SENSOR_NAME) !=
                      std::string::npos)))
                {
                    sdbusplus::message::object_path endpointPath(endpoint);
                    getPowerWattsBySensorName(asyncResp, chassisID,
                                              endpointPath.filename());
                }
                else if (endpoint.find("/energy/") != std::string::npos)
                {
                    sdbusplus::message::object_path endpointPath(endpoint);
                    getEnergyJoulesBySensorName(asyncResp, chassisID,
                                                endpointPath.filename());
                }
            }
        },
        "xyz.openbmc_project.ObjectMapper", chassisPath + "/all_sensors",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

// Helper function to get temperature reading from a sensor path
inline void getTemperatureCelsiusBySensorPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& sensorPath)
{
    sdbusplus::message::object_path objPath(sensorPath);
    const std::string sensorName = objPath.filename();

    const std::array<const char*, 1> sensorInterfaces = {
        "xyz.openbmc_project.Sensor.Value"};

    dbus::utility::async_method_call(
        [asyncResp, chassisID, sensorName, sensorPath](
            const boost::system::error_code& ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                object) {
            if (ec || object.empty())
            {
                BMCWEB_LOG_DEBUG("Failed to find service for sensor {}",
                                 sensorPath);
                return;
            }

            const std::string& service = object.front().first;

            dbus::utility::async_method_call(
                [asyncResp, chassisID,
                 sensorName](const boost::system::error_code& ec2,
                             const std::variant<double>& value) {
                    if (ec2)
                    {
                        BMCWEB_LOG_DEBUG("Failed to get temperature value");
                        return;
                    }
                    const double* reading = std::get_if<double>(&value);
                    if (reading == nullptr || std::isnan(*reading))
                    {
                        return;
                    }

                    std::string sensorURI =
                        boost::urls::format("/redfish/v1/Chassis/{}/Sensors/{}",
                                            chassisID, sensorName)
                            .buffer();

                    asyncResp->res.jsonValue["TemperatureCelsius"] = {
                        {"Reading", *reading},
                        {"DataSourceUri", sensorURI},
                    };
                },
                service, sensorPath, "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Sensor.Value", "Value");
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", sensorPath,
        sensorInterfaces);
}

// Get primary temperature sensor for Chassis EnvironmentMetrics
// Uses primary_temperature_sensor association to get the correct sensor
inline void getTemperatureCelsius(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& chassisPath)
{
    // First try primary_temperature_sensor association
    dbus::utility::async_method_call(
        [asyncResp, chassisID,
         chassisPath](const boost::system::error_code& ec,
                      std::variant<std::vector<std::string>>& resp) {
            std::string sensorPath;

            if (!ec)
            {
                std::vector<std::string>* data =
                    std::get_if<std::vector<std::string>>(&resp);
                if (data != nullptr && !data->empty())
                {
                    // Use primary temperature sensor
                    sensorPath = data->front();
                    BMCWEB_LOG_DEBUG(
                        "Using primary_temperature_sensor for chassis {}: {}",
                        chassisID, sensorPath);
                }
            }

            // If no primary sensor found, fall back to all_sensors
            if (sensorPath.empty())
            {
                dbus::utility::async_method_call(
                    [asyncResp,
                     chassisID](const boost::system::error_code& ec2,
                                std::variant<std::vector<std::string>>& resp2) {
                        if (ec2)
                        {
                            return;
                        }
                        std::vector<std::string>* data2 =
                            std::get_if<std::vector<std::string>>(&resp2);
                        if (data2 == nullptr)
                        {
                            return;
                        }
                        // Find first temperature sensor (fallback behavior)
                        for (const std::string& endpoint : *data2)
                        {
                            if (endpoint.find("/temperature/") !=
                                std::string::npos)
                            {
                                sdbusplus::message::object_path endpointPath(
                                    endpoint);
                                getTemperatureCelsiusBySensorPath(
                                    asyncResp, chassisID, endpoint);
                                return;
                            }
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    chassisPath + "/all_sensors",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
                return;
            }

            // Get temperature from primary sensor
            getTemperatureCelsiusBySensorPath(asyncResp, chassisID, sensorPath);
        },
        "xyz.openbmc_project.ObjectMapper",
        chassisPath + "/primary_temperature_sensor",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getPowerReadings(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& objPath,
    const std::string& chassisID)
{
    // Add get sensor name  from power control
    dbus::utility::async_method_call(
        [asyncResp, chassisID,
         connectionName](const boost::system::error_code& ec,
                         std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("get power control sensor failed");
                return; // no endpoints = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_ERROR("null value returned for  control sensor");
                return;
            }
            // power control sensor
            for (const std::string& sensorPath : *data)
            {
                sdbusplus::message::object_path objPath1(sensorPath);
                const std::string& sensorName = objPath1.filename();

                // Process sensor reading
                dbus::utility::async_method_call(
                    [asyncResp, sensorName,
                     chassisID](const boost::system::error_code& ec1,
                                const std::variant<double>& value) {
                        if (ec1)
                        {
                            BMCWEB_LOG_DEBUG("Can't get Power Watts!");
                            return;
                        }
                        const double* attributeValue =
                            std::get_if<double>(&value);
                        if (attributeValue == nullptr)
                        {
                            return;
                        }
                    },
                    connectionName, sensorPath,
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Sensor.Value", "Value");
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/sensor",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getDefaultPowerCap(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath)
{
    const std::array<const char*, 1> clearPowerCapInterfaces = {
        "com.nvidia.Common.ClearPowerCap"};
    dbus::utility::async_method_call(
        [asyncResp, objPath](
            const boost::system::error_code& errorno,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
            if (errorno)
            {
                BMCWEB_LOG_ERROR("ObjectMapper::GetObject call failed: {}",
                                 errorno);
                return;
            }

            for (const auto& element : objInfo)
            {
                dbus::utility::async_method_call(
                    [asyncResp,
                     objPath](const boost::system::error_code& ec,
                              const std::vector<std::pair<
                                  std::string, std::variant<uint32_t, bool>>>&
                                  propertiesList) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR("DBUS response error for "
                                             "Chassis properties");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        for (const std::pair<std::string,
                                             std::variant<uint32_t, bool>>&
                                 property : propertiesList)
                        {
                            const std::string& propertyName = property.first;
                            if (propertyName == "DefaultPowerCap")
                            {
                                const uint32_t* value =
                                    std::get_if<uint32_t>(&property.second);
                                if (value == nullptr)
                                {
                                    BMCWEB_LOG_ERROR("Null value returned "
                                                     "for type");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                asyncResp->res.jsonValue["PowerLimitWatts"]
                                                        ["DefaultSetPoint"] =
                                    *value;
                            }
                        }
                    },
                    element.first, objPath, "org.freedesktop.DBus.Properties",
                    "GetAll", "com.nvidia.Common.ClearPowerCap");
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
        clearPowerCapInterfaces);
}

inline void getPowerCap(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& chassisID,
                        const std::string& objPath)
{
    const std::array<const char*, 1> powerCapInterfaces = {
        "xyz.openbmc_project.Control.Power.Cap"};
    dbus::utility::async_method_call(
        [asyncResp, chassisID, objPath](
            const boost::system::error_code& errorno,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
            if (errorno)
            {
                BMCWEB_LOG_ERROR("ObjectMapper::GetObject call failed: {}",
                                 errorno);
                messages::internalError(asyncResp->res);
                return;
            }

            for (const auto& element : objInfo)
            {
                dbus::utility::async_method_call(
                    [asyncResp,
                     objPath](const boost::system::error_code& ec,
                              const std::vector<std::pair<
                                  std::string, std::variant<uint32_t, bool>>>&
                                  propertiesList) {
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG("DBUS response error for "
                                             "Chassis properties");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        for (const std::pair<std::string,
                                             std::variant<uint32_t, bool>>&
                                 property : propertiesList)
                        {
                            const std::string& propertyName = property.first;
                            if (propertyName == "PowerCap")
                            {
                                const uint32_t* value =
                                    std::get_if<uint32_t>(&property.second);
                                if (value == nullptr)
                                {
                                    BMCWEB_LOG_DEBUG("Null value returned "
                                                     "for type");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                asyncResp->res
                                    .jsonValue["PowerLimitWatts"]["SetPoint"] =
                                    *value;
                            }
                            else if (propertyName == "MinPowerCapValue")
                            {
                                const uint32_t* value =
                                    std::get_if<uint32_t>(&property.second);
                                if (value == nullptr)
                                {
                                    BMCWEB_LOG_DEBUG("Null value returned "
                                                     "for type");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                asyncResp->res.jsonValue["PowerLimitWatts"]
                                                        ["AllowableMin"] =
                                    *value;
                            }
                            else if (propertyName == "MaxPowerCapValue")
                            {
                                const uint32_t* value =
                                    std::get_if<uint32_t>(&property.second);
                                if (value == nullptr)
                                {
                                    BMCWEB_LOG_DEBUG("Null value returned "
                                                     "for type");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                asyncResp->res.jsonValue["PowerLimitWatts"]
                                                        ["AllowableMax"] =
                                    *value;
                            }
                            else if (propertyName == "PowerCapEnable")
                            {
                                const bool* value =
                                    std::get_if<bool>(&property.second);
                                if (value == nullptr)
                                {
                                    BMCWEB_LOG_DEBUG("Null value returned "
                                                     "for type");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }

                                if (*value)
                                {
                                    asyncResp->res.jsonValue["PowerLimitWatts"]
                                                            ["ControlMode"] =
                                        "Automatic";
                                }
                                else
                                {
                                    asyncResp->res.jsonValue["PowerLimitWatts"]
                                                            ["ControlMode"] =
                                        "Disabled";
                                }
                            }
                            else if (propertyName == "DefaultPowerCap")
                            {
                                const uint32_t* value =
                                    std::get_if<uint32_t>(&property.second);
                                if (value == nullptr)
                                {
                                    BMCWEB_LOG_DEBUG("Null value returned "
                                                     "for type");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                asyncResp->res.jsonValue["PowerLimitWatts"]
                                                        ["DefaultSetPoint"] =
                                    *value;
                            }
                        }
                    },
                    element.first, objPath, "org.freedesktop.DBus.Properties",
                    "GetAll", "xyz.openbmc_project.Control.Power.Cap");
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
        powerCapInterfaces);
    getDefaultPowerCap(asyncResp, objPath);
}

inline void getEDPpData(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& connectionName,
                        const std::string& objPath)
{
    dbus::utility::async_method_call(
        [asyncResp, connectionName,
         objPath](const boost::system::error_code& ec,
                  const SetPointProperties& properties) {
            if (ec || properties.empty())
            {
                BMCWEB_LOG_DEBUG("DBUS response error for "
                                 "procesor EDPp scaling properties");
                messages::internalError(asyncResp->res);
                return;
            }

            for (const auto& [key, variant] : properties)
            {
                if (key == "SetPoint")
                {
                    using SetPointProperty = std::tuple<size_t, bool>;
                    const auto* setPoint =
                        std::get_if<SetPointProperty>(&variant);
                    if (setPoint != nullptr)
                    {
                        const auto& [limit, persistency] = *setPoint;
                        asyncResp->res.jsonValue["Oem"]["Nvidia"]["EDPpPercent"]
                                                ["SetPoint"] = limit;
                        asyncResp->res.jsonValue["Oem"]["Nvidia"]["EDPpPercent"]
                                                ["Persistency"] = persistency;
                    }
                }
                else if (key == "AllowableMax")
                {
                    const size_t* value = std::get_if<size_t>(&variant);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for type");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["EDPpPercent"]
                                            ["AllowableMax"] = *value;
                }
                else if (key == "AllowableMin")
                {
                    const size_t* value = std::get_if<size_t>(&variant);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for type");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["EDPpPercent"]
                                            ["AllowableMin"] = *value;
                }
            }
        },
        connectionName, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.Edpp");
}

inline void getPowerLimitPersistency(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& objPath)
{
    dbus::utility::async_method_call(
        [asyncResp, connectionName,
         objPath](const boost::system::error_code& ec,
                  const SetPointProperties& properties) {
            if (ec || properties.empty())
            {
                BMCWEB_LOG_DEBUG("DBUS response error for "
                                 "procesor EDPp scaling properties");
                messages::internalError(asyncResp->res);
                return;
            }
            for (const auto& property : properties)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "Persistency")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for Persistency");
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    asyncResp->res
                        .jsonValue["Oem"]["Nvidia"]["PowerLimitPersistency"] =
                        *value;
                }
                else if (propertyName == "PersistentPowerLimit")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for Persistent Power Limit");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res
                        .jsonValue["Oem"]["Nvidia"]
                                  ["RequestedPersistentPowerLimitWatts"] =
                        *value;
                }
                else if (propertyName == "OneShotPowerLimit")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for OneShot Power Limit");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res
                        .jsonValue["Oem"]["Nvidia"]
                                  ["RequestedOneshotPowerLimitWatts"] = *value;
                }
            }
        },
        connectionName, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Control.Power.Persistency");
}

inline void getPowerLimits(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& objPath)
{
    dbus::utility::async_method_call(
        [asyncResp, connectionName, objPath](
            const boost::system::error_code& ec,
            const std::vector<std::pair<std::string, std::variant<uint32_t>>>&
                propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for "
                                 "Chassis properties");
                messages::internalError(asyncResp->res);
                return;
            }
            for (const std::pair<std::string, std::variant<uint32_t>>&
                     property : propertiesList)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "MaxPowerWatts")
                {
                    const uint32_t* value =
                        std::get_if<uint32_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for type");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res
                        .jsonValue["PowerLimitWatts"]["AllowableMax"] = *value;
                }
                if (propertyName == "MinPowerWatts")
                {
                    const uint32_t* value =
                        std::get_if<uint32_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for type");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res
                        .jsonValue["PowerLimitWatts"]["AllowableMin"] = *value;
                }
            }
        },
        connectionName, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Inventory.Decorator.PowerLimit");
}

inline void getPowerLimitDataSourceUri(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& ctrlPath)
{
    sdbusplus::message::object_path path(ctrlPath);
    const std::string name = path.filename();
    asyncResp->res.jsonValue["PowerLimitWatts"]["DataSourceUri"] =
        "/redfish/v1/Chassis/" + chassisID + "/Controls/" + name;
}

inline void getControlMode(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& objPath)
{
    dbus::utility::async_method_call(
        [asyncResp, connectionName,
         objPath](const boost::system::error_code& ec,
                  const std::vector<std::pair<std::string, std::variant<bool>>>&
                      propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for "
                                 "Chassis properties");
                messages::internalError(asyncResp->res);
                return;
            }
            for (const std::pair<std::string, std::variant<bool>>& property :
                 propertiesList)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "Manual")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for type");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    std::string controlMode = "Automatic";
                    if (*value)
                    {
                        controlMode = "Manual";
                    }
                    asyncResp->res.jsonValue["PowerLimitWatts"]["ControlMode"] =
                        controlMode;
                }
            }
        },
        connectionName, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Control.Mode");
}

template <std::size_t SIZE>
inline void getPowerAndControlData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& resourceId,
    const std::array<std::string_view, SIZE>& interfaces)
{
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [asyncResp, resourceId](const boost::system::error_code& ec,
                                const dbus::utility::GetSubTreeType& subtree) {
            if (ec)
            {
                return;
            }

            // Iterate over all retrieved ObjectPaths.
            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                     object : subtree)
            {
                const std::string& path = object.first;
                const std::vector<
                    std::pair<std::string, std::vector<std::string>>>&
                    connectionNames = object.second;

                sdbusplus::message::object_path objPath(path);
                if (objPath.filename() != resourceId)
                {
                    continue;
                }

                if (connectionNames.empty())
                {
                    BMCWEB_LOG_ERROR("Got 0 Connection names");
                    continue;
                }

                const std::string& connectionName = connectionNames[0].first;
                const std::vector<std::string>& interfaceList =
                    connectionNames[0].second;

                if (std::find(interfaceList.begin(), interfaceList.end(),
                              "xyz.openbmc_project.Inventory.Item.Cpu") !=
                    interfaceList.end())
                {
                    // Skip PowerAndControlData for
                    // /Chassis/CPU_{ID}/EnvironmentMetrics URI The CPU power
                    // cap is handled by
                    // /Systems/{ID}/Processor/CPU_{ID}/Controls URI
                    continue;
                }

                dbus::utility::async_method_call(
                    [asyncResp, connectionName, interfaceList, resourceId](
                        const boost::system::error_code& e,
                        std::variant<std::vector<std::string>>& resp1) {
                        if (e)
                        {
                            return;
                        }
                        std::vector<std::string>* data1 =
                            std::get_if<std::vector<std::string>>(&resp1);
                        if (data1 == nullptr)
                        {
                            return;
                        }
                        for (const std::string& ctrlPath : *data1)
                        {
                            getPowerCap(asyncResp, connectionName, ctrlPath);
                            getPowerCap(asyncResp, resourceId, ctrlPath);
                            // Skip getControlMode if it does not support the
                            // Control Mode
                            if (std::find(interfaceList.begin(),
                                          interfaceList.end(),
                                          "xyz.openbmc_project.Control.Mode") !=
                                interfaceList.end())
                            {
                                getControlMode(asyncResp, connectionName,
                                               ctrlPath);
                            }
                            if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                            {
                                getPowerMode(asyncResp, connectionName,
                                             ctrlPath);
                                getClearPowerCap(asyncResp, resourceId,
                                                 ctrlPath);
                            }
                            getPowerReadings(asyncResp, connectionName,
                                             ctrlPath, resourceId);
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    path + "/power_controls", "org.freedesktop.DBus.Properties",
                    "Get", "xyz.openbmc_project.Association", "endpoints");
            }
        });
}

/**
 * Handle the PATCH operation of the power limit property. Do basic
 * validation of the input data, and then set the D-Bus property.
 *
 * @param[in,out]   resp            Async HTTP response.
 * @param[in]       resourceId       Resource Id.
 * @param[in]       powerLimit      New property value to apply.
 * @param[in]       objectPath      Path of resource object to modify.
 * @param[in]       serviceName      Service for resource object.
 */
inline void patchPowerLimit(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                            const std::string& resourceId, const int powerLimit,
                            const std::string& objectPath,
                            const std::string& resourceType,
                            const bool persistency = false)
{
    const std::array<const char*, 1> powerCapInterfaces = {
        "xyz.openbmc_project.Control.Power.Cap"};
    dbus::utility::async_method_call(
        [resp, resourceId, persistency, powerLimit, resourceType, objectPath](
            const boost::system::error_code& errorno,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
            if (errorno)
            {
                BMCWEB_LOG_ERROR("ObjectMapper::GetObject call failed: {}",
                                 errorno);
                messages::internalError(resp->res);
                return;
            }
            for (const auto& element : objInfo)
            {
                dbus::utility::getDbusObject(
                    objectPath,
                    std::array<std::string_view, 1>{
                        nvidia_async_operation_utils::setAsyncInterfaceName},
                    [resp, objectPath, powerLimit, element, persistency,
                     resourceId, resourceType](
                        const boost::system::error_code& ec,
                        const dbus::utility::MapperGetObject& object) {
                        if (!ec)
                        {
                            for (const auto& [serv, _] : object)
                            {
                                if (serv != element.first)
                                {
                                    continue;
                                }

                                std::tuple<bool, uint32_t> reqPowerLimit(
                                    persistency,
                                    static_cast<uint32_t>(powerLimit));

                                if (resourceType == "Processors")
                                {
                                    nvidia_async_operation_utils::
                                        doGenericSetAsyncAndGatherResult(
                                            resp, std::chrono::seconds(60),
                                            element.first, objectPath,
                                            "xyz.openbmc_project.Control.Power.Cap",
                                            "PowerCap",
                                            std::variant<
                                                std::tuple<bool, uint32_t>>(
                                                reqPowerLimit),
                                            nvidia_async_operation_utils::
                                                PatchPowerCapCallback{
                                                    resp, powerLimit});
                                }
                                else
                                {
                                    nvidia_async_operation_utils::
                                        doGenericSetAsyncAndGatherResult(
                                            resp, std::chrono::seconds(60),
                                            element.first, objectPath,
                                            "xyz.openbmc_project.Control.Power.Cap",
                                            "PowerCap",
                                            std::variant<uint32_t>(
                                                static_cast<uint32_t>(
                                                    powerLimit)),
                                            nvidia_async_operation_utils::
                                                PatchPowerCapCallback{
                                                    resp, powerLimit});
                                }

                                return;
                            }
                        }

                        BMCWEB_LOG_DEBUG(
                            "Performing Patch using set-property Call");

                        // Set the property, with handler to check error
                        // responses
                        dbus::utility::setProperty(
                            element.first, objectPath,
                            "xyz.openbmc_project.Control.Power.Cap", "PowerCap",
                            static_cast<uint32_t>(powerLimit),
                            [resp, resourceId, powerLimit,
                             resourceType](const boost::system::error_code& ec1,
                                           sdbusplus::message::message& msg) {
                                if (!ec1)
                                {
                                    BMCWEB_LOG_DEBUG(
                                        "Set power limit property succeeded");
                                    messages::success(resp->res);
                                    return;
                                }

                                BMCWEB_LOG_ERROR(
                                    "{}: {} set power limit property failed: {}",
                                    resourceType, resourceId, ec1);
                                // Read and convert dbus error message to
                                // redfish error
                                const sd_bus_error* dbusError = msg.get_error();
                                if (dbusError == nullptr)
                                {
                                    messages::internalError(resp->res);
                                    return;
                                }
                                if (strcmp(
                                        dbusError->name,
                                        "xyz.openbmc_project.Common.Error.InvalidArgument") ==
                                    0)
                                {
                                    // Invalid value
                                    messages::propertyValueIncorrect(
                                        resp->res, "powerLimit",
                                        std::to_string(powerLimit));
                                }
                                else if (
                                    strcmp(
                                        dbusError->name,
                                        "xyz.openbmc_project.Common.Error.Unavailable") ==
                                    0)
                                {
                                    std::string errBusy = "0x50A";
                                    std::string errBusyResolution =
                                        "SMBPBI Command failed with error busy, please try after 60 seconds";

                                    // busy error
                                    messages::asyncError(resp->res, errBusy,
                                                         errBusyResolution);
                                }
                                else if (
                                    strcmp(
                                        dbusError->name,
                                        "xyz.openbmc_project.Common.Error.Timeout") ==
                                    0)
                                {
                                    std::string errTimeout = "0x600";
                                    std::string errTimeoutResolution =
                                        "Settings may/maynot have applied, please check get response before patching";

                                    // timeout error
                                    messages::asyncError(resp->res, errTimeout,
                                                         errTimeoutResolution);
                                }
                                else if (strcmp(dbusError->name,
                                                "xyz.openbmc_project.Common."
                                                "Device.Error.WriteFailure") ==
                                         0)
                                {
                                    // Service failed to change the config
                                    messages::operationFailed(resp->res);
                                }
                                else
                                {
                                    messages::internalError(resp->res);
                                }
                            });
                    });
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objectPath,
        powerCapInterfaces);
}

inline void patchBasePowerWattsByService(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& basePowerWattPath, const std::string& resourceId,
    const uint32_t basePowerWatts, const bool persistency = false)
{
    BMCWEB_LOG_DEBUG(
        "Patch base power watts by service. basePowerWattPath: {}, resourceId: {}, basePowerWatts: {}, persistency: {}",
        basePowerWattPath, resourceId, basePowerWatts, persistency);
    dbus::utility::getDbusObject(
        basePowerWattPath,
        std::array<std::string_view, 1>{
            nvidia_async_operation_utils::setAsyncInterfaceName},
        [resp, basePowerWatts, resourceId, basePowerWattPath,
         persistency](const boost::system::error_code& ec,
                      const dbus::utility::MapperGetObject& object) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "Failed to patch base power watts: {} for resourceId: {}",
                    ec.message(), resourceId);
                messages::resourceNotFound(
                    resp->res, "#Processor.v1_20_0.Processor", resourceId);
                return;
            }
            for (const auto& [serv, interfaces] : object)
            {
                if (std::find(interfaces.begin(), interfaces.end(),
                              "xyz.openbmc_project.Control.Power.Cap") ==
                    interfaces.end())
                {
                    continue;
                }

                BMCWEB_LOG_DEBUG(
                    "Performing Patch using Set Async Method Call");
                std::tuple<bool, uint32_t> reqPowerLimit(persistency,
                                                         basePowerWatts);
                nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
                    resp, std::chrono::seconds(60), serv, basePowerWattPath,
                    "xyz.openbmc_project.Control.Power.Cap", "PowerCap",
                    std::variant<std::tuple<bool, uint32_t>>(reqPowerLimit),
                    nvidia_async_operation_utils::PatchBasePowerWattsCallback{
                        resp});

                break;
            }
        });
}

inline void patchBasePowerWatts(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                                const std::string& resourceId,
                                const uint32_t basePowerWatts,
                                const bool persistency = false)
{
    BMCWEB_LOG_DEBUG(
        "Patch base power watts. resourceId: {}, basePowerWatts: {}, persistency: {}",
        resourceId, basePowerWatts, persistency);
    constexpr std::array<std::string_view, 1> processorInterfaces = {
        "com.nvidia.GPMMetrics"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, processorInterfaces,
        [resp, resourceId, basePowerWatts,
         persistency](const boost::system::error_code& ec,
                      const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Failed to patch base power watts: {}",
                                 ec.message());
                messages::internalError(resp->res);
                return;
            }

            for (const auto& [path, object] : subtree)
            {
                if (!path.ends_with(resourceId))
                {
                    continue;
                }

                dbus::utility::getAssociationEndPoints(
                    path + "/Base_Power_Limit",
                    [resp, resourceId, basePowerWatts, persistency](
                        const boost::system::error_code& e,
                        const dbus::utility::MapperEndPoints& endpoints) {
                        if (e)
                        {
                            BMCWEB_LOG_DEBUG(
                                "Failed to get Base Power Limit: {}",
                                e.message());
                            return;
                        }

                        for (const auto& basePowerWattPath : endpoints)
                        {
                            patchBasePowerWattsByService(
                                resp, basePowerWattPath, resourceId,
                                basePowerWatts, persistency);
                        }
                    });
            }
        });
}

inline void getSensorDataByService(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& chassisId, const std::string& objPath,
    const std::string& resourceType, bool isSupportPowerLimit = false)
{
    BMCWEB_LOG_DEBUG("Get sensor data.");
    using PropertyType =
        std::variant<std::string, double, uint64_t, std::vector<std::string>>;
    using PropertiesMap = boost::container::flat_map<std::string, PropertyType>;
    dbus::utility::async_method_call(
        [aResp, chassisId, resourceType, objPath,
         isSupportPowerLimit](const boost::system::error_code& ec,
                              const PropertiesMap& properties) {
            if (ec || properties.empty())
            {
                BMCWEB_LOG_DEBUG("Can't get sensor reading for {}", objPath);
                // Not reporting Internal Failure for services that dont host
                // sensor path in case of Processor Env Eg: GpuOobRecovery in
                // case of FPGA Processor
                if (resourceType != "Processor")
                {
                    // Not reporting Internal Failure because we might have
                    // another service with the same objpath to set up config
                    // only. Eg: PartLoaction
                    BMCWEB_LOG_WARNING(
                        "Can't get Processor sensor DBus properties {}",
                        objPath);
                }
                return;
            }
            for (const auto& property : properties)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "Value")
                {
                    const double* attributeValue =
                        std::get_if<double>(&property.second);
                    // Take relevant code from sensors
                    std::vector<std::string> split;
                    // Reserve space for
                    // /xyz/openbmc_project/sensors/<name>/<subname>
                    split.reserve(6);
                    bmcweb::split(split, objPath, '/');
                    if (split.size() < 6)
                    {
                        BMCWEB_LOG_ERROR("Got path that isn't long enough {}",
                                         objPath);
                        return;
                    }
                    // These indexes aren't intuitive, as boost::split puts an
                    // empty string at the beginning
                    const std::string& sensorType = split[4];
                    const std::string& sensorName = split[5];
                    BMCWEB_LOG_DEBUG("sensorName {} sensorType {}", sensorName,
                                     sensorType);

                    std::string sensorURI =
                        boost::urls::format("/redfish/v1/Chassis/{}/Sensors/{}",
                                            chassisId, sensorName)
                            .buffer();
                    if (sensorType == "temperature")
                    {
                        aResp->res.jsonValue["TemperatureCelsius"] = {
                            {"Reading", *attributeValue},
                            {"DataSourceUri", sensorURI},
                        };
                    }
                    else if (sensorType == "power")
                    {
                        aResp->res.jsonValue["PowerWatts"] = {
                            {"Reading", *attributeValue},
                            {"DataSourceUri", sensorURI},
                        };
                        if (isSupportPowerLimit)
                        {
                            aResp->res.jsonValue["PowerLimitWatts"]["Reading"] =
                                *attributeValue;
                        }
                    }
                    else if (sensorType == "energy")
                    {
                        aResp->res.jsonValue["EnergykWh"] = {
                            {"Reading", joulesToKwh(*attributeValue)},
                        };
                        aResp->res.jsonValue["EnergyJoules"] = {
                            {"Reading", *attributeValue},
                            {"DataSourceUri", sensorURI},
                        };
                    }
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll", "");
}

inline void getSensorDataService(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    [[maybe_unused]] const std::string& service, const std::string& chassisId,
    const std::string& objPath, const std::string& resourceType,
    bool isSupportPowerLimit = false)
{
    BMCWEB_LOG_DEBUG("Get sensor service.");

    const std::array<const char*, 1> sensorInterfaces = {
        "xyz.openbmc_project.Sensor.Value"};
    // Process sensor reading
    dbus::utility::async_method_call(
        [aResp, chassisId, resourceType, objPath, isSupportPowerLimit](
            const boost::system::error_code& ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                object) {
            if (ec)
            {
                // The path does not implement any state interfaces.
                return;
            }

            for (const auto& [serviceEntry, interfaces] : object)
            {
                if (std::find(interfaces.begin(), interfaces.end(),
                              "xyz.openbmc_project.Sensor.Value") !=
                    interfaces.end())
                {
                    getSensorDataByService(aResp, serviceEntry, chassisId,
                                           objPath, resourceType,
                                           isSupportPowerLimit);
                }
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
        sensorInterfaces);
}

// Helper function to query all_sensors association for processor metrics
inline void queryAllSensorsForProcessorMetrics(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& resourceType, const std::string& chassisId,
    const std::string& chassisPath, bool skipTemperatureSensors,
    bool isSupportPowerLimit = false)
{
    dbus::utility::async_method_call(
        [aResp, resourceType, chassisId, skipTemperatureSensors,
         isSupportPowerLimit](const boost::system::error_code& e,
                              std::variant<std::vector<std::string>>& resp) {
            if (e)
            {
                BMCWEB_LOG_ERROR("Failed to get all sensors: {}", e.message());
                messages::internalError(aResp->res);
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            for (const std::string& sensorPath : *data)
            {
                // Skip temperature sensors if primary was found
                // to avoid overwriting the correct DataSourceUri
                if (skipTemperatureSensors &&
                    sensorPath.find("/temperature/") != std::string::npos)
                {
                    continue;
                }
                getSensorDataService(aResp, "", chassisId, sensorPath,
                                     resourceType, isSupportPowerLimit);
            }
        },
        "xyz.openbmc_project.ObjectMapper", chassisPath + "/all_sensors",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

// Helper function to query primary_temperature_sensor, then all_sensors
inline void queryPrimaryTempAndAllSensors(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& resourceType, const std::string& chassisId,
    const std::string& chassisPath, bool isSupportPowerLimit = false)
{
    dbus::utility::async_method_call(
        [aResp, resourceType, chassisId, chassisPath, isSupportPowerLimit](
            const boost::system::error_code& primaryEc,
            std::variant<std::vector<std::string>>& primaryResp) {
            bool foundPrimary = false;
            if (!primaryEc)
            {
                std::vector<std::string>* primarySensors =
                    std::get_if<std::vector<std::string>>(&primaryResp);
                if (primarySensors != nullptr && !primarySensors->empty())
                {
                    // Use the primary temperature sensor
                    BMCWEB_LOG_DEBUG("Using primary_temperature_sensor: {}",
                                     primarySensors->front());
                    getSensorDataService(aResp, "", chassisId,
                                         primarySensors->front(), resourceType,
                                         isSupportPowerLimit);
                    foundPrimary = true;
                }
            }
            // Query all_sensors for power, energy, voltage sensors
            // (skip temperature if primary was found)
            queryAllSensorsForProcessorMetrics(aResp, resourceType, chassisId,
                                               chassisPath, foundPrimary,
                                               isSupportPowerLimit);
        },
        "xyz.openbmc_project.ObjectMapper",
        chassisPath + "/primary_temperature_sensor",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getEnvironmentMetricsDataByService(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath, const std::string& resourceType,
    bool isSupportPowerLimit = false)
{
    BMCWEB_LOG_DEBUG("Get environment metrics data.");
    // Get parent chassis for sensors URI
    dbus::utility::async_method_call(
        [aResp, service, resourceType, objPath,
         isSupportPowerLimit](const boost::system::error_code& ec,
                              std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no chassis = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr || data->empty())
            {
                // Object must have single parent chassis
                return;
            }
            const std::string& chassisPath = data->front();
            sdbusplus::message::object_path objectPath(chassisPath);
            std::string chassisName = objectPath.filename();
            if (chassisName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            const std::string& chassisId = chassisName;

            // Query primary temperature sensor first, then all sensors
            // (fixes NVBug 5229182 - ensures correct DataSourceUri)
            queryPrimaryTempAndAllSensors(aResp, resourceType, chassisId,
                                          chassisPath, isSupportPowerLimit);
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getMemoryEnvironmentMetricsDataByService(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath, bool isSupportPowerLimit = false)
{
    BMCWEB_LOG_DEBUG("Get environment metrics data.");

    // Get parent chassis for sensors URI
    dbus::utility::async_method_call(
        [aResp, service, objPath,
         isSupportPowerLimit](const boost::system::error_code& ec,
                              std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no chassis = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr || data->empty())
            {
                // Object must have single parent chassis
                return;
            }
            const std::string& chassisPath = data->front();
            sdbusplus::message::object_path objectPath(chassisPath);
            std::string chassisName = objectPath.filename();
            if (chassisName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            const std::string& chassisId = chassisName;
            dbus::utility::async_method_call(
                [aResp, service, chassisId, isSupportPowerLimit](
                    const boost::system::error_code& e,
                    std::variant<std::vector<std::string>>& sensorResp) {
                    if (e)
                    {
                        BMCWEB_LOG_ERROR("Failed to get all sensors: {}",
                                         e.message());
                        messages::internalError(aResp->res);
                        return;
                    }
                    std::vector<std::string>* sensorData =
                        std::get_if<std::vector<std::string>>(&sensorResp);
                    if (sensorData == nullptr)
                    {
                        return;
                    }
                    const std::string resourceType = "Memory";
                    for (const std::string& sensorPath : *sensorData)
                    {
                        getSensorDataByService(aResp, service, chassisId,
                                               sensorPath, resourceType,
                                               isSupportPowerLimit);
                    }
                },
                // all_sensors association to get sensors associated with dimm
                // Note: objPath belong to dimm where Item.Dimm iface is
                // implemented
                "xyz.openbmc_project.ObjectMapper", objPath + "/all_sensors",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Association", "endpoints");
        },
        // parent_chassis to get the chassis, dimm is present on
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getCpuEnvironmentMetricsDataByService(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get CPU environment metrics data.");
    // Get parent chassis for sensors URI
    dbus::utility::async_method_call(
        [aResp, service,
         objPath](const boost::system::error_code& ec,
                  std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no chassis = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr || data->empty())
            {
                // Object must have single parent chassis
                return;
            }
            const std::string& chassisPath = data->front();
            sdbusplus::message::object_path objectPath(chassisPath);
            std::string chassisName = objectPath.filename();
            if (chassisName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            const std::string& chassisId = chassisName;

            // Use the same priority logic as accelerators:
            // 1. primary_temperature_sensor association
            // 2. Fall back to any sensor from all_sensors
            // objPath is used as the base for both associations
            // since they live on the CPU inventory object
            queryPrimaryTempAndAllSensors(aResp, "Processor", chassisId,
                                          objPath);
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getCpuPowerCapData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath, const std::string& cpuId, bool persistence)
{
    BMCWEB_LOG_DEBUG("Get CPU power cap data.");
    dbus::utility::async_method_call(
        [aResp, service, objPath, cpuId,
         persistence](const boost::system::error_code& e,
                      const std::variant<bool>& value) {
            if (e)
            {
                // The path does not implement any state interfaces.
                return;
            }

            const bool* data = std::get_if<bool>(&value);
            if (data == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }
            if (persistence != *data)
            {
                // Not the sensor we expected
                return;
            }

            sdbusplus::message::object_path objectPath(objPath);
            std::string sensorName = objectPath.filename();
            if (sensorName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            std::string sensorURI =
                boost::urls::format("/redfish/v1/Chassis/{}/Controls/{}", cpuId,
                                    sensorName)
                    .buffer();
            aResp->res.jsonValue["PowerLimitWatts"]["DataSourceUri"] =
                sensorURI;

            getPowerCap(aResp, cpuId, objPath);
        },
        service, objPath, "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.State.Decorator.Persistence", "persistent");
}

inline void getCpuPowerCapService(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath, const std::string& cpuId)
{
    BMCWEB_LOG_DEBUG("Get CPU power cap service.");

    const std::array<const char*, 1> sensorInterfaces = {
        "xyz.openbmc_project.Control.Power.Cap"};
    // Process sensor reading
    dbus::utility::async_method_call(
        [aResp, service, objPath, cpuId](
            const boost::system::error_code& ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                object) {
            if (ec)
            {
                // The path does not implement any state interfaces.
                return;
            }

            for (const auto& [serviceEntry, interfaces] : object)
            {
                if (std::find(interfaces.begin(), interfaces.end(),
                              "xyz.openbmc_project.Control.Power.Cap") !=
                    interfaces.end())
                {
                    getCpuPowerCapData(aResp, serviceEntry, objPath, cpuId,
                                       true);
                }
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
        sensorInterfaces);
}

inline void getCpuPowerCapByService(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get CPU power Cap");
    // Get parent chassis for sensors URI
    dbus::utility::async_method_call(
        [aResp, service,
         objPath](const boost::system::error_code& ec,
                  std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no chassis = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr || data->empty())
            {
                // Object must have single parent chassis
                return;
            }
            const std::string& chassisPath = data->front();
            sdbusplus::message::object_path objectPath(chassisPath);
            std::string cpuName = objectPath.filename();
            if (cpuName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            const std::string& cpuId = cpuName;
            dbus::utility::async_method_call(
                [aResp, service, objPath,
                 cpuId](const boost::system::error_code& e,
                        std::variant<std::vector<std::string>>& powerResp) {
                    if (e)
                    {
                        // The path does not implement any power cap interfaces.
                        return;
                    }
                    std::vector<std::string>* data1 =
                        std::get_if<std::vector<std::string>>(&powerResp);
                    if (data1 == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Failed to get all sensors: {}",
                                         e.message());
                        messages::internalError(aResp->res);
                        return;
                    }
                    for (const std::string& sensorPath : *data1)
                    {
                        getCpuPowerCapService(aResp, service, sensorPath,
                                              cpuId);
                    }
                },
                "xyz.openbmc_project.ObjectMapper", objPath + "/power_controls",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Association", "endpoints");
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getGpuCopyCpuPowerLimitByServicePath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path, [[maybe_unused]] const std::string& gpuId)
{
    dbus::utility::getDbusObject(
        path, std::array<std::string_view, 0>{},
        [asyncResp, path](const boost::system::error_code& errorno,
                          const dbus::utility::MapperGetObject& objInfo) {
            if (errorno)
            {
                BMCWEB_LOG_ERROR("ObjectMapper::GetObject call failed: {}",
                                 errorno);
                messages::internalError(asyncResp->res);
                return;
            }

            for (const auto& element : objInfo)
            {
                for (const auto& interface : element.second)
                {
                    if (interface == "xyz.openbmc_project.Control.Power.Cap")
                    {
                        dbus::utility::getAllProperties(
                            element.first, path, interface,
                            [asyncResp](
                                const boost::system::error_code& errorno2,
                                const dbus::utility::DBusPropertiesMap&
                                    propertiesList) {
                                if (errorno2)
                                {
                                    BMCWEB_LOG_ERROR("GetAll call failed: {}",
                                                     errorno2);
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                for (const auto& property : propertiesList)
                                {
                                    if (property.first == "PowerCap")
                                    {
                                        const uint32_t* data =
                                            std::get_if<uint32_t>(
                                                &property.second);
                                        if (data == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "PowerCap property not of type uint32_t");
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }

                                        asyncResp->res
                                            .jsonValue["Oem"]["Nvidia"]
                                                      ["GPUViewCPULimitWatts"] =
                                            *data;
                                    }
                                }
                            });
                    }
                }
            }
        });
}

inline void getGpuCopyCpuPowerLimitPath(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& gpuId)
{
    dbus::utility::getAssociationEndPoints(
        objPath + "/GPU_copy_Cpu_Power",
        [aResp, gpuId](const boost::system::error_code& e,
                       const dbus::utility::MapperEndPoints& endpoints) {
            if (e)
            {
                BMCWEB_LOG_DEBUG("Failed to get GPU copy CPU power limit: {}",
                                 e.message());
                return;
            }
            for (const auto& path : endpoints)
            {
                getGpuCopyCpuPowerLimitByServicePath(aResp, path, gpuId);
                break;
            }
        });
}

inline void getBasePowerLimitValues(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path,
    const std::string& interface)
{
    dbus::utility::getAllProperties(
        service, path, interface,
        [asyncResp](const boost::system::error_code& errorno2,
                    const dbus::utility::DBusPropertiesMap& propertiesList) {
            if (errorno2)
            {
                BMCWEB_LOG_ERROR("ObjectMapper::GetObject call failed:{}",
                                 errorno2);
                messages::internalError(asyncResp->res);
                return;
            }
            for (const auto& property : propertiesList)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "PowerCap")
                {
                    const uint32_t* data =
                        std::get_if<uint32_t>(&property.second);
                    if (data == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "PowerCap property not of type uint32_t");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                            ["GPUBasePowerWatts"]["SetPoint"] =
                        *data;
                }
                else if (propertyName == "DefaultPowerCap")
                {
                    const uint32_t* data =
                        std::get_if<uint32_t>(&property.second);
                    if (data == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "DefaultPowerCap property not of type uint32_t");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res
                        .jsonValue["Oem"]["Nvidia"]["GPUBasePowerWatts"]
                                  ["DefaultSetPoint"] = *data;
                }
                else if (propertyName == "MinPowerCapValue")
                {
                    const uint32_t* data =
                        std::get_if<uint32_t>(&property.second);
                    if (data == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "MinPowerCapValue property not of type uint32_t");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res
                        .jsonValue["Oem"]["Nvidia"]["GPUBasePowerWatts"]
                                  ["AllowableMin"] = *data;
                }
                else if (propertyName == "MaxPowerCapValue")
                {
                    const uint32_t* data =
                        std::get_if<uint32_t>(&property.second);
                    if (data == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "MaxPowerCapValue property not of type uint32_t");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res
                        .jsonValue["Oem"]["Nvidia"]["GPUBasePowerWatts"]
                                  ["AllowableMax"] = *data;
                }
                else if (propertyName == "Persistency")
                {
                    const bool* data = std::get_if<bool>(&property.second);
                    if (data == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Persistency property not of type bool");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res
                        .jsonValue["Oem"]["Nvidia"]["GPUBasePowerWatts"]
                                  ["Persistency"] = *data;
                }
                else if (propertyName == "OneShotPowerLimit")
                {
                    const double* data = std::get_if<double>(&property.second);
                    if (data == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "OneShotPowerLimit property not of type double");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if (std::isnan(*data))
                    {
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["GPUBasePowerWatts"]
                                      ["RequestedOneshotSetPointWatts"] =
                            nullptr;
                    }
                    else
                    {
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["GPUBasePowerWatts"]
                                      ["RequestedOneshotSetPointWatts"] = *data;
                    }
                }
                else if (propertyName == "PersistentPowerLimit")
                {
                    const double* data = std::get_if<double>(&property.second);
                    if (data == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "PersistentPowerLimit property not of type double");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if (std::isnan(*data))
                    {
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["GPUBasePowerWatts"]
                                      ["RequestedPersistentSetPointWatts"] =
                            nullptr;
                    }
                    else
                    {
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["GPUBasePowerWatts"]
                                      ["RequestedPersistentSetPointWatts"] =
                            *data;
                    }
                }
            }
        });
}

inline void getProcessorBasePowerLimitByServicePath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path, [[maybe_unused]] const std::string& gpuId)
{
    dbus::utility::getDbusObject(
        path, std::array<std::string_view, 0>{},
        [asyncResp, path](const boost::system::error_code& errorno,
                          const dbus::utility::MapperGetObject& objInfo) {
            if (errorno)
            {
                BMCWEB_LOG_ERROR("ObjectMapper::GetObject call failed: {}",
                                 errorno);
                messages::internalError(asyncResp->res);
                return;
            }

            for (const auto& element : objInfo)
            {
                for (const auto& interface : element.second)
                {
                    if (interface == "xyz.openbmc_project.Control.Power.Cap" ||
                        interface == "com.nvidia.Common.ClearPowerCap" ||
                        interface ==
                            "xyz.openbmc_project.Control.Power.Persistency")
                    {
                        getBasePowerLimitValues(asyncResp, element.first, path,
                                                interface);
                    }
                }
            }
        });
}

inline void getProcessorBasePowerLimitPath(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& gpuId)
{
    dbus::utility::getAssociationEndPoints(
        objPath + "/Base_Power_Limit",
        [aResp, gpuId](const boost::system::error_code& e,
                       const dbus::utility::MapperEndPoints& endpoints) {
            if (e)
            {
                BMCWEB_LOG_DEBUG("Failed to get GPU copy CPU power limit: {}",
                                 e.message());
                return;
            }
            for (const auto& path : endpoints)
            {
                getProcessorBasePowerLimitByServicePath(aResp, path, gpuId);
                break;
            }
        });
}

inline void getProcessorEnvironmentMetricsData(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& processorId)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    dbus::utility::async_method_call(
        [processorId, aResp{std::move(aResp)}](
            const boost::system::error_code& ec,
            const boost::container::flat_map<
                std::string, boost::container::flat_map<
                                 std::string, std::vector<std::string>>>&
                subtree) {
            if (ec || subtree.empty())
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);

                return;
            }
            const std::string resourceType = "Processor";
            for (const auto& [path, object] : subtree)
            {
                if (!path.ends_with(processorId))
                {
                    continue;
                }
                for (const auto& [service, interfaces] : object)
                {
                    if (std::find(
                            interfaces.begin(), interfaces.end(),
                            "xyz.openbmc_project.Inventory.Decorator.PowerLimit") !=
                        interfaces.end())
                    {
                        getPowerLimits(aResp, service, path);
                        // Set the PowerLimit support flag as true to get Power
                        // sensor reading by
                        // getEnvironmentMetricsDataByService()
                        getEnvironmentMetricsDataByService(aResp, service, path,
                                                           resourceType, true);
                    }
                    if (std::find(interfaces.begin(), interfaces.end(),
                                  "xyz.openbmc_project.Control.Power.Cap") !=
                        interfaces.end())
                    {
                        getPowerCap(aResp, processorId, path);
                    }
                    if (std::find(interfaces.begin(), interfaces.end(),
                                  "xyz.openbmc_project.Control.Mode") !=
                        interfaces.end())
                    {
                        getControlMode(aResp, service, path);
                    }

                    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                    {
                        if (std::find(interfaces.begin(), interfaces.end(),
                                      "com.nvidia.Edpp") != interfaces.end())
                        {
                            getEDPpData(aResp, service, path);
                            aResp->res.jsonValue
                                ["Actions"]["Oem"]
                                ["#NvidiaEnvironmentMetrics.ResetEDPp"] = {
                                {"target",
                                 "/redfish/v1/Systems/" +
                                     std::string(
                                         BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                     "/Processors/" + processorId +
                                     "/EnvironmentMetrics/Actions/Oem/NvidiaEnvironmentMetrics.ResetEDPp"}};
                            aResp->res
                                .jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                                "#NvidiaEnvironmentMetrics.v1_2_0.NvidiaEnvironmentMetrics";
                        }

                        if (std::find(interfaces.begin(), interfaces.end(),
                                      "com.nvidia.Common.ClearPowerCap") !=
                            interfaces.end())
                        {
                            aResp->res.jsonValue
                                ["Actions"]["Oem"]
                                ["#NvidiaEnvironmentMetrics.ClearOOBSetPoint"] = {
                                {"target",
                                 "/redfish/v1/Systems/" +
                                     std::string(
                                         BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                     "/Processors/" + processorId +
                                     "/EnvironmentMetrics/Actions/Oem/NvidiaEnvironmentMetrics.ClearOOBSetPoint"}};
                        }

                        if (std::find(
                                interfaces.begin(), interfaces.end(),
                                "xyz.openbmc_project.Control.Power.Persistency") !=
                            interfaces.end())
                        {
                            getPowerLimitPersistency(aResp, service, path);
                            aResp->res
                                .jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                                "#NvidiaEnvironmentMetrics.v1_4_0.NvidiaEnvironmentMetrics";
                        }

                        if (std::find(interfaces.begin(), interfaces.end(),
                                      "com.nvidia.GPMMetrics") !=
                            interfaces.end())
                        {
                            getEnvironmentMetricsDataByService(
                                aResp, service, path, resourceType);
                            getProcessorBasePowerLimitPath(aResp, path,
                                                           processorId);
                            getGpuCopyCpuPowerLimitPath(aResp, path,
                                                        processorId);
                        }
                    }

                    if (std::find(
                            interfaces.begin(), interfaces.end(),
                            "xyz.openbmc_project.Inventory.Item.Accelerator") !=
                        interfaces.end())
                    {
                        getEnvironmentMetricsDataByService(aResp, service, path,
                                                           resourceType);
                    }
                    else if (std::find(
                                 interfaces.begin(), interfaces.end(),
                                 "xyz.openbmc_project.Inventory.Item.Cpu") !=
                             interfaces.end())
                    {
                        getCpuEnvironmentMetricsDataByService(aResp, service,
                                                              path);
                        getCpuPowerCapByService(aResp, service, path);
                    }
                }
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#Processor.v1_20_0.Processor", processorId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 3>{
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "xyz.openbmc_project.Inventory.Item.Cpu", "com.nvidia.GPMMetrics"});
}

inline void getMemoryEnvironmentMetricsData(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& dimmId)
{
    BMCWEB_LOG_DEBUG("Get available system memory resource");
    dbus::utility::async_method_call(
        [dimmId, aResp{std::move(aResp)}](
            const boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, boost::container::flat_map<
                                 std::string, std::vector<std::string>>>&
                subtree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);

                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!path.ends_with(dimmId))
                {
                    continue;
                }
                for (const auto& [service, interfaces] : object)
                {
                    getMemoryEnvironmentMetricsDataByService(aResp, service,
                                                             path);
                }
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#EnvironmentMetrics.v1_2_0.EnvironmentMetrics",
                dimmId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 1>{"xyz.openbmc_project.Inventory.Item.Dimm"});
}

inline void postEdppReset(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                          const std::string& processorId,
                          const std::string& cpuObjectPath,
                          const MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::find(interfaceList.begin(), interfaceList.end(),
                      "com.nvidia.Edpp") != interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        messages::internalError(resp->res);
        return;
    }

    static const auto* const resetEdppAsyncIntf =
        "com.nvidia.Common.ResetEdppAsync";

    dbus::utility::getDbusObject(
        cpuObjectPath, std::array<std::string_view, 1>{resetEdppAsyncIntf},
        [resp, cpuObjectPath, conName = *inventoryService,
         processorId](const boost::system::error_code& ec,
                      const dbus::utility::MapperGetObject& object) {
            if (!ec)
            {
                for (const auto& [serv, _] : object)
                {
                    if (serv != conName)
                    {
                        continue;
                    }

                    nvidia_async_operation_utils::
                        doGenericCallAsyncAndGatherResult<int>(
                            resp, std::chrono::seconds(60), conName,
                            cpuObjectPath, resetEdppAsyncIntf, "Reset",
                            [resp, processorId](
                                const std::string& status,
                                [[maybe_unused]] const int* retValue) {
                                if (status == nvidia_async_operation_utils::
                                                  asyncStatusValueSuccess)
                                {
                                    BMCWEB_LOG_DEBUG(
                                        "Edpp Reset for {} Succeded",
                                        processorId);
                                    messages::success(resp->res);
                                    return;
                                }
                                BMCWEB_LOG_ERROR("Edpp Reset for {} failed",
                                                 processorId, status);
                                messages::internalError(resp->res);
                            });

                    return;
                }
            }

            // Call Edpp Reset Method
            dbus::utility::async_method_call(
                [resp, processorId](boost::system::error_code& ec1,
                                    const int retValue) {
                    if (!ec1)
                    {
                        if (retValue != 0)
                        {
                            BMCWEB_LOG_ERROR("{}", retValue);
                            messages::operationFailed(resp->res);
                        }
                        BMCWEB_LOG_DEBUG("CPU:{} Edpp Reset Succeded",
                                         processorId);
                        messages::success(resp->res);
                        return;
                    }
                    BMCWEB_LOG_DEBUG("{}", ec1);
                    messages::internalError(resp->res);
                    return;
                },
                conName, cpuObjectPath, "com.nvidia.Edpp", "Reset");
        });
}

inline void getfanSpeedsPercent(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID)
{
    BMCWEB_LOG_DEBUG("Get properties for getFan associated to chassis = {}",
                     chassisID);
    const std::array<std::string, 1> sensorInterfaces = {
        "xyz.openbmc_project.Sensor.Value"};
    dbus::utility::async_method_call(
        [asyncResp, chassisID](
            const boost::system::error_code& ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                sensorsubtree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("D-Bus response error on GetSubTree {}", ec);
                if (ec.value() == boost::system::errc::io_error)
                {
                    messages::resourceNotFound(asyncResp->res, "Chassis",
                                               chassisID);
                    return;
                }
                messages::internalError(asyncResp->res);
                return;
            }
            nlohmann::json& fanList =
                asyncResp->res.jsonValue["FanSpeedsPercent"];
            fanList = nlohmann::json::array();

            for (const auto& [objectPath, serviceName] : sensorsubtree)
            {
                if (objectPath.empty() || serviceName.size() != 1)
                {
                    BMCWEB_LOG_DEBUG("Error getting D-Bus object!");
                    messages::internalError(asyncResp->res);
                    return;
                }
                const std::string& validPath = objectPath;
                const std::string& connectionName = serviceName[0].first;
                std::vector<std::string> split;
                // Reserve space for
                // /xyz/openbmc_project/sensors/<name>/<subname>
                split.reserve(6);
                bmcweb::split(split, validPath, '/');
                if (split.size() < 6)
                {
                    BMCWEB_LOG_ERROR("Got path that isn't long enough {}",
                                     validPath);
                    continue;
                }
                // These indexes aren't intuitive, as boost::split puts an empty
                // string at the beginning
                const std::string& sensorType = split[4];
                const std::string& sensorName = split[5];
                BMCWEB_LOG_DEBUG("sensorName {} sensorType {}", sensorName,
                                 sensorType);
                if (sensorType == "fan" || sensorType == "fan_tach" ||
                    sensorType == "fan_pwm")
                {
                    // if the sensor does not belong to the same chassis will
                    // discard it
                    dbus::utility::findAssociations(
                        validPath + "/chassis",
                        [asyncResp, chassisID, &fanList, sensorName, validPath,
                         connectionName](const boost::system::error_code& ec1,
                                         const std::vector<std::string>& data) {
                            if (ec1)
                            {
                                BMCWEB_LOG_ERROR("{} : {}", validPath,
                                                 ec1.message());
                                return;
                            }
                            if (data.empty())
                            {
                                BMCWEB_LOG_ERROR(
                                    "{} : No chassis association found",
                                    validPath);
                                return;
                            }
                            std::filesystem::path chassisPath(data.front());
                            std::string sensorChassisID =
                                chassisPath.filename();
                            if (sensorChassisID == chassisID)
                            {
                                dbus::utility::async_method_call(
                                    [asyncResp, chassisID, &fanList,
                                     sensorName](
                                        const boost::system::error_code& ec2,
                                        const std::variant<double>& value) {
                                        if (ec2)
                                        {
                                            BMCWEB_LOG_DEBUG(
                                                "Can't get Fan speed!");
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }

                                        const double* attributeValue =
                                            std::get_if<double>(&value);
                                        if (attributeValue == nullptr)
                                        {
                                            // illegal property
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }
                                        std::string tempPath =
                                            "/redfish/v1/Chassis/" + chassisID +
                                            "/Sensors/";
                                        fanList.push_back(
                                            {{"DeviceName",
                                              "Chassis Fan #" + sensorName},
                                             {"SpeedRPM", *attributeValue},
                                             {"DataSourceUri",
                                              tempPath + sensorName},
                                             {"@odata.id",
                                              tempPath + sensorName}});
                                    },
                                    connectionName, validPath,
                                    "org.freedesktop.DBus.Properties", "Get",
                                    "xyz.openbmc_project.Sensor.Value",
                                    "Value");
                            }
                        });
                }
                else
                {
                    BMCWEB_LOG_DEBUG(
                        "This is not a fan-related sensor,sensortype = {}",
                        sensorType);
                    continue;
                }
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/sensors", 0, sensorInterfaces);
}

inline void handleEnvironmentMetricsPatchBody(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    std::optional<nlohmann::json> powerLimit;
    std::optional<nlohmann::json> oem;

    // Read json request
    if (!json_util::readJsonAction(req, asyncResp->res, "PowerLimitWatts",
                                   powerLimit, "Oem", oem))
    {
        return;
    }

    // Update power limit
    if (powerLimit)
    {
        std::optional<int> setPoint;
        if (json_util::readJson(*powerLimit, asyncResp->res, "SetPoint",
                                setPoint))
        {
            const std::array<const char*, 1> interfacesList = {
                "xyz.openbmc_project.Inventory.Item.Chassis"};

            dbus::utility::async_method_call(
                [asyncResp, chassisId,
                 setPoint](const boost::system::error_code& ec,
                           const dbus::utility::GetSubTreeType& subtree) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    // Iterate over all retrieved ObjectPaths.
                    for (const std::pair<
                             std::string,
                             std::vector<std::pair<std::string,
                                                   std::vector<std::string>>>>&
                             object : subtree)
                    {
                        const std::string& path = object.first;
                        const std::vector<
                            std::pair<std::string, std::vector<std::string>>>&
                            connectionNames = object.second;

                        sdbusplus::message::object_path objPath(path);
                        if (objPath.filename() != chassisId)
                        {
                            continue;
                        }

                        if (connectionNames.empty())
                        {
                            BMCWEB_LOG_ERROR("Got 0 Connection names");
                            continue;
                        }

                        const std::string& connectionName =
                            connectionNames[0].first;
                        (void)connectionName;

                        dbus::utility::async_method_call(
                            [asyncResp, chassisId, setPoint](
                                const boost::system::error_code& ec1,
                                std::variant<std::vector<std::string>>& resp) {
                                if (ec1)
                                {
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                std::vector<std::string>* data =
                                    std::get_if<std::vector<std::string>>(
                                        &resp);
                                if (data == nullptr)
                                {
                                    return;
                                }
                                for (const std::string& ctrlPath : *data)
                                {
                                    std::string resourceType = "Chassis";
                                    redfish::nvidia_env_utils::patchPowerLimit(
                                        asyncResp, chassisId, *setPoint,
                                        ctrlPath, resourceType);
                                }
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            path + "/power_controls",
                            "org.freedesktop.DBus.Properties", "Get",
                            "xyz.openbmc_project.Association", "endpoints");

                        return;
                    }

                    messages::resourceNotFound(
                        asyncResp->res, "#Chassis.v1_15_0.Chassis", chassisId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/inventory", 0, interfacesList);
        }
    }

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        if (oem)
        {
            std::optional<nlohmann::json> nvidiaObj;
            if (!redfish::json_util::readJson(*oem, asyncResp->res, "Nvidia",
                                              nvidiaObj))
            {
                return;
            }
            if (nvidiaObj)
            {
                std::optional<std::string> powerMode;
                if (!redfish::json_util::readJson(*nvidiaObj, asyncResp->res,
                                                  "PowerMode", powerMode))
                {
                    return;
                }
                if (powerMode)
                {
                    messages::propertyNotWritable(asyncResp->res, "PowerMode");
                }
            }
        }
    }
}

inline void populateEnvironmentMetricsOemAndData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& validChassisPath)
{
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
            "#NvidiaEnvironmentMetrics.v1_2_0.NvidiaEnvironmentMetrics";
    }

    getfanSpeedsPercent(asyncResp, chassisId);
    getPowerWattsEnergyJoules(asyncResp, chassisId, validChassisPath);
    getTemperatureCelsius(asyncResp, chassisId, validChassisPath);

    const std::array<std::string_view, 2> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Board",
        "xyz.openbmc_project.Inventory.Item.Chassis"};
    getPowerAndControlData(asyncResp, chassisId, interfaces);
}

} // namespace nvidia_env_utils
} // namespace redfish
