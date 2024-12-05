#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_processor
{
// clang-format off

enum class MLNVLPeerType{
    Invalid,
    Bridge,
    Direct,
};

NLOHMANN_JSON_SERIALIZE_ENUM(MLNVLPeerType, {
    {MLNVLPeerType::Invalid, "Invalid"},
    {MLNVLPeerType::Bridge, "Bridge"},
    {MLNVLPeerType::Direct, "Direct"},
});

}
// clang-format on
