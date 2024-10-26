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
/*!
 * @file    conditions_utils.cpp
 * @brief   Source code for utility functions of handling service conditions.
 */

#pragma once

#include <utils/dbus_log_utils.hpp>
#include <utils/file_utils.hpp>
#include <utils/origin_utils.hpp>
#include <utils/registry_utils.hpp>
#include <utils/time_utils.hpp>

namespace redfish
{
namespace conditions_utils
{

inline void handleDeviceServiceConditions(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp{asyncResp}, chassisId(std::string(chassisId))](
            const boost::system::error_code ec,
            const dbus::utility::ManagedObjectType& resp) {
        if (ec)
        {
            messages::internalError(asyncResp->res);
            BMCWEB_LOG_ERROR("getLogEntriesIfaceData resp_handler got error {}",
                             ec);
            return;
        }

        const uint32_t* id = nullptr;
        const std::string* message = nullptr;
        const std::string* severity = nullptr;
        const std::vector<std::string>* additionalDataRaw = nullptr;
        const std::string prefix = "xyz.openbmc_project.Logging.Entry.Level.";
        const std::string criticalSev = prefix + "Critical";
        const std::string warningSev = prefix + "Warning";
        std::time_t timestamp{};

        for (auto& objectPath : resp)
        {
            additionalDataRaw = nullptr;
            for (auto& interfaceMap : objectPath.second)
            {
                if (interfaceMap.first == "xyz.openbmc_project.Logging.Entry")
                {
                    for (auto& propertyMap : interfaceMap.second)
                    {
                        if (propertyMap.first == "Id")
                        {
                            id = std::get_if<uint32_t>(&propertyMap.second);
                        }
                        else if (propertyMap.first == "Severity")
                        {
                            severity =
                                std::get_if<std::string>(&propertyMap.second);
                        }
                        else if (propertyMap.first == "Message")
                        {
                            message =
                                std::get_if<std::string>(&propertyMap.second);
                        }
                        else if (propertyMap.first == "AdditionalData")
                        {
                            additionalDataRaw =
                                std::get_if<std::vector<std::string>>(
                                    &propertyMap.second);
                        }
                        else if (propertyMap.first == "Timestamp")
                        {
                            const uint64_t* millisTimeStamp =
                                std::get_if<uint64_t>(&propertyMap.second);
                            if (millisTimeStamp != nullptr)
                            {
                                timestamp = redfish::time_utils::getTimestamp(
                                    *millisTimeStamp);
                            }
                        }
                    }
                    if (id == nullptr || message == nullptr ||
                        severity == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "id, message, severity of log entry is null");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    break;
                }
            }
            std::string originOfCondition;
            std::string messageArgs;
            std::string messageId;
            std::string deviceName;

            if (additionalDataRaw != nullptr)
            {
                AdditionalData additional(*additionalDataRaw);
                if (additional.count("REDFISH_ORIGIN_OF_CONDITION") > 0)
                {
                    originOfCondition =
                        additional["REDFISH_ORIGIN_OF_CONDITION"];
                }
                if (additional.count("REDFISH_MESSAGE_ARGS") > 0)
                {
                    messageArgs = additional["REDFISH_MESSAGE_ARGS"];
                }
                if (additional.count("REDFISH_MESSAGE_ID") > 0)
                {
                    messageId = additional["REDFISH_MESSAGE_ID"];
                }
                if (additional.count("DEVICE_NAME") > 0)
                {
                    deviceName = additional["DEVICE_NAME"];
                }
                if ((*severity == criticalSev || *severity == warningSev) &&
                    messageArgs.find(chassisId) != std::string::npos)
                {
                    origin_utils::convertDbusObjectToOriginOfCondition(
                        originOfCondition, std::to_string(*id), asyncResp,
                        asyncResp->res.jsonValue, deviceName,
                        (*severity).substr(prefix.length()), messageArgs,
                        redfish::time_utils::getDateTimeStdtime(timestamp),
                        messageId);
                }
            }
        }
    },
        "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
        "xyz.openbmc_project.Logging.Namespace", "GetAll", chassisId,
        "xyz.openbmc_project.Logging.Namespace.ResolvedFilterType.Unresolved");
}

inline void handleServiceConditionsURI(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp{asyncResp}](const boost::system::error_code ec,
                               const dbus::utility::ManagedObjectType& resp) {
        if (ec)
        {
            messages::internalError(asyncResp->res);
            BMCWEB_LOG_ERROR("getLogEntriesIfaceData resp_handler got error {}",
                             ec);
            return;
        }
        const std::map<std::string, int> severityMap = {
            {"OK", 1}, {"Warning", 2}, {"Critical", 3}};
        const uint32_t* id = nullptr;
        const std::string* message = nullptr;
        const std::string* severity = nullptr;
        const std::vector<std::string>* additionalDataRaw = nullptr;
        const std::string prefix = "xyz.openbmc_project.Logging.Entry.Level.";
        const std::string criticalSev = prefix + "Critical";
        const std::string warningSev = prefix + "Warning";
        std::time_t timestamp{};
        for (auto& objectPath : resp)
        {
            additionalDataRaw = nullptr;
            for (auto& interfaceMap : objectPath.second)
            {
                if (interfaceMap.first == "xyz.openbmc_project.Logging.Entry")
                {
                    for (auto& propertyMap : interfaceMap.second)
                    {
                        if (propertyMap.first == "Id")
                        {
                            id = std::get_if<uint32_t>(&propertyMap.second);
                        }
                        else if (propertyMap.first == "Severity")
                        {
                            severity =
                                std::get_if<std::string>(&propertyMap.second);
                        }
                        else if (propertyMap.first == "Message")
                        {
                            message =
                                std::get_if<std::string>(&propertyMap.second);
                        }
                        else if (propertyMap.first == "AdditionalData")
                        {
                            additionalDataRaw =
                                std::get_if<std::vector<std::string>>(
                                    &propertyMap.second);
                        }
                        else if (propertyMap.first == "Timestamp")
                        {
                            const uint64_t* millisTimeStamp =
                                std::get_if<uint64_t>(&propertyMap.second);
                            if (millisTimeStamp != nullptr)
                            {
                                timestamp = redfish::time_utils::getTimestamp(
                                    *millisTimeStamp);
                            }
                        }
                    }
                    if (id == nullptr || message == nullptr ||
                        severity == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "id, message, severity of log entry is null");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    break;
                }
            }
            std::string originOfCondition;
            std::string messageArgs;
            std::string messageId;
            std::string deviceName;

            if (additionalDataRaw != nullptr)
            {
                AdditionalData additional(*additionalDataRaw);
                if (additional.count("REDFISH_ORIGIN_OF_CONDITION") > 0)
                {
                    originOfCondition =
                        additional["REDFISH_ORIGIN_OF_CONDITION"];
                }
                if (additional.count("REDFISH_MESSAGE_ARGS") > 0)
                {
                    messageArgs = additional["REDFISH_MESSAGE_ARGS"];
                }
                if (additional.count("REDFISH_MESSAGE_ID") > 0)
                {
                    messageId = additional["REDFISH_MESSAGE_ID"];
                }
                if (additional.count("DEVICE_NAME") > 0)
                {
                    deviceName = additional["DEVICE_NAME"];
                }
                if (*severity == criticalSev || *severity == warningSev)
                {
                    origin_utils::convertDbusObjectToOriginOfCondition(
                        originOfCondition, std::to_string(*id), asyncResp,
                        asyncResp->res.jsonValue, deviceName,
                        (*severity).substr(prefix.length()), messageArgs,
                        redfish::time_utils::getDateTimeStdtime(timestamp),
                        messageId);

                    std::string sev = (*severity).substr(prefix.length());
                    std::string currSev =
                        asyncResp->res.jsonValue["HealthRollup"];

                    if (severityMap.at(sev) > severityMap.at(currSev))
                    {
                        asyncResp->res.jsonValue["HealthRollup"] = sev;
                    }
                }
            }
        }
        if (asyncResp->res.jsonValue["Conditions"].size() == 0)
        {
            asyncResp->res.jsonValue["HealthRollup"] = "OK";
        }
    },
        "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
        "xyz.openbmc_project.Logging.Namespace", "GetAll", "Namespace.All",
        "xyz.openbmc_project.Logging.Namespace.ResolvedFilterType.Unresolved");
}

/** NOTES: This is a temporary solution to avoid performance issues may impact
 *  other Redfish services. Please call for architecture decisions from all
 *  NvBMC teams if want to use it in other places.
 */
inline void handleDeviceServiceConditionsFromFile(crow::Response& resp,
                                                  const std::string& deviceId)
{
    static const std::string deviceStatusFSPath = bmcwebDeviceStatusFSPath;

    std::string deviceStatusPath = deviceStatusFSPath + "/" + deviceId;

    nlohmann::json jStatus{};

    int rc = file_utils::readFile2Json(deviceStatusPath, jStatus);
    if (rc != 0)
    {
        BMCWEB_LOG_ERROR("Condtions: read {} status file failed!", deviceId);
        // No need to report error since no status file means device is OK.
        return;
    }

    auto jSts = jStatus.find("Status");
    if (jSts == jStatus.end())
    {
        BMCWEB_LOG_ERROR("Condtions: No Status in status file of {}!",
                         deviceId);
        messages::internalError(resp);
        return;
    }

    auto jCond = jSts->find("Conditions");
    if (jCond == jSts->end())
    {
        BMCWEB_LOG_ERROR("Condtions: No Conditions in status file of {}!",
                         deviceId);
        messages::internalError(resp);
        return;
    }

    for (auto& j : *jCond)
    {
        nlohmann::json conditionResp{};
        conditionResp["@odata.type"] = "#LogEntry.v1_13_0.LogEntry";

        // Support both MessageRegistry or non-MessageRegitry formats
        auto jMsgId = j.find("MessageId");
        auto jMsgArgs = j.find("MessageArgs");
        if (jMsgId != j.end() && jMsgArgs != j.end())
        {
            // MessageRegistry Format
            std::string messageId = *jMsgId;
            std::string message = message_registries::composeMessage(*jMsgId,
                                                                     *jMsgArgs);

            conditionResp["MessageId"] = messageId;
            conditionResp["MessageArgs"] = *jMsgArgs;
            conditionResp["Message"] = message;
        }
        else
        {
            // Non-MessageRegistry Format
            auto jMsg = j.find("Message");
            if (jMsg != j.end())
            {
                conditionResp["Message"] = *jMsg;
            }
        }

        auto jOOC = j.find("OriginOfCondition");
        if (jOOC != j.end())
        {
            std::string ooc = *jOOC;
            std::string originOfCondition =
                origin_utils::getDeviceRedfishURI(ooc);

            if (originOfCondition.length() == 0)
            {
                BMCWEB_LOG_ERROR("getDeviceRedfishURI of {} failed!", ooc);
            }
            else
            {
                BMCWEB_LOG_DEBUG("Get {} OriginOfCondition {}!", deviceId,
                                 originOfCondition);
                conditionResp["OriginOfCondition"]["@odata.id"] =
                    originOfCondition;
            }
        }

        if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
        {
            auto jDevice = j.find("Device");
            if (jDevice != j.end())
            {
                std::string device = *jDevice;
                conditionResp["Oem"]["Nvidia"]["Device"] = device;
            }

            auto jErrorId = j.find("ErrorId");
            if (jErrorId != j.end())
            {
                std::string errorId = *jErrorId;
                conditionResp["Oem"]["Nvidia"]["ErrorId"] = errorId;
            }

            // If Device or ErrorId exists,
            if (conditionResp.contains("Oem") &&
                conditionResp["Oem"].contains("Nvidia"))
            {
                conditionResp["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaLogEntry.v1_1_0.NvidiaLogEntry";
            }
        }
        auto jResolution = j.find("Resolution");
        if (jResolution != j.end())
        {
            std::string resolution = *jResolution;
            if (resolution.length() == 0)
            {
                BMCWEB_LOG_ERROR("Get {} Resolution failed!", deviceId);
            }
            else
            {
                BMCWEB_LOG_DEBUG("Get {} Resolution {}!", deviceId, resolution);
                conditionResp["Resolution"] = resolution;
            }
        }

        auto jSeverity = j.find("Severity");
        if (jSeverity != j.end())
        {
            std::string severity = *jSeverity;
            if (severity.length() == 0)
            {
                BMCWEB_LOG_ERROR("Get {} Severity failed!", deviceId);
            }
            else
            {
                BMCWEB_LOG_DEBUG("Get {} Severity {}!", deviceId, severity);
                conditionResp["Severity"] = severity;
            }
        }

        auto jTimestamp = j.find("Timestamp");
        if (jTimestamp != j.end())
        {
            std::string timestamp = *jTimestamp;
            if (timestamp.length() == 0)
            {
                BMCWEB_LOG_ERROR("Get {} Timestamp failed!", deviceId);
            }
            else
            {
                BMCWEB_LOG_DEBUG("Get {} Timestamp {}!", deviceId, timestamp);
                conditionResp["Timestamp"] = timestamp;
            }
        }

        // Add condition into array
        if (resp.jsonValue.contains("Conditions"))
        {
            resp.jsonValue["Conditions"].push_back(conditionResp);
        }
        else
        {
            resp.jsonValue["Status"]["Conditions"].push_back(conditionResp);
        }
    }
}

/**
 * Utility function for populating Conditions
 * array of the ServiceConditions uri
 * at /redfish/v1/ServiceConditions or the Conditions
 * array of each device depending on the chassisId
 */

inline void populateServiceConditions(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    BMCWEB_LOG_DEBUG("Populating service conditions for device {}", chassisId);
    BMCWEB_LOG_DEBUG("ON REDFISH URI {}",
                     asyncResp->res.jsonValue["@odata.id"]);
    BMCWEB_LOG_DEBUG("PLATFORM DEVICE PREFIX IS {}",
                     BMCWEB_PLATFORM_DEVICE_PREFIX);

    std::string chasId = chassisId;
    if (!BMCWEB_PLATFORM_DEVICE_PREFIX.empty())
    {
        if (boost::starts_with(chassisId, BMCWEB_PLATFORM_DEVICE_PREFIX))
        {
            chasId = chassisId.substr(BMCWEB_PLATFORM_DEVICE_PREFIX.size());
        }
    }
    bool isDevice = !chasId.empty();

    if (isDevice)
    {
        if (!asyncResp->res.jsonValue["Status"].contains("Conditions"))
        {
            asyncResp->res.jsonValue["Status"]["Conditions"] =
                nlohmann::json::array();
        }
        if constexpr (BMCWEB_NVIDIA_OEM_DEVICE_STATUS_FROM_FILE)
        {
            handleDeviceServiceConditionsFromFile(asyncResp->res, chasId);
        }
        else
        {
            handleDeviceServiceConditions(asyncResp, chasId);
        }
    }
    else
    {
        if (!asyncResp->res.jsonValue.contains("Conditions"))
        {
            asyncResp->res.jsonValue["Conditions"] = nlohmann::json::array();
        }
        handleServiceConditionsURI(asyncResp);
    }
}

} // namespace conditions_utils
} // namespace redfish
