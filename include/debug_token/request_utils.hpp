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

#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace redfish::debug_token
{

/**
 * @brief Enumeration of supported debug token types
 *
 * Defines the different types of debug tokens that can be requested
 * from the BMC.
 */
enum class TokenType
{
    Invalid,
    DebugTokenRequest,
    DOTCAKUnlockTokenRequest,
    DOTEnableTokenRequest,
    DOTSignTestToken,
    DOTOverrideTokenRequest
};

/**
 * @brief Checks if a token type is a DOT type
 *
 * Determines whether the given token type belongs to the DOT family of tokens.
 * This function is conditionally compiled based on BMCWEB_DOT_SUPPORT flag.
 *
 * @param type The token type to check
 * @return bool True if the token type is a DOT type, false otherwise
 */
inline bool isDOTTokenType(const TokenType& type) noexcept
{
    if constexpr (BMCWEB_DOT_SUPPORT)
    {
        return type == TokenType::DOTCAKUnlockTokenRequest ||
               type == TokenType::DOTEnableTokenRequest ||
               type == TokenType::DOTSignTestToken ||
               type == TokenType::DOTOverrideTokenRequest;
    }
    return false;
}

/**
 * @brief Converts a string representation to a TokenType enum value
 *
 * Maps string representations of token types to their corresponding enum
 * values. Returns TokenType::Invalid for unrecognized strings.
 *
 * @param type String representation of the token type
 * @return TokenType The corresponding enum value, or TokenType::Invalid if not
 * found
 */
inline TokenType stringToTokenType(const std::string& type) noexcept
{
    if (type == "GetDebugTokenRequest")
    {
        return TokenType::DebugTokenRequest;
    }
    if constexpr (BMCWEB_DOT_SUPPORT)
    {
        if (type == "GetDOTCAKUnlockTokenRequest")
        {
            return TokenType::DOTCAKUnlockTokenRequest;
        }
        if (type == "GetDOTEnableTokenRequest")
        {
            return TokenType::DOTEnableTokenRequest;
        }
        if (type == "GetDOTSignTestToken")
        {
            return TokenType::DOTSignTestToken;
        }
        if (type == "GetDOTOverrideTokenRequest")
        {
            return TokenType::DOTOverrideTokenRequest;
        }
    }
    return TokenType::Invalid;
}

/**
 * @brief Maps a TokenType to its corresponding SPDM measurement index
 *
 * Provides the SPDM (Security Protocol and Data Model) measurement index
 * associated with each token type for security measurement purposes.
 *
 * @param type The token type to map
 * @return uint8_t The SPDM measurement index for the given token type
 * @throws std::out_of_range If the token type is not found in the mapping
 */
inline uint8_t spdmTokenTypeToMeasurementIndex(TokenType type)
{
    static const std::map<TokenType, uint8_t> indexMap{
        {TokenType::DebugTokenRequest, 50},
        {TokenType::DOTCAKUnlockTokenRequest, 58},
        {TokenType::DOTEnableTokenRequest, 59},
        {TokenType::DOTSignTestToken, 60},
        {TokenType::DOTOverrideTokenRequest, 61}};

    return indexMap.at(type);
}

#pragma pack(1)
/**
 * @brief Header structure for server token requests
 *
 * Contains versioning and size information for token request structures
 * sent to the server. Uses packed layout for binary protocol compatibility.
 * The version field is set to 0x0002 in the current implementation.
 */
struct ServerRequestHeader
{
    /* Versioning for token request structure (0x0002) */
    uint16_t version;
    /* Size of the token request structure */
    uint16_t size;
};
#pragma pack()

/**
 * @brief Prepends a ServerRequestHeader to a token request payload
 *
 * Creates a complete token request by adding the appropriate header
 * structure to the request data. The header includes version and size
 * information required by the server protocol. The request data is
 * expected to be a TLV-encoded structure containing measurement transcript
 * and certificate chain.
 *
 * @param request The TLV-encoded token request data without header
 * @return std::vector<uint8_t> Complete request with header prepended
 */
inline std::vector<uint8_t> addTokenRequestHeader(
    const std::vector<uint8_t>& request)
{
    std::vector<uint8_t> requestWithHeader;
    size_t size = sizeof(ServerRequestHeader) + request.size();
    auto header = std::make_unique<ServerRequestHeader>();
    header->version = 0x0002;
    header->size = static_cast<uint16_t>(size);
    requestWithHeader.reserve(size);
    requestWithHeader.resize(sizeof(ServerRequestHeader));
    std::memcpy(requestWithHeader.data(), header.get(),
                sizeof(ServerRequestHeader));
    requestWithHeader.insert(requestWithHeader.end(), request.begin(),
                             request.end());

    return requestWithHeader;
}

/**
 * @brief Enumeration of token file types
 *
 * Defines the different types of data that can be stored in token files.
 * Used to distinguish between token requests and actual debug tokens.
 */
enum class TokenFileType
{
    TokenRequest = 1,
    DebugToken = 2
};

#pragma pack(1)
/**
 * @brief Header structure for token files
 *
 * Contains metadata for token files including version, type, record count,
 * and file size information. Uses packed layout for binary file format
 * compatibility.
 */
struct TokenFileHeader
{
    /* Set to 0x01 */
    uint8_t version;
    /* Either 1 for token request or 2 for token data */
    uint8_t type;
    /* Count of stored debug tokens / requests */
    uint16_t numberOfRecords;
    /* Equal to sizeof(struct TokenFileHeader) for version 0x01. */
    uint16_t offsetToListOfStructs;
    /* Equal to sum of sizes of a given structure type +
     * sizeof(struct TokenFileHeader) */
    uint32_t fileSize;
    /* Padding */
    std::array<uint8_t, 6> reserved;
};
#pragma pack()

/**
 * @brief Generates a complete token request file with header
 *
 * Creates a binary file containing multiple token requests with appropriate
 * file header metadata. The file format includes a header with version,
 * type, record count, and size information followed by the actual request data.
 *
 * @param tokenRequests Vector of token request data to include in the file
 * @return std::vector<uint8_t> Complete token request file as binary data
 */
inline std::vector<uint8_t> generateTokenRequestFile(
    const std::vector<std::vector<uint8_t>>& tokenRequests)
{
    std::vector<uint8_t> output;
    if (tokenRequests.empty())
    {
        return output;
    }
    size_t size = sizeof(TokenFileHeader);
    for (const auto& request : tokenRequests)
    {
        size += request.size();
    }
    auto header = std::make_unique<TokenFileHeader>();
    header->version = 0x02;
    header->type = static_cast<uint8_t>(TokenFileType::TokenRequest);
    header->numberOfRecords = static_cast<uint16_t>(tokenRequests.size());
    header->offsetToListOfStructs = sizeof(TokenFileHeader);
    header->fileSize = static_cast<uint32_t>(size);
    output.reserve(size);
    output.resize(sizeof(TokenFileHeader));
    std::memcpy(output.data(), header.get(), sizeof(TokenFileHeader));
    for (const auto& request : tokenRequests)
    {
        output.insert(output.end(), request.begin(), request.end());
    }
    return output;
}

} // namespace redfish::debug_token
