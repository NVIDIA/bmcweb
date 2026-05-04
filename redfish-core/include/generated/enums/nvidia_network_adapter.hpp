// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_network_adapter
{
// clang-format off

enum class ProtectionOption{
    Invalid,
    NoProtection,
    PreventAll,
    PreventHostFirmwareUpdates,
    PreventHostConfigurations,
};

enum class DPUOperationMode{
    Invalid,
    DPU,
    NIC,
};

enum class LLDPModeType{
    Invalid,
    Off,
    Mandatory,
    All,
};

NLOHMANN_JSON_SERIALIZE_ENUM(ProtectionOption, {
    {ProtectionOption::Invalid, "Invalid"},
    {ProtectionOption::NoProtection, "NoProtection"},
    {ProtectionOption::PreventAll, "PreventAll"},
    {ProtectionOption::PreventHostFirmwareUpdates, "PreventHostFirmwareUpdates"},
    {ProtectionOption::PreventHostConfigurations, "PreventHostConfigurations"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(DPUOperationMode, {
    {DPUOperationMode::Invalid, "Invalid"},
    {DPUOperationMode::DPU, "DPU"},
    {DPUOperationMode::NIC, "NIC"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(LLDPModeType, {
    {LLDPModeType::Invalid, "Invalid"},
    {LLDPModeType::Off, "Off"},
    {LLDPModeType::Mandatory, "Mandatory"},
    {LLDPModeType::All, "All"},
});

// clang-format on
} // namespace nvidia_network_adapter
