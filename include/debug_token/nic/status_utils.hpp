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

#include <logging.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <tuple>
#include <unordered_map>

namespace redfish::debug_token::nic
{

using DbusTokenStatus =
    std::tuple<std::string, std::string, std::string, uint32_t>;

/**
 * @brief Represents the status of an NSM debug token
 *
 * This struct contains the parsed and processed information from a D-Bus token
 * status response, including the token type, status, additional information,
 * and remaining time.
 */
struct TokenStatus
{
    /**
     * @brief Constructs an TokenStatus from D-Bus token status data
     *
     * Parses the D-Bus token status tuple and extracts the relevant
     * information, converting the full D-Bus enum values to their short form by
     * removing the namespace prefix.
     *
     * @param dbusStatus D-Bus token status tuple containing (tokenType,
     *                   tokenStatus, additionalInfo, timeLeft)
     * @throws std::exception if parsing fails due to invalid input data
     */
    explicit TokenStatus(const DbusTokenStatus& dbusStatus)
    {
        // NOLINTNEXTLINE(bugprone-unused-local-non-trivial-variable)
        auto dbusTokenType = std::get<0>(dbusStatus);
        // NOLINTNEXTLINE(bugprone-unused-local-non-trivial-variable)
        auto dbusTokenStatus = std::get<1>(dbusStatus);
        // NOLINTNEXTLINE(bugprone-unused-local-non-trivial-variable)
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
 * @brief Maps NSM D-Bus debug token status values to Redfish-compatible strings
 *
 * Converts the short-form D-Bus token status values to their corresponding
 * Redfish representation. If no mapping is found, returns the original value.
 *
 * @param[in] tokenStatus The D-Bus token status value to map
 * @return std::string The corresponding Redfish status string
 */
static std::string getTokenStatusMapping(const std::string& tokenStatus)
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
    if (nsmTokenStatusMapping.contains(tokenStatus))
    {
        return nsmTokenStatusMapping.at(tokenStatus);
    }
    return tokenStatus;
}

/**
 * @brief Maps NSM D-Bus debug token additional info values to
 * Redfish-compatible strings
 *
 * Converts the short-form D-Bus additional info values to their corresponding
 * Redfish representation. If no mapping is found, returns the original value.
 *
 * @param[in] additionalInfo The D-Bus additional info value to map
 * @return std::string The corresponding Redfish additional info string
 */
static std::string getTokenAdditionalInfoMapping(
    const std::string& additionalInfo)
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
    if (nsmTokenadditionalInfoMapping.contains(additionalInfo))
    {
        return nsmTokenadditionalInfoMapping.at(additionalInfo);
    }
    return additionalInfo;
}

/**
 * @brief Converts an TokenStatus object to a JSON representation
 *
 * Populates the provided JSON object with the token status information,
 * applying Redfish-compatible mappings for status and additional info values.
 *
 * @param[in] status The TokenStatus object to convert
 * @param[out] json The JSON object to populate with the status data
 */
inline void tokenStatusToJson(const TokenStatus& status, nlohmann::json& json)
{
    json["TokenType"] = status.tokenType;
    json["Status"] = getTokenStatusMapping(status.tokenStatus);
    json["AdditionalInfo"] =
        getTokenAdditionalInfoMapping(status.additionalInfo);
    json["TimeLeftSeconds"] = status.timeLeft;
}

} // namespace redfish::debug_token::nic
