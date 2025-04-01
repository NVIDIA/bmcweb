// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_switch
{
// clang-format off

enum class SwitchIsolationMode{
    Invalid,
    SwitchCommunicationEnabled,
    SwitchCommunicationDisabled,
};

NLOHMANN_JSON_SERIALIZE_ENUM(SwitchIsolationMode, {
    {SwitchIsolationMode::Invalid, "Invalid"},
    {SwitchIsolationMode::SwitchCommunicationEnabled, "SwitchCommunicationEnabled"},
    {SwitchIsolationMode::SwitchCommunicationDisabled, "SwitchCommunicationDisabled"},
});

}
// clang-format on
