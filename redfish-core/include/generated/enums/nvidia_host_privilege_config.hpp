// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_host_privilege_config
{
// clang-format off

enum class NicAttributeValue{
    Invalid,
    Enabled,
    Disabled,
};

enum class TristateValue{
    Invalid,
    Default,
    Enabled,
    Disabled,
};

enum class PrivilegeModeType{
    Invalid,
    Privileged,
    Restricted,
    Custom,
};

enum class HostPrivilegeLevelInput{
    Invalid,
    Privileged,
    Restricted,
};

NLOHMANN_JSON_SERIALIZE_ENUM(NicAttributeValue, {
    {NicAttributeValue::Invalid, "Invalid"},
    {NicAttributeValue::Enabled, "Enabled"},
    {NicAttributeValue::Disabled, "Disabled"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(TristateValue, {
    {TristateValue::Invalid, "Invalid"},
    {TristateValue::Default, "Default"},
    {TristateValue::Enabled, "Enabled"},
    {TristateValue::Disabled, "Disabled"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(PrivilegeModeType, {
    {PrivilegeModeType::Invalid, "Invalid"},
    {PrivilegeModeType::Privileged, "Privileged"},
    {PrivilegeModeType::Restricted, "Restricted"},
    {PrivilegeModeType::Custom, "Custom"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(HostPrivilegeLevelInput, {
    {HostPrivilegeLevelInput::Invalid, "Invalid"},
    {HostPrivilegeLevelInput::Privileged, "Privileged"},
    {HostPrivilegeLevelInput::Restricted, "Restricted"},
});

}
// clang-format on
