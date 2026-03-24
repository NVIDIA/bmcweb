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

#include <cstdint>
#include <string_view>

namespace redfish::debug_token
{

constexpr const std::string_view debugTokenIntf = "com.nvidia.DebugToken";
constexpr const std::string_view debugTokenActionIntf =
    "com.nvidia.DebugToken.Action";
constexpr const std::string_view debugTokenStatusIntf =
    "com.nvidia.DebugToken.Status";
constexpr const std::string_view debugTokenBasePath =
    "/xyz/openbmc_project/debug_token";
constexpr const std::string_view debugTokenOpcodesEnumPrefix =
    "com.nvidia.DebugToken.TokenOpcodes.";
constexpr const std::string_view debugTokenTypesEnumPrefix =
    "com.nvidia.DebugToken.TokenTypes.";

constexpr const std::string_view asyncStatusIntf = "com.nvidia.Async.Status";
constexpr const std::string_view asyncStatusProperty = "Status";
constexpr const std::string_view asyncValueIntf = "com.nvidia.Async.Value";
constexpr const std::string_view asyncValueProperty = "Value";
constexpr const std::string_view asyncOperationBasePath =
    "/com/nvidia/nsmd/AsyncOperation";

constexpr const uint16_t debugTokenSuccessNsmErrorCode = 0;
constexpr const uint16_t debugTokenUnsupportedNsmErrorCode = 5;
constexpr const uint16_t debugTokenAlreadyInstalledNsmErrorCode = 0x100E;
constexpr const uint16_t debugTokenNotInstalledNsmErrorCode = 0x100F;

using NsmResult = std::tuple<uint16_t, std::string>;

} // namespace redfish::debug_token
