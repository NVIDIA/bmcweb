// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_histogram
{
// clang-format off

enum class BucketUnits{
    Invalid,
    Watts,
    Counts,
    HundredthsPercent,
};

enum class BucketFormat{
    Invalid,
    NvU8,
    NvS8,
    NvU16,
    NvS16,
    NvU32,
    NvS32,
    NvU64,
    NvS64,
};

NLOHMANN_JSON_SERIALIZE_ENUM(BucketUnits, {
    {BucketUnits::Invalid, "Invalid"},
    {BucketUnits::Watts, "Watts"},
    {BucketUnits::Counts, "Counts"},
    {BucketUnits::HundredthsPercent, "HundredthsPercent"},
});

NLOHMANN_JSON_SERIALIZE_ENUM(BucketFormat, {
    {BucketFormat::Invalid, "Invalid"},
    {BucketFormat::NvU8, "NvU8"},
    {BucketFormat::NvS8, "NvS8"},
    {BucketFormat::NvU16, "NvU16"},
    {BucketFormat::NvS16, "NvS16"},
    {BucketFormat::NvU32, "NvU32"},
    {BucketFormat::NvS32, "NvS32"},
    {BucketFormat::NvU64, "NvU64"},
    {BucketFormat::NvS64, "NvS64"},
});

// clang-format on
} // namespace nvidia_histogram
