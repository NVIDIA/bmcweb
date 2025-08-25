// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
// NOLINTNEXTLINE(misc-include-cleaner)
#include "registries.hpp"
// NOLINTNEXTLINE(misc-include-cleaner)
#include "registries/base_message_registry.hpp"
// NOLINTNEXTLINE(misc-include-cleaner)
#include "registries/heartbeat_event_message_registry.hpp"
// NOLINTNEXTLINE(misc-include-cleaner)
#include "registries/openbmc_message_registry.hpp"
#include "registries/platform_message_registry.hpp"
// NOLINTNEXTLINE(misc-include-cleaner)
#include "registries/resource_event_message_registry.hpp"
#include "registries/sensor_event_message_registry.hpp"
// NOLINTNEXTLINE(misc-include-cleaner)
#include "registries/task_event_message_registry.hpp"
// NOLINTNEXTLINE(misc-include-cleaner)
#include "registries/telemetry_message_registry.hpp"
#include "registries/update_message_registry.hpp"

#include <optional>
#include <span>
#include <string_view>

namespace redfish::registries
{
struct HeaderAndUrl
{
    const Header& header;
    const char* url;
};
// TODO:Rohit to add support for bios reg and diff with upstream
inline std::optional<registries::HeaderAndUrl>
    getRegistryHeaderAndUrlFromPrefix(std::string_view registryName)
{
    if (base::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{base::header, base::url};
    }
    if (heartbeat_event::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{heartbeat_event::header, heartbeat_event::url};
    }
    if (openbmc::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{openbmc::header, openbmc::url};
    }
    if (resource_event::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{resource_event::header, resource_event::url};
    }
    if (task_event::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{task_event::header, task_event::url};
    }
    if (telemetry::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{telemetry::header, telemetry::url};
    }
    if (platform::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{platform::header, platform::url};
    }
    if (update::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{update::header, update::url};
    }
    return std::nullopt;
}

inline std::span<const MessageEntry> getRegistryFromPrefix(
    std::string_view registryName)
{
    if (base::header.registryPrefix == registryName)
    {
        return {base::registry};
    }
    if (heartbeat_event::header.registryPrefix == registryName)
    {
        return {heartbeat_event::registry};
    }
    if (openbmc::header.registryPrefix == registryName)
    {
        return {openbmc::registry};
    }
    if (resource_event::header.registryPrefix == registryName)
    {
        return {resource_event::registry};
    }
    if (task_event::header.registryPrefix == registryName)
    {
        return {task_event::registry};
    }
    if (telemetry::header.registryPrefix == registryName)
    {
        return {telemetry::registry};
    }
    if (update::header.registryPrefix == registryName)
    {
        return {update::registry};
    }
    if (platform::header.registryPrefix == registryName)
    {
        return {platform::registry};
    }
    if (sensor_event::header.registryPrefix == registryName)
    {
        return {sensor_event::registry};
    }

    return {openbmc::registry};
}
} // namespace redfish::registries
||||||| 80d2ef31c

#include <optional>
#include <span>
#include <string_view>

namespace redfish::registries
{
struct HeaderAndUrl
{
    const Header& header;
    const char* url;
};

inline std::optional<registries::HeaderAndUrl>
    getRegistryHeaderAndUrlFromPrefix(std::string_view registryName)
{
    if (base::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{base::header, base::url};
    }
    if (heartbeat_event::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{heartbeat_event::header, heartbeat_event::url};
    }
    if (openbmc::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{openbmc::header, openbmc::url};
    }
    if (resource_event::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{resource_event::header, resource_event::url};
    }
    if (task_event::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{task_event::header, task_event::url};
    }
    if (telemetry::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{telemetry::header, telemetry::url};
    }
    return std::nullopt;
}

inline std::span<const MessageEntry> getRegistryFromPrefix(
    std::string_view registryName)
{
    if (base::header.registryPrefix == registryName)
    {
        return {base::registry};
    }
    if (heartbeat_event::header.registryPrefix == registryName)
    {
        return {heartbeat_event::registry};
    }
    if (openbmc::header.registryPrefix == registryName)
    {
        return {openbmc::registry};
    }
    if (resource_event::header.registryPrefix == registryName)
    {
        return {resource_event::registry};
    }
    if (task_event::header.registryPrefix == registryName)
    {
        return {task_event::registry};
    }
    if (telemetry::header.registryPrefix == registryName)
    {
        return {telemetry::registry};
    }
    return {openbmc::registry};
}
} // namespace redfish::registries
=======
>>>>>>> origin/master
