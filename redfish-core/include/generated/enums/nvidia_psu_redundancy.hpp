// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include <nlohmann/json.hpp>

namespace nvidia_psu_redundancy
{
// clang-format off

enum class RedundancyType{
    Invalid,
    NoRedundancy,
    NPlusOne,
    NPlusN,
};

NLOHMANN_JSON_SERIALIZE_ENUM(RedundancyType, {
    {RedundancyType::Invalid, "Invalid"},
    {RedundancyType::NoRedundancy, "NoRedundancy"},
    {RedundancyType::NPlusOne, "NPlusOne"},
    {RedundancyType::NPlusN, "NPlusN"},
});

}
// clang-format on
