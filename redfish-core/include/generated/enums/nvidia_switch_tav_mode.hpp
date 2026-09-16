// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_switch_tav_mode
{
// clang-format off

enum class TAVMode{
    Invalid,
    Enabled,
    Disabled,
};

NLOHMANN_JSON_SERIALIZE_ENUM(TAVMode, {
    {TAVMode::Invalid, "Invalid"},
    {TAVMode::Enabled, "Enabled"},
    {TAVMode::Disabled, "Disabled"},
});

// clang-format on
} // namespace nvidia_switch_tav_mode
