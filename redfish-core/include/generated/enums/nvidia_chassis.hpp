#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_chassis
{
// clang-format off

enum class BackgroundCopyStatus{
    Invalid,
    Pending,
    InProgress,
    Completed,
};

enum class StaticPowerHintOutputState{
    Completed,
    InProgress,
    Failed,
    InvalidArgument,
    Invalid,
};

NLOHMANN_JSON_SERIALIZE_ENUM(BackgroundCopyStatus, {
    {BackgroundCopyStatus::Invalid, "Invalid"},
    {BackgroundCopyStatus::Pending, "Pending"},
    {BackgroundCopyStatus::InProgress, "InProgress"},
    {BackgroundCopyStatus::Completed, "Completed"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(StaticPowerHintOutputState, {
    {StaticPowerHintOutputState::Completed, "Completed"},
    {StaticPowerHintOutputState::InProgress, "InProgress"},
    {StaticPowerHintOutputState::Failed, "Failed"},
    {StaticPowerHintOutputState::InvalidArgument, "InvalidArgument"},
    {StaticPowerHintOutputState::Invalid, "Invalid"},
});

}
// clang-format on
