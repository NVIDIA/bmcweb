// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "async_resp.hpp"
#include "http/http2_connection.hpp"
#include "http/http_connection.hpp"
#include "http/http_request.hpp"
#include "http/http_response.hpp"
#include "http_connect_types.hpp"
#include "multipart_parser.hpp"
#include "test_stream.hpp"

#include <nghttp2/nghttp2.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/_experimental/test/stream.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "gtest/gtest.h"
namespace crow
{

struct FakeHandler
{
    template <typename Adaptor>
    static void handleUpgrade(
        const std::shared_ptr<Request>& /*req*/,
        const std::shared_ptr<bmcweb::AsyncResp>& /*asyncResp*/,
        Adaptor&& /*adaptor*/)
    {
        // Handle Upgrade should never be called
        EXPECT_FALSE(true);
    }

    void handleHeaders(const std::shared_ptr<Request>& /*req*/,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       std::move_only_function<void()> headersCompleteCallback)
    {
        handleHeadersCalled = true;
        if (rejectHeaders)
        {
            asyncResp->res.result(boost::beast::http::status::bad_request);
            asyncResp->res.write("HeadersRejectedResponse");
        }
        headersCompleteCallback();
    }

    void handle(const std::shared_ptr<Request>& req,
                const std::shared_ptr<bmcweb::AsyncResp>& /*asyncResp*/)
    {
        EXPECT_EQ(req->method(), boost::beast::http::verb::get);
        EXPECT_EQ(req->target(), "/");
        EXPECT_EQ(req->getHeaderValue(boost::beast::http::field::host),
                  "openbmc_project.xyz");
        EXPECT_FALSE(req->keepAlive());
        EXPECT_EQ(req->version(), 11);
        EXPECT_EQ(req->body(), "Hello, World!");

        called = true;
    }

    bool handleAuthFailed(const std::shared_ptr<Request>& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
    {
        authFailedTarget = req->target();
        asyncResp->res.result(boost::beast::http::status::unauthorized);
        asyncResp->res.addHeader(boost::beast::http::field::www_authenticate,
                                 "Basic");
        asyncResp->res.write("AuthFailedResponseString");
        authFailedCalled = true;
        return true;
    }

    bool called = false;
    bool authFailedCalled = false;
    bool handleHeadersCalled = false;
    bool rejectHeaders = false;
    std::string authFailedTarget;
};

struct ClockFake
{
    bool wascalled = false;
    std::string getDateStr()
    {
        wascalled = true;
        return "TestTime";
    }
};

TEST(http_connection, RequestPropogates)
{
    boost::asio::io_context io;
    ClockFake clock;
    TestStream stream(io);
    TestStream out(io);
    stream.connect(out);

    out.write_some(boost::asio::buffer(
        "GET / HTTP/1.1\r\n"
        "Host: openbmc_project.xyz\r\n"
        "Connection: close\r\n"
        "Content-Length: 13\r\n\r\n"
        "Hello, World!"));
    FakeHandler handler;
    boost::asio::steady_timer timer(io);
    std::function<std::string()> date(
        std::bind_front(&ClockFake::getDateStr, &clock));

    boost::asio::ssl::context context{boost::asio::ssl::context::tls};
    std::shared_ptr<crow::Connection<TestStream, FakeHandler>> conn =
        std::make_shared<crow::Connection<TestStream, FakeHandler>>(
            &handler, HttpType::HTTP, std::move(timer), date,
            boost::asio::ssl::stream<TestStream>(std::move(stream), context));
    conn->disableAuth();
    conn->start();

    std::string expected =
        "HTTP/1.1 200 OK\r\n"
        "Connection: close\r\n"
        "Strict-Transport-Security: max-age=31536000; includeSubdomains\r\n"
        "Pragma: no-cache\r\n"
        "Cache-Control: no-store, max-age=0\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Date: TestTime\r\n"
        "Content-Length: 0\r\n\r\n";
    const size_t expectedTotal = expected.size();

    std::string outStr;
    while (outStr.size() < expectedTotal)
    {
        io.run_one();
        outStr = out.str();
    }
    EXPECT_TRUE(handler.called);
    EXPECT_TRUE(handler.handleHeadersCalled);
    EXPECT_EQ(outStr, expected);
    EXPECT_TRUE(clock.wascalled);
}

TEST(http_connection, AuthFailedCallsHandler)
{
    boost::asio::io_context io;
    ClockFake clock;
    TestStream stream(io);
    TestStream out(io);
    stream.connect(out);

    out.write_some(boost::asio::buffer("GET /redfish/v1/Systems HTTP/1.1\r\n"
                                       "Host: openbmc_project.xyz\r\n"
                                       "Connection: close\r\n\r\n"));

    FakeHandler handler;
    boost::asio::steady_timer timer(io);
    std::function<std::string()> date(
        std::bind_front(&ClockFake::getDateStr, &clock));

    boost::asio::ssl::context context{boost::asio::ssl::context::tls};
    std::shared_ptr<crow::Connection<TestStream, FakeHandler>> conn =
        std::make_shared<crow::Connection<TestStream, FakeHandler>>(
            &handler, HttpType::HTTP, std::move(timer), date,
            boost::asio::ssl::stream<TestStream>(std::move(stream), context));
    conn->start();

    std::string expected =
        "HTTP/1.1 401 Unauthorized\r\n"
        "WWW-Authenticate: Basic\r\n"
        "Connection: close\r\n"
        "Strict-Transport-Security: max-age=31536000; includeSubdomains\r\n"
        "Pragma: no-cache\r\n"
        "Cache-Control: no-store, max-age=0\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Date: TestTime\r\n"
        "Content-Length: 24\r\n\r\n"
        "AuthFailedResponseString";
    const size_t expectedTotal = expected.size();

    std::string outStr;
    while (outStr.size() < expectedTotal)
    {
        io.run_one();
        outStr = out.str();
    }
    EXPECT_TRUE(handler.authFailedCalled);
    EXPECT_FALSE(handler.handleHeadersCalled);
    EXPECT_EQ(handler.authFailedTarget, "/redfish/v1/Systems");
    EXPECT_EQ(outStr, expected);
    EXPECT_TRUE(clock.wascalled);
}

TEST(http_connection, AuthFailedBeforeStreamingRequestHandler)
{
    boost::asio::io_context io;
    ClockFake clock;
    TestStream stream(io);
    TestStream out(io);
    stream.connect(out);

    out.write_some(boost::asio::buffer(
        "POST /redfish/v1/UpdateService/update-multipart/ HTTP/1.1\r\n"
        "Host: openbmc_project.xyz\r\n"
        "Connection: close\r\n"
        "Content-Type: multipart/form-data; boundary=x\r\n"
        "Content-Length: 1\r\n\r\n"
        "x"));

    FakeHandler handler;
    boost::asio::steady_timer timer(io);
    std::function<std::string()> date(
        std::bind_front(&ClockFake::getDateStr, &clock));

    boost::asio::ssl::context context{boost::asio::ssl::context::tls};
    std::shared_ptr<crow::Connection<TestStream, FakeHandler>> conn =
        std::make_shared<crow::Connection<TestStream, FakeHandler>>(
            &handler, HttpType::HTTP, std::move(timer), date,
            boost::asio::ssl::stream<TestStream>(std::move(stream), context));
    conn->start();

    std::string expected =
        "HTTP/1.1 401 Unauthorized\r\n"
        "WWW-Authenticate: Basic\r\n"
        "Connection: close\r\n"
        "Strict-Transport-Security: max-age=31536000; includeSubdomains\r\n"
        "Pragma: no-cache\r\n"
        "Cache-Control: no-store, max-age=0\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Date: TestTime\r\n"
        "Content-Length: 24\r\n\r\n"
        "AuthFailedResponseString";
    const size_t expectedTotal = expected.size();

    std::string outStr;
    while (outStr.size() < expectedTotal)
    {
        io.run_one();
        outStr = out.str();
    }
    EXPECT_TRUE(handler.authFailedCalled);
    EXPECT_FALSE(handler.handleHeadersCalled);
    EXPECT_EQ(handler.authFailedTarget,
              "/redfish/v1/UpdateService/update-multipart/");
    EXPECT_EQ(outStr, expected);
    EXPECT_TRUE(clock.wascalled);
}

TEST(http_connection, RejectedHeadersReturnResponseBeforeBodyRead)
{
    boost::asio::io_context io;
    ClockFake clock;
    TestStream stream(io);
    TestStream out(io);
    stream.connect(out);

    out.write_some(boost::asio::buffer(
        "POST /redfish/v1/UpdateService/update-multipart/ HTTP/1.1\r\n"
        "Host: openbmc_project.xyz\r\n"
        "Connection: keep-alive\r\n"
        "Content-Type: multipart/form-data; boundary=x\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Expect: 100-continue\r\n\r\n"
        "1\r\nx\r\n0\r\n\r\n"));

    FakeHandler handler;
    handler.rejectHeaders = true;
    boost::asio::steady_timer timer(io);
    std::function<std::string()> date(
        std::bind_front(&ClockFake::getDateStr, &clock));

    boost::asio::ssl::context context{boost::asio::ssl::context::tls};
    std::shared_ptr<crow::Connection<TestStream, FakeHandler>> conn =
        std::make_shared<crow::Connection<TestStream, FakeHandler>>(
            &handler, HttpType::HTTP, std::move(timer), date,
            boost::asio::ssl::stream<TestStream>(std::move(stream), context));
    conn->disableAuth();
    conn->start();

    std::string expected =
        "HTTP/1.1 400 Bad Request\r\n"
        "Connection: close\r\n"
        "Strict-Transport-Security: max-age=31536000; includeSubdomains\r\n"
        "Pragma: no-cache\r\n"
        "Cache-Control: no-store, max-age=0\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Date: TestTime\r\n"
        "Content-Length: 23\r\n\r\n"
        "HeadersRejectedResponse";
    const size_t expectedTotal = expected.size();

    std::string outStr;
    for (size_t operation = 0; operation < 20 && outStr.size() < expectedTotal;
         operation++)
    {
        if (io.run_one() == 0)
        {
            break;
        }
        outStr = out.str();
    }
    EXPECT_FALSE(handler.called);
    EXPECT_TRUE(handler.handleHeadersCalled);
    EXPECT_EQ(outStr, expected);
    EXPECT_TRUE(clock.wascalled);
}

namespace
{

struct Http2RejectedStreamHandler
{
    void rejectStream(const std::shared_ptr<Request>& req,
                      const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
    {
        req->streamInputRoute = true;

        MultipartParserStreamingCallbacks callbacks;
        callbacks.onDataAvailable = [this](std::string_view /*data*/) {
            bodyDispatchCount++;
        };
        callbacks.onParseComplete = [this]() { updateStartCount++; };
        req->req.body().setMultipartParserCallbacks(std::move(callbacks));

        asyncResp->res.result(boost::beast::http::status::forbidden);
        asyncResp->res.write("HeadersRejectedResponse");
    }

    void handleHeaders(const std::shared_ptr<Request>& req,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       std::move_only_function<void()> onValidationDone)
    {
        headersCalled++;
        rejectStream(req, asyncResp);
        onValidationDone();
    }

    bool handleAuthFailed(const std::shared_ptr<Request>& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
    {
        authFailedCalled++;
        rejectStream(req, asyncResp);
        return true;
    }

    void handle(const std::shared_ptr<Request>& /*req*/,
                const std::shared_ptr<bmcweb::AsyncResp>& /*asyncResp*/)
    {
        updateStartCount++;
    }

    size_t headersCalled = 0;
    size_t authFailedCalled = 0;
    size_t bodyDispatchCount = 0;
    size_t updateStartCount = 0;
};

void appendHttp2Frame(std::string& output, uint8_t type, uint8_t flags,
                      uint32_t streamId, std::string_view payload)
{
    const uint32_t length = static_cast<uint32_t>(payload.size());
    output.push_back(static_cast<char>((length >> 16U) & 0xffU));
    output.push_back(static_cast<char>((length >> 8U) & 0xffU));
    output.push_back(static_cast<char>(length & 0xffU));
    output.push_back(static_cast<char>(type));
    output.push_back(static_cast<char>(flags));
    output.push_back(static_cast<char>((streamId >> 24U) & 0x7fU));
    output.push_back(static_cast<char>((streamId >> 16U) & 0xffU));
    output.push_back(static_cast<char>((streamId >> 8U) & 0xffU));
    output.push_back(static_cast<char>(streamId & 0xffU));
    output.append(payload);
}

std::string makeHttp2StreamingRequest(std::string_view path)
{
    std::string request = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    appendHttp2Frame(request, NGHTTP2_SETTINGS, NGHTTP2_FLAG_NONE, 0, {});

    std::string headers;
    headers.push_back(static_cast<char>(0x83)); // :method POST
    headers.push_back(static_cast<char>(0x87)); // :scheme https
    headers.push_back(static_cast<char>(0x01)); // :authority, literal
    constexpr std::string_view authority = "localhost";
    headers.push_back(static_cast<char>(authority.size()));
    headers.append(authority);
    headers.push_back(static_cast<char>(0x04)); // :path, literal
    headers.push_back(static_cast<char>(path.size()));
    headers.append(path);
    appendHttp2Frame(request, NGHTTP2_HEADERS, NGHTTP2_FLAG_END_HEADERS, 1,
                     headers);
    appendHttp2Frame(request, NGHTTP2_DATA, NGHTTP2_FLAG_END_STREAM, 1,
                     "multipart-body");
    return request;
}

struct Http2ResponseFrames
{
    size_t headers = 0;
    size_t final = 0;
    std::string body;
};

uint32_t readHttp2Uint24(std::string_view input)
{
    return (static_cast<uint32_t>(static_cast<uint8_t>(input[0])) << 16U) |
           (static_cast<uint32_t>(static_cast<uint8_t>(input[1])) << 8U) |
           static_cast<uint32_t>(static_cast<uint8_t>(input[2]));
}

uint32_t readHttp2StreamId(std::string_view input)
{
    return (static_cast<uint32_t>(static_cast<uint8_t>(input[0]) & 0x7fU)
            << 24U) |
           (static_cast<uint32_t>(static_cast<uint8_t>(input[1])) << 16U) |
           (static_cast<uint32_t>(static_cast<uint8_t>(input[2])) << 8U) |
           static_cast<uint32_t>(static_cast<uint8_t>(input[3]));
}

Http2ResponseFrames parseHttp2ResponseFrames(std::string_view wireData)
{
    Http2ResponseFrames response;
    constexpr size_t frameHeaderSize = 9;
    while (wireData.size() >= frameHeaderSize)
    {
        const uint32_t length = readHttp2Uint24(wireData);
        if (wireData.size() < frameHeaderSize + length)
        {
            break;
        }
        const uint8_t type = static_cast<uint8_t>(wireData[3]);
        const uint8_t flags = static_cast<uint8_t>(wireData[4]);
        const uint32_t streamId = readHttp2StreamId(wireData.substr(5, 4));
        const std::string_view payload =
            wireData.substr(frameHeaderSize, length);
        if (streamId == 1)
        {
            if (type == NGHTTP2_HEADERS)
            {
                response.headers++;
            }
            if (type == NGHTTP2_DATA)
            {
                response.body.append(payload);
            }
            if ((flags & NGHTTP2_FLAG_END_STREAM) != 0)
            {
                response.final++;
            }
        }
        wireData.remove_prefix(frameHeaderSize + length);
    }
    return response;
}

TEST(Http2Connection, RejectedStreamingHeadersSendOneResponse)
{
    boost::asio::io_context io;
    TestStream stream(io);
    TestStream output(io);
    stream.connect(output);

    const std::string request =
        makeHttp2StreamingRequest("/redfish/v1/SessionService/Sessions");
    boost::asio::write(output, boost::asio::buffer(request));

    Http2RejectedStreamHandler handler;
    std::function<std::string()> date([]() { return "TestTime"; });
    boost::asio::ssl::context sslCtx(boost::asio::ssl::context::tls_server);
    auto conn = std::make_shared<
        HTTP2Connection<TestStream, Http2RejectedStreamHandler>>(
        boost::asio::ssl::stream<TestStream>(std::move(stream), sslCtx),
        &handler, date, HttpType::HTTP, nullptr);
    conn->start();

    Http2ResponseFrames frames;
    for (size_t operation = 0; operation < 100 && frames.final == 0;
         operation++)
    {
        if (io.run_one() == 0)
        {
            break;
        }
        frames = parseHttp2ResponseFrames(output.str());
    }

    ASSERT_EQ(handler.authFailedCalled, 0U);
    EXPECT_EQ(handler.headersCalled, 1U);
    EXPECT_EQ(handler.bodyDispatchCount, 0U);
    EXPECT_EQ(handler.updateStartCount, 0U);
    EXPECT_EQ(frames.headers, 1U);
    EXPECT_EQ(frames.final, 1U);
    EXPECT_EQ(frames.body, "HeadersRejectedResponse");

    conn->close();
}

TEST(Http2Connection, UnauthenticatedStreamingHeadersSendOneResponse)
{
    boost::asio::io_context io;
    TestStream stream(io);
    TestStream output(io);
    stream.connect(output);

    const std::string request = makeHttp2StreamingRequest(
        "/redfish/v1/UpdateService/update-multipart/");
    boost::asio::write(output, boost::asio::buffer(request));

    Http2RejectedStreamHandler handler;
    std::function<std::string()> date([]() { return "TestTime"; });
    boost::asio::ssl::context sslCtx(boost::asio::ssl::context::tls_server);
    auto conn = std::make_shared<
        HTTP2Connection<TestStream, Http2RejectedStreamHandler>>(
        boost::asio::ssl::stream<TestStream>(std::move(stream), sslCtx),
        &handler, date, HttpType::HTTP, nullptr);
    conn->start();

    Http2ResponseFrames frames;
    for (size_t operation = 0; operation < 100 && frames.final == 0;
         operation++)
    {
        if (io.run_one() == 0)
        {
            break;
        }
        frames = parseHttp2ResponseFrames(output.str());
    }

    ASSERT_EQ(handler.authFailedCalled, 1U);
    EXPECT_EQ(handler.headersCalled, 0U);
    EXPECT_EQ(handler.bodyDispatchCount, 0U);
    EXPECT_EQ(handler.updateStartCount, 0U);
    EXPECT_EQ(frames.headers, 1U);
    EXPECT_EQ(frames.final, 1U);
    EXPECT_EQ(frames.body, "HeadersRejectedResponse");

    conn->close();
}

} // namespace

} // namespace crow
