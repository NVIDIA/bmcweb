#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_port_metrics
{
// clang-format off

enum class PCIeCounterType{
    Invalid,
    CorrectableErrorCount,
    FatalErrorCount,
    L0ToRecoveryCount,
    NAKReceivedCount,
    NAKSentCount,
    NonFatalErrorCount,
    ReplayCount,
    ReplayRolloverCount,
    UnsupportedRequestCount,
};

NLOHMANN_JSON_SERIALIZE_ENUM(PCIeCounterType, {
    {PCIeCounterType::Invalid, "Invalid"},
    {PCIeCounterType::CorrectableErrorCount, "CorrectableErrorCount"},
    {PCIeCounterType::FatalErrorCount, "FatalErrorCount"},
    {PCIeCounterType::L0ToRecoveryCount, "L0ToRecoveryCount"},
    {PCIeCounterType::NAKReceivedCount, "NAKReceivedCount"},
    {PCIeCounterType::NAKSentCount, "NAKSentCount"},
    {PCIeCounterType::NonFatalErrorCount, "NonFatalErrorCount"},
    {PCIeCounterType::ReplayCount, "ReplayCount"},
    {PCIeCounterType::ReplayRolloverCount, "ReplayRolloverCount"},
    {PCIeCounterType::UnsupportedRequestCount, "UnsupportedRequestCount"},
});

}
// clang-format on
