// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
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

enum class LinkDownReasonCodeTypes{
    Invalid,
    NoLinkDown,
    Unknown,
    HighBitErrorRate,
    BlockLockLost,
    AlignmentLost,
    FECSyncLost,
    PllLockLost,
    FIFOOverflow,
    FalseSkipDetected,
    MinorErrorThresholdExceeded,
    PhyRetransmitTimeout,
    HeartbeatErrors,
    CreditMonitorWatchdogTimeout,
    LinkLayerIntegrityThresholdExceeded,
    LinkLayerBufferOverrun,
    OOBCommandLinkHealthy,
    OOBCommandLinkHighBER,
    InbandCommandLinkHealthy,
    InbandCommandLinkHighBER,
    VerificationGatewayDown,
    RemoteFaultReceived,
    TrainingSequenceReceived,
    ManagementCommandDown,
    CableDisconnected,
    CableAccessFault,
    ThermalShutdown,
    CurrentIssue,
    PowerBudgetExceeded,
    FastRawBERRecovery,
    FastEffectiveBERRecovery,
    FastSymbolBERRecovery,
    FastCreditWatchdogRecovery,
    PeerSleep,
    PeerDisabled,
    PeerDisableLocked,
    PeerThermalEvent,
    PeerForcedEvent,
    PeerResetEvent,
};

enum class EarlyHealthIndication{
    Invalid,
    Healthy,
    Attention,
    Unknown,
};

enum class AttentionTriggerReason{
    Invalid,
    Unknown,
    RawBER,
    EffectiveBER,
    SymbolBER,
    PLRTXBandwidthLoss,
    PLRRXBandwidthLoss,
    RecoveryBandwidthLoss,
    PortTotalBandwidthLoss,
    LinkDownCount,
    SymbolErrorCount,
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

NLOHMANN_JSON_SERIALIZE_ENUM(LinkDownReasonCodeTypes, {
    {LinkDownReasonCodeTypes::Invalid, "Invalid"},
    {LinkDownReasonCodeTypes::NoLinkDown, "NoLinkDown"},
    {LinkDownReasonCodeTypes::Unknown, "Unknown"},
    {LinkDownReasonCodeTypes::HighBitErrorRate, "HighBitErrorRate"},
    {LinkDownReasonCodeTypes::BlockLockLost, "BlockLockLost"},
    {LinkDownReasonCodeTypes::AlignmentLost, "AlignmentLost"},
    {LinkDownReasonCodeTypes::FECSyncLost, "FECSyncLost"},
    {LinkDownReasonCodeTypes::PllLockLost, "PllLockLost"},
    {LinkDownReasonCodeTypes::FIFOOverflow, "FIFOOverflow"},
    {LinkDownReasonCodeTypes::FalseSkipDetected, "FalseSkipDetected"},
    {LinkDownReasonCodeTypes::MinorErrorThresholdExceeded, "MinorErrorThresholdExceeded"},
    {LinkDownReasonCodeTypes::PhyRetransmitTimeout, "PhyRetransmitTimeout"},
    {LinkDownReasonCodeTypes::HeartbeatErrors, "HeartbeatErrors"},
    {LinkDownReasonCodeTypes::CreditMonitorWatchdogTimeout, "CreditMonitorWatchdogTimeout"},
    {LinkDownReasonCodeTypes::LinkLayerIntegrityThresholdExceeded, "LinkLayerIntegrityThresholdExceeded"},
    {LinkDownReasonCodeTypes::LinkLayerBufferOverrun, "LinkLayerBufferOverrun"},
    {LinkDownReasonCodeTypes::OOBCommandLinkHealthy, "OOBCommandLinkHealthy"},
    {LinkDownReasonCodeTypes::OOBCommandLinkHighBER, "OOBCommandLinkHighBER"},
    {LinkDownReasonCodeTypes::InbandCommandLinkHealthy, "InbandCommandLinkHealthy"},
    {LinkDownReasonCodeTypes::InbandCommandLinkHighBER, "InbandCommandLinkHighBER"},
    {LinkDownReasonCodeTypes::VerificationGatewayDown, "VerificationGatewayDown"},
    {LinkDownReasonCodeTypes::RemoteFaultReceived, "RemoteFaultReceived"},
    {LinkDownReasonCodeTypes::TrainingSequenceReceived, "TrainingSequenceReceived"},
    {LinkDownReasonCodeTypes::ManagementCommandDown, "ManagementCommandDown"},
    {LinkDownReasonCodeTypes::CableDisconnected, "CableDisconnected"},
    {LinkDownReasonCodeTypes::CableAccessFault, "CableAccessFault"},
    {LinkDownReasonCodeTypes::ThermalShutdown, "ThermalShutdown"},
    {LinkDownReasonCodeTypes::CurrentIssue, "CurrentIssue"},
    {LinkDownReasonCodeTypes::PowerBudgetExceeded, "PowerBudgetExceeded"},
    {LinkDownReasonCodeTypes::FastRawBERRecovery, "FastRawBERRecovery"},
    {LinkDownReasonCodeTypes::FastEffectiveBERRecovery, "FastEffectiveBERRecovery"},
    {LinkDownReasonCodeTypes::FastSymbolBERRecovery, "FastSymbolBERRecovery"},
    {LinkDownReasonCodeTypes::FastCreditWatchdogRecovery, "FastCreditWatchdogRecovery"},
    {LinkDownReasonCodeTypes::PeerSleep, "PeerSleep"},
    {LinkDownReasonCodeTypes::PeerDisabled, "PeerDisabled"},
    {LinkDownReasonCodeTypes::PeerDisableLocked, "PeerDisableLocked"},
    {LinkDownReasonCodeTypes::PeerThermalEvent, "PeerThermalEvent"},
    {LinkDownReasonCodeTypes::PeerForcedEvent, "PeerForcedEvent"},
    {LinkDownReasonCodeTypes::PeerResetEvent, "PeerResetEvent"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(EarlyHealthIndication, {
    {EarlyHealthIndication::Invalid, "Invalid"},
    {EarlyHealthIndication::Healthy, "Healthy"},
    {EarlyHealthIndication::Attention, "Attention"},
    {EarlyHealthIndication::Unknown, "Unknown"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(AttentionTriggerReason, {
    {AttentionTriggerReason::Invalid, "Invalid"},
    {AttentionTriggerReason::Unknown, "Unknown"},
    {AttentionTriggerReason::RawBER, "RawBER"},
    {AttentionTriggerReason::EffectiveBER, "EffectiveBER"},
    {AttentionTriggerReason::SymbolBER, "SymbolBER"},
    {AttentionTriggerReason::PLRTXBandwidthLoss, "PLRTXBandwidthLoss"},
    {AttentionTriggerReason::PLRRXBandwidthLoss, "PLRRXBandwidthLoss"},
    {AttentionTriggerReason::RecoveryBandwidthLoss, "RecoveryBandwidthLoss"},
    {AttentionTriggerReason::PortTotalBandwidthLoss, "PortTotalBandwidthLoss"},
    {AttentionTriggerReason::LinkDownCount, "LinkDownCount"},
    {AttentionTriggerReason::SymbolErrorCount, "SymbolErrorCount"},
});

// clang-format on
} // namespace nvidia_port_metrics
