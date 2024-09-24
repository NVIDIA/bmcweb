#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_update_service
{
// clang-format off

enum class UpdateOptionSupport{
    Invalid,
    StageOnly,
    StageAndActivate,
};

NLOHMANN_JSON_SERIALIZE_ENUM(UpdateOptionSupport, {
    {UpdateOptionSupport::Invalid, "Invalid"},
    {UpdateOptionSupport::StageOnly, "StageOnly"},
    {UpdateOptionSupport::StageAndActivate, "StageAndActivate"},
});

}
// clang-format on
