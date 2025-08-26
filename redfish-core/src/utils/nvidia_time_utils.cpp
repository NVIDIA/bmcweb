// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

#include "utils/nvidia_time_utils.hpp"

#include "error_messages.hpp"
#include "http_response.hpp"
#include "logging.hpp"
#include "utils/time_utils.hpp"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <optional>
#include <ratio>
#include <string>
#include <string_view>
#include <utility>
#include <version>

#if __cpp_lib_chrono < 201907L
#include "utils/extern/date.h"
#endif
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <format>
#include <optional>
#include <ratio>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace redfish::time_utils
{
inline std::string nanoSecToDurationString(std::chrono::nanoseconds ns)
{
    if (ns < std::chrono::nanoseconds::zero())
    {
        return "";
    }

    std::string fmt;
    fmt.reserve(sizeof("PxxxxxxxxxxxxDTxxHxxMxx.xxxxxxS"));

    details::Days days = std::chrono::floor<details::Days>(ns);
    ns -= days;

    std::chrono::hours hours = std::chrono::floor<std::chrono::hours>(ns);
    ns -= hours;

    std::chrono::minutes minutes = std::chrono::floor<std::chrono::minutes>(ns);
    ns -= minutes;

    std::chrono::seconds seconds = std::chrono::floor<std::chrono::seconds>(ns);
    ns -= seconds;

    fmt = "P";
    if (days.count() > 0)
    {
        fmt += std::to_string(days.count()) + "D";
    }
    fmt += "T";
    if (hours.count() > 0)
    {
        fmt += std::to_string(hours.count()) + "H";
    }
    if (minutes.count() > 0)
    {
        fmt += std::to_string(minutes.count()) + "M";
    }

    if (seconds.count() != 0 || ns.count() != 0)
    {
        fmt += std::to_string(seconds.count()) + ".";
        fmt += details::padZeros(ns.count(), 9);
        fmt += "S";
    }
    else if (fmt == "PT")
    {
        fmt += "0S"; // Append "0S" when time is zero seconds
    }

    return fmt;
}

std::optional<std::string> toDurationStringFromNano(const uint64_t timeNs)
{
    static const uint64_t maxTimeNs =
        static_cast<uint64_t>(std::chrono::nanoseconds::max().count());

    if (maxTimeNs < timeNs)
    {
        return std::nullopt;
    }

    std::chrono::nanoseconds nanosecs(timeNs);

    std::string duration = nanoSecToDurationString(nanosecs);
    if (duration.empty())
    {
        return std::nullopt;
    }

    return std::make_optional(duration);
}
} // namespace redfish::time_utils
