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
#include "nvidia_persistent_data.hpp"

#include "bmcweb_config.h"

#include "persistent_data.hpp"

namespace persistent_data::nvidia
{

void Config::fromJson(const nlohmann::json::object_t& json)
{
    const bool* ptr = json.at("tls_auth_enabled").get_ptr<const bool*>();
    if (ptr != nullptr)
    {
        currentTlsAuth = *ptr;
        pendingTlsAuth = *ptr;
    }
}

void Config::toJson(nlohmann::json::object_t& json) const
{
    json["tls_auth_enabled"] = pendingTlsAuth;
}

void Config::enableTLSAuth()
{
    pendingTlsAuth = true;
    persistent_data::getConfig().writeData();
}

bool Config::isTLSAuthEnabled() const
{
    if constexpr (BMCWEB_TLS_AUTH_OPT_IN)
    {
        return currentTlsAuth;
    }
    return true;
}

} // namespace persistent_data::nvidia
