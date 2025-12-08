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

/**
 * @brief Convert nonce type string to DBus enum format
 *
 * @param nonceType Nonce type: "DeviceUniqueIdentifier", "RandomNonce", or
 * "StaticValue"
 * @return Full DBus enum path or empty string on error
 */
inline std::string convertNonceTypeToDbusEnum(const std::string& nonceType)
{
    if (nonceType == "DeviceUniqueIdentifier")
    {
        return std::string(dot::dotActionIntf) +
               ".UnlockMethod.DeviceUniqueIdentifier";
    }
    if (nonceType == "RandomNonce")
    {
        return std::string(dot::dotActionIntf) + ".UnlockMethod.RandomNonce";
    }
    if (nonceType == "StaticValue")
    {
        return std::string(dot::dotActionIntf) + ".UnlockMethod.StaticValue";
    }
    BMCWEB_LOG_ERROR("Invalid nonce type: {}", nonceType);
    return "";
}

/**
 * @brief Convert unlock type string to DBus enum format
 *
 * @param unlockType Unlock type: "OwnerUnlock" or "VendorUnlock"
 * @return Full DBus enum path or empty string on error
 */
inline std::string convertUnlockTypeToDbusEnum(const std::string& unlockType)
{
    if (unlockType == "OwnerUnlock")
    {
        return std::string(dot::dotActionIntf) + ".UnlockType.OwnerUnlock";
    }
    if (unlockType == "VendorUnlock")
    {
        return std::string(dot::dotActionIntf) + ".UnlockType.VendorUnlock";
    }
    BMCWEB_LOG_ERROR("Invalid unlock type: {}", unlockType);
    return "";
}

// DOT FuseChangeState values from D-Bus
enum class FuseChangeState : uint8_t
{
    None = 0,
    InProgress = 1,
    Completed = 2
};

/**
 * @brief Convert DOT FuseChangeState byte value to Redfish enum string
 *
 * @param state Backend FuseChangeState value
 * @return Redfish enum string representation
 */
constexpr std::string_view dotFuseChangeStateToString(uint8_t state)
{
    switch (static_cast<FuseChangeState>(state))
    {
        case FuseChangeState::InProgress:
            return "InProgress";
        case FuseChangeState::Completed:
            return "Completed";
        case FuseChangeState::None:
        default:
            return "None";
    }
}

/**
 * @brief Converts D-Bus DOTState enum to Redfish string format
 *
 * Extracts the state value from a D-Bus enum string (e.g.,
 * "com.nvidia.Dot.Action.DOTState.Uninitialized" -> "Uninitialized")
 *
 * @param dbusState Full D-Bus enum state string
 * @return Redfish-formatted state string, defaults to "Uninitialized" on error
 */
inline std::string convertDOTStateToRedfish(const std::string& dbusState)
{
    std::string defaultState = "Uninitialized";
    size_t lastDot = dbusState.find_last_of('.');
    if (lastDot != std::string::npos && lastDot + 1 < dbusState.length())
    {
        return dbusState.substr(lastDot + 1);
    }
    return defaultState;
}

} // namespace redfish::dot_utils
