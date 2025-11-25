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

}
// clang-format on
