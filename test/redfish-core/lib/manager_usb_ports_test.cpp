// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
// AFFILIATES. All rights reserved.

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "http_response.hpp"
#include "manager_usb_ports.hpp"

#include <boost/beast/http/status.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>

#include <memory>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

const dbus::utility::MapperGetSubTreeResponse portControlTree{
    {"/xyz/openbmc_project/control/port/usb0",
     {{"xyz.openbmc_project.Control.Port",
       {"xyz.openbmc_project.Object.Enable"}}}}};

const dbus::utility::MapperGetSubTreeResponse debugSerialOnlyTree{
    {"/xyz/openbmc_project/control/port/debug_serial0",
     {{"xyz.openbmc_project.Control.DebugSerial",
       {"xyz.openbmc_project.Object.Enable"}}}}};

boost::system::error_code notFound()
{
    return boost::system::errc::make_error_code(boost::system::errc::io_error);
}

TEST(ManagerUSBPortsTest, PortControlServiceDetected)
{
    EXPECT_TRUE(manager_usb_ports::hasPortControlService(portControlTree));
}

TEST(ManagerUSBPortsTest, DebugSerialAloneIsNotPortControl)
{
    EXPECT_FALSE(manager_usb_ports::hasPortControlService(debugSerialOnlyTree));
    EXPECT_FALSE(manager_usb_ports::hasPortControlService({}));
}

TEST(ManagerUSBPortsTest, ManagerLinksUSBPortsWhenPresent)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    manager_usb_ports::afterGetPortControlForManager(asyncResp, "bmc", {},
                                                     portControlTree);

    EXPECT_EQ(asyncResp->res.jsonValue["USBPorts"]["@odata.id"],
              "/redfish/v1/Managers/bmc/USBPorts");
}

TEST(ManagerUSBPortsTest, ManagerOmitsUSBPortsWhenAbsent)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    manager_usb_ports::afterGetPortControlForManager(asyncResp, "bmc", {},
                                                     debugSerialOnlyTree);
    manager_usb_ports::afterGetPortControlForManager(asyncResp, "bmc",
                                                     notFound(), {});

    EXPECT_FALSE(asyncResp->res.jsonValue.contains("USBPorts"));
}

TEST(ManagerUSBPortsTest, CollectionNotFoundWhenAbsent)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    manager_usb_ports::afterGetPortControlForCollection(asyncResp, "bmc", {},
                                                        debugSerialOnlyTree);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

TEST(ManagerUSBPortsTest, PatchNotFoundWhenAbsent)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    manager_usb_ports::afterGetPortControlForPatch(
        asyncResp, "bmc", "debug_serial0", true, {}, debugSerialOnlyTree);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

TEST(ManagerUSBPortsTest, MapperFailureIsInternalError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    manager_usb_ports::afterGetPortControlForPort(
        asyncResp, "bmc", "usb0",
        boost::system::errc::make_error_code(boost::system::errc::timed_out),
        {});

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

} // namespace
} // namespace redfish
