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

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Json = nlohmann::json;

struct CommitImageValueEntry
{
  public:
    std::string inventoryUri;
    uint32_t mctpEndpointId = {};

    friend bool operator==(const CommitImageValueEntry& c1,
                           const std::string& c2)
    {
        return c1.inventoryUri == c2;
    }
};

inline std::vector<CommitImageValueEntry> getAllowableValues()
{
    static std::vector<CommitImageValueEntry> allowableValues;

    if (allowableValues.empty() == false)
    {
        return allowableValues;
    }

    std::string configPath(BMCWEB_FW_MCTP_MAPPING_JSON);

    if (!fs::exists(configPath))
    {
        BMCWEB_LOG_ERROR("The file doesn't exist: {}", configPath);
        return allowableValues;
    }

    std::ifstream jsonFile(configPath);
    auto data = Json::parse(jsonFile, nullptr, false);
    if (data.is_discarded())
    {
        BMCWEB_LOG_ERROR("Unable to parse json data {}", configPath);
        return allowableValues;
    }

    const Json emptyJson{};

    auto entries = data.value("FwMctpMap", emptyJson);
    for (const auto& element : entries.items())
    {
        try
        {
            CommitImageValueEntry allowableVal;

            allowableVal.inventoryUri = element.key();
            allowableVal.mctpEndpointId =
                static_cast<uint32_t>(element.value());

            allowableValues.push_back(allowableVal);
        }
        catch ([[maybe_unused]] const std::exception& e)
        {
            BMCWEB_LOG_ERROR("FW MCTP EID map format error.");
        }
    }

    return allowableValues;
}
