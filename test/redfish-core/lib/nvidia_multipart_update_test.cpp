// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.

#include "bmcweb_config.h"

#include "async_resp.hpp"
#include "http/http_connection.hpp"
#include "http/http_request.hpp"
#include "http/http_response.hpp"
#include "http_connect_types.hpp"
#include "nvidia_multipart_update.hpp"
#include "test_stream.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/_experimental/test/stream.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>

#include <chrono>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include <gtest/gtest.h>

namespace redfish
{
namespace
{

TEST(ParseRfaUri, LocalTargetWithoutPrefixGoesToLocal)
{
    std::vector<std::string> local;
    std::vector<std::string> satellite;

    EXPECT_TRUE(
        parseRfaUri("/redfish/v1/Chassis/HGX_Chassis_0", local, satellite));
    ASSERT_EQ(local.size(), 1U);
    EXPECT_EQ(local[0], "/redfish/v1/Chassis/HGX_Chassis_0");
    EXPECT_TRUE(satellite.empty());
}

TEST(ParseRfaUri, SatelliteTargetWithPrefixGoesToSatellite)
{
    std::vector<std::string> local;
    std::vector<std::string> satellite;

    std::string uri = std::format("/redfish/v1/Chassis/{}_HGX_Chassis_0",
                                  BMCWEB_REDFISH_AGGREGATION_PREFIX);
    EXPECT_TRUE(parseRfaUri(uri, local, satellite));
    EXPECT_TRUE(local.empty());
    ASSERT_EQ(satellite.size(), 1U);
    EXPECT_EQ(satellite[0], uri);
}

TEST(ParseRfaUri, PrefixWithoutUnderscoreSeparatorGoesToLocal)
{
    std::vector<std::string> local;
    std::vector<std::string> satellite;

    // The function requires "<prefix>_" exactly; without the underscore the
    // segment is treated as a local target.
    std::string uri = std::format("/redfish/v1/Chassis/{}HGX",
                                  BMCWEB_REDFISH_AGGREGATION_PREFIX);
    EXPECT_TRUE(parseRfaUri(uri, local, satellite));
    ASSERT_EQ(local.size(), 1U);
    EXPECT_EQ(local[0], uri);
    EXPECT_TRUE(satellite.empty());
}

TEST(ParseRfaUri, PrefixInNonTrailingSegmentGoesToLocal)
{
    std::vector<std::string> local;
    std::vector<std::string> satellite;

    // Only the trailing segment is inspected for the prefix.
    std::string uri = std::format("/redfish/v1/{}_Chassis/HGX_Chassis_0",
                                  BMCWEB_REDFISH_AGGREGATION_PREFIX);
    EXPECT_TRUE(parseRfaUri(uri, local, satellite));
    ASSERT_EQ(local.size(), 1U);
    EXPECT_EQ(local[0], uri);
    EXPECT_TRUE(satellite.empty());
}

TEST(ParseRfaUri, EmptyPathReturnsFalse)
{
    std::vector<std::string> local;
    std::vector<std::string> satellite;

    EXPECT_FALSE(parseRfaUri("", local, satellite));
    EXPECT_TRUE(local.empty());
    EXPECT_TRUE(satellite.empty());
}

TEST(ParseRfaUri, AppendsToExistingOutputs)
{
    std::vector<std::string> local;
    std::vector<std::string> satellite;

    std::string satUri = std::format("/redfish/v1/Chassis/{}_HGX_Chassis_0",
                                     BMCWEB_REDFISH_AGGREGATION_PREFIX);

    EXPECT_TRUE(
        parseRfaUri("/redfish/v1/Chassis/HGX_Chassis_0", local, satellite));
    EXPECT_TRUE(parseRfaUri(satUri, local, satellite));
    EXPECT_TRUE(parseRfaUri("/redfish/v1/Managers/bmc", local, satellite));

    ASSERT_EQ(local.size(), 2U);
    EXPECT_EQ(local[0], "/redfish/v1/Chassis/HGX_Chassis_0");
    EXPECT_EQ(local[1], "/redfish/v1/Managers/bmc");
    ASSERT_EQ(satellite.size(), 1U);
    EXPECT_EQ(satellite[0], satUri);
}

struct ClockFake
{
    bool wascalled = false;
    std::string getDateStr()
    {
        wascalled = true;
        return "TestTime";
    }
};

TEST(NvidiaMultipartUpdate, FullUpdateGoldenPath)
{
    using crow::TestStream;
    App app;
    requestRoutesNvUpdateServiceMultipartUpdate(app);
    app.validate();

    boost::asio::io_context io;
    ClockFake clock;
    TestStream stream(io);
    TestStream out(io);
    stream.connect(out);

    constexpr std::string_view boundary = "BMCWEBTESTBOUNDARY";
    std::string body = std::format(
        "--{0}\r\n"
        "Content-Disposition: form-data; name=\"UpdateParameters\"\r\n"
        "Content-Type: application/json\r\n"
        "\r\n"
        "{{\"@Redfish.OperationApplyTime\":\"OnReset\"}}"
        "\r\n--{0}\r\n"
        "Content-Disposition: form-data; name=\"UpdateFile\"; "
        "filename=\"image.bin\"\r\n"
        "Content-Type: application/octet-stream\r\n"
        "\r\n"
        "DUMMYIMAGEDATA"
        "\r\n--{0}--\r\n",
        boundary);

    std::string request = std::format(
        "POST /redfish/v1/UpdateService/update-multipart/ HTTP/1.1\r\n"
        "Host: openbmc_project.xyz\r\n"
        "Content-Type: multipart/form-data; boundary={}\r\n"
        "Content-Length: {}\r\n"
        "Connection: close\r\n"
        "\r\n"
        "{}",
        boundary, body.size(), body);
    out.write_some(boost::asio::buffer(request));
    boost::asio::steady_timer timer(io);
    std::function<std::string()> date(
        std::bind_front(&ClockFake::getDateStr, &clock));

    boost::asio::ssl::context context{boost::asio::ssl::context::tls};
    std::shared_ptr<crow::Connection<TestStream, App>> conn =
        std::make_shared<crow::Connection<TestStream, App>>(
            &app, crow::HttpType::HTTP, std::move(timer), date,
            boost::asio::ssl::stream<TestStream>(std::move(stream), context));
    conn->disableAuth();
    conn->start();
    io.run_for(std::chrono::seconds(5));
    std::string outStr = out.str();

    std::string expected =
        "HTTP/1.1 200 OK\r\n"
        "Allow: POST\r\n"
        "Connection: close\r\n"
        "Strict-Transport-Security: max-age=31536000; includeSubdomains\r\n"
        "Pragma: no-cache\r\n"
        "Cache-Control: no-store, max-age=0\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Date: TestTime\r\n"
        "Content-Length: 0\r\n\r\n";
    EXPECT_EQ(outStr, expected);
}

} // namespace
} // namespace redfish
