/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "dbus_utility.hpp"
#include "logging.hpp"
#include "str_utility.hpp"
#include "utils/dbus_log_utils.hpp"

#include <nlohmann/json.hpp>
#include <sdbusplus/exception.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace redfish::nvidia_log_entry
{

/**
 * @brief The parts of an xyz.openbmc_project.Logging.Entry a task message
 *        uses.
 *
 * EventId is read from the Entry property rather than from AdditionalData:
 * phosphor-log-manager lifts fully qualified keys out of the map into real
 * properties.
 *
 * The firmware update path in nvidia_update_service.hpp keeps its own parsing,
 * since that path is already in the field.
 */
struct LogEntryProperties
{
    std::string objectPath;
    std::string messageId;
    std::vector<std::string> messageArgs;
    std::string eventId;
    std::string logNamespace;
    std::string resolution;
    std::string severity;

    /** @brief The entry among an object's interfaces, as GetManagedObjects
     *         returns them; empty when the object is not a log entry. */
    static std::optional<LogEntryProperties> fromInterfaces(
        const sdbusplus::object_path& objectPath,
        const dbus::utility::DBusInterfacesMap& interfaces);

    /** @brief The entry an InterfacesAdded signal announces. */
    static std::optional<LogEntryProperties> fromInterfacesAdded(
        sdbusplus::message_t& msg);

  private:
    void readProperty(const std::string& name,
                      const dbus::utility::DbusVariantType& value);
    void readAdditionalData(
        const std::vector<std::pair<std::string, std::string>>& data);
};

inline std::optional<LogEntryProperties> LogEntryProperties::fromInterfaces(
    const sdbusplus::object_path& objectPath,
    const dbus::utility::DBusInterfacesMap& interfaces)
{
    for (const auto& [interface, properties] : interfaces)
    {
        if (interface != "xyz.openbmc_project.Logging.Entry")
        {
            continue;
        }

        LogEntryProperties entry;
        entry.objectPath = objectPath.str;
        for (const auto& [name, value] : properties)
        {
            entry.readProperty(name, value);
        }
        return entry;
    }
    return std::nullopt;
}

inline std::optional<LogEntryProperties>
    LogEntryProperties::fromInterfacesAdded(sdbusplus::message_t& msg)
{
    dbus::utility::DBusInterfacesMap interfaces;
    sdbusplus::object_path objectPath;
    try
    {
        msg.read(objectPath, interfaces);
    }
    catch (const sdbusplus::exception_t& e)
    {
        BMCWEB_LOG_ERROR("Unable to read InterfacesAdded: {}", e.what());
        return std::nullopt;
    }
    return fromInterfaces(objectPath, interfaces);
}

inline void LogEntryProperties::readProperty(
    const std::string& name, const dbus::utility::DbusVariantType& value)
{
    if (name == "AdditionalData")
    {
        const auto* data =
            std::get_if<std::vector<std::pair<std::string, std::string>>>(
                &value);
        if (data != nullptr)
        {
            readAdditionalData(*data);
        }
        return;
    }

    const auto* text = std::get_if<std::string>(&value);
    if (text == nullptr)
    {
        return;
    }
    if (name == "EventId")
    {
        eventId = *text;
    }
    else if (name == "Resolution")
    {
        resolution = *text;
    }
    else if (name == "Severity")
    {
        severity = translateSeverityDbusToRedfish(*text);
    }
}

inline void LogEntryProperties::readAdditionalData(
    const std::vector<std::pair<std::string, std::string>>& data)
{
    redfish::AdditionalData additional(data);
    if (additional.contains("REDFISH_MESSAGE_ID"))
    {
        messageId = additional["REDFISH_MESSAGE_ID"];
    }
    if (additional.contains("REDFISH_MESSAGE_ARGS"))
    {
        bmcweb::split(messageArgs, additional["REDFISH_MESSAGE_ARGS"], ',');
    }
    if (additional.contains("namespace"))
    {
        logNamespace = additional["namespace"];
    }
}

/** @brief The message fields an entry sets over its registry's defaults. */
// NOLINTNEXTLINE(readability-identifier-naming)
inline void to_json(nlohmann::json& json, const LogEntryProperties& entry)
{
    json = nlohmann::json::object();
    if (!entry.resolution.empty())
    {
        json["Resolution"] = entry.resolution;
    }
    if (!entry.severity.empty())
    {
        json["MessageSeverity"] = entry.severity;
    }
}

} // namespace redfish::nvidia_log_entry
