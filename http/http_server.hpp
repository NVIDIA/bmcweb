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

#include <boost/asio/signal_set.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/stream_traits.hpp> // For lowest_layer_type

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
    Server(Handler* handlerIn, std::vector<Acceptor>&& acceptorsIn) :
        getCachedDateStr(std::bind_front(&self_t::getCachedDateStrImpl, this)),
        acceptors(std::move(acceptorsIn)),
        // NOLINTNEXTLINE(misc-include-cleaner)
        signals(getIoContext(), SIGINT, SIGTERM, SIGHUP), handler(handlerIn),
        adaptorCtx(nullptr), fileWatcher(getIoContext())
    {}

    std::string getCachedDateStrImpl()
    {
        std::chrono::steady_clock::time_point now =
            std::chrono::steady_clock::now();
        if (now - lastDateUpdate >= std::chrono::seconds(10))
        {
            lastDateUpdate = now;
            using std::chrono::floor;
            using std::chrono::seconds;
            using std::chrono::system_clock;
            std::chrono::time_point<system_clock, seconds> systemNow =
                floor<seconds>(system_clock::now());
            dateStr = std::format("{:%a, %d %b %Y %H:%M:%S GMT}", systemNow);
        }
        return dateStr;
    }

    void run()
    {
        BMCWEB_LOG_INFO("Server<Handler,Adaptor>::run()");
        loadCertificate();
        watchCertificateChange();

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
        adaptorCtx = ensuressl::getSslServerContext();
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
            // ast-grep-ignore: long-lambda
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

    void afterAccept(Acceptor* acceptor, SocketPtr socket, HttpType httpType,
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
        doAcceptOne(*acceptor);
    }
    void doAcceptOne(Acceptor& acceptor)
    {
        SocketPtr socket = std::make_unique<Adaptor>(getIoContext());
        Adaptor* socketPtr = socket.get();
        acceptor.acceptor.async_accept(
            *socketPtr, std::bind_front(&self_t::afterAccept, this, &acceptor,
                                        std::move(socket), acceptor.httpType));
    }

    void doAccept()
    {
        for (Acceptor& accept : acceptors)
        {
            doAcceptOne(accept);
        }
    }

  private:
    std::function<std::string()> getCachedDateStr;
    std::vector<Acceptor> acceptors;
    boost::asio::signal_set signals;

    std::string dateStr;
    std::chrono::steady_clock::time_point lastDateUpdate;

    Handler* handler;

    std::shared_ptr<boost::asio::ssl::context> adaptorCtx;
    InotifyFileWatcher fileWatcher;
};
} // namespace crow
