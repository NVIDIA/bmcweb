// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_switch_u_phy_recovery_mode
{
// clang-format off

enum class UPhyRecoveryMode{
    Invalid,
    Enabled,
    Disabled,
};

NLOHMANN_JSON_SERIALIZE_ENUM(UPhyRecoveryMode, {
    {UPhyRecoveryMode::Invalid, "Invalid"},
    {UPhyRecoveryMode::Enabled, "Enabled"},
    {UPhyRecoveryMode::Disabled, "Disabled"},
});

// clang-format on
} // namespace nvidia_switch_u_phy_recovery_mode
