// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
// AFFILIATES. All rights reserved.

#include "async_resp.hpp"
#include "http_response.hpp"
#include "utils/chassis_utils.hpp"

#include <boost/beast/http/status.hpp>

#include <memory>
#include <string>

#include <gtest/gtest.h>

namespace redfish::chassis_utils
{
namespace
{

TEST(IsRotChassisName, MatchesKnownRotChassisIds)
{
    EXPECT_TRUE(isRotChassisName("BlueField_ERoT_BMC_0"));
    EXPECT_TRUE(isRotChassisName("BlueField_IRoT_NIC_0"));
    EXPECT_TRUE(isRotChassisName("HGX_ERoT_FPGA_0"));
    EXPECT_TRUE(isRotChassisName("IRoT_CPU_0"));
}

TEST(IsRotChassisName, TokenIsMatchedInAnyLetterCase)
{
    EXPECT_TRUE(isRotChassisName("BlueField_EROT_BMC_0"));
    EXPECT_TRUE(isRotChassisName("BlueField_erot_BMC_0"));
    EXPECT_TRUE(isRotChassisName("BlueField_IROT_NIC_0"));
    EXPECT_TRUE(isRotChassisName("irot_cpu_0"));
}

TEST(IsRotChassisName, MatchesTokenEndingInRotAtAnyPosition)
{
    EXPECT_TRUE(isRotChassisName("RoT"));
    EXPECT_TRUE(isRotChassisName("RoT_0"));
    EXPECT_TRUE(isRotChassisName("ERoT"));
    EXPECT_TRUE(isRotChassisName("ERoT_0"));
    EXPECT_TRUE(isRotChassisName("Card1_ERoT"));
    EXPECT_TRUE(isRotChassisName("BlueField_MyIRoT_0"));
}

TEST(IsRotChassisName, DoesNotMatchRotInTheMiddleOfAToken)
{
    EXPECT_FALSE(isRotChassisName("ERoTX"));
    EXPECT_FALSE(isRotChassisName("NotERoTish_0"));
    EXPECT_FALSE(isRotChassisName("RoTX_0"));
}

TEST(IsRotChassisName, DoesNotMatchOtherChassis)
{
    EXPECT_FALSE(isRotChassisName(""));
    EXPECT_FALSE(isRotChassisName("_"));
    EXPECT_FALSE(isRotChassisName("Bluefield_BMC"));
    EXPECT_FALSE(isRotChassisName("HGX_GPU_SXM_1"));
    EXPECT_FALSE(isRotChassisName("ERoT-BMC"));
    EXPECT_FALSE(isRotChassisName("BlueField ERoT BMC"));
}

TEST(PruneIfRotChassis, RotChassisIs404AndCallerShouldReturn)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    EXPECT_TRUE(
        pruneIfRotChassis(asyncResp, "BlueField_ERoT_BMC_0", "Sensors"));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

TEST(PruneIfRotChassis, NonRotChassisIsUntouched)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    EXPECT_FALSE(pruneIfRotChassis(asyncResp, "Bluefield_BMC", "Sensors"));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
}

} // namespace
} // namespace redfish::chassis_utils
