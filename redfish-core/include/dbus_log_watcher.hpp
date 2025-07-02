#pragma once

#include "dbus_utility.hpp"
#include "event_logs_object_type.hpp"
#include "utils/dbus_event_log_entry.hpp"
#include <sdbusplus/bus/match.hpp>
namespace redfish
{
class DbusEventLogMonitor
{
  public:
    DbusEventLogMonitor();
    sdbusplus::bus::match_t dbusEventLogMonitor;

    static bool eventLogObjectFromDBus(
        const dbus::utility::DBusPropertiesMap& map,
        EventLogObjectsType& event);
    // Nvidia specific function to convert DbusEventLogEntry to RedfishEventEntry
    static bool redfishEventEntryToSendEvent(const DbusEventLogEntry& entry);
};

class DbusTelemetryMonitor
{
  public:
    DbusTelemetryMonitor();

    sdbusplus::bus::match_t matchTelemetryMonitor;
};
} // namespace redfish
