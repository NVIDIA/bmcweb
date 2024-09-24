#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_software_inventory
{
// clang-format off

enum class BuildType{
    Invalid,
    Release,
    Development,
};

enum class FirmwareState{
    Invalid,
    Unknown,
    Activated,
    PendingActivation,
    Staged,
    WriteInProgress,
    Inactive,
    FailedAuthentication,
};

NLOHMANN_JSON_SERIALIZE_ENUM(BuildType, {
    {BuildType::Invalid, "Invalid"},
    {BuildType::Release, "Release"},
    {BuildType::Development, "Development"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(FirmwareState, {
    {FirmwareState::Invalid, "Invalid"},
    {FirmwareState::Unknown, "Unknown"},
    {FirmwareState::Activated, "Activated"},
    {FirmwareState::PendingActivation, "PendingActivation"},
    {FirmwareState::Staged, "Staged"},
    {FirmwareState::WriteInProgress, "WriteInProgress"},
    {FirmwareState::Inactive, "Inactive"},
    {FirmwareState::FailedAuthentication, "FailedAuthentication"},
});

}
// clang-format on
