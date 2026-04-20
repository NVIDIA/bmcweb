#include "dbus_log_watcher.hpp"

#include "bmcweb_config.h"

#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "event_logs_object_type.hpp"
#include "event_service_manager.hpp"
#include "logging.hpp"
// NOLINTNEXTLINE(misc-include-cleaner)
#include "metric_report.hpp"
#include "nvidia_event_service_manager.hpp"
#include "str_utility.hpp"
#include "telemetry_readings.hpp"
#include "utils/dbus_event_log_entry.hpp"
#include "utils/dbus_log_utils.hpp"
#include "utils/nvidia_time_utils.hpp"
#include "utils/nvidia_utils.hpp"
#include "utils/time_utils.hpp"

#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

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

    bool success = DbusEventLogMonitor::redfishEventEntryToSendEvent(entry);
    if (!success)
    {
        BMCWEB_LOG_ERROR("Could not parse event log entry from dbus");
        return;
    }
}

bool DbusEventLogMonitor::eventLogObjectFromDBus(
    const dbus::utility::DBusPropertiesMap& map, EventLogObjectsType& event,
    std::string_view tz)
{
    std::optional<DbusEventLogEntry> optEntry =
        fillDbusEventLogEntryFromPropertyMap(map);

    if (!optEntry.has_value())
    {
        BMCWEB_LOG_ERROR(
            "Could not construct event log entry from dbus properties");
        return false;
    }
    DbusEventLogEntry& entry = optEntry.value();
    event.id = std::to_string(entry.Id);
    event.timestamp =
        redfish::time_utils::getDateTimeUintMs(entry.Timestamp, tz);

    // This dbus property is not documented to contain the Redfish Message Id,
    // but can be used as such. As a temporary solution that is sufficient,
    // the event filtering code will drop the event anyways if event.messageId
    // is not valid.
    //
    // This will need resolved before
    // experimental-redfish-dbus-log-subscription is stabilized
    event.messageId = entry.Message;

    // The order of 'AdditionalData' is not what's specified in an e.g.
    // busctl call to create the Event Log Entry. So it cannot be used
    // to map to the message args. Leaving this branch here for it to be
    // implemented when the mapping is available

    return true;
}

static void dbusEventLogMatchHandlerSingleEntry(
    const dbus::utility::DBusPropertiesMap& map)
{
    std::vector<EventLogObjectsType> eventRecords;
    EventLogObjectsType& event = eventRecords.emplace_back();
    bool success = DbusEventLogMonitor::eventLogObjectFromDBus(map, event);
    if (!success)
    {
        BMCWEB_LOG_ERROR("Could not parse event log entry from dbus");
        return;
    }

    BMCWEB_LOG_DEBUG("Found Event Log Entry Id={}, Timestamp={}, Message={}",
                     event.id, event.timestamp, event.messageId);
    EventServiceManager::sendEventsToSubs(eventRecords);
}

static void onDbusEventLogCreated(sdbusplus::message_t& msg)
{
    BMCWEB_LOG_DEBUG("Handling new DBus Event Log Entry");

    sdbusplus::object_path objectPath;
    dbus::utility::DBusInterfacesMap interfaces;

    msg.read(objectPath, interfaces);

    for (auto& pair : interfaces)
    {
        BMCWEB_LOG_DEBUG("Found dbus interface {}", pair.first);
        if (pair.first == "xyz.openbmc_project.Logging.Entry")
        {
            const dbus::utility::DBusPropertiesMap& map = pair.second;
            if constexpr (BMCWEB_NVIDIA_REDFISH_EVENT_SUPPORT)
            {
                nvDbusEventLogMatchHandlerSingleEntry(map);
            }
            else
            {
                dbusEventLogMatchHandlerSingleEntry(map);
            }
        }
    }
}

const std::string propertiesMatchString(
    "type='signal', "
    "member='InterfacesAdded', "
    "path_namespace='/xyz/openbmc_project/logging'");

DbusEventLogMonitor::DbusEventLogMonitor() :
    dbusEventLogMonitor(*crow::connections::systemBus, propertiesMatchString,
                        onDbusEventLogCreated)

{}

static void getReadingsForReport(sdbusplus::message_t& msg)
{
    sdbusplus::object_path path(msg.get_path());
    std::string id = path.filename();
    if (id.empty())
    {
        BMCWEB_LOG_ERROR("Failed to get Id from path");
        return;
    }

    std::string interface;
    dbus::utility::DBusPropertiesMap props;
    std::vector<std::string> invalidProps;
    msg.read(interface, props, invalidProps);

    auto found = std::ranges::find_if(props, [](const auto& x) {
        return x.first == "Readings";
    });
    if (found == props.end())
    {
        BMCWEB_LOG_INFO("Failed to get Readings from Report properties");
        return;
    }

    const telemetry::TimestampReadings* readings =
        std::get_if<telemetry::TimestampReadings>(&found->second);
    if (readings == nullptr)
    {
        BMCWEB_LOG_INFO("Failed to get Readings from Report properties");
        return;
    }
    EventServiceManager::sendTelemetryReportToSubs(id, *readings);
}

const std::string telemetryMatchStr =
    "type='signal',member='PropertiesChanged',"
    "interface='org.freedesktop.DBus.Properties',"
    "arg0=xyz.openbmc_project.Telemetry.Report";

DbusTelemetryMonitor::DbusTelemetryMonitor() :
    matchTelemetryMonitor(*crow::connections::systemBus, telemetryMatchStr,
                          getReadingsForReport)
{}

bool DbusEventLogMonitor::redfishEventEntryToSendEvent(
    const DbusEventLogEntry& entry)
{
    std::string messageId;
    std::string errorId;
    std::string severity;
    std::string timestamp;
    std::string originOfCondition;
    std::string deviceName;
    std::string resourceType;
    std::string logEntryId;
    std::string satBMCLogEntryUrl;
    std::string resolution;
    std::string message;
    std::vector<std::string> messageArgs = {};
    nlohmann::json::object_t cper;

    // Extract data from entry object
    if (!entry.AdditionalData.empty())
    {
        AdditionalData additionalData(entry.AdditionalData);
        std::string redfishMessageArgs;

        // Iterate through AdditionalData and extract values
        for (const auto& [key, value] : additionalData)
        {
            if (key == "DEVICE_NAME")
            {
                deviceName = value;
            }
            else if (key == "ERROR_ID")
            {
                errorId = value;
            }
            else if (key == "SENSOR_PATH" ||
                     key == "REDFISH_ORIGIN_OF_CONDITION")
            {
                // convert SEL SENSOR_PATH or REDFISH_ORIGIN_OF_CONDITION to
                // RF OriginOfCondition
                originOfCondition = value;
            }
            else if (key == "REDFISH_LOGENTRY")
            {
                satBMCLogEntryUrl = value;
            }
            else if (key == "REDFISH_MESSAGE_ID")
            {
                messageId = value;
                BMCWEB_LOG_DEBUG("Found message ID: {}", messageId);
            }
            else if (key == "REDFISH_MESSAGE_ARGS")
            {
                redfishMessageArgs = value;
            }
        }

        // Process message args if both messageId and args are present
        if (!messageId.empty() && !redfishMessageArgs.empty())
        {
            BMCWEB_LOG_DEBUG("Processing message args: {}", redfishMessageArgs);
            bmcweb::split(messageArgs, redfishMessageArgs, ',');
            // Trim leading and tailing whitespace of each argument
            for (auto& msgArg : messageArgs)
            {
                msgArg = redfish::trim(msgArg);
            }

            if (!messageArgs.empty() && !messageArgs[0].empty())
            {
                // Map dbus property to redfish property
                bool mappingFound = false;
                for (const auto& [dbusKey, redfishValue] :
                     dBusToRedfishProperty)
                {
                    if (messageArgs[0] == dbusKey)
                    {
                        std::string oldArg = messageArgs[0];
                        messageArgs[0] = redfishValue;
                        BMCWEB_LOG_DEBUG("Mapped property: {} -> {}", oldArg,
                                         messageArgs[0]);
                        mappingFound = true;
                        break;
                    }
                }
                if (!mappingFound)
                {
                    BMCWEB_LOG_WARNING("property mapping not found for {}",
                                       messageArgs[0]);
                }
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
        message = entry.Message;
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

    NvEvent event(messageId);
    event.messageSeverity = translateSeverityDbusToRedfish(severity);
    event.eventTimestamp = timestamp;
    if (messageId.empty())
    {
        BMCWEB_LOG_DEBUG("Creating event for message: {}", message);
        event.message = message;
    }
    else
    {
        BMCWEB_LOG_DEBUG("Creating event for messageId: {}", messageId);
        event.setRegistryMsg(messageArgs);
        event.messageArgs = messageArgs;
    }

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        BMCWEB_LOG_DEBUG("Adding NVIDIA OEM properties for device: {}",
                         deviceName);
        event.oem = {{"Oem",
                      {{"Nvidia",
                        {{"@odata.type", "#NvidiaEvent.v1_0_0.EventRecord"},
                         {"Device", deviceName},
                         {"ErrorId", errorId}}}}}};
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
        for (const auto& it : dBusToResourceType)
        {
            if (originOfCondition.find(it.first) != std::string::npos)
            {
                resourceType = it.second;
                break;
            }
        }
        event.resourceType = resourceType;
        EventServiceManager::getInstance().eventServiceOOC(originOfCondition,
                                                           deviceName, event);
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
