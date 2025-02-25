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
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

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

namespace redfish
{
constexpr std::string_view minpasswordLengthDbus = "MinPasswordLength";
constexpr std::string_view minpasswordLength = "MinPasswordLength";
constexpr std::string_view accountUnlockTimeoutDbus = "AccountUnlockTimeout";
constexpr std::string_view accountLockoutDuration = "AccountLockoutDuration";
constexpr std::string_view maxLoginAttemptBeforeLockoutDbus =
    "MaxLoginAttemptBeforeLockout";
constexpr std::string_view maxLoginAttemptBeforeLockout =
    "MaxLoginAttemptBeforeLockout";
constexpr std::string_view userEnabledDbus = "UserEnabled";
constexpr std::string_view userEnabled = "UserEnabled";
constexpr std::string_view userLockedForFailedAttemptDbus =
    "UserLockedForFailedAttempt";
constexpr std::string_view locked = "Locked";
constexpr std::string_view userPrivilegeDbus = "UserPrivilege";
constexpr std::string_view roleid = "RoleId";
constexpr std::string_view ldapbindDNPasswordDbus = "LDAPBindDNPassword";
constexpr std::string_view password = "Password";
constexpr std::string_view ldapBindDNDbus = "LDAPBindDN";
constexpr std::string_view usernameDbus = "UserName";
constexpr std::string_view username = "UserName";
constexpr std::string_view ldapServerURIDbus = "LDAPServerURI";
constexpr std::string_view serviceAddresses = "ServiceAddresses";
constexpr std::string_view enabledDbus = "Enabled";
constexpr std::string_view srvcEnabled = "ServiceEnabled";
constexpr std::string_view ldapBaseDNDbus = "LDAPBaseDN";
constexpr std::string_view baseDistinguishedNames = "BaseDistinguishedNames";
constexpr std::string_view groupNameAttributeDbus = "GroupNameAttribute";
constexpr std::string_view groupsAttribute = "GroupsAttribute";
constexpr std::string_view userNameAttributeDbus = "UserNameAttribute";
constexpr std::string_view userNameAttribute = "UsernameAttribute";
constexpr std::string_view privilageDbus = "Privilege";
constexpr std::string_view localRole = "LocalRole";
constexpr std::string_view groupNameDbus = "GroupName";
constexpr std::string_view remoteGroup = "RemoteGroup";
constexpr std::string_view modulePowercapDbus = "ModulePowerCap";
constexpr std::string_view setpoint = "SetPoint";
constexpr std::string_view nicEnabledDbus = "NICEnabled";
constexpr std::string_view vlanEnable = "VLANEnable";
constexpr std::string_view dhcbEnableDbus = "DHCPEnabled";
constexpr std::string_view dhcbEnabled = "DHCPEnabled";
constexpr std::string_view secureBootEnableDbus = "SecureBootEnable";
constexpr std::string_view secureBootEnable = "SecureBootEnable";
constexpr std::string_view secureBootModeDbus = "SecureBootMode";
constexpr std::string_view secureBootMode = "SecureBootMode";
constexpr std::string_view secureCurrentBootDbus = "ScureCurrentBoot";
constexpr std::string_view secureCurrentBoot = "ScureCurrentBoot";
constexpr std::string_view resetBIOSSettingsDbus = "ResetBIOSSettings";
constexpr std::string_view resetBIOSSettings = "ResetBIOSSettings";
constexpr std::string_view biosPassowrdDbus = "BIOSPassword";
constexpr std::string_view biosPassword = "NewPassword";
constexpr std::string_view hostPowerStateDbus = "RequestedHostTransition";
constexpr std::string_view hostPowerState = "ResetType";

inline std::unordered_map<std::string_view, std::string_view>
    dBusToRedfishProperty = {
        {minpasswordLengthDbus, minpasswordLength},
        {accountUnlockTimeoutDbus, accountLockoutDuration},
        {maxLoginAttemptBeforeLockoutDbus, maxLoginAttemptBeforeLockout},
        {userEnabledDbus, userEnabled},
        {userLockedForFailedAttemptDbus, locked},
        {userPrivilegeDbus, roleid},
        {ldapbindDNPasswordDbus, password},
        {ldapBindDNDbus, username},
        {ldapServerURIDbus, serviceAddresses},
        {enabledDbus, srvcEnabled},
        {ldapBaseDNDbus, baseDistinguishedNames},
        {usernameDbus, username},
        {groupNameAttributeDbus, groupsAttribute},
        {userNameAttributeDbus, userNameAttribute},
        {privilageDbus, localRole},
        {groupNameDbus, remoteGroup},
        {modulePowercapDbus, setpoint},
        {nicEnabledDbus, vlanEnable},
        {dhcbEnableDbus, dhcbEnabled},
        {secureBootEnableDbus, secureBootEnable},
        {secureBootModeDbus, secureBootMode},
        {resetBIOSSettingsDbus, resetBIOSSettings},
        {biosPassowrdDbus, biosPassword},
        {secureCurrentBootDbus, secureCurrentBoot},
        {hostPowerStateDbus, hostPowerState}};

inline void convertDbusToRedfishProperty(AdditionalData& additional,
                                         std::string& messageArgs)
{
    std::vector<std::string> messageArgsDbus = {};
    std::string args = additional["REDFISH_MESSAGE_ARGS"];
    bmcweb::split(messageArgsDbus, args, ',');
    for (auto& msgArg : messageArgsDbus)
    {
        boost::trim(msgArg);
    }
    if (!messageArgsDbus.empty() && !messageArgsDbus[0].empty())
    {
        if (dBusToRedfishProperty.find(messageArgsDbus[0]) !=
            dBusToRedfishProperty.end())
        {
            messageArgsDbus[0] = dBusToRedfishProperty[messageArgsDbus[0]];
            messageArgs = boost::algorithm::join(messageArgsDbus, ", ");
        }
        else
        {
            BMCWEB_LOG_WARNING("property mapping not found for {}",
                               messageArgsDbus[0]);
            messageArgs = additional["REDFISH_MESSAGE_ARGS"];
        }
    }
}
} // namespace redfish
