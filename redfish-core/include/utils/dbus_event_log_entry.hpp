#pragma once

#include "dbus_utility.hpp"
#include "utils/dbus_utils.hpp"

#include <optional>
#include <string>

namespace redfish
{
struct DbusEventLogEntry
{
    // represents a subset of an instance of dbus interface
    // xyz.openbmc_project.Logging.Entry

    uint32_t Id = 0;
    std::string Message;
    const std::string* Path = nullptr;
    const std::string* Resolution = nullptr;
    bool Resolved = false;
    std::string ServiceProviderNotify;
    std::string Severity;
    uint64_t Timestamp = 0;
    uint64_t UpdateTimestamp = 0;
    const std::string* eventId = nullptr;
    const std::vector<std::string>* additionalDataRaw = nullptr;
};

inline std::optional<DbusEventLogEntry> fillDbusEventLogEntryFromPropertyMap(
    const dbus::utility::DBusPropertiesMap& resp)
{
    DbusEventLogEntry entry;

    // clang-format off
    bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), resp,
        "Id", entry.Id,
        "Message", entry.Message,
        "Path", entry.Path,
        "Resolution", entry.Resolution,
        "Resolved", entry.Resolved,
        "ServiceProviderNotify", entry.ServiceProviderNotify,
        "Severity", entry.Severity,
        "Timestamp", entry.Timestamp,
        "UpdateTimestamp", entry.UpdateTimestamp,
        "EventId", entry.eventId,
        "AdditionalData", entry.additionalDataRaw
    );
    // clang-format on
    if (!success)
    {
        return std::nullopt;
    }
    return entry;
}
} // namespace redfish
