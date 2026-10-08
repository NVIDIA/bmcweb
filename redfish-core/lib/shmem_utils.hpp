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
#include "utils/hex_utils.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace redfish
{
namespace shmem
{

struct MetricsReplacement
{
    std::string searchPattern;   // The pattern to search for (e.g. chassisName)
    std::string alternateSearchPattern;
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

    MetricsReplacement(
        const std::array<std::string_view, 2>& searches,
        std::string_view pattern, std::string_view name,
                       bool enabled = false) :
        searchPattern(searches.front()),
        alternateSearchPattern(searches.back()),
        wildcardPattern(pattern), wildcardName(name), isEnabled(enabled)
    {}
    // clang-format on
};

inline void updateReplacementFlag(const MetricsReplacement& replacement,
                                  const std::set<std::string>& allowedWildcards)
{
    replacement.isEnabled =
        (allowedWildcards.contains(replacement.wildcardName));
}

// MGX_PlatformEnvironmentMetrics_0 (switch) gets no Nvidia OEM block.
inline bool isOemPlatformMetricsReport(std::string_view metricId)
{
    return metricId == "PlatformEnvironmentMetrics_0" ||
           metricId == "HGX_PlatformEnvironmentMetrics_0";
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

        if (isOemPlatformMetricsReport(metricId))
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

            if (isOemPlatformMetricsReport(metricId))
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
// Matched as substrings, so they also match platform-prefixed
// names such as HGX_GPU_SXM_1.
const static std::string chassisName = "Chassis_";
const static std::string fpgaChassiName = "FPGA_";
const static std::string gpuName = gpuPrefix;
const static std::string nvSwitch = "NVSwitch_";
const static std::string pcieRetimer = "PCIeRetimer_";
const static std::string pcieSwtich = "PCIeSwitch_";
const static std::string processorModule = "ProcessorModule_";
const static std::string cpu = "CPU_";
constexpr const std::string_view nvLink = "NVLink_";
constexpr const std::string_view cLink = "CLink_";
constexpr std::array<std::string_view, 3> nvLinkTypes = {
    "NVLink", "InterswitchPort", "NVLinkManagement"};
constexpr const std::string_view nvSwitchValue = "NVSwitch_";
constexpr const std::string_view cpuProcessor = "CPU_";
constexpr const std::string_view processor = "ProcessorModule_";
constexpr const std::string_view pcieLink = "PCIeLink_";
constexpr const std::string_view cpuCore = "CoreUtil_";
constexpr const std::string_view networkAdapter = "NIC_";
constexpr std::array<std::string_view, 2> networkAdapterLink = {
    "Port_", "NVLinkManagement_"};
constexpr const std::string_view gpmInstances = "UtilizationPercent/";
constexpr const std::string_view nvLinkManagementNIC = "NIC_";
constexpr const std::string_view nvLinkManagementNICPort = "Port_";
constexpr const std::string_view retimer = "PCIeRetimer_";
constexpr const std::string_view ioBoard = "IO_Board_";
constexpr const std::string_view pdb = "PDB_";
constexpr const std::string_view blueField = "Riser_Slot";
constexpr const std::string_view bf3Sensor = "BF3_Slot_";
constexpr const std::string_view blueFieldSensor = "BlueField_";
constexpr const std::string_view storageBP = "StorageBackplane_";
constexpr const std::string_view storageDevice = "SSD_";
constexpr const std::string_view networkAdapterConnectX = "ConnectX_";
constexpr std::array<std::string_view, 2> networkAdapterCX = {"CX_", "CX_NIC_"};
constexpr const std::string_view inlet = "Chassis_0_Inlet_";
constexpr const std::string_view pcb = "Chassis_0_PCB_";
constexpr const std::string_view hsc = "Chassis_0_HSC_";
constexpr const std::string_view sxm = "GPU_SXM_";
constexpr const std::string_view sxmSma = "SXM_SMA_";
constexpr const std::string_view ioSma = "IO_Board_SMA_";
constexpr const std::string_view cxSma = "ConnectX_SMA_";
constexpr const std::string_view gpuSma = "GPU_SMA_";
constexpr const std::string_view pmSma = "ProcessorModule_SMA_";
constexpr const std::string_view nvSwitchSma = "NVSwitch_SMA_";
constexpr const std::string_view gpuTemp = "GPU_\\d+_TEMP_";
constexpr const std::string_view fanBoardFan = "FanBoard_\\d+_Fan_";
constexpr const std::string_view psu = "PSU_";
constexpr const std::string_view psuTemp = "PSU_\\d+_Temp_";
constexpr const std::string_view hscc = "Chassis_0_HSCC_";
constexpr const std::string_view dramTemp = "DramTemp_";

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
inline const MetricsReplacement bf3SensorsPlatformEnvironmentMetrics(
    bf3Sensor, "{BF3SWild}", "BF3SWild");
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
inline const MetricsReplacement cxPlatformEnvironmentMetrics(
    networkAdapterCX, "{CNXWild}", "CNXWild");
inline const MetricsReplacement sxmSmaPlatformEnvironmentMetrics(
    sxmSma, "{SSMAWild}", "SSMAWild");
inline const MetricsReplacement ioSmaPlatformEnvironmentMetrics(
    ioSma, "{ISMAWild}", "ISMAWild");
inline const MetricsReplacement cxSmaPlatformEnvironmentMetrics(
    cxSma, "{CSMAWild}", "CSMAWild");
inline const MetricsReplacement gpuSmaPlatformEnvironmentMetrics(
    gpuSma, "{GSMAWild}", "GSMAWild");
inline const MetricsReplacement pmSmaPlatformEnvironmentMetrics(
    pmSma, "{PSMAWild}", "PSMAWild");
inline const MetricsReplacement nvSwitchSmaPlatformEnvironmentMetrics(
    nvSwitchSma, "{NSMAWild}", "NSMAWild");
inline const MetricsReplacement gpuTempPlatformEnvironmentMetrics(
    gpuTemp, "{GTWild}", "GTWild");
inline const MetricsReplacement hsccPlatformEnvironmentMetrics(hscc, "{HCWild}",
                                                               "HCWild");
inline const MetricsReplacement memDramTempPlatformEnvironmentMetrics(
    dramTemp, "{MWild}", "MWild");
inline const MetricsReplacement fanPlatformEnvironmentMetrics(
    fanBoardFan, "{FanWild}", "FanWild");
inline const MetricsReplacement psuPlatformEnvironmentMetrics(psu, "{PsuId}",
                                                              "PsuId");
inline const MetricsReplacement psuTempPlatformEnvironmentMetrics(
    psuTemp, "{PsuTempId}", "PsuTempId");

struct NumberMatch
{
    size_t numberBegin;
    size_t numberEnd;
};

inline bool isAsciiDigit(char value)
{
    return value >= '0' && value <= '9';
}

inline size_t findDigitEnd(std::string_view input, size_t begin)
{
    size_t end = begin;
    while (end < input.size() && isAsciiDigit(input[end]))
    {
        end++;
    }
    return end;
}

// MetricsReplacement::searchPattern is normally a literal prefix. The GPU
// temperature pattern additionally contains a "\\d+" token. Match that small,
// well-defined pattern language without constructing a regular expression.
inline std::optional<NumberMatch> findNumberAfterPattern(
    std::string_view input, std::string_view pattern)
{
    constexpr std::string_view digitToken = "\\d+";
    size_t firstToken = pattern.find(digitToken);
    if (firstToken == std::string_view::npos)
    {
        size_t patternPos = input.find(pattern);
        while (patternPos != std::string_view::npos)
        {
            size_t numberBegin = patternPos + pattern.size();
            size_t numberEnd = findDigitEnd(input, numberBegin);
            if (numberEnd > numberBegin)
            {
                return NumberMatch{numberBegin, numberEnd};
            }
            patternPos = input.find(pattern, patternPos + 1);
        }
        return std::nullopt;
    }

    std::string_view prefix = pattern.substr(0, firstToken);
    size_t matchBegin = input.find(prefix);
    while (matchBegin != std::string_view::npos)
    {
        size_t inputPos = matchBegin + prefix.size();
        size_t patternPos = firstToken;
        bool matched = true;
        while (patternPos < pattern.size())
        {
            if (pattern.substr(patternPos).starts_with(digitToken))
            {
                size_t digitEnd = findDigitEnd(input, inputPos);
                if (digitEnd == inputPos)
                {
                    matched = false;
                    break;
                }
                inputPos = digitEnd;
                patternPos += digitToken.size();
                continue;
            }

            size_t nextToken = pattern.find(digitToken, patternPos);
            size_t literalEnd = nextToken == std::string_view::npos
                                    ? pattern.size()
                                    : nextToken;
            std::string_view literal =
                pattern.substr(patternPos, literalEnd - patternPos);
            if (!input.substr(inputPos).starts_with(literal))
            {
                matched = false;
                break;
            }
            inputPos += literal.size();
            patternPos = literalEnd;
        }

        if (matched)
        {
            size_t numberEnd = findDigitEnd(input, inputPos);
            if (numberEnd > inputPos)
            {
                return NumberMatch{inputPos, numberEnd};
            }
        }
        matchBegin = input.find(prefix, matchBegin + 1);
    }
    return std::nullopt;
}

inline void insertNumberAfterPattern(
    std::string_view input, std::string_view pattern, std::set<int64_t>& values)
{
    std::optional<NumberMatch> match = findNumberAfterPattern(input, pattern);
    if (!match)
    {
        return;
    }
    values.insert(
        stringToInt64(input.substr(match->numberBegin,
                                   match->numberEnd - match->numberBegin))
            .value_or(0));
}

inline void insertNumberAfterPattern(
    std::string_view input, const std::array<std::string_view, 2>& patterns,
    std::set<int64_t>& values)
{
    size_t bestPosition = std::string_view::npos;
    std::optional<NumberMatch> bestMatch;
    for (std::string_view pattern : patterns)
    {
        std::optional<NumberMatch> match =
            findNumberAfterPattern(input, pattern);
        if (match && match->numberBegin - pattern.size() < bestPosition)
        {
            bestPosition = match->numberBegin - pattern.size();
            bestMatch = match;
        }
    }
    if (bestMatch)
    {
        values.insert(
            stringToInt64(
                input.substr(bestMatch->numberBegin,
                             bestMatch->numberEnd - bestMatch->numberBegin))
                .value_or(0));
    }
}

struct TypeNumberMatch
{
    std::string_view type;
    int64_t id;
};

inline std::optional<TypeNumberMatch> findNvLinkTypeAndId(
    std::string_view input)
{
    size_t bestPosition = std::string_view::npos;
    std::optional<TypeNumberMatch> bestMatch;
    for (std::string_view type : nvLinkTypes)
    {
        size_t position = input.find(type);
        while (position != std::string_view::npos)
        {
            size_t separator = position + type.size();
            if (separator < input.size() && input[separator] == '_')
            {
                size_t numberBegin = separator + 1;
                size_t numberEnd = findDigitEnd(input, numberBegin);
                if (numberEnd > numberBegin && position < bestPosition)
                {
                    bestPosition = position;
                    bestMatch = TypeNumberMatch{
                        type,
                        stringToInt64(
                            input.substr(numberBegin, numberEnd - numberBegin))
                            .value_or(0)};
                }
            }
            position = input.find(type, position + 1);
        }
    }
    return bestMatch;
}

struct PortMatch
{
    std::string_view type;
    int64_t id;
};

inline bool isWordCharacter(char value)
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           isAsciiDigit(value) || value == '_';
}

inline std::optional<PortMatch> findPort(std::string_view input)
{
    constexpr std::string_view portsPrefix = "/Ports/";
    size_t prefixPos = input.find(portsPrefix);
    while (prefixPos != std::string_view::npos)
    {
        size_t typeBegin = prefixPos + portsPrefix.size();
        size_t wordEnd = typeBegin;
        while (wordEnd < input.size() && isWordCharacter(input[wordEnd]))
        {
            wordEnd++;
        }

        size_t separator = wordEnd;
        while (separator > typeBegin)
        {
            separator = input.rfind('_', separator - 1);
            if (separator == std::string_view::npos || separator < typeBegin)
            {
                break;
            }
            size_t numberBegin = separator + 1;
            size_t numberEnd = findDigitEnd(input, numberBegin);
            if (separator == typeBegin || numberEnd == numberBegin)
            {
                continue;
            }

            return PortMatch{
                input.substr(typeBegin, separator - typeBegin),
                stringToInt64(
                    input.substr(numberBegin, numberEnd - numberBegin))
                    .value_or(0)};
        }
        prefixPos = input.find(portsPrefix, typeBegin);
    }
    return std::nullopt;
}

inline void replaceNumber(std::string_view input, std::string_view key,
                          std::string_view value,
                          std::set<std::string>& replacedName)
{
    std::optional<NumberMatch> match = findNumberAfterPattern(input, key);
    if (!match)
    {
        return;
    }

    if (value == "{BSWild}" || value == "{PDBWild}" || value == "{BFSWild}")
    {
        size_t lastSlashPos = input.find_last_of('/');
        if (lastSlashPos != std::string_view::npos)
        {
            replacedName.emplace(input.substr(lastSlashPos + 1));
        }
    }
    else if (value == "{BF3SWild}")
    {
        size_t lastSlashPos = input.find_last_of('/');
        if (lastSlashPos == std::string_view::npos)
        {
            return;
        }
        std::string_view name = input.substr(lastSlashPos + 1);
        match = findNumberAfterPattern(name, key);
        if (match)
        {
            std::string wildName(key);
            wildName += "{BFWild}";
            wildName += name.substr(match->numberEnd);
            replacedName.insert(std::move(wildName));
        }
    }
    else
    {
        replacedName.emplace(input.substr(
            match->numberBegin, match->numberEnd - match->numberBegin));
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
    bool allowCLinkId = (wildcardSet.contains("CLinkId"));
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

    std::set<int64_t> nvSwitchIdType1;
    std::set<int64_t> nvlinkIdType1;
    std::set<std::string> nvlinkType1;
    std::set<int64_t> gpuId;
    std::set<int64_t> gpmInstance;
    std::set<int64_t> networkAdapterNId;
    std::set<int64_t> nvLinkManagementId;
    std::set<int64_t> retimerId;
    std::set<std::string> portTypes;
    std::set<int64_t> portIds;
    std::set<int64_t> cpuId;
    std::set<int64_t> processorId;
    std::set<int64_t> coreId;
    std::set<int64_t> nvLinkId;
    std::set<int64_t> cLinkId;
    std::set<int64_t> pcieLinkId;
    std::set<int64_t> networkAdapterCXId;
    nlohmann::json& wildCards = asyncResp->res.jsonValue["Wildcards"];
    for (const auto& property : inputMetricProperties)
    {
        if (deviceType == "NVSwitchPortMetrics")
        {
            if (allowNVSwitchId)
            {
                insertNumberAfterPattern(property, nvSwitchValue,
                                         nvSwitchIdType1);
            }
            if (allowLinkType || allowNvlinkId)
            {
                std::optional<TypeNumberMatch> link =
                    findNvLinkTypeAndId(property);
                if (link)
                {
                    if (allowLinkType)
                    {
                        nvlinkType1.emplace(link->type);
                    }
                    if (allowNvlinkId)
                    {
                        nvlinkIdType1.insert(link->id);
                    }
                }
            }
        }
        if (deviceType == "NVSwitchMetrics")
        {
            if (allowNVSwitchId)
            {
                insertNumberAfterPattern(property, nvSwitchValue,
                                         nvSwitchIdType1);
            }
        }
        if (deviceType == "PCIeRetimerMetrics")
        {
            if (allowRetimerId)
            {
                insertNumberAfterPattern(property, retimer, retimerId);
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
                insertNumberAfterPattern(property, gpuPrefix, gpuId);
            }
        }
        if (deviceType == "ProcessorGPMMetrics")
        {
            if (allowInstanceId)
            {
                insertNumberAfterPattern(property, gpmInstances, gpmInstance);
            }
        }
        if (deviceType == "ProcessorPortMetrics" ||
            deviceType == "ProcessorPortGPMMetrics")
        {
            if (allowNvlinkId)
            {
                insertNumberAfterPattern(property, nvLink, nvlinkIdType1);
            }
        }
        if (deviceType == "NetworkAdapterPortMetrics")
        {
            if (allowNetworkAdapterNId)
            {
                insertNumberAfterPattern(property, networkAdapter,
                                         networkAdapterNId);
            }
            if (allowNvlinkId)
            {
                insertNumberAfterPattern(property, networkAdapterLink,
                                         nvLinkManagementId);
            }
            if (allowNetworkAdapterCXId)
            {
                insertNumberAfterPattern(property, networkAdapterConnectX,
                                         networkAdapterCXId);
            }
        }
        if (deviceType == "PCIeRetimerPortMetrics")
        {
            if (allowRetimerId)
            {
                insertNumberAfterPattern(property, retimer, retimerId);
            }
            if (allowPortType && allowPortId)
            {
                std::optional<PortMatch> port = findPort(property);
                if (port)
                {
                    portTypes.emplace(port->type);
                    portIds.insert(port->id);
                }
            }
        }
        if (deviceType == "CpuProcessorMetrics")
        {
            if (allowCpuId)
            {
                insertNumberAfterPattern(property, cpuProcessor, cpuId);
            }
            if (allowProcessorId)
            {
                insertNumberAfterPattern(property, processor, processorId);
            }
            if (allowCoreId)
            {
                insertNumberAfterPattern(property, cpuCore, coreId);
            }
            if (allowNvlinkId)
            {
                insertNumberAfterPattern(property, nvLink, nvLinkId);
            }
            if (allowCLinkId)
            {
                insertNumberAfterPattern(property, cLink, cLinkId);
            }
            if (allowPCIeLinkId)
            {
                insertNumberAfterPattern(property, pcieLink, pcieLinkId);
            }
        }
        if (deviceType == "HealthMetrics")
        {
            if (allowCpuId)
            {
                insertNumberAfterPattern(property, cpuProcessor, cpuId);
            }
            if (allowGpuId)
            {
                insertNumberAfterPattern(property, gpuPrefix, gpuId);
            }
            if (allowProcessorId)
            {
                insertNumberAfterPattern(property, processor, processorId);
            }
            if (allowRetimerId)
            {
                insertNumberAfterPattern(property, retimer, retimerId);
            }
            if (allowNVSwitchId)
            {
                insertNumberAfterPattern(property, nvSwitchValue,
                                         nvSwitchIdType1);
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
    }
    if (deviceType == "CpuProcessorMetrics")
    {
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
        if (allowCLinkId)
        {
            nlohmann::json devCountCLinkId = nlohmann::json::array();
            for (const auto& e : cLinkId)
            {
                devCountCLinkId.push_back(std::to_string(e));
            }
            wildCards.push_back({
                {"Name", "CLinkId"},
                {"Values", devCountCLinkId},
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

    for (const auto& property : inputMetricProperties)
    {
        replaceNumber(property, replacement.searchPattern,
                      replacement.wildcardPattern, wildCardValues);
        if (!replacement.alternateSearchPattern.empty())
        {
            replaceNumber(property, replacement.alternateSearchPattern,
                          replacement.wildcardPattern, wildCardValues);
        }
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
            const std::string& propertyStr =
                property.get_ref<const std::string&>();
            size_t tokenBegin = propertyStr.find('{');
            while (tokenBegin != std::string::npos)
            {
                size_t tokenEnd = propertyStr.find('}', tokenBegin + 1);
                if (tokenEnd == std::string::npos)
                {
                    break;
                }
                size_t nestedTokenBegin = propertyStr.find('{', tokenBegin + 1);
                if (tokenEnd > tokenBegin + 1 &&
                    (nestedTokenBegin == std::string::npos ||
                     nestedTokenBegin > tokenEnd))
                {
                    allowedWildcards.emplace(propertyStr, tokenBegin + 1,
                                             tokenEnd - tokenBegin - 1);
                }
                tokenBegin = propertyStr.find('{', tokenEnd + 1);
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
            updateReplacementFlag(bf3SensorsPlatformEnvironmentMetrics,
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
            updateReplacementFlag(cxPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(sxmSmaPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(ioSmaPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(cxSmaPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(gpuSmaPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(pmSmaPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(nvSwitchSmaPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(gpuTempPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(hsccPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(memDramTempPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(fanPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(psuPlatformEnvironmentMetrics,
                                  allowedWildcards);
            updateReplacementFlag(psuTempPlatformEnvironmentMetrics,
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
            metricsReplacements(bf3SensorsPlatformEnvironmentMetrics, wildCards,
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
            metricsReplacements(cxPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(sxmSmaPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(ioSmaPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(cxSmaPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(gpuSmaPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(pmSmaPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(nvSwitchSmaPlatformEnvironmentMetrics,
                                wildCards, inputMetricProperties);
            metricsReplacements(gpuTempPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(hsccPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(memDramTempPlatformEnvironmentMetrics,
                                wildCards, inputMetricProperties);
            metricsReplacements(fanPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(psuPlatformEnvironmentMetrics, wildCards,
                                inputMetricProperties);
            metricsReplacements(psuTempPlatformEnvironmentMetrics, wildCards,
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
