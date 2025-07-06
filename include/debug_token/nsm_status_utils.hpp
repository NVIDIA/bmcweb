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

#include <tuple>
#include <unordered_map>

namespace redfish::debug_token
{

using NsmDbusTokenStatus =
    std::tuple<std::string, std::string, std::string, uint32_t>;

struct NsmTokenStatus
{
    /**
     * @brief Convert D-Bus token status to NsmTokenStatus
     *
     * @param dbusStatus D-Bus token status
     */
    NsmTokenStatus(const NsmDbusTokenStatus& dbusStatus)
    {
        auto dbusTokenType = std::get<0>(dbusStatus);
        auto dbusTokenStatus = std::get<1>(dbusStatus);
        auto dbusAdditionalInfo = std::get<2>(dbusStatus);
        try
        {
            tokenType =
                dbusTokenType.substr(dbusTokenType.find_last_of('.') + 1);
            tokenStatus =
                dbusTokenStatus.substr(dbusTokenStatus.find_last_of('.') + 1);
            additionalInfo = dbusAdditionalInfo.substr(
                dbusAdditionalInfo.find_last_of('.') + 1);
        }
        catch (const std::exception& e)
        {
            BMCWEB_LOG_ERROR("Invalid token status: {} {} {}", dbusTokenType,
                             dbusTokenStatus, dbusAdditionalInfo);
            throw e;
        }
        timeLeft = std::get<3>(dbusStatus);
    }

    std::string tokenType;
    std::string tokenStatus;
    std::string additionalInfo;
    uint32_t timeLeft;
};

/**
 * @brief Get Redfish mapping for NSM DBus debug token status value
 *
 * @param[in] tokenStatus
 * @return std::string
 */
inline std::string getNsmTokenStatus(const std::string& tokenStatus)
{
    const static std::unordered_map<std::string, std::string>
        nsmTokenStatusMapping{
            {"DebugSessionEnded", "DebugSessionEnded"},
            {"OperationFailure", "Failed"},
            {"DebugSessionActive", "DebugSessionActive"},
            {"NoTokenApplied", "NoTokenApplied"},
            {"ChallengeProvided", "ChallengeProvidedNoTokenInstalled"},
            {"InstallationTimeout", "TimeoutBeforeTokenInstalled"},
            {"TokenTimeout", "ActiveTokenTimeout"},
        };
    if (nsmTokenStatusMapping.find(tokenStatus) != nsmTokenStatusMapping.end())
    {
        return nsmTokenStatusMapping.at(tokenStatus);
    }
    return tokenStatus;
}

/**
 * @brief Get Redfish mapping for NSM DBus debug token additional info value
 *
 * @param[in] additionalInfo
 * @return std::string
 */
inline std::string getNsmTokenAdditionalInfo(const std::string& additionalInfo)
{
    const static std::unordered_map<std::string, std::string>
        nsmTokenadditionalInfoMapping{
            {"None", "None"},
            {"NoDebugSession", "NoDebugSessionInProgress"},
            {"FirmwareNotSecured", "InsecureFirmware"},
            {"DebugSessionEndRequestNotAccepted", "DebugEndRequestFailed"},
            {"DebugSessionQueryDisallowed", "QueryDebugSessionFailed"},
            {"DebugSessionActive", "DebugSessionActive"},
        };
    if (nsmTokenadditionalInfoMapping.find(additionalInfo) !=
        nsmTokenadditionalInfoMapping.end())
    {
        return nsmTokenadditionalInfoMapping.at(additionalInfo);
    }
    return additionalInfo;
}

/**
 * @brief Convert NsmTokenStatus to JSON
 *
 * @param status NsmTokenStatus
 * @param json JSON object to store the NsmTokenStatus
 */
inline void nsmTokenStatusToJson(const NsmTokenStatus& status,
                                 nlohmann::json& json)
{
    json["TokenType"] = status.tokenType;
    json["Status"] = getNsmTokenStatus(status.tokenStatus);
    json["AdditionalInfo"] = getNsmTokenAdditionalInfo(status.additionalInfo);
    json["TimeLeftSeconds"] = status.timeLeft;
}

} // namespace redfish::debug_token
