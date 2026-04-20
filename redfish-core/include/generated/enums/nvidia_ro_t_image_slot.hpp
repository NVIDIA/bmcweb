// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_ro_t_image_slot
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
    ImageCopyPending,
    ImageCopyInProgress,
    ImageCopyFailed,
};

enum class SigningType{
    Invalid,
    Debug,
    Production,
    External,
    DOT,
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
    {FirmwareState::ImageCopyPending, "ImageCopyPending"},
    {FirmwareState::ImageCopyInProgress, "ImageCopyInProgress"},
    {FirmwareState::ImageCopyFailed, "ImageCopyFailed"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(SigningType, {
    {SigningType::Invalid, "Invalid"},
    {SigningType::Debug, "Debug"},
    {SigningType::Production, "Production"},
    {SigningType::External, "External"},
    {SigningType::DOT, "DOT"},
});

// clang-format on
} // namespace nvidia_ro_t_image_slot
