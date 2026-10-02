// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_astra
{
// clang-format off

enum class AstraMode{
    Invalid,
    Enabled,
    Disabled,
};

enum class AstraState{
    Invalid,
    Enabled,
    Disabled,
    PendingEnable,
    PendingDisable,
    Error,
};

NLOHMANN_JSON_SERIALIZE_ENUM(AstraMode, {
    {AstraMode::Invalid, "Invalid"},
    {AstraMode::Enabled, "Enabled"},
    {AstraMode::Disabled, "Disabled"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(AstraState, {
    {AstraState::Invalid, "Invalid"},
    {AstraState::Enabled, "Enabled"},
    {AstraState::Disabled, "Disabled"},
    {AstraState::PendingEnable, "PendingEnable"},
    {AstraState::PendingDisable, "PendingDisable"},
    {AstraState::Error, "Error"},
});

}
// clang-format on
