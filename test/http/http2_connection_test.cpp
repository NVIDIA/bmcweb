// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "async_resp.hpp"
#include "http/http2_connection.hpp"
#include "http/http_request.hpp"
#include "http/http_response.hpp"
#include "http_connect_types.hpp"
#include "nghttp2_adapters.hpp"
#include "test_stream.hpp"

#include <nghttp2/nghttp2.h>
#include <unistd.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/http/field.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
namespace crow
{

namespace
{

using ::testing::Pair;
using ::testing::UnorderedElementsAre;

struct FakeHandler
{
    bool called = false;
    bool authFailedCalled = false;

    void handle(const std::shared_ptr<Request>& req,
                const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
    {
        called = true;
        EXPECT_EQ(req->url().buffer(), "/redfish/v1/");
        EXPECT_EQ(req->methodString(), "GET");
        EXPECT_EQ(req->getHeaderValue(boost::beast::http::field::user_agent),
                  "curl/8.5.0");
        EXPECT_EQ(req->getHeaderValue(boost::beast::http::field::accept),
                  "*/*");
        asyncResp->res.write("StringOutput");
    }

    bool handleAuthFailed(const std::shared_ptr<Request>& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
    {
        authFailedCalled = true;
        EXPECT_EQ(req->url().buffer(), "/redfish/v1/Systems");
        asyncResp->res.result(boost::beast::http::status::unauthorized);
        asyncResp->res.addHeader(boost::beast::http::field::www_authenticate,
                                 "Basic");
        asyncResp->res.write("AuthFailedResponse");
        return true;
    }
};

std::string getDateStr()
{
    return "TestTime";
}

void unpackHeaders(std::string_view dataField,
                   std::vector<std::pair<std::string, std::string>>& headers)
{
    nghttp2_hd_inflater_ex inflater;

    while (!dataField.empty())
    {
        nghttp2_nv nv;
        int inflateFlags = 0;
        const uint8_t* data = std::bit_cast<const uint8_t*>(dataField.data());
        ssize_t parsed =
            inflater.hd2(&nv, &inflateFlags, data, dataField.size(), 1);

        ASSERT_GT(parsed, 0);
        dataField.remove_prefix(static_cast<size_t>(parsed));
        if ((inflateFlags & NGHTTP2_HD_INFLATE_EMIT) > 0)
        {
            const char* namePtr = std::bit_cast<const char*>(nv.name);
            std::string key(namePtr, nv.namelen);
            const char* valPtr = std::bit_cast<const char*>(nv.value);
            std::string value(valPtr, nv.valuelen);
            headers.emplace_back(key, value);
        }
        if ((inflateFlags & NGHTTP2_HD_INFLATE_FINAL) > 0)
        {
            EXPECT_EQ(inflater.endHeaders(), 0);
            break;
        }
    }
    EXPECT_TRUE(dataField.empty());
}

TEST(http_connection, RequestPropogates)
{
    using namespace std::literals;
    boost::asio::io_context io;
    TestStream stream(io);
    TestStream out(io);
    stream.connect(out);
    // This is a binary pre-encrypted stream captured from curl for a request to
    // curl https://localhost:18080/redfish/v1/
    std::string_view toSend =
        // Hello
        "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
        // 18 byte settings frame
        "\x00\x00\x12\x04\x00\x00\x00\x00\x00"
        // Settings
        "\x00\x03\x00\x00\x00\x64\x00\x04\x00\xa0\x00\x00\x00\x02\x00\x00\x00\x00"
        // Window update frame
        "\x00\x00\x04\x08\x00\x00\x00\x00\x00"
        // Window update
        "\x3e\x7f\x00\x01"
        // Header frame END_STREAM, END_HEADERS set
        "\x00\x00\x29\x01\x05\x00\x00\x00"
        // Header payload
        "\x01\x82\x87\x41\x8b\xa0\xe4\x1d\x13\x9d\x09\xb8\x17\x80\xf0\x3f"
        "\x04\x89\x62\xc2\xc9\x29\x91\x3b\x1d\xc2\xc7\x7a\x88\x25\xb6\x50"
        "\xc3\xcb\xb6\xb8\x3f\x53\x03\x2a\x2f\x2a"sv;

    boost::asio::write(out, boost::asio::buffer(toSend));

    FakeHandler handler;
    std::function<std::string()> date(getDateStr);
    boost::asio::ssl::context sslCtx(boost::asio::ssl::context::tls_server);
    auto conn = std::make_shared<HTTP2Connection<TestStream, FakeHandler>>(
        boost::asio::ssl::stream<TestStream>(std::move(stream), sslCtx),
        &handler, date, HttpType::HTTP, nullptr);
    conn->start();

    std::array<std::string_view, 9> expectedPrefix = {
        // Settings frame size 24
        "\x00\x00\x18\x04\x00\x00\x00\x00\x00"sv,
        // 4 max concurrent streams
        "\x00\x03\x00\x00\x00\x04"sv,
        // Enable push = false
        "\x00\x02\x00\x00\x00\x00"sv,
        // Max window size 1 << 20
        "\x00\x04\x00\x10\x00\x00"sv,
        // Max frame size 1 << 14
        "\x00\x05\x00\x00\x40\x00"sv,

        // Frame window update stream 0
        "\x00\x00\x04\x08\x00\x00\x00\x00\x00\x00\x0f\x00\x01"sv,

        // Settings ACK from server to client
        "\x00\x00\x00\x04\x01\x00\x00\x00\x00"sv,

        // Window update stream 1
        "\x00\x00\x04\x08\x00\x00\x00\x00\x01\x00\x07\x00\x01"sv,

        // Start Headers frame stream 1, size 0x005f
        "\x00\x00\x5f\x01\x04\x00\x00\x00\x01"sv,
    };

    // Flatten expectedPrefix into a single contiguous byte string for
    // comparison
    size_t expectedPrefixTotalSize = 0;
    for (std::string_view s : expectedPrefix)
    {
        expectedPrefixTotalSize += s.size();
    }
    std::string expectedPrefixFlat;
    expectedPrefixFlat.reserve(expectedPrefixTotalSize);
    for (std::string_view s : expectedPrefix)
    {
        expectedPrefixFlat.append(s.data(), s.size());
    }

    std::string_view expectedPostfix =
        // Data Frame, Length 12, Stream 1, End Stream flag set
        "\x00\x00\x0c\x00\x01\x00\x00\x00\x01"
        // The body expected
        "StringOutput"sv;

    std::string outStr;
    constexpr size_t headerSize = 0x05f;
    const size_t expectedTotal =
        expectedPrefixTotalSize + headerSize + expectedPostfix.size();

    // Run until we receive the expected amount of data
    while (outStr.size() < expectedTotal)
    {
        io.run_one();
        outStr = out.str();
    }
    EXPECT_TRUE(handler.called);

    // check the stream output against expected
    EXPECT_EQ(outStr.substr(0, expectedPrefixTotalSize), expectedPrefixFlat);
    outStr.erase(0, expectedPrefixTotalSize);
    std::vector<std::pair<std::string, std::string>> headers;
    unpackHeaders(outStr.substr(0, headerSize), headers);
    outStr.erase(0, headerSize);

    EXPECT_THAT(headers,
                UnorderedElementsAre(
                    Pair(":status", "200"), Pair("content-length", "12"),
                    Pair("strict-transport-security",
                         "max-age=31536000; includeSubdomains"),
                    Pair("cache-control", "no-store, max-age=0"),
                    Pair("x-content-type-options", "nosniff"),
                    Pair("pragma", "no-cache"), Pair("date", "TestTime")));

    EXPECT_EQ(outStr, expectedPostfix);
}

TEST(http_connection, AuthFailedCallsHandler)
{
    using namespace std::literals;
    boost::asio::io_context io;
    TestStream stream(io);
    TestStream out(io);
    stream.connect(out);

    // HTTP/2 request to /redfish/v1/Systems (not on allowlist, triggers auth
    // failure). Same framing as RequestPropogates but with a different :path
    // HPACK-encoded without Huffman.
    std::string_view toSend =
        // Client preface
        "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
        // Settings frame (18 byte payload)
        "\x00\x00\x12\x04\x00\x00\x00\x00\x00"
        "\x00\x03\x00\x00\x00\x64\x00\x04\x00\xa0\x00\x00\x00\x02\x00\x00"
        "\x00\x00"
        // Window update frame
        "\x00\x00\x04\x08\x00\x00\x00\x00\x00"
        "\x3e\x7f\x00\x01"
        // HEADERS frame: 51 bytes payload, END_STREAM | END_HEADERS, stream 1
        "\x00\x00\x33\x01\x05\x00\x00\x00\x01"
        // :method GET, :scheme https
        "\x82\x87"
        // :authority localhost:18080 (Huffman)
        "\x41\x8b\xa0\xe4\x1d\x13\x9d\x09\xb8\x17\x80\xf0\x3f"
        // :path /redfish/v1/Systems (literal, no Huffman)
        "\x04\x13/redfish/v1/Systems"
        // user-agent curl/8.5.0 (Huffman)
        "\x7a\x88\x25\xb6\x50\xc3\xcb\xb6\xb8\x3f"
        // accept */*
        "\x53\x03\x2a\x2f\x2a"sv;

    boost::asio::write(out, boost::asio::buffer(toSend));

    FakeHandler handler;
    std::function<std::string()> date(getDateStr);
    boost::asio::ssl::context sslCtx(boost::asio::ssl::context::tls_server);
    auto conn = std::make_shared<HTTP2Connection<TestStream, FakeHandler>>(
        boost::asio::ssl::stream<TestStream>(std::move(stream), sslCtx),
        &handler, date, HttpType::HTTP, nullptr);
    conn->start();

    // Settings/window prefix shared with RequestPropogates (first 8 elements)
    std::array<std::string_view, 8> expectedPrefix = {
        // Settings frame size 24
        "\x00\x00\x18\x04\x00\x00\x00\x00\x00"sv,
        // 4 max concurrent streams
        "\x00\x03\x00\x00\x00\x04"sv,
        // Enable push = false
        "\x00\x02\x00\x00\x00\x00"sv,
        // Max window size 1 << 20
        "\x00\x04\x00\x10\x00\x00"sv,
        // Max frame size 1 << 14
        "\x00\x05\x00\x00\x40\x00"sv,

        // Frame window update stream 0
        "\x00\x00\x04\x08\x00\x00\x00\x00\x00\x00\x0f\x00\x01"sv,

        // Settings ACK from server to client
        "\x00\x00\x00\x04\x01\x00\x00\x00\x00"sv,

        // Window update stream 1
        "\x00\x00\x04\x08\x00\x00\x00\x00\x01\x00\x07\x00\x01"sv,
    };

    size_t prefixTotalSize = 0;
    for (std::string_view s : expectedPrefix)
    {
        prefixTotalSize += s.size();
    }
    std::string prefixFlat;
    prefixFlat.reserve(prefixTotalSize);
    for (std::string_view s : expectedPrefix)
    {
        prefixFlat.append(s.data(), s.size());
    }

    // "AuthFailedResponse" = 18 bytes = 0x12
    std::string_view expectedPostfix =
        // Data Frame, Length 18, Stream 1, End Stream flag set
        "\x00\x00\x12\x00\x01\x00\x00\x00\x01"
        "AuthFailedResponse"sv;

    constexpr size_t frameHeaderLen = 9;

    // Run until we receive the expected amount of data. Once we can parse the
    // HEADERS frame length, adjust the target to include headers + data frame.
    std::string outStr;
    size_t headersLen = 0;
    size_t expectedTotal = prefixTotalSize + frameHeaderLen;

    while (outStr.size() < expectedTotal)
    {
        io.run_one();
        outStr = out.str();
        if (headersLen == 0 &&
            outStr.size() >= prefixTotalSize + frameHeaderLen)
        {
            // RFC 7540: 24-bit frame payload length (big-endian); use uint8_t
            // so signed char is not sign-extended.
            headersLen = (static_cast<size_t>(
                              static_cast<uint8_t>(outStr[prefixTotalSize]))
                          << 16) |
                         (static_cast<size_t>(
                              static_cast<uint8_t>(outStr[prefixTotalSize + 1]))
                          << 8) |
                         static_cast<size_t>(
                             static_cast<uint8_t>(outStr[prefixTotalSize + 2]));
            expectedTotal = prefixTotalSize + frameHeaderLen + headersLen +
                            expectedPostfix.size();
        }
    }
    EXPECT_TRUE(handler.authFailedCalled);

    // Verify settings prefix
    EXPECT_EQ(outStr.substr(0, prefixTotalSize), prefixFlat);
    outStr.erase(0, prefixTotalSize);

    // Verify HEADERS frame type and flags
    EXPECT_EQ(static_cast<uint8_t>(outStr[3]), 0x01); // type = HEADERS
    EXPECT_TRUE((static_cast<uint8_t>(outStr[4]) & 0x04) != 0); // END_HEADERS
    outStr.erase(0, frameHeaderLen);

    // Unpack and verify response headers
    std::vector<std::pair<std::string, std::string>> headers;
    unpackHeaders(outStr.substr(0, headersLen), headers);
    outStr.erase(0, headersLen);

    EXPECT_THAT(headers, ::testing::Contains(Pair(":status", "401")));
    EXPECT_THAT(headers, ::testing::Contains(Pair("content-length", "18")));
    EXPECT_THAT(headers, ::testing::Contains(Pair("date", "TestTime")));

    // Verify DATA frame with "AuthFailedResponse" body
    EXPECT_EQ(outStr, expectedPostfix);
}

} // namespace
} // namespace crow
