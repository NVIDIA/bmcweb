// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
// AFFILIATES. All rights reserved.

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "http_response.hpp"
#include "utils/nvidia_chassis_util.hpp"

#include <boost/beast/http/status.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>

#include <memory>

#include <gtest/gtest.h>

namespace redfish::nvidia_chassis_utils
{
namespace
{

constexpr const char* hostNetworkAccessPath =
    "/xyz/openbmc_project/control/host0/HostManagementNetworkAccess";

// Walk with find()/contains() rather than operator[]: on a const json a
// missing key is undefined behavior, and absence is what these tests assert.
bool hasHostNetworkAccess(const bmcweb::AsyncResp& asyncResp)
{
    const nlohmann::json& json = asyncResp.res.jsonValue;
    const auto oem = json.find("Oem");
    if (oem == json.end())
    {
        return false;
    }
    const auto nvidia = oem->find("Nvidia");
    if (nvidia == oem->end())
    {
        return false;
    }
    return nvidia->contains("HostManagementNetworkAccess");
}

// A chassis that carries no host_management_network_access association does
// not own the host-NIC control, so the property is omitted -- and that is a
// normal outcome, not a client-visible failure.

TEST(HostManagementNetworkAccess, GetWithoutAssociationOmitsProperty)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    afterGetHostNetworkAccessEndpoints(asyncResp, boost::system::error_code{},
                                       dbus::utility::MapperEndPoints{});

    EXPECT_FALSE(hasHostNetworkAccess(*asyncResp));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
}

TEST(HostManagementNetworkAccess, GetWithMapperErrorOmitsProperty)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    afterGetHostNetworkAccessEndpoints(
        asyncResp,
        boost::system::errc::make_error_code(boost::system::errc::io_error),
        dbus::utility::MapperEndPoints{hostNetworkAccessPath});

    EXPECT_FALSE(hasHostNetworkAccess(*asyncResp));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
}

// A write is different: reporting success for a property the BMC never set
// would lie to the client, so the same missing association must fail.

TEST(HostManagementNetworkAccess, PatchWithoutAssociationIsPropertyUnknown)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    afterSetHostNetworkAccessEndpoints(asyncResp, true,
                                       boost::system::error_code{},
                                       dbus::utility::MapperEndPoints{});

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

TEST(HostManagementNetworkAccess, PatchWithMapperErrorIsPropertyUnknown)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    afterSetHostNetworkAccessEndpoints(
        asyncResp, false,
        boost::system::errc::make_error_code(boost::system::errc::io_error),
        dbus::utility::MapperEndPoints{hostNetworkAccessPath});

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

} // namespace
} // namespace redfish::nvidia_chassis_utils
