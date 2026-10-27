// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_host_recovery
{
// clang-format off

enum class RecoveryState{
    Invalid,
    Unknown,
    Requested,
    Confirmed,
    EnterFailed,
    ExitRequested,
    ExitObserved,
    PoweredOff,
};

enum class LastCheckStatus{
    Invalid,
    Unknown,
    NeverRun,
    Detected,
    NotDetected,
    TimedOut,
    RouteFailed,
    EnumerationFailed,
    RestoreFailed,
};

enum class OperationState{
    Invalid,
    Unknown,
    Idle,
    EnteringRecovery,
    Resetting,
    Checking,
    RestoringUsb,
    Abandoning,
};

NLOHMANN_JSON_SERIALIZE_ENUM(RecoveryState, {
    {RecoveryState::Invalid, "Invalid"},
    {RecoveryState::Unknown, "Unknown"},
    {RecoveryState::Requested, "Requested"},
    {RecoveryState::Confirmed, "Confirmed"},
    {RecoveryState::EnterFailed, "EnterFailed"},
    {RecoveryState::ExitRequested, "ExitRequested"},
    {RecoveryState::ExitObserved, "ExitObserved"},
    {RecoveryState::PoweredOff, "PoweredOff"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(LastCheckStatus, {
    {LastCheckStatus::Invalid, "Invalid"},
    {LastCheckStatus::Unknown, "Unknown"},
    {LastCheckStatus::NeverRun, "NeverRun"},
    {LastCheckStatus::Detected, "Detected"},
    {LastCheckStatus::NotDetected, "NotDetected"},
    {LastCheckStatus::TimedOut, "TimedOut"},
    {LastCheckStatus::RouteFailed, "RouteFailed"},
    {LastCheckStatus::EnumerationFailed, "EnumerationFailed"},
    {LastCheckStatus::RestoreFailed, "RestoreFailed"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(OperationState, {
    {OperationState::Invalid, "Invalid"},
    {OperationState::Unknown, "Unknown"},
    {OperationState::Idle, "Idle"},
    {OperationState::EnteringRecovery, "EnteringRecovery"},
    {OperationState::Resetting, "Resetting"},
    {OperationState::Checking, "Checking"},
    {OperationState::RestoringUsb, "RestoringUsb"},
    {OperationState::Abandoning, "Abandoning"},
});

// clang-format on
} // namespace nvidia_host_recovery
