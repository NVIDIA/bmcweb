// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_power_compliance_manager
{
// clang-format off

enum class NvidiaManagerType{
    Invalid,
    PowerManager,
};

NLOHMANN_JSON_SERIALIZE_ENUM(NvidiaManagerType, {
    {NvidiaManagerType::Invalid, "Invalid"},
    {NvidiaManagerType::PowerManager, "PowerManager"},
});

}
// clang-format on
