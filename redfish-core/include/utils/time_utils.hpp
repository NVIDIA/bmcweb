// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "http_response.hpp"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <optional>
#include <ratio>
#include <string>
#include <string_view>
#include <utility>

namespace redfish
{

namespace time_utils
{

namespace details
{

constexpr intmax_t dayDuration = static_cast<intmax_t>(24 * 60 * 60);
using Days = std::chrono::duration<long long, std::ratio<dayDuration>>;

// Creates a string from an integer in the most efficient way possible without
// using std::locale.  Adds an exact zero pad based on the pad input parameter.
// Does not handle negative numbers.
inline std::string padZeros(int64_t value, size_t pad)
{
    std::string result(pad, '0');
    for (int64_t val = value; pad > 0; pad--)
    {
        result[pad - 1] = static_cast<char>('0' + val % 10);
        val /= 10;
    }
    return result;
}

} // namespace details

/**
 * @brief Convert string that represents value in Duration Format to its numeric
 *        equivalent.
 */
std::optional<std::chrono::milliseconds> fromDurationString(std::string_view v);

/**
 * @brief Convert time value into duration format that is based on ISO 8601.
 *        Example output: "P12DT1M5.5S"
 *        Ref: Redfish Specification, Section 9.4.4. Duration values
 */
std::string toDurationString(std::chrono::milliseconds ms);

std::optional<std::string> toDurationStringFromUint(uint64_t timeMs);

// Returns the formatted date time string.
// Note that the maximum supported date is 9999-12-31T23:59:59+00:00, if
// the given |secondsSinceEpoch| is too large, we return the maximum supported
// date.
std::string getDateTimeUint(uint64_t secondsSinceEpoch);

// Returns the formatted date time string with millisecond precision
// Note that the maximum supported date is 9999-12-31T23:59:59+00:00, if
// the given |secondsSinceEpoch| is too large, we return the maximum supported
// date.
std::string getDateTimeUintMs(uint64_t milliSecondsSinceEpoch);

// Returns the formatted date time string with microsecond precision
std::string getDateTimeUintUs(uint64_t microSecondsSinceEpoch);

std::string getDateTimeStdtime(std::time_t secondsSinceEpoch);

/**
 * Returns the current Date, Time & the local Time Offset
 * information in a pair
 *
 * @param[in] None
 *
 * @return std::pair<std::string, std::string>, which consist
 * of current DateTime & the TimeOffset strings respectively.
 */
std::pair<std::string, std::string> getDateTimeOffsetNow();

inline std::time_t getTimestamp(uint64_t millisTimeStamp)
{
    // Retrieve Created property with format:
    // yyyy-mm-ddThh:mm:ss
    std::chrono::milliseconds chronoTimeStamp(millisTimeStamp);
    return std::chrono::duration_cast<std::chrono::duration<int>>(
               chronoTimeStamp)
        .count();
}

using usSinceEpoch = std::chrono::duration<int64_t, std::micro>;
std::optional<usSinceEpoch> dateStringToEpoch(std::string_view datetime);

/**
 * @brief Returns the datetime in ISO 8601 format
 *
 * @param[in] std::string_view the date of item manufacture in ISO 8601 format,
 *            either as YYYYMMDD or YYYYMMDDThhmmssZ
 * Ref: https://github.com/openbmc/phosphor-dbus-interfaces/blob/master/yaml/
 *      xyz/openbmc_project/Inventory/Decorator/Asset.interface.yaml#L16
 *
 * @return std::string which consist the datetime
 */
std::optional<std::string> getDateTimeIso8601(std::string_view datetime);

/**
 * @brief ProductionDate report
 */
void productionDateReport(crow::Response& res, const std::string& buildDate);

std::optional<std::string> toDurationStringFromNano(uint64_t timeNs);
} // namespace time_utils
} // namespace redfish
