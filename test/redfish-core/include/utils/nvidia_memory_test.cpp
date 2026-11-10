// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
// AFFILIATES. All rights reserved.

#include "async_resp.hpp"
#include "utils/nvidia_memory.hpp"

#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>

#include <gtest/gtest.h>

namespace
{

constexpr const char* normal =
    "com.nvidia.MemoryPerformance.PerformanceStates.Normal";
constexpr const char* throttled =
    "com.nvidia.MemoryPerformance.PerformanceStates.Throttled";
constexpr const char* degraded =
    "com.nvidia.MemoryPerformance.PerformanceStates.Degraded";
constexpr const char* unknown =
    "com.nvidia.MemoryPerformance.PerformanceStates.Unknown";

bool hasPerformanceDegraded(const nlohmann::json& json)
{
    return json.contains("HealthData") &&
           json["HealthData"].contains("PerformanceDegraded");
}

TEST(PopulatePerformanceData, normalIsNotDegraded)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    populatePerformanceData(resp, {}, normal);
    EXPECT_EQ(resp->res.jsonValue["HealthData"]["PerformanceDegraded"], false);
}

TEST(PopulatePerformanceData, throttledAndDegradedAreDegraded)
{
    for (const char* state : {throttled, degraded})
    {
        auto resp = std::make_shared<bmcweb::AsyncResp>();
        populatePerformanceData(resp, {}, state);
        EXPECT_EQ(resp->res.jsonValue["HealthData"]["PerformanceDegraded"],
                  true);
    }
}

TEST(PopulatePerformanceData, unknownIsOmitted)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    populatePerformanceData(resp, {}, unknown);
    EXPECT_FALSE(hasPerformanceDegraded(resp->res.jsonValue));
}

TEST(PopulatePerformanceData, unrecognizedIsOmitted)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    populatePerformanceData(resp, {}, "");
    populatePerformanceData(resp, {}, "com.nvidia.Bogus");
    EXPECT_FALSE(hasPerformanceDegraded(resp->res.jsonValue));
}

TEST(PopulatePerformanceData, dbusErrorIsOmitted)
{
    auto resp = std::make_shared<bmcweb::AsyncResp>();
    populatePerformanceData(
        resp,
        boost::system::errc::make_error_code(boost::system::errc::io_error),
        throttled);
    EXPECT_FALSE(hasPerformanceDegraded(resp->res.jsonValue));
}

} // namespace
