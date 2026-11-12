#include "dbus_log_watcher.hpp"
#include "dbus_utility.hpp"
#include "event_logs_object_type.hpp"
#include "utils/dbus_event_log_entry.hpp"
#include "utils/time_utils.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace redfish
{

using namespace dbus::utility;

TEST(DBusLogWatcher, EventLogObjectFromDBusSuccess)
{
    const DBusPropertiesMap propMapStub = {
        {"AdditionalData",
         DbusVariantType(std::vector<std::pair<std::string, std::string>>{
             {"KEY", "VALUE"}})},
        {"EventId", DbusVariantType("")},
        {"Id", DbusVariantType(static_cast<uint32_t>(1838))},

        // use 'Message' for MessageId as per the design
        // https://github.com/openbmc/docs/blob/d886ce89fe66c128b3ab492e530ad48fa0c1b4eb/designs/event-logging.md?plain=1#L448
        {"Message", DbusVariantType("OpenBMC.0.1.PowerButtonPressed")},
        {"Resolution", DbusVariantType("")},
        {"Resolved", DbusVariantType(true)},
        {"ServiceProviderNotify", DbusVariantType("")},
        {"Severity", DbusVariantType("")},
        {"Timestamp", DbusVariantType(static_cast<uint64_t>(1638312095123))},
        {"UpdateTimestamp", DbusVariantType(static_cast<uint64_t>(3899))},
    };

    EventLogObjectsType event;

    const bool status =
        DbusEventLogMonitor::eventLogObjectFromDBus(propMapStub, event);

    EXPECT_TRUE(status);

    EXPECT_EQ(event.id, "1838");

    EXPECT_EQ(event.timestamp,
              redfish::time_utils::getDateTimeUintMs(uint64_t{1638312095123}));

    EXPECT_EQ(event.messageId, "OpenBMC.0.1.PowerButtonPressed");

    // dbus event subscriptions currently do not support message args
    EXPECT_TRUE(event.messageArgs.empty());
}

TEST(DBusLogWatcher, EventLogObjectFromDBusFailMissingProperty)
{
    // missing 'Resolved'
    const DBusPropertiesMap propMapWrong = {
        {"AdditionalData",
         DbusVariantType(std::vector<std::pair<std::string, std::string>>{
             {"KEY", "VALUE"}})},
        {"EventId", DbusVariantType("")},
        {"Id", DbusVariantType(static_cast<uint32_t>(1838))},
        {"Message", DbusVariantType("")},
        {"Resolution", DbusVariantType("")},
        {"ServiceProviderNotify", DbusVariantType("")},
        {"Severity", DbusVariantType("")},
        {"Timestamp", DbusVariantType(static_cast<uint64_t>(3832))},
        {"UpdateTimestamp", DbusVariantType(static_cast<uint64_t>(3899))},
    };

    EventLogObjectsType event;

    const bool status =
        DbusEventLogMonitor::eventLogObjectFromDBus(propMapWrong, event);

    EXPECT_FALSE(status);
}

TEST(DBusLogWatcher, EventTimestampFromEntryIsMilliseconds)
{
    DbusEventLogEntry entry;
    // A phosphor-logging Timestamp (milliseconds) seen on hardware. Formatted
    // as seconds it overflows and is clamped to 9999-12-31T23:59:59+00:00.
    entry.Timestamp = 1791450469877;
    EXPECT_EQ(DbusEventLogMonitor::eventTimestampFromEntry(entry),
              "2026-10-08T09:07:49.877+00:00");

    entry.Timestamp = 0;
    EXPECT_EQ(DbusEventLogMonitor::eventTimestampFromEntry(entry), "");
}

} // namespace redfish
