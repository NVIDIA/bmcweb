// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "telemetry_service.hpp"

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

TEST(TelemetryServiceTest, SupportedCollectionFunctionsListedWithHostOs)
{
    nlohmann::json json;

    addSupportedCollectionFunctions(json, true);

    EXPECT_EQ(json.size(), 1);
    EXPECT_EQ(
        json["SupportedCollectionFunctions"],
        nlohmann::json::array({"Maximum", "Minimum", "Average", "Summation"}));
}

TEST(TelemetryServiceTest, SupportedCollectionFunctionsOmittedWithoutHostOs)
{
    nlohmann::json json = nlohmann::json::object();

    addSupportedCollectionFunctions(json, false);

    EXPECT_FALSE(json.contains("SupportedCollectionFunctions"));
    EXPECT_TRUE(json.empty());
}

} // namespace
} // namespace redfish
