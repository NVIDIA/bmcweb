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

enum class DiagStatus{
    Invalid,
    InProgress,
    RecoveryMode,
    Completed,
    Aborted,
    NotStarted,
    TestRunning,
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

NLOHMANN_JSON_SERIALIZE_ENUM(DiagStatus, {
    {DiagStatus::Invalid, "Invalid"},
    {DiagStatus::InProgress, "InProgress"},
    {DiagStatus::RecoveryMode, "RecoveryMode"},
    {DiagStatus::Completed, "Completed"},
    {DiagStatus::Aborted, "Aborted"},
    {DiagStatus::NotStarted, "NotStarted"},
    {DiagStatus::TestRunning, "TestRunning"},
});

// clang-format on
} // namespace nvidia_computer_system
