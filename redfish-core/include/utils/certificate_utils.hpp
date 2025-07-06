/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION &
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

#include <string_view>

namespace redfish
{
namespace cert_utils
{

/**
 * @brief Parse and update Certificate Issue/Subject property
 *
 * @param[out] out JSON object to update with parsed values
 * @param[in] value Issuer/Subject value in key=value pairs
 * @return None
 */
inline void updateCertIssuerOrSubject(nlohmann::json& out,
                                      std::string_view value)
{
    // example: O=openbmc.xyz,CN=localhost or O=openbmc.xyz, CN=localhost
    std::string_view::iterator i = value.begin();
    while (i != value.end())
    {
        std::string_view::iterator tokenBegin = i;
        while (i != value.end() && *i != '=')
        {
            std::advance(i, 1);
        }
        if (i == value.end())
        {
            break;
        }
        std::string_view key(tokenBegin, static_cast<size_t>(i - tokenBegin));
        std::advance(i, 1);
        tokenBegin = i;
        while (i != value.end() && *i != ',')
        {
            std::advance(i, 1);
        }
        std::string_view val(tokenBegin, static_cast<size_t>(i - tokenBegin));
        if (key == "L")
        {
            out["City"] = val;
        }
        else if (key == "CN")
        {
            out["CommonName"] = val;
        }
        else if (key == "C")
        {
            out["Country"] = val;
        }
        else if (key == "O")
        {
            out["Organization"] = val;
        }
        else if (key == "OU")
        {
            out["OrganizationalUnit"] = val;
        }
        else if (key == "ST")
        {
            out["State"] = val;
        }
        // skip comma and any following spaces
        if (i != value.end())
        {
            i = std::find_if_not(std::next(i), value.end(), [](char c) {
                return c == ',' || c == ' ';
            });
        }
    }
}

} // namespace cert_utils
} // namespace redfish
