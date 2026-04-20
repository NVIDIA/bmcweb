// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_power_domain
{
// clang-format off

enum class UnitType{
    Invalid,
    Watts,
};

enum class ComparisonType{
    Invalid,
    Above,
    Below,
};

NLOHMANN_JSON_SERIALIZE_ENUM(UnitType, {
    {UnitType::Invalid, "Invalid"},
    {UnitType::Watts, "Watts"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(ComparisonType, {
    {ComparisonType::Invalid, "Invalid"},
    {ComparisonType::Above, "Above"},
    {ComparisonType::Below, "Below"},
});

// clang-format on
} // namespace nvidia_power_domain
