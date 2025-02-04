/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include "log_services.hpp"

#include <sys/select.h>

#include <boost/asio.hpp>
#include <boost/asio/basic_socket_acceptor.hpp>
#include <boost/asio/basic_stream_socket.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/beast/core/flat_static_buffer.hpp>
#include <boost/beast/http.hpp>
#include <http_stream.hpp>

namespace crow
{
namespace obmc_dump
{

static std::string unixSocketPathDir = "/var/lib/bmcweb/";

inline void handleDumpOffloadUrl(const crow::Request& req, crow::Response& res,
                                 const std::string& entryId,
                                 const std::string& dumpEntryType);
inline void resetHandler();

static constexpr size_t socketBufferSize = static_cast<size_t>(10 * 64 * 1024);
static constexpr uint8_t maxConnectRetryCount = 3;

/** class Handler
 *  Handles data transfer between unix domain socket and http connection socket.
 *  This handler invokes dump offload reads data from unix domain socket
 *  and writes on to http stream connection socket.
 */
class Handler : public std::enable_shared_from_this<Handler>
{
  public:
    Handler(boost::asio::io_context& ios, const std::string& entryIDIn,
            const std::string& dumpTypeIn,
            const std::string& unixSocketPathIn) :
        entryID(entryIDIn),
        dumpType(dumpTypeIn),
        outputBuffer(boost::beast::flat_static_buffer<socketBufferSize>()),
        unixSocketPath(unixSocketPathIn), unixSocket(ios), dumpSize(0),
        waitTimer(ios), connectRetryCount(0)
    {}

    /**
     * @brief Connects to unix socket to read dump data
     *
     * @return void
     */
    void doConnect()
    {
        this->unixSocket.async_connect(
            unixSocketPath.c_str(),
            [this, self(shared_from_this())](boost::system::error_code ec) {
            if (ec)
            {
                // TODO:
                // right now we don't have dbus method which can make sure
                // unix socket is ready to accept connection so its possible
                // that bmcweb can try to connect to socket before even
                // socket setup, so using retry mechanism with timeout.
                if (ec == boost::system::errc::no_such_file_or_directory ||
                    ec == boost::system::errc::connection_refused)
                {
                    BMCWEB_LOG_DEBUG("UNIX Socket: async_connect {}{}",
                                     ec.message(), ec);
                    retrySocketConnect();
                    return;
                }
                BMCWEB_LOG_ERROR("UNIX Socket: async_connect error {}{}",
                                 ec.message(), ec);
                waitTimer.cancel();
                this->connection->sendStreamErrorStatus(
                    boost::beast::http::status::internal_server_error);
                this->connection->close();
                return;
            }
            waitTimer.cancel();
            this->connection->sendStreamHeaders(std::to_string(this->dumpSize),
                                                "application/octet-stream");
            this->doReadStream();
        });
    }

    /**
     * @brief  Invokes InitiateOffload method of dump manager which
     *         directs dump manager to start writing on unix domain socket.
     *
     * @return void
     */
    void initiateOffload()
    {
        crow::connections::systemBus->async_method_call(
            [this,
             self(shared_from_this())](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
                this->connection->sendStreamErrorStatus(
                    boost::beast::http::status::internal_server_error);
                this->connection->close();
                return;
            }
        },
            "xyz.openbmc_project.Dump.Manager",
            "/xyz/openbmc_project/dump/" + dumpType + "/entry/" + entryID,
            "xyz.openbmc_project.Dump.Entry", "InitiateOffload",
            unixSocketPath.c_str());
    }

    /**
     * @brief  This function setup a timer for retrying unix socket connect.
     *
     * @return void
     */
    void retrySocketConnect()
    {
        waitTimer.expires_after(std::chrono::milliseconds(500));

        waitTimer.async_wait([this, self(shared_from_this())](
                                 const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Async_wait failed {}", ec);
                return;
            }

            if (connectRetryCount < maxConnectRetryCount)
            {
                BMCWEB_LOG_DEBUG(
                    "Calling doConnect() by checking retry count: {}",
                    connectRetryCount);
                connectRetryCount++;
                doConnect();
            }
            else
            {
                BMCWEB_LOG_ERROR(
                    "Failed to connect, reached max retry count: {}",
                    connectRetryCount);
                waitTimer.cancel();
                this->connection->sendStreamErrorStatus(
                    boost::beast::http::status::internal_server_error);
                this->connection->close();
                return;
            }
        });
    }

    void getDumpSize(const std::string& entryID, const std::string& dumpType)
    {
        crow::connections::systemBus->async_method_call(
            [this,
             self(shared_from_this())](const boost::system::error_code ec,
                                       const std::variant<uint64_t>& size) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error: Unable to get the dump size {}", ec);
                this->connection->sendStreamErrorStatus(
                    boost::beast::http::status::internal_server_error);
                this->connection->close();
                return;
            }
            const uint64_t* dumpsize = std::get_if<uint64_t>(&size);
            this->dumpSize = *dumpsize;
            this->initiateOffload();
            this->doConnect();
        },
            "xyz.openbmc_project.Dump.Manager",
            "/xyz/openbmc_project/dump/" + dumpType + "/entry/" + entryID,
            "org.freedesktop.DBus.Properties", "Get",
            "xyz.openbmc_project.Dump.Entry", "Size");
    }

    /**
     * @brief  Reads data from unix domain socket and writes on
     *         http stream connection socket.
     *
     * @return void
     */

    void doReadStream()
    {
        std::size_t bytes = outputBuffer.capacity() - outputBuffer.size();

        this->unixSocket.async_read_some(
            outputBuffer.prepare(bytes),
            [this, self(shared_from_this())](
                const boost::system::error_code& ec, std::size_t bytesRead) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Couldn't read from local peer: {}", ec);

                if (ec != boost::asio::error::eof)
                {
                    BMCWEB_LOG_ERROR("Couldn't read from local peer: {}", ec);
                    this->connection->sendStreamErrorStatus(
                        boost::beast::http::status::internal_server_error);
                }
                this->connection->close();
                return;
            }

            outputBuffer.commit(bytesRead);
            auto streamHandler = [this, bytesRead](bool error) {
                this->outputBuffer.consume(bytesRead);
                if (!error)
                {
                    this->doReadStream();
                }
                else
                {
                    this->unixSocket.close();
                }
            };
            this->connection->sendMessage(outputBuffer.data(), streamHandler);
        });
    }

    std::string entryID;
    std::string dumpType;
    boost::beast::flat_static_buffer<socketBufferSize> outputBuffer;
    std::filesystem::path unixSocketPath;
    boost::asio::local::stream_protocol::socket unixSocket;
    uint64_t dumpSize;
    boost::asio::steady_timer waitTimer;
    std::shared_ptr<crow::streaming_response::Connection> connection = nullptr;
    uint16_t connectRetryCount;
};

static boost::container::flat_map<crow::streaming_response::Connection*,
                                  std::shared_ptr<Handler>>
    handlers;

inline void requestRoutes(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/" +
                          std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                          "/LogServices/Dump/Entries/<str>/attachment/")
        .privileges({{"ConfigureComponents", "ConfigureManager"}})
        .streamingResponse()
        .onopen([](crow::streaming_response::Connection& conn) {
        std::string url(conn.req.target());
        std::filesystem::path dumpIdPath(
            url.substr(0, url.find("/attachment")));
        std::string dumpId = dumpIdPath.filename();
        std::string dumpType = "bmc";
        boost::asio::io_context* ioCon = conn.getIoContext();

        std::string unixSocketPath = unixSocketPathDir + dumpType + "_dump_" +
                                     dumpId;

        handlers[&conn] = std::make_shared<Handler>(*ioCon, dumpId, dumpType,
                                                    unixSocketPath);
        handlers[&conn]->connection = conn.getSharedReference();
        handlers[&conn]->getDumpSize(dumpId, dumpType);
    }).onclose([](crow::streaming_response::Connection& conn) {
        auto handler = handlers.find(&conn);
        if (handler == handlers.end())
        {
            BMCWEB_LOG_DEBUG("No handler to cleanup");
            return;
        }
        handler->second->outputBuffer.clear();
        handlers.erase(handler);
    });

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/LogServices/Dump/Entries/<str>/attachment/")
        .privileges({{"ConfigureComponents", "ConfigureManager"}})
        .streamingResponse()
        .onopen([](crow::streaming_response::Connection& conn) {
        std::string url(conn.req.target());
        std::filesystem::path dumpIdPath(
            url.substr(0, url.find("/attachment")));
        std::string dumpId = dumpIdPath.filename();
        std::string dumpType = "system";
        boost::asio::io_context* ioCon = conn.getIoContext();

        std::string unixSocketPath = unixSocketPathDir + dumpType + "_dump_" +
                                     dumpId;

        handlers[&conn] = std::make_shared<Handler>(*ioCon, dumpId, dumpType,
                                                    unixSocketPath);
        handlers[&conn]->connection = conn.getSharedReference();
        handlers[&conn]->getDumpSize(dumpId, dumpType);
    }).onclose([](crow::streaming_response::Connection& conn) {
        auto handler = handlers.find(&conn);
        if (handler == handlers.end())
        {
            BMCWEB_LOG_DEBUG("No handler to cleanup");
            return;
        }
        handlers.erase(handler);
        handler->second->outputBuffer.clear();
    });
#ifdef BMCWEB_ENABLE_REDFISH_SYSTEM_FAULTLOG_DUMP_LOG
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/LogServices/FaultLog/Entries/<str>/attachment/")
        .privileges({{"ConfigureComponents", "ConfigureManager"}})
        .streamingResponse()
        .onopen([](crow::streaming_response::Connection& conn) {
        std::string url(conn.req.target());
        std::filesystem::path dumpIdPath(
            url.substr(0, url.find("/attachment")));
        std::string dumpId = dumpIdPath.filename();
        std::string dumpType = "faultlog";
        boost::asio::io_context* ioCon = conn.getIoContext();

        std::string unixSocketPath = unixSocketPathDir + dumpType + "_dump_" +
                                     dumpId;

        handlers[&conn] = std::make_shared<Handler>(*ioCon, dumpId, dumpType,
                                                    unixSocketPath);
        handlers[&conn]->connection = conn.getSharedReference();
        handlers[&conn]->getDumpSize(dumpId, dumpType);
    }).onclose([](crow::streaming_response::Connection& conn) {
        auto handler = handlers.find(&conn);
        if (handler == handlers.end())
        {
            BMCWEB_LOG_DEBUG("No handler to cleanup");
            return;
        }
        handlers.erase(handler);
        handler->second->outputBuffer.clear();
    });
#endif // BMCWEB_ENABLE_REDFISH_SYSTEM_FAULTLOG_DUMP_LOG

    if constexpr (BMCWEB_REDFISH_AGGREGATION)
    {
        BMCWEB_ROUTE(app,
                     "/redfish/v1/Managers/" +
                         std::string(redfishAggregationPrefix) +
                         "<str>/LogServices/Dump/Entries/<str>/attachment/")
            .privileges({{"ConfigureComponents", "ConfigureManager"}})
            .methods(boost::beast::http::verb::get)(std::bind_front(
                redfish::handleSetUpRedfishRoute, std::ref(app), "BMC"));

        BMCWEB_ROUTE(app,
                     "/redfish/v1/Systems/" +
                         std::string(redfishAggregationPrefix) +
                         "<str>/LogServices/Dump/Entries/<str>/attachment/")
            .privileges({{"ConfigureComponents", "ConfigureManager"}})
            .methods(boost::beast::http::verb::get)(std::bind_front(
                redfish::handleSetUpRedfishRoute, std::ref(app), "System"));

#ifdef BMCWEB_ENABLE_REDFISH_SYSTEM_FAULTLOG_DUMP_LOG
        BMCWEB_ROUTE(app,
                     "/redfish/v1/Systems/" +
                         std::string(redfishAggregationPrefix) +
                         "<str>/LogServices/FaultLog/Entries/<str>/attachment/")
            .privileges({{"ConfigureComponents", "ConfigureManager"}})
            .methods(boost::beast::http::verb::get)(std::bind_front(
                redfish::handleSetUpRedfishRoute, std::ref(app), "FaultLog"));
#endif
    }
}

} // namespace obmc_dump
} // namespace crow
