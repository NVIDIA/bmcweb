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

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace nlohmann
{
template <typename T>
struct adl_serializer<std::optional<T>>
{
    static void to_json(json& j, const std::optional<T>& opt)
    {
        if (opt.has_value())
        {
            j = *opt;
        }
        else
        {
            j = nullptr;
        }
    }

    static void from_json(const json& j, std::optional<T>& opt)
    {
        if (j.is_null())
        {
            opt = std::nullopt;
        }
        else
        {
            opt = j.get<T>();
        }
    }
};
} // namespace nlohmann

namespace nvidia
{
namespace nsm_utils
{
inline std::optional<int64_t> tryConvertToInt64(double number)
{
    // Check for NaN
    if (std::isnan(number))
    {
        return std::nullopt;
    }

    // Check if the number is too large to fit in a int64_t
    if (number > static_cast<double>(std::numeric_limits<int64_t>::max()) ||
        number < 0)
    {
        return std::nullopt;
    }

    // If all checks pass, return the number as int64_t
    return static_cast<int64_t>(number);
}
} // namespace nsm_utils
} // namespace nvidia
