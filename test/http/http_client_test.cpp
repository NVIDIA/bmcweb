// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "http/http_body.hpp"
#include "http/http_client.hpp"
#include "ssl_key_handler.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <boost/asio/connect_pipe.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/asio/writable_pipe.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/url.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "gtest/gtest.h"

namespace crow
{
namespace
{

using HttpResponse = boost::beast::http::response<bmcweb::HttpBody>;

HttpResponse makeResponse(boost::beast::http::status status,
                          std::string_view contentType,
                          std::string_view contentLength)
{
    HttpResponse res;
    res.result(status);
    if (!contentType.empty())
    {
        res.set(boost::beast::http::field::content_type, contentType);
    }
    if (!contentLength.empty())
    {
        res.set(boost::beast::http::field::content_length, contentLength);
    }
    return res;
}

// parseStreamingContentLength tests

TEST(ParseStreamingContentLength, PresentAndValid)
{
    HttpResponse res =
        makeResponse(boost::beast::http::status::ok, "", "1234567");
    EXPECT_EQ(parseStreamingContentLength(res), 1234567U);
}

TEST(ParseStreamingContentLength, Missing)
{
    HttpResponse res;
    res.result(boost::beast::http::status::ok);
    EXPECT_EQ(parseStreamingContentLength(res), 0U);
}

TEST(ParseStreamingContentLength, NonNumericReturnsZero)
{
    HttpResponse res = makeResponse(boost::beast::http::status::ok, "", "abc");
    EXPECT_EQ(parseStreamingContentLength(res), 0U);
}

TEST(ParseStreamingContentLength, ZeroReturnsZero)
{
    HttpResponse res = makeResponse(boost::beast::http::status::ok, "", "0");
    EXPECT_EQ(parseStreamingContentLength(res), 0U);
}

// shouldStreamResponse tests

TEST(ShouldStreamResponse, BinaryResponseWithContentStreams)
{
    HttpResponse res = makeResponse(boost::beast::http::status::ok,
                                    "application/octet-stream", "1000000");
    EXPECT_TRUE(shouldStreamResponse(res, 1000000, false));
}

TEST(ShouldStreamResponse, JsonResponseIsBuffered)
{
    HttpResponse res = makeResponse(boost::beast::http::status::ok,
                                    "application/json", "1000");
    EXPECT_FALSE(shouldStreamResponse(res, 1000, false));
}

TEST(ShouldStreamResponse, JsonWithCharsetIsBuffered)
{
    HttpResponse res = makeResponse(boost::beast::http::status::ok,
                                    "application/json;charset=utf-8", "1000");
    EXPECT_FALSE(shouldStreamResponse(res, 1000, false));
}

TEST(ShouldStreamResponse, ZeroContentLengthIsBuffered)
{
    HttpResponse res = makeResponse(boost::beast::http::status::ok,
                                    "application/octet-stream", "0");
    EXPECT_FALSE(shouldStreamResponse(res, 0, false));
}

TEST(ShouldStreamResponse, NoContentStatusIsBuffered)
{
    HttpResponse res;
    res.result(boost::beast::http::status::no_content);
    EXPECT_FALSE(shouldStreamResponse(res, 0, false));
}

TEST(ShouldStreamResponse, InvalidResponseIsBuffered)
{
    HttpResponse res =
        makeResponse(boost::beast::http::status::internal_server_error,
                     "application/octet-stream", "1000");
    EXPECT_FALSE(shouldStreamResponse(res, 1000, /*responseIsInvalid=*/true));
}

TEST(ShouldStreamResponse, NoContentTypeWithBodyStreams)
{
    HttpResponse res =
        makeResponse(boost::beast::http::status::ok, "", "5000000");
    EXPECT_TRUE(shouldStreamResponse(res, 5000000, false));
}

// ============================================================
// Tests from Aishwary Joshi's review comments on MR !8824
// ============================================================

// BUG-5: upstream error silently delivers truncated data
// ---------------------------------------------------------------------------
// When the HMC TCP connection drops mid-transfer, afterStreamBodyRead receives
// a non-EOF error. The fix closes the write-end of the relay pipe immediately
// without flushing any buffered bytes. The Redfish client then receives EOF
// before Content-Length bytes and can detect the truncation — it is not
// silently delivered a corrupt, partial FDR dump.
//
// This test validates the pipe-level semantics that the fix relies on: closing
// the write end without flushing signals a short EOF to the reader.

TEST(StreamingPipeBug5, ClosingWritePipeWithoutFlush_SignalsShortEofToReader)
{
    boost::asio::io_context ioc;
    boost::asio::writable_pipe writePipe(ioc);
    boost::asio::readable_pipe readPipe(ioc);
    boost::system::error_code pipeEc;
    boost::asio::connect_pipe(readPipe, writePipe, pipeEc);
    ASSERT_FALSE(pipeEc);

    // Write partial data to simulate bytes flushed before the upstream error.
    const std::string partialData(128, 'X');
    boost::asio::write(writePipe, boost::asio::buffer(partialData), pipeEc);
    ASSERT_FALSE(pipeEc);

    // BUG-5 fix: close write end immediately; do NOT flush more bytes.
    writePipe.close();

    // Reader sees partial data followed by EOF — a short, detectable
    // truncation.
    std::string buf(1024, '\0');
    boost::system::error_code readEc;
    size_t n = boost::asio::read(readPipe, boost::asio::buffer(buf), readEc);

    EXPECT_EQ(readEc, boost::asio::error::eof);
    EXPECT_EQ(n, partialData.size());
}

// BUG-4: zero readLimit causes a tight async loop when the read buffer is full
// ---------------------------------------------------------------------------
// scheduleStreamBodyRawRead() computes:
//   readLimit = min(buffer.max_size() - buffer.size(), remaining)
// When the flat_static_buffer<httpReadBufferSize> is completely full,
// buffer.max_size() - buffer.size() == 0 and so readLimit == 0.
// Calling async_read_some with a zero-length buffer returns immediately,
// spinning until the 120-second stall timer kills the download.
//
// The fix posts() back to scheduleStreamBodyRead() when readLimit == 0 so the
// write side can drain the buffer before another read is attempted.
// This test documents the arithmetic that triggers the guard condition.

TEST(StreamingReadLimitBug4, FullBufferProducesZeroReadLimit)
{
    constexpr size_t bufferMaxSize = httpReadBufferSize;
    const size_t bufferUsed = bufferMaxSize; // completely occupied
    const size_t remaining = 1024UL * 1024;  // data still to receive

    const size_t readLimit = std::min(bufferMaxSize - bufferUsed, remaining);

    EXPECT_EQ(readLimit, 0U);
}

// BUG-1: handleReadHeaderError must follow the retry state machine
// ---------------------------------------------------------------------------
// Before the fix, handleReadHeaderError() called callback(false,...) directly
// without cancelling the receive timer or updating ConnState. When the
// 60-second timer later fired, waitAndRetry() ran with retryCount=0, scheduling
// a fresh doResolve() — but sendNext() (invoked from the callback) had already
// called doClose() + restartConnection(). The result was two concurrent
// doResolve() calls racing on the same ConnectionInfo.
//
// The fix mirrors afterRead(): cancel timer → state = recvFailed →
// waitAndRetry.
//
// Test: a loopback TCP acceptor sends "GARBAGE\r\n\r\n" for every connection.
// Beast's async_read_header fails with a parse error on each attempt.
// With maxRetryAttempts=1 the client makes two sequential connections (initial
// + one retry), then fires the callback exactly once with bad_gateway.
// connectionCount == 2 also proves no concurrent doResolve() was spawned
// (the old bug would produce an extra connection from the stale timer firing).

TEST(HttpClientBug1, MalformedHmcHeader_CallbackOnceAtRetryExhaustion)
{
    boost::asio::io_context ioc;

    // --- Fake HMC: for every TCP connection send garbage then close ---
    boost::asio::ip::tcp::acceptor acceptor(
        ioc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
    int connectionCount = 0;

    std::function<void()> doAccept = [&]() {
        auto sock = std::make_shared<boost::asio::ip::tcp::socket>(ioc);
        acceptor.async_accept(*sock, [&, sock](boost::system::error_code ec) {
            if (ec)
            {
                return;
            }
            ++connectionCount;
            auto garbage = std::make_shared<std::string>("GARBAGE\r\n\r\n");
            boost::asio::async_write(
                *sock, boost::asio::buffer(*garbage),
                [sock, garbage](boost::system::error_code, std::size_t) {
                    sock->close();
                });
            doAccept();
        });
    };
    doAccept();

    uint16_t port = acceptor.local_endpoint().port();
    boost::urls::url destUrl(
        "http://127.0.0.1:" + std::to_string(port) + "/fdr");

    auto policy = std::make_shared<ConnectionPolicy>();
    policy->maxRetryAttempts = 1;
    policy->retryIntervalSecs = std::chrono::seconds(0);
    policy->retryPolicyAction = "TerminateAfterRetries";

    HttpClient client(ioc, policy);

    int callbackCount = 0;
    boost::beast::http::status callbackStatus = boost::beast::http::status::ok;

    boost::beast::http::fields headers;
    client.sendDataWithCallback(
        "", destUrl, ensuressl::VerifyCertificate::NoVerify, headers,
        boost::beast::http::verb::get, [&](Response& res) {
            ++callbackCount;
            callbackStatus = res.result();
            acceptor.close();
            ioc.stop();
        });

    ioc.run();

    EXPECT_EQ(callbackCount, 1);
    EXPECT_EQ(callbackStatus, boost::beast::http::status::bad_gateway);
    EXPECT_EQ(connectionCount, 2); // initial + 1 retry, never concurrent
}

// ============================================================
// HMC mid-stream truncation: stall / TCP-drop scenarios
// ============================================================

// Helper: dup the pipe fd from a streaming Response and clear O_NONBLOCK so
// it can be read with blocking read() after ioc.run() drains.
// Must be called inside the sendDataWithCallback resHandler before it returns
// (openStreamFdAndStart calls res.clear() immediately after the callback).
static int dupStreamFd(Response& res)
{
    int raw = res.response.body().file().native_handle();
    if (raw < 0)
    {
        return -1;
    }
    int fd = ::dup(raw);
    if (fd < 0)
    {
        return -1;
    }
    int flags = ::fcntl(fd, F_GETFL);
    ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    return fd;
}

// Helper: drain a blocking fd to a string until EOF.
static std::string drainFd(int fd)
{
    std::string out;
    char buf[4096];
    for (;;)
    {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0)
        {
            break;
        }
        out.append(buf, static_cast<size_t>(n));
    }
    return out;
}

// Helper: build a minimal HTTP/1.1 200 streaming response header.
static std::string makeHmcHeader(size_t contentLength)
{
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: application/octet-stream\r\n"
           "Content-Length: " +
           std::to_string(contentLength) + "\r\n\r\n";
}

// TCP-drop path (fast, < 1 s)
// ---------------------------------------------------------------------------
// HMC sends a valid header claiming claimedSize bytes, writes sentSize bytes
// of body, then closes the TCP socket mid-transfer.
//
// Code path exercised:
//   async_read_some → afterStreamBodyRawRead → afterStreamBodyRead
//   (hadEof=true, done=false) → writeChunkToPipe → afterChunkWrite
//   (hadEof=true) → streaming.reset() closes write-end of relay pipe →
//   onRelayDone() unblocks sendNext().
//
// The relay pipe's write-end closes before contentLength bytes are written,
// so the downstream reader receives a short EOF it can detect rather than
// a silent corruption or an indefinite hang.
//
// sentSize is chosen to fit inside the relay pipe buffer (kernel default
// ~64 KiB; expanded to 1 MiB by F_SETPIPE_SZ when permitted).
// httpReadBufferSize (32 KiB) fits in either case.

TEST(HmcTruncation, TcpDropMidStream_PipeGivesShortEof)
{
    boost::asio::io_context ioc;

    constexpr size_t sentSize = httpReadBufferSize;
    constexpr size_t claimedSize = sentSize * 2;

    boost::asio::ip::tcp::acceptor acceptor(
        ioc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));

    auto doHmcSession = [&]() {
        auto sock = std::make_shared<boost::asio::ip::tcp::socket>(ioc);
        acceptor.async_accept(*sock, [&, sock](boost::system::error_code ec) {
            if (ec)
            {
                return;
            }
            auto hdr =
                std::make_shared<std::string>(makeHmcHeader(claimedSize));
            boost::asio::async_write(
                *sock, boost::asio::buffer(*hdr),
                [&, sock, hdr](boost::system::error_code, std::size_t) {
                    auto body = std::make_shared<std::string>(sentSize, 'X');
                    boost::asio::async_write(
                        *sock, boost::asio::buffer(*body),
                        [&acceptor, sock,
                         body](boost::system::error_code, std::size_t) {
                            sock->close();    // TCP drop mid-transfer
                            acceptor.close(); // no further connections
                        });
                });
        });
    };
    doHmcSession();

    uint16_t port = acceptor.local_endpoint().port();
    boost::urls::url destUrl(
        "http://127.0.0.1:" + std::to_string(port) + "/fdr");

    auto policy = std::make_shared<ConnectionPolicy>();
    policy->maxRetryAttempts = 0;
    policy->retryIntervalSecs = std::chrono::seconds(0);
    policy->retryPolicyAction = "TerminateAfterRetries";

    HttpClient client(ioc, policy);
    int capturedFd = -1;

    boost::beast::http::fields hdrs;
    client.sendDataWithCallback(
        "", destUrl, ensuressl::VerifyCertificate::NoVerify, hdrs,
        boost::beast::http::verb::get, [&](Response& res) {
            // Fires when headers arrive + pipe is ready, before body data.
            // Do NOT stop ioc here; let body streaming run to completion.
            capturedFd = dupStreamFd(res);
        });

    ioc.run(); // returns after streaming finishes + connection closes

    ASSERT_GE(capturedFd, 0) << "header callback did not fire";

    std::string received = drainFd(capturedFd);
    ::close(capturedFd);

    EXPECT_EQ(received.size(), sentSize);    // all sent bytes arrived
    EXPECT_LT(received.size(), claimedSize); // short of Content-Length
}

// Stall-timer path (DISABLED — requires ~120 s real time)
// ---------------------------------------------------------------------------
// HMC sends 1 MiB of body then holds the TCP socket open without sending
// more data.  The chunk-stall timer in http_client.hpp fires after 120 s:
//
//   onChunkStallTimeout → streaming.reset() → writePipe RAII-closes →
//   onRelayDone() unblocks sendNext().
//
// The downstream reader receives a short EOF (however many bytes fit in the
// relay pipe before the timer fired) rather than blocking indefinitely.
//
// The stall timeout is hardcoded at 120 s; making it configurable via
// ConnectionPolicy would require a source change.  This test is DISABLED so
// normal CI runs skip it.  Enable with:
//
//   --gtest_also_run_disabled_tests \
//   --gtest_filter=HmcTruncation.DISABLED_StallTimer120s_Integration

TEST(HmcTruncation, DISABLED_StallTimer120s_Integration)
{
    boost::asio::io_context ioc;

    constexpr size_t sentSize = 1024UL * 1024; // 1 MiB
    constexpr size_t claimedSize = sentSize * 2;

    boost::asio::ip::tcp::acceptor acceptor(
        ioc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));

    // stallSock is kept alive for the duration of the test so the TCP
    // connection stays open after the body bytes are written.
    auto stallSock = std::make_shared<boost::asio::ip::tcp::socket>(ioc);

    acceptor.async_accept(*stallSock, [&, stallSock](
                                          boost::system::error_code ec) {
        if (ec)
        {
            return;
        }
        auto hdr = std::make_shared<std::string>(makeHmcHeader(claimedSize));
        boost::asio::async_write(
            *stallSock, boost::asio::buffer(*hdr),
            [&, stallSock, hdr](boost::system::error_code, std::size_t) {
                auto body = std::make_shared<std::string>(sentSize, 'X');
                boost::asio::async_write(
                    *stallSock, boost::asio::buffer(*body),
                    [&acceptor, stallSock,
                     body](boost::system::error_code, std::size_t) {
                        // Body written; hold socket open to force stall.
                        // Do NOT close stallSock here.
                        acceptor.close();
                    });
            });
    });

    uint16_t port = acceptor.local_endpoint().port();
    boost::urls::url destUrl(
        "http://127.0.0.1:" + std::to_string(port) + "/fdr");

    auto policy = std::make_shared<ConnectionPolicy>();
    policy->maxRetryAttempts = 0;
    policy->retryIntervalSecs = std::chrono::seconds(0);
    policy->retryPolicyAction = "TerminateAfterRetries";

    HttpClient client(ioc, policy);
    int capturedFd = -1;

    boost::beast::http::fields hdrs;
    client.sendDataWithCallback(
        "", destUrl, ensuressl::VerifyCertificate::NoVerify, hdrs,
        boost::beast::http::verb::get,
        [&](Response& res) { capturedFd = dupStreamFd(res); });

    // Run for 135 s to allow the 120-second chunk-stall timer to fire.
    ioc.run_for(std::chrono::seconds(135));

    stallSock->close(); // release after ioc

    ASSERT_GE(capturedFd, 0) << "header callback did not fire";

    std::string received = drainFd(capturedFd);
    ::close(capturedFd);

    // However many bytes reached the pipe before the timer fired, the
    // transfer must be truncated relative to the claimed Content-Length.
    EXPECT_LT(received.size(), claimedSize);
}

} // namespace
} // namespace crow
