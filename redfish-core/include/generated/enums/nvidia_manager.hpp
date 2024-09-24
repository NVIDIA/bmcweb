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

}
// clang-format on
