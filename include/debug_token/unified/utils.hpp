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

#include <cstdint>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace redfish::debug_token::unified
{

/**
 * @brief Represents the status of NSM debug tokens
 *
 * This struct encapsulates the installation and processing status of debug
 * tokens, along with a list of token type and subtype pairs that are currently
 * active.
 */
struct TokenStatus
{
    /**
     * @brief Constructs an TokenStatus object
     *
     * @param installationStatus Whether the token is installed
     * @param processingStatus Whether the token is being processed
     * @param tokenTypeSubtypeList Vector of token type/subtype pairs
     * @param deviceID Device ID associated with the token
     */
    explicit TokenStatus(
        bool installationStatus, bool processingStatus,
        const std::vector<std::tuple<uint32_t, uint32_t>>& tokenTypeSubtypeList,
        const std::string& deviceID) :
        installation(installationStatus), processing(processingStatus),
        tokenTypesSubtypes(tokenTypeSubtypeList), tokenDeviceID(deviceID)
    {}

    bool installation;
    bool processing;
    std::vector<std::tuple<uint32_t, uint32_t>> tokenTypesSubtypes;
    std::string tokenDeviceID;
};

/**
 * @brief Converts a numeric NSM token type to its string representation
 *
 * @param tokenType The numeric token type value
 * @return std::string The string representation of the token type, or the
 * numeric value as string if not found
 */
inline std::string getTokenTypeAsString(const uint32_t& tokenType)
{
    const static std::unordered_map<uint32_t, std::string> typeMapping{
        {0, "None"},
        {1, "DebugFirmwareUnlock"},
        {2, "OTPDumpEnable"},
        {4, "RAS"}};
    if (typeMapping.contains(tokenType))
    {
        return typeMapping.at(tokenType);
    }
    return std::to_string(tokenType);
}

/**
 * @brief Converts a numeric NSM token subtype to its string representation
 *
 * @param tokenSubtype The numeric token subtype value
 * @return std::string The string representation of the token subtype, or the
 * numeric value as string if not found
 */
inline std::string getTokenSubtypeAsString(const uint32_t& tokenSubtype)
{
    const static std::unordered_map<uint32_t, std::string> subtypeMapping{
        {0, "None"}};
    if (subtypeMapping.contains(tokenSubtype))
    {
        return subtypeMapping.at(tokenSubtype);
    }
    return std::to_string(tokenSubtype);
}

/**
 * @brief Converts a string NSM token type to its numeric representation
 *
 * @param tokenType The string token type value
 * @return uint32_t The numeric representation of the token type, or 0 if not
 * found
 */
inline uint32_t getTokenTypeAsUint32(const std::string& tokenType)
{
    const static std::unordered_map<std::string, uint32_t> typeMapping{
        {"None", 0},
        {"DebugFirmwareUnlock", 1},
        {"OTPDumpEnable", 2},
        {"RAS", 4},
        {"EraseAllAndRatchetCounterIncreased", 0xFFFFFFFE},
        {"EraseAll", 0xFFFFFFFF},
    };
    if (typeMapping.contains(tokenType))
    {
        return typeMapping.at(tokenType);
    }
    return 0;
}

/**
 * @brief Gets the list of allowable individual token types (excluding special
 * erase types)
 *
 * Returns token types that can be used for individual token operations,
 * excluding None and the special erase-all types.
 *
 * @return std::vector<std::string> List of allowable token type strings
 */
inline std::vector<std::string> getAllowableTokenTypes()
{
    return {"DebugFirmwareUnlock", "OTPDumpEnable", "RAS"};
}

/**
 * @brief Converts TokenStatus to JSON format. Used in aggregate token status
 *
 * @param status The NSM token status to convert
 * @param j The JSON object to populate with the status information
 */
inline void tokenStatusToJson(const TokenStatus& status, nlohmann::json& j)
{
    j["ProcessingStatus"] = status.processing ? "Processed" : "NotProcessed";
    j["TokenInstalled"] = status.installation;
    j["DeviceID"] = status.tokenDeviceID;
    nlohmann::json tokens;
    for (const auto& [tokenType, tokenSubtype] : status.tokenTypesSubtypes)
    {
        nlohmann::json token;
        token["TokenType"] = getTokenTypeAsString(tokenType);
        token["TokenSubtype"] = getTokenSubtypeAsString(tokenSubtype);
        tokens.push_back(std::move(token));
    }
    j["Tokens"] = std::move(tokens);
}

/**
 * @brief Converts a specific token from TokenStatus to JSON format
 *
 * @param status The NSM token status containing the token information
 * @param tokenIndex The index of the specific token to convert
 * @param j The JSON object to populate with the token information
 * @throws std::runtime_error If the tokenIndex is out of bounds
 */
inline void tokenStatusToJson(const TokenStatus& status, uint32_t tokenIndex,
                              nlohmann::json& j)
{
    if (tokenIndex >= status.tokenTypesSubtypes.size())
    {
        throw std::runtime_error(
            "Invalid index: " + std::to_string(tokenIndex));
    }
    j["TokenProcessed"] = status.processing;
    j["TokenInstalled"] = status.installation;
    j["TokenType"] = getTokenTypeAsString(
        std::get<0>(status.tokenTypesSubtypes[tokenIndex]));
    j["TokenSubType"] = getTokenSubtypeAsString(
        std::get<1>(status.tokenTypesSubtypes[tokenIndex]));
}

} // namespace redfish::debug_token::unified
