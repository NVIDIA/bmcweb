// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_switch_power_cap_mode
{
// clang-format off

enum class PowerCapMode{
    Invalid,
    Enabled,
    Disabled,
};

NLOHMANN_JSON_SERIALIZE_ENUM(PowerCapMode, {
    {PowerCapMode::Invalid, "Invalid"},
    {PowerCapMode::Enabled, "Enabled"},
    {PowerCapMode::Disabled, "Disabled"},
});

// clang-format on
} // namespace nvidia_switch_power_cap_mode
