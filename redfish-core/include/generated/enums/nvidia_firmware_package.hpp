#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_firmware_package
{
// clang-format off

enum class VerificationStatus{
    Invalid,
    Success,
    Failed,
};

NLOHMANN_JSON_SERIALIZE_ENUM(VerificationStatus, {
    {VerificationStatus::Invalid, "Invalid"},
    {VerificationStatus::Success, "Success"},
    {VerificationStatus::Failed, "Failed"},
});

}
// clang-format on
