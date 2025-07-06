#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_power_policy
{
// clang-format off

enum class UnitType{
    Invalid,
    Watts,
};

enum class ActionType{
    Invalid,
    AssertPowerBrake,
    DeassertPowerBrake,
    SendEvent,
    DoNothing,
};

enum class ComparisonType{
    Invalid,
    Above,
    Below,
    Inclusive,
};

NLOHMANN_JSON_SERIALIZE_ENUM(UnitType, {
    {UnitType::Invalid, "Invalid"},
    {UnitType::Watts, "Watts"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(ActionType, {
    {ActionType::Invalid, "Invalid"},
    {ActionType::AssertPowerBrake, "AssertPowerBrake"},
    {ActionType::DeassertPowerBrake, "DeassertPowerBrake"},
    {ActionType::SendEvent, "SendEvent"},
    {ActionType::DoNothing, "DoNothing"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(ComparisonType, {
    {ComparisonType::Invalid, "Invalid"},
    {ComparisonType::Above, "Above"},
    {ComparisonType::Below, "Below"},
    {ComparisonType::Inclusive, "Inclusive"},
});

}
// clang-format on
