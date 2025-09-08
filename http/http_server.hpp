// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "bmcweb_config.h"

#include "asn1.hpp"
#include "file_watcher.hpp"
#include "http_connect_types.hpp"
#include "http_connection.hpp"
#include "io_context_singleton.hpp"
#include "logging.hpp"
#include "lsp.hpp"
#include "nvidia_ssl_key_handler.hpp"
#include "ssl_key_handler.hpp"

#include <openssl/pem.h> // For PEM_read_PrivateKey
#include <sys/inotify.h> // For IN_CLOSE_WRITE

#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/beast/core.hpp> // For lowest_layer_type

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdio> // For FILE
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace crow
{

struct Acceptor
{
    boost::asio::ip::tcp::acceptor acceptor;
    HttpType httpType;
};

template <typename Handler, typename Adaptor = boost::asio::ip::tcp::socket>
class Server
{
    using self_t = Server<Handler, Adaptor>;

  public:
    Server(Handler* handlerIn, std::vector<Acceptor>&& acceptorsIn,
           std::shared_ptr<boost::asio::ssl::context> adaptorCtxIn,
           boost::asio::io_context& io) :
        acceptors(std::move(acceptorsIn)),
        // NOLINTNEXTLINE(misc-include-cleaner)
        signals(io, SIGINT, SIGTERM, SIGHUP), handler(handlerIn),
        adaptorCtx(std::move(adaptorCtxIn)), fileWatcher(io)
    {}

    void updateDateStr()
    {
        time_t lastTimeT = time(nullptr);
        tm myTm{};

        gmtime_r(&lastTimeT, &myTm);

        dateStr.resize(100);
        size_t dateStrSz = strftime(dateStr.data(), dateStr.size() - 1,
                                    "%a, %d %b %Y %H:%M:%S GMT", &myTm);
        dateStr.resize(dateStrSz);
    }

    void run()
    {
        BMCWEB_LOG_INFO("Server<Handler,Adaptor>::run()");
        loadCertificate();
        watchCertificateChange();
        updateDateStr();

        getCachedDateStr = [this]() -> std::string {
            static std::chrono::time_point<std::chrono::steady_clock>
                lastDateUpdate = std::chrono::steady_clock::now();
            if (std::chrono::steady_clock::now() - lastDateUpdate >=
                std::chrono::seconds(10))
            {
                lastDateUpdate = std::chrono::steady_clock::now();
                updateDateStr();
            }
            return dateStr;
        };

        for (const Acceptor& accept : acceptors)
        {
            BMCWEB_LOG_INFO(
                "bmcweb server is running, local endpoint {}",
                accept.acceptor.local_endpoint().address().to_string());
        }
        startAsyncWaitForSignal();
        doAccept();
    }

    void loadCertificate()
    {
        if constexpr (BMCWEB_INSECURE_DISABLE_SSL)
        {
            return;
        }
        if constexpr (std::is_same<Adaptor,
                                   boost::asio::ssl::stream<
                                       boost::asio::ip::tcp::socket>>::value)
        {
            auto sslContext = ensuressl::getSslServerContext();

            adaptorCtx = sslContext;
            handler->ssl(std::move(sslContext));
        }
    }

    bool fileHasCredentials(const std::string& filename)
    {
        FILE* fp = fopen(filename.c_str(), "r");
        if (fp == nullptr)
        {
            BMCWEB_LOG_ERROR("Cannot open filename for reading: {}", filename);
            return false;
        }
        BMCWEB_LOG_INFO("Opened {}", filename);
        return PEM_read_PrivateKey(fp, nullptr, lsp::passwordCallback,
                                   nullptr) != nullptr;
    }

    void ensureCredentialsAreEncrypted(const std::string& filename)
    {
        bool isEncrypted = false;
        asn1::pemPkeyIsEncrypted(filename, &isEncrypted);
        if (!isEncrypted)
        {
            BMCWEB_LOG_INFO("Credentials are not encrypted, encrypting.");
            ensuressl::encryptCredentials(filename);
        }
    }

    void watchCertificateChange()
    {
        fileWatcher.setup();
        fileWatcher.addPath("/etc/ssl/certs/https/", IN_CLOSE_WRITE);
        fileWatcher.watch([&](const std::vector<FileWatcherEvent>& events) {
            for (const auto& ev : events)
            {
                std::string filename = ev.path + ev.name;
                if (fileHasCredentials(filename))
                {
                    BMCWEB_LOG_INFO("Written file has credentials.");
                    ensureCredentialsAreEncrypted(filename);
                }
            }
        });
        adaptorCtx = ensuressl::getSslServerContext();
    }

    void startAsyncWaitForSignal()
    {
        signals.async_wait(
            [this](const boost::system::error_code& ec, int signalNo) {
                if (ec)
                {
                    BMCWEB_LOG_INFO("Error in signal handler{}", ec.message());
                }
                else
                {
                    if (signalNo == SIGHUP)
                    {
                        BMCWEB_LOG_INFO("Receivied reload signal");
                        loadCertificate();
                        startAsyncWaitForSignal();
                    }
                    else
                    {
                        getIoContext().stop();
                    }
                }
            });
    }

    using AcceptSocket = boost::asio::ip::tcp::socket;
    using SocketPtr = std::unique_ptr<AcceptSocket>;

    void afterAccept(SocketPtr socket, HttpType httpType,
                     const boost::system::error_code& ec)
    {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to accept socket {}", ec);
            return;
        }

        boost::asio::steady_timer timer(getIoContext());
        if (adaptorCtx == nullptr)
        {
            adaptorCtx = std::make_shared<boost::asio::ssl::context>(
                boost::asio::ssl::context::tls_server);
        }

        boost::asio::ssl::stream<Adaptor> stream(std::move(*socket),
                                                 *adaptorCtx);
        using ConnectionType = Connection<Adaptor, Handler>;
        auto connection = std::make_shared<ConnectionType>(
            handler, httpType, std::move(timer), getCachedDateStr,
            std::move(stream));

        boost::asio::post(getIoContext(),
                          [connection] { connection->start(); });

        doAccept();
    }

    void doAccept()
    {
        SocketPtr socket = std::make_unique<AcceptSocket>(getIoContext());
        // Keep a raw pointer so when the socket is moved, the pointer is still
        // valid
        AcceptSocket* socketPtr = socket.get();
        for (Acceptor& accept : acceptors)
        {
            accept.acceptor.async_accept(
                *socketPtr,
                std::bind_front(&self_t::afterAccept, this, std::move(socket),
                                accept.httpType));
        }
    }

  private:
    std::function<std::string()> getCachedDateStr;
    std::vector<Acceptor> acceptors;
    boost::asio::signal_set signals;
    std::string dateStr;

    Handler* handler;

    std::shared_ptr<boost::asio::ssl::context> adaptorCtx;
    InotifyFileWatcher fileWatcher;
};
} // namespace crow
