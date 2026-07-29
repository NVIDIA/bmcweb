// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "duplicatable_file_handle.hpp"
#include "logging.hpp"
// Nvidia code starts here
#include "multipart_parser.hpp"
// Nvidia code ends here
#include "utility.hpp"
#include "zstd_decompressor.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <boost/asio/buffer.hpp>
#include <boost/beast/core/buffer_traits.hpp>
#include <boost/beast/core/buffers_range.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/file_base.hpp>
#include <boost/beast/core/file_posix.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/none.hpp>
#include <boost/optional/optional.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/url_view.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
// Nvidia code starts here
#include <functional>
#include <limits>
#include <memory>
// Nvidia code ends here
#include <optional>
#include <string>
#include <string_view>
#include <utility>
// Nvidia code starts here
#include <variant>
// Nvidia code ends here

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

// Nvidia code starts here
struct FileBody
// Nvidia code ends here
{
    std::optional<size_t> fileSize;
    // Nvidia code starts here
    DuplicatableFileHandle fileHandle;
};

struct MultiPartBody
{
    std::vector<FormPart> parts;
};

struct BackpressureState
{
    bool paused = false;
    std::function<void()> resumeCallback;
};

class HttpBody::value_type
{
    friend HttpBody::reader;
    friend HttpBody::writer;

    std::variant<std::string, FileBody, MultiPartBody> bodyData;

    std::span<const FormPart> getMimeFields() const
    {
        if (const auto* multiPartBody = std::get_if<MultiPartBody>(&bodyData))
        {
            return {multiPartBody->parts};
        }
        return {};
    }

    std::span<FormPart> getMimeFields()
    {
        if (auto* multiPartBody = std::get_if<MultiPartBody>(&bodyData))
        {
            return {multiPartBody->parts};
        }
        return {};
    }
    // Nvidia code ends here

  public:
    // Skips the Content-Length DoS guard for trusted internal streaming.
    bool streamingReceiver = false;

    value_type() = default;
    // Nvidia code starts here
    explicit value_type(std::string_view s) : bodyData(std::string(s)) {}
    // Nvidia code ends here
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

    const boost::beast::file_posix& file() const
    {
        // Nvidia code starts here
        if (const auto* fileBody = std::get_if<FileBody>(&bodyData))
        {
            return fileBody->fileHandle.fileHandle;
        }
        static boost::beast::file_posix emptyFile;
        return emptyFile;
        // Nvidia code ends here
    }

    // NVIDIA code start
    void setStreamingReceiver(bool enable)
    {
        streamingReceiver = enable;
    }

    // Set file size when fstat cannot determine it (e.g. pipes).
    void setFileSize(size_t size)
    {
        if (auto* fileBody = std::get_if<FileBody>(&bodyData))
        {
            fileBody->fileSize = size;
        }
    }
    // NVIDIA code end

    std::string& str()
    {
        // Nvidia code starts here
        if (auto* s = std::get_if<std::string>(&bodyData))
        {
            return *s;
        }
        return bodyData.emplace<std::string>();
        // Nvidia code ends here
    }

    const std::string& str() const
    {
        // Nvidia code starts here
        if (const auto* s = std::get_if<std::string>(&bodyData))
        {
            return *s;
        }
        static const std::string emptyString;
        return emptyString;
    }

    std::span<FormPart> multipart()
    {
        if (auto* multiPartBody = std::get_if<MultiPartBody>(&bodyData))
        {
            return {multiPartBody->parts};
        }
        return {};
        // Nvidia code ends here
    }

    std::span<const FormPart> multipart() const
    {
        return getMimeFields();
    }

    std::optional<size_t> payloadSize() const
    {
        // Nvidia code starts here
        if (const auto* s = std::get_if<std::string>(&bodyData))
        {
            return s->size();
        }
        if (const auto* fileBody = std::get_if<FileBody>(&bodyData))
        {
            if (fileBody->fileHandle.fileHandle.is_open() && fileBody->fileSize)
            {
                if (encodingType == EncodingType::Base64)
                {
                    return crow::utility::Base64Encoder::encodedSize(
                        *fileBody->fileSize);
                }
            }
            return fileBody->fileSize;
        }
        return std::nullopt;
        // Nvidia code ends here
    }

    void clear()
    {
        // Nvidia code starts here
        bodyData = std::string{};
        // Nvidia code ends here
        encodingType = EncodingType::Raw;
        streamingReceiver = false;
    }

    void open(const char* path, boost::beast::file_mode mode,
              boost::system::error_code& ec)
    {
        // Nvidia code starts here
        FileBody fileBody;
        fileBody.fileHandle.fileHandle.open(path, mode, ec);
        // Nvidia code ends here
        if (ec)
        {
            return;
        }
        boost::system::error_code ec2;
        // Nvidia code starts here
        uint64_t size = fileBody.fileHandle.fileHandle.size(ec2);
        // Nvidia code ends here
        if (!ec2)
        {
            BMCWEB_LOG_INFO("File size was {} bytes", size);
            // Nvidia code starts here
            fileBody.fileSize = static_cast<size_t>(size);
            // Nvidia code ends here
        }
        else
        {
            BMCWEB_LOG_WARNING("Failed to read file size on {}", path);
        }

        // Nvidia code starts here
        int fadvise =
            posix_fadvise(fileBody.fileHandle.fileHandle.native_handle(), 0, 0,
                          POSIX_FADV_SEQUENTIAL);
        // Nvidia code ends here
        if (fadvise != 0)
        {
            // Nvidia code starts here
            BMCWEB_LOG_WARNING("Fadvise returned {} ignoring", fadvise);
        }
        bodyData = std::move(fileBody);
        // Nvidia code ends here
        ec = {};
    }

    void setFd(DuplicatableFileHandle handle, boost::system::error_code& ec)
    {
        // Nvidia code starts here
        FileBody& fileBody = bodyData.emplace<FileBody>();
        fileBody.fileHandle = std::move(handle);
        // Nvidia code ends here

        boost::system::error_code ec2;
        // Nvidia code starts here
        uint64_t size = fileBody.fileHandle.fileHandle.size(ec2);
        // Nvidia code ends here
        if (!ec2)
        {
            if (size != 0 && size < std::numeric_limits<size_t>::max())
            {
                // Nvidia code starts here
                fileBody.fileSize = static_cast<size_t>(size);
                // Nvidia code ends here
            }
        }
        ec = {};
    }
    // Nvidia code starts here

    void setMultipartParserCallbacks(
        MultipartParserStreamingCallbacks&& callbacks)
    {
        multipartParserCallbacks = std::move(callbacks);
    }

    void pauseRead()
    {
        backpressureState->paused = true;
    }

    void resumeRead()
    {
        if (!backpressureState->paused)
        {
            return;
        }
        backpressureState->paused = false;
        auto cb = std::move(backpressureState->resumeCallback);
        backpressureState->resumeCallback = nullptr;
        if (cb)
        {
            cb();
        }
    }

    bool isReadPaused() const
    {
        return backpressureState->paused;
    }

    void setResumeReadCallback(std::function<void()>&& cb)
    {
        backpressureState->resumeCallback = std::move(cb);
    }

    const std::shared_ptr<BackpressureState>& getBackpressureState() const
    {
        return backpressureState;
    }

    // private
    std::optional<MultipartParserStreamingCallbacks> multipartParserCallbacks;
    std::shared_ptr<BackpressureState> backpressureState =
        std::make_shared<BackpressureState>();
    // Nvidia code ends here
};

class HttpBody::writer
{
  public:
    using const_buffers_type = boost::asio::const_buffer;

  private:
    std::string buf;
    crow::utility::Base64Encoder encoder;

    std::optional<ZstdDecompressor> zstdDecompressor;

    value_type& body;
    size_t sent = 0;
    size_t fileBytesRead = 0;
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
            // Nvidia modified: keep per-chunk file reads at DEBUG to avoid
            // journal flooding during large satellite firmware relays.
            BMCWEB_LOG_DEBUG("Reading {}", readReq);
            boost::system::error_code readEc;
            size_t read = body.file().read(fileReadBuf.data(), readReq, readEc);
            if (readEc)
            {
                if (readEc == boost::system::errc::operation_would_block ||
                    readEc ==
                        boost::system::errc::resource_unavailable_try_again)
                {
                    if (read == 0)
                    {
                        ec = readEc;
                        return boost::none;
                    }
                    readEc = {};
                }
                else
                {
                    BMCWEB_LOG_CRITICAL("Failed to read from file {}",
                                        readEc.message());
                    ec = readEc;
                    return boost::none;
                }
            }

            std::string_view chunkView(fileReadBuf.data(), read);
            // Nvidia code starts here
            BMCWEB_LOG_DEBUG("Read {} bytes from file", read);
            fileBytesRead += read;
            // Detect EOF by byte count; pipes can short-read.
            const auto* fb = std::get_if<FileBody>(&body.bodyData);
            // Zero-length read with a pending request is EOF; skip if
            // readReq==0 (caller retry).
            if (read == 0 && readReq > 0)
            {
                if (fb != nullptr && fb->fileSize &&
                    fileBytesRead < *fb->fileSize)
                {
                    // Upstream closed before delivering the declared
                    // Content-Length. Fail the response so the client sees a
                    // truncated transfer rather than a hung 200.
                    BMCWEB_LOG_ERROR(
                        "Upstream closed early: got {} of {} bytes, failing response",
                        fileBytesRead, *fb->fileSize);
                    ec = boost::beast::http::error::partial_message;
                    return boost::none;
                }
                ret.second = false;
            }
            else if (fb != nullptr && fb->fileSize)
            {
                ret.second = fileBytesRead < *fb->fileSize;
            }
            else
            {
                ret.second = read != 0;
            }
            // Nvidia code ends here
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
        // Nvidia code starts here
        BMCWEB_LOG_DEBUG("Returning {} bytes more={}", ret.first.size(),
                         // Nvidia code ends here
                         ret.second);
        return ret;
    }
};

class HttpBody::reader
{
    value_type& value;
    // Nvidia code starts here
    std::optional<MultipartParser> multipartParser;
    bool multipartParserFailed = false;
    const boost::beast::http::fields& hdr;
    // Nvidia code ends here

    bool handleMultipartError(ParserError state, boost::beast::error_code& ec)
    {
        if (multipartParser && multipartParser->callbacks &&
            multipartParser->callbacks->onParseError)
        {
            // Let the streaming consumer build the error response, then
            // discard the remaining body without turning a request error into
            // a transport failure.
            multipartParserFailed = true;
            multipartParser->callbacks->onParseError(state);
            ec = {};
            return true;
        }
        ec = {boost::system::errc::invalid_argument,
              boost::system::generic_category()};
        return false;
    }

  public:
    template <bool IsRequest, class Fields>
    // Nvidia code starts here
    reader(boost::beast::http::header<IsRequest, Fields>& headers,
           value_type& body) : value(body), hdr(headers)
    // Nvidia code ends here
    {}

    void init(const boost::optional<std::uint64_t>& contentLength,
              boost::beast::error_code& ec)
    {
        // Nvidia code starts here
        std::string_view contentType =
            hdr[boost::beast::http::field::content_type];

        if (contentType.starts_with("multipart/form-data"))
        {
            BMCWEB_LOG_DEBUG("Processing multipart/form-data");
            if (!contentLength)
            {
                BMCWEB_LOG_ERROR("Content-Length header not found");
                ec = {boost::system::errc::invalid_argument,
                      boost::system::generic_category()};
                return;
            }
            MultipartParser& mp = multipartParser.emplace(*contentLength);
            if (value.multipartParserCallbacks)
            {
                BMCWEB_LOG_DEBUG("Setting multipart parser callbacks");
                MultipartParserStreamingCallbacks cbs =
                    *value.multipartParserCallbacks;
                if (cbs.onStart)
                {
                    // Capture the shared backpressure state by value so the
                    // thunks stay valid even if the body is moved or destroyed
                    // before the consumer is done with them.
                    std::shared_ptr<BackpressureState> bp =
                        value.getBackpressureState();
                    auto pause = [bp]() { bp->paused = true; };
                    auto resume = [bp]() {
                        if (!bp->paused)
                        {
                            return;
                        }
                        bp->paused = false;
                        auto cb = std::move(bp->resumeCallback);
                        bp->resumeCallback = nullptr;
                        if (cb)
                        {
                            cb();
                        }
                    };
                    cbs.onStart(std::move(pause), std::move(resume));
                }
                mp.callbacks.emplace(std::move(cbs));
                value.multipartParserCallbacks.reset();
            }

            ParserError state = mp.start(contentType);
            if (state != ParserError::PARSER_SUCCESS)
            {
                BMCWEB_LOG_ERROR("Failed to parse content-type: {}",
                                 contentType);
                handleMultipartError(state, ec);
                return;
            }

            ec = {};
            return;
        }

        // Nvidia code ends here
        if (contentLength && !value.file().is_open() &&
            !value.streamingReceiver)
        {
            constexpr size_t maxReserveSize =
                1024UL * 1024UL * BMCWEB_HTTP_BODY_LIMIT;

            if (*contentLength > maxReserveSize)
            {
                BMCWEB_LOG_WARNING(
                    "Content-Length {} exceeds max body size {}, rejecting.",
                    *contentLength, maxReserveSize);
                ec = boost::beast::http::error::body_limit;
                return;
            }

            value.str().reserve(
                std::min(static_cast<size_t>(*contentLength), maxReserveSize));
        }
        ec = {};
    }

    template <class ConstBufferSequence>
    std::size_t put(const ConstBufferSequence& buffers,
                    boost::system::error_code& ec)
    {
        size_t extra = boost::beast::buffer_bytes(buffers);
        // Nvidia code starts here
        // BMCWEB_LOG_DEBUG("http body put called with {} bytes", extra);
        // Nvidia code ends here
        for (const auto b : boost::beast::buffers_range_ref(buffers))
        {
            const char* ptr = static_cast<const char*>(b.data());
            // Nvidia code starts here
            if (multipartParser)
            {
                if (multipartParserFailed)
                {
                    continue;
                }
                std::string_view buf(ptr, b.size());
                ParserError state = multipartParser->parsePart(buf);
                if (state != ParserError::PARSER_SUCCESS)
                {
                    BMCWEB_LOG_ERROR("Failed to parse part: {}",
                                     static_cast<int>(state));
                    if (!handleMultipartError(state, ec))
                    {
                        return 0;
                    }
                }
            }
            else
            {
                value.str().append(ptr, b.size());
            }
            // Nvidia code ends here
        }
        ec = {};
        return extra;
    }

    // Nvidia code starts here
    void finish(boost::beast::error_code& ec)
    {
        if (multipartParser)
        {
            if (!multipartParserFailed)
            {
                ParserError state = multipartParser->finish();
                if (state != ParserError::PARSER_SUCCESS)
                {
                    BMCWEB_LOG_ERROR("Failed to finish multipart parser: {}",
                                     static_cast<int>(state));
                    if (!handleMultipartError(state, ec))
                    {
                        return;
                    }
                }
            }
            if (multipartParserFailed && multipartParser->callbacks)
            {
                if (multipartParser->callbacks->onParseComplete)
                {
                    multipartParser->callbacks->onParseComplete();
                }
                multipartParser->callbacks.reset();
            }
            value.bodyData =
                MultiPartBody{std::move(multipartParser->mime_fields)};
        }
        // Nvidia code ends here
        ec = {};
    }
};

inline std::uint64_t HttpBody::size(const value_type& body)
{
    std::optional<size_t> payloadSize = body.payloadSize();
    return payloadSize.value_or(0U);
}

} // namespace bmcweb
