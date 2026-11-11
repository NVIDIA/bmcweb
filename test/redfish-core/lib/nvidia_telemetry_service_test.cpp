/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 */
#include "nvidia_telemetry_service.hpp"

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

namespace redfish::nvidia
{
namespace
{

nlohmann::json telemetryServiceJson()
{
    nlohmann::json json;
    json["Id"] = "TelemetryService";
    json["MaxReports"] = 10;
    json["SupportedCollectionFunctions"] =
        nlohmann::json::array({"Maximum", "Minimum", "Average", "Summation"});
    return json;
}

TEST(NvidiaTelemetryServiceGet, KeepsListWithHostOsFeatures)
{
    nlohmann::json json = telemetryServiceJson();

    nvidiaTelemetryServiceGet(json, true);

    EXPECT_EQ(json, telemetryServiceJson());
}

TEST(NvidiaTelemetryServiceGet, RemovesOnlyListWithoutHostOsFeatures)
{
    nlohmann::json json = telemetryServiceJson();

    nvidiaTelemetryServiceGet(json, false);

    nlohmann::json expected = telemetryServiceJson();
    expected.erase("SupportedCollectionFunctions");
    EXPECT_EQ(json, expected);
}

TEST(NvidiaTelemetryServiceGet, NoListNoChange)
{
    nlohmann::json json = nlohmann::json::object();

    nvidiaTelemetryServiceGet(json, false);

    EXPECT_TRUE(json.empty());
}

} // namespace
} // namespace redfish::nvidia
