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
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "logging.hpp"
#include "utils/dbus_log_utils.hpp"
#include "utils/file_utils.hpp"
#include "utils/nvidia_time_utils.hpp"
#include "utils/origin_utils.hpp"
#include "utils/registry_utils.hpp"
#include "utils/time_utils.hpp"

#include <sdbusplus/message.hpp>

namespace redfish
{
namespace conditions_utils
{

inline void handleDeviceServiceConditions(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    dbus::utility::async_method_call(
        [asyncResp{asyncResp}, chassisId(std::string(chassisId))](
            const boost::system::error_code ec,
            const dbus::utility::ManagedObjectType& resp) {
            if (ec)
            {
                // ignore the error while BMC is booting
                if (ec.value() !=
                    boost::system::errc::no_such_device_or_address)
                {
                    BMCWEB_LOG_ERROR(
                        "getLogEntriesIfaceData resp_handler got error {}", ec);
                    messages::internalError(asyncResp->res);
                }
                return;
            }

            const uint32_t* id = nullptr;
            const std::string* message = nullptr;
            const std::string* severity = nullptr;
            const std::vector<std::pair<std::string, std::string>>*
                additionalData = nullptr;
            const std::string prefix =
                "xyz.openbmc_project.Logging.Entry.Level.";
            const std::string criticalSev = prefix + "Critical";
            const std::string warningSev = prefix + "Warning";
            std::time_t timestamp{};

            for (const auto& objectPath : resp)
            {
                additionalData = nullptr;
                for (const auto& interfaceMap : objectPath.second)
                {
                    if (interfaceMap.first ==
                        "xyz.openbmc_project.Logging.Entry")
                    {
                        for (const auto& propertyMap : interfaceMap.second)
                        {
                            if (propertyMap.first == "Id")
                            {
                                id = std::get_if<uint32_t>(&propertyMap.second);
                            }
                            else if (propertyMap.first == "Severity")
                            {
                                severity = std::get_if<std::string>(
                                    &propertyMap.second);
                            }
                            else if (propertyMap.first == "Message")
                            {
                                message = std::get_if<std::string>(
                                    &propertyMap.second);
                            }
                            else if (propertyMap.first == "AdditionalData")
                            {
                                additionalData = std::get_if<std::vector<
                                    std::pair<std::string, std::string>>>(
                                    &propertyMap.second);
                            }
                            else if (propertyMap.first == "Timestamp")
                            {
                                const uint64_t* millisTimeStamp =
                                    std::get_if<uint64_t>(&propertyMap.second);
                                if (millisTimeStamp != nullptr)
                                {
                                    timestamp =
                                        redfish::time_utils::getTimestamp(
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

                if (additionalData != nullptr)
                {
                    redfish::AdditionalData additional(*additionalData);
                    if (additional.contains("REDFISH_ORIGIN_OF_CONDITION"))
                    {
                        originOfCondition =
                            additional["REDFISH_ORIGIN_OF_CONDITION"];
                    }
                    if (additional.contains("REDFISH_MESSAGE_ARGS"))
                    {
                        messageArgs = additional["REDFISH_MESSAGE_ARGS"];
                    }
                    if (additional.contains("REDFISH_MESSAGE_ID"))
                    {
                        messageId = additional["REDFISH_MESSAGE_ID"];
                    }
                    if (additional.contains("DEVICE_NAME"))
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
    dbus::utility::async_method_call(
        [asyncResp{asyncResp}](const boost::system::error_code ec,
                               const dbus::utility::ManagedObjectType& resp) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                BMCWEB_LOG_ERROR(
                    "getLogEntriesIfaceData resp_handler got error {}", ec);
                return;
            }
            const uint32_t* id = nullptr;
            const std::string* message = nullptr;
            const std::string* severity = nullptr;
            const std::vector<std::pair<std::string, std::string>>*
                additionalData = nullptr;
            const std::string prefix =
                "xyz.openbmc_project.Logging.Entry.Level.";
            const std::string criticalSev = prefix + "Critical";
            const std::string warningSev = prefix + "Warning";
            std::time_t timestamp{};
            for (const auto& objectPath : resp)
            {
                additionalData = nullptr;
                for (const auto& interfaceMap : objectPath.second)
                {
                    if (interfaceMap.first ==
                        "xyz.openbmc_project.Logging.Entry")
                    {
                        for (const auto& propertyMap : interfaceMap.second)
                        {
                            if (propertyMap.first == "Id")
                            {
                                id = std::get_if<uint32_t>(&propertyMap.second);
                            }
                            else if (propertyMap.first == "Severity")
                            {
                                severity = std::get_if<std::string>(
                                    &propertyMap.second);
                            }
                            else if (propertyMap.first == "Message")
                            {
                                message = std::get_if<std::string>(
                                    &propertyMap.second);
                            }
                            else if (propertyMap.first == "AdditionalData")
                            {
                                additionalData = std::get_if<std::vector<
                                    std::pair<std::string, std::string>>>(
                                    &propertyMap.second);
                            }
                            else if (propertyMap.first == "Timestamp")
                            {
                                const uint64_t* millisTimeStamp =
                                    std::get_if<uint64_t>(&propertyMap.second);
                                if (millisTimeStamp != nullptr)
                                {
                                    timestamp =
                                        redfish::time_utils::getTimestamp(
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

                if (additionalData != nullptr)
                {
                    redfish::AdditionalData additional(*additionalData);
                    if (additional.contains("REDFISH_ORIGIN_OF_CONDITION"))
                    {
                        originOfCondition =
                            additional["REDFISH_ORIGIN_OF_CONDITION"];
                    }
                    if (additional.contains("REDFISH_MESSAGE_ARGS"))
                    {
                        messageArgs = additional["REDFISH_MESSAGE_ARGS"];
                    }
                    if (additional.contains("REDFISH_MESSAGE_ID"))
                    {
                        messageId = additional["REDFISH_MESSAGE_ID"];
                    }
                    if (additional.contains("DEVICE_NAME"))
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
                    }
                }
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
        BMCWEB_LOG_WARNING("Condtions: read {} status file failed!", deviceId);
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

        // Support both MessageRegistry or non-MessageRegitry formats
        auto jMsgId = j.find("MessageId");
        auto jMsgArgs = j.find("MessageArgs");
        if (jMsgId != j.end() && jMsgArgs != j.end())
        {
            // MessageRegistry Format
            std::string messageId = jMsgId->get<std::string>();
            std::string message =
                message_registries::composeMessage(messageId, *jMsgArgs);

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
            std::string ooc = jOOC->get<std::string>();
            std::string originOfCondition =
                origin_utils::getDeviceRedfishURI(ooc);

            if (originOfCondition.empty())
            {
                BMCWEB_LOG_WARNING("getDeviceRedfishURI of {} failed!", ooc);
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
                std::string device = jDevice->get<std::string>();
                conditionResp["Oem"]["Nvidia"]["Device"] = device;
            }

            auto jErrorId = j.find("ErrorId");
            if (jErrorId != j.end())
            {
                std::string errorId = jErrorId->get<std::string>();
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
            std::string resolution = jResolution->get<std::string>();
            if (resolution.empty())
            {
                BMCWEB_LOG_WARNING("Get {} Resolution failed!", deviceId);
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
            std::string severity = jSeverity->get<std::string>();
            if (severity.empty())
            {
                BMCWEB_LOG_WARNING("Get {} Severity failed!", deviceId);
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
            std::string timestamp = jTimestamp->get<std::string>();
            if (timestamp.empty())
            {
                BMCWEB_LOG_WARNING("Get {} Timestamp failed!", deviceId);
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
    if (!asyncResp->res.jsonValue.contains("@odata.id"))
    {
        BMCWEB_LOG_DEBUG("Service conditions not found for device {}",
                         chassisId);
        return;
    }
    std::string redfishUri =
        asyncResp->res.jsonValue.at("@odata.id").get<std::string>();
    if (redfishUri.empty())
    {
        BMCWEB_LOG_DEBUG("Service conditions not found for device {}",
                         chassisId);
        return;
    }
    BMCWEB_LOG_DEBUG("ON REDFISH URI {}", redfishUri);
    BMCWEB_LOG_DEBUG("PLATFORM DEVICE PREFIX IS {}",
                     BMCWEB_PLATFORM_DEVICE_PREFIX);
    std::string chasId = chassisId;
    if (!BMCWEB_PLATFORM_DEVICE_PREFIX.empty())
    {
        if (chassisId.starts_with(BMCWEB_PLATFORM_DEVICE_PREFIX))
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
