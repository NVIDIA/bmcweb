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

#include "utils/nvidia_time_utils.hpp"
#include "utils/time_utils.hpp"

#include <utils/chassis_utils.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/port_utils.hpp>

// Inline function to check if a key-value pair json object already exists in
// the JSON array
inline bool containsJsonObject(const nlohmann::json& j, const std::string& key,
                               const std::string& value)
{
    nlohmann::json temp = {{key, value}};
    for (const auto& item : j)
    {
        if (temp == item)
        {
            return true;
        }
    }
    return false;
}

inline std::string getPropertySuffix(const std::string& ifaceName,
                                     const std::string& metricName)
{
    std::string suffix;
    // form redfish URI for device/sub device property
    if (ifaceName == "xyz.openbmc_project.Inventory.Decorator.PortInfo")
    {
        if (metricName == "CurrentSpeed")
        {
            suffix = "#/CurrentSpeedGbps";
        }
        else if (metricName == "MaxSpeed")
        {
            suffix = "#/MaxSpeedGbps";
        }
    }
    else if (ifaceName == "xyz.openbmc_project.Inventory.Decorator.PortState")
    {
        if (metricName == "LinkStatus")
        {
            suffix = "#/LinkStatus";
        }
    }
    else if (ifaceName == "xyz.openbmc_project.Metrics.PortMetricsOem1")
    {
        if (metricName == "DataCRCCount")
        {
            suffix = "/Metrics#/Oem/Nvidia/NVLinkErrors/DataCRCCount";
        }
        else if (metricName == "FlitCRCCount")
        {
            suffix = "/Metrics#/Oem/Nvidia/NVLinkErrors/FlitCRCCount";
        }
        else if (metricName == "RecoveryCount")
        {
            suffix = "/Metrics#/Oem/Nvidia/NVLinkErrors/RecoveryCount";
        }
        else if (metricName == "ReplayErrorsCount")
        {
            suffix = "/Metrics#/Oem/Nvidia/NVLinkErrors/ReplayCount";
        }
    }
    else if (ifaceName == "xyz.openbmc_project.Metrics.PortMetricsOem2")
    {
        if (metricName == "RXBytes")
        {
            suffix = "/Metrics#/RXBytes";
        }
        else if (metricName == "TXBytes")
        {
            suffix = "/Metrics#/TXBytes";
        }
    }
    else if (ifaceName == "xyz.openbmc_project.Metrics.PortMetricsOem3")
    {
        if (metricName == "RXNoProtocolBytes")
        {
            suffix = "/Metrics#/Oem/Nvidia/RXNoProtocolBytes";
        }
        else if (metricName == "TXNoProtocolBytes")
        {
            suffix = "/Metrics#/Oem/Nvidia/TXNoProtocolBytes";
        }
        else if (metricName == "RuntimeError")
        {
            suffix = "/Metrics#/Oem/Nvidia/NVLinkErrors/RuntimeError";
        }
        else if (metricName == "TrainingError")
        {
            suffix = "/Metrics#/Oem/Nvidia/NVLinkErrors/TrainingError";
        }
        else if (metricName == "TXWidth")
        {
            suffix = "#/Oem/Nvidia/TXWidth";
        }
        else if (metricName == "RXWidth")
        {
            suffix = "#/Oem/Nvidia/RXWidth";
        }
    }
    else if (ifaceName == "xyz.openbmc_project.State.ProcessorPerformance")
    {
        if (metricName == "ThrottleReason")
        {
            suffix = "/Oem/Nvidia/ThrottleReasons";
        }
        if (metricName == "PowerLimitThrottleDuration")
        {
            suffix = "/PowerLimitThrottleDuration";
        }
        if (metricName == "ThermalLimitThrottleDuration")
        {
            suffix = "/ThermalLimitThrottleDuration";
        }
        if (metricName == "AccumulatedSMUtilizationDuration")
        {
            suffix = "/Oem/Nvidia/AccumulatedSMUtilizationDuration";
        }
        if (metricName == "AccumulatedGPUContextUtilizationDuration")
        {
            suffix = "/Oem/Nvidia/AccumulatedGPUContextUtilizationDuration";
        }
        if (metricName == "GlobalSoftwareViolationThrottleDuration")
        {
            suffix = "/Oem/Nvidia/GlobalSoftwareViolationThrottleDuration";
        }
        if (metricName == "HardwareViolationThrottleDuration")
        {
            suffix = "/Oem/Nvidia/HardwareViolationThrottleDuration";
        }
        if (metricName == "PCIeTXBytes")
        {
            suffix = "/Oem/Nvidia/PCIeTXBytes";
        }
        if (metricName == "PCIeRXBytes")
        {
            suffix = "/Oem/Nvidia/PCIeRXBytes";
        }
    }
    else if (ifaceName == "com.nvidia.NVLink.NVLinkMetrics")
    {
        if (metricName == "NVLinkRawTxBandwidthGbps")
        {
            suffix = "/Oem/Nvidia/NVLinkRawTxBandwidthGbps";
        }
        if (metricName == "NVLinkRawRxBandwidthGbps")
        {
            suffix = "/Oem/Nvidia/NVLinkRawRxBandwidthGbps";
        }
        if (metricName == "NVLinkDataTxBandwidthGbps")
        {
            suffix = "/Oem/Nvidia/NVLinkDataTxBandwidthGbps";
        }
        if (metricName == "NVLinkDataRxBandwidthGbps")
        {
            suffix = "/Oem/Nvidia/NVLinkDataRxBandwidthGbps";
        }
    }
    else if (ifaceName == "com.nvidia.GPMMetrics")
    {
        if (metricName == "NVDecInstanceUtilizationPercent")
        {
            suffix = "/Oem/Nvidia/NVDecInstanceUtilizationPercent";
        }
        if (metricName == "NVJpgInstanceUtilizationPercent")
        {
            suffix = "/Oem/Nvidia/NVJpgInstanceUtilizationPercent";
        }
        if (metricName == "GraphicsEngineActivityPercent")
        {
            suffix = "/Oem/Nvidia/GraphicsEngineActivityPercent";
        }
        if (metricName == "SMActivityPercent")
        {
            suffix = "/Oem/Nvidia/SMActivityPercent";
        }
        if (metricName == "SMOccupancyPercent")
        {
            suffix = "/Oem/Nvidia/SMOccupancyPercent";
        }
        if (metricName == "TensorCoreActivityPercent")
        {
            suffix = "/Oem/Nvidia/TensorCoreActivityPercent";
        }
        if (metricName == "FP64ActivityPercent")
        {
            suffix = "/Oem/Nvidia/FP64ActivityPercent";
        }
        if (metricName == "FP32ActivityPercent")
        {
            suffix = "/Oem/Nvidia/FP32ActivityPercent";
        }
        if (metricName == "FP16ActivityPercent")
        {
            suffix = "/Oem/Nvidia/FP16ActivityPercent";
        }
        if (metricName == "NVDecUtilizationPercent")
        {
            suffix = "/Oem/Nvidia/NVDecUtilizationPercent";
        }
        if (metricName == "NVJpgUtilizationPercent")
        {
            suffix = "/Oem/Nvidia/NVJpgUtilizationPercent";
        }
        if (metricName == "NVOfaUtilizationPercent")
        {
            suffix = "/Oem/Nvidia/NVOfaUtilizationPercent";
        }
        if (metricName == "PCIeRawTxBandwidthGbps")
        {
            suffix = "/Oem/Nvidia/PCIeRawTxBandwidthGbps";
        }
        if (metricName == "PCIeRawRxBandwidthGbps")
        {
            suffix = "/Oem/Nvidia/PCIeRawRxBandwidthGbps";
        }
        if (metricName == "IntegerActivityUtilizationPercent")
        {
            suffix = "/Oem/Nvidia/IntegerActivityUtilizationPercent";
        }
        if (metricName == "DMMAUtilizationPercent")
        {
            suffix = "/Oem/Nvidia/DMMAUtilizationPercent";
        }
        if (metricName == "HMMAUtilizationPercent")
        {
            suffix = "/Oem/Nvidia/HMMAUtilizationPercent";
        }
        if (metricName == "IMMAUtilizationPercent")
        {
            suffix = "/Oem/Nvidia/IMMAUtilizationPercent";
        }
    }
    else if (ifaceName == "xyz.openbmc_project.PCIe.PCIeECC")
    {
        if (metricName == "nonfeCount")
        {
            suffix = "/PCIeErrors/NonFatalErrorCount";
        }
        else if (metricName == "feCount")
        {
            suffix = "/PCIeErrors/FatalErrorCount";
        }
        else if (metricName == "ceCount" || metricName == "PCIeECC.ceCount")
        {
            suffix = "/PCIeErrors/CorrectableErrorCount";
        }
        else if (metricName == "L0ToRecoveryCount")
        {
            suffix = "/PCIeErrors/L0ToRecoveryCount";
        }
        else if (metricName == "NAKReceivedCount")
        {
            suffix = "/PCIeErrors/NAKReceivedCount";
        }
        else if (metricName == "ReplayCount")
        {
            suffix = "/PCIeErrors/ReplayCount";
        }
        else if (metricName == "NAKSentCount")
        {
            suffix = "/PCIeErrors/NAKSentCount";
        }
        else if (metricName == "ReplayRolloverCount")
        {
            suffix = "/PCIeErrors/ReplayRolloverCount";
        }
        else if (metricName == "PCIeType")
        {
            suffix = "#/PCIeInterface/PCIeType";
        }
        else if (metricName == "MaxLanes")
        {
            suffix = "#/PCIeInterface/MaxLanes";
        }
        else if (metricName == "LanesInUse")
        {
            suffix = "#/PCIeInterface/LanesInUse";
        }
    }
    else if (ifaceName == "xyz.openbmc_project.Memory.MemoryECC")
    {
        if (metricName == "ueCount")
        {
            suffix = "/UncorrectableECCErrorCount";
        }
        else if (metricName == "ceCount")
        {
            suffix = "/CorrectableECCErrorCount";
        }
    }
    else if (ifaceName ==
             "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig")
    {
        if (metricName == "Utilization")
        {
            suffix = "/BandwidthPercent";
        }
        else if (metricName == "OperatingSpeed")
        {
            suffix = "/OperatingSpeedMHz";
        }
    }
    else if (ifaceName == "xyz.openbmc_project.Inventory.Item.Dimm")
    {
        if (metricName == "MemoryConfiguredSpeedInMhz")
        {
            suffix = "/OperatingSpeedMHz";
        }
        else if (metricName == "Utilization")
        {
            suffix = "/BandwidthPercent";
        }
    }
    else if (ifaceName == "xyz.openbmc_project.Inventory.Item.PCIeDevice")
    {
        if (metricName == "PCIeType")
        {
            suffix = "#/PCIeInterface/PCIeType";
        }
        else if (metricName == "MaxLanes")
        {
            suffix = "#/PCIeInterface/MaxLanes";
        }
    }
    else if (ifaceName == "com.nvidia.MemoryRowRemapping")
    {
        if (metricName == "ueRowRemappingCount")
        {
            suffix = "/Oem/Nvidia/RowRemapping/UncorrectableRowRemappingCount";
        }
        else if (metricName == "ceRowRemappingCount")
        {
            suffix = "/Oem/Nvidia/RowRemapping/CorrectableRowRemappingCount";
        }
        else if (metricName == "RowRemappingFailureState")
        {
            suffix = "/Oem/Nvidia/RowRemappingFailed";
        }
    }
    else if (ifaceName ==
             "xyz.openbmc_project.State.Decorator.OperationalStatus")
    {
        if (metricName == "State")
        {
            suffix = "#/Status/State";
        }
    }
    else
    {
        suffix.clear();
    }
    return suffix;
}

static std::string generateURI(
    const std::string& deviceType, const std::string& deviceName,
    const std::string& subDeviceName, const std::string& devicePath,
    const std::string& metricName, const std::string& ifaceName)
{
    std::string metricURI;
    std::string propSuffix;
    // form redfish URI for sub device
    if (deviceType == "ProcessorPortMetrics")
    {
        metricURI = "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME);
        metricURI += "/Processors/";
        metricURI += deviceName;
        metricURI += "/Ports/";
        metricURI += subDeviceName;
        propSuffix = getPropertySuffix(ifaceName, metricName);
    }
    else if (deviceType == "ProcessorPortGpmMetrics")
    {
        metricURI = "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME);
        metricURI += "/Processors/";
        metricURI += deviceName;
        metricURI += "/Ports/";
        metricURI += subDeviceName;
        metricURI += "/Metrics#";
        propSuffix = getPropertySuffix(ifaceName, metricName);
    }
    else if (deviceType == "NVSwitchPortMetrics")
    {
        metricURI = "/redfish/v1/Fabrics/";
        metricURI += BMCWEB_PLATFORM_DEVICE_PREFIX;
        metricURI += "NVLinkFabric_0/Switches/";
        metricURI += deviceName;
        metricURI += "/Ports/";
        metricURI += subDeviceName;
        propSuffix = getPropertySuffix(ifaceName, metricName);
    }
    else if (deviceType == "ProcessorMetrics")
    {
        metricURI = "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME);
        metricURI += "/Processors/";
        metricURI += deviceName;
        metricURI += "/ProcessorMetrics#";
        if (ifaceName == "xyz.openbmc_project.Memory.MemoryECC")
        {
            metricURI += "/CacheMetricsTotal/LifeTime";
        }
        else if (ifaceName == "xyz.openbmc_project.PCIe.PCIeECC")
        {
            if (metricName == "PCIeType" || metricName == "MaxLanes" ||
                metricName == "LanesInUse")
            {
                sdbusplus::message::object_path deviceObjectPath(devicePath);
                const std::string childDeviceName = deviceObjectPath.filename();
                std::string parentDeviceName(BMCWEB_PLATFORM_DEVICE_PREFIX);
                parentDeviceName += childDeviceName;
                metricURI = "/redfish/v1/Chassis/";
                metricURI += parentDeviceName;
                metricURI += "/PCIeDevices/";
                metricURI += childDeviceName;
            }
        }
        else if (ifaceName ==
                 "xyz.openbmc_project.State.Decorator.OperationalStatus")
        {
            metricURI = "/redfish/v1/Systems/" +
                        std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME);
            metricURI += "/Processors/";
            metricURI += deviceName;
        }
        propSuffix = getPropertySuffix(ifaceName, metricName);
    }
    else if (deviceType == "ProcessorGpmMetrics")
    {
        metricURI = "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME);
        metricURI += "/Processors/";
        metricURI += deviceName;
        metricURI += "/ProcessorMetrics#";
        propSuffix = getPropertySuffix(ifaceName, metricName);
    }
    else if (deviceType == "NVSwitchMetrics")
    {
        metricURI = "/redfish/v1/Fabrics/";
        metricURI += BMCWEB_PLATFORM_DEVICE_PREFIX;
        metricURI += "NVLinkFabric_0/Switches/";
        metricURI += deviceName;
        metricURI += "/SwitchMetrics#";
        if (ifaceName == "xyz.openbmc_project.Memory.MemoryECC")
        {
            metricURI += "/InternalMemoryMetrics/LifeTime";
        }
        propSuffix = getPropertySuffix(ifaceName, metricName);
    }
    else if (deviceType == "MemoryMetrics")
    {
        metricURI = "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME);
        metricURI += "/Memory/";
        metricURI += deviceName;
        if (ifaceName == "com.nvidia.MemoryRowRemapping")
        {
            if (metricName == "RowRemappingFailureState" ||
                metricName == "RowRemappingPendingState")
            {
                metricURI += "#";
            }
            else
            {
                metricURI += "/MemoryMetrics#";
            }
        }
        else if (ifaceName == "xyz.openbmc_project.Memory.MemoryECC")
        {
            metricURI += "/MemoryMetrics#/LifeTime";
        }
        else
        {
            metricURI += "/MemoryMetrics#";
        }
        propSuffix = getPropertySuffix(ifaceName, metricName);
    }
    else
    {
        metricURI.clear();
    }

    // Check if property Suffix is empty then property doesn;t exist
    if (!propSuffix.empty())
    {
        metricURI += propSuffix;
    }
    else
    {
        metricURI.clear();
    }
    return metricURI;
}

inline std::string toPCIeType(const std::string& pcieType)
{
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeDevice.PCIeTypes.Gen1")
    {
        return "Gen1";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeDevice.PCIeTypes.Gen2")
    {
        return "Gen2";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeDevice.PCIeTypes.Gen3")
    {
        return "Gen3";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeDevice.PCIeTypes.Gen4")
    {
        return "Gen4";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeDevice.PCIeTypes.Gen5")
    {
        return "Gen5";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeDevice.PCIeTypes.Gen6")
    {
        return "Gen6";
    }
    // Unknown or others
    return "Unknown";
}

inline std::string translateReading(const std::string& ifaceName,
                                    const std::string& metricName,
                                    const std::string& reading)
{
    std::string metricValue;
    if (ifaceName == "xyz.openbmc_project.State.ProcessorPerformance")
    {
        if (metricName == "ThrottleReason")
        {
            metricValue = redfish::dbus_utils::toReasonType(reading);
        }
    }
    else if (ifaceName == "xyz.openbmc_project.PCIe.PCIeECC")
    {
        if (metricName == "PCIeType")
        {
            metricValue = toPCIeType(reading);
        }
    }
    else if (ifaceName == "xyz.openbmc_project.Inventory.Decorator.PortState")
    {
        if (metricName == "LinkStatus")
        {
            metricValue = redfish::port_utils::getLinkStatusType(reading);
        }
    }
    else if (ifaceName ==
             "xyz.openbmc_project.State.Decorator.OperationalStatus")
    {
        if (metricName == "State")
        {
            metricValue = redfish::chassis_utils::getPowerStateType(reading);
        }
    }
    else
    {
        metricValue = reading;
    }
    return metricValue;
}

inline std::string translateThrottleDuration(const std::string& metricName,
                                             const uint64_t& reading)
{
    std::string metricValue;
    if ((metricName == "PowerLimitThrottleDuration") ||
        (metricName == "ThermalLimitThrottleDuration") ||
        (metricName == "HardwareViolationThrottleDuration") ||
        (metricName == "GlobalSoftwareViolationThrottleDuration"))
    {
        std::optional<std::string> duration =
            redfish::time_utils::toDurationStringFromNano(reading);

        if (duration)
        {
            metricValue = *duration;
        }
    }
    else
    {
        metricValue = std::to_string(reading);
    }
    return metricValue;
}

inline std::string translateAccumlatedDuration(const uint64_t& reading)
{
    std::string metricValue;
    std::optional<std::string> duration =
        redfish::time_utils::toDurationStringFromUint(reading);
    if (duration)
    {
        metricValue = *duration;
    }

    return metricValue;
}

inline void getMetricValue(
    const std::string& deviceType, const std::string& deviceName,
    const std::string& subDeviceName, const std::string& devicePath,
    const std::string& metricName, const std::string& ifaceName,
    const dbus::utility::DbusVariantType& value, const uint64_t& t,
    nlohmann::json& resArray)
{
    nlohmann::json thisMetric = nlohmann::json::object();
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
        t;

    if (const std::vector<std::string>* stringReadingArray =
            std::get_if<std::vector<std::string>>(&value))
    {
        // This is for the property whose value is of type list and each element
        // in the list on the redfish is represented with
        // "PropertyName/<index_of_list_element>". and it always starts with 0
        // Eg:- ThrottleReasosns: [Idle, AppClock]-> "Idle" maps to
        // ThrottleReasons/0
        int i = 0;
        for (const std::string& reading : *stringReadingArray)
        {
            std::string val = translateReading(ifaceName, metricName, reading);
            thisMetric["MetricValue"] = val;
            std::string metricProp =
                generateURI(deviceType, deviceName, subDeviceName, devicePath,
                            metricName, ifaceName);
            metricProp += "/";
            metricProp += std::to_string(i);
            thisMetric["MetricProperty"] = metricProp;
            thisMetric["Timestamp"] = redfish::time_utils::getDateTimeUintMs(
                sensorUpdatetimeSystemClock);
            resArray.push_back(thisMetric);
            i++;
        }
    }
    else if (const std::vector<double>* doubleReadingArray =
                 std::get_if<std::vector<double>>(&value))
    {
        // This is for the property whose value is of type list and each element
        // in the list on the redfish is represented with
        // "PropertyName/<index_of_list_element>". and it always starts with 0
        int i = 0;
        for (const double& reading : *doubleReadingArray)
        {
            // double val = translateReading(ifaceName, metricName, reading);
            thisMetric["MetricValue"] = std::to_string(reading);
            std::string metricProp =
                generateURI(deviceType, deviceName, subDeviceName, devicePath,
                            metricName, ifaceName);
            metricProp += "/";
            metricProp += std::to_string(i);
            thisMetric["MetricProperty"] = metricProp;
            thisMetric["Timestamp"] = redfish::time_utils::getDateTimeUintMs(
                sensorUpdatetimeSystemClock);
            resArray.push_back(thisMetric);
            i++;
        }
    }
    else
    {
        const std::string metricURI =
            generateURI(deviceType, deviceName, subDeviceName, devicePath,
                        metricName, ifaceName);
        if (metricURI.empty())
        {
            return;
        }
        thisMetric["MetricProperty"] = metricURI;
        thisMetric["Timestamp"] =
            redfish::time_utils::getDateTimeUintMs(sensorUpdatetimeSystemClock);
        if (const std::string* strReading = std::get_if<std::string>(&value))
        {
            std::string val =
                translateReading(ifaceName, metricName, *strReading);
            thisMetric["MetricValue"] = val;
        }
        else if (const int* intReading = std::get_if<int>(&value))
        {
            thisMetric["MetricValue"] = std::to_string(*intReading);
        }
        else if (const int16_t* int16Reading = std::get_if<int16_t>(&value))
        {
            thisMetric["MetricValue"] = std::to_string(*int16Reading);
        }
        else if (const int64_t* int64Reading = std::get_if<int64_t>(&value))
        {
            thisMetric["MetricValue"] = std::to_string(*int64Reading);
        }
        else if (const uint16_t* uint16Reading = std::get_if<uint16_t>(&value))
        {
            thisMetric["MetricValue"] = std::to_string(*uint16Reading);
        }
        else if (const uint32_t* uint32Reading = std::get_if<uint32_t>(&value))
        {
            thisMetric["MetricValue"] = std::to_string(*uint32Reading);
        }
        else if (const uint64_t* uint64Reading = std::get_if<uint64_t>(&value))
        {
            if ((ifaceName ==
                 "xyz.openbmc_project.State.ProcessorPerformance") &&
                ((metricName == "AccumulatedSMUtilizationDuration") ||
                 (metricName == "AccumulatedGPUContextUtilizationDuration")))
            {
                std::string val = translateAccumlatedDuration(*uint64Reading);
                thisMetric["MetricValue"] = val;
            }
            else
            {
                std::string val =
                    translateThrottleDuration(metricName, *uint64Reading);
                thisMetric["MetricValue"] = val;
            }
        }
        else if (const double* doubleReading = std::get_if<double>(&value))
        {
            thisMetric["MetricValue"] = std::to_string(*doubleReading);
        }
        else if (const bool* boolReading = std::get_if<bool>(&value))
        {
            thisMetric["MetricValue"] = "false";
            if (*boolReading)
            {
                thisMetric["MetricValue"] = "true";
            }
        }
        resArray.push_back(thisMetric);
    }
}

inline std::string getKeyNameonTimeStampIface(const std::string& ifaceName)
{
    size_t pos = ifaceName.find_last_of('.');
    if (pos == std::string::npos)
    {
        pos = 0;
    }
    else
    {
        pos++;
    }
    // "Port"
    std::string iface = ifaceName.substr(pos);
    return iface;
}

namespace redfish
{
namespace telemetry
{

constexpr const char* metricReportDefinitionUriStr =
    "/redfish/v1/TelemetryService/MetricReportDefinitions";
constexpr const char* metricReportUri =
    "/redfish/v1/TelemetryService/MetricReports";

inline void addMetricReportMembers(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::async_method_call(
        [asyncResp](boost::system::error_code& ec,
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
                if (object.ends_with("platformmetrics"))
                {
                    std::string uripath = metricReportUriPath;
                    uripath += BMCWEB_PLATFORM_METRICS_ID;
                    if (!containsJsonObject(addMembers, "@odata.id", uripath))
                    {
                        addMembers.push_back({{"@odata.id", uripath}});
                    }
                }
                else if (object.ends_with("memory"))
                {
                    std::string memoryMetricId = std::format(
                        "{}MemoryMetrics", BMCWEB_PLATFORM_DEVICE_PREFIX);
                    memoryMetricId += "_0";
                    std::string uripath = metricReportUriPath + memoryMetricId;
                    addMembers.push_back({{"@odata.id", uripath}});
                }
                else if (object.ends_with("processors"))
                {
                    std::string processorMetricId =
                        std::string(BMCWEB_PLATFORM_DEVICE_PREFIX) +
                        +"ProcessorMetrics";
                    processorMetricId += "_0";
                    std::string uripath =
                        metricReportUriPath + processorMetricId;
                    addMembers.push_back({{"@odata.id", uripath}});

                    std::string processorGpmMetricId =
                        std::string(BMCWEB_PLATFORM_DEVICE_PREFIX) +
                        +"ProcessorGPMMetrics";
                    processorGpmMetricId += "_0";
                    std::string uripathGpm =
                        metricReportUriPath + processorGpmMetricId;
                    addMembers.push_back({{"@odata.id", uripathGpm}});

                    std::string processorPortMetricId =
                        std::string(BMCWEB_PLATFORM_DEVICE_PREFIX) +
                        +"ProcessorPortMetrics";
                    processorPortMetricId += "_0";
                    uripath = metricReportUriPath + processorPortMetricId;
                    addMembers.push_back({{"@odata.id", uripath}});

                    std::string processorPortGpmMetricId =
                        std::string(BMCWEB_PLATFORM_DEVICE_PREFIX) +
                        +"ProcessorPortGPMMetrics";
                    processorPortGpmMetricId += "_0";
                    uripath = metricReportUriPath + processorPortGpmMetricId;
                    addMembers.push_back({{"@odata.id", uripath}});
                }
                else if (object.ends_with("Switches"))
                {
                    std::string switchMetricId =
                        std::string(BMCWEB_PLATFORM_DEVICE_PREFIX) +
                        +"NVSwitchMetrics";
                    switchMetricId += "_0";
                    std::string uripath = metricReportUriPath + switchMetricId;
                    addMembers.push_back({{"@odata.id", uripath}});

                    std::string switchPortMetricId =
                        std::string(BMCWEB_PLATFORM_DEVICE_PREFIX) +
                        +"NVSwitchPortMetrics";
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

inline void getSensorMap(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& serviceName, const std::string& objectPath,
    const uint32_t& staleSensorUpperLimit, const uint64_t& requestTimestamp)
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
         requestTimestamp](const boost::system::error_code& ec,
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
                thisMetric["Timestamp"] =
                    redfish::time_utils::getDateTimeUintMs(
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
        BMCWEB_PLATFORM_METRICS_SENSING_INTERVAL;
    asyncResp->res.jsonValue["MetricValues"] = nlohmann::json::array();
    sdbusplus::asio::getProperty<uint32_t>(
        *crow::connections::systemBus, serviceName, objectPath,
        "xyz.openbmc_project.Sensor.Aggregation",
        "BMCWEB_STALESENSOR_UPPER_LIMIT_MILISECOND",
        [asyncResp, objectPath, serviceName,
         requestTimestamp](const boost::system::error_code& ec,
                           const uint32_t& staleSensorUpperLimit) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(asyncResp->res);
                return;
            }
            getSensorMap(asyncResp, serviceName, objectPath,
                         staleSensorUpperLimit, requestTimestamp);
        });
}

inline void getPlatformMetrics(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const uint64_t& requestTimestamp = 0)
{
    const std::array<const char*, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Chassis"};
    auto respHandler = [asyncResp, requestTimestamp, chassisId](
                           const boost::system::error_code& ec,
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
            asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
                "/redfish/v1/TelemetryService/MetricReports/{}",
                BMCWEB_PLATFORM_METRICS_ID);
            asyncResp->res.jsonValue["Id"] = BMCWEB_PLATFORM_METRICS_ID;
            asyncResp->res.jsonValue["Name"] = BMCWEB_PLATFORM_METRICS_ID;
            asyncResp->res.jsonValue["MetricReportDefinition"]["@odata.id"] =
                std::format("{}/{}", telemetry::metricReportDefinitionUriStr,
                            BMCWEB_PLATFORM_METRICS_ID);
            asyncResp->res.jsonValue["MetricValues"] = nlohmann::json::array();
            // Identify sensor services for sensor readings
            redfish::nvidia_thermal_metrics_utils::processSensorServices(
                asyncResp, chassisPath, "all",
                BMCWEB_PLATFORM_METRICS_SENSING_INTERVAL, requestTimestamp);
            return;
        }
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
    };
    // Get the Chassis Collection
    dbus::utility::async_method_call(
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
            std::string subDeviceName;
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

// This function populate the metric report for sub devices. Eg All nvlinks
// of all processors or switches
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

    std::string memoryMetrics =
        std::string(BMCWEB_PLATFORM_DEVICE_PREFIX) + +"MemoryMetrics";
    memoryMetrics += "_0";

    std::string processorMetrics =
        std::string(BMCWEB_PLATFORM_DEVICE_PREFIX) + +"ProcessorMetrics";
    processorMetrics += "_0";

    std::string processorGpmMetrics =
        std::string(BMCWEB_PLATFORM_DEVICE_PREFIX) + +"ProcessorGPMMetrics";
    processorGpmMetrics += "_0";

    std::string processorPortMetrics =
        std::string(BMCWEB_PLATFORM_DEVICE_PREFIX) + +"ProcessorPortMetrics";
    processorPortMetrics += "_0";

    std::string processorPortGpmMetrics =
        std::string(BMCWEB_PLATFORM_DEVICE_PREFIX) + +"ProcessorPortGPMMetrics";
    processorPortGpmMetrics += "_0";

    std::string nvswitchMetrics =
        std::string(BMCWEB_PLATFORM_DEVICE_PREFIX) + +"NVSwitchMetrics";
    nvswitchMetrics += "_0";

    std::string nvswitchPortMetrics =
        std::string(BMCWEB_PLATFORM_DEVICE_PREFIX) + +"NVSwitchPortMetrics";
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
    dbus::utility::async_method_call(
        [asyncResp,
         deviceType](const boost::system::error_code& ec,
                     const dbus::utility::ManagedObjectType& objects) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            if (deviceType == "MemoryMetrics" ||
                deviceType == "NVSwitchMetrics" ||
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
                        getAggregatedSubDeviceMetrics(
                            asyncResp, deviceType, deviceName, subDeviceName,
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

inline bool isMetricIdSupported(
    const std::string& requestedMetricId,
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

inline void getPlatforMetrics(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& metricId, const uint64_t& requestTimestamp = 0)
{
    using MapperServiceMapType =
        std::vector<std::pair<std::string, std::vector<std::string>>>;

    // Map of object paths to MapperServiceMaps
    using MapperGetSubTreeResponse =
        std::vector<std::pair<std::string, MapperServiceMapType>>;
    dbus::utility::async_method_call(
        [asyncResp, metricId,
         requestTimestamp](boost::system::error_code& ec,
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
                    if (metricId == BMCWEB_PLATFORM_METRICS_ID)
                    {
                        if (metricfname == "platformmetrics")
                        {
                            supportedMetricIds.emplace_back(
                                BMCWEB_PLATFORM_METRICS_ID);
                            getPlatforMetricsFromSensorMap(
                                asyncResp, objectPath, serviceName, metricId,
                                requestTimestamp);
                        }
                    }
                    else if (metricfname == "memory" ||
                             metricfname == "processors" ||
                             metricfname == "Switches")
                    {
                        getManagedObjectForMetrics(
                            asyncResp, objectPath, serviceName, metricId,
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

} // namespace telemetry
} // namespace redfish
