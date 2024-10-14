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

#include "utils/nvidia_async_set_callbacks.hpp"
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
         service = *inventoryService](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetObject& object) {
        if (!ec)
        {
            for (const auto& [serv, _] : object)
            {
                if (serv != service)
                {
                    continue;
                }

                nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
                    resp, std::chrono::seconds(60), service, cpuObjectPath,
                    "com.nvidia.Edpp", "SetPoint",
                    std::variant<std::tuple<bool, uint32_t>>(std::make_tuple(
                        persistency, static_cast<uint32_t>(setPoint))),
                    nvidia_async_operation_utils::PatchEdppSetPointCallback{
                        resp});

                return;
            }
        }

        std::tuple<size_t, bool> reqSetPoint;
        reqSetPoint = std::make_tuple(setPoint, persistency);

        // Set the property, with handler to check error responses
        crow::connections::systemBus->async_method_call(
            [resp, processorId, setPoint](boost::system::error_code ec,
                                          sdbusplus::message::message& msg) {
            if (!ec)
            {
                BMCWEB_LOG_DEBUG("Set point property succeeded");
                return;
            }

            BMCWEB_LOG_ERROR("Processor ID: {} set point property failed: {}",
                             processorId, ec);
            // Read and convert dbus error message to redfish error
            const sd_bus_error* dbusError = msg.get_error();
            if (dbusError == nullptr)
            {
                BMCWEB_LOG_ERROR("Internal error for patch EDPp Setpoint");
                messages::internalError(resp->res);
                return;
            }
            if (strcmp(dbusError->name,
                       "xyz.openbmc_project.Common.Error.InvalidArgument") == 0)
            {
                // Invalid value
                BMCWEB_LOG_ERROR("Invalid value for EDPp Setpoint");
                messages::propertyValueIncorrect(resp->res, "setPoint",
                                                 std::to_string(setPoint));
            }
            else if (strcmp(dbusError->name,
                            "xyz.openbmc_project.Common.Error.Unavailable") ==
                     0)
            {
                std::string errBusy = "0x50A";
                std::string errBusyResolution =
                    "SMBPBI Command failed with error busy, please try after 60 seconds";
                // busy error
                BMCWEB_LOG_ERROR(
                    "Command failed with error busy, for patch EDPp Setpoint");
                messages::asyncError(resp->res, errBusy, errBusyResolution);
            }
            else if (strcmp(dbusError->name,
                            "xyz.openbmc_project.Common.Error.Timeout") == 0)
            {
                std::string errTimeout = "0x600";
                std::string errTimeoutResolution =
                    "Settings may/maynot have applied, please check get response before patching";
                // timeout error
                BMCWEB_LOG_ERROR("Timeout error for patch EDPp Setpoint");
                messages::asyncError(resp->res, errTimeout,
                                     errTimeoutResolution);
            }
            else if (strcmp(dbusError->name, "xyz.openbmc_project.Common."
                                             "Device.Error.WriteFailure") == 0)
            {
                // Service failed to change the config
                BMCWEB_LOG_ERROR(
                    "Write Operation failed for patch EDPp Setpoint");
                messages::operationFailed(resp->res);
            }
            else
            {
                BMCWEB_LOG_ERROR("Unknown error for patch EDPp Setpoint");
                messages::internalError(resp->res);
            }
        },
            service, cpuObjectPath, "org.freedesktop.DBus.Properties", "Set",
            "com.nvidia.Edpp", "SetPoint",
            std::variant<std::tuple<size_t, bool>>(reqSetPoint));
    });
}

inline void getPowerMode(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& connectionName,
                         const std::string& objPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, connectionName, objPath](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string, std::variant<std::string>>>& propertiesList) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error for "
                             "Chassis properties");
            return;
        }
        for (const std::pair<std::string, std::variant<std::string>>& property :
             propertiesList)
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

inline void getPowerWattsBySensorName(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& sensorName)
{
    const std::string& totalPowerPath = "/xyz/openbmc_project/sensors/power/" +
                                        sensorName;
    // Add total power sensor to associated chassis only
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID, sensorName,
         totalPowerPath](const boost::system::error_code ec,
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
            crow::connections::systemBus->async_method_call(
                [asyncResp, chassisID, sensorName, totalPowerPath](
                    const boost::system::error_code ec,
                    const std::vector<std::pair<
                        std::string, std::vector<std::string>>>& object) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG("DBUS response error");
                    messages::internalError(asyncResp->res);
                    return;
                }
                for (const auto& tempObject : object)
                {
                    const std::string& connectionName = tempObject.first;
                    crow::connections::systemBus->async_method_call(
                        [asyncResp, sensorName,
                         chassisID](const boost::system::error_code ec,
                                    const std::variant<double>& value) {
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG("Can't get Power Watts!");
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
                        std::string tempPath = "/redfish/v1/Chassis/" +
                                               chassisID + "/Sensors/";
                        asyncResp->res.jsonValue["PowerWatts"] = {
                            {"Reading", *attributeValue},
                            {"DataSourceUri", tempPath + sensorName}};
                        // look for the correct moduel power sensor by the
                        // pattern
                        // ProcessorModule_{instance_id}_Power{instance_id}
                        std::size_t found = chassisID.find_last_of('_');
                        std::string name = "ProcessorModule_";
                        if (found != std::string::npos)
                        {
                            std::string index = chassisID.substr(found + 1, 1);
                            name += index + "_Power";
                        }
                        /// Reading is the same in PowerWatt and PowerLimitWatts
                        /// objects for module.
                        if (sensorName.find(name) != std::string::npos)
                        {
                            asyncResp->res
                                .jsonValue["PowerLimitWatts"]["Reading"] =
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
                "xyz.openbmc_project.ObjectMapper", "GetObject", totalPowerPath,
                totalPowerInterfaces);
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
    const std::string& sensorPath = "/xyz/openbmc_project/sensors/energy/" +
                                    sensorName;
    // Add total power sensor to associated chassis only
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID, sensorName,
         sensorPath](const boost::system::error_code ec,
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
            crow::connections::systemBus->async_method_call(
                [asyncResp, chassisID, sensorName, sensorPath](
                    const boost::system::error_code ec,
                    const std::vector<std::pair<
                        std::string, std::vector<std::string>>>& object) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG("DBUS response error");
                    messages::internalError(asyncResp->res);
                    return;
                }
                for (const auto& tempObject : object)
                {
                    const std::string& connectionName = tempObject.first;
                    crow::connections::systemBus->async_method_call(
                        [asyncResp, sensorName,
                         chassisID](const boost::system::error_code ec,
                                    const std::variant<double>& value) {
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG("Can't get Energy Joules!");
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
                        std::string tempPath = "/redfish/v1/Chassis/" +
                                               chassisID + "/Sensors/";
                        asyncResp->res.jsonValue["EnergykWh"] = {
                            {"Reading", joulesToKwh(*attributeValue)},
                        };
                        asyncResp->res.jsonValue["EnergyJoules"] = {
                            {"Reading", *attributeValue},
                            {"DataSourceUri", tempPath + sensorName}};
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
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID,
         chassisPath](const boost::system::error_code ec,
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

inline void
    getPowerReadings(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& connectionName,
                     const std::string& objPath, const std::string& chassisID)
{
    // Add get sensor name  from power control
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID,
         connectionName](const boost::system::error_code ec,
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
            sdbusplus::message::object_path objPath(sensorPath);
            const std::string& sensorName = objPath.filename();

            // Process sensor reading
            crow::connections::systemBus->async_method_call(
                [asyncResp, sensorName,
                 chassisID](const boost::system::error_code ec,
                            const std::variant<double>& value) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG("Can't get Power Watts!");
                    return;
                }
                const double* attributeValue = std::get_if<double>(&value);
                if (attributeValue == nullptr)
                {
                    return;
                }
                std::string tempPath = "/redfish/v1/Chassis/" + chassisID +
                                       "/Sensors/";
                asyncResp->res.jsonValue["PowerLimitWatts"]["Reading"] =
                    *attributeValue;
            },
                connectionName, sensorPath, "org.freedesktop.DBus.Properties",
                "Get", "xyz.openbmc_project.Sensor.Value", "Value");
        }
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/sensor",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void
    getDefaultPowerCap(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& objPath)
{
    const std::array<const char*, 1> clearPowerCapInterfaces = {
        "com.nvidia.Common.ClearPowerCap"};
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath](
            const boost::system::error_code errorno,
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
            crow::connections::systemBus->async_method_call(
                [asyncResp, objPath](
                    const boost::system::error_code ec,
                    const std::vector<
                        std::pair<std::string, std::variant<uint32_t, bool>>>&
                        propertiesList) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("DBUS response error for "
                                     "Chassis properties");
                    messages::internalError(asyncResp->res);
                    return;
                }
                for (const std::pair<std::string, std::variant<uint32_t, bool>>&
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
                                                ["DefaultSetPoint"] = *value;
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
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID, objPath](
            const boost::system::error_code errorno,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
        if (errorno)
        {
            BMCWEB_LOG_ERROR("ObjectMapper::GetObject call failed: {}",
                             errorno);
            messages::internalError(asyncResp->res);
            return;
        }

        sdbusplus::message::object_path path(objPath);
        const std::string name = path.filename();
        asyncResp->res.jsonValue["PowerLimitWatts"]["DataSourceUri"] =
            "/redfish/v1/Chassis/" + chassisID + "/Controls/" + name;

        for (const auto& element : objInfo)
        {
            crow::connections::systemBus->async_method_call(
                [asyncResp, objPath](
                    const boost::system::error_code ec,
                    const std::vector<
                        std::pair<std::string, std::variant<uint32_t, bool>>>&
                        propertiesList) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG("DBUS response error for "
                                     "Chassis properties");
                    messages::internalError(asyncResp->res);
                    return;
                }
                for (const std::pair<std::string, std::variant<uint32_t, bool>>&
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
                            .jsonValue["PowerLimitWatts"]["SetPoint"] = *value;
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
                                                ["AllowableMin"] = *value;
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
                                                ["AllowableMax"] = *value;
                    }
                    else if (propertyName == "PowerCapEnable")
                    {
                        const bool* value = std::get_if<bool>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_DEBUG("Null value returned "
                                             "for type");
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        if (*value)
                        {
                            asyncResp->res
                                .jsonValue["PowerLimitWatts"]["ControlMode"] =
                                "Automatic";
                        }
                        else
                        {
                            asyncResp->res
                                .jsonValue["PowerLimitWatts"]["ControlMode"] =
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
                                                ["DefaultSetPoint"] = *value;
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
    crow::connections::systemBus->async_method_call(
        [asyncResp, connectionName,
         objPath](const boost::system::error_code ec,
                  const SetPointProperties& properties) {
        if (ec)
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
                const auto* setPoint = std::get_if<SetPointProperty>(&variant);
                if (setPoint != nullptr)
                {
                    const auto& [limit, persistency] = *setPoint;
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["EDPpPercent"]
                                            ["SetPoint"] = limit;
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["EDPpPercent"]
                                            ["Persistency"] = {};
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
    crow::connections::systemBus->async_method_call(
        [asyncResp, connectionName,
         objPath](const boost::system::error_code ec,
                  const SetPointProperties& properties) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error for "
                             "procesor EDPp scaling properties");
            messages::internalError(asyncResp->res);
            return;
        }
        for (const auto& [key, variant] : properties)
        {
            if (key == "Persistency")
            {
                asyncResp->res
                    .jsonValue["Oem"]["Nvidia"]["PowerLimitPersistency"] = {};
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
    crow::connections::systemBus->async_method_call(
        [asyncResp, connectionName, objPath](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::variant<uint32_t>>>&
                propertiesList) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error for "
                             "Chassis properties");
            messages::internalError(asyncResp->res);
            return;
        }
        for (const std::pair<std::string, std::variant<uint32_t>>& property :
             propertiesList)
        {
            const std::string& propertyName = property.first;
            if (propertyName == "MaxPowerWatts")
            {
                const uint32_t* value = std::get_if<uint32_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_DEBUG("Null value returned "
                                     "for type");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["PowerLimitWatts"]["AllowableMax"] =
                    *value;
            }
            if (propertyName == "MinPowerWatts")
            {
                const uint32_t* value = std::get_if<uint32_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_DEBUG("Null value returned "
                                     "for type");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["PowerLimitWatts"]["AllowableMin"] =
                    *value;
            }
        }
    },
        connectionName, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Inventory.Decorator.PowerLimit");
}

inline void getControlMode(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& objPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, connectionName,
         objPath](const boost::system::error_code ec,
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
                if (*value == true)
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
inline void
    getPowerAndControlData(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& resourceId,
                           const std::array<const char*, SIZE>& interfaces)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp,
         resourceId](const boost::system::error_code ec,
                     const crow::openbmc_mapper::GetSubTreeType& subtree) {
        if (ec)
        {
            return;
        }

        // Iterate over all retrieved ObjectPaths.
        for (const std::pair<
                 std::string,
                 std::vector<std::pair<std::string, std::vector<std::string>>>>&
                 object : subtree)
        {
            const std::string& path = object.first;
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                connectionNames = object.second;

            sdbusplus::message::object_path objPath(path);
            if (objPath.filename() != resourceId)
            {
                continue;
            }

            if (connectionNames.size() < 1)
            {
                BMCWEB_LOG_ERROR("Got 0 Connection names");
                continue;
            }

            const std::string& connectionName = connectionNames[0].first;
            const std::vector<std::string>& interfaces =
                connectionNames[0].second;

            if (std::find(interfaces.begin(), interfaces.end(),
                          "xyz.openbmc_project.Inventory.Item.Cpu") !=
                interfaces.end())
            {
                // Skip PowerAndControlData for
                // /Chassis/CPU_{ID}/EnvironmentMetrics URI The CPU power cap is
                // handled by /Systems/{ID}/Processor/CPU_{ID}/Controls URI
                continue;
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp, connectionName, interfaces,
                 resourceId](const boost::system::error_code& e,
                             std::variant<std::vector<std::string>>& resp) {
                if (e)
                {
                    return;
                }
                std::vector<std::string>* data =
                    std::get_if<std::vector<std::string>>(&resp);
                if (data == nullptr)
                {
                    return;
                }
                for (const std::string& ctrlPath : *data)
                {
                    getPowerCap(asyncResp, resourceId, ctrlPath);
                    // Skip getControlMode if it does not support the Control
                    // Mode
                    if (std::find(interfaces.begin(), interfaces.end(),
                                  "xyz.openbmc_project.Control.Mode") !=
                        interfaces.end())
                    {
                        getControlMode(asyncResp, connectionName, ctrlPath);
                    }
                    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                    {
                        getPowerMode(asyncResp, connectionName, ctrlPath);
                        asyncResp->res.jsonValue
                            ["Actions"]["Oem"]
                            ["#NvidiaEnvironmentMetrics.ClearOOBSetPoint"] = {
                            {"target",
                             "/redfish/v1/Chassis/" + resourceId +
                                 "/EnvironmentMetrics/Actions/Oem/NvidiaEnvironmentMetrics.ClearOOBSetPoint"}};
                    }
                    getPowerReadings(asyncResp, connectionName, ctrlPath,
                                     resourceId);
                }
            },
                "xyz.openbmc_project.ObjectMapper", path + "/power_controls",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Association", "endpoints");
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0, interfaces);
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
    crow::connections::systemBus->async_method_call(
        [resp, resourceId, persistency, powerLimit, resourceType, objectPath](
            const boost::system::error_code errorno,
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
                [resp, objectPath, powerLimit, element, persistency, resourceId,
                 resourceType](const boost::system::error_code& ec,
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
                            persistency, static_cast<uint32_t>(powerLimit));

                        if (resourceType == "Processors")
                        {
                            nvidia_async_operation_utils::
                                doGenericSetAsyncAndGatherResult(
                                    resp, std::chrono::seconds(60),
                                    element.first, objectPath,
                                    "xyz.openbmc_project.Control.Power.Cap",
                                    "PowerCap",
                                    std::variant<std::tuple<bool, uint32_t>>(
                                        reqPowerLimit),
                                    nvidia_async_operation_utils::
                                        PatchPowerCapCallback{resp,
                                                              powerLimit});
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
                                        static_cast<uint32_t>(powerLimit)),
                                    nvidia_async_operation_utils::
                                        PatchPowerCapCallback{resp,
                                                              powerLimit});
                        }

                        return;
                    }
                }

                BMCWEB_LOG_DEBUG("Performing Patch using set-property Call");

                // Set the property, with handler to check error responses
                crow::connections::systemBus->async_method_call(
                    [resp, resourceId, powerLimit,
                     resourceType](boost::system::error_code ec,
                                   sdbusplus::message::message& msg) {
                    if (!ec)
                    {
                        BMCWEB_LOG_DEBUG("Set power limit property succeeded");
                        messages::success(resp->res);
                        return;
                    }

                    BMCWEB_LOG_ERROR(
                        "{}: {} set power limit property failed: {}",
                        resourceType, resourceId, ec);
                    // Read and convert dbus error message to redfish error
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
                    else if (strcmp(
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
                                    "Device.Error.WriteFailure") == 0)
                    {
                        // Service failed to change the config
                        messages::operationFailed(resp->res);
                    }
                    else
                    {
                        messages::internalError(resp->res);
                    }
                },
                    element.first, objectPath,
                    "org.freedesktop.DBus.Properties", "Set",
                    "xyz.openbmc_project.Control.Power.Cap", "PowerCap",
                    std::variant<uint32_t>(static_cast<uint32_t>(powerLimit)));
            });
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objectPath,
        powerCapInterfaces);
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
    crow::connections::systemBus->async_method_call(
        [aResp, chassisId, resourceType, objPath,
         isSupportPowerLimit](const boost::system::error_code ec,
                              const PropertiesMap& properties) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("Can't get sensor reading for {}", objPath);
            // Not reporting Internal Failure for services that dont host
            // sensor path in case of Processor Env Eg: GpuOobRecovery in
            // case of FPGA Processor
            if (resourceType != "Processor")
            {
                // Not reporting Internal Failure because we might have another
                // service with the same objpath to set up config only. Eg:
                // PartLoaction
                BMCWEB_LOG_WARNING(
                    "Can't get Processor sensor DBus properties {}", objPath);
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
                boost::algorithm::split(split, objPath, boost::is_any_of("/"));
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
                    if (isSupportPowerLimit == true)
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
    const std::string& objPath, const std::string& resourceType)
{
    BMCWEB_LOG_DEBUG("Get sensor service.");

    const std::array<const char*, 1> sensorInterfaces = {
        "xyz.openbmc_project.Sensor.Value"};
    // Process sensor reading
    crow::connections::systemBus->async_method_call(
        [aResp, chassisId, resourceType, objPath](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                object) {
        if (ec)
        {
            // The path does not implement any state interfaces.
            return;
        }

        for (const auto& [service, interfaces] : object)
        {
            if (std::find(interfaces.begin(), interfaces.end(),
                          "xyz.openbmc_project.Sensor.Value") !=
                interfaces.end())
            {
                getSensorDataByService(aResp, service, chassisId, objPath,
                                       resourceType);
            }
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
        sensorInterfaces);
}

inline void getEnvironmentMetricsDataByService(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath, const std::string& resourceType,
    bool isSupportPowerLimit = false)
{
    BMCWEB_LOG_DEBUG("Get environment metrics data.");
    // Get parent chassis for sensors URI
    crow::connections::systemBus->async_method_call(
        [aResp, service, resourceType, objPath,
         isSupportPowerLimit](const boost::system::error_code ec,
                              std::variant<std::vector<std::string>>& resp) {
        if (ec)
        {
            return; // no chassis = no failures
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr || data->size() > 1)
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
        crow::connections::systemBus->async_method_call(
            [aResp, service, resourceType, chassisId, isSupportPowerLimit](
                const boost::system::error_code& e,
                std::variant<std::vector<std::string>>& resp) {
            if (e)
            {
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
                getSensorDataByService(aResp, service, chassisId, sensorPath,
                                       resourceType, isSupportPowerLimit);
            }
        },
            "xyz.openbmc_project.ObjectMapper", chassisPath + "/all_sensors",
            "org.freedesktop.DBus.Properties", "Get",
            "xyz.openbmc_project.Association", "endpoints");
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getCpuEnvironmentMetricsDataByService(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get CPU environment metrics data.");
    crow::connections::systemBus->async_method_call(
        [aResp, service,
         objPath](const boost::system::error_code& e,
                  std::variant<std::vector<std::string>>& resp) {
        if (e)
        {
            messages::internalError(aResp->res);
            return;
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            return;
        }

        sdbusplus::message::object_path objectPath(objPath);
        std::string chassisName = objectPath.filename();
        if (chassisName.empty())
        {
            messages::internalError(aResp->res);
            return;
        }
        const std::string& chassisId = chassisName;
        const std::string resourceType = "Processor";
        for (const std::string& sensorPath : *data)
        {
            getSensorDataService(aResp, service, chassisId, sensorPath,
                                 resourceType);
        }
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_sensors",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getCpuPowerCapData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const std::string& service,
                               const std::string& objPath,
                               const std::string& cpuId, bool persistence)
{
    BMCWEB_LOG_DEBUG("Get CPU power cap data.");
    crow::connections::systemBus->async_method_call(
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
        aResp->res.jsonValue["PowerLimitWatts"]["DataSourceUri"] = sensorURI;

        getPowerCap(aResp, cpuId, objPath);
    },
        service, objPath, "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.State.Decorator.Persistence", "persistent");
}

inline void
    getCpuPowerCapService(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                          const std::string& service,
                          const std::string& objPath, const std::string& cpuId)
{
    BMCWEB_LOG_DEBUG("Get CPU power cap service.");

    const std::array<const char*, 1> sensorInterfaces = {
        "xyz.openbmc_project.Control.Power.Cap"};
    // Process sensor reading
    crow::connections::systemBus->async_method_call(
        [aResp, service, objPath, cpuId](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                object) {
        if (ec)
        {
            // The path does not implement any state interfaces.
            return;
        }

        for (const auto& [service, interfaces] : object)
        {
            if (std::find(interfaces.begin(), interfaces.end(),
                          "xyz.openbmc_project.Control.Power.Cap") !=
                interfaces.end())
            {
                getCpuPowerCapData(aResp, service, objPath, cpuId, true);
            }
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
        sensorInterfaces);
}

inline void
    getCpuPowerCapByService(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            const std::string& service,
                            const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get CPU power Cap");
    crow::connections::systemBus->async_method_call(
        [aResp, service,
         objPath](const boost::system::error_code& e,
                  std::variant<std::vector<std::string>>& resp) {
        if (e)
        {
            // The path does not implement any power cap interfaces.
            return;
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            messages::internalError(aResp->res);
            return;
        }

        sdbusplus::message::object_path objectPath(objPath);
        std::string cpuName = objectPath.filename();
        if (cpuName.empty())
        {
            messages::internalError(aResp->res);
            return;
        }
        const std::string& cpuId = cpuName;
        for (const std::string& sensorPath : *data)
        {
            getCpuPowerCapService(aResp, service, sensorPath, cpuId);
        }
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/power_controls",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void
    getProcessorEnvironmentMetricsData(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                       const std::string& processorId)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    crow::connections::systemBus->async_method_call(
        [processorId, aResp{std::move(aResp)}](
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
                    // sensor reading by getEnvironmentMetricsDataByService()
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
                                 std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                 "/Processors/" + processorId +
                                 "/EnvironmentMetrics/Actions/Oem/NvidiaEnvironmentMetrics.ResetEDPp"}};
                        aResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
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
                                 std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                 "/Processors/" + processorId +
                                 "/EnvironmentMetrics/Actions/Oem/NvidiaEnvironmentMetrics.ClearOOBSetPoint"}};
                    }

                    if (std::find(
                            interfaces.begin(), interfaces.end(),
                            "xyz.openbmc_project.Control.Power.Persistency") !=
                        interfaces.end())
                    {
                        getPowerLimitPersistency(aResp, service, path);
                        aResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                            "#NvidiaEnvironmentMetrics.v1_2_0.NvidiaEnvironmentMetrics";
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
                else if (std::find(interfaces.begin(), interfaces.end(),
                                   "xyz.openbmc_project.Inventory.Item.Cpu") !=
                         interfaces.end())
                {
                    getCpuEnvironmentMetricsDataByService(aResp, service, path);
                    getCpuPowerCapByService(aResp, service, path);
                }
            }
            return;
        }
        // Object not found
        messages::resourceNotFound(aResp->res, "#Processor.v1_20_0.Processor",
                                   processorId);
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 2>{
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "xyz.openbmc_project.Inventory.Item.Cpu"});
}

inline void
    getMemoryEnvironmentMetricsData(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                    const std::string& dimmId)
{
    BMCWEB_LOG_DEBUG("Get available system memory resource");
    crow::connections::systemBus->async_method_call(
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
        const std::string resourceType = "Memory";
        for (const auto& [path, object] : subtree)
        {
            if (!path.ends_with(dimmId))
            {
                continue;
            }
            for (const auto& [service, interfaces] : object)
            {
                getEnvironmentMetricsDataByService(aResp, service, path,
                                                   resourceType);
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

    static const auto resetEdppAsyncIntf = "com.nvidia.Common.ResetEdppAsync";

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

                nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<
                    int>(
                    resp, std::chrono::seconds(60), conName, cpuObjectPath,
                    resetEdppAsyncIntf, "Reset",
                    [resp, processorId](const std::string& status,
                                        [[maybe_unused]] const int* retValue) {
                    if (status ==
                        nvidia_async_operation_utils::asyncStatusValueSuccess)
                    {
                        BMCWEB_LOG_DEBUG("Edpp Reset for {} Succeded",
                                         processorId);
                        messages::success(resp->res);
                        return;
                    }
                    BMCWEB_LOG_ERROR("Edpp Reset for {} failed", processorId,
                                     status);
                    messages::internalError(resp->res);
                });

                return;
            }
        }

        // Call Edpp Reset Method
        crow::connections::systemBus->async_method_call(
            [resp, processorId](boost::system::error_code ec,
                                const int retValue) {
            if (!ec)
            {
                if (retValue != 0)
                {
                    BMCWEB_LOG_ERROR("{}", retValue);
                    messages::operationFailed(resp->res);
                }
                BMCWEB_LOG_DEBUG("CPU:{} Edpp Reset Succeded", processorId);
                messages::success(resp->res);
                return;
            }
            BMCWEB_LOG_DEBUG("{}", ec);
            messages::internalError(resp->res);
            return;
        },
            conName, cpuObjectPath, "com.nvidia.Edpp", "Reset");
    });
}

} // namespace nvidia_env_utils
} // namespace redfish
