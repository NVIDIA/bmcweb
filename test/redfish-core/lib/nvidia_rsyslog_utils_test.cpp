// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "nvidia_rsyslog_utils.hpp"

#include <asm-generic/errno.h>

#include <boost/beast/http/status.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstdint>
#include <memory>
#include <string>

#include <gtest/gtest.h>

namespace redfish::rsyslog
{
namespace
{

std::shared_ptr<bmcweb::AsyncResp> respWithRsyslogStub()
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
        "#NvidiaNetworkProtocol.v1_2_0.NvidiaNetworkProtocol";
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["Rsyslog"]["@odata.type"] =
        "#NvidiaNetworkProtocol.v1_1_0.Rsyslog";
    return asyncResp;
}

TEST(RsyslogClientSettings, AbsentConfigOmitsRsyslog)
{
    for (int err : {EBADR, EHOSTUNREACH})
    {
        auto asyncResp = respWithRsyslogStub();
        boost::system::error_code ec(err, boost::system::system_category());
        afterGetRsyslogClientSettings(asyncResp, ec, {});
        EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
        EXPECT_FALSE(
            asyncResp->res.jsonValue["Oem"]["Nvidia"].contains("Rsyslog"));
        EXPECT_TRUE(
            asyncResp->res.jsonValue["Oem"]["Nvidia"].contains("@odata.type"));
    }
}

TEST(RsyslogClientSettings, OtherDbusErrorIsInternalError)
{
    auto asyncResp = respWithRsyslogStub();
    boost::system::error_code ec =
        boost::system::errc::make_error_code(boost::system::errc::timed_out);
    afterGetRsyslogClientSettings(asyncResp, ec, {});
    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

TEST(RsyslogClientSettings, PropertiesArePopulated)
{
    auto asyncResp = respWithRsyslogStub();
    dbus::utility::DBusPropertiesMap properties;
    properties.emplace_back("Address", std::string("10.0.0.1"));
    properties.emplace_back("Port", static_cast<uint16_t>(514));
    afterGetRsyslogClientSettings(asyncResp, {}, properties);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
    const nlohmann::json& rsyslog =
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["Rsyslog"];
    EXPECT_EQ(rsyslog["Address"], "10.0.0.1");
    EXPECT_EQ(rsyslog["Port"], 514);
    EXPECT_EQ(rsyslog["@odata.type"], "#NvidiaNetworkProtocol.v1_1_0.Rsyslog");
}

} // namespace
} // namespace redfish::rsyslog
