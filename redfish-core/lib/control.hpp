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

#include "error_messages.hpp"
#include "generated/enums/resource.hpp"
#include "query.hpp"
#include "utils/nvidia_async_set_callbacks.hpp"

#include <app.hpp>
#include <dbus_utility.hpp>
#include <registries/privilege_registry.hpp>
#include <utils/chassis_utils.hpp>
#include <utils/json_utils.hpp>
#include <utils/nvidia_async_set_utils.hpp>
#include <utils/nvidia_chassis_util.hpp>
#include <utils/nvidia_control_utils.hpp>

namespace redfish
{

constexpr std::string_view setPointPropName()
{
    if constexpr (BMCWEB_POWER_CONTROL_TYPE_PERCENTAGE)
    {
        return "PowerCapPercentage";
    }
    else
    {
        return "PowerCap";
    }
}

constexpr std::string_view setPointUnits()
{
    if constexpr (BMCWEB_POWER_CONTROL_TYPE_PERCENTAGE)
    {
        return "%";
    }
    else
    {
        return "W";
    }
}
static const std::map<std::string, std::string> modes = {
    {"xyz.openbmc_project.Control.Power.Mode.PowerMode.MaximumPerformance",
     "Automatic"},
    {"xyz.openbmc_project.Control.Power.Mode.PowerMode.OEM", "Override"},
    {"xyz.openbmc_project.Control.Power.Mode.PowerMode.PowerSaving", "Manual"},
    {"xyz.openbmc_project.Control.Power.Mode.PowerMode.Static", "Disabled"}};

const std::array<std::string_view, 3> powerinterfaces = {
    "xyz.openbmc_project.Control.Power.Cap", "com.nvidia.Common.ClearPowerCap",
    "xyz.openbmc_project.Control.Power.Mode"};
inline void getPowercontrolObjects(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& chassisPath)
{
    nlohmann::json& members = asyncResp->res.jsonValue["Members"];
    members = nlohmann::json::array();
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", chassisPath + "/power_controls",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, chassisID, &members](const boost::system::error_code&,
                                         const std::vector<std::string>& resp) {
            for (const auto& object : resp)
            {
                sdbusplus::message::object_path objPath(object);
                members.push_back(
                    {{"@odata.id", "/redfish/v1/Chassis/" + chassisID +
                                       "/Controls/" + objPath.filename()}});
            }
            asyncResp->res.jsonValue["Members@odata.count"] = members.size();
        });
}

inline void getChassisPower(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& path,
                            const std::string& /*chassisPath*/)
{
    dbus::utility::getDbusObject(
        path, powerinterfaces,
        [asyncResp, path](
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
                for (const auto& interface : element.second)
                {
                    if ((interface ==
                         "xyz.openbmc_project.Control.Power.Cap") ||
                        (interface ==
                         "xyz.openbmc_project.Control.Power.Mode") ||
                        (interface == "com.nvidia.Common.ClearPowerCap") ||
                        (interface ==
                         "xyz.openbmc_project.Inventory.Decorator.Area"))
                    {
                        dbus::utility::getAllProperties(
                            element.first, path, interface,
                            [asyncResp, path,
                             interface](const boost::system::error_code& ec1,
                                        const dbus::utility::DBusPropertiesMap&
                                            propertiesList) {
                                if (ec1)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "ObjectMapper::GetObject call failed:{}",
                                        ec1);
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                for (const auto& property : propertiesList)
                                {
                                    std::string propertyName = property.first;
                                    if (propertyName == "MaxPowerCapValue")
                                    {
                                        propertyName = "AllowableMax";
                                        const auto* value = std::get_if<size_t>(
                                            &property.second);
                                        if (value == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Internal errror for AllowableMax");
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }
                                        asyncResp->res.jsonValue[propertyName] =
                                            *value;
                                        continue;
                                    }
                                    if (propertyName == "MinPowerCapValue")
                                    {
                                        propertyName = "AllowableMin";
                                        const auto* value = std::get_if<size_t>(
                                            &property.second);
                                        if (value == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Internal errror for AllowableMin");
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }
                                        asyncResp->res.jsonValue[propertyName] =
                                            *value;
                                        continue;
                                    }
                                    if (propertyName == setPointPropName())
                                    {
                                        propertyName = "SetPoint";
                                        const auto* value = std::get_if<size_t>(
                                            &property.second);
                                        if (value == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Internal errror for SetPoint");
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }
                                        asyncResp->res.jsonValue[propertyName] =
                                            *value;
                                        continue;
                                    }
                                    if (propertyName == "DefaultPowerCap")
                                    {
                                        propertyName = "DefaultSetPoint";
                                        const auto* value = std::get_if<size_t>(
                                            &property.second);
                                        if (value == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Internal errror for DefaultSetPoint");
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }
                                        asyncResp->res.jsonValue[propertyName] =
                                            *value;
                                        continue;
                                    }
                                    if (propertyName == "PhysicalContext")
                                    {
                                        const auto* physicalcontext =
                                            std::get_if<std::string>(
                                                &property.second);
                                        asyncResp->res.jsonValue[propertyName] =
                                            redfish::dbus_utils::
                                                toPhysicalContext(
                                                    *physicalcontext);
                                        continue;
                                    }
                                    if (propertyName == "PowerMode")
                                    {
                                        propertyName = "ControlMode";
                                        const std::string* mode =
                                            std::get_if<std::string>(
                                                &property.second);
                                        for (const auto& itr : modes)
                                        {
                                            if (*mode == itr.first)
                                            {
                                                asyncResp->res
                                                    .jsonValue[propertyName] =
                                                    itr.second;
                                                break;
                                            }
                                        }

                                        continue;
                                    }
                                }
                            });
                    }
                }
            }
        });
}

inline void getTotalPower(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& chassisID)
{
    const std::string sensorName(BMCWEB_PLATFORM_POWER_CONTROL_SENSOR_NAME);

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/sensors", 0,
        std::array<std::string_view, 1>{"xyz.openbmc_project.Sensor.Value"},
        [asyncResp, sensorName, chassisID](
            const boost::system::error_code& ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            if (ec)
            {
                // do not add err msg in redfish response, because this is not
                //     mandatory property
                BMCWEB_LOG_DEBUG("DBUS error: no matched iface {}", ec);
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
                if (objPath.filename() != sensorName)
                {
                    continue;
                }

                if (connectionNames.empty())
                {
                    BMCWEB_LOG_ERROR("Got 0 Connection names");
                    continue;
                }
                const std::string& serviceName = connectionNames[0].first;

                // Read Sensor value
                dbus::utility::getProperty<double>(
                    serviceName, path, "xyz.openbmc_project.Sensor.Value",
                    "Value",
                    [asyncResp, chassisID, sensorName, serviceName,
                     path](const boost::system::error_code& ec1,
                           const double& totalPower) {
                        if (ec1)
                        {
                            BMCWEB_LOG_ERROR("Get Sensor value failed: {}",
                                             ec1);
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        asyncResp->res.jsonValue["Sensor"]["Reading"] =
                            totalPower;
                        asyncResp->res.jsonValue["Sensor"]["DataSourceUri"] =
                            ("/redfish/v1/Chassis/" + chassisID + "/Sensors/")
                                .append(sensorName);
                    });

                // Read related items
                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper",
                    path + "/all_processors", "xyz.openbmc_project.Association",
                    "endpoints",
                    [asyncResp,
                     chassisID](const boost::system::error_code& ec2,
                                const std::vector<std::string>& resp) {
                        if (ec2)
                        {
                            BMCWEB_LOG_DEBUG("Get Related Items failed: {}",
                                             ec2);
                            return; // no gpus = no failures
                        }
                        nlohmann::json& relatedItemsArray =
                            asyncResp->res.jsonValue["RelatedItem"];
                        relatedItemsArray = nlohmann::json::array();
                        for (const std::string& gpuPath : resp)
                        {
                            sdbusplus::message::object_path objectPath(gpuPath);
                            std::string gpuName = objectPath.filename();
                            if (gpuName.empty())
                            {
                                return;
                            }
                            relatedItemsArray.push_back(
                                {{"@odata.id",
                                  "/redfish/v1/Systems/" +
                                      std::string(
                                          BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                      "/Processors/" + gpuName}});
                        }
                    });
            }
        });
}

inline void getControlSettings(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path)
{
    dbus::utility::getDbusObject(
        path, powerinterfaces,
        [asyncResp, path](
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
                dbus::utility::getAllProperties(
                    element.first, path,
                    "xyz.openbmc_project.Control.Power.Cap",
                    [asyncResp, path](const boost::system::error_code& ec1,
                                      const dbus::utility::DBusPropertiesMap&
                                          propertiesList) {
                        if (ec1)
                        {
                            BMCWEB_LOG_ERROR(
                                "ObjectMapper::GetObject call failed:{}", ec1);
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        for (const auto& [propertyName, value] : propertiesList)
                        {
                            if (propertyName == "MaxPowerCapValue" &&
                                std::holds_alternative<uint32_t>(value))
                            {
                                asyncResp->res.jsonValue["AllowableMax"] =
                                    std::get<uint32_t>(value);
                            }
                            else if (propertyName == "MinPowerCapValue" &&
                                     std::holds_alternative<uint32_t>(value))
                            {
                                asyncResp->res.jsonValue["AllowableMin"] =
                                    std::get<uint32_t>(value);
                            }
                            else if (propertyName == "PowerCap" &&
                                     std::holds_alternative<uint32_t>(value))
                            {
                                asyncResp->res.jsonValue["SetPoint"] =
                                    std::get<uint32_t>(value);
                            }
                            else if (propertyName == "PowerCapEnable" &&
                                     std::holds_alternative<bool>(value))
                            {
                                if (std::get<bool>(value))
                                {
                                    asyncResp->res.jsonValue["ControlMode"] =
                                        "Automatic";
                                }
                                else
                                {
                                    asyncResp->res.jsonValue["ControlMode"] =
                                        "Disabled";
                                }
                                asyncResp->res.jsonValue["Status"]["Health"] =
                                    resource::Health::OK;
                            }
                        }
                    });

                dbus::utility::getAllProperties(
                    element.first, path,
                    "xyz.openbmc_project.Inventory.Decorator.Area",
                    [asyncResp, path](const boost::system::error_code& ec,
                                      const dbus::utility::DBusPropertiesMap&
                                          propertiesList) {
                        if (ec)
                        {
                            return;
                        }

                        for (const auto& [propertyName, value] : propertiesList)
                        {
                            if (propertyName == "PhysicalContext")
                            {
                                const auto* physicalcontext =
                                    std::get_if<std::string>(&value);
                                if (physicalcontext == nullptr)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "PropertyName resource not found.");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                asyncResp->res.jsonValue[propertyName] =
                                    redfish::dbus_utils::toPhysicalContext(
                                        *physicalcontext);
                                return;
                            }
                        }
                    });

                // Read related items
                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper", path + "/chassis",
                    "xyz.openbmc_project.Association", "endpoints",
                    [asyncResp](const boost::system::error_code& ec4,
                                const std::vector<std::string>& resp) {
                        if (ec4)
                        {
                            BMCWEB_LOG_DEBUG("Get Related Items failed: {}",
                                             ec4);
                            return;
                        }

                        nlohmann::json& relatedItemsArray =
                            asyncResp->res.jsonValue["RelatedItem"];
                        relatedItemsArray = nlohmann::json::array();
                        for (const std::string& chassisPath : resp)
                        {
                            sdbusplus::message::object_path objectPath(
                                chassisPath);
                            std::string chassisName = objectPath.filename();
                            if (chassisName.empty())
                            {
                                return;
                            }
                            redfish::nvidia_chassis_utils::
                                getChassisRelatedItem(
                                    asyncResp, objectPath, chassisName,
                                    redfish::nvidia_control_utils::
                                        getControlSettingRelatedItems);
                        }
                    });
            }
        });
}

inline void getPowerReading(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisID,
                            const std::string& chassisPath)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", chassisPath + "/all_sensors",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, chassisID,
         chassisPath](const boost::system::error_code& ec,
                      const std::vector<std::string>& resp) {
            if (ec)
            {
                return; // no sensors = no failures
            }
            for (const auto& sensorPath : resp)
            {
                sdbusplus::message::object_path objPath(sensorPath);
                std::string prefix = "/xyz/openbmc_project/sensors/power/" +
                                     chassisID + "_Power";
                if (sensorPath.find(prefix) == std::string::npos)
                {
                    continue;
                }

                dbus::utility::getDbusObject(
                    sensorPath,
                    std::array<std::string_view, 1>{
                        "xyz.openbmc_project.Sensor.Value"},
                    [asyncResp, chassisPath, sensorPath](
                        const boost::system::error_code& ec2,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& objInfo) {
                        if (ec2)
                        {
                            BMCWEB_LOG_ERROR(
                                "ObjectMapper::GetObject call failed: {}",
                                ec2.what());
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        for (const auto& [service, interfaces] : objInfo)
                        {
                            dbus::utility::getAllProperties(
                                service, sensorPath,
                                "xyz.openbmc_project.Sensor.Value",
                                [asyncResp, chassisPath, sensorPath](
                                    const boost::system::error_code& ec3,
                                    const dbus::utility::DBusPropertiesMap&
                                        propertiesList) {
                                    if (ec3)
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "ObjectMapper::GetObject call failed:{}",
                                            ec3.what());
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }
                                    for (const auto& [propertyName, val] :
                                         propertiesList)
                                    {
                                        if (propertyName == "Value" &&
                                            std::holds_alternative<double>(val))
                                        {
                                            const auto value =
                                                std::get<double>(val);
                                            sdbusplus::message::object_path
                                                chassisObjectPath(chassisPath);
                                            sdbusplus::message::object_path
                                                sensorObjectPath(sensorPath);
                                            asyncResp->res
                                                .jsonValue["Sensor"]
                                                          ["Reading"] = value;
                                            asyncResp->res
                                                .jsonValue["Sensor"]
                                                          ["DataSourceUri"] =
                                                ("/redfish/v1/Chassis/" +
                                                 chassisObjectPath.filename() +
                                                 "/Sensors/" +
                                                 sensorObjectPath.filename());
                                            return;
                                        }
                                    }
                                });
                        }
                    });
            }
        });
}

inline void changepowercap(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& path, size_t setpointValue)
{
    dbus::utility::getDbusObject(
        path,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Control.Power.Cap"},
        [asyncResp, setpointValue,
         path](const boost::system::error_code& errorno,
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
                dbus::utility::getDbusObject(
                    path,
                    std::array<std::string_view, 1>{
                        nvidia_async_operation_utils::setAsyncInterfaceName},
                    [asyncResp, path, setpointValue,
                     element](const boost::system::error_code& ec,
                              const dbus::utility::MapperGetObject& object) {
                        if (!ec)
                        {
                            for (const auto& [serv, _] : object)
                            {
                                if (serv != element.first)
                                {
                                    continue;
                                }

                                BMCWEB_LOG_DEBUG(
                                    "Performing Patch using Set Async Method Call");
                                std::string setPointPropName2(
                                    setPointPropName());
                                nvidia_async_operation_utils::
                                    doGenericSetAsyncAndGatherResult(
                                        asyncResp, std::chrono::seconds(60),
                                        element.first, path,
                                        "xyz.openbmc_project.Control.Power.Cap",
                                        setPointPropName2,
                                        dbus::utility::DbusVariantType(
                                            setpointValue),
                                        nvidia_async_operation_utils::
                                            PatchPowerCapCallback{
                                                asyncResp, static_cast<int64_t>(
                                                               setpointValue)});

                                return;
                            }
                        }

                        BMCWEB_LOG_DEBUG(
                            "Performing Patch using set-property Call");

                        dbus::utility::async_method_call(
                            [asyncResp, path, setpointValue,
                             element](const boost::system::error_code& ec2,
                                      sdbusplus::message::message& msg) {
                                if (!ec2)
                                {
                                    BMCWEB_LOG_DEBUG(
                                        "Set power limit property succeeded");
                                    messages::success(asyncResp->res);
                                    return;
                                }
                                // Read and convert dbus error message to
                                // redfish error
                                const sd_bus_error* dbusError = msg.get_error();
                                if (dbusError == nullptr)
                                {
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                if (strcmp(
                                        dbusError->name,
                                        "xyz.openbmc_project.Common.Error.InvalidArgument") ==
                                    0)
                                {
                                    // Invalid value
                                    messages::propertyValueIncorrect(
                                        asyncResp->res, "setpoint",
                                        std::to_string(setpointValue));
                                }
                                else if (strcmp(dbusError->name,
                                                "xyz.openbmc_project.Common."
                                                "Device.Error.WriteFailure") ==
                                         0)
                                {
                                    // Service failed to change the config
                                    messages::operationFailed(asyncResp->res);
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
                                    messages::asyncError(asyncResp->res,
                                                         errBusy,
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
                                    messages::asyncError(asyncResp->res,
                                                         errTimeout,
                                                         errTimeoutResolution);
                                }
                                else
                                {
                                    messages::internalError(asyncResp->res);
                                }
                            },
                            element.first, path,
                            "org.freedesktop.DBus.Properties", "Set",
                            "xyz.openbmc_project.Control.Power.Cap",
                            setPointPropName(),
                            dbus::utility::DbusVariantType(setpointValue));
                    });
            }
        });
}

inline void changePowerCapEnable(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path, const bool& enabled)
{
    dbus::utility::getDbusObject(
        path,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Control.Power.Cap"},
        [asyncResp, enabled,
         path](const boost::system::error_code& errorno,
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
                dbus::utility::setProperty(
                    element.first, path,
                    "xyz.openbmc_project.Control.Power.Cap", "PowerCapEnable",
                    enabled,
                    [asyncResp, path,
                     element](const boost::system::error_code& ec2,
                              const sdbusplus::message::message& msg) {
                        if (!ec2)
                        {
                            BMCWEB_LOG_DEBUG(
                                "Set power cap enable property succeeded");
                            messages::success(asyncResp->res);
                            return;
                        }
                        // Read and convert dbus error message to redfish error
                        const sd_bus_error* dbusError = msg.get_error();
                        if (dbusError == nullptr)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        if (strcmp(dbusError->name,
                                   "xyz.openbmc_project.Common."
                                   "Device.Error.WriteFailure") == 0)
                        {
                            // Service failed to change the config
                            messages::operationFailed(asyncResp->res);
                        }
                        else if (
                            strcmp(
                                dbusError->name,
                                "org.freedesktop.DBus.Error.UnknownProperty") ==
                            0)
                        {
                            // Some implementation does not have PowerCapEnable
                            return;
                        }
                        else
                        {
                            messages::internalError(asyncResp->res);
                        }
                    });
            }
        });
}

inline void changeControlMode(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path, const std::string& mode)
{
    dbus::utility::getDbusObject(
        path,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Control.Power.Mode"},
        [asyncResp, mode, path](const boost::system::error_code& errorno,
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
                dbus::utility::async_method_call(
                    [asyncResp, path, mode,
                     element](const boost::system::error_code& ec2,
                              sdbusplus::message::message& msg) {
                        if (!ec2)
                        {
                            BMCWEB_LOG_DEBUG(
                                "Set ControlMode property succeeded");
                            messages::success(asyncResp->res);
                            return;
                        }
                        // Read and convert dbus error message to redfish error
                        const sd_bus_error* dbusError = msg.get_error();
                        if (dbusError == nullptr)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        if (strcmp(dbusError->name,
                                   "xyz.openbmc_project.Common."
                                   "Device.Error.WriteFailure") == 0)
                        {
                            // Service failed to change the config
                            messages::operationFailed(asyncResp->res);
                        }
                        else if (
                            strcmp(
                                dbusError->name,
                                "org.freedesktop.DBus.Error.UnknownProperty") ==
                            0)
                        {
                            // Some implementation does not have PowerCapEnable
                            return;
                        }
                        else
                        {
                            messages::internalError(asyncResp->res);
                        }
                    },
                    element.first, path, "org.freedesktop.DBus.Properties",
                    "Set", "xyz.openbmc_project.Control.Power.Mode",
                    "PowerMode", dbus::utility::DbusVariantType(mode));
            }
        });
}

inline void requestRoutesChassisControlsCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Controls/")
        .privileges(redfish::privileges::getControl)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& chassisID) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                auto getChassisPath = [asyncResp, chassisID](
                                          const std::optional<std::string>&
                                              validChassisPath) {
                    if (!validChassisPath)
                    {
                        BMCWEB_LOG_ERROR("Not a valid chassis ID: {}",
                                         chassisID);
                        messages::resourceNotFound(asyncResp->res, "Chassis",
                                                   chassisID);
                        return;
                    }
                    asyncResp->res.jsonValue = {
                        {"@odata.type", "#ControlCollection.ControlCollection"},
                        {"@odata.id",
                         "/redfish/v1/Chassis/" + chassisID + "/Controls"},
                        {"Name", "Controls"},
                        {"Description",
                         "The collection of Controlable resource instances " +
                             chassisID}};
                    getPowercontrolObjects(asyncResp, chassisID,
                                           *validChassisPath);
                    redfish::nvidia_control_utils::getClockLimitControlObjects(
                        asyncResp, chassisID, *validChassisPath);
                };
                redfish::chassis_utils::getValidChassisPath(
                    asyncResp, chassisID, std::move(getChassisPath));
            });
}

inline void requestRoutesChassisControls(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Controls/<str>/")
        .privileges(redfish::privileges::getControl)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisID,
                            const std::string& controlID) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            auto getControlSystem = [asyncResp, chassisID, controlID](
                                        const std::optional<std::string>&
                                            validChassisPath) {
                if (!validChassisPath)
                {
                    BMCWEB_LOG_ERROR("Not a valid chassis ID:{}", chassisID);
                    messages::resourceNotFound(asyncResp->res, "Chassis",
                                               chassisID);
                    return;
                }
                asyncResp->res.jsonValue["@odata.type"] =
                    "#Control.v1_3_0.Control";
                asyncResp->res.jsonValue["SetPointUnits"] = setPointUnits();
                asyncResp->res.jsonValue["Id"] = controlID;
                asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Chassis/" + chassisID + "/Controls/" +
                    controlID;
                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper",
                    *validChassisPath + "/power_controls",
                    "xyz.openbmc_project.Association", "endpoints",
                    [asyncResp, chassisID, controlID,
                     validChassisPath](const boost::system::error_code& ec,
                                       const std::vector<std::string>& resp) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR(
                                "ObjectMapper::GetObject call failed: {}", ec);
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        auto validendpoint = false;
                        for (const auto& object : resp)
                        {
                            sdbusplus::message::object_path objPath(object);
                            if (objPath.filename() == controlID)
                            {
                                asyncResp->res.jsonValue["Name"] =
                                    "System Power Control";
                                asyncResp->res.jsonValue["ControlType"] =
                                    "Power";
                                asyncResp->res.jsonValue["Status"]["Health"] =
                                    resource::Health::OK;

                                getChassisPower(asyncResp, object,
                                                *validChassisPath);
                                getTotalPower(asyncResp, chassisID);
                                validendpoint = true;
                                break;
                            }
                        }
                        if (!validendpoint)
                        {
                            BMCWEB_LOG_ERROR("control id resource not found");
                            messages::resourceNotFound(asyncResp->res,
                                                       "ControlID", controlID);
                        }
                    });
            };

            auto getControlCpu = [asyncResp, chassisID, controlID](
                                     const std::optional<std::string>&
                                         validChassisPath) {
                if (!validChassisPath)
                {
                    BMCWEB_LOG_ERROR("Not a valid chassis ID:{}", chassisID);
                    messages::resourceNotFound(asyncResp->res, "Chassis",
                                               chassisID);
                    return;
                }
                asyncResp->res.jsonValue["@odata.type"] =
                    "#Control.v1_3_0.Control";
                asyncResp->res.jsonValue["SetPointUnits"] = "W";
                asyncResp->res.jsonValue["Id"] = controlID;
                asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Chassis/" + chassisID + "/Controls/" +
                    controlID;
                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper",
                    *validChassisPath + "/power_controls",
                    "xyz.openbmc_project.Association", "endpoints",
                    [asyncResp, chassisID, controlID,
                     validChassisPath](const boost::system::error_code& ec2,
                                       const std::vector<std::string>& resp) {
                        if (ec2)
                        {
                            BMCWEB_LOG_ERROR("Get Related Items failed: {}",
                                             ec2);
                            return;
                        }
                        auto validendpoint = false;
                        for (const auto& object : resp)
                        {
                            sdbusplus::message::object_path objPath(object);
                            if (objPath.filename() == controlID)
                            {
                                if (controlID.find("_CPU_") !=
                                    std::string::npos)
                                {
                                    asyncResp->res.jsonValue["Name"] =
                                        "Cpu Power Control";
                                }
                                else
                                {
                                    asyncResp->res.jsonValue["Name"] =
                                        "Module Power Control";
                                    // Automatic mode from H100 8-GPU
                                    // Redfish SMBPBI Supplement
                                    asyncResp->res.jsonValue["ControlMode"] =
                                        "Automatic";
                                }
                                asyncResp->res.jsonValue["ControlType"] =
                                    "Power";
                                getControlSettings(asyncResp, object);
                                getPowerReading(asyncResp, chassisID,
                                                *validChassisPath);
                                validendpoint = true;
                                break;
                            }
                        }
                        if (!validendpoint)
                        {
                            BMCWEB_LOG_ERROR("control id resource not found");
                            messages::resourceNotFound(asyncResp->res,
                                                       "ControlID", controlID);
                        }
                    });
            };

            auto getChassisControl = [asyncResp, chassisID, controlID,
                                      getControlSystem](
                                         const std::optional<std::string>&
                                             validChassisPath) {
                if (!validChassisPath)
                {
                    BMCWEB_LOG_ERROR("Not a valid chassis ID:{}", chassisID);
                    messages::resourceNotFound(asyncResp->res, "Chassis",
                                               chassisID);
                    return;
                }

                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper",
                    *validChassisPath + "/all_processors",
                    "xyz.openbmc_project.Association", "endpoints",
                    [asyncResp, controlID, chassisID, validChassisPath,
                     getControlSystem](const boost::system::error_code& ec,
                                       const std::vector<std::string>& resp) {
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG(
                                "ObjectMapper::Get Associated Processor object call failed : {}",
                                ec);
                            getControlSystem(validChassisPath);
                            return;
                        }

                        for (const auto& processorPath : resp)
                        {
                            dbus::utility::getDbusObject(
                                processorPath,
                                std::array<std::string_view, 0>{},
                                [asyncResp, controlID, chassisID, processorPath,
                                 validChassisPath](
                                    const boost::system::error_code& ec1,
                                    const dbus::utility::MapperGetObject&
                                        objType) {
                                    if (ec1 || objType.empty())
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "GetObject for path {} failed",
                                            processorPath.c_str());
                                        messages::resourceNotFound(
                                            asyncResp->res, "ControlID",
                                            controlID);
                                        return;
                                    }
                                    for (auto [service, interfaces] : objType)
                                    {
                                        if ((std::find(
                                                 interfaces.begin(),
                                                 interfaces.end(),
                                                 "xyz.openbmc_project.Inventory.Item.Accelerator") !=
                                             interfaces.end()) ||
                                            (std::find(
                                                 interfaces.begin(),
                                                 interfaces.end(),
                                                 "com.nvidia.GPMMetrics") !=
                                             interfaces.end()))
                                        {
                                            auto processorName =
                                                processorPath.substr(
                                                    processorPath.find_last_of(
                                                        '/') +
                                                    1);
                                            redfish::nvidia_control_utils::
                                                getClockLimitControl(
                                                    asyncResp, chassisID,
                                                    controlID, validChassisPath,
                                                    processorName);
                                            return;
                                        }
                                    }
                                });
                        }
                    });
            };

            auto getControl = [asyncResp, chassisID, getControlSystem,
                               getChassisControl,
                               getControlCpu](const std::optional<std::string>&
                                                  validChassisPath) {
                if (!validChassisPath)
                {
                    BMCWEB_LOG_ERROR("Not a valid chassis ID:{}", chassisID);
                    messages::resourceNotFound(asyncResp->res, "Chassis",
                                               chassisID);
                    return;
                }
                dbus::utility::getDbusObject(
                    *validChassisPath, std::array<std::string_view, 0>{},
                    [asyncResp, getControlSystem, getControlCpu,
                     getChassisControl, validChassisPath](
                        const boost::system::error_code& ec,
                        const dbus::utility::MapperGetObject& objType) {
                        if (ec || objType.empty())
                        {
                            BMCWEB_LOG_ERROR("GetObject for path {}",
                                             (*validChassisPath).c_str());
                            return;
                        }
                        for (auto [service, interfaces] : objType)
                        {
                            if (std::find(
                                    interfaces.begin(), interfaces.end(),
                                    "xyz.openbmc_project.Inventory.Item.Cpu") !=
                                    interfaces.end() ||
                                std::find(
                                    interfaces.begin(), interfaces.end(),
                                    "xyz.openbmc_project.Inventory.Item.ProcessorModule") !=
                                    interfaces.end())
                            {
                                getControlCpu(validChassisPath);
                                return;
                            }
                        }
                        redfish::nvidia_control_utils::getControlCpuObjects(
                            asyncResp, getControlCpu, validChassisPath);
                        // Not a CPU
                        getChassisControl(validChassisPath);
                    });
            };
            redfish::chassis_utils::getValidChassisPath(asyncResp, chassisID,
                                                        std::move(getControl));
        });

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Controls/<str>/")
        .privileges(redfish::privileges::patchControl)
        .methods(
            boost::beast::http::verb::
                patch)([&app](
                           const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& chassisID,
                           const std::string& controlID) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            auto patchControlSystem = [asyncResp, chassisID, controlID,
                                       &req](const std::optional<std::string>&
                                                 validChassisPath) {
                if (!validChassisPath)
                {
                    BMCWEB_LOG_ERROR("Not a valid chassis ID: {}", chassisID);
                    messages::resourceNotFound(asyncResp->res, "Chassis",
                                               chassisID);
                    return;
                }
                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper",
                    *validChassisPath + "/power_controls",
                    "xyz.openbmc_project.Association", "endpoints",
                    [asyncResp, chassisID, controlID,
                     &req](const boost::system::error_code& ec,
                           const std::vector<std::string>& resp) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR(
                                "ObjectMapper::GetObject call failed: {}", ec);
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        auto validendpoint = false;
                        for (const auto& object : resp)
                        {
                            sdbusplus::message::object_path objPath(object);
                            if (objPath.filename() == controlID)
                            {
                                validendpoint = true;
                                std::optional<std::string> mode;
                                std::optional<uint32_t> setpointValue;
                                if (!json_util::readJsonAction(
                                        req, asyncResp->res, "ControlMode",
                                        mode, "SetPoint", setpointValue))
                                {
                                    return;
                                }
                                if (mode)
                                {
                                    auto modefound = false;
                                    for (const auto& pair : modes)
                                    {
                                        if (pair.second == mode)
                                        {
                                            changeControlMode(asyncResp, object,
                                                              pair.first);
                                            modefound = true;
                                            break;
                                        }
                                    }
                                    if (!modefound)
                                    {
                                        BMCWEB_LOG_ERROR("invalid input");
                                        messages::actionParameterUnknown(
                                            asyncResp->res, "ControlMode",
                                            *mode);
                                    }
                                }
                                if (setpointValue)
                                {
                                    if (BMCWEB_POWER_CONTROL_TYPE_PERCENTAGE &&
                                        (setpointValue > 100))
                                    {
                                        BMCWEB_LOG_ERROR("invalid input");
                                        std::string strValue = std::to_string(
                                            setpointValue.value());
                                        messages::actionParameterUnknown(
                                            asyncResp->res, "SetPoint",
                                            std::string_view(strValue));
                                    }
                                    changepowercap(asyncResp, object,
                                                   *setpointValue);
                                }
                                break;
                            }
                        }
                        if (!validendpoint)
                        {
                            BMCWEB_LOG_ERROR("control id resource not found");
                            messages::resourceNotFound(asyncResp->res,
                                                       "ControlID", controlID);
                        }
                    });
            };

            auto patchControlCpu = [asyncResp, chassisID, controlID,
                                    &req](const std::optional<std::string>&
                                              validChassisPath) {
                if (!validChassisPath)
                {
                    BMCWEB_LOG_ERROR("Not a valid chassis ID:{}", chassisID);
                    messages::resourceNotFound(asyncResp->res, "Chassis",
                                               chassisID);
                    return;
                }
                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper",
                    *validChassisPath + "/power_controls",
                    "xyz.openbmc_project.Association", "endpoints",
                    [asyncResp, chassisID, controlID,
                     &req](const boost::system::error_code& ec,
                           const std::vector<std::string>& resp) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR(
                                "ObjectMapper::GetObject call failed: {}", ec);
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        auto validendpoint = false;
                        for (const auto& object : resp)
                        {
                            sdbusplus::message::object_path objPath(object);
                            if (objPath.filename() == controlID)
                            {
                                validendpoint = true;
                                std::optional<std::string> mode;
                                std::optional<uint32_t> setpointValue2;
                                if (!json_util::readJsonPatch(
                                        req, asyncResp->res, "ControlMode",
                                        mode, "SetPoint", setpointValue2))
                                {
                                    return;
                                }

                                if (mode)
                                {
                                    if (controlID.find("_CPU_") !=
                                        std::string::npos)
                                    {
                                        if ((*mode == "Automatic") ||
                                            (*mode == "Override") ||
                                            (*mode == "Manual"))
                                        {
                                            changePowerCapEnable(asyncResp,
                                                                 object, true);
                                        }
                                        else if (*mode == "Disabled")
                                        {
                                            changePowerCapEnable(asyncResp,
                                                                 object, false);
                                        }
                                        else
                                        {
                                            BMCWEB_LOG_ERROR("invalid input");
                                            messages::actionParameterUnknown(
                                                asyncResp->res, "ControlMode",
                                                *mode);
                                        }
                                    }
                                    else
                                    {
                                        messages::actionParameterNotSupported(
                                            asyncResp->res, "ControlMode",
                                            *mode);
                                    }
                                }

                                if (setpointValue2)
                                {
                                    changepowercap(asyncResp, object,
                                                   *setpointValue2);
                                }
                                break;
                            }
                        }
                        if (!validendpoint)
                        {
                            BMCWEB_LOG_ERROR("control id resource not found");
                            messages::resourceNotFound(asyncResp->res,
                                                       "ControlID", controlID);
                        }
                    });
            };

            auto patchChassisControl = [asyncResp, chassisID, controlID,
                                        patchControlSystem, patchControlCpu,
                                        &req](const std::optional<std::string>&
                                                  validChassisPath) {
                if (!validChassisPath)
                {
                    BMCWEB_LOG_ERROR("Not a valid chassis ID:{}", chassisID);
                    messages::resourceNotFound(asyncResp->res, "Chassis",
                                               chassisID);
                    return;
                }

                // Handles the getDbusObject result for each processor.
                // Defined as a named lambda so clang-format keeps it at
                // a readable indentation level.
                auto handleProcessorObject =
                    [asyncResp, controlID, chassisID, validChassisPath,
                     patchControlCpu,
                     &req](const std::string& processorPath,
                           const boost::system::error_code& ec1,
                           const dbus::utility::MapperGetObject& objType) {
                        if (ec1 || objType.empty())
                        {
                            BMCWEB_LOG_ERROR("GetObject for path {} failed",
                                             processorPath);
                            messages::resourceNotFound(asyncResp->res,
                                                       "ControlID", controlID);
                            return;
                        }
                        for (const auto& [service, interfaces] : objType)
                        {
                            bool isGpu =
                                std::find(interfaces.begin(), interfaces.end(),
                                          "xyz.openbmc_project.Inventory.Item"
                                          ".Accelerator") != interfaces.end() ||
                                std::find(interfaces.begin(), interfaces.end(),
                                          "com.nvidia.GPMMetrics") !=
                                    interfaces.end();
                            bool isCpu =
                                std::find(interfaces.begin(), interfaces.end(),
                                          "xyz.openbmc_project.Inventory.Item"
                                          ".Cpu") != interfaces.end() ||
                                std::find(interfaces.begin(), interfaces.end(),
                                          "xyz.openbmc_project.Inventory.Item"
                                          ".ProcessorModule") !=
                                    interfaces.end();
                            if (isGpu)
                            {
                                std::string processorName =
                                    processorPath.substr(
                                        processorPath.find_last_of('/') + 1);
                                redfish::nvidia_control_utils::
                                    patchClockLimitControl(
                                        asyncResp, chassisID, controlID, req,
                                        validChassisPath, processorName);
                                return;
                            }
                            if (isCpu)
                            {
                                patchControlCpu(validChassisPath);
                                return;
                            }
                        }
                    };

                // Handles the getProperty all_processors result.
                auto handleAllProcessors =
                    [asyncResp, controlID, validChassisPath, patchControlSystem,
                     chassisID, handleProcessorObject](
                        const boost::system::error_code& ec,
                        const std::vector<std::string>& resp) {
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG(
                                "ObjectMapper::Get Associated Processor "
                                "object call failed : {}",
                                ec);
                            patchControlSystem(validChassisPath);
                            return;
                        }
                        for (const auto& processorPath : resp)
                        {
                            dbus::utility::getDbusObject(
                                processorPath,
                                std::array<std::string_view, 0>{},
                                [handleProcessorObject, processorPath](
                                    const boost::system::error_code& ec1,
                                    const dbus::utility::MapperGetObject&
                                        objType) {
                                    handleProcessorObject(processorPath, ec1,
                                                          objType);
                                });
                        }
                    };

                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper",
                    *validChassisPath + "/all_processors",
                    "xyz.openbmc_project.Association", "endpoints",
                    handleAllProcessors);
            };

            auto patchControl = [asyncResp, chassisID, patchChassisControl,
                                 patchControlSystem, patchControlCpu](
                                    const std::optional<std::string>&
                                        validChassisPath) {
                if (!validChassisPath)
                {
                    BMCWEB_LOG_ERROR("Not a valid chassis ID:{}", chassisID);
                    messages::resourceNotFound(asyncResp->res, "Chassis",
                                               chassisID);
                    return;
                }
                dbus::utility::getDbusObject(
                    *validChassisPath, std::array<std::string_view, 0>{},
                    [asyncResp, patchChassisControl, patchControlSystem,
                     patchControlCpu, validChassisPath](
                        const boost::system::error_code& ec,
                        const dbus::utility::MapperGetObject& objType) {
                        if (ec || objType.empty())
                        {
                            BMCWEB_LOG_ERROR("GetObject for path {}",
                                             (*validChassisPath).c_str());
                            return;
                        }
                        for (auto [service, interfaces] : objType)
                        {
                            if (std::find(
                                    interfaces.begin(), interfaces.end(),
                                    "xyz.openbmc_project.Inventory.Item.Cpu") !=
                                    interfaces.end() ||
                                std::find(
                                    interfaces.begin(), interfaces.end(),
                                    "xyz.openbmc_project.Inventory.Item.ProcessorModule") !=
                                    interfaces.end())
                            {
                                patchControlCpu(validChassisPath);
                                return;
                            }
                        }
                        patchChassisControl(validChassisPath);
                    });
            };
            redfish::chassis_utils::getValidChassisPath(
                asyncResp, chassisID, std::move(patchControl));
        });
}

inline void requestRoutesChassisControlsReset(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Controls/<str>/Actions/Control.ResetToDefaults/")
        .privileges(redfish::privileges::postControl)
        .methods(
            boost::beast::http::verb::
                post)([&app](
                          const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& chassisId,
                          const std::string& controlId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            auto postChassisClockLimitControl = [asyncResp, chassisId,
                                                 controlId](
                                                    const std::optional<
                                                        std::string>&
                                                        validChassisPath,
                                                    const std::string&
                                                        processorName) {
                if (!validChassisPath)
                {
                    BMCWEB_LOG_ERROR("Not a valid chassis ID:{}", chassisId);
                    messages::resourceNotFound(asyncResp->res, "Chassis",
                                               chassisId);
                    return;
                }
                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper",
                    *validChassisPath + "/clock_controls",
                    "xyz.openbmc_project.Association", "endpoints",
                    [asyncResp, chassisId, controlId, validChassisPath,
                     processorName](const boost::system::error_code& ec,
                                    const std::vector<std::string>& resp) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR(
                                "ObjectMapper::Get Associated clock control object call failed: {}",
                                ec);
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        BMCWEB_LOG_DEBUG(
                            "Call Reset Clock Limit Control for processor: {}",
                            processorName);

                        auto validendpoint = false;
                        for (const auto& object : resp)
                        {
                            sdbusplus::message::object_path objPath(object);
                            if (objPath.filename() == controlId)
                            {
                                redfish::nvidia_control_utils::
                                    postClockLimitControl(asyncResp, chassisId,
                                                          controlId,
                                                          validChassisPath);
                                validendpoint = true;
                            }
                        }
                        if (!validendpoint)
                        {
                            BMCWEB_LOG_ERROR("control id resource not found");
                            messages::resourceNotFound(asyncResp->res,
                                                       "ControlID", controlId);
                        }
                    });
            };

            auto postChassisControl = [asyncResp, chassisId, controlId,
                                       postChassisClockLimitControl](
                                          const std::optional<std::string>&
                                              validChassisPath) {
                if (!validChassisPath)
                {
                    BMCWEB_LOG_ERROR("Not a valid chassis ID:{}", chassisId);
                    messages::resourceNotFound(asyncResp->res, "Chassis",
                                               chassisId);
                    return;
                }

                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper",
                    *validChassisPath + "/all_processors",
                    "xyz.openbmc_project.Association", "endpoints",
                    [asyncResp, controlId, validChassisPath,
                     postChassisClockLimitControl](
                        const boost::system::error_code& ec,
                        const std::vector<std::string>& resp) {
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG(
                                "ObjectMapper::Get Associated Processor object call failed : {}",
                                ec);
                            return;
                        }

                        for (const auto& processorPath : resp)
                        {
                            dbus::utility::getDbusObject(
                                processorPath,
                                std::array<std::string_view, 0>{},
                                [asyncResp, controlId, processorPath,
                                 postChassisClockLimitControl,
                                 validChassisPath](
                                    const boost::system::error_code& ec1,
                                    const dbus::utility::MapperGetObject&
                                        objType) {
                                    if (ec1 || objType.empty())
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "GetObject for path {} failed",
                                            processorPath.c_str());
                                        messages::resourceNotFound(
                                            asyncResp->res, "ControlId",
                                            controlId);
                                        return;
                                    }
                                    for (auto [service, interfaces] : objType)
                                    {
                                        if ((std::find(
                                                 interfaces.begin(),
                                                 interfaces.end(),
                                                 "xyz.openbmc_project.Inventory.Item.Accelerator") !=
                                             interfaces.end()) ||
                                            (std::find(
                                                 interfaces.begin(),
                                                 interfaces.end(),
                                                 "com.nvidia.GPMMetrics") !=
                                             interfaces.end()))
                                        {
                                            auto processorName =
                                                processorPath.substr(
                                                    processorPath.find_last_of(
                                                        '/') +
                                                    1);
                                            postChassisClockLimitControl(
                                                validChassisPath,
                                                processorName);
                                            return;
                                        }
                                    }
                                });
                        }
                    });
            };

            // check for CPU
            auto postControl = [asyncResp, postChassisControl, chassisId,
                                controlId](const std::optional<std::string>&
                                               validChassisPath) {
                if (!validChassisPath)
                {
                    BMCWEB_LOG_ERROR("Not a valid chassis ID:{}", chassisId);
                    messages::resourceNotFound(asyncResp->res, "Chassis",
                                               chassisId);
                    return;
                }

                dbus::utility::getDbusObject(
                    *validChassisPath, std::array<std::string_view, 0>{},
                    [asyncResp, postChassisControl, validChassisPath](
                        const boost::system::error_code& ec,
                        const dbus::utility::MapperGetObject& objType) {
                        if (ec || objType.empty())
                        {
                            BMCWEB_LOG_ERROR("GetObject for path {}",
                                             (*validChassisPath).c_str());
                            return;
                        }
                        for (auto [service, interfaces] : objType)
                        {
                            if (std::find(
                                    interfaces.begin(), interfaces.end(),
                                    "xyz.openbmc_project.Inventory.Item.Cpu") !=
                                    interfaces.end() ||
                                std::find(
                                    interfaces.begin(), interfaces.end(),
                                    "xyz.openbmc_project.Inventory.Item.ProcessorModule") !=
                                    interfaces.end())
                            {
                                return;
                            }
                        }
                        postChassisControl(validChassisPath);
                    });
            };

            redfish::chassis_utils::getValidChassisPath(asyncResp, chassisId,
                                                        std::move(postControl));
        });
}

} // namespace redfish
