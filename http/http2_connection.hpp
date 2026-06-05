// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include "bmcweb_config.h"

#include "async_resp.hpp"
#include "authentication.hpp"
#include "complete_response_fields.hpp"
#include "forward_unauthorized.hpp"
#include "http_body.hpp"
#include "http_connect_types.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "logging.hpp"
#include "nvidia_http_body_streaming.hpp" // NVIDIA code for streaming
#include "utility.hpp"

// NOLINTNEXTLINE(misc-include-cleaner)
#include "nghttp2_adapters.hpp"
#include "sessions.hpp"

#include <nghttp2/nghttp2.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/optional/optional.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/url_view.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace crow
{

struct Http2StreamData
{
    std::shared_ptr<Request> req = std::make_shared<Request>();
    std::optional<bmcweb::HttpBody::reader> reqReader;
    std::string accept;
    std::string acceptEnc;
    boost::optional<uint64_t> contentLength;
    Response res;
    std::optional<bmcweb::HttpBody::writer> writer;
    bool valid = true;
    // NVIDIA code starts for streaming
    // Wall-clock cap for streaming responses; mirrors HTTP/1.1's
    // streamAbortTimer. Without this, a slow client perpetually re-arms the
    // per-frame flow-control window, so the body's writer is rarely re-asked
    // for data and the upstream pipe drain alone keeps the stream alive.
    std::optional<boost::asio::steady_timer> streamAbortTimer;
    // NVIDIA code ends for streaming
};

template <typename Adaptor, typename Handler>
class HTTP2Connection :
    public std::enable_shared_from_this<HTTP2Connection<Adaptor, Handler>>
{
    using self_type = HTTP2Connection<Adaptor, Handler>;
    static constexpr size_t frameSize = 65536;
    // NVIDIA code starts for streaming: flow-control constants hoisted (were
    // locals in sendServerConnectionHeader) so the stream-creation path can
    // reuse them. HTTP/2 flow-control sizing. Tuned experimentally to allow a
    // single fast stream to keep parity with HTTP/1.1 throughput; see
    // sendServerConnectionHeader().
    static constexpr uint32_t http2MaxFrameSize = 1U << 14;        // 16 KiB
    static constexpr uint32_t http2InitialWindowSize = 1U << 20;   // 1 MiB
    static constexpr uint32_t http2StreamWindowSize = 16384U * 32; // 512 KiB
    // NVIDIA code ends for streaming

  public:
    HTTP2Connection(
        boost::asio::ssl::stream<Adaptor>&& adaptorIn, Handler* handlerIn,
        std::function<std::string()>& getCachedDateStrF, HttpType httpTypeIn,
        const std::shared_ptr<persistent_data::UserSession>& mtlsSessionIn) :
        httpType(httpTypeIn), adaptor(std::move(adaptorIn)),
        ngSession(initializeNghttp2Session()), handler(handlerIn),
        getCachedDateStr(getCachedDateStrF), mtlsSession(mtlsSessionIn)
    {}

    void start()
    {
        // Create the control stream
        streams[0];

        if (sendServerConnectionHeader() != 0)
        {
            BMCWEB_LOG_ERROR("send_server_connection_header failed");
            return;
        }
        readClientIp();
        doRead();
    }

    void startFromSettings(std::string_view http2UpgradeSettings)
    {
        int ret = ngSession.sessionUpgrade2(http2UpgradeSettings,
                                            false /*head_request*/);
        if (ret != 0)
        {
            BMCWEB_LOG_ERROR("Failed to load upgrade header");
            return;
        }
        // Create the control stream
        streams[0];

        if (sendServerConnectionHeader() != 0)
        {
            BMCWEB_LOG_ERROR("send_server_connection_header failed");
            return;
        }
        readClientIp();
        doRead();
    }

    int sendServerConnectionHeader()
    {
        BMCWEB_LOG_DEBUG("send_server_connection_header()");

        uint32_t maxStreams = 4;

        std::array<nghttp2_settings_entry, 4> iv = {{
            {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, maxStreams},
            {NGHTTP2_SETTINGS_ENABLE_PUSH, 0},
            // NVIDIA code for streaming: use hoisted flow-control constants
            {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, http2InitialWindowSize},
            {NGHTTP2_SETTINGS_MAX_FRAME_SIZE, http2MaxFrameSize},
        }};
        if (ngSession.setLocalWindowSize(NGHTTP2_FLAG_NONE, 0,
                                         http2InitialWindowSize) != 0)
        {
            BMCWEB_LOG_ERROR("Failed to set local window size");
        }
        int rv = ngSession.submitSettings(iv);
        if (rv != 0)
        {
            BMCWEB_LOG_ERROR("Fatal error: {}", nghttp2_strerror(rv));
            return -1;
        }
        writeBuffer();
        return 0;
    }

    // NVIDIA code starts for streaming: data-ready callback registered on a
    // streaming body; resumes a DEFERRED stream when more pipe data arrives.
    static bool onDataReady(const std::weak_ptr<self_type>& selfWeak,
                            int32_t streamId, boost::system::error_code ec)
    {
        auto s = selfWeak.lock();
        if (!s)
        {
            return false;
        }
        if (ec)
        {
            BMCWEB_LOG_ERROR("body data-ready notifier error: {}", ec);
            // Un-defer the stream so fileReadCallback runs and surfaces
            // EOF/error to nghttp2 instead of leaving it stuck DEFERRED.
            s->ngSession.resumeData(streamId);
            s->writeBuffer();
            return false;
        }
        s->ngSession.resumeData(streamId);
        s->writeBuffer();
        return true;
    }
    // NVIDIA code ends for streaming

    static ssize_t fileReadCallback(
        nghttp2_session* /* session */, int32_t streamId, uint8_t* buf,
        size_t length, uint32_t* dataFlags, nghttp2_data_source* /*source*/,
        void* userPtr)
    {
        self_type& self = userPtrToSelf(userPtr);

        auto streamIt = self.streams.find(streamId);
        if (streamIt == self.streams.end())
        {
            return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
        }
        Http2StreamData& stream = streamIt->second;
        BMCWEB_LOG_DEBUG("File read callback length: {}", length);
        if (!stream.writer)
        {
            return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
        }
        // NVIDIA code starts for streaming: register the data-ready callback
        // before the first read so an EAGAIN can arm the pipe watcher, and
        // treat EAGAIN as NGHTTP2_ERR_DEFERRED instead of a fatal callback
        // failure. Register the "body data is readable" callback up-front.
        // getWithMaxSize() invokes body.armNotifier() on EAGAIN, and
        // armNotifier creates the FD watcher only if onReadyCb is already set.
        // Calling setOnReady() here (before getWithMaxSize) guarantees the very
        // first EAGAIN can arm the watcher; otherwise the stream goes DEFERRED
        // with no one to wake it. Idempotent: a second call is a no-op for the
        // same body.
        stream.res.response.body().setOnReady(
            self.adaptor.get_executor(),
            std::bind_front(&self_type::onDataReady, self.weak_from_this(),
                            streamId));
        // NVIDIA code ends for streaming
        boost::beast::error_code ec;
        boost::optional<std::pair<boost::asio::const_buffer, bool>> out =
            stream.writer->getWithMaxSize(ec, length);
        if (ec)
        {
            // NVIDIA code starts for streaming
            if (ec == boost::system::errc::operation_would_block ||
                ec == boost::system::errc::resource_unavailable_try_again)
            {
                BMCWEB_LOG_DEBUG(
                    "fileReadCallback: no body data ready, deferring "
                    "stream {}",
                    streamId);
                return NGHTTP2_ERR_DEFERRED;
            }
            // NVIDIA code ends for streaming
            BMCWEB_LOG_CRITICAL("Failed to get buffer: {}", ec);
            return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
        }
        if (!out)
        {
            BMCWEB_LOG_ERROR("Empty file, setting EOF");
            *dataFlags |= NGHTTP2_DATA_FLAG_EOF;
            return 0;
        }

        BMCWEB_LOG_DEBUG("Send chunk of size: {}", out->first.size());
        if (length < out->first.size())
        {
            BMCWEB_LOG_CRITICAL(
                "Buffer overflow that should never happen happened");
            // Should never happen because of length limit on get() above
            return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
        }
        boost::asio::mutable_buffer writeableBuf(buf, length);
        BMCWEB_LOG_DEBUG("Copying {} bytes to buf", out->first.size());
        size_t copied = boost::asio::buffer_copy(writeableBuf, out->first);
        if (copied != out->first.size())
        {
            BMCWEB_LOG_ERROR(
                "Couldn't copy all {} bytes into buffer, only copied {}",
                out->first.size(), copied);
            return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
        }

        if (!out->second)
        {
            BMCWEB_LOG_DEBUG("Setting EOF flag");
            *dataFlags |= NGHTTP2_DATA_FLAG_EOF;
        }
        return static_cast<ssize_t>(copied);
    }

    nghttp2_nv headerFromStringViews(std::string_view name,
                                     std::string_view value, uint8_t flags)
    {
        uint8_t* nameData = std::bit_cast<uint8_t*>(name.data());
        uint8_t* valueData = std::bit_cast<uint8_t*>(value.data());
        return {nameData, valueData, name.size(), value.size(), flags};
    }

    int sendResponse(Response& completedRes, int32_t streamId)
    {
        BMCWEB_LOG_DEBUG("send_response stream_id:{}", streamId);

        auto it = streams.find(streamId);
        if (it == streams.end())
        {
            close();
            return -1;
        }
        Http2StreamData& stream = it->second;
        Response& res = stream.res;
        res = std::move(completedRes);

        completeResponseFields(stream.accept, stream.acceptEnc, res);
        res.addHeader(boost::beast::http::field::date, getCachedDateStr());
        boost::urls::url_view urlView;
        if (stream.req != nullptr)
        {
            urlView = stream.req->url();
        }
        res.preparePayload(urlView);

        boost::beast::http::fields& fields = res.fields();
        // NVIDIA code for streaming: strip HTTP/1.1-only hop-by-hop headers for
        // the HTTP/2 wire
        bmcweb::prepareResponseHeadersForWireFormat(
            fields, bmcweb::HttpResponseWireFormat::Http2);

        std::string code = std::to_string(res.resultInt());
        std::vector<nghttp2_nv> hdr;
        hdr.emplace_back(
            headerFromStringViews(":status", code, NGHTTP2_NV_FLAG_NONE));
        for (const boost::beast::http::fields::value_type& header : fields)
        {
            hdr.emplace_back(headerFromStringViews(
                header.name_string(), header.value(), NGHTTP2_NV_FLAG_NONE));
        }
        http::response<bmcweb::HttpBody>& fbody = res.response;
        stream.writer.emplace(fbody.base(), fbody.body());

        nghttp2_data_provider dataPrd{
            .source = {.fd = 0},
            .read_callback = fileReadCallback,
        };

        int rv = ngSession.submitResponse(streamId, hdr, &dataPrd);
        if (rv != 0)
        {
            BMCWEB_LOG_ERROR("Fatal error: {}", nghttp2_strerror(rv));
            close();
            return -1;
        }

        // NVIDIA code starts for streaming: absolute wall-clock abort timer for
        // this stream. See Http2StreamData::streamAbortTimer for rationale. On
        // expiry the whole HTTP/2 connection is closed to mirror HTTP/1.1's
        // hardClose behavior at the deadline. Weak capture: the timer lives in
        // the stream owned by this connection, so a strong self would be a
        // reference cycle.
        bmcweb::armStreamAbortTimer(
            stream.streamAbortTimer, adaptor.get_executor(),
            res.response.body().getStreamDeadline(),
            std::format("HTTP/2 stream {}", streamId),
            [weakSelf = this->weak_from_this(), streamId]() {
                auto self = weakSelf.lock();
                if (!self)
                {
                    BMCWEB_LOG_WARNING(
                        "HTTP/2 stream {} streamAbortTimer fired but connection gone",
                        streamId);
                    return;
                }
                self->close();
            });
        // NVIDIA code ends for streaming

        writeBuffer();

        return 0;
    }

    nghttp2_session initializeNghttp2Session()
    {
        nghttp2_session_callbacks callbacks;
        callbacks.setOnFrameRecvCallback(onFrameRecvCallbackStatic);
        callbacks.setOnStreamCloseCallback(onStreamCloseCallbackStatic);
        callbacks.setOnHeaderCallback(onHeaderCallbackStatic);
        callbacks.setOnBeginHeadersCallback(onBeginHeadersCallbackStatic);
        callbacks.setOnDataChunkRecvCallback(onDataChunkRecvStatic);

        nghttp2_session session(callbacks);
        session.setUserData(this);

        return session;
    }

    int onRequestRecv(int32_t streamId)
    {
        BMCWEB_LOG_DEBUG("onRequestRecv streamId:{}", streamId);

        auto it = streams.find(streamId);
        if (it == streams.end())
        {
            close();
            return -1;
        }
        auto& reqReader = it->second.reqReader;
        if (reqReader)
        {
            boost::beast::error_code ec;
            bmcweb::HttpBody::reader::finish(ec);
            if (ec)
            {
                BMCWEB_LOG_CRITICAL("Failed to finalize payload");
                close();
                return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
            }
        }
        Request& thisReq = *it->second.req;
        using boost::beast::http::field;
        it->second.accept = thisReq.getHeaderValue(field::accept);
        it->second.acceptEnc = thisReq.getHeaderValue(field::accept_encoding);
        thisReq.ipAddress = ip;

        BMCWEB_LOG_DEBUG("Handling {} \"{}\"", logPtr(&thisReq),
                         thisReq.url().encoded_path());

        Response& thisRes = it->second.res;

        thisRes.setCompleteRequestHandler(
            [weakSelf = weak_from_this(), streamId](Response& completeRes) {
                BMCWEB_LOG_DEBUG("res.completeRequestHandler called");
                if (auto self = weakSelf.lock(); self)
                {
                    if (self->sendResponse(completeRes, streamId) != 0)
                    {
                        self->close();
                        return;
                    }
                }
            });

        auto asyncResp =
            std::make_shared<bmcweb::AsyncResp>(std::move(it->second.res));
        if (!it->second.valid)
        {
            asyncResp->res.result(boost::beast::http::status::bad_request);
            return 0;
        }
        if constexpr (!BMCWEB_INSECURE_DISABLE_AUTH)
        {
            thisReq.session = crow::authentication::authenticate(
                ip, asyncResp->res, thisReq.method(), thisReq.req, mtlsSession);
            if (!crow::authentication::isOnAllowlist(thisReq.url().path(),
                                                     thisReq.method()) &&
                thisReq.session == nullptr)
            {
                BMCWEB_LOG_WARNING("Authentication failed");
                if (!handler->handleAuthFailed(it->second.req, asyncResp))
                {
                    forward_unauthorized::sendUnauthorized(
                        thisReq.url().encoded_path(),
                        thisReq.getHeaderValue("X-Requested-With"),
                        thisReq.getHeaderValue("Accept"), asyncResp->res);
                }
                return 0;
            }
        }
        std::string_view expectedEtag =
            thisReq.getHeaderValue(boost::beast::http::field::if_none_match);
        BMCWEB_LOG_DEBUG("Setting expected etag {}", expectedEtag);
        if (!expectedEtag.empty())
        {
            asyncResp->res.setExpectedEtag(expectedEtag);
        }
        handler->handle(it->second.req, asyncResp);
        return 0;
    }

    int onDataChunkRecvCallback(uint8_t /*flags*/, int32_t streamId,
                                const uint8_t* data, size_t len)
    {
        auto thisStream = streams.find(streamId);
        if (thisStream == streams.end())
        {
            BMCWEB_LOG_ERROR("Unknown stream{}", streamId);
            close();
            return -1;
        }

        std::optional<bmcweb::HttpBody::reader>& reqReader =
            thisStream->second.reqReader;
        if (!reqReader)
        {
            Request::Body& req = thisStream->second.req->req;
            reqReader.emplace(req.base(), req.body());
            boost::beast::error_code initEc;
            reqReader->init(thisStream->second.contentLength, initEc);
            if (initEc)
            {
                BMCWEB_LOG_CRITICAL("Failed to initialize payload");
                return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
            }
        }
        boost::beast::error_code ec;
        reqReader->put(boost::asio::const_buffer(data, len), ec);
        if (ec)
        {
            BMCWEB_LOG_CRITICAL("Failed to write payload");
            return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
        }
        return 0;
    }

    static int onDataChunkRecvStatic(
        nghttp2_session* /* session */, uint8_t flags, int32_t streamId,
        const uint8_t* data, size_t len, void* userData)
    {
        BMCWEB_LOG_DEBUG("onDataChunkRecvStatic");
        if (userData == nullptr)
        {
            BMCWEB_LOG_CRITICAL("user data was null?");
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
        return userPtrToSelf(userData).onDataChunkRecvCallback(
            flags, streamId, data, len);
    }

    int onFrameRecvCallback(const nghttp2_frame& frame)
    {
        BMCWEB_LOG_DEBUG("frame type {}", static_cast<int>(frame.hd.type));
        switch (frame.hd.type)
        {
            case NGHTTP2_DATA:
            case NGHTTP2_HEADERS:
                // Check that the client request has finished
                if ((frame.hd.flags & NGHTTP2_FLAG_END_STREAM) != 0)
                {
                    return onRequestRecv(frame.hd.stream_id);
                }
                break;
            default:
                break;
        }
        return 0;
    }

    static int onFrameRecvCallbackStatic(nghttp2_session* /* session */,
                                         const nghttp2_frame* frame,
                                         void* userData)
    {
        BMCWEB_LOG_DEBUG("on_frame_recv_callback.  Frame type {}",
                         static_cast<int>(frame->hd.type));
        if (userData == nullptr)
        {
            BMCWEB_LOG_CRITICAL("user data was null?");
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
        if (frame == nullptr)
        {
            BMCWEB_LOG_CRITICAL("frame was null?");
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
        return userPtrToSelf(userData).onFrameRecvCallback(*frame);
    }

    static self_type& userPtrToSelf(void* userData)
    {
        // This method exists to keep the unsafe reinterpret cast in one
        // place.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return *reinterpret_cast<self_type*>(userData);
    }

    static int onStreamCloseCallbackStatic(nghttp2_session* /* session */,
                                           int32_t streamId,
                                           uint32_t /*unused*/, void* userData)
    {
        BMCWEB_LOG_DEBUG("on_stream_close_callback stream {}", streamId);
        if (userData == nullptr)
        {
            BMCWEB_LOG_CRITICAL("user data was null?");
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
        // NVIDIA code starts for streaming: look the stream up so the pipe
        // notifier and the abort timer can be cancelled before the stream is
        // erased (the upstream version just erased by id).
        auto& self = userPtrToSelf(userData);
        auto it = self.streams.find(streamId);
        if (it == self.streams.end())
        {
            BMCWEB_LOG_ERROR("onStreamCloseCallback: stream {} not found",
                             streamId);
            return -1;
        }
        // Cancel before erase to prevent resumeData() on a recycled stream id.
        it->second.res.response.body().cancelNotifier();
        // Cancel the per-stream wall-clock abort timer; otherwise it would
        // fire later and hardClose() a healthy connection that may still be
        // serving other streams.
        if (it->second.streamAbortTimer)
        {
            it->second.streamAbortTimer->cancel();
            it->second.streamAbortTimer.reset();
        }
        self.streams.erase(it);
        // NVIDIA code ends for streaming
        return 0;
    }

    int onHeaderCallback(const nghttp2_frame& frame,
                         std::span<const uint8_t> name,
                         std::span<const uint8_t> value)
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        std::string_view nameSv(reinterpret_cast<const char*>(name.data()),
                                name.size());
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        std::string_view valueSv(reinterpret_cast<const char*>(value.data()),
                                 value.size());

        BMCWEB_LOG_DEBUG("on_header_callback name: {} value {}", nameSv,
                         valueSv);
        if (frame.hd.type != NGHTTP2_HEADERS)
        {
            return 0;
        }
        if (frame.headers.cat != NGHTTP2_HCAT_REQUEST)
        {
            return 0;
        }
        auto thisStream = streams.find(frame.hd.stream_id);
        if (thisStream == streams.end())
        {
            BMCWEB_LOG_ERROR("Unknown stream{}", frame.hd.stream_id);
            close();
            return -1;
        }

        Request& thisReq = *thisStream->second.req;

        if (nameSv == ":path")
        {
            if (!thisReq.target(valueSv))
            {
                BMCWEB_LOG_WARNING("Rejecting request with invalid path");
                thisStream->second.valid = false;
            }
        }
        else if (nameSv == ":method")
        {
            boost::beast::http::verb verb =
                boost::beast::http::string_to_verb(valueSv);
            if (verb == boost::beast::http::verb::unknown)
            {
                BMCWEB_LOG_ERROR("Unknown http verb {}", valueSv);
                verb = boost::beast::http::verb::trace;
            }
            thisReq.method(verb);
        }
        else if (nameSv.starts_with(":"))
        {
            // Ignore all other http2 headers
            // :scheme and :authority are other valid http2 fields that might
            // show up here.
        }
        else
        {
            thisReq.addHeader(nameSv, valueSv);
            if (nameSv == "content-length")
            {
                uint64_t contentLength = 0;
                auto [ptr, err] = std::from_chars(valueSv.begin(),
                                                  valueSv.end(), contentLength);
                if (err != std::errc() || ptr != valueSv.end())
                {
                    BMCWEB_LOG_ERROR("Invalid content length {}", valueSv);
                    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
                }
                thisStream->second.contentLength = contentLength;
            }
        }
        return 0;
    }

    static int onHeaderCallbackStatic(
        nghttp2_session* /* session */, const nghttp2_frame* frame,
        const uint8_t* name, size_t namelen, const uint8_t* value,
        size_t vallen, uint8_t /* flags */, void* userData)
    {
        if (userData == nullptr)
        {
            BMCWEB_LOG_CRITICAL("user data was null?");
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
        if (frame == nullptr)
        {
            BMCWEB_LOG_CRITICAL("frame was null?");
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
        if (name == nullptr)
        {
            BMCWEB_LOG_CRITICAL("name was null?");
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
        if (value == nullptr)
        {
            BMCWEB_LOG_CRITICAL("value was null?");
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
        return userPtrToSelf(userData).onHeaderCallback(*frame, {name, namelen},
                                                        {value, vallen});
    }

    int onBeginHeadersCallback(const nghttp2_frame& frame)
    {
        if (frame.hd.type == NGHTTP2_HEADERS &&
            frame.headers.cat == NGHTTP2_HCAT_REQUEST)
        {
            BMCWEB_LOG_DEBUG("create stream for id {}", frame.hd.stream_id);

            streams[frame.hd.stream_id];
            if (ngSession.setLocalWindowSize(NGHTTP2_FLAG_NONE,
                                             frame.hd.stream_id,
                                             http2StreamWindowSize) !=
                0) // NVIDIA code for streaming: hoisted constant
            {
                BMCWEB_LOG_ERROR("Failed to set local window size");
            }
        }
        return 0;
    }

    static int onBeginHeadersCallbackStatic(nghttp2_session* /* session */,
                                            const nghttp2_frame* frame,
                                            void* userData)
    {
        BMCWEB_LOG_DEBUG("on_begin_headers_callback");
        if (userData == nullptr)
        {
            BMCWEB_LOG_CRITICAL("user data was null?");
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
        if (frame == nullptr)
        {
            BMCWEB_LOG_CRITICAL("frame was null?");
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
        return userPtrToSelf(userData).onBeginHeadersCallback(*frame);
    }

    static void afterWriteBuffer(const std::shared_ptr<self_type>& self,
                                 const boost::system::error_code& ec,
                                 size_t sendLength)
    {
        self->isWriting = false;
        BMCWEB_LOG_DEBUG("Sent {}", sendLength);
        if (ec)
        {
            self->close();
            return;
        }
        self->writeBuffer();
    }

    void writeBuffer()
    {
        if (isWriting)
        {
            return;
        }
        std::span<const uint8_t> data = ngSession.memSend();
        if (data.empty())
        {
            return;
        }
        isWriting = true;
        if (httpType == HttpType::HTTPS)
        {
            boost::asio::async_write(
                adaptor, boost::asio::const_buffer(data.data(), data.size()),
                std::bind_front(afterWriteBuffer, shared_from_this()));
        }
        else if (httpType == HttpType::HTTP)
        {
            boost::asio::async_write(
                adaptor.next_layer(),
                boost::asio::const_buffer(data.data(), data.size()),
                std::bind_front(afterWriteBuffer, shared_from_this()));
        }
    }

    void close()
    {
        adaptor.next_layer().close();
    }

    void afterDoRead(const std::shared_ptr<self_type>& /*self*/,
                     const boost::system::error_code& ec,
                     size_t bytesTransferred)
    {
        BMCWEB_LOG_DEBUG("{} async_read_some {} Bytes", logPtr(this),
                         bytesTransferred);

        if (ec)
        {
            // EOF is normal when client closes HTTP/2 connection
            // Only log non-EOF errors
            if (ec != boost::asio::error::eof)
            {
                BMCWEB_LOG_ERROR("{} Error while reading: {}", logPtr(this),
                                 ec.message());
            }
            close();
            BMCWEB_LOG_DEBUG("{} from read(1)", logPtr(this));
            return;
        }
        std::span<uint8_t> bufferSpan{inBuffer.data(), bytesTransferred};

        ssize_t readLen = ngSession.memRecv(bufferSpan);
        if (readLen < 0)
        {
            BMCWEB_LOG_ERROR("nghttp2_session_mem_recv returned {}", readLen);
            close();
            return;
        }
        writeBuffer();

        doRead();
    }

    void doRead()
    {
        BMCWEB_LOG_DEBUG("{} doRead", logPtr(this));
        if (httpType == HttpType::HTTPS)
        {
            adaptor.async_read_some(boost::asio::buffer(inBuffer),
                                    std::bind_front(&self_type::afterDoRead,
                                                    this, shared_from_this()));
        }
        else if (httpType == HttpType::HTTP)
        {
            adaptor.next_layer().async_read_some(
                boost::asio::buffer(inBuffer),
                std::bind_front(&self_type::afterDoRead, this,
                                shared_from_this()));
        }
    }

    void readClientIp()
    {
        utility::getClientIpAddress(adaptor, ip);
    }

    // A mapping from http2 stream ID to Stream Data
    std::map<int32_t, Http2StreamData> streams;
    // Add the 9 octets for the frame header so we can unpack one full
    // data frame at a time
    std::array<uint8_t, frameSize + 9> inBuffer{};

    HttpType httpType = HttpType::BOTH;
    boost::asio::ssl::stream<Adaptor> adaptor;
    bool isWriting = false;

    nghttp2_session ngSession;

    Handler* handler;

    boost::asio::ip::address ip;

    std::function<std::string()>& getCachedDateStr;

    std::shared_ptr<persistent_data::UserSession> mtlsSession;

    using std::enable_shared_from_this<
        HTTP2Connection<Adaptor, Handler>>::shared_from_this;

    using std::enable_shared_from_this<
        HTTP2Connection<Adaptor, Handler>>::weak_from_this;
};
} // namespace crow
