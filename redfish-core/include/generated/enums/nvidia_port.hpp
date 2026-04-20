// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_port
{
// clang-format off

enum class ClockMode{
    Invalid,
    CommonClock,
    SeparateClock,
};

NLOHMANN_JSON_SERIALIZE_ENUM(ClockMode, {
    {ClockMode::Invalid, "Invalid"},
    {ClockMode::CommonClock, "CommonClock"},
    {ClockMode::SeparateClock, "SeparateClock"},
});

// clang-format on
} // namespace nvidia_port
