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

#ifdef NVIDIA_HAVE_TAL
#include "tal.hpp"
#endif

#include <nlohmann/json.hpp>

#include <unordered_set>

namespace redfish
{
namespace shmem
{

inline void
    getShmemPlatformMetrics(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& metricId,
                            const uint64_t& requestTimestamp = 0)
{
    BMCWEB_LOG_DEBUG("getShmemPlatformMetrics :{} Requested at : {}", metricId,
                     requestTimestamp);
    try
    {
#ifndef NVIDIA_HAVE_TAL
        BMCWEB_LOG_CRITICAL("Attempt to access tal but not available");
        return;
#else
        const auto& values = tal::TelemetryAggregator::getAllMrds(metricId);

        asyncResp->res.jsonValue["@odata.type"] =
            "#MetricReport.v1_4_2.MetricReport";
        std::string metricUri = "/redfish/v1/TelemetryService/MetricReports/";
        metricUri += metricId;
        asyncResp->res.jsonValue["@odata.id"] = metricUri;
        asyncResp->res.jsonValue["Id"] = metricId;
        asyncResp->res.jsonValue["Name"] = metricId;
        std::string metricDefinitionUri =
            "/redfish/v1/TelemetryService/MetricReportDefinitions";
        metricDefinitionUri += "/";
        metricDefinitionUri += metricId;
        asyncResp->res.jsonValue["MetricReportDefinition"]["@odata.id"] =
            metricDefinitionUri;
        nlohmann::json& resArray = asyncResp->res.jsonValue["MetricValues"];
        nlohmann::json thisMetric = nlohmann::json::object();

        if (metricId == BMCWEB_PLATFORM_METRICS_ID)
        {
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaMetricReport.v1_0_0.NvidiaMetricReport";
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["SensingIntervalMilliseconds"] =
                BMCWEB_PLATFORM_METRICS_SENSING_INTERVAL;
            for (const auto& e : values)
            {
                thisMetric["MetricValue"] = e.sensorValue;
                thisMetric["Timestamp"] = e.timestampStr;
                thisMetric["MetricProperty"] = e.metricProperty;
                thisMetric["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaMetricReport.v1_0_0.NvidiaMetricReport";
                thisMetric["Oem"]["Nvidia"]["MetricValueStale"] = true;
                if (requestTimestamp != 0 && thisMetric["MetricValue"] != "nan")
                {
                    int64_t freshness =
                        static_cast<int64_t>(requestTimestamp - e.timestamp);
                    if (freshness <= BMCWEB_STALESENSOR_UPPER_LIMIT_MILISECOND)
                    {
                        thisMetric["Oem"]["Nvidia"]["MetricValueStale"] = false;
                    }
                    // enable this line for sensor age calculation
                    // thisMetric["Oem"]["Nvidia"]["FreshnessInms"] = freshness;
                }
                resArray.push_back(thisMetric);
            }
        }
        else
        {
            for (const auto& e : values)
            {
                thisMetric["MetricValue"] = e.sensorValue;
                thisMetric["Timestamp"] = e.timestampStr;
                thisMetric["MetricProperty"] = e.metricProperty;
                resArray.push_back(thisMetric);
            }
        }

#endif
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("Exception while getting MRD values: {}", e.what());
        messages::resourceNotFound(asyncResp->res, "MetricReport", metricId);
    }
}

constexpr const char* metricReportDefinitionUri =
    "/redfish/v1/TelemetryService/MetricReportDefinitions";

constexpr const char* metricReportUri =
    "/redfish/v1/TelemetryService/MetricReports";

static std::string gpuPrefix(BMCWEB_PLATFORM_GPU_NAME_PREFIX);
static std::string platformDevicePrefix(BMCWEB_PLATFORM_DEVICE_PREFIX);
static std::string platformChassisName(BMCWEB_PLATFORM_CHASSIS_NAME);
static std::string chassisName = platformDevicePrefix + "Chassis_";
static std::string fpgaChassiName = platformDevicePrefix + "FPGA_";
static std::string gpuName = platformDevicePrefix + gpuPrefix;
static std::string nvSwitch = "NVSwitch_";
static std::string pcieRetimer = platformDevicePrefix + "PCIeRetimer_";
static std::string pcieSwtich = platformDevicePrefix + "PCIeSwitch_";
static std::string processorModule = platformDevicePrefix + "ProcessorModule_";
static std::string cpu = platformDevicePrefix + "CPU_";
static std::string nvLink = "NVLink_";

static std::string cpuProcessor = "CPU_";
static std::string processor = "ProcessorModule_";
static std::string pcieLink = "PCIeLink_";
static std::string cpuCore = "CoreUtil_";
static std::string networkAdapter(BMCWEB_NVIDIA_NETWORK_ADAPTER_PREFIX);
static std::string
    networkAdapterLink(BMCWEB_NVIDIA_NETWORK_ADAPTER_LINK_PREFIX);

static std::string gpmInstances = "UtilizationPercent/";
static std::string nvLinkManagementNIC = "NIC_";
static std::string nvLinkManagementNICPort = "Port_";
static std::string retimer = "PCIeRetimer_";

inline std::string getSwitchId(const std::string& key)
{
    // Use regular expressions to extract the numeric part after "NVSwitch_"
    std::regex pattern("NVSwitch_(\\d+)");
    std::smatch match;
    std::string swithId = "";
    if (std::regex_search(key, match, pattern))
    {
        swithId = match[1].str();
    }
    return swithId;
}

inline void replaceNumber(const std::string& input, const std::string& key,
                          const std::string& value,
                          std::set<std::string>& replacedName)
{
    std::regex pattern(key + "(\\d+)");
    std::smatch match;
    std::string res = input;
    if (value == "{BSWild}")
    {
        if (std::regex_search(res, match, pattern))
        {
            size_t lastSlashPos = input.find_last_of('/');
            if (lastSlashPos != std::string::npos)
            {
                replacedName.insert(input.substr(lastSlashPos + 1));
            }
        }
    }
    else
    {
        if (std::regex_search(res, match, pattern))
        {
            std::string number = match[1].str();
            replacedName.insert(number);
        }
    }
    return;
}

inline void metricsReplacementsNonPlatformMetrics(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::vector<std::string> inputMetricProperties,
    const std::string& deviceType)
{
    std::smatch match;
    std::set<int> nvSwitchId_Type_1;
    std::set<int> nvlinkId_Type_1;
    std::set<int> nvSwitchId_Type_2;
    std::set<int> nvlinkId_Type_2;
    std::set<int> gpuId;
    std::set<int> gpmInstance;
    std::set<int> networkAdapterNId;
    std::set<int> nvLinkManagementId;
    std::set<int> retimerId;
    std::set<std::string> portTypes;
    std::set<int> portIds;
    std::set<int> cpuId;
    std::set<int> processorId;
    std::set<int> coreId;
    std::set<int> nvLinkId;
    std::set<int> pcieLinkId;
    nlohmann::json& wildCards = asyncResp->res.jsonValue["Wildcards"];
    for (const auto& e : inputMetricProperties)
    {
        if (deviceType == "NVSwitchPortMetrics")
        {
            std::string switchType = getSwitchId(e);
            if ((switchType == "0" || switchType == "3"))
            {
                std::regex switchPattern(nvSwitch + "(\\d+)");
                if (std::regex_search(e, match, switchPattern))
                {
                    int number = std::stoi(match[1].str());
                    nvSwitchId_Type_1.insert(number);
                }
                std::regex nvLinkPattern(nvLink + "(\\d+)");
                if (std::regex_search(e, match, nvLinkPattern))
                {
                    int number = std::stoi(match[1].str());
                    nvlinkId_Type_1.insert(number);
                }
            }
            else if ((switchType == "1" || switchType == "2"))
            {
                std::regex switchPattern(nvSwitch + "(\\d+)");
                if (std::regex_search(e, match, switchPattern))
                {
                    int number = std::stoi(match[1].str());
                    nvSwitchId_Type_2.insert(number);
                }
                std::regex nvLinkPattern(nvLink + "(\\d+)");
                if (std::regex_search(e, match, nvLinkPattern))
                {
                    int number = std::stoi(match[1].str());
                    nvlinkId_Type_2.insert(number);
                }
            }
        }
        if (deviceType == "NVSwitchMetrics")
        {
            std::regex switchPattern(nvSwitch + "(\\d+)");
            if (std::regex_search(e, match, switchPattern))
            {
                int number = std::stoi(match[1].str());
                nvSwitchId_Type_1.insert(number);
            }
        }
        if (deviceType == "PCIeRetimerMetrics")
        {
            std::regex retimerPattern(retimer + "(\\d+)");
            if (std::regex_search(e, match, retimerPattern))
            {
                int number = std::stoi(match[1].str());
                retimerId.insert(number);
            }
        }
        if (deviceType == "MemoryMetrics" || deviceType == "ProcessorMetrics" ||
            deviceType == "ProcessorGPMMetrics" ||
            deviceType == "ProcessorPortMetrics" ||
            deviceType == "ProcessorPortGPMMetrics")
        {
            std::regex gpuPattern(gpuPrefix + "(\\d+)");
            if (std::regex_search(e, match, gpuPattern))
            {
                int number = std::stoi(match[1].str());
                gpuId.insert(number);
            }
        }
        if (deviceType == "ProcessorGPMMetrics")
        {
            std::regex gpmInstancePattern(gpmInstances + "(\\d+)");
            if (std::regex_search(e, match, gpmInstancePattern))
            {
                int number = std::stoi(match[1].str());
                gpmInstance.insert(number);
            }
        }
        if (deviceType == "ProcessorPortMetrics" ||
            deviceType == "ProcessorPortGPMMetrics")
        {
            std::regex nvLinkPattern(nvLink + "(\\d+)");
            if (std::regex_search(e, match, nvLinkPattern))
            {
                int number = std::stoi(match[1].str());
                nvlinkId_Type_1.insert(number);
            }
        }
        if (deviceType == "NetworkAdapterPortMetrics")
        {
            std::regex networkAdapterPattern(networkAdapter + "(\\d+)");
            if (std::regex_search(e, match, networkAdapterPattern))
            {
                int number = std::stoi(match[1].str());
                networkAdapterNId.insert(number);
            }
            std::regex nvLinkManagementPattern(networkAdapterLink + "(\\d+)");
            if (std::regex_search(e, match, nvLinkManagementPattern))
            {
                int number = std::stoi(match[1].str());
                nvLinkManagementId.insert(number);
            }
        }
        if (deviceType == "PCIeRetimerPortMetrics")
        {
            std::regex pcieRetimerPattern(retimer + "(\\d+)");
            if (std::regex_search(e, match, pcieRetimerPattern))
            {
                int number = std::stoi(match[1].str());
                retimerId.insert(number);
            }
            std::regex retimerPortPattern("/Ports/(\\w+)_(\\d+)");
            if (std::regex_search(e, match, retimerPortPattern) &&
                match.size() > 2)
            {
                std::string portType = match[1].str();
                int portId = std::stoi(match[2].str());

                portTypes.insert(portType);
                portIds.insert(portId);
            }
        }
        if (deviceType == "CpuProcessorMetrics")
        {
            std::regex cpuProcessorPattern(cpuProcessor + "(\\d+)");
            if (std::regex_search(e, match, cpuProcessorPattern))
            {
                int number = std::stoi(match[1].str());
                cpuId.insert(number);
            }
            std::regex processorPattern(processor + "(\\d+)");
            if (std::regex_search(e, match, processorPattern))
            {
                int number = std::stoi(match[1].str());
                processorId.insert(number);
            }
            std::regex cpuCorePattern(cpuCore + "(\\d+)");
            if (std::regex_search(e, match, cpuCorePattern))
            {
                int number = std::stoi(match[1].str());
                coreId.insert(number);
            }
            std::regex nvLinkPattern(nvLink + "(\\d+)");
            if (std::regex_search(e, match, nvLinkPattern))
            {
                int number = std::stoi(match[1].str());
                nvLinkId.insert(number);
            }
            std::regex pcieLinkPattern(pcieLink + "(\\d+)");
            if (std::regex_search(e, match, pcieLinkPattern))
            {
                int number = std::stoi(match[1].str());
                pcieLinkId.insert(number);
            }
        }
        if (deviceType == "HealthMetrics")
        {
            std::regex gpuPattern(gpuPrefix + "(\\d+)");
            if (std::regex_search(e, match, gpuPattern))
            {
                int number = std::stoi(match[1].str());
                gpuId.insert(number);
            }

            std::regex pcieRetimerPattern(retimer + "(\\d+)");
            if (std::regex_search(e, match, pcieRetimerPattern))
            {
                int number = std::stoi(match[1].str());
                retimerId.insert(number);
            }

            std::regex switchPattern(nvSwitch + "(\\d+)");
            if (std::regex_search(e, match, switchPattern))
            {
                int number = std::stoi(match[1].str());
                nvSwitchId_Type_1.insert(number);
            }
        }
    }
    if (deviceType == "NVSwitchPortMetrics")
    {
        nlohmann::json devCountSwitchType_1 = nlohmann::json::array();
        for (const auto& e : nvSwitchId_Type_1)
        {
            devCountSwitchType_1.push_back(std::to_string(e));
        }
        wildCards.push_back({
            {"Name", "NVSwitchId_Type_1"},
            {"Values", devCountSwitchType_1},
        });
        nlohmann::json devCountNVlinkId_Type_1 = nlohmann::json::array();
        for (const auto& e : nvlinkId_Type_1)
        {
            devCountNVlinkId_Type_1.push_back(std::to_string(e));
        }
        wildCards.push_back({
            {"Name", "NvlinkId_Type_1"},
            {"Values", devCountNVlinkId_Type_1},
        });

        nlohmann::json devCountSwitchType_2 = nlohmann::json::array();
        for (const auto& e : nvSwitchId_Type_2)
        {
            devCountSwitchType_2.push_back(std::to_string(e));
        }
        wildCards.push_back({
            {"Name", "NVSwitchId_Type_2"},
            {"Values", devCountSwitchType_2},
        });
        nlohmann::json devCountNVlinkId_Type_2 = nlohmann::json::array();
        for (const auto& e : nvlinkId_Type_2)
        {
            devCountNVlinkId_Type_2.push_back(std::to_string(e));
        }
        wildCards.push_back({
            {"Name", "NvlinkId_Type_2"},
            {"Values", devCountNVlinkId_Type_2},
        });
    }
    if (deviceType == "NetworkAdapterPortMetrics")
    {
        nlohmann::json devCountNetworkAdapter = nlohmann::json::array();
        for (const auto& e : networkAdapterNId)
        {
            devCountNetworkAdapter.push_back(std::to_string(e));
        }
        wildCards.push_back({
            {"Name", "NId"},
            {"Values", devCountNetworkAdapter},
        });

        nlohmann::json devCountNVLinkManagementId = nlohmann::json::array();
        for (const auto& e : nvLinkManagementId)
        {
            devCountNVLinkManagementId.push_back(std::to_string(e));
        }
        wildCards.push_back({
            {"Name", "NvlinkId"},
            {"Values", devCountNVLinkManagementId},
        });
    }
    if (deviceType == "PCIeRetimerPortMetrics")
    {
        nlohmann::json devCountRetimerId = nlohmann::json::array();
        for (const auto& e : retimerId)
        {
            devCountRetimerId.push_back(std::to_string(e));
        }
        wildCards.push_back({
            {"Name", "RetimerId"},
            {"Values", devCountRetimerId},
        });

        nlohmann::json devCountRetimerPortType = nlohmann::json::array();
        for (const auto& e : portTypes)
        {
            devCountRetimerPortType.push_back(e);
        }
        wildCards.push_back({
            {"Name", "PortType"},
            {"Values", devCountRetimerPortType},
        });

        nlohmann::json devCountRetimerPortId = nlohmann::json::array();
        for (const auto& e : portIds)
        {
            devCountRetimerPortId.push_back(std::to_string(e));
        }
        wildCards.push_back({
            {"Name", "PortId"},
            {"Values", devCountRetimerPortId},
        });
    }
    if (deviceType == "PCIeRetimerMetrics" || deviceType == "HealthMetrics")
    {
        nlohmann::json devCountRetimerId = nlohmann::json::array();
        for (const auto& e : retimerId)
        {
            devCountRetimerId.push_back(std::to_string(e));
        }
        wildCards.push_back({
            {"Name", "RetimerId"},
            {"Values", devCountRetimerId},
        });
    }
    if (deviceType == "NVSwitchMetrics" || deviceType == "HealthMetrics")
    {
        nlohmann::json devCountNVSwitchId = nlohmann::json::array();
        for (const auto& e : nvSwitchId_Type_1)
        {
            devCountNVSwitchId.push_back(std::to_string(e));
        }
        wildCards.push_back({
            {"Name", "NVSwitchId"},
            {"Values", devCountNVSwitchId},
        });
    }
    if (deviceType == "MemoryMetrics" || deviceType == "ProcessorMetrics" ||
        deviceType == "ProcessorGPMMetrics" ||
        deviceType == "ProcessorPortMetrics" ||
        deviceType == "ProcessorPortGPMMetrics" ||
        deviceType == "HealthMetrics")
    {
        nlohmann::json devCountGpuId = nlohmann::json::array();
        for (const auto& e : gpuId)
        {
            devCountGpuId.push_back(std::to_string(e));
        }
        wildCards.push_back({
            {"Name", "GpuId"},
            {"Values", devCountGpuId},
        });
    }
    if (deviceType == "ProcessorGPMMetrics")
    {
        nlohmann::json devCountInstanceId = nlohmann::json::array();
        for (const auto& e : gpmInstance)
        {
            devCountInstanceId.push_back(std::to_string(e));
        }
        wildCards.push_back({
            {"Name", "InstanceId"},
            {"Values", devCountInstanceId},
        });
    }
    if (deviceType == "ProcessorPortMetrics" ||
        deviceType == "ProcessorPortGPMMetrics")
    {
        nlohmann::json devCountnvlinkId = nlohmann::json::array();
        for (const auto& e : nvlinkId_Type_1)
        {
            devCountnvlinkId.push_back(std::to_string(e));
        }
        wildCards.push_back({
            {"Name", "NvlinkId"},
            {"Values", devCountnvlinkId},
        });
    }
    if (deviceType == "CpuProcessorMetrics")
    {
        nlohmann::json devCountCpuId = nlohmann::json::array();
        for (const auto& e : cpuId)
        {
            devCountCpuId.push_back(std::to_string(e));
        }
        wildCards.push_back({
            {"Name", "CpuId"},
            {"Values", devCountCpuId},
        });
        nlohmann::json devCountProcessorId = nlohmann::json::array();
        for (const auto& e : processorId)
        {
            devCountProcessorId.push_back(std::to_string(e));
        }
        wildCards.push_back({
            {"Name", "ProcessorId"},
            {"Values", devCountProcessorId},
        });
        nlohmann::json devCountCoreId = nlohmann::json::array();
        for (const auto& e : coreId)
        {
            devCountCoreId.push_back(std::to_string(e));
        }
        wildCards.push_back({
            {"Name", "CoreId"},
            {"Values", devCountCoreId},
        });
        nlohmann::json devCountNvlinkId = nlohmann::json::array();
        for (const auto& e : nvLinkId)
        {
            devCountNvlinkId.push_back(std::to_string(e));
        }
        wildCards.push_back({
            {"Name", "NvlinkId"},
            {"Values", devCountNvlinkId},
        });
        nlohmann::json devCountPcieLinkId = nlohmann::json::array();
        for (const auto& e : pcieLinkId)
        {
            devCountPcieLinkId.push_back(std::to_string(e));
        }
        wildCards.push_back({
            {"Name", "PCIeLinkId"},
            {"Values", devCountPcieLinkId},
        });
    }
}

inline void
    metricsReplacements(std::vector<std::string> name,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        std::vector<std::string> inputMetricProperties)
{
    nlohmann::json& wildCards = asyncResp->res.jsonValue["Wildcards"];
    std::set<std::string> wildCardValues;
    for (const auto& e : inputMetricProperties)
    {
        replaceNumber(e, name[0], name[1], wildCardValues);
    }
    // insert set to json payload here
    nlohmann::json devCount = nlohmann::json::array();
    for (const auto& e : wildCardValues)
    {
        devCount.push_back(e);
    }

    wildCards.push_back({
        {"Name", name[2]},
        {"Values", devCount},
    });
    return;
}

inline void getShmemMetricsDefinitionWildCard(
    [[maybe_unused]] const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& metricId,
    [[maybe_unused]] const std::string& deviceType)
{
    BMCWEB_LOG_DEBUG("getShmemMetricsDefinitionWildCards :{}", metricId);

    std::vector<std::string> chassisPlatformEnvironmentMetricsReplacements = {
        {chassisName, "{BSWild}", "BSWild"}};
    std::vector<std::string> processorPlatformEnvironmentMetricsReplacements = {
        {processorModule, "{PMWild}", "PMWild"}};
    std::vector<std::string> cpuPlatformEnvironmentMetricsReplacements = {
        {cpu, "{CWild}", "CWild"}};
    std::vector<std::string> fpgaPlatformEnvironmentMetricsReplacements = {
        {fpgaChassiName, "{FWild}", "FWild"}};
    std::vector<std::string> gpuPlatformEnvironmentMetricsReplacements = {
        {gpuName, "{GWild}", "GWild"}};
    std::vector<std::string> nvSwitchPlatformEnvironmentMetricsReplacements = {
        {nvSwitch, "{NWild}", "NWild"}};
    std::vector<std::string> pcieRetimerPlatformEnvironmentMetricsReplacements =
        {{pcieRetimer, "{PRWild}", "PRWild"}};
    std::vector<std::string> pcieSwitchPlatformEnvironmentMetricsReplacements =
        {{pcieSwtich, "{PSWild}", "PSWild"}};
    std::vector<std::string>
        nvLinkManagementNICPlatformEnvironmentMetricsReplacements = {
            {nvLinkManagementNIC, "{NicWild}", "NicWild"}};
    std::vector<std::string>
        nvLinkManagementNICPortPlatformEnvironmentMetricsReplacements = {
            {nvLinkManagementNICPort, "{PortWild}", "PortWild"}};

    try
    {
#ifndef NVIDIA_HAVE_TAL

        BMCWEB_LOG_CRITICAL("Attempt to access tal but not available");
        return;
#else

        const auto& values = tal::TelemetryAggregator::getAllMrds(metricId);

        std::vector<std::string> inputMetricProperties;
        std::unordered_set<std::string> inputMetricPropertiesSet;
        nlohmann::json wildCards = nlohmann::json::array();
        asyncResp->res.jsonValue["Wildcards"] = wildCards;
        for (const auto& e : values)
        {
            if (deviceType == "NVSwitchPortMetrics" ||
                deviceType == "ProcessorPortMetrics" ||
                deviceType == "NetworkAdapterPortMetrics" ||
                deviceType == "PCIeRetimerPortMetrics" ||
                deviceType == "ProcessorPortGPMMetrics")
            {
                std::string result = e.metricProperty;
                size_t pos = result.find("#");
                if (pos != std::string::npos)
                {
                    result = result.substr(0, pos);
                }
                inputMetricPropertiesSet.insert(result);
            }
            else
            {
                inputMetricProperties.push_back(e.metricProperty);
            }
        }
        if (deviceType == "NVSwitchPortMetrics" ||
            deviceType == "ProcessorPortMetrics" ||
            deviceType == "NetworkAdapterPortMetrics" ||
            deviceType == "PCIeRetimerPortMetrics" ||
            deviceType == "ProcessorPortGPMMetrics")
        {
            for (const auto& e : inputMetricPropertiesSet)
            {
                inputMetricProperties.push_back(e);
            }
        }

        if (deviceType == "HealthMetrics")
        {
            for (const auto& e : inputMetricPropertiesSet)
            {
                inputMetricProperties.push_back(e);
            }
        }

        if (deviceType == "PlatformEnvironmentMetrics")
        {
            nvSwitch = platformDevicePrefix + "NVSwitch_";
            metricsReplacements(chassisPlatformEnvironmentMetricsReplacements,
                                asyncResp, inputMetricProperties);
            metricsReplacements(processorPlatformEnvironmentMetricsReplacements,
                                asyncResp, inputMetricProperties);
            metricsReplacements(cpuPlatformEnvironmentMetricsReplacements,
                                asyncResp, inputMetricProperties);
            metricsReplacements(fpgaPlatformEnvironmentMetricsReplacements,
                                asyncResp, inputMetricProperties);
            metricsReplacements(gpuPlatformEnvironmentMetricsReplacements,
                                asyncResp, inputMetricProperties);
            metricsReplacements(nvSwitchPlatformEnvironmentMetricsReplacements,
                                asyncResp, inputMetricProperties);
            metricsReplacements(
                pcieRetimerPlatformEnvironmentMetricsReplacements, asyncResp,
                inputMetricProperties);
            metricsReplacements(
                pcieSwitchPlatformEnvironmentMetricsReplacements, asyncResp,
                inputMetricProperties);
            metricsReplacements(
                nvLinkManagementNICPlatformEnvironmentMetricsReplacements,
                asyncResp, inputMetricProperties);
            metricsReplacements(
                nvLinkManagementNICPortPlatformEnvironmentMetricsReplacements,
                asyncResp, inputMetricProperties);
        }
        else
        {
            nvSwitch = "NVSwitch_";
            metricsReplacementsNonPlatformMetrics(
                asyncResp, inputMetricProperties, deviceType);
        }
#endif
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("Exception while getting MRD values: {}", e.what());
        messages::resourceNotFound(asyncResp->res, "MetricReport", metricId);
    }
}

inline void getShmemMetricsReportCollection(
    [[maybe_unused]] const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& reportType)
{
    BMCWEB_LOG_ERROR("Exception while getShmemMetricsReportDefinition");
    try
    {
#ifndef NVIDIA_HAVE_TAL
        BMCWEB_LOG_CRITICAL("Attempt to access tal but not available");
        return;
#else
        const auto& values = tal::TelemetryAggregator::getMrdNamespaces();

        nlohmann::json& addMembers = asyncResp->res.jsonValue["Members"];
        for (std::string memoryMetricId : values)
        {
            // Get the metric object
            std::string metricReportDefUriPath =
                "/redfish/v1/TelemetryService/";
            if (reportType == "MetricReports")
            {
                metricReportDefUriPath += "MetricReports/";
            }
            else
            {
                metricReportDefUriPath += "MetricReportDefinitions/";
            }
            std::string uripath = metricReportDefUriPath + memoryMetricId;
            addMembers.push_back({{"@odata.id", uripath}});
        }
        asyncResp->res.jsonValue["Members@odata.count"] = addMembers.size();

#endif
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("Exception while getting MRD: {}", e.what());
    }
}

} // namespace shmem
} // namespace redfish
