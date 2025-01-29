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
/*!
 * @file    dbus_log_utils.cpp
 * @brief   Source code for utility functions of dbus logging.
 */

#pragma once
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace redfish
{
inline std::string translateSeverityDbusToRedfish(const std::string_view s)
{
    if ((s == "xyz.openbmc_project.Logging.Entry.Level.Alert") ||
        (s == "xyz.openbmc_project.Logging.Entry.Level.Critical") ||
        (s == "xyz.openbmc_project.Logging.Entry.Level.Emergency") ||
        (s == "xyz.openbmc_project.Logging.Entry.Level.Error"))
    {
        return "Critical";
    }
    if ((s == "xyz.openbmc_project.Logging.Entry.Level.Debug") ||
        (s == "xyz.openbmc_project.Logging.Entry.Level.Informational") ||
        (s == "xyz.openbmc_project.Logging.Entry.Level.Notice"))
    {
        return "OK";
    }
    if (s == "xyz.openbmc_project.Logging.Entry.Level.Warning")
    {
        return "Warning";
    }
    return "";
}

const std::string minpasswordLengthDbus = "MinPasswordLength";
const std::string minpasswordLength = "MinPasswordLength";
const std::string accountUnlockTimeoutDbus = "AccountUnlockTimeout";
const std::string accountLockoutDuration = "AccountLockoutDuration";
const std::string maxLoginAttemptBeforeLockoutDbus =
    "MaxLoginAttemptBeforeLockout";
const std::string maxLoginAttemptBeforeLockout = "MaxLoginAttemptBeforeLockout";
const std::string userEnabledDbus = "UserEnabled";
const std::string userEnabled = "UserEnabled";
const std::string userLockedForFailedAttemptDbus = "UserLockedForFailedAttempt";
const std::string locked = "Locked";
const std::string userPrivilegeDbus = "UserPrivilege";
const std::string roleid = "RoleId";
const std::string ldapbindDNPasswordDbus = "LDAPBindDNPassword";
const std::string password = "Password";
const std::string ldapBindDNDbus = "LDAPBindDN";
const std::string usernameDbus = "UserName";
const std::string username = "UserName";
const std::string ldapServerURIDbus = "LDAPServerURI";
const std::string serviceAddresses = "ServiceAddresses";
const std::string enabledDbus = "Enabled";
const std::string srvcEnabled = "ServiceEnabled";
const std::string ldapBaseDNDbus = "LDAPBaseDN";
const std::string baseDistinguishedNames = "BaseDistinguishedNames";
const std::string groupNameAttributeDbus = "GroupNameAttribute";
const std::string groupsAttribute = "GroupsAttribute";
const std::string userNameAttributeDbus = "UserNameAttribute";
const std::string userNameAttribute = "UsernameAttribute";
const std::string privilageDbus = "Privilege";
const std::string localRole = "LocalRole";
const std::string groupNameDbus = "GroupName";
const std::string remoteGroup = "RemoteGroup";
const std::string modulePowercapDbus = "ModulePowerCap";
const std::string setpoint = "SetPoint";
const std::string nicEnabledDbus = "NICEnabled";
const std::string vlanEnable = "VLANEnable";
const std::string dhcbEnableDbus = "DHCPEnabled";
const std::string dhcbEnabled = "DHCPEnabled";
const std::string secureBootEnableDbus = "SecureBootEnable";
const std::string secureBootEnable = "SecureBootEnable";
const std::string secureBootModeDbus = "SecureBootMode";
const std::string secureBootMode = "SecureBootMode";
const std::string secureCurrentBootDbus = "ScureCurrentBoot";
const std::string secureCurrentBoot = "ScureCurrentBoot";
const std::string resetBIOSSettingsDbus = "ResetBIOSSettings";
const std::string resetBIOSSettings = "ResetBIOSSettings";
const std::string biosPassowrdDbus = "BIOSPassword";
const std::string biosPassword = "NewPassword";
const std::string hostPowerStateDbus = "RequestedHostTransition";
const std::string hostPowerState = "ResetType";

/**
 * @brief Map Dbus Property to Redfish Property
 */
inline std::unordered_map<std::string, std::string> dBusToRedfishProperty = {
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

class AdditionalData
{
  public:
    enum SameKeyOp
    {
        overwrite = 0,
        append = 1,
    };

    // DBus Event Log additionalData format is like,
    // "key1=val1" "key2=val2"...
    AdditionalData(const std::vector<std::string>& additionalData,
                   const SameKeyOp& op = overwrite)
    {
        convert(additionalData, data, op);
    }

    void convert(const std::vector<std::string>& additionalData,
                 std::map<std::string, std::string>& data, const SameKeyOp& op)
    {
        for (auto& kv : additionalData)
        {
            std::size_t splitPos = kv.find('=');
            if ((splitPos != std::string::npos) && (splitPos != 0) &&
                (splitPos + 1 <= kv.size()))
            {
                std::string key = kv.substr(0, splitPos);
                std::string val = kv.substr(splitPos + 1);
                if (op == overwrite)
                {
                    data[key] = val;
                }
                else if (op == append)
                {
                    // In append mode, all values for the same key will be
                    // separated by ';', e.g., "key1=val1_1;val1_2;...;val1_n"
                    data[key] += (!data[key].empty()) ? ";" : "";
                    data[key] += val;
                }
            }
            else
            {
                BMCWEB_LOG_ERROR(
                    "Invalid format for Logging entry: {}, expecting \"=\"",
                    kv);
                continue;
            }
        }
    }

    std::string& operator[](const std::string& key)
    {
        return data[key];
    }

    std::size_t count(const std::string& key) const
    {
        return data.count(key);
    }

    std::map<std::string, std::string>::const_iterator begin() const
    {
        return data.cbegin();
    }

    std::map<std::string, std::string>::const_iterator end() const
    {
        return data.cend();
    }

    std::map<std::string, std::string>::const_iterator
        find(const std::string& key) const
    {
        return data.find(key);
    }

  protected:
    std::map<std::string, std::string> data;
};
} // namespace redfish
