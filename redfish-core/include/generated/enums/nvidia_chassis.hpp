// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
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
    Failed,
};

enum class StaticPowerHintOutputState{
    Completed,
    InProgress,
    Failed,
    InvalidArgument,
    Invalid,
};

enum class AuxPowerResetType{
    Invalid,
    AuxPowerCycle,
};

enum class ColorType{
    Invalid,
    Green,
    Amber,
};

enum class LastResetReason{
    Invalid,
    WakeUp,
    PowerOn,
    VoltageDetect,
    WarmReset,
    FatalError,
    Pin,
    DebugAccessPort,
    ResetTimeout,
    LowPowerAcknowledgeTimeout,
    SystemClockGenerator,
    WindowedWatchdog0,
    Software,
    LockupReset,
    CPU1,
    VBAT,
    WindowedWatchdog1,
    CodeWatchdog0,
    CodeWatchdog1,
    JTAG,
    SecurityViolation,
    Tamper,
};

NLOHMANN_JSON_SERIALIZE_ENUM(BackgroundCopyStatus, {
    {BackgroundCopyStatus::Invalid, "Invalid"},
    {BackgroundCopyStatus::Pending, "Pending"},
    {BackgroundCopyStatus::InProgress, "InProgress"},
    {BackgroundCopyStatus::Completed, "Completed"},
    {BackgroundCopyStatus::Failed, "Failed"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(StaticPowerHintOutputState, {
    {StaticPowerHintOutputState::Completed, "Completed"},
    {StaticPowerHintOutputState::InProgress, "InProgress"},
    {StaticPowerHintOutputState::Failed, "Failed"},
    {StaticPowerHintOutputState::InvalidArgument, "InvalidArgument"},
    {StaticPowerHintOutputState::Invalid, "Invalid"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(AuxPowerResetType, {
    {AuxPowerResetType::Invalid, "Invalid"},
    {AuxPowerResetType::AuxPowerCycle, "AuxPowerCycle"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(ColorType, {
    {ColorType::Invalid, "Invalid"},
    {ColorType::Green, "Green"},
    {ColorType::Amber, "Amber"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(LastResetReason, {
    {LastResetReason::Invalid, "Invalid"},
    {LastResetReason::WakeUp, "WakeUp"},
    {LastResetReason::PowerOn, "PowerOn"},
    {LastResetReason::VoltageDetect, "VoltageDetect"},
    {LastResetReason::WarmReset, "WarmReset"},
    {LastResetReason::FatalError, "FatalError"},
    {LastResetReason::Pin, "Pin"},
    {LastResetReason::DebugAccessPort, "DebugAccessPort"},
    {LastResetReason::ResetTimeout, "ResetTimeout"},
    {LastResetReason::LowPowerAcknowledgeTimeout, "LowPowerAcknowledgeTimeout"},
    {LastResetReason::SystemClockGenerator, "SystemClockGenerator"},
    {LastResetReason::WindowedWatchdog0, "WindowedWatchdog0"},
    {LastResetReason::Software, "Software"},
    {LastResetReason::LockupReset, "LockupReset"},
    {LastResetReason::CPU1, "CPU1"},
    {LastResetReason::VBAT, "VBAT"},
    {LastResetReason::WindowedWatchdog1, "WindowedWatchdog1"},
    {LastResetReason::CodeWatchdog0, "CodeWatchdog0"},
    {LastResetReason::CodeWatchdog1, "CodeWatchdog1"},
    {LastResetReason::JTAG, "JTAG"},
    {LastResetReason::SecurityViolation, "SecurityViolation"},
    {LastResetReason::Tamper, "Tamper"},
});

}
// clang-format on
