#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_processor_metrics
{
// clang-format off

enum class PerformanceStateType{
    Invalid,
    Normal,
    Throttled,
    Degraded,
};

enum class EDPViolationStateType{
    Invalid,
    Normal,
    OutOfRange,
};

NLOHMANN_JSON_SERIALIZE_ENUM(PerformanceStateType, {
    {PerformanceStateType::Invalid, "Invalid"},
    {PerformanceStateType::Normal, "Normal"},
    {PerformanceStateType::Throttled, "Throttled"},
    {PerformanceStateType::Degraded, "Degraded"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(EDPViolationStateType, {
    {EDPViolationStateType::Invalid, "Invalid"},
    {EDPViolationStateType::Normal, "Normal"},
    {EDPViolationStateType::OutOfRange, "OutOfRange"},
});

}
// clang-format on
