#pragma once
#include "http/http_request.hpp"
#include "http/http_response.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

namespace crow
{

namespace streaming_response
{

struct Connection : std::enable_shared_from_this<Connection>
{
  public:
    explicit Connection(const crow::Request& reqIn) : req(reqIn.req) {}
    virtual void sendMessage(const boost::asio::mutable_buffer& buffer,
                             std::function<void(bool)> handler) = 0;
    virtual void close() = 0;
    virtual boost::asio::io_context* getIoContext() = 0;
    virtual void sendStreamHeaders(const std::string& streamDataSize,
                                   const std::string& contentType) = 0;
    virtual void sendStreamErrorStatus(boost::beast::http::status status) = 0;

    Connection(const Connection&) = delete;
    Connection(const Connection&&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection& operator=(const Connection&&) = delete;
    virtual ~Connection() = default;
    std::shared_ptr<crow::streaming_response::Connection> getSharedReference()
    {
        return shared_from_this();
    }
    boost::beast::http::request<boost::beast::http::string_body> req;
    crow::DynamicResponse streamres;
};

template <typename Adaptor>
class ConnectionImpl : public Connection
{
  public:
    ConnectionImpl(const crow::Request& reqIn, Adaptor&& adaptorIn,
                   std::function<void(Connection&)> openHandler,
                   std::function<void(Connection&, const std::string&, bool)>
                       messageHandler,
                   std::function<void(Connection&)> closeHandler,
                   std::function<void(Connection&)> errorHandler) :

        Connection(reqIn), adaptor(std::move(adaptorIn)),
        waitTimer(*reqIn.ioService), openHandler(std::move(openHandler)),
        messageHandler(std::move(messageHandler)),
        closeHandler(std::move(closeHandler)),
        errorHandler(std::move(errorHandler)), reqConnImpl(reqIn)
    {}

    boost::asio::io_context* getIoContext() override
    {
        return reqConnImpl.ioService;
    }

    void start()
    {
        streamres.completeRequestHandler = [this] {
            BMCWEB_LOG_DEBUG("running completeRequestHandler");
            this->close();
        };
        openHandler(*this);
    }

    void sendStreamErrorStatus(boost::beast::http::status status) override
    {
        streamres.result(status);
        if (streamres.bufferResponse)
        {
            boost::beast::http::async_write(
                adaptor, *streamres.bufferResponse,
                [this, self(shared_from_this())](
                    const boost::system::error_code& ec2, std::size_t) {
                    if (ec2)
                    {
                        BMCWEB_LOG_DEBUG("Error while writing on socket{}",
                                         ec2);
                        close();
                        return;
                    }
                });
        }
    }

    void sendStreamHeaders(const std::string& streamDataSize,
                           const std::string& contentType) override
    {
        streamres.addHeader("Content-Length", streamDataSize);
        streamres.addHeader("Content-Type", contentType);
        if (streamres.bufferResponse)
        {
            boost::beast::http::async_write(
                adaptor, *streamres.bufferResponse,
                [this, self(shared_from_this())](
                    const boost::system::error_code& ec2, std::size_t) {
                    if (ec2)
                    {
                        BMCWEB_LOG_DEBUG("Error while writing on socket{}",
                                         ec2);
                        close();
                        return;
                    }
                });
        }
    }
    void sendMessage(const boost::asio::mutable_buffer& buffer,
                     std::function<void(bool)> handler) override
    {
        std::size_t size = buffer.size();
        if (size > 0)
        {
            this->handlerFunc = handler;
            if (streamres.bufferResponse)
            {
                auto bytes = boost::asio::buffer_copy(
                    streamres.bufferResponse->body().prepare(size), buffer);
                streamres.bufferResponse->body().commit(bytes);
            }
            doWrite();
        }
    }

    void close() override
    {
        streamres.end();
        boost::beast::get_lowest_layer(adaptor).close();
        if (closeHandler != nullptr)
        {
            closeHandler(*this);
        }
    }

    void doWrite()
    {
        if (streamres.bufferResponse)
        {
            boost::asio::async_write(
                adaptor, streamres.bufferResponse->body().data(),
                [this, self(shared_from_this())](boost::beast::error_code ec,
                                                 std::size_t bytesWritten) {
                    if (streamres.bufferResponse)
                    {
                        streamres.bufferResponse->body().consume(bytesWritten);
                    }

                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG("Error in async_write {}", ec);
                        if (this->handlerFunc != nullptr)
                        {
                            (handlerFunc)(true);
                        }
                        close();
                        return;
                    }
                    if (this->handlerFunc != nullptr)
                    {
                        (handlerFunc)(false);
                    }
                });
        }
    }

  private:
    Adaptor adaptor;
    boost::asio::steady_timer waitTimer;
    bool doingWrite = false;
    std::function<void(Connection&)> openHandler;
    std::function<void(Connection&, const std::string&, bool)> messageHandler;
    std::function<void(Connection&)> closeHandler;
    std::function<void(Connection&)> errorHandler;
    std::function<void(bool)> handlerFunc;
    crow::Request reqConnImpl;
};

} // namespace streaming_response
} // namespace crow
