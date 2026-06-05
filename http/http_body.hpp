// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "duplicatable_file_handle.hpp"
#include "logging.hpp"
#include "nvidia_http_body_streaming.hpp" // NVIDIA code for streaming
#include "utility.hpp"
#include "zstd_compressor.hpp"
#include "zstd_decompressor.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/beast/core/buffer_traits.hpp>
#include <boost/beast/core/buffers_range.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/file_base.hpp>
#include <boost/beast/core/file_posix.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/none.hpp>
#include <boost/optional/optional.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/url_view.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace bmcweb
{

// Maximum allowed length for a single query parameter value.
// Enforced during URL parsing to prevent large input amplification in error
// responses and DoS via oversized query strings.
inline constexpr size_t maxQueryParamValueLength = 256;

inline bool hasOversizedQueryParam(const boost::urls::url_view& url)
{
    for (const auto& param : url.params())
    {
        if (param.value.size() > maxQueryParamValueLength)
        {
            return true;
        }
    }
    return false;
}

struct HttpBody
{
    // Body concept requires specific naming of classes
    // NOLINTBEGIN(readability-identifier-naming)
    class writer;
    class reader;
    class value_type;
    // NOLINTEND(readability-identifier-naming)

    static std::uint64_t size(const value_type& body);
};

enum class EncodingType
{
    Raw,
    Base64,
};

enum class CompressionType
{
    Raw,
    Gzip,
    Zstd,
};

// NVIDIA code for streaming: HttpResponseWireFormat,
// prepareResponseHeadersForWireFormat and PipeNotifier live in
// nvidia_http_body_streaming.hpp (included above).

class HttpBody::value_type
{
    // NVIDIA code starts for streaming: value_type reworked from {fileHandle,
    // strBody} to a variant<string, FdSource> plus streaming state (pipe
    // notifier, lifeline, stream deadline, streamingReceiver). This member
    // block and several methods below differ from upstream. Pipe EOF requires
    // read()==0; file EOF uses read < readReq.
    struct FdSource
    {
        DuplicatableFileHandle handle;
        bool isPipe = false;
    };

    // String by default; FdSource when backed by a file or pipe.
    std::variant<std::string, FdSource> body;
    std::optional<size_t> fileSize;
    // All streaming-only state (pipe watcher, data-ready callback, lifeline,
    // deadline, trusted-receiver flag) lives in StreamingBodyState so this
    // file's divergence from upstream stays a single member plus the thin
    // forwarders below. See nvidia_http_body_streaming.hpp.
    bmcweb::StreamingBodyState streaming;
    // NVIDIA code ends for streaming

  public:
    value_type() = default;
    explicit value_type(std::string_view s) : body(std::string(s)) {}
    explicit value_type(EncodingType e) : encodingType(e) {}
    EncodingType encodingType = EncodingType::Raw;
    CompressionType compressionType = CompressionType::Raw;
    CompressionType clientCompressionType = CompressionType::Raw;

    ~value_type() = default;

    explicit value_type(EncodingType enc, CompressionType comp) :
        encodingType(enc), compressionType(comp)
    {}

    value_type(const value_type& other) = default;
    value_type& operator=(const value_type& other) = default;
    value_type(value_type&& other) noexcept = default;
    value_type& operator=(value_type&& other) noexcept = default;

    // NVIDIA code for streaming: file() reads the handle out of the variant.
    // Returns a closed handle for string bodies.
    const boost::beast::file_posix& file() const
    {
        if (const auto* src = std::get_if<FdSource>(&body))
        {
            return src->handle.fileHandle;
        }
        static const boost::beast::file_posix closed;
        return closed;
    }

    // NVIDIA code starts for streaming: streaming-pipe accessors and lifecycle
    // helpers.
    bool isStreamingPipe() const
    {
        const auto* src = std::get_if<FdSource>(&body);
        return src != nullptr && src->isPipe;
    }

    bool hasOnReady() const
    {
        return streaming.hasOnReady();
    }

    // Register the connection's "pipe is readable" callback.  The body owns
    // the watcher (dup() of the pipe fd +
    // posix::stream_descriptor::async_wait); the connection only supplies an
    // executor to run the callback on and the callback itself.  Idempotent: a
    // second call is ignored, so call sites can register lazily on first need
    // without an explicit hasOnReady() guard. No-op for non-pipe bodies.
    void setOnReady(boost::asio::any_io_executor exec,
                    std::function<bool(boost::system::error_code)> onReady)
    {
        if (!isStreamingPipe())
        {
            return;
        }
        streaming.setOnReady(std::move(exec), std::move(onReady));
    }

    void setLifeline(std::shared_ptr<void> guard)
    {
        streaming.lifeline = std::move(guard);
    }

    void setStreamDeadline(std::chrono::steady_clock::duration dur)
    {
        streaming.setStreamDeadline(dur);
    }

    void setStreamingReceiver(bool enable)
    {
        streaming.streamingReceiver = enable;
    }

    bool isStreamingReceiver() const
    {
        return streaming.streamingReceiver;
    }

    std::chrono::steady_clock::time_point getStreamDeadline() const
    {
        return streaming.streamDeadline;
    }

    bool hasStreamDeadline() const
    {
        return streaming.hasStreamDeadline();
    }

    // Cancel the pipe watcher on EOF to break the re-arm loop.
    void cancelNotifier()
    {
        streaming.cancelNotifier();
    }

    // Arm the pipe watcher, creating it on first use.  Called from the body
    // writer on EAGAIN: the body decides when an FD watcher is needed
    // (only when the producer pipe has nothing ready), creates it from the
    // executor + callback the connection supplied via setOnReady(), and
    // re-arms on subsequent EAGAINs.  No-op if no onReady handler was
    // registered (e.g. non-streaming body, or test path).
    void armNotifier()
    {
        if (streaming.notifier)
        {
            streaming.notifier->arm();
            return;
        }
        if (!isStreamingPipe())
        {
            return;
        }
        streaming.createAndArmNotifier(file().native_handle());
    }
    // NVIDIA code ends for streaming

    // NVIDIA code for streaming: str() switches the variant back to its string
    // alternative.
    std::string& str()
    {
        // std::variant::get throws bad_variant_access if the variant currently
        // holds FdSource (e.g. after open()/setFd()). Switch the variant back
        // to its string alternative so this never throws.
        if (std::string* strPtr = std::get_if<std::string>(&body);
            strPtr != nullptr)
        {
            return *strPtr;
        }
        return body.emplace<std::string>();
    }

    // NVIDIA code for streaming: const str() reads the string alternative out
    // of the variant.
    const std::string& str() const
    {
        if (const std::string* strPtr = std::get_if<std::string>(&body);
            strPtr != nullptr)
        {
            return *strPtr;
        }
        static const std::string empty;
        return empty;
    }

    // NVIDIA code for streaming: payloadSize() reads the string size out of the
    // variant.
    std::optional<size_t> payloadSize() const
    {
        if (const auto* strPtr = std::get_if<std::string>(&body))
        {
            return strPtr->size();
        }
        if (fileSize)
        {
            if (encodingType == EncodingType::Base64)
            {
                return crow::utility::Base64Encoder::encodedSize(*fileSize);
            }
        }
        return fileSize;
    }

    // NVIDIA code for streaming: clear() resets the variant to string and drops
    // streaming state (notifier, onReady callback/executor, lifeline).
    void clear()
    {
        body.emplace<std::string>();
        fileSize = std::nullopt;
        encodingType = EncodingType::Raw;
        streaming.reset();
    }

    // NVIDIA code for streaming: open() stores the handle in the variant's
    // FdSource.
    void open(const char* path, boost::beast::file_mode mode,
              boost::system::error_code& ec)
    {
        auto& src = body.emplace<FdSource>();
        src.handle.fileHandle.open(path, mode, ec);
        if (ec)
        {
            body.emplace<std::string>();
            return;
        }
        boost::system::error_code ec2;
        uint64_t size = src.handle.fileHandle.size(ec2);
        if (!ec2)
        {
            BMCWEB_LOG_INFO("File size was {} bytes", size);
            fileSize = static_cast<size_t>(size);
        }
        else
        {
            BMCWEB_LOG_WARNING("Failed to read file size on {}", path);
        }

        int fadvise = posix_fadvise(src.handle.fileHandle.native_handle(), 0, 0,
                                    POSIX_FADV_SEQUENTIAL);
        if (fadvise != 0)
        {
            BMCWEB_LOG_WARNING("Fasvise returned {} ignoring", fadvise);
        }
        ec = {};
    }

    // NVIDIA code starts for streaming: setFd() detects pipes (fstat/S_ISFIFO),
    // accepts a caller-supplied knownSize for Content-Length, and stores into
    // the variant's FdSource.
    void setFd(int fd, boost::system::error_code& ec,
               std::optional<size_t> knownSize = std::nullopt)
    {
        struct stat fileStat{};
        auto& src = body.emplace<FdSource>();
        src.isPipe = (::fstat(fd, &fileStat) == 0) &&
                     S_ISFIFO(fileStat.st_mode);
        src.handle.fileHandle.native_handle(fd);

        if (src.isPipe)
        {
            // fstat() on a pipe always reports size 0; trust the caller's
            // Content-Length value when provided.
            fileSize = knownSize;
        }
        else
        {
            if (knownSize.has_value())
            {
                fileSize = knownSize;
            }
            else
            {
                boost::system::error_code ec2;
                uint64_t size = src.handle.fileHandle.size(ec2);
                if (!ec2)
                {
                    if (size != 0 && size < std::numeric_limits<size_t>::max())
                    {
                        fileSize = static_cast<size_t>(size);
                    }
                }
            }
        }
        ec = {};
    }
    // NVIDIA code ends for streaming
};

class HttpBody::writer
{
  public:
    using const_buffers_type = boost::asio::const_buffer;

  private:
    std::string buf;
    crow::utility::Base64Encoder encoder;

    std::optional<ZstdDecompressor> zstdDecompressor;
    std::optional<ZstdCompressor> zstdCompressor;

    value_type& body;
    size_t sent = 0;
    // 64KB This number is arbitrary, and selected to try to optimize for larger
    // files and fewer loops over per-connection reduction in memory usage.
    // Nginx uses 16-32KB here, so we're in the range of what other webservers
    // do.
    constexpr static size_t readBufSize = 1024UL * 64UL;
    std::array<char, readBufSize> fileReadBuf{};

  public:
    template <bool IsRequest, class Fields>
    writer(boost::beast::http::header<IsRequest, Fields>& /*header*/,
           value_type& bodyIn) : body(bodyIn)
    {
        // If zstd compressed and client doesn't support zstd, need to
        // decompress
        if (body.compressionType == CompressionType::Zstd &&
            body.clientCompressionType != CompressionType::Zstd)
        {
            zstdDecompressor.emplace();
        }
        if (body.compressionType == CompressionType::Raw &&
            body.clientCompressionType == CompressionType::Zstd)
        {
            std::optional<size_t> size = body.payloadSize();
            if (size)
            {
                BMCWEB_LOG_DEBUG(
                    "Body is raw, client supports zstd, and paylod is not streaming.  Compressing.");
                zstdCompressor.emplace();
                if (!zstdCompressor->init(*size))
                {
                    BMCWEB_LOG_ERROR("Failed to initialize Zstd Compressor");
                    zstdCompressor = std::nullopt;
                }
            }
        }
    }

    static void init(boost::beast::error_code& ec)
    {
        ec = {};
    }

    boost::optional<std::pair<const_buffers_type, bool>> get(
        boost::beast::error_code& ec)
    {
        return getWithMaxSize(ec, std::numeric_limits<size_t>::max());
    }

    boost::optional<std::pair<const_buffers_type, bool>> getWithMaxSize(
        boost::beast::error_code& ec, size_t maxSize)
    {
        std::pair<const_buffers_type, bool> ret;
        if (!body.file().is_open())
        {
            size_t remain = body.str().size() - sent;
            size_t toReturn = std::min(maxSize, remain);
            ret.first = const_buffers_type(&body.str()[sent], toReturn);

            sent += toReturn;
            ret.second = sent < body.str().size();
        }
        else
        {
            size_t readReq = std::min(fileReadBuf.size(), maxSize);
            BMCWEB_LOG_INFO("Reading {} handle", readReq);
            boost::system::error_code readEc;
            size_t read = body.file().read(fileReadBuf.data(), readReq, readEc);
            if (readEc)
            {
                // NVIDIA code starts for streaming: treat EAGAIN on a pipe as
                // backpressure (re-arm the notifier and yield) rather than a
                // fatal error.
                if (readEc == boost::system::errc::operation_would_block ||
                    readEc ==
                        boost::system::errc::resource_unavailable_try_again)
                {
                    if (read == 0)
                    {
                        // Pipe empty; re-arm the notifier lazily so it fires
                        // when data arrives.  arm() is idempotent if already
                        // pending, and handles the case where the callback was
                        // disarmed while a socket write was in progress.
                        body.armNotifier();
                        ec = readEc;
                        return boost::none;
                    }
                    // Bytes read before EAGAIN are valid; clear the spurious
                    // error.
                    readEc = {};
                }
                else
                // NVIDIA code ends for streaming
                {
                    BMCWEB_LOG_CRITICAL("Failed to read from file {}",
                                        readEc.message());
                    ec = readEc;
                    return boost::none;
                }
            }

            std::string_view chunkView(fileReadBuf.data(), read);
            BMCWEB_LOG_INFO("Read {} bytes ec {}", read, readEc.message());
            // NVIDIA code starts for streaming: pipe EOF requires read()==0;
            // file EOF uses read < readReq.
            if (body.isStreamingPipe())
            {
                ret.second =
                    (read > 0); // pipe: more data until write end closed
                if (read == 0)
                {
                    // Stop the notifier to avoid infinite async_wait loop after
                    // EOF.
                    body.cancelNotifier();
                }
            }
            else
            {
                // If the number of bytes read equals the amount requested, we
                // haven't reached EOF yet
                ret.second = read == readReq;
            }
            // NVIDIA code ends for streaming
            if (body.encodingType == EncodingType::Base64)
            {
                buf.clear();
                buf.reserve(crow::utility::Base64Encoder::encodedSize(
                    chunkView.size()));
                encoder.encode(chunkView, buf);
                if (!ret.second)
                {
                    encoder.finalize(buf);
                }
                ret.first = const_buffers_type(buf.data(), buf.size());
            }
            else
            {
                ret.first =
                    const_buffers_type(chunkView.data(), chunkView.size());
            }
        }

        if (zstdDecompressor)
        {
            std::optional<const_buffers_type> decompressed =
                zstdDecompressor->decompress(ret.first);
            if (!decompressed)
            {
                return boost::none;
            }
            ret.first = *decompressed;
        }
        if (zstdCompressor)
        {
            BMCWEB_LOG_DEBUG("Compressing body more={}", ret.second);
            std::span<const uint8_t> spanIn(
                static_cast<const uint8_t*>(ret.first.data()),
                ret.first.size());
            std::optional<std::span<const uint8_t>> compressed =
                zstdCompressor->compress(spanIn, ret.second);
            if (!compressed)
            {
                return boost::none;
            }
            ret.first = *compressed;
        }
        BMCWEB_LOG_INFO("Returning {} bytes more={}", ret.first.size(),
                        ret.second);
        return ret;
    }
};

class HttpBody::reader
{
    value_type& value;

  public:
    template <bool IsRequest, class Fields>
    reader(boost::beast::http::header<IsRequest, Fields>& /*headers*/,
           value_type& body) : value(body)
    {}

    void init(const boost::optional<std::uint64_t>& contentLength,
              boost::beast::error_code& ec)
    {
        // NVIDIA code starts for streaming: skip the Content-Length
        // reserve/guard for streaming receivers (they never reserve
        // proportional memory); cap the reservation otherwise.
        if (contentLength && !value.file().is_open() &&
            !value.isStreamingReceiver())
        {
            constexpr size_t maxReserveSize =
                1024UL * 1024UL * BMCWEB_HTTP_BODY_LIMIT;

            // Reject only when this is an inbound body from an external
            // client that would be reserved into a std::string. Trusted
            // internal receivers that stream the body chunk-by-chunk mark
            // their parser body via setStreamingReceiver(true) so this guard
            // is skipped; they never reserve memory proportional to
            // Content-Length.
            if (*contentLength > maxReserveSize)
            {
                BMCWEB_LOG_WARNING(
                    "Content-Length {} exceeds max body size {}, rejecting.",
                    *contentLength, maxReserveSize);
                ec = boost::beast::http::error::body_limit;
                return;
            }

            // Cap reservation to avoid OOM for large responses on
            // low-memory BMCs.
            value.str().reserve(
                std::min(static_cast<size_t>(*contentLength), maxReserveSize));
        }
        // NVIDIA code ends for streaming
        ec = {};
    }

    template <class ConstBufferSequence>
    std::size_t put(const ConstBufferSequence& buffers,
                    boost::system::error_code& ec)
    {
        size_t extra = boost::beast::buffer_bytes(buffers);
        for (const auto b : boost::beast::buffers_range_ref(buffers))
        {
            const char* ptr = static_cast<const char*>(b.data());
            value.str() += std::string_view(ptr, b.size());
        }
        ec = {};
        return extra;
    }

    static void finish(boost::system::error_code& ec)
    {
        ec = {};
    }
};

inline std::uint64_t HttpBody::size(const value_type& body)
{
    std::optional<size_t> payloadSize = body.payloadSize();
    return payloadSize.value_or(0U);
}

} // namespace bmcweb
