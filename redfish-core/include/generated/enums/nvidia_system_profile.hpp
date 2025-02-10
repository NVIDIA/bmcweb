#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_system_profile
{
// clang-format off

enum class ActionStatus{
    None,
    Start,
    StartBios,
    StartVerification,
    ProfileSaved,
    ProfileSavedBios,
    BiosStarted,
    BiosFinished,
    BmcStarted,
    Active,
    Failed,
    PendingBios,
    Invalid,
};

NLOHMANN_JSON_SERIALIZE_ENUM(ActionStatus, {
    {ActionStatus::None, "None"},
    {ActionStatus::Start, "Start"},
    {ActionStatus::StartBios, "StartBios"},
    {ActionStatus::StartVerification, "StartVerification"},
    {ActionStatus::ProfileSaved, "ProfileSaved"},
    {ActionStatus::ProfileSavedBios, "ProfileSavedBios"},
    {ActionStatus::BiosStarted, "BiosStarted"},
    {ActionStatus::BiosFinished, "BiosFinished"},
    {ActionStatus::BmcStarted, "BmcStarted"},
    {ActionStatus::Active, "Active"},
    {ActionStatus::Failed, "Failed"},
    {ActionStatus::PendingBios, "PendingBios"},
    {ActionStatus::Invalid, "Invalid"},
});

}
// clang-format on
