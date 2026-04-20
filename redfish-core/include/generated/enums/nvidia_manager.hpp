// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_manager
{
// clang-format off

enum class FirmwareBuildType{
    Invalid,
    ProvisioningDebug,
    ProvisioningProduction,
    PlatformDebug,
    PlatformProduction,
    ProvisioningDevelopment,
    PlatformDevelopment,
};

enum class SMBPBIFencingPrivilege{
    Invalid,
    HMC,
    HostBMC,
    None,
};

enum class OOBRawCommandTargetType{
    Invalid,
    GPU,
    NVSwitch,
    Baseboard,
};

enum class FabricManagerState{
    Invalid,
    Offline,
    Standby,
    Configured,
    Error,
    Unknown,
};

enum class ReportStatus{
    Invalid,
    Pending,
    Received,
    Timeout,
    Unknown,
};

enum class SWEIJRequestType{
    Invalid,
    Setup,
    Cleanup,
    Injection,
};

enum class RestrictionMode{
    Invalid,
    None,
    Allowlist,
    ProvisionedHostAllowlist,
    ProvisionedHostDisabled,
};

enum class RASRawCommandType{
    Invalid,
    I2C,
    I3C,
};

enum class RASRawCommandOpCode{
    Invalid,
    Write,
    Read,
};

NLOHMANN_JSON_SERIALIZE_ENUM(FirmwareBuildType, {
    {FirmwareBuildType::Invalid, "Invalid"},
    {FirmwareBuildType::ProvisioningDebug, "ProvisioningDebug"},
    {FirmwareBuildType::ProvisioningProduction, "ProvisioningProduction"},
    {FirmwareBuildType::PlatformDebug, "PlatformDebug"},
    {FirmwareBuildType::PlatformProduction, "PlatformProduction"},
    {FirmwareBuildType::ProvisioningDevelopment, "ProvisioningDevelopment"},
    {FirmwareBuildType::PlatformDevelopment, "PlatformDevelopment"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(SMBPBIFencingPrivilege, {
    {SMBPBIFencingPrivilege::Invalid, "Invalid"},
    {SMBPBIFencingPrivilege::HMC, "HMC"},
    {SMBPBIFencingPrivilege::HostBMC, "HostBMC"},
    {SMBPBIFencingPrivilege::None, "None"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(OOBRawCommandTargetType, {
    {OOBRawCommandTargetType::Invalid, "Invalid"},
    {OOBRawCommandTargetType::GPU, "GPU"},
    {OOBRawCommandTargetType::NVSwitch, "NVSwitch"},
    {OOBRawCommandTargetType::Baseboard, "Baseboard"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(FabricManagerState, {
    {FabricManagerState::Invalid, "Invalid"},
    {FabricManagerState::Offline, "Offline"},
    {FabricManagerState::Standby, "Standby"},
    {FabricManagerState::Configured, "Configured"},
    {FabricManagerState::Error, "Error"},
    {FabricManagerState::Unknown, "Unknown"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(ReportStatus, {
    {ReportStatus::Invalid, "Invalid"},
    {ReportStatus::Pending, "Pending"},
    {ReportStatus::Received, "Received"},
    {ReportStatus::Timeout, "Timeout"},
    {ReportStatus::Unknown, "Unknown"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(SWEIJRequestType, {
    {SWEIJRequestType::Invalid, "Invalid"},
    {SWEIJRequestType::Setup, "Setup"},
    {SWEIJRequestType::Cleanup, "Cleanup"},
    {SWEIJRequestType::Injection, "Injection"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(RestrictionMode, {
    {RestrictionMode::Invalid, "Invalid"},
    {RestrictionMode::None, "None"},
    {RestrictionMode::Allowlist, "Allowlist"},
    {RestrictionMode::ProvisionedHostAllowlist, "ProvisionedHostAllowlist"},
    {RestrictionMode::ProvisionedHostDisabled, "ProvisionedHostDisabled"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(RASRawCommandType, {
    {RASRawCommandType::Invalid, "Invalid"},
    {RASRawCommandType::I2C, "I2C"},
    {RASRawCommandType::I3C, "I3C"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(RASRawCommandOpCode, {
    {RASRawCommandOpCode::Invalid, "Invalid"},
    {RASRawCommandOpCode::Write, "Write"},
    {RASRawCommandOpCode::Read, "Read"},
});

// clang-format on
} // namespace nvidia_manager
