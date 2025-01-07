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

#include "shmem_utils.hpp"
#include "utils/file_utils.hpp"

namespace redfish
{
namespace nvidia_metric_report_def_utils
{

constexpr const char* metricReportDefinitionUri =
    "/redfish/v1/TelemetryService/MetricReportDefinitions";

constexpr const char* metricReportUri =
    "/redfish/v1/TelemetryService/MetricReports";

constexpr const char* mrdConfigFile = "/usr/share/bmcweb/";

inline int mrdConfigRead(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& deviceType, const std::string& id)
{
    asyncResp->res.jsonValue["@odata.id"] =
        metricReportDefinitionUri + std::string("/") + id;
    asyncResp->res.jsonValue["@odata.type"] =
        "#MetricReportDefinition.v1_4_1.MetricReportDefinition";
    asyncResp->res.jsonValue["Id"] = id;
    asyncResp->res.jsonValue["Name"] = id;
    asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
    asyncResp->res.jsonValue["MetricReportDefinitionType"] = "OnRequest";
    std::vector<std::string> redfishReportActions;
    redfishReportActions.emplace_back("LogToMetricReportsCollection");
    asyncResp->res.jsonValue["ReportActions"] = redfishReportActions;
    asyncResp->res.jsonValue["ReportUpdates"] = "Overwrite";
    asyncResp->res.jsonValue["MetricReport"]["@odata.id"] =
        metricReportUri + std::string("/") + id;
    std::string mrdConfigPath = mrdConfigFile;
    mrdConfigPath += std::string("mrd_");
    mrdConfigPath += deviceType;
    mrdConfigPath += std::string(".json");
    nlohmann::json jStatus = nlohmann::json::object();
    int rc = file_utils::readJsonFromFile(mrdConfigPath, jStatus);
    if (rc == 0)
    {
        auto jMsgNamespace = jStatus.find("Namespace");
        std::string msgNamespace = *jMsgNamespace;
        if (jMsgNamespace != jStatus.end() && msgNamespace == deviceType)
        {
            auto jMsgMetricProperties = jStatus.find("MetricProperties");
            if (jMsgMetricProperties != jStatus.end())
            {
                asyncResp->res.jsonValue["MetricProperties"] =
                    *jMsgMetricProperties;
            }
        }
    }
    return rc;
}

inline std::string metricId(const std::string& input,
                            const std::string& platformDevicePrefix)
{
    std::string result = input;

    // Check if the input string starts with the platformDevicePrefix
    if (result.find(platformDevicePrefix) == 0)
    {
        // Remove the platformDevicePrefix from the input string
        result = result.substr(platformDevicePrefix.length());
    }

    // Find the position of the underscore
    size_t underscorePos = result.find('_');
    if (underscorePos != std::string::npos)
    {
        // Extract the part before the underscore
        result = result.substr(0, underscorePos);
    }
    return result;
}

inline void validateAndGetMetricReportDefinition(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const std::string& id)
{
    if constexpr (BMCWEB_SHMEM_PLATFORM_METRICS)
    {
        static std::string platformDevicePrefix(BMCWEB_PLATFORM_DEVICE_PREFIX);
        std::string deviceType = metricId(id, platformDevicePrefix);
        int rc = mrdConfigRead(asyncResp, deviceType, id);
        if (rc != 0)
        {
            messages::resourceNotFound(
                asyncResp->res,
                "MetricReportDefinition Config File Not Present", id);
            return;
        }
        redfish::shmem::getShmemMetricsDefinitionWildCard(asyncResp, id,
                                                          deviceType);
        return;
    }
    else
    {
        messages::resourceNotFound(asyncResp->res,
                                   "Enable Shmem to get the MRD", id);
        return;
    }
}

inline void getMetricReportCollection(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if constexpr (BMCWEB_SHMEM_PLATFORM_METRICS)
    {
        redfish::shmem::getShmemMetricsReportCollection(
            asyncResp, "MetricReportDefinitions");
        return;
    }
    else
    {
        messages::resourceNotFound(asyncResp->res,
                                   "Enable Shmem to get the MRD",
                                   "MetricReportDefinitions");
        return;
    }
}
} // namespace nvidia_metric_report_def_utils
} // namespace redfish
