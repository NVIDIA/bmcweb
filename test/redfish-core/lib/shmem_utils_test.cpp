// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
// AFFILIATES. All rights reserved.

#include "shmem_utils.hpp"

#include <gtest/gtest.h>

namespace redfish::shmem
{
namespace
{

TEST(ShmemUtilsTest, OemReportIsUnprefixedOrHgxPlatformEnvironmentMetrics)
{
    EXPECT_TRUE(isOemPlatformMetricsReport("PlatformEnvironmentMetrics_0"));
    EXPECT_TRUE(isOemPlatformMetricsReport("HGX_PlatformEnvironmentMetrics_0"));
}

TEST(ShmemUtilsTest, OtherReportsAreNotOemReports)
{
    EXPECT_FALSE(
        isOemPlatformMetricsReport("MGX_PlatformEnvironmentMetrics_0"));
    EXPECT_FALSE(isOemPlatformMetricsReport("HGX_ProcessorMetrics_0"));
    EXPECT_FALSE(isOemPlatformMetricsReport("PlatformEnvironmentMetrics_1"));
    EXPECT_FALSE(isOemPlatformMetricsReport("PlatformMetrics"));
    EXPECT_FALSE(isOemPlatformMetricsReport(""));
}

} // namespace
} // namespace redfish::shmem
