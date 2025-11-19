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

#include "error_message_utils.hpp"
#include "error_messages.hpp"
#include "registries/oem/nvidia_resource_event_message_registry.hpp"
#include "registries/oem/nvidia_update_message_registry.hpp"

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
inline nlohmann::json getLogNvidia(
    redfish::registries::NvidiaUpdate::Index name,
    std::span<const std::string_view> args)
{
    size_t index = static_cast<size_t>(name);
    if (index >= redfish::registries::NvidiaUpdate::registry.size())
    {
        return {};
    }
    return getLogFromRegistry(redfish::registries::NvidiaUpdate::header,
                              redfish::registries::NvidiaUpdate::registry,
                              index, args);
}

/**
 * @brief Method to get error message from NVIDIA resource event registry
 */
inline nlohmann::json getLogNvidia(
    redfish::registries::NvidiaResourceEvent::Index name,
    std::span<const std::string_view> args)
{
    size_t index = static_cast<size_t>(name);
    if (index >= redfish::registries::NvidiaResourceEvent::registry.size())
    {
        return {};
    }
    return getLogFromRegistry(
        redfish::registries::NvidiaResourceEvent::header,
        redfish::registries::NvidiaResourceEvent::registry, index, args);
}

inline nlohmann::json debugTokenAlreadyInstalled(std::string_view arg1)
{
    std::array<std::string_view, 1> args{arg1};
    return getLogNvidia(
        redfish::registries::NvidiaUpdate::Index::debugTokenAlreadyInstalled,
        args);
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
        redfish::registries::NvidiaUpdate::Index::debugTokenInstallationSuccess,
        args);
}

inline nlohmann::json debugTokenRequestSuccess(std::string_view arg1)
{
    std::array<std::string_view, 1> args{arg1};
    return getLogNvidia(
        redfish::registries::NvidiaUpdate::Index::debugTokenRequestSuccess,
        args);
}

inline nlohmann::json debugTokenStatusSuccess(std::string_view arg1)
{
    std::array<std::string_view, 1> args{arg1};
    return getLogNvidia(
        redfish::registries::NvidiaUpdate::Index::debugTokenStatusSuccess,
        args);
}

inline nlohmann::json debugTokenUnsupported(std::string_view arg1)
{
    std::array<std::string_view, 1> args{arg1};
    return getLogNvidia(
        redfish::registries::NvidiaUpdate::Index::debugTokenUnsupported, args);
}

inline void debugTokenUnsupported(crow::Response& res, std::string_view arg1)
{
    res.result(boost::beast::http::status::not_implemented);
    addMessageToErrorJson(res.jsonValue, debugTokenUnsupported(arg1));
}

inline nlohmann::json debugTokenEraseFailed(std::string_view arg1,
                                            std::string_view arg2)
{
    std::array<std::string_view, 2> args{arg1, arg2};
    return getLogNvidia(
        redfish::registries::NvidiaUpdate::Index::debugTokenEraseFailed, args);
}

inline nlohmann::json debugTokenEraseSuccess(std::string_view arg1)
{
    std::array<std::string_view, 1> args{arg1};
    return getLogNvidia(
        redfish::registries::NvidiaUpdate::Index::debugTokenEraseSuccess, args);
}

inline nlohmann::json debugTokenInstallationFailed(std::string_view arg1,
                                                   std::string_view arg2)
{
    std::array<std::string_view, 2> args{arg1, arg2};
    return getLogNvidia(
        redfish::registries::NvidiaUpdate::Index::debugTokenInstallationFailed,
        args);
}

inline nlohmann::json dotActionResponseError(std::string_view arg1)
{
    std::array<std::string_view, 1> args{arg1};
    return getLogNvidia(
        redfish::registries::NvidiaUpdate::Index::dOTActionResponseError, args);
}

inline void dotActionResponseError(crow::Response& res, std::string_view arg1)
{
    res.result(boost::beast::http::status::internal_server_error);
    addMessageToErrorJson(res.jsonValue, dotActionResponseError(arg1));
}

inline nlohmann::json dotMctpStatusError(std::string_view arg1)
{
    std::array<std::string_view, 1> args{arg1};
    return getLogNvidia(
        redfish::registries::NvidiaUpdate::Index::dOTMCTPStatusError, args);
}

inline void dotMctpStatusError(crow::Response& res, std::string_view arg1)
{
    res.result(boost::beast::http::status::internal_server_error);
    addMessageToErrorJson(res.jsonValue, dotMctpStatusError(arg1));
}

inline nlohmann::json componentUpdateSkipped(std::string_view arg1,
                                             std::string_view arg2)
{
    std::array<std::string_view, 2> args{arg1, arg2};
    return getLogNvidia(
        redfish::registries::NvidiaUpdate::Index::componentUpdateSkipped, args);
}

inline nlohmann::json recoveryStarted(std::string_view arg1)
{
    std::array<std::string_view, 1> args{arg1};
    return getLogNvidia(
        redfish::registries::NvidiaUpdate::Index::recoveryStarted, args);
}

inline nlohmann::json recoverySuccessful(std::string_view arg1)
{
    std::array<std::string_view, 1> args{arg1};
    return getLogNvidia(
        redfish::registries::NvidiaUpdate::Index::recoverySuccessful, args);
}

inline nlohmann::json firmwareNotInRecovery(std::string_view arg1)
{
    std::array<std::string_view, 1> args{arg1};
    return getLogNvidia(
        redfish::registries::NvidiaUpdate::Index::firmwareNotInRecovery, args);
}

inline nlohmann::json stageSuccessful(std::string_view arg1,
                                      std::string_view arg2)
{
    (void)arg2;
    std::array<std::string_view, 1> args{arg1};
    return getLogNvidia(
        redfish::registries::NvidiaUpdate::Index::stageSuccessful, args);
}

inline nlohmann::json bmcDriverErrorsDetected(
    std::string_view arg1, std::string_view arg2, std::string_view arg3)
{
    std::array<std::string_view, 3> args{arg1, arg2, arg3};
    return getLogNvidia(redfish::registries::NvidiaResourceEvent::Index::
                            bmcDriverErrorsDetected,
                        args);
}

inline nlohmann::json deviceDriverErrorsDetected(
    std::string_view arg1, std::string_view arg2, std::string_view arg3)
{
    std::array<std::string_view, 3> args{arg1, arg2, arg3};
    return getLogNvidia(redfish::registries::NvidiaResourceEvent::Index::
                            deviceDriverErrorsDetected,
                        args);
}

inline nlohmann::json actionParameterNotSupported(
    std::string_view actionParameterValue, std::string_view actionParameter,
    std::string_view actionInfoURI)
{
    std::array<std::string_view, 3> args{actionParameterValue, actionParameter,
                                         actionInfoURI};
    return getLogNvidia(
        redfish::registries::NvidiaUpdate::Index::actionParameterNotSupported,
        args);
}

inline nlohmann::json headerValueInvalid(
    std::string_view arg1, std::string_view arg2, std::string_view arg3)
{
    std::array<std::string_view, 3> args{arg1, arg2, arg3};
    return getLogNvidia(
        redfish::registries::NvidiaUpdate::Index::headerValueInvalid, args);
}
} // namespace redfish::messages
