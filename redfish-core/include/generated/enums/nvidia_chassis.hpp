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
    WDT_IACCVIOL,
    WDT_DACCVIOL,
    WDT_MUNSTKERR,
    WDT_MSTKERR,
    WDT_MMARVALID,
    WDT_BFARVALID,
    WDT_STKERR,
    WDT_UNSTKERR,
    WDT_IMPRECISEERR,
    WDT_PRECISERR,
    WDT_IBUSERR,
    WDT_UNDEFINSTR,
    WDT_INVSTATE,
    WDT_INVPC,
    WDT_NOCP,
    WDT_UNALIGNED,
    WDT_DIVBYZERO,
    WDT_VECTTBL,
    WDT_FORCED,
    WDT_DEBUGEVT,
    WDT_MCTP,
    WDT_I2C,
    WDT_I3C,
    WDT_PLDM,
    WDT_USB,
    WDT_Flash,
    WDT_Logger,
    WDT_SPDM,
};

enum class FailoverPolicy{
    Invalid,
    NoFailover,
    AutomaticFailover,
};

enum class EmbeddedProcessorOSState{
    Invalid,
    ResetBootROM,
    FWBootStage1,
    FWBootStage2,
    PreOS,
    OSBooting,
    OSRunning,
    OSQuiesced,
    FWUpdateInProgress,
    OSCrashDumpInProgress,
    OSCrashDumpCompleted,
    FWFaultInProgress,
    FWFaultCompleted,
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
    {LastResetReason::WDT_IACCVIOL, "WDT_IACCVIOL"},
    {LastResetReason::WDT_DACCVIOL, "WDT_DACCVIOL"},
    {LastResetReason::WDT_MUNSTKERR, "WDT_MUNSTKERR"},
    {LastResetReason::WDT_MSTKERR, "WDT_MSTKERR"},
    {LastResetReason::WDT_MMARVALID, "WDT_MMARVALID"},
    {LastResetReason::WDT_BFARVALID, "WDT_BFARVALID"},
    {LastResetReason::WDT_STKERR, "WDT_STKERR"},
    {LastResetReason::WDT_UNSTKERR, "WDT_UNSTKERR"},
    {LastResetReason::WDT_IMPRECISEERR, "WDT_IMPRECISEERR"},
    {LastResetReason::WDT_PRECISERR, "WDT_PRECISERR"},
    {LastResetReason::WDT_IBUSERR, "WDT_IBUSERR"},
    {LastResetReason::WDT_UNDEFINSTR, "WDT_UNDEFINSTR"},
    {LastResetReason::WDT_INVSTATE, "WDT_INVSTATE"},
    {LastResetReason::WDT_INVPC, "WDT_INVPC"},
    {LastResetReason::WDT_NOCP, "WDT_NOCP"},
    {LastResetReason::WDT_UNALIGNED, "WDT_UNALIGNED"},
    {LastResetReason::WDT_DIVBYZERO, "WDT_DIVBYZERO"},
    {LastResetReason::WDT_VECTTBL, "WDT_VECTTBL"},
    {LastResetReason::WDT_FORCED, "WDT_FORCED"},
    {LastResetReason::WDT_DEBUGEVT, "WDT_DEBUGEVT"},
    {LastResetReason::WDT_MCTP, "WDT_MCTP"},
    {LastResetReason::WDT_I2C, "WDT_I2C"},
    {LastResetReason::WDT_I3C, "WDT_I3C"},
    {LastResetReason::WDT_PLDM, "WDT_PLDM"},
    {LastResetReason::WDT_USB, "WDT_USB"},
    {LastResetReason::WDT_Flash, "WDT_Flash"},
    {LastResetReason::WDT_Logger, "WDT_Logger"},
    {LastResetReason::WDT_SPDM, "WDT_SPDM"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(FailoverPolicy, {
    {FailoverPolicy::Invalid, "Invalid"},
    {FailoverPolicy::NoFailover, "NoFailover"},
    {FailoverPolicy::AutomaticFailover, "AutomaticFailover"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(EmbeddedProcessorOSState, {
    {EmbeddedProcessorOSState::Invalid, "Invalid"},
    {EmbeddedProcessorOSState::ResetBootROM, "ResetBootROM"},
    {EmbeddedProcessorOSState::FWBootStage1, "FWBootStage1"},
    {EmbeddedProcessorOSState::FWBootStage2, "FWBootStage2"},
    {EmbeddedProcessorOSState::PreOS, "PreOS"},
    {EmbeddedProcessorOSState::OSBooting, "OSBooting"},
    {EmbeddedProcessorOSState::OSRunning, "OSRunning"},
    {EmbeddedProcessorOSState::OSQuiesced, "OSQuiesced"},
    {EmbeddedProcessorOSState::FWUpdateInProgress, "FWUpdateInProgress"},
    {EmbeddedProcessorOSState::OSCrashDumpInProgress, "OSCrashDumpInProgress"},
    {EmbeddedProcessorOSState::OSCrashDumpCompleted, "OSCrashDumpCompleted"},
    {EmbeddedProcessorOSState::FWFaultInProgress, "FWFaultInProgress"},
    {EmbeddedProcessorOSState::FWFaultCompleted, "FWFaultCompleted"},
});

// clang-format on
} // namespace nvidia_chassis
