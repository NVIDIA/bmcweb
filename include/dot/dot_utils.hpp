/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION &
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

#include "bmcweb_config.h"

#include "dbus_utility.hpp"
#include "dot/base.hpp"

#include <sdbusplus/message.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace redfish::dot_utils
{

/**
 * @brief Find matching D-Bus object for a DOT component ID
 *
 * Searches through discovered D-Bus objects and finds one that matches
 * the component ID pattern, handling both prefixed and non-prefixed names.
 *
 * @param componentId The component ID (e.g., "IRoT_CPU_0")
 * @param resp The D-Bus discovery response containing object paths
 * @return Optional pair of (path, serviceMap) if found, nullopt otherwise
 */
inline std::optional<std::pair<
    std::string, std::vector<std::pair<std::string, std::vector<std::string>>>>>
    findMatchingDOTComponent(
        const std::string& componentId,
        const dbus::utility::MapperGetSubTreeResponse& resp)
{
    constexpr std::string_view platformPrefix(PLATFORMDEVICEPREFIX);

    for (const auto& [path, serviceMap] : resp)
    {
        sdbusplus::message::object_path objPath(path);
        std::string dbusComponentName = objPath.filename();

        if (dbusComponentName == componentId)
        {
            return std::make_pair(path, serviceMap);
        }

        std::string componentWithPrefix =
            std::string(platformPrefix) + componentId;
        if (dbusComponentName == componentWithPrefix)
        {
            return std::make_pair(path, serviceMap);
        }

        if (dbusComponentName.starts_with(platformPrefix))
        {
            std::string dbusWithoutPrefix =
                dbusComponentName.substr(platformPrefix.size());
            if (dbusWithoutPrefix == componentId)
            {
                return std::make_pair(path, serviceMap);
            }
        }
    }

    return std::nullopt;
}

} // namespace redfish::dot_utils
