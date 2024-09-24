#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_pcie_device
{
// clang-format off

enum class LTSSMState{
    Invalid,
    Detect,
    Polling,
    Configuration,
    Recovery,
    RecoveryEQ,
    L0,
    L0s,
    L1,
    L1_PLL_PD,
    L2,
    L1_CPM,
    L1_1,
    L1_2,
    HotReset,
    Loopback,
    Disabled,
    LinkDown,
    LinkReady,
    LanesInSleep,
    IllegalState,
};

NLOHMANN_JSON_SERIALIZE_ENUM(LTSSMState, {
    {LTSSMState::Invalid, "Invalid"},
    {LTSSMState::Detect, "Detect"},
    {LTSSMState::Polling, "Polling"},
    {LTSSMState::Configuration, "Configuration"},
    {LTSSMState::Recovery, "Recovery"},
    {LTSSMState::RecoveryEQ, "RecoveryEQ"},
    {LTSSMState::L0, "L0"},
    {LTSSMState::L0s, "L0s"},
    {LTSSMState::L1, "L1"},
    {LTSSMState::L1_PLL_PD, "L1_PLL_PD"},
    {LTSSMState::L2, "L2"},
    {LTSSMState::L1_CPM, "L1_CPM"},
    {LTSSMState::L1_1, "L1_1"},
    {LTSSMState::L1_2, "L1_2"},
    {LTSSMState::HotReset, "HotReset"},
    {LTSSMState::Loopback, "Loopback"},
    {LTSSMState::Disabled, "Disabled"},
    {LTSSMState::LinkDown, "LinkDown"},
    {LTSSMState::LinkReady, "LinkReady"},
    {LTSSMState::LanesInSleep, "LanesInSleep"},
    {LTSSMState::IllegalState, "IllegalState"},
});

}
// clang-format on
