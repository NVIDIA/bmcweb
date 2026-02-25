/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mapping D-Bus string properties to Redfish JSON.
 * Backend tombstone values (empty, "NOT_SUPPORTED") are converted
 * based on the property's Redfish schema type:
 *
 * Public API:
 *   mapStringOrEmpty -- empty -> "" (user-writable, spec 9.5.3)
 *   mapStringOrNull  -- empty -> null (device-reported, nullable)
 *   mapStringOrOmit  -- empty -> omit key (optional, non-nullable)
 *
 * In all cases, NOT_SUPPORTED -> omit key.
 */

#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace redfish
{

constexpr std::string_view propertyNotSupported = "NOT_SUPPORTED";

namespace details
{

enum class EmptyStringPolicy
{
    allowEmpty,
    schemaAllowsNull,
    omitResponse
};

inline void mapString(nlohmann::json& json, const std::string& key,
                      const std::string* value, EmptyStringPolicy emptyPolicy)
{
    if (value == nullptr)
    {
        return;
    }
    if (*value == propertyNotSupported)
    {
        return;
    }
    if (value->empty())
    {
        switch (emptyPolicy)
        {
            case EmptyStringPolicy::allowEmpty:
                json[key] = "";
                return;
            case EmptyStringPolicy::schemaAllowsNull:
                json[key] = nullptr;
                return;
            case EmptyStringPolicy::omitResponse:
            default:
                return;
        }
    }
    json[key] = *value;
}

} // namespace details

inline void mapStringOrEmpty(nlohmann::json& json, const std::string& key,
                             const std::string* value)
{
    details::mapString(json, key, value,
                       details::EmptyStringPolicy::allowEmpty);
}

inline void mapStringOrNull(nlohmann::json& json, const std::string& key,
                            const std::string* value)
{
    details::mapString(json, key, value,
                       details::EmptyStringPolicy::schemaAllowsNull);
}

inline void mapStringOrOmit(nlohmann::json& json, const std::string& key,
                            const std::string* value)
{
    details::mapString(json, key, value,
                       details::EmptyStringPolicy::omitResponse);
}

} // namespace redfish
