// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once
#include "bmcweb_config.h"

#include "async_resp.hpp"
#include "authentication.hpp"
#include "complete_response_fields.hpp"
#include "forward_unauthorized.hpp"
#include "http2_connection.hpp"
#include "http_body.hpp"
#include "http_connect_types.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "http_utility.hpp"
#include "logging.hpp"
#include "mutual_tls.hpp"
#include "nvidia_persistent_data.hpp"
#include "sessions.hpp"
#include "str_utility.hpp"
#include "utility.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/ssl/verify_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/_experimental/test/stream.hpp>
#include <boost/beast/core/buffers_generator.hpp>
#include <boost/beast/core/detect_ssl.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/flat_static_buffer.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/rfc7230.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/none.hpp>
#include <boost/optional/optional.hpp>
#include <boost/url/url_view.hpp>

#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace crow
{

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int connectionCount = 0;

// request body limit size set by the BMCWEB_HTTP_BODY_LIMIT option
constexpr uint64_t httpReqBodyLimit = 1024UL * 1024UL * BMCWEB_HTTP_BODY_LIMIT;

constexpr uint64_t loggedOutPostBodyLimit = 4096U;

constexpr uint32_t httpHeaderLimit = 8192U;

enum class DeadlineTimerType
{
    Default,
    Keepalive,
};

template <typename Adaptor, typename Handler>
class Connection :
    public std::enable_shared_from_this<Connection<Adaptor, Handler>>
{
    using self_type = Connection<Adaptor, Handler>;

  public:
    Connection(Handler* handlerIn, HttpType httpTypeIn,
               boost::asio::steady_timer&& timerIn,
               std::function<std::string()>& getCachedDateStrF,
               boost::asio::ssl::stream<Adaptor>&& adaptorIn) :
        httpType(httpTypeIn), adaptor(std::move(adaptorIn)), handler(handlerIn),
        timer(std::move(timerIn)), getCachedDateStr(getCachedDateStrF)
    {
        initParser();

        connectionCount++;

        BMCWEB_LOG_DEBUG("{} Connection created, total {}", logPtr(this),
                         connectionCount);
    }

    ~Connection()
    {
        res.releaseCompleteRequestHandler();
        cancelDeadlineTimer();

        connectionCount--;
        BMCWEB_LOG_DEBUG("{} Connection closed, total {}", logPtr(this),
                         connectionCount);
    }

    Connection(const Connection&) = delete;
    Connection(Connection&&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection& operator=(Connection&&) = delete;

    bool tlsVerifyCallback(bool preverified,
                           boost::asio::ssl::verify_context& /*ctx*/)
    {
        BMCWEB_LOG_DEBUG("{} tlsVerifyCallback called with preverified {}",
                         logPtr(this), preverified);
        const persistent_data::AuthConfigMethods& c =
            persistent_data::SessionStore::getInstance().getAuthMethodsConfig();
        if (c.tlsStrict)
        {
            BMCWEB_LOG_DEBUG(
                "{} TLS is in strict mode, returning preverified as is.",
                logPtr(this));
            return preverified;
        }
        // If tls strict mode is disabled
        // We always return true to allow full auth flow for resources that
        // don't require auth
        return true;
    }

    bool prepareMutualTls()
    {
        BMCWEB_LOG_DEBUG("prepareMutualTls");

        constexpr std::string_view id = "bmcweb";

        const char* idPtr = id.data();
        const auto* idCPtr = std::bit_cast<const unsigned char*>(idPtr);
        auto idLen = static_cast<unsigned int>(id.length());
        int ret =
            SSL_set_session_id_context(adaptor.native_handle(), idCPtr, idLen);
        if (ret == 0)
        {
            BMCWEB_LOG_ERROR("{} failed to set SSL id", logPtr(this));
            return false;
        }

        BMCWEB_LOG_DEBUG("set_verify_callback");

        boost::system::error_code ec;
        adaptor.set_verify_callback(
            std::bind_front(&self_type::tlsVerifyCallback, this), ec);
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to set verify callback {}", ec);
            return false;
        }

        return true;
    }

    void afterDetectSsl(const std::shared_ptr<self_type>& /*self*/,
                        boost::beast::error_code ec, bool isTls)
    {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Couldn't detect ssl ", ec);
            return;
        }
        BMCWEB_LOG_DEBUG("{} TLS was detected as {}", logPtr(this), isTls);
        if (isTls)
        {
            if (httpType != HttpType::HTTPS && httpType != HttpType::BOTH)
            {
                BMCWEB_LOG_WARNING(
                    "{} Connection closed due to incompatible type",
                    logPtr(this));
                return;
            }
            httpType = HttpType::HTTPS;
            adaptor.async_handshake(
                boost::asio::ssl::stream_base::server, buffer.data(),
                std::bind_front(&self_type::afterSslHandshake, this,
                                shared_from_this()));
        }
        else
        {
            if (httpType != HttpType::HTTP && httpType != HttpType::BOTH)
            {
                BMCWEB_LOG_WARNING(
                    "{} Connection closed due to incompatible type",
                    logPtr(this));
                return;
            }

            httpType = HttpType::HTTP;
            BMCWEB_LOG_INFO("Starting non-SSL session");
            doReadHeaders();
        }
    }

    void start()
    {
        BMCWEB_LOG_DEBUG("{} Connection started, total {}", logPtr(this),
                         connectionCount);
        if (connectionCount >= 200)
        {
            BMCWEB_LOG_CRITICAL("{}Max connection count exceeded.",
                                logPtr(this));
            return;
        }

        if constexpr (BMCWEB_MUTUAL_TLS_AUTH)
        {
            if (!prepareMutualTls())
            {
                BMCWEB_LOG_ERROR("{} Failed to prepare mTLS", logPtr(this));
                return;
            }
        }

        startDeadline(DeadlineTimerType::Default);

        readClientIp();
        boost::beast::async_detect_ssl(
            adaptor.next_layer(), buffer,
            std::bind_front(&self_type::afterDetectSsl, this,
                            shared_from_this()));
    }

    void afterSslHandshake(const std::shared_ptr<self_type>& /*self*/,
                           const boost::system::error_code& ec,
                           size_t bytesParsed)
    {
        buffer.consume(bytesParsed);
        if (ec)
        {
            BMCWEB_LOG_ERROR("{} SSL handshake failed", logPtr(this));
            return;
        }
        BMCWEB_LOG_DEBUG("{} SSL handshake succeeded", logPtr(this));

        if constexpr (BMCWEB_MUTUAL_TLS_AUTH)
        {
            BMCWEB_LOG_DEBUG(
                "{} Establishing mTLS session after handshake, session reused: {}",
                logPtr(this), SSL_session_reused(adaptor.native_handle()) != 0);
            mtlsSession = verifyMtlsUser(ip, adaptor.native_handle());
            if (mtlsSession != nullptr)
            {
                BMCWEB_LOG_DEBUG("{} Generated TLS session: {}", logPtr(this),
                                 mtlsSession->uniqueId);
            }
        }
        // If http2 is enabled, negotiate the protocol
        if constexpr (BMCWEB_HTTP2)
        {
            const unsigned char* alpn = nullptr;
            unsigned int alpnlen = 0;
            SSL_get0_alpn_selected(adaptor.native_handle(), &alpn, &alpnlen);
            if (alpn != nullptr)
            {
                std::string_view selectedProtocol(
                    std::bit_cast<const char*>(alpn), alpnlen);
                BMCWEB_LOG_DEBUG("ALPN selected protocol \"{}\" len: {}",
                                 selectedProtocol, alpnlen);
                if (selectedProtocol == "h2")
                {
                    upgradeToHttp2();
                    return;
                }
            }
        }

        doReadHeaders();
    }

    void initParser()
    {
        boost::beast::http::request_parser<bmcweb::HttpBody>& instance =
            parser.emplace();

        // reset header limit for newly created parser
        instance.header_limit(httpHeaderLimit);

        // Initially set no body limit. We don't yet know if the user is
        // authenticated.
        instance.body_limit(boost::none);
    }

    void upgradeToHttp2()
    {
        auto http2 = std::make_shared<HTTP2Connection<Adaptor, Handler>>(
            std::move(adaptor), handler, getCachedDateStr, httpType,
            mtlsSession);
        if (http2settings.empty())
        {
            http2->start();
        }
        else
        {
            http2->startFromSettings(http2settings);
        }
    }

    // returns whether connection was upgraded
    bool doUpgrade(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
    {
        using boost::beast::http::field;
        using boost::beast::http::token_list;

        bool isSse =
            isContentTypeAllowed(req->getHeaderValue("Accept"),
                                 http_helpers::ContentType::EventStream, false);

        bool isWebsocket = false;
        bool isH2c = false;
        // Check connection header is upgrade
        if (token_list{req->req[field::connection]}.exists("upgrade"))
        {
            BMCWEB_LOG_DEBUG("{} Connection: Upgrade header was present",
                             logPtr(this));
            // Parse if upgrade is h2c or websocket
            token_list upgrade{req->req[field::upgrade]};
            isWebsocket = upgrade.exists("websocket");
            isH2c = upgrade.exists("h2c");
            BMCWEB_LOG_DEBUG("{} Upgrade isWebsocket: {} isH2c: {}",
                             logPtr(this), isWebsocket, isH2c);
        }

        if (BMCWEB_HTTP2 && isH2c)
        {
            std::string_view base64settings = req->req["HTTP2-Settings"];
            if (utility::base64Decode<true>(base64settings, http2settings))
            {
                res.result(boost::beast::http::status::switching_protocols);
                res.addHeader(boost::beast::http::field::connection, "Upgrade");
                res.addHeader(boost::beast::http::field::upgrade, "h2c");
            }
        }

        // websocket and SSE are only allowed on GET
        if (req->req.method() == boost::beast::http::verb::get)
        {
            if (isWebsocket || isSse)
            {
                asyncResp->res.setCompleteRequestHandler(
                    [self(shared_from_this())](crow::Response& thisRes) {
                        if (thisRes.result() != boost::beast::http::status::ok)
                        {
                            // When any error occurs before handle upgradation,
                            // the result in response will be set to respective
                            // error. By default the Result will be OK (200),
                            // which implies successful handle upgrade. Response
                            // needs to be sent over this connection only on
                            // failure.
                            self->completeRequest(thisRes);
                            return;
                        }
                    });
                BMCWEB_LOG_INFO("{} Upgrading socket", logPtr(this));
                if (httpType == HttpType::HTTP)
                {
                    handler->handleUpgrade(req, asyncResp,
                                           std::move(adaptor.next_layer()));
                }
                else
                {
                    handler->handleUpgrade(req, asyncResp, std::move(adaptor));
                }

                return true;
            }
        }
        return false;
    }

    void handle()
    {
        std::error_code reqEc;
        if (!parser)
        {
            return;
        }
        req = std::make_shared<Request>(parser->release(), reqEc);
        if (reqEc)
        {
            BMCWEB_LOG_DEBUG("Request failed to construct{}", reqEc.message());
            res.result(boost::beast::http::status::bad_request);
            completeRequest(res);
            return;
        }
        req->session = userSession;
        using boost::beast::http::field;
        accept = req->getHeaderValue(field::accept);
        acceptEncoding = req->getHeaderValue(field::accept_encoding);
        // Fetch the client IP address
        req->ipAddress = ip;

        // Check for HTTP version 1.1.
        if (req &&
            req->version() == 11) // NVIDIA code for streaming: added null guard
        {
            if (req->getHeaderValue(field::host).empty())
            {
                res.result(boost::beast::http::status::bad_request);
                completeRequest(res);
                return;
            }
        }

        BMCWEB_LOG_INFO("Request:  {} HTTP/{}.{} {} {} {}", logPtr(this),
                        req->version() / 10, req->version() % 10,
                        req->methodString(), req->target(),
                        req->ipAddress.to_string());

        if (res.completed)
        {
            completeRequest(res);
            return;
        }
        keepAlive = req->keepAlive();

        if (authenticationEnabled)
        {
            if (persistent_data::nvidia::getConfig().isTLSAuthEnabled())
            {
                if (!crow::authentication::isOnAllowlist(req->url().path(),
                                                         req->method()) &&
                    req->session == nullptr)
                {
                    BMCWEB_LOG_WARNING("Authentication failed");

                    auto asyncResp =
                        std::make_shared<bmcweb::AsyncResp>(std::move(res));
                    BMCWEB_LOG_DEBUG("Setting completion handler");
                    asyncResp->res.setCompleteRequestHandler(
                        [self(shared_from_this())](crow::Response& thisRes) {
                            self->completeRequest(thisRes);
                        });
                    if (!handler->handleAuthFailed(req, asyncResp))
                    {
                        forward_unauthorized::sendUnauthorized(
                            req->url().encoded_path(),
                            req->getHeaderValue("X-Requested-With"),
                            req->getHeaderValue("Accept"), asyncResp->res);
                    }
                    return;
                }
            }
        }

        auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
        BMCWEB_LOG_DEBUG("Setting completion handler");
        asyncResp->res.setCompleteRequestHandler(
            [self(shared_from_this())](Response& thisRes) {
                self->completeRequest(thisRes);
            });
        if (doUpgrade(asyncResp))
        {
            return;
        }
        std::string_view expectedEtag =
            req->getHeaderValue(boost::beast::http::field::if_none_match);
        if (!expectedEtag.empty())
        {
            asyncResp->res.setExpectedEtag(expectedEtag);
        }

        handler->handle(req, asyncResp);
    }

    void hardClose()
    {
        BMCWEB_LOG_DEBUG("{} Closing socket", logPtr(this));
        adaptor.next_layer().close();
    }

    void tlsShutdownComplete(const std::shared_ptr<self_type>& self,
                             const boost::system::error_code& ec)
    {
        if (ec)
        {
            BMCWEB_LOG_WARNING("{} Failed to shut down TLS cleanly {}",
                               logPtr(self.get()), ec);
        }
        self->hardClose();
    }

    void gracefulClose()
    {
        BMCWEB_LOG_DEBUG("{} Socket close requested", logPtr(this));

        if (httpType == HttpType::HTTPS)
        {
            if (mtlsSession != nullptr)
            {
                BMCWEB_LOG_DEBUG("{} Removing TLS session: {}", logPtr(this),
                                 mtlsSession->uniqueId);
                persistent_data::SessionStore::getInstance().removeSession(
                    mtlsSession);
            }

            adaptor.async_shutdown(std::bind_front(
                &self_type::tlsShutdownComplete, this, shared_from_this()));
        }
        else
        {
            hardClose();
        }
    }

    void completeRequest(Response& thisRes)
    {
        res = std::move(thisRes);
        res.keepAlive(keepAlive);

        completeResponseFields(accept, acceptEncoding, res);
        res.addHeader(boost::beast::http::field::date, getCachedDateStr());

        doWrite();

        // delete lambda with self shared_ptr
        // to enable connection destruction
        res.setCompleteRequestHandler(nullptr);
    }

    void readClientIp()
    {
        utility::getClientIpAddress(adaptor, ip);
    }

    void disableAuth()
    {
        authenticationEnabled = false;
    }

  private:
    uint64_t getContentLengthLimit()
    {
        if constexpr (!BMCWEB_INSECURE_DISABLE_AUTH)
        {
            if (persistent_data::nvidia::getConfig().isTLSAuthEnabled() &&
                userSession == nullptr)
            {
                return loggedOutPostBodyLimit;
            }
        }

        return httpReqBodyLimit;
    }

    // Returns true if content length was within limits
    // Returns false if content length error has been returned
    bool handleContentLengthError()
    {
        if (!parser)
        {
            BMCWEB_LOG_CRITICAL("Parser was null");
            return false;
        }
        const boost::optional<uint64_t> contentLength =
            parser->content_length();
        if (!contentLength)
        {
            BMCWEB_LOG_DEBUG("{} No content length available", logPtr(this));
            return true;
        }

        uint64_t maxAllowedContentLength = getContentLengthLimit();

        if (*contentLength > maxAllowedContentLength)
        {
            // If the users content limit is between the logged in
            // and logged out limits They probably just didn't log
            // in
            if (*contentLength > loggedOutPostBodyLimit &&
                *contentLength < httpReqBodyLimit)
            {
                BMCWEB_LOG_DEBUG(
                    "{} Content length {} valid, but greater than logged out"
                    " limit of {}. Setting unauthorized",
                    logPtr(this), *contentLength, loggedOutPostBodyLimit);
                res.result(boost::beast::http::status::unauthorized);
            }
            else
            {
                // Otherwise they're over both limits, so inform
                // them
                BMCWEB_LOG_DEBUG(
                    "{} Content length {} was greater than global limit {}."
                    " Setting payload too large",
                    logPtr(this), *contentLength, httpReqBodyLimit);
                res.result(boost::beast::http::status::payload_too_large);
            }

            keepAlive = false;
            doWrite();
            return false;
        }

        return true;
    }

    void afterReadHeaders(const std::shared_ptr<self_type>& /*self*/,
                          const boost::system::error_code& ec,
                          std::size_t bytesTransferred)
    {
        BMCWEB_LOG_DEBUG("{} async_read_header {} Bytes", logPtr(this),
                         bytesTransferred);

        if (ec)
        {
            cancelDeadlineTimer();

            if (ec == boost::beast::http::error::header_limit)
            {
                BMCWEB_LOG_ERROR("{} Header field too large, closing",
                                 logPtr(this));

                res.result(boost::beast::http::status::
                               request_header_fields_too_large);
                keepAlive = false;
                doWrite();
                return;
            }
            BMCWEB_LOG_WARNING("{} End of stream, closing {}", logPtr(this),
                               ec);
            hardClose();
            return;
        }

        if (!parser)
        {
            BMCWEB_LOG_ERROR("Parser was unexpectedly null");
            return;
        }
        auto& parse = *parser;
        const auto& value = parser->get();

        if (authenticationEnabled)
        {
            if (persistent_data::nvidia::getConfig().isTLSAuthEnabled())
            {
                boost::beast::http::verb method = value.method();
                userSession = crow::authentication::authenticate(
                    ip, res, method, value.base(), mtlsSession);
            }
        }

        if (!handleContentLengthError())
        {
            return;
        }

        parse.body_limit(getContentLengthLimit());

        std::string_view expect = value[boost::beast::http::field::expect];
        if (bmcweb::asciiIEquals(expect, "100-continue"))
        {
            res.result(boost::beast::http::status::continue_);
            doWrite();
            return;
        }

        if (parse.is_done())
        {
            handle();
            return;
        }

        doRead();
    }

    void doReadHeaders()
    {
        BMCWEB_LOG_DEBUG("{} doReadHeaders", logPtr(this));

        startDeadline(DeadlineTimerType::Keepalive);

        if (!parser)
        {
            BMCWEB_LOG_CRITICAL("Parser was not initialized.");
            return;
        }

        if (httpType == HttpType::HTTP)
        {
            boost::beast::http::async_read_header(
                adaptor.next_layer(), buffer, *parser,
                std::bind_front(&self_type::afterReadHeaders, this,
                                shared_from_this()));
        }
        else
        {
            boost::beast::http::async_read_header(
                adaptor, buffer, *parser,
                std::bind_front(&self_type::afterReadHeaders, this,
                                shared_from_this()));
        }
    }

    void afterRead(const std::shared_ptr<self_type>& /*self*/,
                   const boost::system::error_code& ec,
                   std::size_t /*bytesTransferred*/)
    {
        // BMCWEB_LOG_DEBUG("{} async_read_some {} Bytes", logPtr(this),
        //                  bytesTransferred);

        if (ec)
        {
            BMCWEB_LOG_ERROR("{} Error while reading: {}", logPtr(this),
                             ec.message());
            if (ec == boost::beast::http::error::body_limit)
            {
                if (handleContentLengthError())
                {
                    BMCWEB_LOG_CRITICAL("Body length limit reached, "
                                        "but no content-length "
                                        "available?  Should never happen");
                    res.result(
                        boost::beast::http::status::internal_server_error);
                    keepAlive = false;
                    doWrite();
                }

                BMCWEB_LOG_WARNING("{} End of stream, closing {}", logPtr(this),
                                   ec);
                hardClose();
                return;
            }
            BMCWEB_LOG_WARNING("{} End of stream, closing {}", logPtr(this),
                               ec);
            hardClose();

            return;
        }

        // If the user is logged in, allow them to send files
        // incrementally one piece at a time. If authentication is
        // disabled then there is no user session hence always allow to
        // send one piece at a time.
        if (userSession != nullptr)
        {
            cancelDeadlineTimer();
        }

        if (!parser)
        {
            BMCWEB_LOG_ERROR("Parser was unexpectedly null");
            return;
        }
        if (!parser->is_done())
        {
            doRead();
            return;
        }

        cancelDeadlineTimer();
        handle();
    }

    void doRead()
    {
        // BMCWEB_LOG_DEBUG("{} doRead", logPtr(this));
        if (!parser)
        {
            return;
        }
        auto& parse = *parser;
        startDeadline(DeadlineTimerType::Default);
        if (httpType == HttpType::HTTP)
        {
            boost::beast::http::async_read_some(
                adaptor.next_layer(), buffer, parse,
                std::bind_front(&self_type::afterRead, this,
                                shared_from_this()));
        }
        else
        {
            boost::beast::http::async_read_some(
                adaptor, buffer, parse,
                std::bind_front(&self_type::afterRead, this,
                                shared_from_this()));
        }
    }

    void afterDoWrite(const std::shared_ptr<self_type>& /*self*/,
                      const boost::system::error_code& ec,
                      std::size_t bytesTransferred)
    {
        BMCWEB_LOG_DEBUG("{} afterDoWrite {} bytes ec={}", logPtr(this),
                         bytesTransferred, ec);

        // NVIDIA code starts for streaming: cancel the absolute streaming abort
        // timer once the write finished (replaces the original EAGAIN doWrite
        // retry). Streaming finished (cleanly or with error); cancel the
        // absolute abort timer so it doesn't fire after the wire is already
        // done.
        if (streamAbortTimer)
        {
            streamAbortTimer->cancel();
            streamAbortTimer.reset();
        }
        // NVIDIA code ends for streaming

        if (ec == boost::beast::http::error::end_of_stream ||
            ec == boost::asio::ssl::error::stream_truncated)
        {
            BMCWEB_LOG_WARNING("{} End of stream, closing {}", logPtr(this),
                               ec);
            hardClose();
            return;
        }

        if (ec)
        {
            BMCWEB_LOG_DEBUG("{} from write(2)", logPtr(this));
            return;
        }

        if (res.result() == boost::beast::http::status::switching_protocols)
        {
            upgradeToHttp2();
            return;
        }

        if (res.result() == boost::beast::http::status::continue_)
        {
            // Reset the result to ok
            res.result(boost::beast::http::status::ok);
            doRead();
            return;
        }

        // NVIDIA code starts for streaming: a streamed response is not
        // reusable; close instead of reading another request on the same
        // socket.
        if (responseWasStreaming)
        {
            BMCWEB_LOG_DEBUG(
                "{} Streaming response complete; closing socket without "
                "reading next request",
                logPtr(this));
            gracefulClose();
            return;
        }
        // NVIDIA code ends for streaming

        if (!keepAlive)
        {
            BMCWEB_LOG_DEBUG("{} keepalive not set.  Closing socket",
                             logPtr(this));

            gracefulClose();
            return;
        }

        BMCWEB_LOG_DEBUG("{} Clearing response", logPtr(this));
        res.clear();
        initParser();

        userSession = nullptr;

        req->clear();
        doReadHeaders();
    }

    void doWrite()
    {
        BMCWEB_LOG_DEBUG("{} doWrite", logPtr(this));
        boost::urls::url_view urlView;
        if (req != nullptr)
        {
            urlView = req->url();
        }

        ForceChunking chunked = ForceChunking::Disabled;
        if constexpr (BMCWEB_HTTP_CHUNKING)
        {
            if (req && req->version() == 11)
            {
                std::string_view acceptEncodings = req->getHeaderValue(
                    boost::beast::http::field::accept_encoding);
                if (http_helpers::headerContains(acceptEncodings, "chunked"))
                {
                    chunked = ForceChunking::Enabled;
                }
            }
        }
        res.preparePayload(urlView, chunked);

        // NVIDIA code starts for streaming: streaming-pipe write path. When the
        // body is a pipe, register the data-ready callback, arm an absolute
        // abort timer, and drive the write through doWriteStreamChunk() instead
        // of a single boost::beast::async_write of the whole message.
        if (res.response.body().isStreamingPipe())
        {
            // Remember that this response was a streaming pipe so afterDoWrite
            // skips doReadHeaders() and closes the connection instead of
            // attempting to reuse it for another request.
            responseWasStreaming = true;
            // Register "body data is readable" callback. The body owns the FD
            // watcher (dup + async_wait); we only hand it our executor and
            // a member-function entry point. Connection state checks
            // (writeGen / writeActive) live in onDataReady() — they are
            // connection concerns and stay on this side of the boundary.
            res.response.body().setOnReady(
                adaptor.get_executor(),
                std::bind_front(&self_type::onDataReady, shared_from_this()));

            // Absolute wall-clock cap on the whole streaming response.
            // Closing the upstream pipe alone is not enough when the client
            // is rate-limited: the kernel pipe + TCP send buffer can keep
            // the connection alive for hours after the upstream deadline.
            // Schedule a hard close here so the wire is dropped at the cap
            // regardless of per-chunk progress. The connection is kept alive
            // by the strong capture until the timer fires or is cancelled in
            // afterDoWrite().
            bmcweb::armStreamAbortTimer(
                streamAbortTimer, adaptor.get_executor(),
                res.response.body().getStreamDeadline(),
                std::format("{} HTTP/1.1", logPtr(this)),
                [self = shared_from_this()]() { self->hardClose(); });

            // Only streaming-pipe bodies use the manual
            // prepare()/async_write_some()/consume() loop, because that loop is
            // the only thing that can suspend on EAGAIN and resume when the
            // pipe FD becomes readable. Buffered responses fall through to the
            // composed async_write below.
            writeGen = std::make_unique<boost::beast::http::message_generator>(
                std::move(res.response));
            doWriteStreamChunk();
            return;
        }
        // NVIDIA code starts for streaming: file-backed (non-pipe) bodies must
        // also use the manual chunked loop, not the composed async_write below.
        // doWriteStreamChunk() resets the per-chunk response deadline on every
        // write, so a multi-GB file survives a slow downstream client. The
        // composed async_write arms startDeadline() exactly once, which becomes
        // a hard cap on the whole transfer at BMCWEB_HTTP_RESPONSE_TIMEOUT
        // (300s in the meta-layer): an HTTP/2 client draining slower than that
        // gets the connection hard-closed mid-file. file().is_open() is true
        // only for FdSource bodies; the pipe case already returned above, so
        // reaching here with an open file means a regular file body.
        if (res.response.body().file().is_open())
        {
            writeGen = std::make_unique<boost::beast::http::message_generator>(
                std::move(res.response));
            doWriteStreamChunk();
            return;
        }
        // NVIDIA code ends for streaming

        // Buffered in-memory (string, e.g. JSON) response: original bmcweb
        // write path, unchanged. A single composed async_write of the whole
        // in-memory message. Safe here because a string body is fully resident
        // and small, so it completes well within the response deadline; it
        // cannot suspend on an external FD the way a file or pipe can.
        startDeadline(DeadlineTimerType::Default);
        if (httpType == HttpType::HTTP)
        {
            boost::beast::async_write(
                adaptor.next_layer(),
                boost::beast::http::message_generator(std::move(res.response)),
                std::bind_front(&self_type::afterDoWrite, this,
                                shared_from_this()));
        }
        else
        {
            boost::beast::async_write(
                adaptor,
                boost::beast::http::message_generator(std::move(res.response)),
                std::bind_front(&self_type::afterDoWrite, this,
                                shared_from_this()));
        }
    }

    // NVIDIA code starts for streaming
    // Callback registered with the body via setOnReady(). Fires when more
    // body data becomes readable. Returns true to keep the watcher
    // re-arming, false to disarm.
    bool onDataReady(boost::system::error_code ec)
    {
        if (ec)
        {
            return false;
        }
        // writeGen is null after the body is fully serialised; guard against
        // a second afterDoWrite() call if the notifier fires post pipe EOF.
        if (!writeGen)
        {
            return false;
        }
        // A socket write is already in flight; disarm rather than spin.
        // afterWriteSome -> doWriteStreamChunk() will drain the pipe once the
        // write completes, and re-arm on the next EAGAIN if needed.
        if (writeActive)
        {
            return false;
        }
        doWriteStreamChunk();
        return true;
    }

    void doWriteStreamChunk()
    {
        if (!writeGen || writeGen->is_done())
        {
            writeGen.reset();
            afterDoWrite(shared_from_this(), {}, 0);
            return;
        }
        // Prevent concurrent writes racing on fileReadBuf.
        if (writeActive)
        {
            return;
        }
        // Per-chunk stall timer: resets after every successful buffer write.
        cancelDeadlineTimer();
        startDeadline(DeadlineTimerType::Default);

        // Use prepare()/async_write_some()/consume() for broad Boost version
        // compatibility.
        boost::system::error_code prepEc{};
        auto buf = writeGen->prepare(prepEc);
        if (prepEc)
        {
            // EAGAIN: no data yet; the body writer re-arms the notifier
            // lazily (from getWithMaxSize) so doWriteStreamChunk() will be
            // called when data arrives.
            if (prepEc == boost::system::errc::operation_would_block ||
                prepEc == boost::system::errc::resource_unavailable_try_again)
            {
                return;
            }
            cancelDeadlineTimer();
            writeGen.reset();
            BMCWEB_LOG_ERROR("{} write prepare error: {}", logPtr(this),
                             prepEc.message());
            // Hard-close so the client detects the error without waiting for
            // stall timer.
            hardClose();
            return;
        }
        if (boost::asio::buffer_size(buf) == 0)
        {
            // CL serializer stalls on empty buf (pipe EOF): skip straight to
            // afterDoWrite().
            BMCWEB_LOG_DEBUG(
                "{} prepare returned empty buffer (pipe EOF), closing",
                logPtr(this));
            cancelDeadlineTimer();
            writeGen.reset();
            afterDoWrite(shared_from_this(), {}, 0);
            return;
        }

        writeActive = true;
        auto afterWrite =
            [self = shared_from_this(),
             this](boost::system::error_code ec, std::size_t transferred) {
                writeActive = false;
                if (transferred > 0)
                {
                    writeGen->consume(transferred);
                }
                afterWriteSome(self, ec, transferred);
            };
        if (httpType == HttpType::HTTP)
        {
            adaptor.next_layer().async_write_some(buf, std::move(afterWrite));
        }
        else
        {
            adaptor.async_write_some(buf, std::move(afterWrite));
        }
    }

    void afterWriteSome(const std::shared_ptr<self_type>& /*self*/,
                        const boost::system::error_code& ec,
                        std::size_t /*bytesTransferred*/)
    {
        cancelDeadlineTimer();

        if (ec == boost::system::errc::operation_would_block ||
            ec == boost::system::errc::resource_unavailable_try_again)
        {
            doWriteStreamChunk();
            return;
        }

        if (ec)
        {
            writeGen.reset();
            BMCWEB_LOG_DEBUG("{} write error: {}", logPtr(this), ec.message());
            return;
        }

        if (writeGen && !writeGen->is_done())
        {
            doWriteStreamChunk();
            return;
        }

        writeGen.reset();
        afterDoWrite(shared_from_this(), {}, 0);
    }
    // NVIDIA code ends for streaming

    void cancelDeadlineTimer()
    {
        timer.cancel();
        timerStarted = false;
    }

    void afterTimerWait(const std::weak_ptr<self_type>& weakSelf,
                        const boost::system::error_code& ec)
    {
        // Note, we are ignoring other types of errors here;  If the timer
        // failed for any reason, we should still close the connection
        std::shared_ptr<Connection<Adaptor, Handler>> self = weakSelf.lock();
        if (!self)
        {
            if (ec == boost::asio::error::operation_aborted)
            {
                BMCWEB_LOG_DEBUG(
                    "{} Timer canceled on connection being destroyed",
                    logPtr(self.get()));
            }
            else
            {
                BMCWEB_LOG_CRITICAL("{} Failed to capture connection",
                                    logPtr(self.get()));
            }
            return;
        }

        self->timerStarted = false;

        if (ec)
        {
            if (ec == boost::asio::error::operation_aborted)
            {
                // BMCWEB_LOG_DEBUG("{} Timer canceled", logPtr(self.get()));
                return;
            }
            BMCWEB_LOG_CRITICAL("{} Timer failed {}", logPtr(self.get()), ec);
        }

        BMCWEB_LOG_WARNING("{} Connection timed out, hard closing",
                           logPtr(self.get()));

        self->hardClose();
    }

    void startDeadline(DeadlineTimerType timerType)
    {
        // Timer is already started so no further action is required.
        if (timerStarted)
        {
            return;
        }

        int timeoutDurationSeconds = BMCWEB_HTTP_RESPONSE_TIMEOUT;
        if (timerType == DeadlineTimerType::Keepalive)
        {
            // if we're waiting for future requests while idle in keepalive,
            // allow up to 15 minutes of delay
            timeoutDurationSeconds = 15 * 60;
        }

        std::chrono::seconds timeout(timeoutDurationSeconds);

        std::weak_ptr<Connection<Adaptor, Handler>> weakSelf = weak_from_this();
        timer.expires_after(timeout);
        timer.async_wait(std::bind_front(&self_type::afterTimerWait, this,
                                         weak_from_this()));

        timerStarted = true;
        // BMCWEB_LOG_DEBUG("{} timer started", logPtr(this));
    }
    bool authenticationEnabled = !BMCWEB_INSECURE_DISABLE_AUTH;
    HttpType httpType = HttpType::BOTH;

    boost::asio::ssl::stream<Adaptor> adaptor;
    Handler* handler;

    boost::asio::ip::address ip;

    // Making this a std::optional allows it to be efficiently destroyed and
    // re-created on Connection reset
    std::optional<boost::beast::http::request_parser<bmcweb::HttpBody>> parser;

    boost::beast::flat_static_buffer<8192> buffer;

    std::shared_ptr<Request> req;
    std::string accept;
    std::string http2settings;
    std::string acceptEncoding;

    Response res;

    std::shared_ptr<persistent_data::UserSession> userSession;
    std::shared_ptr<persistent_data::UserSession> mtlsSession;

    boost::asio::steady_timer timer;

    // NVIDIA code starts for streaming
    // Wall-clock cap on a single streaming response. Distinct from `timer`
    // (per-chunk wire stall, reset on every write) — this one is set once at
    // streaming start from body.getStreamDeadline() and forces hardClose at
    // the absolute time, regardless of per-chunk progress. Needed because a
    // slow client can keep the per-chunk timer perpetually reset by ack-ing
    // one byte at a time, so the existing 60s deadline never fires.
    std::optional<boost::asio::steady_timer> streamAbortTimer;
    // NVIDIA code ends for streaming

    bool keepAlive = true;

    // NVIDIA code starts for streaming
    // Set in doWrite() when the response body is a streaming pipe.
    // afterDoWrite() reads this (post-completion) to close the connection
    // instead of calling doReadHeaders() to read another request on the same
    // socket.
    bool responseWasStreaming = false;
    // NVIDIA code ends for streaming

    bool timerStarted = false;

    // NVIDIA code starts for streaming
    // Holds the response generator across per-chunk async_write_some calls.
    // Using a pointer so we can detect "write in progress" vs "idle".
    std::unique_ptr<boost::beast::http::message_generator> writeGen;

    // Prevents concurrent writes from racing on fileReadBuf.
    bool writeActive = false;
    // NVIDIA code ends for streaming

    std::function<std::string()>& getCachedDateStr;

    using std::enable_shared_from_this<
        Connection<Adaptor, Handler>>::shared_from_this;

    using std::enable_shared_from_this<
        Connection<Adaptor, Handler>>::weak_from_this;
};
} // namespace crow
