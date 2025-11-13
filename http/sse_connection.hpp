/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "bmcweb_config.h"

#include "async_resolve.hpp"
#include "http_body.hpp"
#include "http_client.hpp"
#include "http_response.hpp"
#include "logging.hpp"
#include "ssl_key_handler.hpp"

#include <openssl/ssl.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/url_view.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace crow
{

/**
 * @brief Class dedicated to Server-Sent Events (SSE) connections
 *
 * Provides SSE-specific functionality like event parsing and streaming
 * data handling. This is a standalone implementation that manages its
 * own connection lifecycle independent of ConnectionInfo.
 */
class SSEConnection : public std::enable_shared_from_this<SSEConnection>
{
  private:
    ConnState state = ConnState::initialized;

    std::string id;
    boost::urls::url host;

    // Data buffers
    boost::beast::flat_static_buffer<httpReadBufferSize> buffer;
    boost::beast::http::request<bmcweb::HttpBody> req;
    using Resolver = std::conditional_t<BMCWEB_DNS_RESOLVER == "systemd-dbus",
                                        async_resolve::Resolver,
                                        boost::asio::ip::tcp::resolver>;
    Resolver resolver;

    boost::asio::ip::tcp::socket conn;
    std::optional<boost::asio::ssl::stream<boost::asio::ip::tcp::socket&>>
        sslConn;
    boost::asio::steady_timer timer;

    std::move_only_function<void(boost::system::error_code, std::string_view)>
        sseDataCallback;
    std::move_only_function<void(crow::Response&)> sseInitialCallback;

    // sseRecvMessage to read SSE event
    void sseRecvMessage()
    {
        state = ConnState::recvInProgress;
        BMCWEB_LOG_DEBUG("Starting async read for streaming data");

        // Prepare buffer space for reading
        auto preparedBuffer = buffer.prepare(httpReadBufferSize);

        if (sslConn)
        {
            sslConn->async_read_some(
                preparedBuffer, std::bind_front(&SSEConnection::sseAfterRead,
                                                this, shared_from_this()));
        }
        else
        {
            conn.async_read_some(preparedBuffer,
                                 std::bind_front(&SSEConnection::sseAfterRead,
                                                 this, shared_from_this()));
        }
    }

    // sseAfterRead callback to handle SSE data
    void sseAfterRead(const std::shared_ptr<SSEConnection>& /*self*/,
                      const boost::beast::error_code& ec,
                      const std::size_t& bytesTransferred)
    {
        if (ec == boost::asio::error::operation_aborted)
        {
            BMCWEB_LOG_DEBUG("SSE read operation aborted");
            state = ConnState::closed;
            return;
        }

        if (ec == boost::asio::error::eof ||
            ec == boost::asio::ssl::error::stream_truncated)
        {
            BMCWEB_LOG_WARNING("SSE connection closed by peer");
            state = ConnState::recvFailed;
            // Handle reconnect in callback function
            if (sseDataCallback != nullptr)
            {
                sseDataCallback(ec, "");
            }
            return;
        }

        if (ec)
        {
            BMCWEB_LOG_ERROR("SSE streaming read failed: {} from {}",
                             ec.message(), host.buffer());
            state = ConnState::recvFailed;
            // Handle reconnect in callback function
            if (sseDataCallback != nullptr)
            {
                sseDataCallback(ec, "");
            }
            return;
        }

        BMCWEB_LOG_DEBUG("SSE afterStreamingRead() bytes: {}",
                         bytesTransferred);

        buffer.commit(bytesTransferred);
        std::string data;
        {
            auto bufferData = buffer.data();
            data.assign(static_cast<const char*>(bufferData.data()),
                        bufferData.size());
        }
        buffer.consume(buffer.size());

        // Process the SSE data
        if (sseDataCallback != nullptr)
        {
            sseDataCallback(boost::system::error_code{}, data);
        }

        // Continue reading if we're still connected
        state = ConnState::recvInProgress;
        sseRecvMessage();
    }

    void doResolve()
    {
        state = ConnState::resolveInProgress;
        BMCWEB_LOG_DEBUG("Trying to resolve: {}, id: {}", host, id);

        resolver.async_resolve(host.encoded_host_address(), host.port(),
                               std::bind_front(&SSEConnection::afterResolve,
                                               this, shared_from_this()));
    }

    void afterResolve(const std::shared_ptr<SSEConnection>& /*self*/,
                      const boost::system::error_code& ec,
                      const Resolver::results_type& endpointList)
    {
        if (ec || (endpointList.empty()))
        {
            BMCWEB_LOG_ERROR("Resolve failed: {} {}", ec.message(), host);
            state = ConnState::resolveFailed;
            return;
        }
        BMCWEB_LOG_DEBUG("Resolved {}, id: {}", host, id);
        state = ConnState::connectInProgress;

        BMCWEB_LOG_DEBUG("Trying to connect to: {}, id: {}", host, id);

        timer.expires_after(std::chrono::seconds(5));
        timer.async_wait(std::bind_front(onTimeout, weak_from_this()));

        boost::asio::async_connect(conn, endpointList,
                                   std::bind_front(&SSEConnection::afterConnect,
                                                   this, shared_from_this()));
    }

    void afterConnect(const std::shared_ptr<SSEConnection>& /*self*/,
                      const boost::beast::error_code& ec,
                      const boost::asio::ip::tcp::endpoint& endpoint)
    {
        if (ec && ec == boost::asio::error::operation_aborted)
        {
            return;
        }

        timer.cancel();
        if (ec)
        {
            BMCWEB_LOG_ERROR("Connect {}:{}, id: {} failed: {}",
                             host.encoded_host_address(), host.port(), id,
                             ec.message());
            state = ConnState::connectFailed;
            return;
        }
        BMCWEB_LOG_DEBUG("Connected to: {}:{}, id: {}",
                         endpoint.address().to_string(), endpoint.port(), id);
        if (sslConn)
        {
            doSslHandshake();
            return;
        }
        state = ConnState::connected;
        sendMessage();
    }

    void doSslHandshake()
    {
        if (!sslConn)
        {
            return;
        }
        auto& ssl = *sslConn;
        state = ConnState::handshakeInProgress;
        timer.expires_after(std::chrono::seconds(30));
        timer.async_wait(std::bind_front(onTimeout, weak_from_this()));
        ssl.async_handshake(boost::asio::ssl::stream_base::client,
                            std::bind_front(&SSEConnection::afterSslHandshake,
                                            this, shared_from_this()));
    }

    void afterSslHandshake(const std::shared_ptr<SSEConnection>& /*self*/,
                           const boost::beast::error_code& ec)
    {
        if (ec && ec == boost::asio::error::operation_aborted)
        {
            return;
        }

        timer.cancel();
        if (ec)
        {
            BMCWEB_LOG_ERROR("SSL Handshake failed - id: {} error: {}", id,
                             ec.message());
            state = ConnState::handshakeFailed;
            return;
        }
        BMCWEB_LOG_DEBUG("SSL Handshake successful - id: {}", id);
        state = ConnState::connected;
        sendMessage();
    }

    void sendMessage()
    {
        state = ConnState::sendInProgress;

        timer.expires_after(std::chrono::seconds(30));
        timer.async_wait(std::bind_front(onTimeout, weak_from_this()));

        if (sslConn)
        {
            boost::beast::http::async_write(
                *sslConn, req,
                std::bind_front(&SSEConnection::afterWrite, this,
                                shared_from_this()));
        }
        else
        {
            boost::beast::http::async_write(
                conn, req,
                std::bind_front(&SSEConnection::afterWrite, this,
                                shared_from_this()));
        }
    }

    void afterWrite(const std::shared_ptr<SSEConnection>& /*self*/,
                    const boost::beast::error_code& ec, size_t bytesTransferred)
    {
        if (ec && ec == boost::asio::error::operation_aborted)
        {
            return;
        }

        timer.cancel();
        if (ec)
        {
            BMCWEB_LOG_ERROR("sendMessage() failed: {} {}", ec.message(), host);
            state = ConnState::sendFailed;
            return;
        }
        BMCWEB_LOG_DEBUG("sendMessage() bytes transferred: {}",
                         bytesTransferred);

        sseRecvMessage();
    }

    static void onTimeout(const std::weak_ptr<SSEConnection>& weakSelf,
                          const boost::system::error_code& ec)
    {
        if (ec == boost::asio::error::operation_aborted)
        {
            BMCWEB_LOG_DEBUG(
                "async_wait failed since the operation is aborted");
            return;
        }
        if (ec)
        {
            BMCWEB_LOG_ERROR("async_wait failed: {}", ec.message());
        }
        std::shared_ptr<SSEConnection> self = weakSelf.lock();
        if (self == nullptr)
        {
            return;
        }
        BMCWEB_LOG_ERROR("SSE connection timeout");
        self->state = ConnState::closed;
    }

  public:
    SSEConnection(
        boost::asio::io_context& iocIn, const std::string& idIn,
        const boost::urls::url_view_base& urlIn,
        const boost::beast::http::fields& headers,
        std::move_only_function<void(boost::system::error_code,
                                     std::string_view)>
            sseDataCallbackIn,
        std::move_only_function<void(crow::Response&)> sseInitialCallbackIn) :
        id(idIn), host(urlIn), resolver(iocIn), conn(iocIn), timer(iocIn),
        sseDataCallback(std::move(sseDataCallbackIn)),
        sseInitialCallback(std::move(sseInitialCallbackIn))
    {
        req.method(boost::beast::http::verb::get);
        req.target(host.encoded_target());
        req.version(11);
        req.set(boost::beast::http::field::host, host.encoded_host_address());
        req.set(boost::beast::http::field::accept, "text/event-stream");
        for (const auto& field : headers)
        {
            req.set(field.name_string(), field.value());
        }

        if (host.scheme() == "https")
        {
            std::optional<boost::asio::ssl::context> sslCtx =
                ensuressl::getSSLClientContext(
                    ensuressl::VerifyCertificate::Verify);

            if (!sslCtx)
            {
                BMCWEB_LOG_ERROR("prepareSSLContext failed - {}, id: {}", host,
                                 id);
                state = ConnState::sslInitFailed;
                return;
            }
            sslConn.emplace(conn, *sslCtx);

            if (host.host_type() == boost::urls::host_type::name)
            {
                std::string hostname(host.encoded_host_address());
                if (SSL_set_tlsext_host_name(sslConn->native_handle(),
                                             hostname.data()) == 0)
                {
                    BMCWEB_LOG_ERROR("SSL_set_tlsext_host_name failed for {}",
                                     host);
                    state = ConnState::sslInitFailed;
                    return;
                }
            }
        }
    }

    void connect()
    {
        doResolve();
    }

    ~SSEConnection() = default;

    SSEConnection(const SSEConnection&) = delete;
    SSEConnection& operator=(const SSEConnection&) = delete;
    SSEConnection(SSEConnection&&) = delete;
    SSEConnection& operator=(SSEConnection&&) = delete;

    ConnState getConnectionState() const
    {
        return state;
    }
};

} // namespace crow
