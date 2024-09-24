#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_environment_metrics
{
// clang-format off

enum class PowerMode{
    Invalid,
    MaxP,
    MaxQ,
    Custom,
};

NLOHMANN_JSON_SERIALIZE_ENUM(PowerMode, {
    {PowerMode::Invalid, "Invalid"},
    {PowerMode::MaxP, "MaxP"},
    {PowerMode::MaxQ, "MaxQ"},
    {PowerMode::Custom, "Custom"},
});

}
// clang-format on
