// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_switch_ltx_mode
{
// clang-format off

enum class LTXMode{
    Invalid,
    Enabled,
    Disabled,
};

NLOHMANN_JSON_SERIALIZE_ENUM(LTXMode, {
    {LTXMode::Invalid, "Invalid"},
    {LTXMode::Enabled, "Enabled"},
    {LTXMode::Disabled, "Disabled"},
});

}
// clang-format on
