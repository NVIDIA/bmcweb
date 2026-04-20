// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_pcie_equalization
{
// clang-format off

enum class PresetConfiguration{
    Invalid,
    DeviceDefault,
    Preset0,
    Preset1,
    Preset2,
    Preset3,
    Preset4,
    Preset5,
    Preset6,
    Preset7,
    Preset8,
};

NLOHMANN_JSON_SERIALIZE_ENUM(PresetConfiguration, {
    {PresetConfiguration::Invalid, "Invalid"},
    {PresetConfiguration::DeviceDefault, "DeviceDefault"},
    {PresetConfiguration::Preset0, "Preset0"},
    {PresetConfiguration::Preset1, "Preset1"},
    {PresetConfiguration::Preset2, "Preset2"},
    {PresetConfiguration::Preset3, "Preset3"},
    {PresetConfiguration::Preset4, "Preset4"},
    {PresetConfiguration::Preset5, "Preset5"},
    {PresetConfiguration::Preset6, "Preset6"},
    {PresetConfiguration::Preset7, "Preset7"},
    {PresetConfiguration::Preset8, "Preset8"},
});

// clang-format on
} // namespace nvidia_pcie_equalization
