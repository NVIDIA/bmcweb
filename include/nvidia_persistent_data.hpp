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

#include <nlohmann/json.hpp>

namespace persistent_data::nvidia
{

class Config
{
  public:
    Config() = default;
    Config(const Config&) = delete;
    Config(Config&&) = delete;
    Config& operator=(const Config&) = delete;
    Config& operator=(const Config&&) = delete;

    void fromJson(const nlohmann::json::object_t&);
    void toJson(nlohmann::json::object_t&) const;

    void enableTLSAuth();
    bool isTLSAuthEnabled() const;

  private:
    bool currentTlsAuth{false};
    bool pendingTlsAuth{false};
};

inline Config& getConfig()
{
    static Config f;
    return f;
}

} // namespace persistent_data::nvidia
