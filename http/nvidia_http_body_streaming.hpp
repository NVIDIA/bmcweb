// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright NVIDIA
//
// NVIDIA code for streaming: streaming-body helpers for the FDR dump download
// proxy. Extracted from http_body.hpp so these additions live in an
// NVIDIA-owned file and do not collide on upstream syncs. Included by
// http_body.hpp.
#pragma once

#include "logging.hpp"

#include <unistd.h>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/system/error_code.hpp>

#include <cerrno>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace bmcweb
{

/** Target wire encoding for an outgoing response header set (after
 *  Response::preparePayload).  */
enum class HttpResponseWireFormat
{
    Http11,
    Http2,
};

/** Adjust header fields for the wire format.  For HTTP/2, removes hop-by-hop
 *  and connection-specific fields (RFC 7540 §8.1.2.2) that are valid on
 * HTTP/1.1 but illegal on HTTP/2 — e.g. Transfer-Encoding added by Beast for
 * chunked encoding.  For HTTP/1.1 this is a no-op.
 */
inline void prepareResponseHeadersForWireFormat(
    boost::beast::http::fields& hdrs, HttpResponseWireFormat fmt)
{
    if (fmt != HttpResponseWireFormat::Http2)
    {
        return;
    }
    // Remove hop-by-hop headers prohibited in HTTP/2 (RFC 7540 §8.1.2.2)
    hdrs.erase(boost::beast::http::field::transfer_encoding);
    hdrs.erase(boost::beast::http::field::connection);
    hdrs.erase(boost::beast::http::field::keep_alive);
    hdrs.erase(boost::beast::http::field::proxy_connection);
    hdrs.erase(boost::beast::http::field::upgrade);
    // TE field requires special handling (only "trailers" allowed in HTTP/2)
}

// Self-rearming async watcher on the read end of a pipe.
struct PipeNotifier : std::enable_shared_from_this<PipeNotifier>
{
    boost::asio::posix::stream_descriptor sd;
    // Returns true to re-arm for next chunk; false to stop watching.
    std::function<bool(boost::system::error_code)> callback;
    // Prevents double-scheduling when arm() is called while an async_wait
    // is already pending (e.g. EAGAIN re-arm races the initial lazy arm).
    bool armPending = false;

    PipeNotifier(const boost::asio::any_io_executor& exec, int fd,
                 std::function<bool(boost::system::error_code)> cb) :
        sd(exec, fd), callback(std::move(cb))
    {}

    // Idempotent: no-op when an async_wait is already in flight.
    void arm()
    {
        if (armPending)
        {
            return;
        }
        armPending = true;
        sd.async_wait(
            boost::asio::posix::stream_descriptor::wait_read,
            [self = shared_from_this()](boost::system::error_code ec) {
                self->armPending = false;
                if (self->callback(ec))
                {
                    self->arm();
                }
            });
    }
};

// All streaming-only state of an HttpBody::value_type, grouped so the
// upstream-divergent surface of http_body.hpp stays a single member plus thin
// forwarders. Holds the lazily-created pipe watcher, the connection-supplied
// "data ready" callback + its executor, an opaque lifeline whose lifetime
// tracks the body (e.g. a concurrency slot), the wall-clock streaming deadline,
// and the trusted-receiver flag that lets HttpBody::reader skip the
// Content-Length guard. Default-constructed state means "not a streaming body".
struct StreamingBodyState
{
    // Shared so the watcher survives moving the body into Beast's
    // message_generator. Created lazily by createAndArmNotifier() on the first
    // EAGAIN from the body writer.
    std::shared_ptr<PipeNotifier> notifier;
    // Executor + callback supplied by the owning connection via setOnReady().
    // Stored, not consumed immediately: the watcher is only created on demand
    // (first pipe EAGAIN), so a body that never blocks never allocates one.
    boost::asio::any_io_executor onReadyExec;
    std::function<bool(boost::system::error_code)> onReadyCb;
    // Opaque object whose lifetime tracks the body's. Used to keep a caller-
    // supplied resource (e.g. a concurrency slot) alive for as long as bytes
    // are being produced/sent. Released when the body is destroyed.
    std::shared_ptr<void> lifeline;
    // Wall-clock cap for streaming bodies. The writer returns clean EOF once
    // this point is reached, so a slow client cannot keep BMC pipe + send
    // buffers + a downstream socket pinned beyond the configured cap. Default
    // = max() means "no deadline".
    std::chrono::steady_clock::time_point streamDeadline =
        std::chrono::steady_clock::time_point::max();
    // When true, HttpBody::reader::init() skips the BMCWEB_HTTP_BODY_LIMIT
    // Content-Length guard. Set by trusted internal receivers that stream the
    // body chunk-by-chunk and therefore do not reserve memory proportional to
    // Content-Length. Must remain false for inbound external request bodies,
    // where the guard is a DoS protection.
    bool streamingReceiver = false;

    bool hasOnReady() const
    {
        return static_cast<bool>(onReadyCb);
    }

    // Register the connection's "pipe is readable" callback. Idempotent: a
    // second call is ignored, so call sites can register lazily on first need.
    // The pipe-body guard lives in the value_type forwarder.
    void setOnReady(boost::asio::any_io_executor exec,
                    std::function<bool(boost::system::error_code)> onReady)
    {
        if (onReadyCb)
        {
            return;
        }
        onReadyExec = std::move(exec);
        onReadyCb = std::move(onReady);
    }

    void setStreamDeadline(std::chrono::steady_clock::duration dur)
    {
        streamDeadline = std::chrono::steady_clock::now() + dur;
    }

    bool hasStreamDeadline() const
    {
        return streamDeadline != std::chrono::steady_clock::time_point::max();
    }

    // Cancel the pipe watcher on EOF to break the re-arm loop.
    void cancelNotifier()
    {
        if (notifier)
        {
            notifier->sd.cancel();
            notifier.reset();
        }
    }

    // Create the pipe watcher from a dup() of the supplied pipe fd and arm it.
    // The value_type forwarder handles the "watcher already exists -> re-arm"
    // and non-pipe-body short circuits before calling this.
    void createAndArmNotifier(int pipeFd)
    {
        if (!onReadyCb)
        {
            return;
        }
        int watchFd = ::dup(pipeFd);
        if (watchFd < 0)
        {
            BMCWEB_LOG_ERROR("dup() failed for pipe fd {}: {}", pipeFd,
                             std::generic_category().message(errno));
            return;
        }
        notifier =
            std::make_shared<PipeNotifier>(onReadyExec, watchFd, onReadyCb);
        notifier->arm();
    }

    // Drop the watcher + callback + lifeline. Mirrors the subset of state
    // cleared by value_type::clear(); streamDeadline and streamingReceiver are
    // intentionally left untouched.
    void reset()
    {
        notifier.reset();
        onReadyCb = nullptr;
        onReadyExec = {};
        lifeline.reset();
    }
};

// Arm an absolute wall-clock abort timer for a streaming response. Shared by
// the HTTP/1.1 (http_connection) and HTTP/2 (http2_connection) write paths:
// closing the upstream pipe alone is not enough when the client is rate-limited
// (the kernel pipe + TCP send buffer can keep the wire alive long past the
// upstream deadline), so each transport schedules a hard close at the cap.
// `deadline` == time_point::max() means "no deadline" and is a no-op. `tag`
// prefixes the log lines (e.g. "<ptr> HTTP/1.1" or "HTTP/2 stream 3"). The
// caller supplies `onExpire`, which decides strong vs. weak capture of the
// owning connection and performs the actual close.
inline void armStreamAbortTimer(std::optional<boost::asio::steady_timer>& timer,
                                const boost::asio::any_io_executor& exec,
                                std::chrono::steady_clock::time_point deadline,
                                std::string tag, std::function<void()> onExpire)
{
    if (deadline == std::chrono::steady_clock::time_point::max())
    {
        BMCWEB_LOG_DEBUG("{} streaming response but NO streamDeadline set",
                         tag);
        return;
    }
    auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
                         deadline - std::chrono::steady_clock::now())
                         .count();
    BMCWEB_LOG_WARNING("{} streamAbortTimer scheduled in {} s", tag, remaining);
    timer.emplace(exec);
    timer->expires_at(deadline);
    timer->async_wait([tag = std::move(tag), onExpire = std::move(onExpire)](
                          const boost::system::error_code& ec) {
        if (ec == boost::asio::error::operation_aborted)
        {
            BMCWEB_LOG_WARNING("{} streamAbortTimer aborted", tag);
            return;
        }
        BMCWEB_LOG_WARNING("{} streamAbortTimer fired; hard close", tag);
        onExpire();
    });
}

} // namespace bmcweb
