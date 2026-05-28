// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_computer_system
{
// clang-format off

enum class CapabilityStatus{
    Invalid,
    Enabled,
    Disabled,
    Offline,
    Unknown,
};

enum class ResetBiosType{
    Invalid,
    SecureReset,
    NonSecureReset,
};

enum class DOTCAKInitializationState{
    Invalid,
    Waiting,
    Complete,
    EarlyBoot,
};

enum class EnableDisableOption{
    Invalid,
    Enable,
    Disable,
    Default,
};

enum class OperatingSystemState{
    Invalid,
    Inactive,
    Standby,
    BootComplete,
    PXEBoot,
    CBoot,
    CDROMBoot,
    DiagBoot,
    ROMBoot,
};

NLOHMANN_JSON_SERIALIZE_ENUM(CapabilityStatus, {
    {CapabilityStatus::Invalid, "Invalid"},
    {CapabilityStatus::Enabled, "Enabled"},
    {CapabilityStatus::Disabled, "Disabled"},
    {CapabilityStatus::Offline, "Offline"},
    {CapabilityStatus::Unknown, "Unknown"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(ResetBiosType, {
    {ResetBiosType::Invalid, "Invalid"},
    {ResetBiosType::SecureReset, "SecureReset"},
    {ResetBiosType::NonSecureReset, "NonSecureReset"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(DOTCAKInitializationState, {
    {DOTCAKInitializationState::Invalid, "Invalid"},
    {DOTCAKInitializationState::Waiting, "Waiting"},
    {DOTCAKInitializationState::Complete, "Complete"},
    {DOTCAKInitializationState::EarlyBoot, "EarlyBoot"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(EnableDisableOption, {
    {EnableDisableOption::Invalid, "Invalid"},
    {EnableDisableOption::Enable, "Enable"},
    {EnableDisableOption::Disable, "Disable"},
    {EnableDisableOption::Default, "Default"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(OperatingSystemState, {
    {OperatingSystemState::Invalid, "Invalid"},
    {OperatingSystemState::Inactive, "Inactive"},
    {OperatingSystemState::Standby, "Standby"},
    {OperatingSystemState::BootComplete, "BootComplete"},
    {OperatingSystemState::PXEBoot, "PXEBoot"},
    {OperatingSystemState::CBoot, "CBoot"},
    {OperatingSystemState::CDROMBoot, "CDROMBoot"},
    {OperatingSystemState::DiagBoot, "DiagBoot"},
    {OperatingSystemState::ROMBoot, "ROMBoot"},
});

}
// clang-format on
