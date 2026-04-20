// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_dot_management
{
// clang-format off

enum class CAKInstalledState{
    Invalid,
    NotInstalled,
    Installed,
    Error,
};

NLOHMANN_JSON_SERIALIZE_ENUM(CAKInstalledState, {
    {CAKInstalledState::Invalid, "Invalid"},
    {CAKInstalledState::NotInstalled, "NotInstalled"},
    {CAKInstalledState::Installed, "Installed"},
    {CAKInstalledState::Error, "Error"},
});

// clang-format on
} // namespace nvidia_dot_management
