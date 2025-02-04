#pragma once

#include "async_resp.hpp"
#include "http_connection.hpp"
#include "http_request.hpp"
#include "http_server.hpp"
#include "logging.hpp"
#include "privileges.hpp"
#include "routing.hpp"
#include "utility.hpp"

#include <systemd/sd-daemon.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <utility>

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage, clang-diagnostic-unused-macros)
#define BMCWEB_ROUTE(app, url)                                                 \
    app.template route<crow::utility::getParameterTag(url)>(url)

namespace crow
{
class App
{
  public:
    using ssl_socket_t = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;
    using raw_socket_t = boost::asio::ip::tcp::socket;

    using raw_server_type = Server<App, raw_socket_t>;
    using ssl_server_type = Server<App, ssl_socket_t>;

    explicit App(std::shared_ptr<boost::asio::io_context> ioIn =
                     std::make_shared<boost::asio::io_context>()) :
        io(std::move(ioIn))
    {}
    ~App()
    {
        stop();
    }

    App(const App&) = delete;
    App(App&&) = delete;
    App& operator=(const App&) = delete;
    App& operator=(const App&&) = delete;

    template <typename Adaptor>
    void handleUpgrade(const std::shared_ptr<Request>& req,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       Adaptor&& adaptor)
    {
        router.handleUpgrade(req, asyncResp, std::forward<Adaptor>(adaptor));
    }

    void handle(const std::shared_ptr<Request>& req,
                const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
    {
        router.handle(req, asyncResp);
    }

    DynamicRule& routeDynamic(const std::string& rule)
    {
        return router.newRuleDynamic(rule);
    }

    template <uint64_t Tag>
    auto& route(std::string&& rule)
    {
        return router.newRuleTagged<Tag>(std::move(rule));
    }

    void validate()
    {
        router.validate();
    }

    void loadCertificate()
    {
#ifdef BMCWEB_ENABLE_SSL
        if (persistent_data::getConfig().isTLSAuthEnabled())
        {
            if (!sslServer)
            {
                return;
            }
            sslServer->loadCertificate();
        }
#endif
    }

    std::optional<boost::asio::ip::tcp::acceptor> setupSocket()
    {
        if (io == nullptr)
        {
            BMCWEB_LOG_CRITICAL("IO was nullptr?");
            return std::nullopt;
        }
        constexpr int defaultPort = 18080;
        int listenFd = sd_listen_fds(0);
        if (listenFd == 1)
        {
            BMCWEB_LOG_INFO("attempting systemd socket activation");
            if (sd_is_socket_inet(SD_LISTEN_FDS_START, AF_UNSPEC, SOCK_STREAM,
                                  1, 0) != 0)
            {
                BMCWEB_LOG_INFO("Starting webserver on socket handle {}",
                                SD_LISTEN_FDS_START);
                return boost::asio::ip::tcp::acceptor(
                    *io, boost::asio::ip::tcp::v6(), SD_LISTEN_FDS_START);
            }
            BMCWEB_LOG_ERROR(
                "bad incoming socket, starting webserver on port {}",
                defaultPort);
        }
        BMCWEB_LOG_INFO("Starting webserver on port {}", defaultPort);
        return boost::asio::ip::tcp::acceptor(
            *io, boost::asio::ip::tcp::endpoint(
                     boost::asio::ip::make_address("0.0.0.0"), defaultPort));
    }

    void run()
    {
        validate();

        std::optional<boost::asio::ip::tcp::acceptor> acceptor = setupSocket();
        if (!acceptor)
        {
            BMCWEB_LOG_CRITICAL("Couldn't start server");
            return;
        }
#ifdef BMCWEB_ENABLE_SSL
        if (persistent_data::getConfig().isTLSAuthEnabled())
        {
            BMCWEB_LOG_INFO("TLS RUN");
            sslServer = std::make_unique<ssl_server_type>(
                this, std::move(*acceptor), sslContext, io);
            sslServer->run();
        }
        else
        {
            BMCWEB_LOG_INFO("NON TLS RUN");
            rawServer = std::make_unique<raw_server_type>(
                this, std::move(*acceptor), sslContext, io);
            rawServer->run();
        }
#else
        BMCWEB_LOG_INFO("NON TLS RUN");
        rawServer = std::make_unique<raw_server_type>(
            this, std::move(*acceptor), sslContext, io);
        rawServer->run();
#endif
    }

    void stop()
    {
        io->stop();
    }

    void debugPrint()
    {
        BMCWEB_LOG_DEBUG("Routing:");
        router.debugPrint();
    }

    std::vector<const std::string*> getRoutes()
    {
        const std::string root;
        return router.getRoutes(root);
    }
    std::vector<const std::string*> getRoutes(const std::string& parent)
    {
        return router.getRoutes(parent);
    }

    App& ssl(std::shared_ptr<boost::asio::ssl::context>&& ctx)
    {
        sslContext = std::move(ctx);
        BMCWEB_LOG_INFO("app::ssl context use_count={}",
                        sslContext.use_count());
        return *this;
    }

    std::shared_ptr<boost::asio::ssl::context> sslContext = nullptr;

    boost::asio::io_context& ioContext()
    {
        return *io;
    }

  private:
    std::shared_ptr<boost::asio::io_context> io;

    Router router;
    std::unique_ptr<ssl_server_type> sslServer;
    std::unique_ptr<raw_server_type> rawServer;
};
} // namespace crow
using App = crow::App;
