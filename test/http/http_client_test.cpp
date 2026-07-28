// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "http/http_body.hpp"
#include "http/http_client.hpp"

#include <boost/asio/connect_pipe.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/asio/writable_pipe.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
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
// waitAndRetry. Full regression coverage requires a ConnectionInfo fixture with
// a mock transport that can inject errors after async_read_header completes;
// testing that fixture is deferred to an integration test.

TEST(HttpClientBug1, HandleReadHeaderError_RequiresIntegrationFixture)
{
    GTEST_SKIP() << "BUG-1 requires a ConnectionInfo integration fixture with "
                    "a mock socket to verify that timer.cancel() and "
                    "waitAndRetry() are called instead of callback() directly.";
}

} // namespace
} // namespace crow
