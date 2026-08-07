/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "logging.hpp"

#include <cstdio>
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace nvidia
{
namespace logging
{

template <crow::LogLevel level, typename... Args>
inline void vlogWithoutLevelCheck(std::format_string<Args...>&& format,
                                  Args&&... args,
                                  const std::source_location& loc) noexcept
{
    constexpr int systemdLevel = crow::toSystemdLevel(level);
    std::string_view filename = loc.file_name();
    const size_t separator = filename.rfind('/');
    if (separator != std::string_view::npos)
    {
        filename.remove_prefix(separator + 1);
    }
    std::string logLocation;
    try
    {
        logLocation =
            std::format("<{}>[{}:{}] ", systemdLevel, filename, loc.line());
        logLocation +=
            std::format(std::move(format), std::forward<Args>(args)...);
        logLocation += '\n';
        // Intentionally ignore error return.
        fwrite(logLocation.data(), sizeof(std::string::value_type),
               logLocation.size(), stdout);
    }
    catch (...)
    {
        constexpr std::string_view formatFailure = "Failed to format\n";
        // Avoid allocating while handling a logging failure.
        fwrite(formatFailure.data(), sizeof(std::string_view::value_type),
               formatFailure.size(), stdout);
    }
    fflush(stdout);
}

} // namespace logging
} // namespace nvidia

/**
 * NVIDIA logging helpers emit messages at their specified priority
 * independently of the configured bmcweb log level.
 */
template <typename... Args>
// NOLINTNEXTLINE(readability-identifier-naming)
struct NVIDIA_LOG_INFO
{
    explicit NVIDIA_LOG_INFO(std::format_string<Args...> format, Args&&... args,
                             const std::source_location& loc =
                                 std::source_location::current()) noexcept
    {
        nvidia::logging::vlogWithoutLevelCheck<crow::LogLevel::Info, Args...>(
            std::move(format), std::forward<Args>(args)..., loc);
    }
};

template <typename... Args>
NVIDIA_LOG_INFO(std::format_string<Args...>, Args&&...)
    -> NVIDIA_LOG_INFO<Args...>;
