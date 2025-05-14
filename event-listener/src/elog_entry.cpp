// NOLINTBEGIN
#include "../include/elog_entry.hpp"

#include "utils/dbus_log_utils.hpp"

namespace phosphor
{
namespace logging
{

evtEntry::evtEntry(sdbusplus::bus::bus& bus, const std::string& path,
                   uint32_t idErr, uint64_t timestampErr, Level severityErr,
                   std::string&& msg, std::string&& resolutionErr,
                   std::vector<std::string>&& additionalDataErr) :
    EntryIfaces(bus, path.c_str(), EntryIfaces::action::defer_emit)
{
    id(idErr, true);
    severity(severityErr, true);
    timestamp(timestampErr, true);
    updateTimestamp(timestampErr, true);
    message(std::move(msg), true);
    resolution(std::move(resolutionErr), true);
    redfish::AdditionalData additional(std::move(additionalDataErr));
    std::map<std::string, std::string> dataMap;
    for (const auto& [key, value] : additional)
    {
        dataMap[key] = value;
    }
    additionalData(dataMap, true);

    EntryIfaces::emit_object_added();
};

evtEntry::~evtEntry() {}

} // namespace logging
} // namespace phosphor
// NOLINTEND
