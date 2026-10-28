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
#include "io_context_singleton.hpp"
#include "logging.hpp"

#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace redfish::device_prefix
{

// Entity Manager publishes the platform's device name prefix, e.g. "HGX_".
constexpr std::string_view prefixInterface =
    "xyz.openbmc_project.Configuration.DevicePrefix";

// Entity Manager may still be probing when bmcweb starts; platforms without
// a prefix never publish one, so stop after five minutes.
constexpr int maxLoadAttempts = 60;
constexpr std::chrono::seconds loadRetryDelay{5};

// The cached prefix; empty until loaded, and on platforms without one.
inline std::string& prefix()
{
    static std::string value;
    return value;
}

// "HGX_GPU_0" -> "GPU_0"; names without the prefix are returned unchanged.
inline std::string_view strip(std::string_view name)
{
    const std::string& p = prefix();
    if (!p.empty() && name.starts_with(p))
    {
        return name.substr(p.size());
    }
    return name;
}

// True when a D-Bus object name names the given Redfish component id.
inline bool matches(std::string_view dbusName, std::string_view id)
{
    return dbusName == id || strip(dbusName) == id;
}

// The chassis object name for a device name: "GPU_0" -> "HGX_GPU_0".
inline std::string chassisFor(std::string_view device)
{
    const std::string& p = prefix();
    if (p.empty() || device.starts_with(p))
    {
        return std::string(device);
    }
    return p + std::string(device);
}

inline void loadPrefix(int attempt);

inline void afterLoadRetryDelay(int attempt,
                                const boost::system::error_code& ec)
{
    if (!ec)
    {
        loadPrefix(attempt);
    }
}

inline void retryLoadPrefix(int attempt)
{
    if (attempt + 1 >= maxLoadAttempts)
    {
        BMCWEB_LOG_INFO("No device prefix published; using none");
        return;
    }
    static boost::asio::steady_timer timer(getIoContext());
    timer.expires_after(loadRetryDelay);
    timer.async_wait(std::bind_front(afterLoadRetryDelay, attempt + 1));
}

// Uses the prefix only when every published record agrees on it.
inline void applyPrefixes(const std::vector<std::string>& values)
{
    if (values.empty())
    {
        return;
    }
    if (std::ranges::any_of(values, [&values](const std::string& v) {
            return v != values.front();
        }))
    {
        BMCWEB_LOG_ERROR("Device prefix records disagree; using none");
        prefix().clear();
        return;
    }
    prefix() = values.front();
    BMCWEB_LOG_INFO("Device prefix: '{}'", prefix());
}

struct PrefixReads
{
    int attempt = 0;
    size_t pending = 0;
    bool failed = false;
    std::vector<std::string> values;
};

inline void afterGetPrefix(const std::shared_ptr<PrefixReads>& reads,
                           const boost::system::error_code& ec,
                           const std::string& value)
{
    if (ec)
    {
        BMCWEB_LOG_WARNING("Device prefix read failed: {}", ec.message());
        reads->failed = true;
    }
    else
    {
        reads->values.push_back(value);
    }
    if (--reads->pending != 0)
    {
        return;
    }
    if (reads->failed)
    {
        retryLoadPrefix(reads->attempt);
        return;
    }
    applyPrefixes(reads->values);
}

inline void afterGetPrefixObject(
    int attempt, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    // The mapper answers io_error until Entity Manager publishes the record.
    if (ec || subtree.empty())
    {
        retryLoadPrefix(attempt);
        return;
    }
    auto reads = std::make_shared<PrefixReads>();
    reads->attempt = attempt;
    for (const auto& [path, services] : subtree)
    {
        if (!services.empty())
        {
            reads->pending++;
        }
    }
    if (reads->pending == 0)
    {
        retryLoadPrefix(attempt);
        return;
    }
    for (const auto& [path, services] : subtree)
    {
        if (services.empty())
        {
            continue;
        }
        dbus::utility::getProperty<std::string>(
            services.front().first, path, std::string(prefixInterface),
            "Prefix", std::bind_front(afterGetPrefix, reads));
    }
}

inline void loadPrefix(int attempt)
{
    constexpr std::array<std::string_view, 1> interfaces{prefixInterface};
    dbus::utility::getSubTree("/xyz/openbmc_project/inventory", 0, interfaces,
                              std::bind_front(afterGetPrefixObject, attempt));
}

// Reads the prefix once after start-up, retrying while Entity Manager starts.
inline void startLoad()
{
    loadPrefix(0);
}

} // namespace redfish::device_prefix
