// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "async_resp.hpp"
#include "http/http_connection.hpp"
#include "http/http_request.hpp"
#include "http/http_response.hpp"
#include "http_connect_types.hpp"
#include "test_stream.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/_experimental/test/stream.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>

#include <functional>
#include <memory>
#include <string>
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
        headersAsyncResp = asyncResp;
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
        EXPECT_EQ(req->target(), "/redfish/v1/Systems");
        authFailedUsedHeadersAsyncResp = headersAsyncResp.lock() == asyncResp;
        asyncResp->res.result(boost::beast::http::status::unauthorized);
        asyncResp->res.addHeader(boost::beast::http::field::www_authenticate,
                                 "Basic");
        asyncResp->res.write("AuthFailedResponseString");
        authFailedCalled = true;
        return true;
    }

    bool called = false;
    bool authFailedCalled = false;
    bool authFailedUsedHeadersAsyncResp = false;
    bool handleHeadersCalled = false;
    bool rejectHeaders = false;
    std::weak_ptr<bmcweb::AsyncResp> headersAsyncResp;
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
    EXPECT_TRUE(handler.authFailedUsedHeadersAsyncResp);
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

} // namespace crow
