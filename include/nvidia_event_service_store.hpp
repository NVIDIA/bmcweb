// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace persistent_data
{

// Returns true if the key was handled, false otherwise
inline bool handleOriginFields(const std::string& key,
                               const nlohmann::json& value,
                               std::vector<std::string>& originResources,
                               bool& includeOriginOfCondition)
{
    if (key == "OriginResources")
    {
        const nlohmann::json::array_t* obj =
            value.get_ptr<const nlohmann::json::array_t*>();
        if (obj == nullptr)
        {
            return true;
        }
        for (const auto& val : *obj)
        {
            const std::string* parsedValue =
                val.get_ptr<const std::string*>();
            if (parsedValue == nullptr)
            {
                continue;
            }
            originResources.emplace_back(*parsedValue);
        }
        return true;
    }
    if (key == "IncludeOriginOfCondition")
    {
        const bool* parsedValue = value.get_ptr<const bool*>();
        if (parsedValue == nullptr)
        {
            return true;
        }
        includeOriginOfCondition = *parsedValue;
        return true;
    }
    return false;
}

} // namespace persistent_data 