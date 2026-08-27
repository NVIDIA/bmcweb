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

#include "utils/nvidia_platform_power_cycle_utils.hpp"

#include <boost/beast/http/status.hpp>

#include <memory>
#include <string>

#include <gtest/gtest.h>

namespace redfish::nvidia_platform_power_cycle
{
namespace
{
TEST(PlatformPowerCycleUtils, BuildsPerSystemObjectPath)
{
    EXPECT_EQ(getObjectPath(0).str,
              "/xyz/openbmc_project/control/power_cycle/host0");
    EXPECT_EQ(getObjectPath(7).str,
              "/xyz/openbmc_project/control/power_cycle/host7");
}

TEST(PlatformPowerCycleUtils, RequiresAnExactAdvertisedType)
{
    const Capabilities capabilities{
        "com.nvidia.Control.Platform.PowerCycle",
        {std::string(auxPowerCycle), std::string(fullPowerCycle)}};

    EXPECT_TRUE(supports(capabilities, auxPowerCycle));
    EXPECT_TRUE(supports(capabilities, fullPowerCycle));
    EXPECT_FALSE(supports(capabilities, auxPowerCycleForce));
    EXPECT_FALSE(supports(capabilities, "FullPowerCycle"));
}

TEST(PlatformPowerCycleUtils, ConvertsCapabilitiesToFullPowerCycleSupport)
{
    const Capabilities capabilities{"com.nvidia.Control.Platform.PowerCycle",
                                    {std::string(fullPowerCycle)}};

    bool callbackCalled = false;
    afterGetPowerCycleSupport(
        [&callbackCalled](const boost::system::error_code& ec, bool supported) {
            EXPECT_FALSE(ec);
            EXPECT_TRUE(supported);
            callbackCalled = true;
        },
        std::string(fullPowerCycle), {}, capabilities);

    EXPECT_TRUE(callbackCalled);
}

TEST(PlatformPowerCycleUtils, MissingCapabilitiesReportUnsupported)
{
    bool callbackCalled = false;
    afterGetPowerCycleSupport(
        [&callbackCalled](const boost::system::error_code& ec, bool supported) {
            EXPECT_FALSE(ec);
            EXPECT_FALSE(supported);
            callbackCalled = true;
        },
        std::string(fullPowerCycle), {}, std::nullopt);

    EXPECT_TRUE(callbackCalled);
}

TEST(PlatformPowerCycleUtils, EmptyMapperSubTreeReportsProviderAbsent)
{
    bool callbackCalled = false;
    afterGetPowerCycleSubTree(
        getObjectPath(0),
        [&callbackCalled](const boost::system::error_code& ec,
                          const std::optional<Capabilities>& capabilities) {
            EXPECT_FALSE(ec);
            EXPECT_FALSE(capabilities);
            callbackCalled = true;
        },
        {}, {});

    EXPECT_TRUE(callbackCalled);
}

TEST(PlatformPowerCycleUtils, MapperSubTreeRequiresExactHostPath)
{
    const dbus::utility::MapperGetSubTreeResponse subtree = {
        {getObjectPath(1).str,
         {{"com.nvidia.Control.Platform.PowerCycle",
           {std::string(interface)}}}}};

    bool callbackCalled = false;
    afterGetPowerCycleSubTree(
        getObjectPath(0),
        [&callbackCalled](const boost::system::error_code& ec,
                          const std::optional<Capabilities>& capabilities) {
            EXPECT_FALSE(ec);
            EXPECT_FALSE(capabilities);
            callbackCalled = true;
        },
        {}, subtree);

    EXPECT_TRUE(callbackCalled);
}

TEST(PlatformPowerCycleUtils, MapperFailureDoesNotReportProviderAbsent)
{
    const boost::system::error_code mapperError =
        boost::system::errc::make_error_code(boost::system::errc::io_error);

    bool callbackCalled = false;
    afterGetPowerCycleSubTree(
        getObjectPath(0),
        [&callbackCalled,
         &mapperError](const boost::system::error_code& ec,
                       const std::optional<Capabilities>& capabilities) {
            EXPECT_EQ(ec, mapperError);
            EXPECT_FALSE(capabilities);
            callbackCalled = true;
        },
        mapperError, {});

    EXPECT_TRUE(callbackCalled);
}

TEST(PlatformPowerCycleUtils, MultipleProvidersReportAnError)
{
    const dbus::utility::MapperGetSubTreeResponse subtree = {
        {getObjectPath(0).str,
         {{"com.nvidia.Control.Platform.PowerCycle.0",
           {std::string(interface)}},
          {"com.nvidia.Control.Platform.PowerCycle.1",
           {std::string(interface)}}}}};

    bool callbackCalled = false;
    afterGetPowerCycleSubTree(
        getObjectPath(0),
        [&callbackCalled](const boost::system::error_code& ec,
                          const std::optional<Capabilities>& capabilities) {
            EXPECT_EQ(ec, boost::system::errc::state_not_recoverable);
            EXPECT_FALSE(capabilities);
            callbackCalled = true;
        },
        {}, subtree);

    EXPECT_TRUE(callbackCalled);
}

TEST(PlatformPowerCycleUtils, MissingProviderRejectsFullPowerCycleAsUnsupported)
{
    auto response = std::make_shared<bmcweb::AsyncResp>();

    afterGetCapabilitiesForRequest(
        response, getObjectPath(0), std::string(fullPowerCycle),
        "FullPowerCycle", "ComputerSystem.Reset", {}, std::nullopt);

    EXPECT_EQ(response->res.result(), boost::beast::http::status::bad_request);
    EXPECT_EQ(response->res.jsonValue["error"]["code"],
              "Base.1.19.ActionParameterValueNotInList");
}
} // namespace
} // namespace redfish::nvidia_platform_power_cycle
