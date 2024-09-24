#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_ro_t_chassis
{
// clang-format off

enum class RequestType{
    Invalid,
    Enable,
    Disable,
};

NLOHMANN_JSON_SERIALIZE_ENUM(RequestType, {
    {RequestType::Invalid, "Invalid"},
    {RequestType::Enable, "Enable"},
    {RequestType::Disable, "Disable"},
});

}
// clang-format on
