#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_debug_token
{
// clang-format off

enum class TokenType{
    Invalid,
    FRC,
    CRCS,
    CRDT,
    DebugFirmwareRunning,
};

enum class TokenStatus{
    Invalid,
    Failed,
    DebugSessionEnded,
    DebugSessionActive,
    NoTokenApplied,
    ChallengeProvidedNoTokenInstalled,
    TimeoutBeforeTokenInstalled,
    ActiveTokenTimeout,
};

enum class AdditionalInfo{
    Invalid,
    None,
    NoDebugSessionInProgress,
    InsecureFirmware,
    DebugEndRequestFailed,
    QueryDebugSessionFailed,
    DebugSessionActive,
};

NLOHMANN_JSON_SERIALIZE_ENUM(TokenType, {
    {TokenType::Invalid, "Invalid"},
    {TokenType::FRC, "FRC"},
    {TokenType::CRCS, "CRCS"},
    {TokenType::CRDT, "CRDT"},
    {TokenType::DebugFirmwareRunning, "DebugFirmwareRunning"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(TokenStatus, {
    {TokenStatus::Invalid, "Invalid"},
    {TokenStatus::Failed, "Failed"},
    {TokenStatus::DebugSessionEnded, "DebugSessionEnded"},
    {TokenStatus::DebugSessionActive, "DebugSessionActive"},
    {TokenStatus::NoTokenApplied, "NoTokenApplied"},
    {TokenStatus::ChallengeProvidedNoTokenInstalled, "ChallengeProvidedNoTokenInstalled"},
    {TokenStatus::TimeoutBeforeTokenInstalled, "TimeoutBeforeTokenInstalled"},
    {TokenStatus::ActiveTokenTimeout, "ActiveTokenTimeout"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(AdditionalInfo, {
    {AdditionalInfo::Invalid, "Invalid"},
    {AdditionalInfo::None, "None"},
    {AdditionalInfo::NoDebugSessionInProgress, "NoDebugSessionInProgress"},
    {AdditionalInfo::InsecureFirmware, "InsecureFirmware"},
    {AdditionalInfo::DebugEndRequestFailed, "DebugEndRequestFailed"},
    {AdditionalInfo::QueryDebugSessionFailed, "QueryDebugSessionFailed"},
    {AdditionalInfo::DebugSessionActive, "DebugSessionActive"},
});

}
// clang-format on
