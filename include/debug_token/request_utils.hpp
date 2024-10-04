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

#include "ossl_random.hpp"

#include <boost/interprocess/streams/bufferstream.hpp>

#include <optional>
#include <sstream>
#include <vector>

namespace redfish::debug_token
{

enum NsmDebugTokenChallengeQueryStatus
{
    OK = 0,
    TokenAlreadyApplied = 1,
    TokenNotSupported = 2,
    NoKeyConfigured = 3,
    InterfaceNotAllowed = 4
};

#pragma pack(1)
struct NsmDebugTokenRequest
{
    uint16_t tokenRequestVersion;
    uint16_t tokenRequestSize;
    uint8_t reserved1[20];
    uint8_t deviceUuid[8];
    uint16_t deviceType;
    uint8_t reserved2[2];
    uint8_t tokenOpcode;
    uint8_t status;
    uint16_t deviceIndex:12;
    uint8_t reserved3:4;
    uint8_t keypairUuid[16];
    uint8_t baseMac[8];
    uint8_t psid[16];
    uint8_t reserved4[3];
    uint8_t fwVersion[5];
    uint8_t sourceAddress[16];
    uint16_t sessionId;
    uint8_t reserved5;
    uint8_t challengeVersion;
    uint8_t challenge[32];
};
#pragma pack()

inline std::vector<uint8_t>
    convertNsmTokenRequestToSpdmTranscript(const std::vector<uint8_t>& request)
{
    constexpr const size_t wrapperOverhead = 86;
    constexpr const size_t measurementRecordOverhead = 4;
    constexpr const size_t DMTFSpecOverhead = 3;

    std::vector<uint8_t> wrappedRequest;
    std::uniform_int_distribution<uint8_t> dist(0);
    bmcweb::OpenSSLGenerator gen;
    size_t measurementLen = request.size() + DMTFSpecOverhead;
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
    wrappedRequest.emplace_back((recordLen >> 8) &
                                0xFF);             // measurement record length
    wrappedRequest.emplace_back((recordLen >> 16) &
                                0xFF);             // measurement record length
    wrappedRequest.emplace_back(0x32);             // measurement index
    wrappedRequest.emplace_back(0x01);             // measurement specification
    wrappedRequest.emplace_back(measurementLen & 0xFF); // measurement size
    wrappedRequest.emplace_back((measurementLen >> 8) &
                                0xFF);                  // measurement size
    wrappedRequest.emplace_back(0x85); // DMTF spec measurement value type
    wrappedRequest.emplace_back(request.size() &
                                0xFF); // DMTF spec measurement value size
    wrappedRequest.emplace_back((request.size() >> 8) &
                                0xFF); // DMTF spec measurement value size
    wrappedRequest.insert(wrappedRequest.end(), request.begin(), request.end());
    for (size_t i = 0; i < 32; ++i)
    {
        wrappedRequest.emplace_back(dist(gen)); // nonce
    }
    wrappedRequest.emplace_back(0);             // opaque data length
    wrappedRequest.emplace_back(0);             // opaque data length

    return wrappedRequest;
}

#pragma pack(1)
struct ServerRequestHeader
{
    /* Versioning for token request structure (0x0001) */
    uint16_t version;
    /* Size of the token request structure */
    uint16_t size;
};
#pragma pack()

inline std::vector<uint8_t>
    addTokenRequestHeader(const std::vector<uint8_t>& request)
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

inline bool readNsmTokenRequestFd(int fd, std::vector<uint8_t>& buffer)
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
    rewind(file.get());
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

} // namespace redfish::debug_token
