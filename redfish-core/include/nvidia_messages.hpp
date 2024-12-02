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

#include "error_messages.hpp"
#include "registries/oem/nvidia_message_registry.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <span>

namespace redfish::messages
{

/**
 * @brief Method to get error message from NVIDIA message registry
 *
 * @param[in] name - registry index
 * @param[in] args - argument
 * @return nlohmann::json
 */
inline nlohmann::json getLogNvidia(redfish::registries::nvidia::Index name,
                                   std::span<const std::string_view> args)
{
    size_t index = static_cast<size_t>(name);
    if (index >= redfish::registries::nvidia::registry.size())
    {
        return {};
    }
    return getLogFromRegistry(redfish::registries::nvidia::header,
                              redfish::registries::nvidia::registry, index,
                              args);
}

inline nlohmann::json debugTokenAlreadyInstalled(std::string_view arg1)
{
    std::array<std::string_view, 1> args{arg1};
    return getLogNvidia(
        redfish::registries::nvidia::Index::debugTokenAlreadyInstalled, args);
}

inline void debugTokenAlreadyInstalled(crow::Response& res,
                                       std::string_view arg1)
{
    res.result(boost::beast::http::status::service_unavailable);
    addMessageToErrorJson(res.jsonValue, debugTokenAlreadyInstalled(arg1));
}

inline nlohmann::json debugTokenInstallationSuccess(std::string_view arg1)
{
    std::array<std::string_view, 1> args{arg1};
    return getLogNvidia(
        redfish::registries::nvidia::Index::debugTokenInstallationSuccess,
        args);
}

inline nlohmann::json debugTokenRequestSuccess(std::string_view arg1)
{
    std::array<std::string_view, 1> args{arg1};
    return getLogNvidia(
        redfish::registries::nvidia::Index::debugTokenRequestSuccess, args);
}

inline nlohmann::json debugTokenStatusSuccess(std::string_view arg1)
{
    std::array<std::string_view, 1> args{arg1};
    return getLogNvidia(
        redfish::registries::nvidia::Index::debugTokenStatusSuccess, args);
}

inline nlohmann::json debugTokenUnsupported(std::string_view arg1)
{
    std::array<std::string_view, 1> args{arg1};
    return getLogNvidia(
        redfish::registries::nvidia::Index::debugTokenUnsupported, args);
}

inline void debugTokenUnsupported(crow::Response& res, std::string_view arg1)
{
    res.result(boost::beast::http::status::not_implemented);
    addMessageToErrorJson(res.jsonValue, debugTokenUnsupported(arg1));
}

inline nlohmann::json
    debugTokenEraseFailed(std::string_view arg1, std::string_view arg2)
{
    std::array<std::string_view, 2> args{arg1, arg2};
    return getLogNvidia(
        redfish::registries::nvidia::Index::debugTokenEraseFailed, args);
}

} // namespace redfish::messages
