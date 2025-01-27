#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_psc_state
{
// clang-format off

enum class StatusType{
    Invalid,
    Configured,
    NotResponding,
    Operational,
    Unknown,
};

NLOHMANN_JSON_SERIALIZE_ENUM(StatusType, {
    {StatusType::Invalid, "Invalid"},
    {StatusType::Configured, "Configured"},
    {StatusType::NotResponding, "NotResponding"},
    {StatusType::Operational, "Operational"},
    {StatusType::Unknown, "Unknown"},
});

}
// clang-format on
