/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Mapping D-Bus properties to Redfish JSON with tombstone handling.
 *
 * A "tombstone" is the out-of-domain marker a backend stamps on a property
 * when it has no genuine reading to report. This utility translates that
 * marker into the representation the property's Redfish schema allows, so a
 * stale or default value is never mistaken for a real reading.
 *
 * Each D-Bus type has type-specific tombstone values:
 *   string   : "" (empty) and "NOT_SUPPORTED"
 *   enum     : determined by EnumTranslator return value
 *   uint32_t : numeric_limits<uint32_t>::max()
 *   uint64_t : numeric_limits<uint64_t>::max()
 *   size_t   : numeric_limits<size_t>::max()
 *   double   : NaN
 *
 * Numeric markers are stamped by nsmd's Sentinel<T> - keep the values here in
 * lock-step with nsmd/common/telemetryTombstone.hpp.
 *
 * Public API - all named mapValidOr*:
 *   mapValidOrNull (json, key, value)              -- string / numeric
 *   mapValidOrNull (json, key, value, xlate)       -- enum -> string
 *   mapValidOrNull (json, key, value, boolXlate)   -- enum -> bool
 *   mapValidOrOmit (json, key, value)              -- string / numeric
 *   mapValidOrOmit (json, key, value, xlate)       -- enum -> string
 *   mapValidOrOmit (json, key, value, boolXlate)   -- enum -> bool
 *   mapValidOrEmpty(json, key, value)              -- string only
 *
 * Aggregate helpers (numeric, nullable) - a marker in any operand yields null,
 * so a "no reading" is never hidden inside an arithmetic result:
 *   mapSumOrNull         -- N distinct readings added at once
 *
 * For strings, NOT_SUPPORTED always omits the key.
 *
 * For enums, the EnumTranslator decides the tombstone state: the utility does
 * not inspect D-Bus enum strings directly. Translators should return "" for
 * .Unknown (policy decides null vs omit) and std::nullopt for .Unsupported
 * (always omit).
 */

#pragma once

#include <nlohmann/json.hpp>

#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace redfish
{

constexpr std::string_view propertyNotSupported = "NOT_SUPPORTED";

using EnumTranslator =
    std::function<std::optional<std::string>(const std::string&)>;

// Same idea as EnumTranslator, but the Redfish property is a boolean: the
// translator maps a D-Bus enum to true/false, or std::nullopt for the
// tombstone (no reading / unsupported). Lets a property stay a schema boolean
// while flowing through the same enum tombstone pipeline (no schema change).
using BoolEnumTranslator =
    std::function<std::optional<bool>(const std::string&)>;

namespace details
{

enum class TombstonePolicy
{
    allowEmpty,
    schemaAllowsNull,
    omitResponse
};

/**
 * @brief Map a string D-Bus value to JSON, handling "" and "NOT_SUPPORTED".
 * @param[in,out] json Response object to populate.
 * @param[in] key      Redfish property name.
 * @param[in] value    D-Bus string value (may be nullptr).
 * @param[in] policy   How to represent an empty string.
 */
inline void mapString(nlohmann::json& json, const std::string& key,
                      const std::string* value, TombstonePolicy policy)
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
        switch (policy)
        {
            case TombstonePolicy::allowEmpty:
                json[key] = "";
                return;
            case TombstonePolicy::schemaAllowsNull:
                json[key] = nullptr;
                return;
            case TombstonePolicy::omitResponse:
            default:
                return;
        }
    }
    json[key] = *value;
}

/**
 * @brief Map a D-Bus enum to JSON via an EnumTranslator.
 *
 * Translator returns "" for unknown, std::nullopt for unsupported.
 * @param[in,out] json Response object to populate.
 * @param[in] key      Redfish property name.
 * @param[in] value    D-Bus enum string (may be nullptr).
 * @param[in] translate Maps a D-Bus enum to its Redfish value / tombstone.
 * @param[in] policy   How to represent the unknown ("") state.
 */
inline void mapEnum(nlohmann::json& json, const std::string& key,
                    const std::string* value, const EnumTranslator& translate,
                    TombstonePolicy policy)
{
    if (value == nullptr)
    {
        return;
    }
    std::optional<std::string> redfishValue = translate(*value);
    if (!redfishValue)
    {
        return;
    }
    if (redfishValue->empty())
    {
        if (policy == TombstonePolicy::schemaAllowsNull)
        {
            json[key] = nullptr;
        }
        return;
    }
    json[key] = *redfishValue;
}

/**
 * @brief Map a D-Bus enum to a boolean Redfish property via a translator.
 *
 * Same shape as mapEnum, but assigns a JSON bool: the translator returns
 * true/false for the meaningful states, or std::nullopt for the tombstone
 * (no reading / unsupported), which @p policy renders as null or omit.
 * @param[in,out] json Response object to populate.
 * @param[in] key       Redfish property name.
 * @param[in] value     D-Bus enum string (may be nullptr).
 * @param[in] translate Maps a D-Bus enum to true/false or nullopt.
 * @param[in] policy    How to represent the tombstone (null vs omit).
 */
inline void mapEnumBool(nlohmann::json& json, const std::string& key,
                        const std::string* value,
                        const BoolEnumTranslator& translate,
                        TombstonePolicy policy)
{
    if (value == nullptr)
    {
        return;
    }
    std::optional<bool> redfishValue = translate(*value);
    if (!redfishValue)
    {
        if (policy == TombstonePolicy::schemaAllowsNull)
        {
            json[key] = nullptr;
        }
        return;
    }
    json[key] = *redfishValue;
}

/**
 * @brief True if a numeric value is the backend "no reading" marker.
 *
 * Integer type-max (uint16/int32/int64/size_t/...) or NaN for double.
 * @tparam T Numeric type (integral non-bool, or double).
 * @param[in] value Value to test.
 */
template <typename T>
inline bool isTombstone(const T& value)
{
    if constexpr (std::is_same_v<T, double>)
    {
        return std::isnan(value);
    }
    else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
    {
        return value == std::numeric_limits<T>::max();
    }
    else
    {
        static_assert(
            !sizeof(T),
            "Unsupported type for generating redfish response using isTombstone");
    }
}

/**
 * @brief Map a numeric D-Bus value to JSON, converting the "no reading" marker.
 * @tparam T Numeric type.
 * @param[in,out] json Response object to populate.
 * @param[in] key      Redfish property name.
 * @param[in] value    D-Bus numeric value (may be nullptr).
 * @param[in] policy   How to represent the marker (null vs omit).
 */
template <typename T>
inline void mapNumeric(nlohmann::json& json, const std::string& key,
                       const T* value, TombstonePolicy policy)
{
    if (value == nullptr)
    {
        return;
    }
    if (isTombstone(*value))
    {
        if (policy == TombstonePolicy::schemaAllowsNull)
        {
            json[key] = nullptr;
        }
        return;
    }
    json[key] = *value;
}

} // namespace details

/*
 * Public API - tombstone -> null (nullable Redfish schema)
 *
 * Usage:
 *   mapValidOrNull(json, "SerialNumber", strPtr);
 *   mapValidOrNull(json, "SpeedGbps", doublePtr);
 *   mapValidOrNull(json, "PortType", enumPtr, translator);
 */

/** @brief Nullable string property: "" -> null, "NOT_SUPPORTED" -> omit. */
inline void mapValidOrNull(nlohmann::json& json, const std::string& key,
                           const std::string* value)
{
    details::mapString(json, key, value,
                       details::TombstonePolicy::schemaAllowsNull);
}

/** @brief Nullable numeric property: type-max/NaN -> null. @tparam T type. */
template <typename T>
inline void mapValidOrNull(nlohmann::json& json, const std::string& key,
                           const T* value)
{
    details::mapNumeric(json, key, value,
                        details::TombstonePolicy::schemaAllowsNull);
}

/** @brief Nullable enum property: translator "" -> null, nullopt -> omit. */
inline void mapValidOrNull(nlohmann::json& json, const std::string& key,
                           const std::string* value,
                           const EnumTranslator& translate)
{
    details::mapEnum(json, key, value, translate,
                     details::TombstonePolicy::schemaAllowsNull);
}

/** @brief Nullable enum-as-bool: translator true/false -> bool,
 *         nullopt (no reading / unsupported) -> null. */
inline void mapValidOrNull(nlohmann::json& json, const std::string& key,
                           const std::string* value,
                           const BoolEnumTranslator& translate)
{
    details::mapEnumBool(json, key, value, translate,
                         details::TombstonePolicy::schemaAllowsNull);
}

/*
 * Public API - tombstone -> omit key (non-nullable Redfish schema)
 *
 * Usage:
 *   mapValidOrOmit(json, "Location", strPtr);
 *   mapValidOrOmit(json, "PortCount", uint32Ptr);
 *   mapValidOrOmit(json, "LinkState", enumPtr, translator);
 */

/** @brief Non-nullable string property: "" or "NOT_SUPPORTED" -> omit. */
inline void mapValidOrOmit(nlohmann::json& json, const std::string& key,
                           const std::string* value)
{
    details::mapString(json, key, value,
                       details::TombstonePolicy::omitResponse);
}

/**
 * @brief Non-nullable numeric property: type-max/NaN -> omit.
 * @tparam T Numeric type.
 */
template <typename T>
inline void mapValidOrOmit(nlohmann::json& json, const std::string& key,
                           const T* value)
{
    details::mapNumeric(json, key, value,
                        details::TombstonePolicy::omitResponse);
}

/** @brief Non-nullable enum property: translator "" or nullopt -> omit. */
inline void mapValidOrOmit(nlohmann::json& json, const std::string& key,
                           const std::string* value,
                           const EnumTranslator& translate)
{
    details::mapEnum(json, key, value, translate,
                     details::TombstonePolicy::omitResponse);
}

/** @brief Non-nullable enum-as-bool: translator true/false -> bool,
 *         nullopt (no reading / unsupported) -> omit. */
inline void mapValidOrOmit(nlohmann::json& json, const std::string& key,
                           const std::string* value,
                           const BoolEnumTranslator& translate)
{
    details::mapEnumBool(json, key, value, translate,
                         details::TombstonePolicy::omitResponse);
}

/*
 * Public API - tombstone -> "" (string only, user-writable fields)
 *
 * Usage:
 *   mapValidOrEmpty(json, "AssetTag", strPtr);
 */

/** @brief User-writable string property: "" -> "", "NOT_SUPPORTED" -> omit. */
inline void mapValidOrEmpty(nlohmann::json& json, const std::string& key,
                            const std::string* value)
{
    details::mapString(json, key, value, details::TombstonePolicy::allowEmpty);
}

/*
 * Public API - aggregate helper (numeric, nullable schema)
 *
 * A marker in any operand collapses the whole result to null, so a "no reading"
 * is never buried inside a summed value, and a raw type-max never enters the
 * arithmetic.
 */

/**
 * @brief Sum of N distinct readings available at once; any marker -> null.
 *
 * Every operand is a reading, so every operand is marker-checked. An absent
 * (nullptr) or marker operand yields null, which also avoids type-max + x
 * overflow.
 * @tparam T Numeric reading type.
 * @param[in,out] json    Response object to populate.
 * @param[in] key         Redfish property name.
 * @param[in] readings    Pointers to each reading (any may be nullptr).
 */
template <typename T>
inline void mapSumOrNull(nlohmann::json& json, const std::string& key,
                         std::initializer_list<const T*> readings)
{
    T total = 0;
    for (const T* reading : readings)
    {
        if (reading == nullptr || details::isTombstone(*reading))
        {
            json[key] = nullptr;
            return;
        }
        total += *reading;
    }
    json[key] = total;
}

} // namespace redfish
