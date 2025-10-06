// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
// NOLINTNEXTLINE(misc-include-cleaner)
#include "registries.hpp"
// NOLINTNEXTLINE(misc-include-cleaner)
#include "registries/base_message_registry.hpp"
#include "registries/bios_attribute_registry.hpp"
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
// Nvidia code starts here
// TODO:Rohit to add support for bios reg and diff with upstream
inline std::optional<registries::HeaderAndUrl>
    getRegistryHeaderAndUrlFromPrefix(std::string_view registryName)
{
    if (Base::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{Base::header, Base::url};
    }
    if (HeartbeatEvent::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{HeartbeatEvent::header, HeartbeatEvent::url};
    }
    if (Openbmc::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{Openbmc::header, Openbmc::url};
    }
    if (ResourceEvent::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{ResourceEvent::header, ResourceEvent::url};
    }
    if (TaskEvent::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{TaskEvent::header, TaskEvent::url};
    }
    if (Telemetry::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{Telemetry::header, Telemetry::url};
    }
    if (Platform::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{Platform::header, Platform::url};
    }
    if (Update::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{Update::header, Update::url};
    }
    if (bios::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{bios::header, bios::url};
    }
    if (bios::header.registryPrefix == registryName)
    {
        return HeaderAndUrl{bios::header, bios::url};
    }
    return std::nullopt;
}

inline std::span<const MessageEntry> getRegistryFromPrefix(
    std::string_view registryName)
{
    if (Base::header.registryPrefix == registryName)
    {
        return {Base::registry};
    }
    if (HeartbeatEvent::header.registryPrefix == registryName)
    {
        return {HeartbeatEvent::registry};
    }
    if (Openbmc::header.registryPrefix == registryName)
    {
        return {Openbmc::registry};
    }
    if (ResourceEvent::header.registryPrefix == registryName)
    {
        return {ResourceEvent::registry};
    }
    if (TaskEvent::header.registryPrefix == registryName)
    {
        return {TaskEvent::registry};
    }
    if (Telemetry::header.registryPrefix == registryName)
    {
        return {Telemetry::registry};
    }
    if (Update::header.registryPrefix == registryName)
    {
        return {Update::registry};
    }
    if (Platform::header.registryPrefix == registryName)
    {
        return {Platform::registry};
    }
    if (SensorEvent::header.registryPrefix == registryName)
    {
        return {SensorEvent::registry};
    }
    if (bios::header.registryPrefix == registryName)
    {
        return {bios::registry};
    }
    if (bios::header.registryPrefix == registryName)
    {
        return {bios::registry};
    }

    return {Openbmc::registry};
}
// Nvidia code ends here
} // namespace redfish::registries
