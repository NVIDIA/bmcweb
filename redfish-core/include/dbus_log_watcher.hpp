#pragma once

#include "dbus_utility.hpp"
#include "event_logs_object_type.hpp"
#include "utils/dbus_event_log_entry.hpp"

#include <sdbusplus/bus/match.hpp>

#include <string>
namespace redfish
{
class DbusEventLogMonitor
{
  public:
    DbusEventLogMonitor();
    sdbusplus::match dbusEventLogMonitor;

    static bool eventLogObjectFromDBus(
        const dbus::utility::DBusPropertiesMap& map, EventLogObjectsType& event,
        std::string_view tz = "");
    // Nvidia specific function to convert DbusEventLogEntry to
    // RedfishEventEntry
    static bool redfishEventEntryToSendEvent(const DbusEventLogEntry& entry);
    // EventTimestamp for an event pushed from a D-Bus log entry. The entry's
    // Timestamp is milliseconds since the epoch; empty when it is unset.
    static std::string eventTimestampFromEntry(const DbusEventLogEntry& entry);
};

class DbusTelemetryMonitor
{
  public:
    DbusTelemetryMonitor();

    sdbusplus::match matchTelemetryMonitor;
};
} // namespace redfish
