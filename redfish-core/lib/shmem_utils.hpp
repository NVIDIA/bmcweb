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
#include "bmcweb_config.h"

#include "async_resp.hpp"
#include "error_messages.hpp"

#include <nlohmann/json.hpp>

#include <regex>
#include <set>
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
    MetricsReplacement(std::string_view search, std::string_view pattern,
                       std::string_view name, bool enabled = false) :
        searchPattern(search), wildcardPattern(pattern),
        wildcardName(name), isEnabled(enabled)
    {}
    // clang-format on
};

inline void updateReplacementFlag(const MetricsReplacement& replacement,
                                  const std::set<std::string>& allowedWildcards)
{
    replacement.isEnabled =
        (allowedWildcards.contains(replacement.wildcardName));
}

inline void getShmemPlatformMetrics(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& metricId, const uint64_t& requestTimestamp = 0)
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

        if (metricId == PLATFORMMETRICSID)
        {
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaMetricReport.v1_0_0.NvidiaMetricReport";
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["SensingIntervalMilliseconds"] =
                BMCWEB_PLATFORM_METRICS_SENSING_INTERVAL;
        }

        for (const auto& e : values)
        {
            nlohmann::json& metricValue = thisMetric["MetricValue"];
            if (e.sensorValue == "nan")
            {
                metricValue = nullptr;
            }
            else
            {
                metricValue = e.sensorValue;
            }
            thisMetric["Timestamp"] = e.timestampStr;
            thisMetric["MetricProperty"] = e.metricProperty;

            if (metricId == PLATFORMMETRICSID)
            {
                thisMetric["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaMetricReport.v1_0_0.NvidiaMetricReport";
                thisMetric["Oem"]["Nvidia"]["MetricValueStale"] = true;

                if (requestTimestamp != 0 && !metricValue.is_null())
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
            }
            resArray.push_back(thisMetric);
        }
#endif
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("Exception while getting MRD values: {}", e.what());
        messages::resourceNotFound(asyncResp->res, "MetricReport", metricId);
    }
}

constexpr const std::string_view metricReportDefinitionUri =
    "/redfish/v1/TelemetryService/MetricReportDefinitions";

constexpr const std::string_view metricReportUri =
    "/redfish/v1/TelemetryService/MetricReports";

const static std::string gpuPrefix(platformGpuNamePrefix);
constexpr const std::string_view platformChassisName(PLATFORMCHASSISNAME);
const static std::string platformDevicePrefix(PLATFORMDEVICEPREFIX);
const static std::string chassisName = platformDevicePrefix + "Chassis_";
const static std::string fpgaChassiName = platformDevicePrefix + "FPGA_";
const static std::string gpuName = platformDevicePrefix + gpuPrefix;
const static std::string nvSwitch = platformDevicePrefix + "NVSwitch_";
const static std::string pcieRetimer = platformDevicePrefix + "PCIeRetimer_";
const static std::string pcieSwtich = platformDevicePrefix + "PCIeSwitch_";
const static std::string processorModule =
    platformDevicePrefix + "ProcessorModule_";
const static std::string cpu = platformDevicePrefix + "CPU_";
constexpr const std::string_view nvLink = "NVLink_";
constexpr const std::string_view nvLinkType =
    "(NVLink|InterswitchPort|NVLinkManagement)_";
constexpr const std::string_view cpuProcessor = "CPU_";
constexpr const std::string_view processor = "ProcessorModule_";
constexpr const std::string_view pcieLink = "PCIeLink_";
constexpr const std::string_view cpuCore = "CoreUtil_";
constexpr const std::string_view networkAdapter(NETWORKADAPTERPREFIX);
constexpr const std::string_view networkAdapterLink(NETWORKADAPTERLINKPREFIX);
constexpr const std::string_view gpmInstances = "UtilizationPercent/";
constexpr const std::string_view nvLinkManagementNIC = "NIC_";
constexpr const std::string_view nvLinkManagementNICPort = "Port_";
constexpr const std::string_view retimer = "PCIeRetimer_";
constexpr const std::string_view ioBoard = "IO_Board_";
constexpr const std::string_view pdb = "PDB_";
constexpr const std::string_view blueField = "Riser_Slot";
constexpr const std::string_view blueFieldSensor = "BF3_Slot_";
constexpr const std::string_view storageBP = "StorageBackplane_";
constexpr const std::string_view storageDevice = "SSD_";
constexpr const std::string_view networkAdapterConnectX = "ConnectX_";
constexpr const std::string_view inlet = "Chassis_0_Inlet_";
constexpr const std::string_view pcb = "Chassis_0_PCB_";
constexpr const std::string_view hsc = "Chassis_0_HSC_";
constexpr const std::string_view sxm = "GPU_SXM_";
constexpr const std::string_view sxmSma = "SXM_SMA_";
constexpr const std::string_view cxSma = "ConnectX_SMA_";
constexpr const std::string_view gpuSma = "GPU_SMA_";
constexpr const std::string_view pmSma = "ProcessorModule_SMA_";
constexpr const std::string_view gpuTemp = "GPU_\\d+_TEMP_";
constexpr const std::string_view hscc = "Chassis_0_HSCC_";

// Add inline to prevent multiple definition errors
inline const MetricsReplacement chassisPlatformEnvironmentMetrics(
    chassisName, "{BSWild}", "BSWild");
inline const MetricsReplacement processorPlatformEnvironmentMetrics(
    processorModule, "{PMWild}", "PMWild");
inline const MetricsReplacement cpuPlatformEnvironmentMetrics(cpu, "{CWild}",
                                                              "CWild");
inline const MetricsReplacement fpgaPlatformEnvironmentMetrics(
    fpgaChassiName, "{FWild}", "FWild");
inline const MetricsReplacement gpuPlatformEnvironmentMetrics(
    gpuName, "{GWild}", "GWild");
inline const MetricsReplacement nvSwitchPlatformEnvironmentMetrics(
    nvSwitch, "{NWild}", "NWild");
inline const MetricsReplacement pcieRetimerPlatformEnvironmentMetrics(
    pcieRetimer, "{PRWild}", "PRWild");
inline const MetricsReplacement pcieSwitchPlatformEnvironmentMetrics(
    pcieSwtich, "{PSWild}", "PSWild");
inline const MetricsReplacement nvLinkManagementNICPlatformEnvironmentMetrics(
    nvLinkManagementNIC, "{NicWild}", "NicWild");
inline const MetricsReplacement
    nvLinkManagementNICPortPlatformEnvironmentMetrics(nvLinkManagementNICPort,
                                                      "{PortWild}", "PortWild");
inline const MetricsReplacement ioBoardPlatformEnvironmentMetrics(
    ioBoard, "{IWild}", "IWild");
inline const MetricsReplacement pdbPlatformEnvironmentMetrics(pdb, "{PDBWild}",
                                                              "PDBWild");
inline const MetricsReplacement blueFieldPlatformEnvironmentMetrics(
    blueField, "{BFWild}", "BFWild");
inline const MetricsReplacement blueFieldSensorsPlatformEnvironmentMetrics(
    blueFieldSensor, "{BFSWild}", "BFSWild");
inline const MetricsReplacement storageBPSensorsPlatformEnvironmentMetrics(
    storageBP, "{SBWild}", "SBWild");
inline const MetricsReplacement storageBPDevicePlatformEnvironmentMetrics(
    storageDevice, "{SBDWild}", "SBDWild");
inline const MetricsReplacement inletPlatformEnvironmentMetrics(
    inlet, "{ILWild}", "ILWild");
inline const MetricsReplacement pcbPlatformEnvironmentMetrics(pcb, "{PCBWild}",
                                                              "PCBWild");
inline const MetricsReplacement hscPlatformEnvironmentMetrics(hsc, "{HWild}",
                                                              "HWild");
inline const MetricsReplacement sxmPlatformEnvironmentMetrics(sxm, "{SXMWild}",
                                                              "SXMWild");
inline const MetricsReplacement connectXPlatformEnvironmentMetrics(
    networkAdapterConnectX, "{CXWild}", "CXWild");
inline const MetricsReplacement sxmSmaPlatformEnvironmentMetrics(
    sxmSma, "{SSMAWild}", "SSMAWild");
inline const MetricsReplacement cxSmaPlatformEnvironmentMetrics(
    cxSma, "{CSMAWild}", "CSMAWild");
inline const MetricsReplacement gpuSmaPlatformEnvironmentMetrics(
    gpuSma, "{GSMAWild}", "GSMAWild");
inline const MetricsReplacement pmSmaPlatformEnvironmentMetrics(
    pmSma, "{PSMAWild}", "PSMAWild");
inline const MetricsReplacement gpuTempPlatformEnvironmentMetrics(
    gpuTemp, "{GTWild}", "GTWild");
inline const MetricsReplacement hsccPlatformEnvironmentMetrics(hscc, "{HCWild}",
                                                               "HCWild");

inline void replaceNumber(const std::string& input, const std::string& key,
                          const std::regex& pattern, const std::string& value,
                          std::set<std::string>& replacedName)
{
    std::smatch match;
    const std::string& res = input;
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
            replacedName.insert(match[1].str());
        }
    }
}

inline void metricsReplacementsNonPlatformMetrics(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::vector<std::string>& inputMetricProperties,
    const std::string& deviceType, const std::set<std::string>& wildcardSet)
{
    // Precompute allowed flags based on the wildcards present in
    // "MetricProperties"
    bool allowNVSwitchId = (wildcardSet.contains("NVSwitchId"));
    bool allowLinkType = (wildcardSet.contains("LinkType"));
    bool allowNvlinkId = (wildcardSet.contains("NvlinkId"));
    bool allowGpuId = (wildcardSet.contains("GpuId"));
    bool allowCpuId = (wildcardSet.contains("CpuId"));
    bool allowProcessorId = (wildcardSet.contains("ProcessorId"));
    bool allowCoreId = (wildcardSet.contains("CoreId"));
    bool allowPCIeLinkId = (wildcardSet.contains("PCIeLinkId"));
    bool allowRetimerId = (wildcardSet.contains("RetimerId"));
    bool allowPortType = (wildcardSet.contains("PortType"));
    bool allowPortId = (wildcardSet.contains("PortId"));
    bool allowInstanceId = (wildcardSet.contains("InstanceId"));
    bool allowNetworkAdapterNId = (wildcardSet.contains("NId"));
    bool allowNetworkAdapterCXId = (wildcardSet.contains("CXId"));

    std::string nvSwitchValue = "NVSwitch_";
    std::smatch match;
    std::set<int> nvSwitchIdType1;
    std::set<int> nvlinkIdType1;
    std::set<std::string> nvlinkType1;
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
    for (const auto& property : inputMetricProperties)
    {
        if (deviceType == "NVSwitchPortMetrics")
        {
            if (allowNVSwitchId)
            {
                std::regex switchPattern(std::string(nvSwitchValue) + "(\\d+)");
                if (std::regex_search(property, match, switchPattern))
                {
                    int number = std::stoi(match[1].str());
                    nvSwitchIdType1.insert(number);
                }
            }
            if (allowLinkType || allowNvlinkId)
            {
                std::regex nvLinkPattern(std::string(nvLinkType) + "(\\d+)");
                if (std::regex_search(property, match, nvLinkPattern) &&
                    match.size() > 2)
                {
                    if (allowLinkType)
                    {
                        nvlinkType1.insert(match[1].str());
                    }
                    if (allowNvlinkId)
                    {
                        int number = std::stoi(match[2].str());
                        nvlinkIdType1.insert(number);
                    }
                }
            }
        }
        if (deviceType == "NVSwitchMetrics")
        {
            if (allowNVSwitchId)
            {
                std::regex switchPattern(std::string(nvSwitchValue) + "(\\d+)");
                if (std::regex_search(property, match, switchPattern))
                {
                    int number = std::stoi(match[1].str());
                    nvSwitchIdType1.insert(number);
                }
            }
        }
        if (deviceType == "PCIeRetimerMetrics")
        {
            if (allowRetimerId)
            {
                std::regex retimerPattern(std::string(retimer) + "(\\d+)");
                if (std::regex_search(property, match, retimerPattern))
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
                std::regex gpuPattern(std::string(gpuPrefix) + "(\\d+)");
                if (std::regex_search(property, match, gpuPattern))
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
                std::regex gpmInstancePattern(
                    std::string(gpmInstances) + "(\\d+)");
                if (std::regex_search(property, match, gpmInstancePattern))
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
                std::regex nvLinkPattern(std::string(nvLink) + "(\\d+)");
                if (std::regex_search(property, match, nvLinkPattern))
                {
                    int number = std::stoi(match[1].str());
                    nvlinkIdType1.insert(number);
                }
            }
        }
        if (deviceType == "NetworkAdapterPortMetrics")
        {
            if (allowNetworkAdapterNId)
            {
                std::regex networkAdapterPattern(
                    std::string(networkAdapter) + "(\\d+)");
                if (std::regex_search(property, match, networkAdapterPattern))
                {
                    int number = std::stoi(match[1].str());
                    networkAdapterNId.insert(number);
                }
            }
            if (allowNvlinkId)
            {
                std::regex nvLinkManagementPattern(
                    std::string(networkAdapterLink) + "(\\d+)");
                if (std::regex_search(property, match, nvLinkManagementPattern))
                {
                    int number = std::stoi(match[1].str());
                    nvLinkManagementId.insert(number);
                }
            }
            if (allowNetworkAdapterCXId)
            {
                std::regex networkAdapterPattern(
                    std::string(networkAdapterConnectX) + "(\\d+)");
                if (std::regex_search(property, match, networkAdapterPattern))
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
                std::regex pcieRetimerPattern(std::string(retimer) + "(\\d+)");
                if (std::regex_search(property, match, pcieRetimerPattern))
                {
                    int number = std::stoi(match[1].str());
                    retimerId.insert(number);
                }
            }
            if (allowPortType && allowPortId)
            {
                std::regex retimerPortPattern("/Ports/(\\w+)_(\\d+)");
                if (std::regex_search(property, match, retimerPortPattern) &&
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
                std::regex cpuProcessorPattern(
                    std::string(cpuProcessor) + "(\\d+)");
                if (std::regex_search(property, match, cpuProcessorPattern))
                {
                    int number = std::stoi(match[1].str());
                    cpuId.insert(number);
                }
            }
            if (allowProcessorId)
            {
                std::regex processorPattern(std::string(processor) + "(\\d+)");
                if (std::regex_search(property, match, processorPattern))
                {
                    int number = std::stoi(match[1].str());
                    processorId.insert(number);
                }
            }
            if (allowCoreId)
            {
                std::regex cpuCorePattern(std::string(cpuCore) + "(\\d+)");
                if (std::regex_search(property, match, cpuCorePattern))
                {
                    int number = std::stoi(match[1].str());
                    coreId.insert(number);
                }
            }
            if (allowNvlinkId)
            {
                std::regex nvLinkPattern(std::string(nvLink) + "(\\d+)");
                if (std::regex_search(property, match, nvLinkPattern))
                {
                    int number = std::stoi(match[1].str());
                    nvLinkId.insert(number);
                }
            }
            if (allowPCIeLinkId)
            {
                std::regex pcieLinkPattern(std::string(pcieLink) + "(\\d+)");
                if (std::regex_search(property, match, pcieLinkPattern))
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
                std::regex cpuProcessorPattern(
                    std::string(cpuProcessor) + "(\\d+)");
                if (std::regex_search(property, match, cpuProcessorPattern))
                {
                    int number = std::stoi(match[1].str());
                    cpuId.insert(number);
                }
            }
            if (allowGpuId)
            {
                std::regex gpuPattern(std::string(gpuPrefix) + "(\\d+)");
                if (std::regex_search(property, match, gpuPattern))
                {
                    int number = std::stoi(match[1].str());
                    gpuId.insert(number);
                }
            }
            if (allowRetimerId)
            {
                std::regex pcieRetimerPattern(std::string(retimer) + "(\\d+)");
                if (std::regex_search(property, match, pcieRetimerPattern))
                {
                    int number = std::stoi(match[1].str());
                    retimerId.insert(number);
                }
            }
            if (allowNVSwitchId)
            {
                std::regex switchPattern(std::string(nvSwitchValue) + "(\\d+)");
                if (std::regex_search(property, match, switchPattern))
                {
                    int number = std::stoi(match[1].str());
                    nvSwitchIdType1.insert(number);
                }
            }
        }
    }
    if (deviceType == "NVSwitchPortMetrics")
    {
        if (allowNVSwitchId)
        {
            nlohmann::json devCountSwitchType1 = nlohmann::json::array();
            for (const auto& item : nvSwitchIdType1)
            {
                devCountSwitchType1.push_back(std::to_string(item));
            }
            wildCards.push_back({
                {"Name", "NVSwitchId"},
                {"Values", devCountSwitchType1},
            });
        }
        if (allowLinkType)
        {
            nlohmann::json devNVlinkType1 = nlohmann::json::array();
            for (const auto& item : nvlinkType1)
            {
                devNVlinkType1.push_back(item);
            }
            wildCards.push_back({
                {"Name", "LinkType"},
                {"Values", devNVlinkType1},
            });
        }
        if (allowNvlinkId)
        {
            nlohmann::json devCountNVlinkIdType1 = nlohmann::json::array();
            for (const auto& item : nvlinkIdType1)
            {
                devCountNVlinkIdType1.push_back(std::to_string(item));
            }
            wildCards.push_back({
                {"Name", "NvlinkId"},
                {"Values", devCountNVlinkIdType1},
            });
        }
    }
    if (deviceType == "NetworkAdapterPortMetrics")
    {
        if (allowNetworkAdapterNId)
        {
            nlohmann::json devCountNetworkAdapter = nlohmann::json::array();
            for (const auto& item : networkAdapterNId)
            {
                devCountNetworkAdapter.push_back(std::to_string(item));
            }
            wildCards.push_back({
                {"Name", "NId"},
                {"Values", devCountNetworkAdapter},
            });
        }
        if (allowNvlinkId)
        {
            nlohmann::json devCountNVLinkManagementId = nlohmann::json::array();
            for (const auto& item : nvLinkManagementId)
            {
                devCountNVLinkManagementId.push_back(std::to_string(item));
            }
            wildCards.push_back({
                {"Name", "NvlinkId"},
                {"Values", devCountNVLinkManagementId},
            });
        }
        if (allowNetworkAdapterCXId)
        {
            nlohmann::json devCountNetworkAdapterCX = nlohmann::json::array();
            for (const auto& item : networkAdapterCXId)
            {
                devCountNetworkAdapterCX.push_back(std::to_string(item));
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
            for (const auto& item : retimerId)
            {
                devCountRetimerId.push_back(std::to_string(item));
            }
            wildCards.push_back({
                {"Name", "RetimerId"},
                {"Values", devCountRetimerId},
            });
        }
        if (allowPortType)
        {
            nlohmann::json devCountRetimerPortType = nlohmann::json::array();
            for (const auto& item : portTypes)
            {
                devCountRetimerPortType.push_back(item);
            }
            wildCards.push_back({
                {"Name", "PortType"},
                {"Values", devCountRetimerPortType},
            });
        }
        if (allowPortId)
        {
            nlohmann::json devCountRetimerPortId = nlohmann::json::array();
            for (const auto& item : portIds)
            {
                devCountRetimerPortId.push_back(std::to_string(item));
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
            for (const auto& item : retimerId)
            {
                devCountRetimerId.push_back(std::to_string(item));
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
            for (const auto& item : nvSwitchIdType1)
            {
                devCountNVSwitchId.push_back(std::to_string(item));
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
            for (const auto& item : gpuId)
            {
                devCountGpuId.push_back(std::to_string(item));
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
            for (const auto& item : gpmInstance)
            {
                devCountInstanceId.push_back(std::to_string(item));
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
            for (const auto& item : nvlinkIdType1)
            {
                devCountnvlinkId.push_back(std::to_string(item));
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

inline void metricsReplacements(
    const MetricsReplacement& replacement, nlohmann::json& wildCards,
    const std::vector<std::string>& inputMetricProperties)
{
    // Only process if enabled
    if (!replacement.isEnabled)
    {
        return;
    }

    std::set<std::string> wildCardValues;
    // Compile regex once instead of recreating it in replaceNumber
    std::regex pattern(replacement.searchPattern + "(\\d+)");

    for (const auto& property : inputMetricProperties)
    {
        replaceNumber(property, replacement.searchPattern, pattern,
                      replacement.wildcardPattern, wildCardValues);
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
}

inline void getShmemMetricsDefinitionWildCard(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& metricId, const std::string& deviceType)
{
    BMCWEB_LOG_DEBUG("getShmemMetricsDefinitionWildCards :{}", metricId);
    BMCWEB_LOG_INFO("deviceType: {}", deviceType);
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
    asyncResp->res.jsonValue["Wildcards"] = nlohmann::json::array();

    try
    {
#ifndef NVIDIA_HAVE_TAL
        BMCWEB_LOG_CRITICAL("Attempt to access tal but not available");
        return;
#else
        nlohmann::json& wildCards = asyncResp->res.jsonValue["Wildcards"];
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
            updateReplacementFlag(gpuSmaPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(pmSmaPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(gpuTempPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(hsccPlatformEnvironmentMetrics,
                                  allowedWildcards);

            metricsReplacements(chassisPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(processorPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(cpuPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(fpgaPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(gpuPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(nvSwitchPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(pcieRetimerPlatformEnvironmentMetrics,
                                wildCards, inputMetricProperties);
            metricsReplacements(pcieSwitchPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(nvLinkManagementNICPlatformEnvironmentMetrics,
                                wildCards, inputMetricProperties);
            metricsReplacements(
                nvLinkManagementNICPortPlatformEnvironmentMetrics, wildCards,
                inputMetricProperties);
            metricsReplacements(ioBoardPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(pdbPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(blueFieldPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(blueFieldSensorsPlatformEnvironmentMetrics,
                                wildCards, inputMetricProperties);
            metricsReplacements(storageBPSensorsPlatformEnvironmentMetrics,
                                wildCards, inputMetricProperties);
            metricsReplacements(storageBPDevicePlatformEnvironmentMetrics,
                                wildCards, inputMetricProperties);
            metricsReplacements(inletPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(pcbPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(hscPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(sxmPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(connectXPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(sxmSmaPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(cxSmaPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(gpuSmaPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(pmSmaPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(gpuTempPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(hsccPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
        }
        else
        {
            metricsReplacementsNonPlatformMetrics(
                asyncResp, inputMetricProperties, deviceType, allowedWildcards);
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
    BMCWEB_LOG_DEBUG("Exception while getShmemMetricsReportDefinition");
    BMCWEB_LOG_DEBUG("getShmemMetricsReportCollection: {}", reportType);
    try
    {
#ifndef NVIDIA_HAVE_TAL
        BMCWEB_LOG_CRITICAL("Attempt to access tal but not available");
        return;
#else
        nlohmann::json& addMembers = asyncResp->res.jsonValue["Members"];
#ifdef NVIDIA_HAVE_TAL
        const auto& values = tal::TelemetryAggregator::getMrdNamespaces();

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
#endif
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
