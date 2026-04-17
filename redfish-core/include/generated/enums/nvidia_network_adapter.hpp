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

}
// clang-format on
