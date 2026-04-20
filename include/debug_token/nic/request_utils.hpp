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
#include "ossl_random.hpp"

#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

namespace redfish::debug_token::nic
{

/**
 * @brief Status codes for NSM debug token challenge queries
 */
enum DebugTokenChallengeQueryStatus
{
    OK = 0,
    TokenAlreadyApplied = 1,
    TokenNotSupported = 2,
    NoKeyConfigured = 3,
    InterfaceNotAllowed = 4
};

#pragma pack(1)
/**
 * @brief NSM debug token request structure
 *
 * This structure represents a debug token request for NSM devices. It contains
 * device identification, authentication, and challenge data required for debug
 * token operations.
 */
struct DebugTokenRequest
{
    uint16_t tokenRequestVersion;
    uint16_t tokenRequestSize;
    std::array<uint8_t, 20> reserved1;
    std::array<uint8_t, 8> deviceUuid;
    uint16_t deviceType;
    std::array<uint8_t, 2> reserved2;
    uint8_t tokenOpcode;
    uint8_t status;
    uint16_t deviceIndex:12;
    uint16_t reserved3:4;
    std::array<uint8_t, 16> keypairUuid;
    std::array<uint8_t, 8> baseMac;
    std::array<uint8_t, 16> psid;
    std::array<uint8_t, 3> reserved4;
    std::array<uint8_t, 5> fwVersion;
    std::array<uint8_t, 16> sourceAddress;
    uint16_t sessionId;
    uint8_t reserved5;
    uint8_t challengeVersion;
    std::array<uint8_t, 32> challenge;
};
#pragma pack()

/**
 * @brief Converts NSM token request to SPDM transcript format
 *
 * This function wraps an NSM debug token request in SPDM measurement format. It
 * creates a complete SPDM transcript that includes both request and response
 * portions with proper headers, nonces, and measurement records as per DMTF
 * specification.
 *
 * @param request The raw NSM token request data to be wrapped
 * @return A vector containing the SPDM-formatted transcript
 */
inline std::vector<uint8_t> tokenRequestToSpdmTranscript(
    const std::vector<uint8_t>& request)
{
    constexpr const size_t wrapperOverhead = 86;
    constexpr const size_t measurementRecordOverhead = 4;
    constexpr const size_t dmtfSpecOverhead = 3;

    std::vector<uint8_t> wrappedRequest;
    std::uniform_int_distribution<uint8_t> dist(0);
    bmcweb::OpenSSLGenerator gen;
    size_t measurementLen = request.size() + dmtfSpecOverhead;
    size_t recordLen = measurementLen + measurementRecordOverhead;
    // request
    wrappedRequest.reserve(request.size() + wrapperOverhead);
    wrappedRequest.emplace_back(0x11); // version 1.1
    wrappedRequest.emplace_back(0xE0); // SPDM_MEASUREMENTS
    wrappedRequest.emplace_back(0x02); // param 1
    wrappedRequest.emplace_back(0x32); // param 2
    for (size_t i = 0; i < 32; ++i)
    {
        wrappedRequest.emplace_back(dist(gen)); // nonce
    }
    wrappedRequest.emplace_back(0x00);          // slot ID param
    // response
    wrappedRequest.emplace_back(0x11);             // version 1.1
    wrappedRequest.emplace_back(0x60);             // SPDM_MEASUREMENTS
    wrappedRequest.emplace_back(0x00);             // param 1
    wrappedRequest.emplace_back(0x00);             // param 2
    wrappedRequest.emplace_back(1);                // number of blocks
    wrappedRequest.emplace_back(recordLen & 0xFF); // measurement record length
    wrappedRequest.emplace_back(
        (recordLen >> 8) & 0xFF);                  // measurement record length
    wrappedRequest.emplace_back(
        (recordLen >> 16) & 0xFF);                 // measurement record length
    wrappedRequest.emplace_back(0x32);             // measurement index
    wrappedRequest.emplace_back(0x01);             // measurement specification
    wrappedRequest.emplace_back(measurementLen & 0xFF); // measurement size
    wrappedRequest.emplace_back(
        (measurementLen >> 8) & 0xFF);                  // measurement size
    wrappedRequest.emplace_back(0x85); // DMTF spec measurement value type
    wrappedRequest.emplace_back(
        request.size() & 0xFF);        // DMTF spec measurement value size
    wrappedRequest.emplace_back(
        (request.size() >> 8) & 0xFF); // DMTF spec measurement value size
    wrappedRequest.insert(wrappedRequest.end(), request.begin(), request.end());
    for (size_t i = 0; i < 32; ++i)
    {
        wrappedRequest.emplace_back(dist(gen)); // nonce
    }
    wrappedRequest.emplace_back(0);             // opaque data length
    wrappedRequest.emplace_back(0);             // opaque data length

    return wrappedRequest;
}

/**
 * @brief Reads NSM token request data from a file descriptor
 *
 * This function safely reads the entire contents of a file referenced by the
 * given file descriptor into a buffer. It handles file operations with proper
 * error checking and resource cleanup using RAII patterns.
 *
 * @param fd The file descriptor to read from
 * @param buffer Reference to the vector that will store the read data
 * @return true if the read operation was successful, false otherwise
 */
inline bool readTokenRequestFd(int fd, std::vector<uint8_t>& buffer)
{
    int dupFd = dup(fd);
    if (dupFd < 0)
    {
        BMCWEB_LOG_ERROR("dup error");
        return false;
    }
    auto fCleanup = [dupFd](FILE* f) -> void {
        fclose(f);
        close(dupFd);
    };
    std::unique_ptr<FILE, decltype(fCleanup)> file(fdopen(dupFd, "rb"),
                                                   fCleanup);
    if (!file)
    {
        BMCWEB_LOG_ERROR("fdopen error");
        close(dupFd);
        return false;
    }
    int rc = fseek(file.get(), 0, SEEK_END);
    if (rc < 0)
    {
        BMCWEB_LOG_ERROR("fseek error: {}", rc);
        return false;
    }
    auto filesize = ftell(file.get());
    if (filesize <= 0)
    {
        BMCWEB_LOG_ERROR("ftell error or size is zero: {}", filesize);
        return false;
    }
    if (fseek(file.get(), 0, SEEK_SET) != 0)
    {
        BMCWEB_LOG_ERROR("fseek error when rewinding file");
        return false;
    }
    size_t size = static_cast<size_t>(filesize);
    buffer.resize(size);
    auto len = fread(buffer.data(), 1, size, file.get());
    if (len != size)
    {
        BMCWEB_LOG_ERROR("fread error or length is invalid: {}", len);
        return false;
    }

    return true;
}

} // namespace redfish::debug_token::nic
