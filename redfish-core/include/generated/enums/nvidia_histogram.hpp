#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_histogram
{
// clang-format off

enum class BucketUnits{
    Invalid,
    Watts,
    Counts,
};

NLOHMANN_JSON_SERIALIZE_ENUM(BucketUnits, {
    {BucketUnits::Invalid, "Invalid"},
    {BucketUnits::Watts, "Watts"},
    {BucketUnits::Counts, "Counts"},
});

}
// clang-format on
