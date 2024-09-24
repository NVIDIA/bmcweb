#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_computer_system
{
// clang-format off

enum class CapabilityStatus{
    Invalid,
    Enabled,
    Disabled,
    Offline,
    Unknown,
};

NLOHMANN_JSON_SERIALIZE_ENUM(CapabilityStatus, {
    {CapabilityStatus::Invalid, "Invalid"},
    {CapabilityStatus::Enabled, "Enabled"},
    {CapabilityStatus::Disabled, "Disabled"},
    {CapabilityStatus::Offline, "Offline"},
    {CapabilityStatus::Unknown, "Unknown"},
});

}
// clang-format on
