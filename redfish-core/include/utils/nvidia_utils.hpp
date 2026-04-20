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
#include "logging.hpp"
#include "str_utility.hpp"
#include "utils/dbus_log_utils.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

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
constexpr std::string_view passwordString = "PasswordString";
constexpr std::string_view ldapBindDNDbus = "LDAPBindDN";
constexpr std::string_view usernameDbus = "UserName";
constexpr std::string_view redfishUsername = "UserName";
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
constexpr std::string_view vlanEnableString = "VLANEnable";
constexpr std::string_view dhcbEnableDbus = "DHCPEnabled";
constexpr std::string_view dhcbEnabled = "DHCPEnabled";
constexpr std::string_view secureBootEnableDbus = "SecureBootEnable";
constexpr std::string_view secureBootEnable = "SecureBootEnable";
constexpr std::string_view secureBootModeDbus = "SecureBootMode";
constexpr std::string_view secureBootMode = "SecureBootMode";
constexpr std::string_view secureCurrentBootDbus = "SecureCurrentBoot";
constexpr std::string_view secureCurrentBoot = "SecureCurrentBoot";
constexpr std::string_view resetBIOSSettingsDbus = "ResetBIOSSettings";
constexpr std::string_view resetBIOSSettings = "ResetBIOSSettings";
constexpr std::string_view biosPassowrdDbus = "BIOSPassword";
constexpr std::string_view biosPassword = "NewPassword";
constexpr std::string_view hostPowerStateDbus = "RequestedHostTransition";
constexpr std::string_view hostPowerState = "ResetType";

static const inline std::unordered_map<std::string_view, std::string_view>
    dBusToRedfishProperty = {
        {minpasswordLengthDbus, minpasswordLength},
        {accountUnlockTimeoutDbus, accountLockoutDuration},
        {maxLoginAttemptBeforeLockoutDbus, maxLoginAttemptBeforeLockout},
        {userEnabledDbus, userEnabled},
        {userLockedForFailedAttemptDbus, locked},
        {userPrivilegeDbus, roleid},
        {ldapbindDNPasswordDbus, passwordString},
        {ldapBindDNDbus, redfishUsername},
        {ldapServerURIDbus, serviceAddresses},
        {enabledDbus, srvcEnabled},
        {ldapBaseDNDbus, baseDistinguishedNames},
        {usernameDbus, redfishUsername},
        {groupNameAttributeDbus, groupsAttribute},
        {userNameAttributeDbus, userNameAttribute},
        {privilageDbus, localRole},
        {groupNameDbus, remoteGroup},
        {modulePowercapDbus, setpoint},
        {nicEnabledDbus, vlanEnableString},
        {dhcbEnableDbus, dhcbEnabled},
        {secureBootEnableDbus, secureBootEnable},
        {secureBootModeDbus, secureBootMode},
        {resetBIOSSettingsDbus, resetBIOSSettings},
        {biosPassowrdDbus, biosPassword},
        {secureCurrentBootDbus, secureCurrentBoot},
        {hostPowerStateDbus, hostPowerState}};

inline std::string trim(const std::string& str)
{
    // Find the first non-whitespace character
    size_t first = str.find_first_not_of(" \t\n\r\f\v");
    if (std::string::npos == first)
    {
        // String contains only whitespace or is empty
        return "";
    }

    // Find the last non-whitespace character
    size_t last = str.find_last_not_of(" \t\n\r\f\v");

    // Extract the substring between the first and last non-whitespace
    // characters
    return str.substr(first, (last - first + 1));
}

inline std::string join(const std::vector<std::string>& values,
                        std::string_view delimiter)
{
    return std::ranges::fold_left(
        values, std::string(),
        [delimiter](const std::string& ss, const std::string& s) {
            if (ss.empty())
            {
                return s;
            }
            return std::format("{}{}{}", ss, delimiter, s);
        });
}

inline void convertDbusToRedfishProperty(AdditionalData& additional,
                                         std::string& messageArgs)
{
    BMCWEB_LOG_DEBUG("Converting DBus property to Redfish property");
    std::vector<std::string> messageArgsDbus;
    std::string args = additional["REDFISH_MESSAGE_ARGS"];
    BMCWEB_LOG_DEBUG("Original message args: {}", args);
    bmcweb::split(messageArgsDbus, args, ',');
    for (auto& msgArg : messageArgsDbus)
    {
        msgArg = trim(msgArg);
    }
    if (!messageArgsDbus.empty() && !messageArgsDbus[0].empty())
    {
        BMCWEB_LOG_DEBUG("First message arg: {}", messageArgsDbus[0]);
        if (dBusToRedfishProperty.contains(messageArgsDbus[0]))
        {
            std::string oldArg = messageArgsDbus[0];
            auto it = dBusToRedfishProperty.find(messageArgsDbus[0]);
            if (it == dBusToRedfishProperty.end())
            {
                return;
            }
            messageArgsDbus[0] = it->second;
            BMCWEB_LOG_DEBUG("Mapped property: {} -> {}", oldArg,
                             messageArgsDbus[0]);
            messageArgs = join(messageArgsDbus, ", ");
        }
        else
        {
            BMCWEB_LOG_WARNING("property mapping not found for {}",
                               messageArgsDbus[0]);
            messageArgs = additional["REDFISH_MESSAGE_ARGS"];
        }
    }
    BMCWEB_LOG_DEBUG("Final message args: {}", messageArgs);
}

const std::string certificateDbusPrefix = "/xyz/openbmc_project/certs";
const std::string systemsDbusPrefix = "/xyz/openbmc_project/inventory/system";
const std::string accountServiceDbusPrefix = "/xyz/openbmc_project/user";
const std::string managerAccountDbusPrefix = "/xyz/openbmc_project/user/";
const std::string virtualMediaDbusPrefix = "/xyz/openbmc_project/VirtualMedia";

struct CompareKeys
{
    bool operator()(const std::string& a, const std::string& b) const
    {
        return std::greater<>{}(a, b);
    }
};
/**
 * @brief Map dbuspath  to resourceType
 */
inline const static std::map<std::string, std::string, CompareKeys>
    dBusToResourceType = {{certificateDbusPrefix, "CertificateService"},
                          {systemsDbusPrefix, "Systems"},
                          {accountServiceDbusPrefix, "AccountService"},
                          {managerAccountDbusPrefix, "ManagerAccount"},
                          {virtualMediaDbusPrefix, "VirtualMedia"}};

// Add a helper function to log resource type mapping
inline std::string getResourceType(const std::string& dbusPath)
{
    BMCWEB_LOG_DEBUG("Getting resource type for DBus path: {}", dbusPath);
    for (const auto& [prefix, type] : dBusToResourceType)
    {
        if (dbusPath.starts_with(prefix))
        {
            BMCWEB_LOG_DEBUG("Found resource type: {} for path: {}", type,
                             dbusPath);
            return type;
        }
    }
    BMCWEB_LOG_DEBUG("No resource type found for path: {}", dbusPath);
    return "";
}
} // namespace redfish
