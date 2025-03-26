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

#include <endian.h>

#include <boost/interprocess/streams/bufferstream.hpp>

#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <vector>

namespace redfish::debug_token
{

static std::string setOrAppend(std::string str, std::string in)
{
    if (str.empty())
    {
        return in;
    }
    return str + ", " + in;
}

constexpr const size_t vdmUtilWrapperOutputEidIndex = 0;
constexpr const size_t vdmUtilWrapperOutputVersionIndex = 1;
constexpr const size_t vdmUtilWrapperOutputTxIndex = 2;
constexpr const size_t vdmUtilWrapperOutputRxIndex = 3;

constexpr const size_t vdmStatusDeviceIdLength = 8;
constexpr const size_t vdmStatusV2DeviceIdLength = 8;
constexpr const size_t vdmStatusV3DeviceIdLength = 16;
constexpr const size_t vdmStatusErrorCodeOffset = 8;
constexpr const size_t vdmStatusErrorCodeSuccess = 0x00;
constexpr const size_t vdmStatusErrorCodeNotSupported = 0x05;

enum class VdmResponseStatus
{
    INVALID_LENGTH,
    PROCESSING_ERROR,
    NOT_SUPPORTED,
    ERROR,
    STATUS
};

enum class VdmTokenInstallationStatus
{
    NOT_INSTALLED = 0x00,
    INSTALLED = 0x01,
    INVALID
};

enum class VdmTokenFuseType
{
    DEBUG = 0x01,
    PRODUCTION = 0x02,
    INVALID
};

enum class VdmDeviceType
{
    EROT = 0b00000001,
    GPU = 0b00000010,
    NVSwitch = 0b00000011,
    CX7 = 0b00000100,
    MCU = 0b00000101,
    CX8 = 0b00000110,
    INVALID
};

enum class VdmTokenTypeERoT
{
    UNDEFINED = 0x00,
    DEBUG_FW = 0x01,
    EFRC = 0x04
};

enum class VdmTokenTypeIRoT
{
    UNDEFINED = 0x00,
    DEBUG_FW = 0x01,
    JTAG_UNLOCK = 0x02,
    HW_UNLOCK = 0x04,
    RUNTIME_DEBUG = 0x08,
    FEATURE_UNLOCK = 0x10
};

enum class VdmTokenTypeMCU
{
    UNDEFINED = 0x00,
    DEBUG_FW = 0x01,
    OTP_DUMP_ENABLE = 0x02
};

enum class VdmTokenLifecycle
{
    PERSISTENT = 0,
    TEMPORAL = (1 << 0)
};

enum class VdmTokenActivation
{
    ON_BOOT = 0,
    MANUAL = (1 << 1)
};

enum class VdmTokenRevocation
{
    MANUAL = 0,
    AUTOMATIC = (1 << 2)
};

enum class VdmTokenDevIdStatus
{
    DISABLED = 0,
    ENABLED = (1 << 3)
};

enum class VdmTokenAntiReplay
{
    NONCE_DISABLED = 0,
    NONCE_ENABLED = (1 << 4)
};

enum class VdmTokenResetPostInstall
{
    NOT_MANDATED = 0,
    MANDATED = (1 << 5)
};

enum class VdmTokenProcessingStatus
{
    NOT_PROCESSED = 0x00,
    PROCESSED = 0x01,
    VERIFICATION_FAILURE = 0x02,
    RUNTIME_ERROR = 0x03,
    INVALID
};

/**
 * @brief Decode the binary token installation status to the enum value
 *
 * @param arg Binary token installation status
 * @return Token installation status enum value
 */
static VdmTokenInstallationStatus getTokenInstallationStatus(uint8_t arg)
{
    VdmTokenInstallationStatus ret;
    if (arg == static_cast<uint8_t>(VdmTokenInstallationStatus::NOT_INSTALLED))
    {
        ret = VdmTokenInstallationStatus::NOT_INSTALLED;
    }
    else if (arg == static_cast<uint8_t>(VdmTokenInstallationStatus::INSTALLED))
    {
        ret = VdmTokenInstallationStatus::INSTALLED;
    }
    else
    {
        ret = VdmTokenInstallationStatus::INVALID;
    }
    return ret;
}

/**
 * @brief Convert the token installation status enum value to JSON
 *
 * @param arg Token installation status enum value
 * @param json JSON object to store the token installation status
 */
static void tokenInstallationStatusToJson(const VdmTokenInstallationStatus& arg,
                                          nlohmann::json& json)
{
    json["TokenInstalled"] = arg == VdmTokenInstallationStatus::INSTALLED;
}

/**
 * @brief Decode the binary token fuse type to the enum value
 *
 * @param arg Binary token fuse type
 * @return Token fuse type enum value
 */
static VdmTokenFuseType getTokenFuseType(uint8_t arg)
{
    VdmTokenFuseType ret;
    if (arg == static_cast<uint8_t>(VdmTokenFuseType::DEBUG))
    {
        ret = VdmTokenFuseType::DEBUG;
    }
    else if (arg == static_cast<uint8_t>(VdmTokenFuseType::PRODUCTION))
    {
        ret = VdmTokenFuseType::PRODUCTION;
    }
    else
    {
        ret = VdmTokenFuseType::INVALID;
    }
    return ret;
}

/**
 * @brief Convert the token fuse type enum value to JSON
 *
 * @param arg Token fuse type enum value
 * @param json JSON object to store the token fuse type
 */
static void tokenFuseTypeToJson(const VdmTokenFuseType& type,
                                nlohmann::json& json)
{
    if (type == VdmTokenFuseType::DEBUG)
    {
        json["FirmwareFuseType"] = "Debug";
    }
    else if (type == VdmTokenFuseType::PRODUCTION)
    {
        json["FirmwareFuseType"] = "Production";
    }
    else
    {
        json["FirmwareFuseType"] = "Invalid";
    }
}

/**
 * @brief Decode the binary device type to the enum value
 *
 * @param arg Binary device type
 * @return Device type enum value
 */
static VdmDeviceType getDeviceType(uint16_t arg)
{
    VdmDeviceType ret;
    if (arg == static_cast<uint16_t>(VdmDeviceType::EROT))
    {
        ret = VdmDeviceType::EROT;
    }
    else if (arg == static_cast<uint16_t>(VdmDeviceType::GPU))
    {
        ret = VdmDeviceType::GPU;
    }
    else if (arg == static_cast<uint16_t>(VdmDeviceType::NVSwitch))
    {
        ret = VdmDeviceType::NVSwitch;
    }
    else if (arg == static_cast<uint16_t>(VdmDeviceType::CX7))
    {
        ret = VdmDeviceType::CX7;
    }
    else if (arg == static_cast<uint16_t>(VdmDeviceType::MCU))
    {
        ret = VdmDeviceType::MCU;
    }
    else if (arg == static_cast<uint16_t>(VdmDeviceType::CX8))
    {
        ret = VdmDeviceType::CX8;
    }
    else
    {
        ret = VdmDeviceType::INVALID;
    }
    return ret;
}

/**
 * @brief Convert the device type enum value to JSON
 *
 * @param arg Device type enum value
 * @param json JSON object to store the device type
 */
static void erotTokenTypeToJson(const uint32_t& type, nlohmann::json& json)
{
    if (type == static_cast<uint32_t>(VdmTokenTypeERoT::UNDEFINED))
    {
        json["TokenType"] = "Undefined";
    }
    else
    {
        std::string tokenType;
        if (type & static_cast<uint32_t>(VdmTokenTypeERoT::DEBUG_FW))
        {
            tokenType = setOrAppend(tokenType, "DebugFw");
        }
        if (type & static_cast<uint32_t>(VdmTokenTypeERoT::EFRC))
        {
            tokenType = setOrAppend(tokenType, "Efrc");
        }
        json["TokenType"] = tokenType;
    }
}

/**
 * @brief Convert the GPU IRoT token type enum value to JSON
 *
 * @param arg GPU IRoT token type enum value
 * @param json JSON object to store the GPU IRoT token type
 */
static void gpuTokenTypeToJson(const uint32_t& type, nlohmann::json& json)
{
    if (type == static_cast<uint32_t>(VdmTokenTypeIRoT::UNDEFINED))
    {
        json["TokenType"] = "Undefined";
    }
    else
    {
        std::string tokenType;
        if (type & static_cast<uint32_t>(VdmTokenTypeIRoT::DEBUG_FW))
        {
            tokenType = setOrAppend(tokenType, "DebugFw");
        }
        if (type & static_cast<uint32_t>(VdmTokenTypeIRoT::JTAG_UNLOCK))
        {
            tokenType = setOrAppend(tokenType, "JtagUnlock");
        }
        if (type & static_cast<uint32_t>(VdmTokenTypeIRoT::HW_UNLOCK))
        {
            tokenType = setOrAppend(tokenType, "HwUnlock");
        }
        if (type & static_cast<uint32_t>(VdmTokenTypeIRoT::RUNTIME_DEBUG))
        {
            tokenType = setOrAppend(tokenType, "RuntimeDebug");
        }
        if (type & static_cast<uint32_t>(VdmTokenTypeIRoT::FEATURE_UNLOCK))
        {
            tokenType = setOrAppend(tokenType, "FeatureUnlock");
        }
        json["TokenType"] = tokenType;
    }
}

/**
 * @brief Convert the MCU token type enum value to JSON
 *
 * @param arg MCU token type enum value
 * @param json JSON object to store the MCU token type
 */
static void mcuTokenTypeToJson(const uint32_t& type, nlohmann::json& json)
{
    if (type == static_cast<uint32_t>(VdmTokenTypeMCU::UNDEFINED))
    {
        json["TokenType"] = "Undefined";
    }
    else
    {
        std::string tokenType;
        if (type & static_cast<uint32_t>(VdmTokenTypeMCU::DEBUG_FW))
        {
            tokenType = setOrAppend(tokenType, "DebugFw");
        }
        if (type & static_cast<uint32_t>(VdmTokenTypeMCU::OTP_DUMP_ENABLE))
        {
            tokenType = setOrAppend(tokenType, "OtpDumpEnable");
        }
        json["TokenType"] = tokenType;
    }
}

/**
 * @brief Decode the binary token lifecycle to the enum value
 *
 * @param arg Binary token lifecycle
 * @return Token lifecycle enum value
 */
static VdmTokenLifecycle getTokenLifecycle(uint16_t tokenConfig)
{
    VdmTokenLifecycle ret;
    if (tokenConfig & static_cast<uint16_t>(VdmTokenLifecycle::TEMPORAL))
    {
        ret = VdmTokenLifecycle::TEMPORAL;
    }
    else
    {
        ret = VdmTokenLifecycle::PERSISTENT;
    }
    return ret;
}

/**
 * @brief Convert the token lifecycle enum value to JSON
 *
 * @param arg Token lifecycle enum value
 * @param json JSON object to store the token lifecycle
 */
static void tokenLifecycleToJson(const VdmTokenLifecycle& arg,
                                 nlohmann::json& json)
{
    json["Lifecycle"] = arg == VdmTokenLifecycle::TEMPORAL ? "Temporal"
                                                           : "Persistent";
}

/**
 * @brief Decode the binary token activation to the enum value
 *
 * @param arg Binary token activation
 * @return Token activation enum value
 */
static VdmTokenActivation getTokenActivation(uint16_t tokenConfig)
{
    VdmTokenActivation ret;
    if (tokenConfig & static_cast<uint16_t>(VdmTokenActivation::MANUAL))
    {
        ret = VdmTokenActivation::MANUAL;
    }
    else
    {
        ret = VdmTokenActivation::ON_BOOT;
    }
    return ret;
}

/**
 * @brief Convert the token activation enum value to JSON
 *
 * @param arg Token activation enum value
 * @param json JSON object to store the token activation
 */
static void tokenActivationToJson(const VdmTokenActivation& arg,
                                  nlohmann::json& json)
{
    json["Activation"] = arg == VdmTokenActivation::MANUAL ? "Manual"
                                                           : "OnBoot";
}

/**
 * @brief Decode the binary token revocation to the enum value
 *
 * @param arg Binary token revocation
 * @return Token revocation enum value
 */
static VdmTokenRevocation getTokenRevocation(uint16_t tokenConfig)
{
    VdmTokenRevocation ret;
    if (tokenConfig & static_cast<uint16_t>(VdmTokenRevocation::AUTOMATIC))
    {
        ret = VdmTokenRevocation::AUTOMATIC;
    }
    else
    {
        ret = VdmTokenRevocation::MANUAL;
    }
    return ret;
}

/**
 * @brief Convert the token revocation enum value to JSON
 *
 * @param arg Token revocation enum value
 * @param json JSON object to store the token revocation
 */
static void tokenRevocationToJson(const VdmTokenRevocation& arg,
                                  nlohmann::json& json)
{
    json["Revocation"] = arg == VdmTokenRevocation::AUTOMATIC ? "Automatic"
                                                              : "Manual";
}

/**
 * @brief Decode the binary token device ID status to the enum value
 *
 * @param arg Binary token device ID status
 * @return Token device ID status enum value
 */
static VdmTokenDevIdStatus getTokenDevIdStatus(uint16_t tokenConfig)
{
    VdmTokenDevIdStatus ret;
    if (tokenConfig & static_cast<uint16_t>(VdmTokenDevIdStatus::ENABLED))
    {
        ret = VdmTokenDevIdStatus::ENABLED;
    }
    else
    {
        ret = VdmTokenDevIdStatus::DISABLED;
    }
    return ret;
}

/**
 * @brief Convert the token device ID status enum value to JSON
 *
 * @param arg Token device ID status enum value
 * @param json JSON object to store the token device ID status
 */
static void tokenDevIdStatusToJson(const VdmTokenDevIdStatus& arg,
                                   nlohmann::json& json)
{
    json["DevIdStatus"] = arg == VdmTokenDevIdStatus::ENABLED ? "Enabled"
                                                              : "Disabled";
}

/**
 * @brief Decode the binary token anti-replay to the enum value
 *
 * @param arg Binary token anti-replay
 * @return Token anti-replay enum value
 */
static VdmTokenAntiReplay getTokenAntiReplay(uint16_t tokenConfig)
{
    VdmTokenAntiReplay ret;
    if (tokenConfig & static_cast<uint16_t>(VdmTokenAntiReplay::NONCE_ENABLED))
    {
        ret = VdmTokenAntiReplay::NONCE_ENABLED;
    }
    else
    {
        ret = VdmTokenAntiReplay::NONCE_DISABLED;
    }
    return ret;
}

/**
 * @brief Convert the token anti-replay enum value to JSON
 *
 * @param arg Token anti-replay enum value
 * @param json JSON object to store the token anti-replay
 */
static void tokenAntiReplayToJson(const VdmTokenAntiReplay& arg,
                                  nlohmann::json& json)
{
    json["AntiReplay"] = arg == VdmTokenAntiReplay::NONCE_ENABLED
                             ? "NonceEnabled"
                             : "NonceDisabled";
}

/**
 * @brief Decode the binary token reset post-install to the enum value
 *
 * @param arg Binary token reset post-install
 * @return Token reset post-install enum value
 */
static VdmTokenResetPostInstall getTokenResetPostInstall(uint16_t tokenConfig)
{
    VdmTokenResetPostInstall ret;
    if (tokenConfig & static_cast<uint16_t>(VdmTokenResetPostInstall::MANDATED))
    {
        ret = VdmTokenResetPostInstall::MANDATED;
    }
    else
    {
        ret = VdmTokenResetPostInstall::NOT_MANDATED;
    }
    return ret;
}

/**
 * @brief Convert the token reset post-install enum value to JSON
 *
 * @param arg Token reset post-install enum value
 * @param json JSON object to store the token reset post-install
 */
static void tokenResetPostInstallToJson(const VdmTokenResetPostInstall& arg,
                                        nlohmann::json& json)
{
    json["ResetPostInstall"] =
        arg == VdmTokenResetPostInstall::MANDATED ? "Mandated" : "NotMandated";
}

/**
 * @brief Decode the binary token processing status to the enum value
 *
 * @param arg Binary token processing status
 * @return Token processing status enum value
 */
static VdmTokenProcessingStatus getTokenProcessingStatus(uint16_t arg)
{
    VdmTokenProcessingStatus ret;
    if (arg == static_cast<uint16_t>(VdmTokenProcessingStatus::NOT_PROCESSED))
    {
        ret = VdmTokenProcessingStatus::NOT_PROCESSED;
    }
    else if (arg == static_cast<uint16_t>(VdmTokenProcessingStatus::PROCESSED))
    {
        ret = VdmTokenProcessingStatus::PROCESSED;
    }
    else if (arg == static_cast<uint16_t>(
                        VdmTokenProcessingStatus::VERIFICATION_FAILURE))
    {
        ret = VdmTokenProcessingStatus::VERIFICATION_FAILURE;
    }
    else if (arg ==
             static_cast<uint16_t>(VdmTokenProcessingStatus::RUNTIME_ERROR))
    {
        ret = VdmTokenProcessingStatus::RUNTIME_ERROR;
    }
    else
    {
        ret = VdmTokenProcessingStatus::INVALID;
    }
    return ret;
}

/**
 * @brief Convert the token processing status enum value to JSON
 *
 * @param status Token processing status enum value
 * @param json JSON object to store the token processing status
 */
static void tokenProcessingStatusToJson(const VdmTokenProcessingStatus& status,
                                        nlohmann::json& json)
{
    if (status == VdmTokenProcessingStatus::NOT_PROCESSED)
    {
        json["ProcessingStatus"] = "NotProcessed";
    }
    else if (status == VdmTokenProcessingStatus::PROCESSED)
    {
        json["ProcessingStatus"] = "Processed";
    }
    else if (status == VdmTokenProcessingStatus::VERIFICATION_FAILURE)
    {
        json["ProcessingStatus"] = "VerificationFailure";
    }
    else if (status == VdmTokenProcessingStatus::RUNTIME_ERROR)
    {
        json["ProcessingStatus"] = "RuntimeError";
    }
    else
    {
        json["ProcessingStatus"] = "Invalid";
    }
}

/**
 * @brief Convert the device ID to JSON
 *
 * @param deviceId Device ID
 * @param json JSON object to store the device ID
 */
static void deviceIDToJson(const std::vector<uint8_t>& deviceId,
                           nlohmann::json& json)
{
    std::ostringstream oss;
    oss << "0x";
    oss << std::hex << std::uppercase << std::setfill('0');
    auto itr = deviceId.begin();
    while (itr != deviceId.end())
    {
        oss << std::setw(2) << static_cast<int>(*itr++);
    }
    json["DeviceID"] = oss.str();
}

/**
 * @brief VDM token status class
 */
struct VdmTokenStatus
{
#pragma pack(1)
    struct VdmStatusV1
    {
        uint8_t tokenInstallationStatus;
        uint8_t deviceId[vdmStatusDeviceIdLength];
        uint8_t fuseType;
    };
#pragma pack()
#pragma pack(1)
    struct VdmStatusV2
    {
        uint8_t tokenInstallationStatus;
        uint8_t deviceId[vdmStatusV2DeviceIdLength];
        uint8_t fuseType;
        uint32_t tokenType;
        uint16_t validityCounter;
        uint16_t tokenConfig;
        uint16_t processingStatus;
        uint8_t reserved[8];
    };
#pragma pack()
#pragma pack(1)
    struct VdmStatusV3
    {
        uint8_t responseDataVersion;
        uint16_t deviceType;
        uint8_t tokenInstallationStatus;
        uint8_t deviceId[vdmStatusV3DeviceIdLength];
        uint8_t fuseType;
        uint32_t tokenType;
        uint8_t reserved[16];
    };
#pragma pack()

    VdmResponseStatus responseStatus;
    std::optional<uint8_t> errorCode;
    VdmTokenInstallationStatus tokenStatus =
        VdmTokenInstallationStatus::NOT_INSTALLED;
    VdmTokenFuseType fuseType = VdmTokenFuseType::INVALID;
    std::optional<VdmDeviceType> deviceType;
    std::optional<uint32_t> tokenType;
    std::vector<uint8_t> deviceId;
    std::optional<uint16_t> validityCounter;
    std::optional<VdmTokenLifecycle> tokenLifecycle;
    std::optional<VdmTokenActivation> tokenActivation;
    std::optional<VdmTokenRevocation> tokenRevocation;
    std::optional<VdmTokenDevIdStatus> tokenDevIdStatus;
    std::optional<VdmTokenAntiReplay> tokenAntiReplay;
    std::optional<VdmTokenResetPostInstall> tokenResetPostInstall;
    std::optional<VdmTokenProcessingStatus> tokenProcessingStatus;

    /**
     * @brief Convert VDM status query response to VDM token status
     *
     * @param vdmResponse VDM status query response
     * @param version VDM status query version
     */
    VdmTokenStatus(std::string& vdmResponse, int version)
    {
        if (vdmResponse.empty())
        {
            responseStatus = VdmResponseStatus::INVALID_LENGTH;
            return;
        }
        std::istringstream iss{vdmResponse};
        std::vector<std::string> bytes{std::istream_iterator<std::string>{iss},
                                       std::istream_iterator<std::string>{}};
        if (bytes.size() < vdmStatusErrorCodeOffset + 1)
        {
            BMCWEB_LOG_ERROR("VDM status response is too short");
            responseStatus = VdmResponseStatus::INVALID_LENGTH;
            return;
        }
        try
        {
            errorCode = static_cast<uint8_t>(
                stoul(bytes[vdmStatusErrorCodeOffset], nullptr, 16));
        }
        catch (const std::exception&)
        {
            BMCWEB_LOG_ERROR("Invalid error code byte received: {}",
                             bytes[vdmStatusErrorCodeOffset]);
            responseStatus = VdmResponseStatus::PROCESSING_ERROR;
            return;
        }
        if (*errorCode == vdmStatusErrorCodeNotSupported)
        {
            responseStatus = VdmResponseStatus::NOT_SUPPORTED;
            return;
        }
        if (*errorCode != vdmStatusErrorCodeSuccess)
        {
            responseStatus = VdmResponseStatus::ERROR;
            return;
        }

        std::vector<uint8_t> statusData;
        statusData.reserve(bytes.size() - vdmStatusErrorCodeOffset - 1);
        auto itr = bytes.begin() + vdmStatusErrorCodeOffset + 1;
        while (itr != bytes.end())
        {
            auto byte = *itr++;
            try
            {
                statusData.push_back(
                    static_cast<uint8_t>(stoul(byte, nullptr, 16)));
            }
            catch (const std::exception&)
            {
                BMCWEB_LOG_ERROR("Invalid byte received: {}", byte);
                responseStatus = VdmResponseStatus::PROCESSING_ERROR;
                return;
            }
        }
        if (version == 1)
        {
            if (statusData.size() != sizeof(VdmStatusV1))
            {
                BMCWEB_LOG_ERROR(
                    "Invalid length, received {} bytes, expected {}",
                    statusData.size(), sizeof(VdmStatusV1));
                responseStatus = VdmResponseStatus::INVALID_LENGTH;
                return;
            }
            VdmStatusV1* status =
                reinterpret_cast<VdmStatusV1*>(statusData.data());
            tokenStatus =
                getTokenInstallationStatus(status->tokenInstallationStatus);
            deviceId.resize(vdmStatusDeviceIdLength);
            std::memcpy(deviceId.data(), status->deviceId,
                        vdmStatusDeviceIdLength);
            fuseType = getTokenFuseType(status->fuseType);
            responseStatus = VdmResponseStatus::STATUS;
            return;
        }
        if (version == 2)
        {
            if (statusData.size() != sizeof(VdmStatusV2))
            {
                BMCWEB_LOG_ERROR(
                    "Invalid length, received {} bytes, expected {}",
                    statusData.size(), sizeof(VdmStatusV2));
                responseStatus = VdmResponseStatus::INVALID_LENGTH;
                return;
            }
            VdmStatusV2* status =
                reinterpret_cast<VdmStatusV2*>(statusData.data());
            status->tokenType = be32toh(status->tokenType);
            status->validityCounter = be16toh(status->validityCounter);
            status->tokenConfig = be16toh(status->tokenConfig);
            status->processingStatus = be16toh(status->processingStatus);
            tokenStatus =
                getTokenInstallationStatus(status->tokenInstallationStatus);
            deviceId.resize(vdmStatusV2DeviceIdLength);
            std::memcpy(deviceId.data(), status->deviceId,
                        vdmStatusV2DeviceIdLength);
            fuseType = getTokenFuseType(status->fuseType);
            tokenType = status->tokenType;
            validityCounter = status->validityCounter;
            tokenLifecycle = getTokenLifecycle(status->tokenConfig);
            tokenActivation = getTokenActivation(status->tokenConfig);
            tokenRevocation = getTokenRevocation(status->tokenConfig);
            tokenDevIdStatus = getTokenDevIdStatus(status->tokenConfig);
            tokenAntiReplay = getTokenAntiReplay(status->tokenConfig);
            tokenResetPostInstall =
                getTokenResetPostInstall(status->tokenConfig);
            tokenProcessingStatus =
                getTokenProcessingStatus(status->processingStatus);
            responseStatus = VdmResponseStatus::STATUS;
            return;
        }
        if (version == 3)
        {
            if (statusData.size() != sizeof(VdmStatusV3))
            {
                BMCWEB_LOG_ERROR(
                    "Invalid length, received {} bytes, expected {}",
                    statusData.size(), sizeof(VdmStatusV3));
                responseStatus = VdmResponseStatus::INVALID_LENGTH;
                return;
            }
            VdmStatusV3* status =
                reinterpret_cast<VdmStatusV3*>(statusData.data());
            status->deviceType = be16toh(status->deviceType);
            status->tokenType = be32toh(status->tokenType);
            deviceType = getDeviceType(status->deviceType);
            tokenStatus =
                getTokenInstallationStatus(status->tokenInstallationStatus);
            deviceId.resize(vdmStatusV3DeviceIdLength);
            std::memcpy(deviceId.data(), status->deviceId,
                        vdmStatusV3DeviceIdLength);
            fuseType = getTokenFuseType(status->fuseType);
            tokenType = status->tokenType;
            responseStatus = VdmResponseStatus::STATUS;
            return;
        }
        BMCWEB_LOG_ERROR("Invalid version: {}", version);
        responseStatus = VdmResponseStatus::PROCESSING_ERROR;
    }
};

/**
 * @brief Parse the VDM utility wrapper output
 *
 * @param output VDM utility wrapper output
 * @return Map of EID to VDM token status
 */
inline std::map<int, VdmTokenStatus>
    parseVdmUtilWrapperOutput(std::vector<char>& output)
{
    boost::interprocess::bufferstream outputStream(output.data(),
                                                   output.size());
    std::string line;
    std::map<int, VdmTokenStatus> outputMap;
    while (std::getline(outputStream, line))
    {
        if (line.empty())
        {
            continue;
        }
        std::stringstream lineStream{line};
        std::string elem;
        std::vector<std::string> lineElements;
        // each line of the wrapper script output has the following format:
        // EID;VERSION;TXDATA;RXDATA
        while (std::getline(lineStream, elem, ';'))
        {
            lineElements.push_back(elem);
        }
        if (lineElements.size() < vdmUtilWrapperOutputRxIndex)
        {
            BMCWEB_LOG_ERROR("Invalid data: ", line);
            continue;
        }
        int eid, version;
        try
        {
            eid = std::stoi(lineElements[vdmUtilWrapperOutputEidIndex]);
            version = std::stoi(lineElements[vdmUtilWrapperOutputVersionIndex]);
        }
        catch (const std::exception&)
        {
            BMCWEB_LOG_ERROR("Invalid data: ", line);
            continue;
        }
        auto& txLine = lineElements[vdmUtilWrapperOutputTxIndex];
        auto& rxLine = lineElements[vdmUtilWrapperOutputRxIndex];
        BMCWEB_LOG_DEBUG("EID: {} TX: {}", eid, txLine);
        BMCWEB_LOG_DEBUG("EID: {} RX: {}", eid, rxLine);
        VdmTokenStatus status(rxLine, version);
        // if more than one query was executed, use the one which contained
        // correct status output
        auto prevStatus = outputMap.find(eid);
        if (prevStatus != outputMap.end())
        {
            if (prevStatus->second.responseStatus !=
                    VdmResponseStatus::STATUS &&
                status.responseStatus == VdmResponseStatus::STATUS)
            {
                prevStatus->second = std::move(status);
            }
        }
        else
        {
            outputMap.insert(std::make_pair(eid, std::move(status)));
        }
    }
    return outputMap;
}

/**
 * @brief Convert the VDM token status to JSON
 *
 * @param status VDM token status
 * @param json JSON object to store the VDM token status
 */
inline void vdmTokenStatusToJson(const VdmTokenStatus& status,
                                 nlohmann::json& json)
{
    tokenInstallationStatusToJson(status.tokenStatus, json);
    tokenFuseTypeToJson(status.fuseType, json);
    deviceIDToJson(status.deviceId, json);
    if (status.tokenType)
    {
        if (status.deviceType == VdmDeviceType::EROT)
        {
            erotTokenTypeToJson(*status.tokenType, json);
        }
        else if (status.deviceType == VdmDeviceType::GPU)
        {
            gpuTokenTypeToJson(*status.tokenType, json);
        }
        else if (status.deviceType == VdmDeviceType::MCU)
        {
            mcuTokenTypeToJson(*status.tokenType, json);
        }
    }
    if (status.validityCounter)
    {
        json["ValidityCounter"] = *status.validityCounter;
    }
    if (status.tokenLifecycle)
    {
        tokenLifecycleToJson(*status.tokenLifecycle, json);
    }
    if (status.tokenActivation)
    {
        tokenActivationToJson(*status.tokenActivation, json);
    }
    if (status.tokenRevocation)
    {
        tokenRevocationToJson(*status.tokenRevocation, json);
    }
    if (status.tokenDevIdStatus)
    {
        tokenDevIdStatusToJson(*status.tokenDevIdStatus, json);
    }
    if (status.tokenAntiReplay)
    {
        tokenAntiReplayToJson(*status.tokenAntiReplay, json);
    }
    if (status.tokenResetPostInstall)
    {
        tokenResetPostInstallToJson(*status.tokenResetPostInstall, json);
    }
    if (status.tokenProcessingStatus)
    {
        tokenProcessingStatusToJson(*status.tokenProcessingStatus, json);
    }
}

} // namespace redfish::debug_token
