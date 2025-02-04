#include "registries.hpp"

#include "registries/base_message_registry.hpp"
#include "registries/openbmc_message_registry.hpp"
#include "registries/platform_message_registry.hpp"
#include "registries/resource_event_message_registry.hpp"
#include "registries/task_event_message_registry.hpp"
#include "registries/telemetry_message_registry.hpp"
#include "registries/update_message_registry.hpp"
#include "str_utility.hpp"

#include <algorithm>
#include <cstring>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace redfish::registries
{

const Message* getMessageFromRegistry(const std::string& messageKey,
                                      std::span<const MessageEntry> registry)
{
    std::span<const MessageEntry>::iterator messageIt = std::ranges::find_if(
        registry, [&messageKey](const MessageEntry& messageEntry) {
        return std::strcmp(messageEntry.first, messageKey.c_str()) == 0;
    });
    if (messageIt != registry.end())
    {
        return &messageIt->second;
    }

    return nullptr;
}

const Message* getMessage(std::string_view messageID)
{
    // Redfish MessageIds are in the form
    // RegistryName.MajorVersion.MinorVersion.MessageKey, so parse it to find
    // the right Message
    if (messageID.empty())
    {
        return nullptr;
    }
    std::vector<std::string> fields;
    fields.reserve(4);
    bmcweb::split(fields, messageID, '.');
    if (fields.size() < 4)
    {
        return nullptr;
    }
    const std::string& registryName = fields[0];
    const std::string& messageKey = fields[3];

    // Find the right registry and check it for the MessageKey
    if (std::string(base::header.registryPrefix) == registryName)
    {
        return getMessageFromRegistry(
            messageKey, std::span<const MessageEntry>(base::registry));
    }
    if (std::string(openbmc::header.registryPrefix) == registryName)
    {
        return getMessageFromRegistry(
            messageKey, std::span<const MessageEntry>(openbmc::registry));
    }
    if (std::string(telemetry::header.registryPrefix) == registryName)
    {
        return getMessageFromRegistry(
            messageKey, std::span<const MessageEntry>(telemetry::registry));
    }
    if (std::string(task_event::header.registryPrefix) == registryName)
    {
        return getMessageFromRegistry(
            messageKey, std::span<const MessageEntry>(task_event::registry));
    }
    if (std::string(resource_event::header.registryPrefix) == registryName)
    {
        return getMessageFromRegistry(
            messageKey,
            std::span<const MessageEntry>(resource_event::registry));
    }
    if (std::string(update::header.registryPrefix) == registryName)
    {
        return getMessageFromRegistry(
            messageKey, std::span<const MessageEntry>(update::registry));
    }
    if (std::string(platform::header.registryPrefix) == registryName)
    {
        return getMessageFromRegistry(
            messageKey, std::span<const MessageEntry>(platform::registry));
    }

    return nullptr;
}

} // namespace redfish::registries
