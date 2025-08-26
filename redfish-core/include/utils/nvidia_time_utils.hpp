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
        result[pad - 1] = static_cast<char>('0' + (val % 10));
        val /= 10;
    }
    return result;
}

} // namespace details

inline std::time_t getTimestamp(uint64_t millisTimeStamp)
{
    // Retrieve Created property with format:
    // yyyy-mm-ddThh:mm:ss
    std::chrono::milliseconds chronoTimeStamp(millisTimeStamp);
    return std::chrono::duration_cast<std::chrono::duration<int>>(
               chronoTimeStamp)
        .count();
}

std::optional<std::string> toDurationStringFromNano(uint64_t timeNs);
} // namespace time_utils
} // namespace redfish
