/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION &
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
#include "nvidia_dbus_log_watcher.hpp"

#include "cper_utils.hpp"
#include "event_service_manager.hpp"
#include "utils/nvidia_utils.hpp"
#include "utils/origin_utils.hpp"
namespace redfish
{

static void nvDbusEventLogMatchHandlerSingleEntry(
    const dbus::utility::DBusPropertiesMap& map)
{
    std::optional<DbusEventLogEntry> optEntry =
        fillDbusEventLogEntryFromPropertyMap(map);
    if (!optEntry.has_value())
    {
        BMCWEB_LOG_ERROR(
            "Could not construct event log entry from dbus properties");
        return;
    }
    DbusEventLogEntry& entry = optEntry.value();

    bool success = NvDbusEventLogMonitor::dbusEventLogEntryToSendEvent(entry);
    if (!success)
    {
        BMCWEB_LOG_ERROR("Could not parse event log entry from dbus");
        return;
    }
}

static void onDbusEventLogCreated(sdbusplus::message_t& msg)
{
    BMCWEB_LOG_DEBUG("Handling new DBus Event Log Entry");

    sdbusplus::message::object_path objectPath;
    dbus::utility::DBusInterfacesMap interfaces;

    msg.read(objectPath, interfaces);

    for (auto& pair : interfaces)
    {
        BMCWEB_LOG_DEBUG("Found dbus interface {}", pair.first);
        if (pair.first == "xyz.openbmc_project.Logging.Entry")
        {
            const dbus::utility::DBusPropertiesMap& map = pair.second;
            nvDbusEventLogMatchHandlerSingleEntry(map);
        }
    }
}

static const std::string propertiesMatchString =
    sdbusplus::bus::match::rules::type::signal() +
    sdbusplus::bus::match::rules::sender("xyz.openbmc_project.Logging") +
    sdbusplus::bus::match::rules::interface(
        "org.freedesktop.DBus.ObjectManager") +
    sdbusplus::bus::match::rules::path("/xyz/openbmc_project/logging") +
    sdbusplus::bus::match::rules::member("InterfacesAdded");

NvDbusEventLogMonitor::NvDbusEventLogMonitor() :
    nvDbusEventLogMonitor(*crow::connections::systemBus, propertiesMatchString,
                          onDbusEventLogCreated)
{}

bool NvDbusEventLogMonitor::dbusEventLogEntryToSendEvent(
    const DbusEventLogEntry& entry)
{
    std::string messageId;
    std::string eventId;
    std::string severity;
    std::string timestamp;
    std::string originOfCondition;
    std::string deviceName;
    std::string resourceType;
    std::string logEntryId;
    std::string satBMCLogEntryUrl;
    std::string resolution;
    std::vector<std::string> messageArgs = {};
    nlohmann::json::object_t cper;

    // Extract data from entry object
    if (!entry.AdditionalData.empty())
    {
        AdditionalData additionalData(entry.AdditionalData);

        if (additionalData.count("DEVICE_NAME") > 0)
        {
            deviceName = additionalData["DEVICE_NAME"];
        }
        // convert SEL SENSOR_PATH to RF OriginOfCondition
        if (additionalData.count("SENSOR_PATH") == 1)
        {
            originOfCondition = additionalData["SENSOR_PATH"];
        }
        if (additionalData.count("REDFISH_ORIGIN_OF_CONDITION") == 1)
        {
            originOfCondition = additionalData["REDFISH_ORIGIN_OF_CONDITION"];
        }
        if (additionalData.count("REDFISH_LOGENTRY") == 1)
        {
            satBMCLogEntryUrl = additionalData["REDFISH_LOGENTRY"];
        }
        if (additionalData.count("REDFISH_MESSAGE_ID") == 1)
        {
            messageId = additionalData["REDFISH_MESSAGE_ID"];
            BMCWEB_LOG_DEBUG("Found message ID: {}", messageId);
            if (additionalData.count("REDFISH_MESSAGE_ARGS") == 1)
            {
                std::string args = additionalData["REDFISH_MESSAGE_ARGS"];
                BMCWEB_LOG_DEBUG("Processing message args: {}", args);
                boost::split(messageArgs, args, boost::is_any_of(","));
                // Trim leading and tailing whitespace of each argument
                for (auto& msgArg : messageArgs)
                {
                    boost::trim(msgArg);
                }

                if (!messageArgs[0].empty())
                {
                    // Map dbus property to redfish property
                    if (dBusToRedfishProperty.find(messageArgs[0]) !=
                        dBusToRedfishProperty.end())
                    {
                        std::string oldArg = messageArgs[0];
                        messageArgs[0] = dBusToRedfishProperty[messageArgs[0]];
                        BMCWEB_LOG_DEBUG("Mapped property: {} -> {}", oldArg,
                                         messageArgs[0]);
                    }
                    else
                    {
                        BMCWEB_LOG_WARNING("property mapping not found for {}",
                                           messageArgs[0]);
                    }
                }
            }
            else if (additionalData.count("REDFISH_MESSAGE_ARGS") > 0)
            {
                BMCWEB_LOG_DEBUG(
                    "Multiple REDFISH_MESSAGE_ARGS in the Dbus signal message.");
                return false;
            }
        }
        else
        {
            auto counter = additionalData.count("REDFISH_MESSAGE_ID");
            // when removing entries counter will be 0
            if (counter > 0)
            {
                BMCWEB_LOG_DEBUG(
                    "There should be exactly one MessageId in the Dbus signal message. Found {}",
                    std::to_string(counter));
                return false;
            }
        }

        BMCWEB_LOG_DEBUG("Parsing additional data for CPER");
        nlohmann::json::object_t oem;
        parseAdditionalDataForCPER(cper, oem, additionalData,
                                   originOfCondition);
    }

    // Extract other fields from entry
    if (!entry.Message.empty())
    {
        eventId = entry.Message;
    }

    if (entry.Id != 0)
    {
        logEntryId = std::to_string(entry.Id);
    }

    if (entry.Resolution != nullptr && !entry.Resolution->empty())
    {
        resolution = *entry.Resolution;
    }

    if (!entry.Severity.empty())
    {
        severity = entry.Severity;
    }

    if (entry.Timestamp != 0)
    {
        timestamp = redfish::time_utils::getDateTimeStdtime(
            redfish::time_utils::getTimestamp(entry.Timestamp));
    }

    if (messageId.empty())
    {
        BMCWEB_LOG_DEBUG("Skipping invalid Dbus log entry - empty messageId");
        return false;
    }

    BMCWEB_LOG_DEBUG("Creating event for messageId: {}", messageId);
    NvEvent event(messageId);
    if (!event.isValid())
    {
        BMCWEB_LOG_ERROR("Failed to create valid event for messageId: {}",
                         messageId);
        return false;
    }
    event.messageSeverity = translateSeverityDbusToRedfish(severity);
    event.eventTimestamp = timestamp;
    event.setRegistryMsg(messageArgs);
    event.messageArgs = messageArgs;

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        BMCWEB_LOG_DEBUG("Adding NVIDIA OEM properties for device: {}",
                         deviceName);
        event.oem = {{"Oem",
                      {{"Nvidia",
                        {{"@odata.type", "#NvidiaEvent.v1_0_0.EventRecord"},
                         {"Device", deviceName},
                         {"ErrorId", eventId}}}}}};
    }
    if (!cper.empty())
    {
        event.cper = cper;
    }
    event.eventResolution = resolution;
    event.logEntryId = logEntryId;
    event.satBMCLogEntryUrl = satBMCLogEntryUrl;
    if (!originOfCondition.empty())
    {
        BMCWEB_LOG_DEBUG("Processing event with originOfCondition: {}",
                         originOfCondition);
        for (auto& it : dBusToResourceType)
        {
            if (originOfCondition.find(it.first) != std::string::npos)
            {
                resourceType = it.second;
                break;
            }
        }
        event.resourceType = resourceType;
        EventServiceManager::getInstance().sendEventWithOOC(originOfCondition,
                                                            event);
    }
    else
    {
        BMCWEB_LOG_WARNING("No OriginOfCondition in event log. MsgId: {}",
                           messageId);
        EventServiceManager::getInstance().sendEventWithOOC(std::string{},
                                                            event);
    }

    return true;
}
} // namespace redfish
