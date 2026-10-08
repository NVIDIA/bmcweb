// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
// AFFILIATES. All rights reserved.

#include "async_resp.hpp"
#include "http_response.hpp"
#include "nvidia_igx.hpp"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <string_view>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

using boost::beast::http::status;

boost::system::error_code errc(boost::system::errc::errc_t code)
{
    return boost::system::errc::make_error_code(code);
}

TEST(NvidiaIgxSerialConsoleTest, SourceFromDbus)
{
    EXPECT_EQ(nvidia_igx::serialConsoleSourceFromDbus("Tegra"),
              "HostProcessor");
    EXPECT_EQ(nvidia_igx::serialConsoleSourceFromDbus("SMCU"),
              "SafetyController");
    EXPECT_EQ(nvidia_igx::serialConsoleSourceFromDbus(""), std::nullopt);
    EXPECT_EQ(nvidia_igx::serialConsoleSourceFromDbus("HostProcessor"),
              std::nullopt);
    EXPECT_EQ(nvidia_igx::serialConsoleSourceFromDbus("SafetyController"),
              std::nullopt);
}

TEST(NvidiaIgxSerialConsoleTest, SourceToDbus)
{
    EXPECT_EQ(nvidia_igx::serialConsoleSourceToDbus("HostProcessor"), "Tegra");
    EXPECT_EQ(nvidia_igx::serialConsoleSourceToDbus("SafetyController"),
              "SMCU");
    EXPECT_EQ(nvidia_igx::serialConsoleSourceToDbus("Tegra"), std::nullopt);
    EXPECT_EQ(nvidia_igx::serialConsoleSourceToDbus("SMCU"), std::nullopt);
    EXPECT_EQ(nvidia_igx::serialConsoleSourceToDbus(""), std::nullopt);
}

TEST(NvidiaIgxSerialConsoleTest, GetReportsRedfishName)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    nvidia_igx::afterSerialConsoleGet(asyncResp, {}, "Tegra");
    EXPECT_EQ(asyncResp->res.result(), status::ok);
    EXPECT_EQ(asyncResp->res.jsonValue["SerialConsoleSource"], "HostProcessor");
}

TEST(NvidiaIgxSerialConsoleTest, GetIndeterminateIsNull)
{
    for (std::string_view source : {"", "Unexpected"})
    {
        auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
        nvidia_igx::afterSerialConsoleGet(asyncResp, {}, std::string(source));
        EXPECT_EQ(asyncResp->res.result(), status::ok);
        EXPECT_TRUE(asyncResp->res.jsonValue["SerialConsoleSource"].is_null());
    }
}

TEST(NvidiaIgxSerialConsoleTest, GetDaemonAbsentIsNull)
{
    for (auto code : {boost::system::errc::host_unreachable,
                      boost::system::errc::no_such_device_or_address})
    {
        auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
        nvidia_igx::afterSerialConsoleGet(asyncResp, errc(code), "");
        EXPECT_EQ(asyncResp->res.result(), status::ok);
        EXPECT_TRUE(asyncResp->res.jsonValue["SerialConsoleSource"].is_null());
    }
}

TEST(NvidiaIgxSerialConsoleTest, GetOtherErrorIsInternalError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    nvidia_igx::afterSerialConsoleGet(asyncResp,
                                      errc(boost::system::errc::io_error), "");
    EXPECT_EQ(asyncResp->res.result(), status::internal_server_error);
    EXPECT_FALSE(asyncResp->res.jsonValue.contains("SerialConsoleSource"));
}

TEST(NvidiaIgxSerialConsoleTest, SwitchErrors)
{
    struct Case
    {
        std::string_view dbusError;
        status expected;
        std::string_view retryAfter;
    };
    for (const Case& c : {
             Case{"com.nvidia.ConsoleMux.Error.Busy",
                  status::service_unavailable, "5"},
             Case{"org.freedesktop.DBus.Error.NoReply",
                  status::service_unavailable, "10"},
             Case{"org.freedesktop.DBus.Error.Timeout",
                  status::service_unavailable, "10"},
             Case{"org.freedesktop.DBus.Error.ServiceUnknown",
                  status::bad_request, ""},
             Case{"org.freedesktop.DBus.Error.NameHasNoOwner",
                  status::bad_request, ""},
             Case{"com.nvidia.ConsoleMux.Error.SwitchFailed",
                  status::internal_server_error, ""},
         })
    {
        crow::Response res;
        nvidia_igx::serialConsoleSwitchError(res, c.dbusError);
        EXPECT_EQ(res.result(), c.expected) << c.dbusError;
        EXPECT_EQ(res.getHeaderValue(boost::beast::http::field::retry_after),
                  c.retryAfter)
            << c.dbusError;
    }
}

TEST(NvidiaIgxSerialConsoleTest, AdvertiseLinkAndAction)
{
    nlohmann::json oem;
    nvidia_igx::advertiseSerialConsole(oem, "bmc");
    EXPECT_EQ(oem["@odata.type"], "#NvidiaManager.v1_12_0.NvidiaManager");
    EXPECT_EQ(oem["SerialConsole"]["@odata.id"],
              "/redfish/v1/Managers/bmc/Oem/Nvidia/SerialConsole");

    nlohmann::json actions;
    nvidia_igx::advertiseSwitchSerialConsole(actions, "bmc");
    const nlohmann::json& action =
        actions["#NvidiaManager.SwitchSerialConsole"];
    EXPECT_EQ(
        action["target"],
        "/redfish/v1/Managers/bmc/Actions/Oem/NvidiaManager.SwitchSerialConsole");
    EXPECT_EQ(action["Source@Redfish.AllowableValues"],
              nlohmann::json::array({"HostProcessor", "SafetyController"}));
}

} // namespace
} // namespace redfish
