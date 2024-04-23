/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
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

namespace redfish
{
namespace nvidia_chassis_utils
{
/* * @brief Fill out links association to underneath chassis by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void
    getChassisLinksContains(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get underneath chassis links");
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec2,
                std::variant<std::vector<std::string>>& resp) {
        if (ec2)
        {
            return; // no chassis = no failures
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            return;
        }
        nlohmann::json& linksArray = aResp->res.jsonValue["Links"]["Contains"];
        linksArray = nlohmann::json::array();
        for (const std::string& chassisPath : *data)
        {
            sdbusplus::message::object_path objectPath(chassisPath);
            std::string chassisName = objectPath.filename();
            if (chassisName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            linksArray.push_back(
                {{"@odata.id", "/redfish/v1/Chassis/" + chassisName}});
        }
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Fill out processor links association by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void
    getChassisProcessorLinks(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                             const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get underneath processor links");
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec2,
                std::variant<std::vector<std::string>>& resp) {
        if (ec2)
        {
            return; // no processors = no failures
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            return;
        }
        nlohmann::json& linksArray =
            aResp->res.jsonValue["Links"]["Processors"];
        linksArray = nlohmann::json::array();
        for (const std::string& processorPath : *data)
        {
            sdbusplus::message::object_path objectPath(processorPath);
            std::string processorName = objectPath.filename();
            if (processorName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            linksArray.push_back(
                {{"@odata.id", "/redfish/v1/Systems/" PLATFORMSYSTEMID
                               "/Processors/" +
                                   processorName}});
        }
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_processors",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Fill out fabric switches links association by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getChassisFabricSwitchesLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get fabric switches links");
    crow::connections::systemBus->async_method_call(
        [aResp, objPath](const boost::system::error_code ec2,
                         std::variant<std::vector<std::string>>& resp) {
        if (ec2)
        {
            return; // no fabric = no failures
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr || data->size() > 1)
        {
            // There must be single fabric
            return;
        }
        const std::string& fabricPath = data->front();
        sdbusplus::message::object_path objectPath(fabricPath);
        std::string fabricId = objectPath.filename();
        if (fabricId.empty())
        {
            messages::internalError(aResp->res);
            return;
        }
        // Get the switches
        crow::connections::systemBus->async_method_call(
            [aResp, fabricId](const boost::system::error_code ec1,
                              std::variant<std::vector<std::string>>& resp1) {
            if (ec1)
            {
                return; // no switches = no failures
            }
            std::vector<std::string>* data1 =
                std::get_if<std::vector<std::string>>(&resp1);
            if (data1 == nullptr)
            {
                return;
            }
            // Sort the switches links
            std::sort(data1->begin(), data1->end());
            nlohmann::json& linksArray =
                aResp->res.jsonValue["Links"]["Switches"];
            linksArray = nlohmann::json::array();
            for (const std::string& switchPath : *data1)
            {
                sdbusplus::message::object_path objectPath1(switchPath);
                std::string switchId = objectPath1.filename();
                if (switchId.empty())
                {
                    messages::internalError(aResp->res);
                    return;
                }
                linksArray.push_back(
                    {{"@odata.id", std::string("/redfish/v1/Fabrics/")
                                       .append(fabricId)
                                       .append("/Switches/")
                                       .append(switchId)}});
            }
        },
            "xyz.openbmc_project.ObjectMapper", objPath + "/all_switches",
            "org.freedesktop.DBus.Properties", "Get",
            "xyz.openbmc_project.Association", "endpoints");
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/fabrics",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}


#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
/**
 * @brief Fill out chassis nvidia specific info by
 * requesting data from the associated D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void
    getOemBaseboardChassisAssert(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                 const std::string& objPath)

{
    BMCWEB_LOG_DEBUG("Get chassis OEM info");
    dbus::utility::findAssociations(
        objPath + "/associated_fru",
        [aResp{std::move(aResp)}](
            const boost::system::error_code ec,
            std::variant<std::vector<std::string>>& assoc) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("Cannot get association");
            return;
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&assoc);
        if (data == nullptr)
        {
            return;
        }
        const std::string& fruPath = data->front();
        crow::connections::systemBus->async_method_call(
            [aResp{std::move(aResp)},
             fruPath](const boost::system::error_code ec,
                      const std::vector<std::pair<
                          std::string, std::vector<std::string>>>& objects) {
            if (ec || objects.size() <= 0)
            {
                BMCWEB_LOG_DEBUG("Null value returned " "for serial number");
                messages::internalError(aResp->res);
                return;
            }
            const std::string& fruObject = objects[0].first;
            crow::connections::systemBus->async_method_call(
                [aResp{std::move(aResp)}](
                    const boost::system::error_code ec,
                    const std::vector<
                        std::pair<std::string,
                                  std::variant<std::string, bool, uint64_t>>>&
                        propertiesList) {
                if (ec || propertiesList.size() <= 0)
                {
                    messages::internalError(aResp->res);
                    return;
                }
                for (const auto& property : propertiesList)
                {
                    if (property.first == "CHASSIS_PART_NUMBER")
                    {
                        const std::string* value =
                            std::get_if<std::string>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_DEBUG("Null value returned " "Part number");
                            messages::internalError(aResp->res);
                            return;
                        }
                        aResp->res.jsonValue["Oem"]["Nvidia"]["PartNumber"] =
                            *value;
                    }
                    else if (property.first == "CHASSIS_SERIAL_NUMBER")
                    {
                        const std::string* value =
                            std::get_if<std::string>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_DEBUG("Null value returned " "for serial number");
                            messages::internalError(aResp->res);
                            return;
                        }
                        aResp->res.jsonValue["Oem"]["Nvidia"]["SerialNumber"] =
                            *value;
                    }
                }
            },
                fruObject, fruPath, "org.freedesktop.DBus.Properties", "GetAll",
                "xyz.openbmc_project.FruDevice");
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetObject", fruPath,
            std::array<std::string, 1>({"xyz.openbmc_project.FruDevice"}));
    });
}

/**
 * @brief Write chassis nvidia specific info to eeprom by
 * setting data to the associated Fru D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void setOemBaseboardChassisAssert(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& objPath,
    const std::string& prop, const std::string& value)
{
    BMCWEB_LOG_DEBUG("Set chassis OEM info: ");
    dbus::utility::findAssociations(
        objPath + "/associated_fru",
        [aResp{std::move(aResp)}, prop,
         value](const boost::system::error_code ec,
                std::variant<std::vector<std::string>>& assoc) {
        if (ec)
        {
            messages::internalError(aResp->res);
            return;
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&assoc);
        if (data == nullptr)
        {
            return;
        }
        const std::string& fruPath = data->front();
        crow::connections::systemBus->async_method_call(
            [aResp{std::move(aResp)}, fruPath, prop,
             value](const boost::system::error_code ec,
                    const std::vector<std::pair<
                        std::string, std::vector<std::string>>>& objects) {
            if (ec || objects.size() <= 0)
            {
                messages::internalError(aResp->res);
                return;
            }
            const std::string& fruObject = objects[0].first;
            if (prop == "PartNumber")
            {
                crow::connections::systemBus->async_method_call(
                    [aResp](const boost::system::error_code ec) {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG("DBUS response error: Set CHASSIS_PART_NUMBER{}", ec);
                        messages::internalError(aResp->res);
                        return;
                    }
                    messages::success(aResp->res);
                    BMCWEB_LOG_DEBUG("Set CHASSIS_PART_NUMBER done.");
                },
                    fruObject, fruPath, "org.freedesktop.DBus.Properties",
                    "Set", "xyz.openbmc_project.FruDevice",
                    "CHASSIS_PART_NUMBER",
                    dbus::utility::DbusVariantType(value));
            }
            else if (prop == "SerialNumber")
            {
                crow::connections::systemBus->async_method_call(
                    [aResp](const boost::system::error_code ec) {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG("DBUS response error: Set CHASSIS_SERIAL_NUMBER{}", ec);
                        messages::internalError(aResp->res);
                        return;
                    }
                    messages::success(aResp->res);
                    BMCWEB_LOG_DEBUG("Set CHASSIS_SERIAL_NUMBER done.");
                },
                    fruObject, fruPath, "org.freedesktop.DBus.Properties",
                    "Set", "xyz.openbmc_project.FruDevice",
                    "CHASSIS_SERIAL_NUMBER",
                    dbus::utility::DbusVariantType(value));
            }
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetObject", fruPath,
            std::array<std::string, 1>({"xyz.openbmc_project.FruDevice"}));
    });
}

/**
 * @brief Fill out chassis nvidia specific info by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getOemHdwWriteProtectInfo(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                      const std::string& service,
                                      const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Baseboard Hardware write protect info");
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string, std::variant<std::string, bool, uint64_t>>>&
                propertiesList) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error for " "Baseboard Hardware write protect info");
            messages::internalError(aResp->res);
            return;
        }

        for (const auto& property : propertiesList)
        {
            if (property.first == "WriteProtected")
            {
                const bool* value = std::get_if<bool>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_DEBUG("Null value returned " "for hardware write protected");
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["Oem"]["Nvidia"]
                                    ["HardwareWriteProtected"] = *value;
            }

            if (property.first == "WriteProtectedControl")
            {
                const bool* value = std::get_if<bool>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_DEBUG("Null value returned " "for hardware write protected control");
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["Oem"]["Nvidia"]
                                    ["HardwareWriteProtectedControl"] = *value;
            }
        }
    },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Software.Settings");
}

/**
 * @brief Fill out chassis nvidia specific info by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void
    getOemPCIeDeviceClockReferenceInfo(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                       const std::string& service,
                                       const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Baseboard PCIeReference clock count");
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string, std::variant<std::string, bool, uint64_t>>>&
                propertiesList) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error for " "Baseboard PCIeReference clock count");
            messages::internalError(aResp->res);
            return;
        }

        for (const auto& property : propertiesList)
        {
            if (property.first == "PCIeReferenceClockCount")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_DEBUG("Null value returned " "for pcie refernce clock count");
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["Oem"]["Nvidia"][property.first] = *value;
            }
        }
    },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Inventory.Decorator.PCIeRefClock");
}
#endif // BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES

/**
 * @brief Fill out chassis power limits info of a chassis by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getChassisPowerLimits(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                  const std::string& service,
                                  const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get chassis power limits");
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::variant<size_t>>>&
                propertiesList) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error for " "Chassis power limits");
            messages::internalError(aResp->res);
            return;
        }
        for (const std::pair<std::string, std::variant<size_t>>& property :
             propertiesList)
        {
            const std::string& propertyName = property.first;
            if ((propertyName == "MinPowerWatts") ||
                (propertyName == "MaxPowerWatts"))
            {
                const size_t* value = std::get_if<size_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_DEBUG("Null value returned " "for power limits");
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue[propertyName] = *value;
            }
        }
    },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Inventory.Decorator.PowerLimit");
}

inline void setStaticPowerHintByObjPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath, double cpuClockFrequency, double workloadFactor,
    double temperature)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath, cpuClockFrequency, workloadFactor, temperature](
            const boost::system::error_code errorno,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
        if (errorno)
        {
            return;
        }

        for (const auto& [service, interfaces] : objInfo)
        {
            crow::connections::systemBus->async_method_call(
                [asyncResp, objPath, service{service}, cpuClockFrequency,
                 workloadFactor, temperature](
                    const boost::system::error_code errorno,
                    const std::vector<std::pair<
                        std::string, std::variant<double, std::string, bool>>>&
                        propertiesList) {
                if (errorno)
                {
                    BMCWEB_LOG_ERROR("Properties::GetAll failed:{}objPath:{}", errorno, objPath);
                    messages::internalError(asyncResp->res);
                    return;
                }

                double cpuClockFrequencyMax = 0;
                double cpuClockFrequencyMin = 0;
                double workloadFactorMax = 0;
                double workloadFactorMin = 0;
                double temperatureMax = 0;
                double temperatureMin = 0;

                for (const auto& [propertyName, value] : propertiesList)
                {
                    if (propertyName == "MaxCpuClockFrequency" &&
                        std::holds_alternative<double>(value))
                    {
                        cpuClockFrequencyMax = std::get<double>(value);
                    }
                    else if (propertyName == "MinCpuClockFrequency" &&
                             std::holds_alternative<double>(value))
                    {
                        cpuClockFrequencyMin = std::get<double>(value);
                    }
                    else if (propertyName == "MaxTemperature" &&
                             std::holds_alternative<double>(value))
                    {
                        temperatureMax = std::get<double>(value);
                    }
                    else if (propertyName == "MinTemperature" &&
                             std::holds_alternative<double>(value))
                    {
                        temperatureMin = std::get<double>(value);
                    }
                    else if (propertyName == "MaxWorkloadFactor" &&
                             std::holds_alternative<double>(value))
                    {
                        workloadFactorMax = std::get<double>(value);
                    }
                    else if (propertyName == "MinWorkloadFactor" &&
                             std::holds_alternative<double>(value))
                    {
                        workloadFactorMin = std::get<double>(value);
                    }
                }

                if ((cpuClockFrequencyMax < cpuClockFrequency) ||
                    (cpuClockFrequencyMin > cpuClockFrequency))
                {
                    messages::propertyValueOutOfRange(
                        asyncResp->res, std::to_string(cpuClockFrequency),
                        "CpuClockFrequency");
                    return;
                }

                if ((temperatureMax < temperature) ||
                    (temperatureMin > temperature))
                {
                    messages::propertyValueOutOfRange(
                        asyncResp->res, std::to_string(temperature),
                        "Temperature");
                    return;
                }

                if ((workloadFactorMax < workloadFactor) ||
                    (workloadFactorMin > workloadFactor))
                {
                    messages::propertyValueOutOfRange(
                        asyncResp->res, std::to_string(workloadFactor),
                        "WorkloadFactor");
                    return;
                }

                crow::connections::systemBus->async_method_call(
                    [asyncResp,
                     objPath](const boost::system::error_code errorno) {
                    if (errorno)
                    {
                        BMCWEB_LOG_ERROR("StaticPowerHint::Estimate failed:{}", errorno);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                },
                    service, objPath, "com.nvidia.StaticPowerHint",
                    "EstimatePower", cpuClockFrequency, workloadFactor,
                    temperature);
            },
                service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
                "com.nvidia.StaticPowerHint");
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
        std::array<const char*, 1>{"com.nvidia.StaticPowerHint"});
}

inline void setStaticPowerHintByChassis(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisObjPath, double cpuClockFrequency,
    double workloadFactor, double temperature)
{
    // get endpoints of chassisId/all_controls
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisObjPath, cpuClockFrequency, workloadFactor,
         temperature](const boost::system::error_code,
                      std::variant<std::vector<std::string>>& resp) {
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            return;
        }
        for (const auto& objPath : *data)
        {
            setStaticPowerHintByObjPath(asyncResp, objPath, cpuClockFrequency,
                                        workloadFactor, temperature);
        }
    },
        "xyz.openbmc_project.ObjectMapper", chassisObjPath + "/all_controls",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getStaticPowerHintByObjPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath](
            const boost::system::error_code errorno,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
        if (errorno)
        {
            return;
        }

        for (const auto& [service, interfaces] : objInfo)
        {
            crow::connections::systemBus->async_method_call(
                [asyncResp, objPath](
                    const boost::system::error_code errorno,
                    const std::vector<std::pair<
                        std::string, std::variant<double, std::string, bool>>>&
                        propertiesList) {
                if (errorno)
                {
                    BMCWEB_LOG_ERROR("Properties::GetAll failed:{}objPath:{}", errorno, objPath);
                    messages::internalError(asyncResp->res);
                    return;
                }
                nlohmann::json& staticPowerHint =
                    asyncResp->res
                        .jsonValue["Oem"]["Nvidia"]["StaticPowerHint"];
                for (const auto& [propertyName, value] : propertiesList)
                {
                    if (propertyName == "MaxCpuClockFrequency" &&
                        std::holds_alternative<double>(value))
                    {
                        staticPowerHint["CpuClockFrequencyHz"]["AllowableMax"] =
                            std::get<double>(value);
                    }
                    else if (propertyName == "MinCpuClockFrequency" &&
                             std::holds_alternative<double>(value))
                    {
                        staticPowerHint["CpuClockFrequencyHz"]["AllowableMin"] =
                            std::get<double>(value);
                    }
                    else if (propertyName == "CpuClockFrequency" &&
                             std::holds_alternative<double>(value))
                    {
                        staticPowerHint["CpuClockFrequencyHz"]["SetPoint"] =
                            std::get<double>(value);
                    }
                    else if (propertyName == "MaxTemperature" &&
                             std::holds_alternative<double>(value))
                    {
                        staticPowerHint["TemperatureCelsius"]["AllowableMax"] =
                            std::get<double>(value);
                    }
                    else if (propertyName == "MinTemperature" &&
                             std::holds_alternative<double>(value))
                    {
                        staticPowerHint["TemperatureCelsius"]["AllowableMin"] =
                            std::get<double>(value);
                    }
                    else if (propertyName == "Temperature" &&
                             std::holds_alternative<double>(value))
                    {
                        staticPowerHint["TemperatureCelsius"]["SetPoint"] =
                            std::get<double>(value);
                    }
                    else if (propertyName == "MaxWorkloadFactor" &&
                             std::holds_alternative<double>(value))
                    {
                        staticPowerHint["WorkloadFactor"]["AllowableMax"] =
                            std::get<double>(value);
                    }
                    else if (propertyName == "MinWorkloadFactor" &&
                             std::holds_alternative<double>(value))
                    {
                        staticPowerHint["WorkloadFactor"]["AllowableMin"] =
                            std::get<double>(value);
                    }
                    else if (propertyName == "WorkloadFactor" &&
                             std::holds_alternative<double>(value))
                    {
                        staticPowerHint["WorkloadFactor"]["SetPoint"] =
                            std::get<double>(value);
                    }
                    else if (propertyName == "PowerEstimate" &&
                             std::holds_alternative<double>(value))
                    {
                        staticPowerHint["PowerEstimationWatts"]["Reading"] =
                            std::get<double>(value);
                    }
                    else if (propertyName == "StateOfLastEstimatePower" &&
                             std::holds_alternative<std::string>(value))
                    {
                        staticPowerHint["PowerEstimationWatts"]["State"] =
                            redfish::chassis_utils::
                                getStateOfEstimatePowerMethod(
                                    std::get<std::string>(value));
                    }
                }
            },
                service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
                "com.nvidia.StaticPowerHint");
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
        std::array<const char*, 1>{"com.nvidia.StaticPowerHint"});
}

inline void getStaticPowerHintByChassis(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisObjPath)
{
    // get endpoints of chassisId/all_controls
    crow::connections::systemBus->async_method_call(
        [asyncResp,
         chassisObjPath](const boost::system::error_code,
                         std::variant<std::vector<std::string>>& resp) {
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            return;
        }
        for (const auto& objPath : *data)
        {
            getStaticPowerHintByObjPath(asyncResp, objPath);
        }
    },
        "xyz.openbmc_project.ObjectMapper", chassisObjPath + "/all_controls",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void
    getNetworkAdapters(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& objPath,
                       const std::vector<std::string>& interfaces,
                       const std::string& chassisId)
{
    // NetworkAdapters collection
    const std::string networkInterface =
        "xyz.openbmc_project.Inventory.Item.NetworkInterface";
    if (std::find(interfaces.begin(), interfaces.end(), networkInterface) !=
        interfaces.end())
    {
        // networkInterface at the same chassis objPath
        asyncResp->res.jsonValue["NetworkAdapters"] = {
            {"@odata.id",
             "/redfish/v1/Chassis/" + chassisId + "/NetworkAdapters"}};
        return;
    }

    const std::array<const char*, 1> networkInterfaces = {
        "xyz.openbmc_project.Inventory.Item.NetworkInterface"};
    BMCWEB_LOG_ERROR("test networkInterface: {}", objPath);

    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisId(std::string(chassisId))](
            const boost::system::error_code ec,
            const crow::openbmc_mapper::GetSubTreeType& subtree) {
            if (ec)
            {
                return;
            }

            if (subtree.size() == 0)
            {
                return;
            }
            asyncResp->res.jsonValue["NetworkAdapters"] = {
                {"@odata.id",
                 "/redfish/v1/Chassis/" + chassisId + "/NetworkAdapters"}};
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", objPath, 0,
        networkInterfaces);
}


} // nvidia_chassis_utils
} // namespace redfish