// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright NVIDIA
//
// NVIDIA code for streaming: streaming-response relay for the FDR dump download
// proxy. Extracted from http_client.hpp so the bulk of the FDR streaming logic
// lives in an NVIDIA-owned file and does not collide on upstream syncs.
#pragma once

#include "http_body.hpp"
#include "http_response.hpp"
#include "logging.hpp"
#include "parsing.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/connect_pipe.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/writable_pipe.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/flat_static_buffer.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/url.hpp>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace crow
{
namespace http = boost::beast::http;

// Header-first receive routing for the FDR proxy. http_client reads the
// upstream response headers first, then asks these predicates whether the body
// should be buffered as JSON (the normal aggregator path) or streamed through a
// kernel pipe (the StreamingResponseRelay path). Pulled out of ConnectionInfo
// so the routing policy lives in an NVIDIA-owned file alongside the relay it
// feeds.

// Parse Content-Length, returning 0 when absent or unparseable (caller treats 0
// as "no body to stream" / fall back to chunked).
inline size_t parseStreamingContentLength(
    const http::response<bmcweb::HttpBody>& response)
{
    auto contentLengthIt =
        response.find(boost::beast::http::field::content_length);
    if (contentLengthIt == response.end())
    {
        return 0;
    }
    std::string_view val = contentLengthIt->value();
    size_t parsed = 0;
    auto [ptr, parseEc] = std::from_chars(val.begin(), val.end(), parsed);
    if (parseEc != std::errc{})
    {
        BMCWEB_LOG_WARNING(
            "afterReadHeader: failed to parse Content-Length '{}', "
            "falling back to chunked transfer",
            val);
        return 0;
    }
    return parsed;
}

inline bool isJsonResponse(const http::response<bmcweb::HttpBody>& response)
{
    auto contentTypeIt = response.find(boost::beast::http::field::content_type);
    if (contentTypeIt == response.end())
    {
        return false;
    }
    return isJsonContentType(contentTypeIt->value());
}

// Decide whether this upstream response should be streamed. JSON bodies and
// bodyless replies (4xx/5xx errors, 204 No Content, HEAD, 200 with
// Content-Length: 0) stay on the buffered path: routing them through the pipe
// would spin empty until the chunk-stall timer fires, bypassing the
// retry/error policy. `responseIsInvalid` is connPolicy->invalidResp() for the
// status code, supplied by the caller so this stays free of ConnectionPolicy.
inline bool shouldStreamResponse(
    const http::response<bmcweb::HttpBody>& response, size_t contentLength,
    bool responseIsInvalid)
{
    if (isJsonResponse(response))
    {
        return false;
    }
    if (responseIsInvalid ||
        response.result() == boost::beast::http::status::no_content ||
        contentLength == 0)
    {
        return false;
    }
    return true;
}

// Owns the streaming-response path: pipe pair, kernel pipe buffer sizing,
// chunked/raw read→write→re-arm loop, per-chunk stall timer, wall-clock
// streaming deadline, and pipe close. ConnectionInfo composes a relay on
// demand in afterReadHeader() when the response is non-JSON with a body,
// so the buffered JSON path stays decoupled from streaming concerns.
//
// Templated on the read-buffer size so this header carries no dependency on
// http_client.hpp's httpReadBufferSize constant; ConnectionInfo instantiates
// StreamingResponseRelay<httpReadBufferSize> so the buffer reference binds to
// the same flat_static_buffer<N> type it owns.
template <unsigned int BufferSize>
class StreamingResponseRelay :
    public std::enable_shared_from_this<StreamingResponseRelay<BufferSize>>
{
  public:
    using parser_type = http::response_parser<bmcweb::HttpBody>;

    // Wall-clock cap on streaming so a slow Redfish client cannot pin BMC
    // pipe + send buffers + a downstream socket indefinitely. Stamped on
    // the response body so the downstream HTTP/1.1 and HTTP/2
    // streamAbortTimer expire on the same time_point.
    static constexpr std::chrono::minutes streamingDeadlineDuration{15};

    StreamingResponseRelay(
        boost::asio::io_context& iocIn, parser_type& parserIn,
        boost::beast::flat_static_buffer<BufferSize>& bufferIn,
        boost::asio::ip::tcp::socket& connIn,
        std::optional<boost::asio::ssl::stream<boost::asio::ip::tcp::socket&>>&
            sslConnIn,
        Response& resIn, const boost::urls::url& hostIn, size_t contentLengthIn,
        std::function<void(bool)> onSetupResultIn,
        std::function<void()> onCompleteIn) :
        ioc(iocIn), parser(parserIn), buffer(bufferIn), conn(connIn),
        sslConn(sslConnIn), res(resIn), host(hostIn),
        content_length(contentLengthIn),
        onSetupResult(std::move(onSetupResultIn)),
        onComplete(std::move(onCompleteIn)), chunkStallTimer(iocIn),
        streamingDeadline(iocIn)
    {}

    StreamingResponseRelay(const StreamingResponseRelay&) = delete;
    StreamingResponseRelay& operator=(const StreamingResponseRelay&) = delete;
    StreamingResponseRelay(StreamingResponseRelay&&) = delete;
    StreamingResponseRelay& operator=(StreamingResponseRelay&&) = delete;
    ~StreamingResponseRelay() = default;

    // True while a streaming pipe is open. The pool uses this (via
    // ConnectionInfo::isBusyStreaming) to decide whether sendNext can run
    // immediately or must defer to onComplete.
    bool isActive() const
    {
        return writePipe.has_value();
    }

    // Begin streaming.
    //   On setup success: fires onSetupResult(true) and schedules the
    //   first body read; onComplete will fire later when the stream ends.
    //   On setup failure: fires onSetupResult(false) only — no onComplete.
    //   The caller's onSetupResult(false) handler is the terminal step
    //   (shutdown the connection); the pool must not attempt reuse.
    void start(const http::response<bmcweb::HttpBody>& response)
    {
        copyResponseHeaders(response);
        if (!createStreamPipe())
        {
            onSetupResult(false);
            return;
        }
        if (!openFdAndFireStart())
        {
            // openFdAndFireStart already fired onSetupResult(false).
            return;
        }
        resetChunkStallTimer();
        startStreamingDeadline();
        scheduleStreamRead();
    }

  private:
    void copyResponseHeaders(const http::response<bmcweb::HttpBody>& response)
    {
        res.response.result(response.result());
        res.response.version(response.version());
        for (const auto& hdrEntry : response.base())
        {
            const auto fieldName = hdrEntry.name();
            // preparePayload() sets these; skip to avoid duplicates.
            if (fieldName == boost::beast::http::field::content_length ||
                fieldName == boost::beast::http::field::transfer_encoding)
            {
                continue;
            }
            res.response.insert(hdrEntry.name_string(), hdrEntry.value());
        }
    }

    bool createStreamPipe()
    {
        readPipe.emplace(ioc);
        writePipe.emplace(ioc);
        boost::system::error_code pipeEc{};
        boost::asio::connect_pipe(*readPipe, *writePipe, pipeEc);
        if (pipeEc)
        {
            BMCWEB_LOG_ERROR("StreamingResponseRelay: pipe create failed: {}",
                             pipeEc.message());
            readPipe.reset();
            writePipe.reset();
            return false;
        }
        // Expand the pipe buffer to reduce write stalls under backpressure.
        constexpr int pipeBufferSize = 4 * 1024 * 1024;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        if (::fcntl(writePipe->native_handle(), F_SETPIPE_SZ, pipeBufferSize) <
            0)
        {
            BMCWEB_LOG_WARNING(
                "StreamingResponseRelay: F_SETPIPE_SZ failed, using default pipe size");
        }
        return true;
    }

    int getReadFd()
    {
        if (!readPipe)
        {
            BMCWEB_LOG_ERROR("getReadFd: readPipe not initialised");
            return -1;
        }
        return readPipe->native_handle();
    }

    bool openFdAndFireStart()
    {
        int readFd = getReadFd();
        if (readFd < 0)
        {
            BMCWEB_LOG_ERROR("StreamingResponseRelay: invalid read pipe fd");
            tearDownPipes();
            onSetupResult(false);
            return false;
        }
        std::optional<size_t> knownSize;
        if (content_length > 0)
        {
            knownSize = content_length;
        }
        int dupFd = ::dup(readFd);
        if (dupFd < 0)
        {
            BMCWEB_LOG_ERROR("StreamingResponseRelay: dup() failed: {}",
                             std::generic_category().message(errno));
            tearDownPipes();
            onSetupResult(false);
            return false;
        }
        res.openFd(dupFd, bmcweb::EncodingType::Raw, knownSize);
        // Single point where a streaming response is starting: stamp the
        // wall-clock cap on the body so this relay's upstream timer, the
        // HTTP/1.1 streamAbortTimer, and the HTTP/2 streamAbortTimer all
        // read the same time_point.
        res.response.body().setStreamDeadline(streamingDeadlineDuration);
        onSetupResult(true);
        return true;
    }

    // Idempotent teardown of pipes + timers. fireOnComplete = true for normal
    // stream end (success / error / stall / deadline); false for setup
    // failure, where the caller fires onSetupResult(false) and onComplete must
    // not run.
    void teardown(bool fireOnComplete)
    {
        const bool wasOpen = writePipe.has_value();
        if (wasOpen)
        {
            writePipe->close();
            writePipe.reset();
        }
        readPipe.reset();
        chunkStallTimer.cancel();
        streamingDeadline.cancel();
        if (fireOnComplete && wasOpen && onComplete)
        {
            onComplete();
        }
    }

    void tearDownPipes()
    {
        teardown(/*fireOnComplete=*/false);
    }
    void closePipe()
    {
        teardown(/*fireOnComplete=*/true);
    }

    void resetChunkStallTimer()
    {
        chunkStallTimer.cancel();
        chunkStallTimer.expires_after(std::chrono::seconds(120));
        chunkStallTimer.async_wait(
            std::bind_front(onChunkStallTimeout, this->weak_from_this()));
    }

    void startStreamingDeadline()
    {
        streamingDeadline.cancel();
        // Read the cap from the body so the upstream timer (here), the
        // HTTP/1.1 streamAbortTimer, and the HTTP/2 streamAbortTimer all
        // expire on the same absolute time_point. Fall back to the local
        // duration if no deadline was stamped on the body.
        if (res.response.body().hasStreamDeadline())
        {
            streamingDeadline.expires_at(
                res.response.body().getStreamDeadline());
        }
        else
        {
            streamingDeadline.expires_after(streamingDeadlineDuration);
        }
        streamingDeadline.async_wait(
            std::bind_front(onStreamingDeadlineFired, this->weak_from_this()));
    }

    [[nodiscard]] static std::shared_ptr<std::string> drainParserBodyChunk(
        parser_type& msgParser)
    {
        auto chunk = std::make_shared<std::string>(
            std::move(msgParser.get().body().str()));
        msgParser.get().body().str().clear();
        return chunk;
    }

    void scheduleChunkedRead()
    {
        if (sslConn)
        {
            boost::beast::http::async_read_some(
                *sslConn, buffer, parser,
                std::bind_front(&StreamingResponseRelay::afterStreamRead, this,
                                this->shared_from_this()));
        }
        else
        {
            boost::beast::http::async_read_some(
                conn, buffer, parser,
                std::bind_front(&StreamingResponseRelay::afterStreamRead, this,
                                this->shared_from_this()));
        }
    }

    bool drainPrefetchedBuffer()
    {
        if (buffer.size() == 0)
        {
            return false;
        }
        boost::beast::error_code ec{};
        size_t consumed{parser.put(buffer.data(), ec)};
        buffer.consume(consumed);
        if (consumed == 0 && !ec && !parser.is_done())
        {
            return false;
        }
        // Post to break synchronous recursion when the full body is
        // already buffered.
        boost::asio::post(conn.get_executor(),
                          [self = this->shared_from_this(), ec, consumed]() {
                              self->afterStreamRead(self, ec, consumed);
                          });
        return true;
    }

    void scheduleRawRead()
    {
        size_t remaining{content_length - pipeByteCount};
        size_t readLimit{
            std::min(buffer.max_size() - buffer.size(), remaining)};
        if (sslConn)
        {
            sslConn->async_read_some(
                buffer.prepare(readLimit),
                std::bind_front(&StreamingResponseRelay::afterRawRead, this,
                                this->shared_from_this()));
        }
        else
        {
            conn.async_read_some(
                buffer.prepare(readLimit),
                std::bind_front(&StreamingResponseRelay::afterRawRead, this,
                                this->shared_from_this()));
        }
    }

    void scheduleStreamRead()
    {
        // content_length == 0 means chunked; use Beast's framing-aware
        // read.
        if (content_length == 0)
        {
            scheduleChunkedRead();
            return;
        }
        // Bypass Beast's full-buffer read; drain prefetched bytes first.
        if (!drainPrefetchedBuffer())
        {
            scheduleRawRead();
        }
    }

    void afterRawRead(const std::shared_ptr<StreamingResponseRelay>& self,
                      boost::beast::error_code ec, size_t bytesRead)
    {
        buffer.commit(bytesRead);
        boost::beast::error_code parseEc{};
        size_t consumed{parser.put(buffer.data(), parseEc)};
        buffer.consume(consumed);
        afterStreamRead(self, parseEc.failed() ? parseEc : ec, bytesRead);
    }

    void afterStreamRead(
        const std::shared_ptr<StreamingResponseRelay>& /*self*/,
        const boost::beast::error_code& ec, const std::size_t& bytesTransferred)
    {
        if (ec == boost::asio::error::operation_aborted ||
            ec == boost::system::errc::operation_canceled)
        {
            return;
        }
        if (ec && ec != boost::asio::error::eof)
        {
            BMCWEB_LOG_ERROR("afterStreamRead upstream error: {} {}", ec,
                             ec.message());
            chunkStallTimer.cancel();
            flushLastChunkAndClose();
            return;
        }
        processNormalStreamData(ec, bytesTransferred);
    }

    void processNormalStreamData(const boost::beast::error_code& ec,
                                 const std::size_t& bytesTransferred)
    {
        // Close pipe on EOF even if parser is incomplete; re-reading a
        // half-closed socket spins.
        bool hadEof = (ec == boost::asio::error::eof);
        BMCWEB_LOG_DEBUG("afterStreamRead bytesTransferred={} done={} eof={}",
                         bytesTransferred, parser.is_done(), hadEof);
        if (!hadEof)
        {
            resetChunkStallTimer();
        }
        auto chunk = drainParserBodyChunk(parser);
        bool done = parser.is_done();
        if (chunk->empty())
        {
            handleEmptyChunk(done, hadEof);
            return;
        }
        writeChunkToPipe(chunk, done, hadEof);
    }

    void handleEmptyChunk(bool done, bool hadEof)
    {
        if (done || hadEof)
        {
            if (hadEof && !done)
            {
                BMCWEB_LOG_ERROR(
                    "afterStreamRead: remote server closed connection "
                    "before transfer complete (EOF mid-stream), closing pipe");
            }
            chunkStallTimer.cancel();
            closePipe();
        }
        else
        {
            scheduleStreamRead();
        }
    }

    void writeChunkToPipe(const std::shared_ptr<std::string>& chunk, bool done,
                          bool hadEof)
    {
        // Blocking here (pipe full) propagates backpressure to the TCP
        // receive window automatically.
        if (!writePipe)
        {
            BMCWEB_LOG_ERROR("afterStreamRead: writePipe not initialised");
            return;
        }
        pipeByteCount += chunk->size();
        boost::asio::async_write(
            *writePipe, boost::asio::buffer(*chunk),
            std::bind_front(&StreamingResponseRelay::afterChunkWrite, this,
                            this->shared_from_this(), chunk, done, hadEof));
    }

    void writeLastChunkAndClose(const std::shared_ptr<std::string>& lastChunk)
    {
        if (!writePipe)
        {
            BMCWEB_LOG_ERROR("afterStreamRead: writePipe not initialised");
            return;
        }
        pipeByteCount += lastChunk->size();
        boost::asio::async_write(
            *writePipe, boost::asio::buffer(*lastChunk),
            [self = this->shared_from_this(),
             lastChunk](boost::system::error_code /*writeEc*/, size_t) {
                self->closePipe();
            });
    }

    void flushLastChunkAndClose()
    {
        // Beast's partial_message fires after the final bytes are already
        // in body.str(); flush them so the client sees a complete transfer.
        auto lastChunk = drainParserBodyChunk(parser);
        if (!lastChunk->empty())
        {
            BMCWEB_LOG_DEBUG(
                "afterStreamRead: flushing {} trailing bytes before close",
                lastChunk->size());
            writeLastChunkAndClose(lastChunk);
        }
        else
        {
            closePipe();
        }
    }

    void afterChunkWrite(
        const std::shared_ptr<StreamingResponseRelay>& /*self*/,
        const std::shared_ptr<std::string>& /*chunk*/, bool done, bool hadEof,
        boost::system::error_code writeEc, size_t /*bytesWritten*/)
    {
        if (writeEc)
        {
            // operation_canceled/operation_aborted means the stall timer
            // already fired and closed the pipe; don't double-close.
            if (writeEc == boost::system::errc::operation_canceled ||
                writeEc == boost::asio::error::operation_aborted)
            {
                return;
            }
            BMCWEB_LOG_ERROR("afterStreamRead pipe write error: {}", writeEc);
            chunkStallTimer.cancel();
            closePipe();
            return;
        }
        if (done || hadEof)
        {
            if (done && content_length > 0 && pipeByteCount != content_length)
            {
                BMCWEB_LOG_ERROR("afterStreamRead: pipe byte count mismatch: "
                                 "wrote {} bytes, expected {}",
                                 pipeByteCount, content_length);
            }
            if (hadEof && !done)
            {
                BMCWEB_LOG_ERROR(
                    "afterStreamRead: upstream disconnected "
                    "mid-transfer (EOF), closing pipe after flushing "
                    "last chunk");
            }
            chunkStallTimer.cancel();
            closePipe();
        }
        else
        {
            scheduleStreamRead();
        }
    }

    static void onChunkStallTimeout(
        const std::weak_ptr<StreamingResponseRelay>& weakSelf,
        const boost::system::error_code& ec)
    {
        if (ec == boost::asio::error::operation_aborted)
        {
            return;
        }
        if (ec)
        {
            BMCWEB_LOG_ERROR("chunk-stall async_wait failed: {}", ec.message());
        }
        std::shared_ptr<StreamingResponseRelay> self = weakSelf.lock();
        if (self == nullptr)
        {
            return;
        }
        if (!self->writePipe.has_value())
        {
            return;
        }
        BMCWEB_LOG_ERROR("Streaming stall: 120 s without data from upstream {}",
                         self->host);
        self->closePipe();
    }

    static void onStreamingDeadlineFired(
        const std::weak_ptr<StreamingResponseRelay>& weakSelf,
        const boost::system::error_code& ec)
    {
        if (ec == boost::asio::error::operation_aborted)
        {
            return;
        }
        std::shared_ptr<StreamingResponseRelay> self = weakSelf.lock();
        if (self == nullptr)
        {
            return;
        }
        if (!self->writePipe.has_value())
        {
            return;
        }
        BMCWEB_LOG_ERROR(
            "Streaming deadline ({} min) exceeded for {}, closing pipe",
            streamingDeadlineDuration.count(), self->host);
        self->closePipe();
    }

    boost::asio::io_context& ioc;
    parser_type& parser;
    boost::beast::flat_static_buffer<BufferSize>& buffer;
    boost::asio::ip::tcp::socket& conn;
    std::optional<boost::asio::ssl::stream<boost::asio::ip::tcp::socket&>>&
        sslConn;
    Response& res;
    boost::urls::url host;
    size_t content_length = 0;
    size_t pipeByteCount = 0;

    // Fired exactly once when setup completes: true for success (the
    // response body now owns the read-end fd and is being filled), false
    // when pipe/dup setup failed. On failure, onComplete fires too.
    std::function<void(bool)> onSetupResult;
    // Fired once when the stream ends (success, error, stall, or
    // deadline). The connection is reusable after this fires.
    std::function<void()> onComplete;

    boost::asio::steady_timer chunkStallTimer;
    boost::asio::steady_timer streamingDeadline;
    std::optional<boost::asio::readable_pipe> readPipe;
    std::optional<boost::asio::writable_pipe> writePipe;
};
} // namespace crow
