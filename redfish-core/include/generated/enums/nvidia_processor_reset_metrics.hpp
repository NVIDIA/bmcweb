#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_processor_reset_metrics
{
// clang-format off

enum class LastResetType{
    Invalid,
    PF_FLR,
    Conventional,
    Fundamental,
    IRoTReset,
};

NLOHMANN_JSON_SERIALIZE_ENUM(LastResetType, {
    {LastResetType::Invalid, "Invalid"},
    {LastResetType::PF_FLR, "PF_FLR"},
    {LastResetType::Conventional, "Conventional"},
    {LastResetType::Fundamental, "Fundamental"},
    {LastResetType::IRoTReset, "IRoTReset"},
});

}
// clang-format on
