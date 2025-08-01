#include "dbus_log_watcher.hpp"

#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "event_logs_object_type.hpp"
#include "event_service_manager.hpp"
#include "logging.hpp"
#include "metric_report.hpp"
#include "utils/nvidia_utils.hpp"
#include "utils/time_utils.hpp"

#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <algorithm>
#include <optional>
#include <string>
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
    const dbus::utility::DBusPropertiesMap& map, EventLogObjectsType& event)
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
    event.timestamp = redfish::time_utils::getDateTimeUintMs(entry.Timestamp);

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

    sdbusplus::message::object_path objectPath;
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

const std::string propertiesMatchString =
    sdbusplus::bus::match::rules::type::signal() +
    sdbusplus::bus::match::rules::sender("xyz.openbmc_project.Logging") +
    sdbusplus::bus::match::rules::interface(
        "org.freedesktop.DBus.ObjectManager") +
    sdbusplus::bus::match::rules::path("/xyz/openbmc_project/logging") +
    sdbusplus::bus::match::rules::member("InterfacesAdded");

DbusEventLogMonitor::DbusEventLogMonitor() :
    dbusEventLogMonitor(*crow::connections::systemBus, propertiesMatchString,
                        onDbusEventLogCreated)

{}

static void getReadingsForReport(sdbusplus::message_t& msg)
{
    if (msg.is_method_error())
    {
        BMCWEB_LOG_ERROR("TelemetryMonitor Signal error");
        return;
    }

    sdbusplus::message::object_path path(msg.get_path());
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
