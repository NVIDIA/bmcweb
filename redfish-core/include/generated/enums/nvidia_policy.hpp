#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_policy
{
// clang-format off

enum class PolicyConditionLogicEnum{
    Invalid,
    AllOf,
    AnyOf,
};

enum class CommonReactionEnum{
    Invalid,
    HardPowerOff,
};

NLOHMANN_JSON_SERIALIZE_ENUM(PolicyConditionLogicEnum, {
    {PolicyConditionLogicEnum::Invalid, "Invalid"},
    {PolicyConditionLogicEnum::AllOf, "AllOf"},
    {PolicyConditionLogicEnum::AnyOf, "AnyOf"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(CommonReactionEnum, {
    {CommonReactionEnum::Invalid, "Invalid"},
    {CommonReactionEnum::HardPowerOff, "HardPowerOff"},
});

}
// clang-format on
