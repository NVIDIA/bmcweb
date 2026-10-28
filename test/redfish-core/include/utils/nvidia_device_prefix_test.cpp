/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "dbus_utility.hpp"
#include "dot/dot_utils.hpp"
#include "utils/nvidia_device_prefix.hpp"
#include "utils/origin_utils.hpp"

#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>
#include <memory>
#include <string>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

class DevicePrefix : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        device_prefix::prefix() = "HGX_";
    }
    void TearDown() override
    {
        device_prefix::prefix().clear();
    }
};

TEST_F(DevicePrefix, StripRemovesThePublishedPrefixOnly)
{
    EXPECT_EQ(device_prefix::strip("HGX_GPU_0"), "GPU_0");
    EXPECT_EQ(device_prefix::strip("MGX_Baseboard_0"), "MGX_Baseboard_0");
    EXPECT_EQ(device_prefix::strip("GPU_0"), "GPU_0");
    EXPECT_EQ(device_prefix::strip("ERoT_GPU_0"), "ERoT_GPU_0");
    EXPECT_EQ(device_prefix::strip(""), "");
}

TEST_F(DevicePrefix, StripWithoutPrefixKeepsNames)
{
    device_prefix::prefix().clear();
    EXPECT_EQ(device_prefix::strip("HGX_GPU_0"), "HGX_GPU_0");
    EXPECT_EQ(device_prefix::strip("GPU_0"), "GPU_0");
}

TEST_F(DevicePrefix, MatchesExactOrPrefixedName)
{
    EXPECT_TRUE(device_prefix::matches("GPU_0", "GPU_0"));
    EXPECT_TRUE(device_prefix::matches("HGX_GPU_0", "GPU_0"));
    EXPECT_TRUE(device_prefix::matches("HGX_GPU_0", "HGX_GPU_0"));
    EXPECT_FALSE(device_prefix::matches("MGX_GPU_0", "GPU_0"));
    EXPECT_FALSE(device_prefix::matches("ERoT_GPU_0", "GPU_0"));
    EXPECT_FALSE(device_prefix::matches("HGX_GPU_1", "GPU_0"));
    EXPECT_FALSE(device_prefix::matches("GPU_0", "HGX_GPU_0"));
}

TEST_F(DevicePrefix, ChassisForAddsThePrefix)
{
    EXPECT_EQ(device_prefix::chassisFor("GPU_0"), "HGX_GPU_0");
    EXPECT_EQ(device_prefix::chassisFor("HGX_GPU_0"), "HGX_GPU_0");
    device_prefix::prefix().clear();
    EXPECT_EQ(device_prefix::chassisFor("GPU_0"), "GPU_0");
}

std::shared_ptr<device_prefix::PrefixReads> lastAttemptReads(size_t pending)
{
    auto reads = std::make_shared<device_prefix::PrefixReads>();
    reads->attempt = device_prefix::maxLoadAttempts - 1;
    reads->pending = pending;
    return reads;
}

TEST_F(DevicePrefix, PublishedValueIsCached)
{
    device_prefix::prefix().clear();
    device_prefix::afterGetPrefix(lastAttemptReads(1),
                                  boost::system::error_code{}, "MGX_");
    EXPECT_EQ(device_prefix::prefix(), "MGX_");
}

TEST_F(DevicePrefix, AgreeingRecordsSetThePrefix)
{
    device_prefix::prefix().clear();
    auto reads = lastAttemptReads(2);
    device_prefix::afterGetPrefix(reads, boost::system::error_code{}, "MGX_");
    EXPECT_EQ(device_prefix::prefix(), "");
    device_prefix::afterGetPrefix(reads, boost::system::error_code{}, "MGX_");
    EXPECT_EQ(device_prefix::prefix(), "MGX_");
}

TEST_F(DevicePrefix, DisagreeingRecordsUseNoPrefix)
{
    auto reads = lastAttemptReads(2);
    device_prefix::afterGetPrefix(reads, boost::system::error_code{}, "HGX_");
    device_prefix::afterGetPrefix(reads, boost::system::error_code{}, "MGX_");
    EXPECT_EQ(device_prefix::prefix(), "");
}

TEST_F(DevicePrefix, ReadErrorKeepsCachedValue)
{
    auto reads = lastAttemptReads(2);
    device_prefix::afterGetPrefix(reads, boost::system::error_code{}, "MGX_");
    device_prefix::afterGetPrefix(
        reads,
        boost::system::errc::make_error_code(boost::system::errc::io_error),
        "");
    EXPECT_EQ(device_prefix::prefix(), "HGX_");
}

TEST_F(DevicePrefix, MissingRecordKeepsCachedValue)
{
    device_prefix::afterGetPrefixObject(
        device_prefix::maxLoadAttempts - 1,
        boost::system::errc::make_error_code(boost::system::errc::io_error),
        {});
    device_prefix::afterGetPrefixObject(device_prefix::maxLoadAttempts - 1,
                                        boost::system::error_code{}, {});
    dbus::utility::MapperGetSubTreeResponse noService{
        {"/xyz/openbmc_project/inventory/system/board/X/Prefix", {}}};
    device_prefix::afterGetPrefixObject(device_prefix::maxLoadAttempts - 1,
                                        boost::system::error_code{}, noService);
    EXPECT_EQ(device_prefix::prefix(), "HGX_");
}

TEST_F(DevicePrefix, SensorOriginOfConditionUsesChassisObjectName)
{
    EXPECT_EQ(origin_utils::resolveDbusPathToRedfishUri(
                  "/xyz/openbmc_project/sensors/temperature/GPU_0_TEMP_0",
                  "GPU_0"),
              "/redfish/v1/Chassis/HGX_GPU_0/Sensors/GPU_0_TEMP_0");
}

TEST_F(DevicePrefix, SensorOriginOfConditionWithoutPrefix)
{
    device_prefix::prefix().clear();
    EXPECT_EQ(origin_utils::resolveDbusPathToRedfishUri(
                  "/xyz/openbmc_project/sensors/temperature/GPU_0_TEMP_0",
                  "GPU_0"),
              "/redfish/v1/Chassis/GPU_0/Sensors/GPU_0_TEMP_0");
}

TEST_F(DevicePrefix, DotComponentMatchesPrefixedObject)
{
    dbus::utility::MapperGetSubTreeResponse resp{
        {"/xyz/openbmc_project/dot/HGX_IRoT_CPU_0",
         {{"xyz.openbmc_project.Dot", {}}}},
        {"/xyz/openbmc_project/dot/HGX_IRoT_CPU_1",
         {{"xyz.openbmc_project.Dot", {}}}}};
    auto found = dot_utils::findMatchingDOTComponent("IRoT_CPU_1", resp);
    ASSERT_TRUE(found.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_EQ(found->first, "/xyz/openbmc_project/dot/HGX_IRoT_CPU_1");
    EXPECT_FALSE(
        dot_utils::findMatchingDOTComponent("IRoT_CPU_2", resp).has_value());
}

} // namespace
} // namespace redfish
