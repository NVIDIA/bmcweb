#include <boost/asio/io_context.hpp>
#include <boost/beast/core.hpp>           // For string_view, error_code
#include <boost/beast/http.hpp>           // For http::field, status, verb, error
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/beast/http/error.hpp>
#include <boost/beast/version.hpp>
#include <boost/utility/string_view.hpp>
// NOLINTNEXTLINE(clang-diagnostic-error)
#include "elog_entry.hpp"

#include "nlohmann/json.hpp"
// NOLINTNEXTLINE(misc-include-cleaner)
#include "phosphor-logging/lg2.hpp"
#include "sdbusplus/asio/connection.hpp"
#include "sdbusplus/bus.hpp"
#include "sdbusplus/server/manager.hpp"

#include <algorithm>
#include <cstdint>   // For uint32_t and int64_t
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <chrono>


namespace beast = boost::beast;   // from <boost/beast.hpp>
namespace http = beast::http;     // from <boost/beast/http.hpp>
namespace net = boost::asio;      // from <boost/asio.hpp>

using tcp = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>
using Level = sdbusplus::xyz::openbmc_project::Logging::server::Entry::Level;

const std::string ifaceName{"xyz.openbmc_project.Logging.Entry"};
const std::string entryName{
    "/xyz/openbmc_project/logging/satbmc_listener/entry"};

constexpr uint8_t maxEventQueueLen = 4;
constexpr uint8_t maxSessionNum = 4;

class RedfishEventMgr
{
  public:
    static uint8_t sessionNum;
    static void incSessNum()
    {
        sessionNum++;
    }

    static void decSessNum()
    {
        sessionNum--;
    }

    static uint8_t getSessNum()
    {
        return sessionNum;
    }

    static void createLogEntry(
        std::shared_ptr<sdbusplus::asio::connection>& conn,
        const std::string& entryName, const nlohmann::json& data)
    {
        static uint8_t evtIndex = 0;
        static std::map<std::string,
                        std::shared_ptr<phosphor::logging::evtEntry>>
            eventMap;
        uint32_t id = 0;
        int64_t timestamp = 0;
        Level severity = Level::Informational;
        std::string msg;
        std::vector<std::string> additionalData;
        std::string aData;
        std::string resolution;
        std::string path = entryName + "/" + std::to_string(evtIndex);

        // increase the index of the event list
        evtIndex++;
        if (evtIndex >= maxEventQueueLen)
        {
            evtIndex = 0;
        }

        if (!data.contains("Events"))
        {
            lg2::error("no Eventsn in Redfish message");
            return;
        }
        // parse redfish event
        for (auto& evt : data["Events"])
        {
            if (evt.contains("MessageId"))
            {
                msg = evt["MessageId"];
                additionalData.push_back("REDFISH_MESSAGE_ID=" + msg);
            }
            std::string ooc;
            if (evt.contains("OriginOfCondition"))
            {
                ooc = evt["OriginOfCondition"]["@odata.id"];
                aData = "REDFISH_ORIGIN_OF_CONDITION=" + ooc;
                additionalData.push_back(aData);
            }
            if (evt.contains("MessageArgs"))
            {
                std::string argStr = "REDFISH_MESSAGE_ARGS=";
                uint16_t counter = 0;
                for (auto arg : evt["MessageArgs"])
                {
                    if (counter != 0)
                    {
                        argStr += ",";
                    }
                    argStr += arg;
                    counter++;
                }
                additionalData.push_back(argStr);
            }

            if (evt.contains("LogEntry"))
            {
                std::string logStr = "REDFISH_LOGENTRY=";
                logStr += evt["LogEntry"]["@odata.id"];
                additionalData.push_back(logStr);
            }

            if (evt.contains("MessageSeverity"))
            {
                std::string level = evt["MessageSeverity"];
                if (level == "Warning")
                {
                    severity = Level::Warning;
                }
                else if (level == "Critical")
                {
                    severity = Level::Critical;
                }
            }
        }
        timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();

        if (eventMap.find(path) != eventMap.end())
        {
            eventMap.erase(path);
        }
        // create entry in redfish logging service
        eventMap.emplace(path,
                         std::make_shared<phosphor::logging::evtEntry>(
                             static_cast<sdbusplus::bus::bus&>(*conn), path, id,
                             timestamp, severity, std::move(msg),
                             std::move(resolution), std::move(additionalData)));
    }
};

uint8_t RedfishEventMgr::sessionNum = 0;

// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template <class Body, class Allocator, class Send>
void handleRequest(std::shared_ptr<sdbusplus::asio::connection>& bus,
                    http::request<Body, http::basic_fields<Allocator>>&& req,
                    Send&& send)
{
    // Returns a bad request response
    const auto badRequest = [req = std::move(req)](beast::string_view why) {
        http::response<http::string_body> res{http::status::bad_request,
                                              req.version()};
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = std::string(why);
        res.prepare_payload();
        return res;
    };

    // Make sure we can handle the method
    if (req.method() != http::verb::post)
        return send(badRequest("Unknown HTTP-method"));
    // Request path must be absolute and not contain "..".
    if (req.target().empty() || req.target()[0] != '/' ||
        req.target().find("..") != beast::string_view::npos)
        return send(badRequest("Illegal request-target"));

    std::string body = req.body();

    nlohmann::json data = nlohmann::json::parse(body, nullptr, false);
    if (data.is_discarded())
    {
        lg2::error("Json parse error: {BODY}", "BODY", body);
        return send(badRequest("bad Json format"));
    }
    RedfishEventMgr::createLogEntry(bus, entryName, data);

    http::response<http::string_body> res{http::status::ok, req.version()};
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "text/html");
    res.keep_alive(req.keep_alive());

    return send(std::move(res));
}

//------------------------------------------------------------------------------
// Report a failure
void fail(beast::error_code ec, const char* what, bool excp = true)
{
    lg2::error("{WHAT}: {MSG}", "WHAT", what, "MSG", ec.message());
    if (excp)
    {
        throw std::runtime_error("Exit...");
    }
}

// Handles an HTTP server connection
class Session : public std::enable_shared_from_this<Session>
{
    // This is the C++11 equivalent of a generic lambda.
    // The function object is used to send an HTTP message.
    struct SendLambda
    {
        Session& self_;

        explicit SendLambda(Session& self) : self_(self)
        {}

        template <bool isRequest, class Body, class Fields>
        void operator()(http::message<isRequest, Body, Fields>&& msg) const
        {
            // The lifetime of the message has to extend
            // for the duration of the async operation so
            // we use a shared_ptr to manage it.
            auto sp = std::make_shared<http::message<isRequest, Body, Fields>>(
                std::move(msg));

            // Store a type-erased version of the shared
            // pointer in the class to keep it alive.
            self_.res_ = sp;

            // Write the response
            boost::beast::http::async_write(
                self_.stream_, *sp,
                beast::bind_front_handler(&Session::onWrite,
                                          self_.shared_from_this(),
                                          sp->need_eof()));
        }
    };

    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    std::shared_ptr<sdbusplus::asio::connection> bus_;
    http::request<http::string_body> req_{};
    std::shared_ptr<void> res_;
    SendLambda lambda_;

  public:
    // Take ownership of the stream
    Session(tcp::socket&& socket,
            std::shared_ptr<sdbusplus::asio::connection> bus) :
        stream_(std::move(socket)),
        buffer_,
        bus_(std::move(bus)),
        req_(),
        res_,
        lambda_(*this)
    {
        RedfishEventMgr::incSessNum();
    }

    ~Session()
    {
        RedfishEventMgr::decSessNum();
    }

    // Start the asynchronous operation
    void run()
    {
        // We need to be executing within a strand to perform async
        // operations on the I/O objects in this Session. Although not
        // strictly necessary for single-threaded contexts, this example
        // code is written to be thread-safe by default.
        net::dispatch(
            stream_.get_executor(),
            beast::bind_front_handler(&Session::doRead, shared_from_this()));
    }

    void doRead()
    {
        // Make the request empty before reading,
        // otherwise the operation behavior is undefined.
        req_ = {};

        // Set the timeout.
        stream_.expires_after(std::chrono::seconds(30));

        // Read a request
        http::async_read(
            stream_, buffer_, req_,
            beast::bind_front_handler(&Session::onRead, shared_from_this()));
    }

    static void onRead(beast::error_code ec, std::size_t bytesTransferred)
    {
        boost::ignore_unused(bytesTransferred);

        // This means they closed the connection
        if (ec == http::error::end_of_stream)
        {
            doClose();
            return;
        }

        if (ec)
        {
            fail(ec, "read", false);
            return;
        }

        // Send the response
        handleRequest(bus_, std::move(req_), lambda_);
    }

    void onWrite(bool close, beast::error_code ec,
                  std::size_t bytesTransferred)
    {
        boost::ignore_unused(bytesTransferred);

        if (ec)
        {
            fail(ec, "write", false);
            return;
        }

        if (close)
        {
            // This means we should close the connection, usually because
            // the response indicated the "Connection: close" semantic.
            doClose();
            return;
        }

        // We're done with the response so delete it
        res_ = nullptr;

        // Read another request
        doRead();
    }

    static void doClose()
    {
        // Send a TCP shutdown
        beast::error_code ec;
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

        // At this point the connection is closed gracefully
    }
};

// Accepts incoming connections and launches the sessions
class Listener : public std::enable_shared_from_this<Listener>
{
    net::io_context& ioc_;
    std::shared_ptr<sdbusplus::asio::connection> conn_;
    tcp::acceptor acceptor_;
    tcp::endpoint& endpoint_;

  public:
    Listener(net::io_context& ioc,
             std::shared_ptr<sdbusplus::asio::connection>&& conn,
             tcp::endpoint& endpoint) :
        ioc_(ioc), conn_(conn), acceptor_(ioc), endpoint_(endpoint)
    {}

    static void init()
    {
        beast::error_code ec;

        // Open the acceptor
        acceptor_.open(endpoint_.protocol(), ec);
        if (ec)
        {
            fail(ec, "open");
            return;
        }

        // Allow address reuse
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if (ec)
        {
            acceptor_.close();
            fail(ec, "set_option");
            return;
        }

        // Bind to the server address
        acceptor_.bind(endpoint_, ec);
        if (ec)
        {
            acceptor_.close();
            fail(ec, "bind");
            return;
        }

        // Start listening for connections
        acceptor_.listen(net::socket_base::max_listen_connections, ec);
        if (ec)
        {
            acceptor_.close();
            fail(ec, "listen");
            return;
        }
    }

    // Start accepting incoming connections
    void run(std::string_view host)
    {
        doAccept();
        lg2::debug("Redfish event Listener is ready at {HOST}", "HOST", host);
    }

  private:
    static void doAccept()
    {
        lg2::debug("the current number of sessions: {NUM}", "NUM",
                   RedfishEventMgr::getSessNum());
        // The new connection gets its own strand
        acceptor_.async_accept(ioc_,
                               beast::bind_front_handler(&Listener::onAccept,
                                                         shared_from_this()));
    }

    void onAccept(beast::error_code ec, tcp::socket socket)
    {
        if (ec)
        {
            acceptor_.close();
            fail(ec, "accept");
            return; // To avoid infinite loop
        }
        // Create the Session and run it
        if (RedfishEventMgr::getSessNum() <= maxSessionNum)
        {
            std::make_shared<Session>(std::move(socket), conn_)->run();
        }
        else
        {
            // close the peer's connection since there is no free Session.
            beast::tcp_stream stream(std::move(socket));
            beast::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_send, ec);
            lg2::error("Reach maximum sessions!");
        }
        // Accept another connection
        doAccept();
    }
};

int main(int argc, char* argv[])
{
    try
    {
        if (argc != 3)
        {
            std::cerr << "Usage: listener <address> <port>\n";
            return 1;
        }

        // Use safer string to integer conversion
        char* end = nullptr;
        std::string portStr(argv[2]);
        const auto port = static_cast<unsigned short>(std::strtol(portStr.c_str(), &end, 10));
        if (*end != '\0')
        {
            std::cerr << "Invalid port number\n";
            return 1;
        }

        std::string host(argv[1]);
        host += ':';
        host += portStr;

        // The io_context is required for all I/O
        net::io_context ioc{1};
        auto conn = std::make_shared<sdbusplus::asio::connection>(ioc);

        // create redfish logging service
        sdbusplus::server::manager::manager objManager(
            static_cast<sdbusplus::bus::bus&>(*conn), entryName.c_str());
        conn->request_name("xyz.openbmc_project.logging.rfevtlistener");

        tcp::endpoint ep{net::ip::make_address(argv[1]), port};

        // Create and launch a listening port
        auto rfListener = std::make_shared<Listener>(ioc, std::move(conn), ep);
        rfListener->init();
        rfListener->run(host);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
    ioc.run();
    return 0;
}
