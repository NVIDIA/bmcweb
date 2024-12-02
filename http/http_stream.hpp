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
        boost::beast::http::async_write(
            adaptor, *streamres.bufferResponse,
            [this, self(shared_from_this())](
                const boost::system::error_code& ec2, std::size_t) {
                if (ec2)
                {
                    BMCWEB_LOG_DEBUG("Error while writing on socket{}", ec2);
                    close();
                    return;
                }
            });
    }

    void sendStreamHeaders(const std::string& streamDataSize,
                           const std::string& contentType) override
    {
        streamres.addHeader("Content-Length", streamDataSize);
        streamres.addHeader("Content-Type", contentType);
        boost::beast::http::async_write(
            adaptor, *streamres.bufferResponse,
            [this, self(shared_from_this())](
                const boost::system::error_code& ec2, std::size_t) {
                if (ec2)
                {
                    BMCWEB_LOG_DEBUG("Error while writing on socket{}", ec2);
                    close();
                    return;
                }
            });
    }
    void sendMessage(const boost::asio::mutable_buffer& buffer,
                     std::function<void(bool)> handler) override
    {
        if (buffer.size())
        {
            this->handlerFunc = handler;
            auto bytes = boost::asio::buffer_copy(
                streamres.bufferResponse->body().prepare(buffer.size()),
                buffer);
            streamres.bufferResponse->body().commit(bytes);
            doWrite();
        }
    }

    void close() override
    {
        streamres.end();
        boost::beast::get_lowest_layer(adaptor).close();
        closeHandler(*this);
    }

    void doWrite()
    {
        boost::asio::async_write(
            adaptor, streamres.bufferResponse->body().data(),
            [this, self(shared_from_this())](boost::beast::error_code ec,
                                             std::size_t bytesWritten) {
                streamres.bufferResponse->body().consume(bytesWritten);

                if (ec)
                {
                    BMCWEB_LOG_DEBUG("Error in async_write {}", ec);
                    (handlerFunc)(true);
                    close();
                    return;
                }
                (handlerFunc)(false);
            });
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
