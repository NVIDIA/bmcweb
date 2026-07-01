// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "log_services.hpp"

#include <boost/beast/http/status.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>

#include <memory>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

TEST(LogServicesDumpServiceTest, LogServicesInvalidDumpServiceGetReturnsError)
{
    auto shareAsyncResp = std::make_shared<bmcweb::AsyncResp>();
    getDumpServiceInfo(shareAsyncResp, "Invalid");
    EXPECT_EQ(shareAsyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

// A mapper failure is a genuine server-side error, so clearDump must report
// 500 InternalError rather than treating it as an unsupported dump type.
TEST(LogServicesDumpClearTest, MapperErrorReturnsInternalError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::system::error_code ec =
        boost::system::errc::make_error_code(boost::system::errc::io_error);
    handleDumpClearSubTreePaths(asyncResp, "FaultLog", ec, {});
    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

// When the mapper succeeds but the dump object is not present (e.g. FaultLog on
// a platform that does not back it), clearDump must return 404
// ResourceNotFound.
TEST(LogServicesDumpClearTest, MissingDumpPathReturnsResourceNotFound)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    dbus::utility::MapperGetSubTreePathsResponse subTreePaths = {
        "/xyz/openbmc_project/dump/bmc", "/xyz/openbmc_project/dump/system"};
    handleDumpClearSubTreePaths(asyncResp, "FaultLog",
                                boost::system::error_code{}, subTreePaths);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

TEST(LogServicesDumpClearTest, DumpTypeSupportedWhenPathPresent)
{
    dbus::utility::MapperGetSubTreePathsResponse subTreePaths = {
        "/xyz/openbmc_project/dump/bmc", "/xyz/openbmc_project/dump/faultlog"};
    EXPECT_TRUE(isDumpTypeSupported("FaultLog", subTreePaths));
}

TEST(LogServicesDumpClearTest, DumpTypeUnsupportedWhenPathAbsent)
{
    dbus::utility::MapperGetSubTreePathsResponse subTreePaths = {
        "/xyz/openbmc_project/dump/bmc", "/xyz/openbmc_project/dump/system"};
    EXPECT_FALSE(isDumpTypeSupported("FaultLog", subTreePaths));
    EXPECT_FALSE(isDumpTypeSupported("FaultLog", {}));
}

} // namespace
} // namespace redfish
