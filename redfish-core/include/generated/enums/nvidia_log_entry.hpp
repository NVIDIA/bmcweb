// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_log_entry
{
// clang-format off

enum class ISTType{
    Invalid,
    CPU,
};

enum class ISTStage{
    Invalid,
    Idle,
    CollateralVerification,
    PendingISTBoot,
    PendingPowerCycle,
    RunningIST,
    Cleanup,
};

enum class ISTResult{
    Invalid,
    Failed,
    Aborted,
    Error,
};

NLOHMANN_JSON_SERIALIZE_ENUM(ISTType, {
    {ISTType::Invalid, "Invalid"},
    {ISTType::CPU, "CPU"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(ISTStage, {
    {ISTStage::Invalid, "Invalid"},
    {ISTStage::Idle, "Idle"},
    {ISTStage::CollateralVerification, "CollateralVerification"},
    {ISTStage::PendingISTBoot, "PendingISTBoot"},
    {ISTStage::PendingPowerCycle, "PendingPowerCycle"},
    {ISTStage::RunningIST, "RunningIST"},
    {ISTStage::Cleanup, "Cleanup"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(ISTResult, {
    {ISTResult::Invalid, "Invalid"},
    {ISTResult::Failed, "Failed"},
    {ISTResult::Aborted, "Aborted"},
    {ISTResult::Error, "Error"},
});

}
// clang-format on
