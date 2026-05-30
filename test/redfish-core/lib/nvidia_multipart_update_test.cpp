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

namespace redfish::nvidia
{
namespace
{

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

// Regression test: clients (e.g. libcurl for uploads over ~1KB) send
// "Expect: 100-continue".  The connection must still dispatch the headers to
// the streamInput route so the multipart body callbacks are registered before
// the body is read.  Previously the 100-continue path skipped header dispatch
// entirely, so large uploads parsed with no callbacks.
TEST(NvidiaMultipartUpdate, FullUpdateExpectContinue)
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
        "Expect: 100-continue\r\n"
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

    // The server first emits a "100 Continue" interim response, then the final
    // "200 OK" once the streamed body has been parsed via the route callbacks.
    EXPECT_NE(outStr.find("HTTP/1.1 100 Continue\r\n"), std::string::npos);
    EXPECT_NE(outStr.find("HTTP/1.1 200 OK\r\n"), std::string::npos);
}

TEST(ParseRfaUri, EmptyUriReturnsError)
{
    EXPECT_EQ(parseRfaUri(""), TargetType::Error);
}

TEST(ParseRfaUri, UnparseableUriReturnsError)
{
    // Invalid percent-encoding can't be parsed as a relative ref.
    EXPECT_EQ(parseRfaUri("/redfish/v1/Chassis/%zz"), TargetType::Error);
}

TEST(ParseRfaUri, HmcChassisTargetOmitsTargets)
{
    std::string uri =
        std::format("/redfish/v1/Chassis/{}", BMCWEB_RFA_HMC_UPDATE_TARGET);
    EXPECT_EQ(parseRfaUri(uri), TargetType::SatelliteOmitTargets);
}

TEST(ParseRfaUri, AggregationPrefixedChassisIsSatellite)
{
    // A chassis whose id carries the aggregation prefix (but isn't the HMC
    // update target) routes to a satellite BMC.
    std::string uri = std::format("/redfish/v1/Chassis/{}_Baseboard_0",
                                  BMCWEB_REDFISH_AGGREGATION_PREFIX);
    EXPECT_EQ(parseRfaUri(uri), TargetType::Satellite);
}

TEST(ParseRfaUri, UnprefixedChassisIsLocal)
{
    EXPECT_EQ(parseRfaUri("/redfish/v1/Chassis/Baseboard_0"),
              TargetType::Local);
}

TEST(ParseRfaUri, HmcManagerTargetOmitsTargets)
{
    std::string uri =
        std::format("/redfish/v1/Managers/{}", BMCWEB_RFA_HMC_UPDATE_TARGET);
    EXPECT_EQ(parseRfaUri(uri), TargetType::SatelliteOmitTargets);
}

TEST(ParseRfaUri, NonHmcManagerIsLocal)
{
    EXPECT_EQ(parseRfaUri("/redfish/v1/Managers/bmc"), TargetType::Local);
}

TEST(ParseRfaUri, UnrelatedUriIsLocal)
{
    EXPECT_EQ(parseRfaUri("/redfish/v1/Systems/system"), TargetType::Local);
}

} // namespace
} // namespace redfish::nvidia
