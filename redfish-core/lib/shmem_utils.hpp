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
#include "tal.hpp"

#include <nlohmann/json.hpp>

#include <unordered_set>

namespace redfish
{
namespace shmem
{

struct MetricsReplacement
{
    std::string searchPattern;   // The pattern to search for (e.g. chassisName)
    std::string wildcardPattern; // The wildcard pattern (e.g. "{BSWild}")
    std::string wildcardName;    // The name of the wildcard (e.g. "BSWild")
    mutable bool isEnabled; // Make isEnabled mutable so it can be modified even
                            // on const objects

    // clang-format off
    MetricsReplacement(std::string search, std::string pattern,
                       std::string name, bool enabled = false) :
        searchPattern(std::move(search)), wildcardPattern(std::move(pattern)),
        wildcardName(std::move(name)), isEnabled(enabled)
    {}
    // clang-format on
};

inline void updateReplacementFlag(const MetricsReplacement& replacement,
                                  const std::set<std::string>& allowedWildcards)
{
    replacement.isEnabled = (allowedWildcards.find(replacement.wildcardName) !=
                             allowedWildcards.end());
}

inline void
    getShmemPlatformMetrics(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& metricId,
                            const uint64_t& requestTimestamp = 0)
{
    BMCWEB_LOG_DEBUG("getShmemPlatformMetrics :{} Requested at : {}", metricId,
                     requestTimestamp);
    try
    {
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

        if (metricId == PLATFORMMETRICSID)
        {
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaMetricReport.v1_0_0.NvidiaMetricReport";
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["SensingIntervalMilliseconds"] =
                pmSensingInterval;
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
                    if (freshness <= staleSensorUpperLimitms)
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

static std::string gpuPrefix(platformGpuNamePrefix);
static std::string platformDevicePrefix(PLATFORMDEVICEPREFIX);
static std::string platformChassisName(PLATFORMCHASSISNAME);
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
static std::string networkAdapter(NETWORKADAPTERPREFIX);
static std::string networkAdapterLink(NETWORKADAPTERLINKPREFIX);
static std::string gpmInstances = "UtilizationPercent/";
static std::string nvLinkManagementNIC = "NIC_";
static std::string nvLinkManagementNICPort = "Port_";
static std::string retimer = "PCIeRetimer_";
static std::string ioBoard = "IO_Board_";
static std::string pdb = "PDB_";
static std::string blueField = "Riser_Slot";
static std::string blueFieldSensor = "BF3_Slot_";
static std::string storageBP = "StorageBackplane_";
static std::string storageDevice = "SSD_";
static std::string networkAdapterConnectX = "ConnectX_";
static std::string inlet = "Chassis_0_Inlet_";
static std::string pcb = "Chassis_0_PCB_";
static std::string hsc = "Chassis_0_HSC_";
static std::string sxm = "GPU_SXM_";
static std::string sxmSma = "SXM_SMA_";
static std::string cxSma = "ConnectX_SMA_";

// Add inline to prevent multiple definition errors
inline const MetricsReplacement
    chassisPlatformEnvironmentMetrics(chassisName, "{BSWild}", "BSWild");
inline const MetricsReplacement
    processorPlatformEnvironmentMetrics(processorModule, "{PMWild}", "PMWild");
inline const MetricsReplacement cpuPlatformEnvironmentMetrics(cpu, "{CWild}",
                                                              "CWild");
inline const MetricsReplacement
    fpgaPlatformEnvironmentMetrics(fpgaChassiName, "{FWild}", "FWild");
inline const MetricsReplacement
    gpuPlatformEnvironmentMetrics(gpuName, "{GWild}", "GWild");
inline const MetricsReplacement
    nvSwitchPlatformEnvironmentMetrics(nvSwitch, "{NWild}", "NWild");
inline const MetricsReplacement
    pcieRetimerPlatformEnvironmentMetrics(pcieRetimer, "{PRWild}", "PRWild");
inline const MetricsReplacement
    pcieSwitchPlatformEnvironmentMetrics(pcieSwtich, "{PSWild}", "PSWild");
inline const MetricsReplacement
    nvLinkManagementNICPlatformEnvironmentMetrics(nvLinkManagementNIC,
                                                  "{NicWild}", "NicWild");
inline const MetricsReplacement
    nvLinkManagementNICPortPlatformEnvironmentMetrics(nvLinkManagementNICPort,
                                                      "{PortWild}", "PortWild");
inline const MetricsReplacement
    ioBoardPlatformEnvironmentMetrics(ioBoard, "{IWild}", "IWild");
inline const MetricsReplacement pdbPlatformEnvironmentMetrics(pdb, "{PDBWild}",
                                                              "PDBWild");
inline const MetricsReplacement
    blueFieldPlatformEnvironmentMetrics(blueField, "{BFWild}", "BFWild");
inline const MetricsReplacement
    blueFieldSensorsPlatformEnvironmentMetrics(blueFieldSensor, "{BFSWild}",
                                               "BFSWild");
inline const MetricsReplacement
    storageBPSensorsPlatformEnvironmentMetrics(storageBP, "{SBWild}", "SBWild");
inline const MetricsReplacement
    storageBPDevicePlatformEnvironmentMetrics(storageDevice, "{SBDWild}",
                                              "SBDWild");
inline const MetricsReplacement
    inletPlatformEnvironmentMetrics(inlet, "{ILWild}", "ILWild");
inline const MetricsReplacement pcbPlatformEnvironmentMetrics(pcb, "{PCBWild}",
                                                              "PCBWild");
inline const MetricsReplacement hscPlatformEnvironmentMetrics(hsc, "{HWild}",
                                                              "HWild");
inline const MetricsReplacement sxmPlatformEnvironmentMetrics(sxm, "{SXMWild}",
                                                              "SXMWild");
inline const MetricsReplacement
    connectXPlatformEnvironmentMetrics(networkAdapterConnectX, "{CXWild}",
                                       "CXWild");
inline const MetricsReplacement
    sxmSmaPlatformEnvironmentMetrics(sxmSma, "{SSMAWild}", "SSMAWild");
inline const MetricsReplacement
    cxSmaPlatformEnvironmentMetrics(cxSma, "{CSMAWild}", "CSMAWild");

inline void replaceNumber(const std::string& input, const std::string& key,
                          const std::string& value,
                          std::set<std::string>& replacedName)
{
    std::regex pattern(key + "(\\d+)");
    std::smatch match;
    std::string res = input;
    if (value == "{BSWild}" || value == "{PDBWild}" || value == "{BFSWild}")
    {
        if (std::regex_search(res, match, pattern))
        {
            size_t lastSlashPos = input.find_last_of('/');
            if (lastSlashPos != std::string::npos)
            {
                std::string name = input.substr(lastSlashPos + 1);
                if (value == "{BFSWild}")
                {
                    if (std::regex_search(name, match, pattern))
                    {
                        std::string wildName = key;
                        wildName += "{BFWild}";
                        wildName += match.suffix();
                        replacedName.insert(wildName);
                    }
                }
                else
                {
                    replacedName.insert(name);
                }
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
    const std::string& deviceType, const std::set<std::string>& wildcardSet)
{
    // Precompute allowed flags based on the wildcards present in
    // "MetricProperties"
    bool allowNVSwitchId =
        (wildcardSet.find("NVSwitchId") != wildcardSet.end());
    bool allowNvlinkId = (wildcardSet.find("NvlinkId") != wildcardSet.end());
    bool allowGpuId = (wildcardSet.find("GpuId") != wildcardSet.end());
    bool allowCpuId = (wildcardSet.find("CpuId") != wildcardSet.end());
    bool allowProcessorId =
        (wildcardSet.find("ProcessorId") != wildcardSet.end());
    bool allowCoreId = (wildcardSet.find("CoreId") != wildcardSet.end());
    bool allowPCIeLinkId =
        (wildcardSet.find("PCIeLinkId") != wildcardSet.end());
    bool allowRetimerId = (wildcardSet.find("RetimerId") != wildcardSet.end());
    bool allowPortType = (wildcardSet.find("PortType") != wildcardSet.end());
    bool allowPortId = (wildcardSet.find("PortId") != wildcardSet.end());
    bool allowInstanceId =
        (wildcardSet.find("InstanceId") != wildcardSet.end());
    bool allowNetworkAdapterNId =
        (wildcardSet.find("NId") != wildcardSet.end());
    bool allowNetworkAdapterCXId =
        (wildcardSet.find("CXId") != wildcardSet.end());

    std::smatch match;
    std::set<int> nvSwitchId_Type_1;
    std::set<int> nvlinkId_Type_1;
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
    std::set<int> networkAdapterCXId;
    nlohmann::json& wildCards = asyncResp->res.jsonValue["Wildcards"];
    for (const auto& e : inputMetricProperties)
    {
        if (deviceType == "NVSwitchPortMetrics")
        {
            if (allowNVSwitchId)
            {
                std::regex switchPattern(nvSwitch + "(\\d+)");
                if (std::regex_search(e, match, switchPattern))
                {
                    int number = std::stoi(match[1].str());
                    nvSwitchId_Type_1.insert(number);
                }
            }
            if (allowNvlinkId)
            {
                std::regex nvLinkPattern(nvLink + "(\\d+)");
                if (std::regex_search(e, match, nvLinkPattern))
                {
                    int number = std::stoi(match[1].str());
                    nvlinkId_Type_1.insert(number);
                }
            }
        }
        if (deviceType == "NVSwitchMetrics")
        {
            if (allowNVSwitchId)
            {
                std::regex switchPattern(nvSwitch + "(\\d+)");
                if (std::regex_search(e, match, switchPattern))
                {
                    int number = std::stoi(match[1].str());
                    nvSwitchId_Type_1.insert(number);
                }
            }
        }
        if (deviceType == "PCIeRetimerMetrics")
        {
            if (allowRetimerId)
            {
                std::regex retimerPattern(retimer + "(\\d+)");
                if (std::regex_search(e, match, retimerPattern))
                {
                    int number = std::stoi(match[1].str());
                    retimerId.insert(number);
                }
            }
        }
        if (deviceType == "MemoryMetrics" || deviceType == "ProcessorMetrics" ||
            deviceType == "ProcessorGPMMetrics" ||
            deviceType == "ProcessorPortMetrics" ||
            deviceType == "ProcessorResetMetrics" ||
            deviceType == "ProcessorPortGPMMetrics")
        {
            if (allowGpuId)
            {
                std::regex gpuPattern(gpuPrefix + "(\\d+)");
                if (std::regex_search(e, match, gpuPattern))
                {
                    int number = std::stoi(match[1].str());
                    gpuId.insert(number);
                }
            }
        }
        if (deviceType == "ProcessorGPMMetrics")
        {
            if (allowInstanceId)
            {
                std::regex gpmInstancePattern(gpmInstances + "(\\d+)");
                if (std::regex_search(e, match, gpmInstancePattern))
                {
                    int number = std::stoi(match[1].str());
                    gpmInstance.insert(number);
                }
            }
        }
        if (deviceType == "ProcessorPortMetrics" ||
            deviceType == "ProcessorPortGPMMetrics")
        {
            if (allowNvlinkId)
            {
                std::regex nvLinkPattern(nvLink + "(\\d+)");
                if (std::regex_search(e, match, nvLinkPattern))
                {
                    int number = std::stoi(match[1].str());
                    nvlinkId_Type_1.insert(number);
                }
            }
        }
        if (deviceType == "NetworkAdapterPortMetrics")
        {
            if (allowNetworkAdapterNId)
            {
                std::regex networkAdapterPattern(networkAdapter + "(\\d+)");
                if (std::regex_search(e, match, networkAdapterPattern))
                {
                    int number = std::stoi(match[1].str());
                    networkAdapterNId.insert(number);
                }
            }
            if (allowNvlinkId)
            {
                std::regex nvLinkManagementPattern(networkAdapterLink +
                                                   "(\\d+)");
                if (std::regex_search(e, match, nvLinkManagementPattern))
                {
                    int number = std::stoi(match[1].str());
                    nvLinkManagementId.insert(number);
                }
            }
            if (allowNetworkAdapterCXId)
            {
                std::regex networkAdapterPattern(networkAdapterConnectX +
                                                 "(\\d+)");
                if (std::regex_search(e, match, networkAdapterPattern))
                {
                    int number = std::stoi(match[1].str());
                    networkAdapterCXId.insert(number);
                }
            }
        }
        if (deviceType == "PCIeRetimerPortMetrics")
        {
            if (allowRetimerId)
            {
                std::regex pcieRetimerPattern(retimer + "(\\d+)");
                if (std::regex_search(e, match, pcieRetimerPattern))
                {
                    int number = std::stoi(match[1].str());
                    retimerId.insert(number);
                }
            }
            if (allowPortType && allowPortId)
            {
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
        }
        if (deviceType == "CpuProcessorMetrics")
        {
            if (allowCpuId)
            {
                std::regex cpuProcessorPattern(cpuProcessor + "(\\d+)");
                if (std::regex_search(e, match, cpuProcessorPattern))
                {
                    int number = std::stoi(match[1].str());
                    cpuId.insert(number);
                }
            }
            if (allowProcessorId)
            {
                std::regex processorPattern(processor + "(\\d+)");
                if (std::regex_search(e, match, processorPattern))
                {
                    int number = std::stoi(match[1].str());
                    processorId.insert(number);
                }
            }
            if (allowCoreId)
            {
                std::regex cpuCorePattern(cpuCore + "(\\d+)");
                if (std::regex_search(e, match, cpuCorePattern))
                {
                    int number = std::stoi(match[1].str());
                    coreId.insert(number);
                }
            }
            if (allowNvlinkId)
            {
                std::regex nvLinkPattern(nvLink + "(\\d+)");
                if (std::regex_search(e, match, nvLinkPattern))
                {
                    int number = std::stoi(match[1].str());
                    nvLinkId.insert(number);
                }
            }
            if (allowPCIeLinkId)
            {
                std::regex pcieLinkPattern(pcieLink + "(\\d+)");
                if (std::regex_search(e, match, pcieLinkPattern))
                {
                    int number = std::stoi(match[1].str());
                    pcieLinkId.insert(number);
                }
            }
        }
        if (deviceType == "HealthMetrics")
        {
            if (allowCpuId)
            {
                std::regex cpuProcessorPattern(cpuProcessor + "(\\d+)");
                if (std::regex_search(e, match, cpuProcessorPattern))
                {
                    int number = std::stoi(match[1].str());
                    cpuId.insert(number);
                }
            }
            if (allowGpuId)
            {
                std::regex gpuPattern(gpuPrefix + "(\\d+)");
                if (std::regex_search(e, match, gpuPattern))
                {
                    int number = std::stoi(match[1].str());
                    gpuId.insert(number);
                }
            }
            if (allowRetimerId)
            {
                std::regex pcieRetimerPattern(retimer + "(\\d+)");
                if (std::regex_search(e, match, pcieRetimerPattern))
                {
                    int number = std::stoi(match[1].str());
                    retimerId.insert(number);
                }
            }
            if (allowNVSwitchId)
            {
                std::regex switchPattern(nvSwitch + "(\\d+)");
                if (std::regex_search(e, match, switchPattern))
                {
                    int number = std::stoi(match[1].str());
                    nvSwitchId_Type_1.insert(number);
                }
            }
        }
    }
    if (deviceType == "NVSwitchPortMetrics")
    {
        if (allowNVSwitchId)
        {
            nlohmann::json devCountSwitchType_1 = nlohmann::json::array();
            for (const auto& e : nvSwitchId_Type_1)
            {
                devCountSwitchType_1.push_back(std::to_string(e));
            }
            wildCards.push_back({
                {"Name", "NVSwitchId"},
                {"Values", devCountSwitchType_1},
            });
        }
        if (allowNvlinkId)
        {
            nlohmann::json devCountNVlinkId_Type_1 = nlohmann::json::array();
            for (const auto& e : nvlinkId_Type_1)
            {
                devCountNVlinkId_Type_1.push_back(std::to_string(e));
            }
            wildCards.push_back({
                {"Name", "NvlinkId"},
                {"Values", devCountNVlinkId_Type_1},
            });
        }
    }
    if (deviceType == "NetworkAdapterPortMetrics")
    {
        if (allowNetworkAdapterNId)
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
        }
        if (allowNvlinkId)
        {
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
        if (allowNetworkAdapterCXId)
        {
            nlohmann::json devCountNetworkAdapterCX = nlohmann::json::array();
            for (const auto& e : networkAdapterCXId)
            {
                devCountNetworkAdapterCX.push_back(std::to_string(e));
            }
            wildCards.push_back({
                {"Name", "CXId"},
                {"Values", devCountNetworkAdapterCX},
            });
        }
    }
    if (deviceType == "PCIeRetimerPortMetrics")
    {
        if (allowRetimerId)
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
        if (allowPortType)
        {
            nlohmann::json devCountRetimerPortType = nlohmann::json::array();
            for (const auto& e : portTypes)
            {
                devCountRetimerPortType.push_back(e);
            }
            wildCards.push_back({
                {"Name", "PortType"},
                {"Values", devCountRetimerPortType},
            });
        }
        if (allowPortId)
        {
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
    }
    if (deviceType == "PCIeRetimerMetrics" || deviceType == "HealthMetrics")
    {
        if (allowRetimerId)
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
    }
    if (deviceType == "NVSwitchMetrics" || deviceType == "HealthMetrics")
    {
        if (allowNVSwitchId)
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
    }
    if (deviceType == "MemoryMetrics" || deviceType == "ProcessorMetrics" ||
        deviceType == "ProcessorGPMMetrics" ||
        deviceType == "ProcessorPortMetrics" ||
        deviceType == "ProcessorPortGPMMetrics" ||
        deviceType == "ProcessorResetMetrics" || deviceType == "HealthMetrics")
    {
        if (allowGpuId)
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
    }
    if (deviceType == "ProcessorGPMMetrics")
    {
        if (allowInstanceId)
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
    }
    if (deviceType == "ProcessorPortMetrics" ||
        deviceType == "ProcessorPortGPMMetrics")
    {
        if (allowNvlinkId)
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
    }
    if (deviceType == "CpuProcessorMetrics" || deviceType == "HealthMetrics")
    {
        if (allowCpuId)
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
        }
    }
    if (deviceType == "CpuProcessorMetrics")
    {
        if (allowProcessorId)
        {
            nlohmann::json devCountProcessorId = nlohmann::json::array();
            for (const auto& e : processorId)
            {
                devCountProcessorId.push_back(std::to_string(e));
            }
            wildCards.push_back({
                {"Name", "ProcessorId"},
                {"Values", devCountProcessorId},
            });
        }
        if (allowCoreId)
        {
            nlohmann::json devCountCoreId = nlohmann::json::array();
            for (const auto& e : coreId)
            {
                devCountCoreId.push_back(std::to_string(e));
            }
            wildCards.push_back({
                {"Name", "CoreId"},
                {"Values", devCountCoreId},
            });
        }
        if (allowNvlinkId)
        {
            nlohmann::json devCountNvlinkId = nlohmann::json::array();
            for (const auto& e : nvLinkId)
            {
                devCountNvlinkId.push_back(std::to_string(e));
            }
            wildCards.push_back({
                {"Name", "NvlinkId"},
                {"Values", devCountNvlinkId},
            });
        }
        if (allowPCIeLinkId)
        {
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
}

inline void
    metricsReplacements(const MetricsReplacement& replacement,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::vector<std::string>& inputMetricProperties)
{
    // Only process if enabled
    if (!replacement.isEnabled)
    {
        return;
    }

    nlohmann::json& wildCards = asyncResp->res.jsonValue["Wildcards"];
    std::set<std::string> wildCardValues;

    for (const auto& e : inputMetricProperties)
    {
        replaceNumber(e, replacement.searchPattern, replacement.wildcardPattern,
                      wildCardValues);
    }

    // insert set to json payload here
    nlohmann::json devCount = nlohmann::json::array();
    for (const auto& e : wildCardValues)
    {
        devCount.push_back(e);
    }

    wildCards.push_back({
        {"Name", replacement.wildcardName},
        {"Values", devCount},
    });
    return;
}

inline void getShmemMetricsDefinitionWildCard(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& metricId, const std::string& deviceType)
{
    BMCWEB_LOG_DEBUG("getShmemMetricsDefinitionWildCards :{}", metricId);
    //--------------------------------------------------------------------------
    // Build the allowed wildcards set:
    // Look at the "MetricProperties" JSON array; for each string, extract any
    // token found inside curly braces (i.e. {token}). Every found token is
    // considered allowed.
    std::set<std::string> allowedWildcards;
    if (asyncResp->res.jsonValue.contains("MetricProperties") &&
        asyncResp->res.jsonValue["MetricProperties"].is_array())
    {
        for (const auto& property :
             asyncResp->res.jsonValue["MetricProperties"])
        {
            if (!property.is_string())
            {
                continue;
            }
            std::string propertyStr = property.get<std::string>();
            std::regex tokenRegex("\\{([^}]+)\\}");
            std::smatch match;
            std::string::const_iterator searchStart(propertyStr.cbegin());
            while (std::regex_search(searchStart, propertyStr.cend(), match,
                                     tokenRegex))
            {
                allowedWildcards.insert(match[1].str());
                searchStart = match.suffix().first;
            }
        }
    }

    std::vector<std::string> inputMetricProperties;
    std::unordered_set<std::string> inputMetricPropertiesSet;
    nlohmann::json wildCards = nlohmann::json::array();
    asyncResp->res.jsonValue["Wildcards"] = wildCards;

    try
    {
        const auto& values = tal::TelemetryAggregator::getAllMrds(metricId);
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
            updateReplacementFlag(chassisPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(processorPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(cpuPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(fpgaPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(gpuPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(nvSwitchPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(pcieRetimerPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(pcieSwitchPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(nvLinkManagementNICPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(
                nvLinkManagementNICPortPlatformEnvironmentMetrics,
                allowedWildcards);
            updateReplacementFlag(ioBoardPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(pdbPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(blueFieldPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(blueFieldSensorsPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(storageBPSensorsPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(storageBPDevicePlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(inletPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(pcbPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(hscPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(sxmPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(connectXPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(sxmSmaPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(cxSmaPlatformEnvironmentMetrics,
                                  allowedWildcards);

            nvSwitch = platformDevicePrefix + "NVSwitch_";
            metricsReplacements(chassisPlatformEnvironmentMetrics, asyncResp,
                                inputMetricProperties);
            metricsReplacements(processorPlatformEnvironmentMetrics, asyncResp,
                                inputMetricProperties);
            metricsReplacements(cpuPlatformEnvironmentMetrics, asyncResp,
                                inputMetricProperties);
            metricsReplacements(fpgaPlatformEnvironmentMetrics, asyncResp,
                                inputMetricProperties);
            metricsReplacements(gpuPlatformEnvironmentMetrics, asyncResp,
                                inputMetricProperties);
            metricsReplacements(nvSwitchPlatformEnvironmentMetrics, asyncResp,
                                inputMetricProperties);
            metricsReplacements(pcieRetimerPlatformEnvironmentMetrics,
                                asyncResp, inputMetricProperties);
            metricsReplacements(pcieSwitchPlatformEnvironmentMetrics, asyncResp,
                                inputMetricProperties);
            metricsReplacements(nvLinkManagementNICPlatformEnvironmentMetrics,
                                asyncResp, inputMetricProperties);
            metricsReplacements(
                nvLinkManagementNICPortPlatformEnvironmentMetrics, asyncResp,
                inputMetricProperties);
            metricsReplacements(ioBoardPlatformEnvironmentMetrics, asyncResp,
                                inputMetricProperties);
            metricsReplacements(pdbPlatformEnvironmentMetrics, asyncResp,
                                inputMetricProperties);
            metricsReplacements(blueFieldPlatformEnvironmentMetrics, asyncResp,
                                inputMetricProperties);
            metricsReplacements(blueFieldSensorsPlatformEnvironmentMetrics,
                                asyncResp, inputMetricProperties);
            metricsReplacements(storageBPSensorsPlatformEnvironmentMetrics,
                                asyncResp, inputMetricProperties);
            metricsReplacements(storageBPDevicePlatformEnvironmentMetrics,
                                asyncResp, inputMetricProperties);
            metricsReplacements(inletPlatformEnvironmentMetrics, asyncResp,
                                inputMetricProperties);
            metricsReplacements(pcbPlatformEnvironmentMetrics, asyncResp,
                                inputMetricProperties);
            metricsReplacements(hscPlatformEnvironmentMetrics, asyncResp,
                                inputMetricProperties);
            metricsReplacements(sxmPlatformEnvironmentMetrics, asyncResp,
                                inputMetricProperties);
            metricsReplacements(connectXPlatformEnvironmentMetrics, asyncResp,
                                inputMetricProperties);
            metricsReplacements(sxmSmaPlatformEnvironmentMetrics, asyncResp,
                                inputMetricProperties);
            metricsReplacements(cxSmaPlatformEnvironmentMetrics, asyncResp,
                                inputMetricProperties);
        }
        else
        {
            nvSwitch = "NVSwitch_";
            metricsReplacementsNonPlatformMetrics(
                asyncResp, inputMetricProperties, deviceType, allowedWildcards);
        }
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("Exception while getting MRD values: {}", e.what());
        messages::resourceNotFound(asyncResp->res, "MetricReport", metricId);
    }
}

inline void getShmemMetricsReportCollection(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& reportType)
{
    BMCWEB_LOG_ERROR("Exception while getShmemMetricsReportDefinition");
    try
    {
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
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("Exception while getting MRD: {}", e.what());
    }
}

} // namespace shmem
} // namespace redfish
