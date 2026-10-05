// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "utils/nvidia_sensor_utils.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace redfish::nvidia_sensor_utils
{
namespace
{

using namespace std::chrono_literals;

TEST(TtlCache, ReturnsValuesUntilTheyExpire)
{
    details::TtlCache<int> cache(60s);
    const details::TtlCache<int>::TimePoint start{};

    cache.insert("sensor", 42, start);

    EXPECT_EQ(cache.get("sensor", start), 42);
    EXPECT_EQ(cache.get("sensor", start + 59s), 42);
    EXPECT_FALSE(cache.get("sensor", start + 60s));
}

TEST(TtlCache, UpdatingAValueRefreshesItsLifetime)
{
    details::TtlCache<std::string> cache(60s);
    const details::TtlCache<std::string>::TimePoint start{};

    cache.insert("sensor", "old", start);
    cache.insert("sensor", "new", start + 30s);

    EXPECT_EQ(cache.get("sensor", start + 89s), "new");
    EXPECT_FALSE(cache.get("sensor", start + 90s));
}

TEST(TtlCache, EraseIfChecksOnlyRequestedKey)
{
    details::TtlCache<int> cache(60s);
    const details::TtlCache<int>::TimePoint start{};

    cache.insert("first", 1, start);
    cache.insert("second", 2, start);
    cache.eraseIf("first", [](int value) { return value == 1; });
    cache.eraseIf("second", [](int value) { return value == 1; });

    EXPECT_FALSE(cache.get("first", start));
    EXPECT_EQ(cache.get("second", start), 2);
}

TEST(SensorPathCacheKey, IncludesChassisAndSensorBoundaries)
{
    EXPECT_NE(makeSensorPathCacheKey("chassis-a", "sensor-b"),
              makeSensorPathCacheKey("chassis", "a-sensor-b"));
}

TEST(SensorPathCache, InvalidatesOnlyFailedPathAndService)
{
    details::TtlCache<SensorPathCacheValue>& cache = getSensorPathCache();
    cache.clear();
    cache.insert("failed", {"failed.service", "/sensors/sensor0", {}});
    cache.insert("new-owner", {"new.service", "/sensors/sensor0", {}});
    cache.insert("other-path", {"failed.service", "/sensors/sensor1", {}});

    invalidateSensorPathCache("failed", "/sensors/sensor0", "failed.service");

    EXPECT_FALSE(cache.get("failed"));
    EXPECT_TRUE(cache.get("new-owner"));
    EXPECT_TRUE(cache.get("other-path"));
    cache.clear();
}

TEST(AddFabricSwitchRelatedItems, AddsEveryFabricSwitchCombination)
{
    nlohmann::json items = nlohmann::json::array();
    const std::vector<std::string> fabricPaths = {
        "/xyz/openbmc_project/inventory/fabrics/HGX",
        "/xyz/openbmc_project/inventory/fabrics/NVLink"};
    const std::vector<std::string> switchPaths = {
        "/xyz/openbmc_project/inventory/switches/Switch0",
        "/xyz/openbmc_project/inventory/switches/Switch1"};

    addFabricSwitchRelatedItems(items, fabricPaths, switchPaths);

    EXPECT_EQ(items,
              nlohmann::json::array(
                  {{{"@odata.id",
                     "/redfish/v1/Fabrics/HGX/Switches/Switch0"}},
                   {{"@odata.id",
                     "/redfish/v1/Fabrics/HGX/Switches/Switch1"}},
                   {{"@odata.id",
                     "/redfish/v1/Fabrics/NVLink/Switches/Switch0"}},
                   {{"@odata.id",
                     "/redfish/v1/Fabrics/NVLink/Switches/Switch1"}}}));
}

TEST(AddFabricSwitchRelatedItems, EmptyInputDoesNotChangeRelatedItems)
{
    nlohmann::json items =
        nlohmann::json::array({{{"@odata.id", "/redfish/v1/existing"}}});

    addFabricSwitchRelatedItems(items, {}, {});

    EXPECT_EQ(items,
              nlohmann::json::array(
                  {{{"@odata.id", "/redfish/v1/existing"}}}));
}

} // namespace
} // namespace redfish::nvidia_sensor_utils
