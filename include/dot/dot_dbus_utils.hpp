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

#include "dot/base.hpp"
#include "logging.hpp"

#include <string>
#include <string_view>

namespace redfish::dot_utils
{

/**
 * @brief Convert auth scheme string to DBus enum format
 *
 * @param scheme Authentication scheme: "Ecdsa" or "Hybrid"
 * @return Full DBus enum path or empty string on error
 */
inline std::string convertAuthSchemeToDbusEnum(const std::string& scheme)
{
    if (scheme == "Ecdsa")
    {
        return std::string(dot::dotActionIntf) + ".KeyAuthScheme.Ecdsa";
    }
    if (scheme == "Hybrid")
    {
        return std::string(dot::dotActionIntf) + ".KeyAuthScheme.Hybrid";
    }
    BMCWEB_LOG_ERROR("Invalid authentication scheme: {}", scheme);
    return "";
}

} // namespace redfish::dot_utils
