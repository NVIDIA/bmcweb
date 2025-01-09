#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_ro_t_protected_component
{
// clang-format off

enum class RoTProtectedComponentType{
    Invalid,
    Self,
    AP,
};

enum class UpdateMethods{
    Invalid,
    FLR,
    HotReset,
    WarmReset,
    ACPowerCycle,
    DCPowerCycle,
    MediumSpecificReset,
    Automatic,
};

enum class BackgroundCopyStatus{
    Invalid,
    Pending,
    InProgress,
    Completed,
    Failed,
};

NLOHMANN_JSON_SERIALIZE_ENUM(RoTProtectedComponentType, {
    {RoTProtectedComponentType::Invalid, "Invalid"},
    {RoTProtectedComponentType::Self, "Self"},
    {RoTProtectedComponentType::AP, "AP"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(UpdateMethods, {
    {UpdateMethods::Invalid, "Invalid"},
    {UpdateMethods::FLR, "FLR"},
    {UpdateMethods::HotReset, "HotReset"},
    {UpdateMethods::WarmReset, "WarmReset"},
    {UpdateMethods::ACPowerCycle, "ACPowerCycle"},
    {UpdateMethods::DCPowerCycle, "DCPowerCycle"},
    {UpdateMethods::MediumSpecificReset, "MediumSpecificReset"},
    {UpdateMethods::Automatic, "Automatic"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(BackgroundCopyStatus, {
    {BackgroundCopyStatus::Invalid, "Invalid"},
    {BackgroundCopyStatus::Pending, "Pending"},
    {BackgroundCopyStatus::InProgress, "InProgress"},
    {BackgroundCopyStatus::Completed, "Completed"},
    {BackgroundCopyStatus::Failed, "Failed"},
});

}
// clang-format on
