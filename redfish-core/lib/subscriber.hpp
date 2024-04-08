
// Dbus service for EventService Listener
constexpr const char* mode = "replace";
constexpr const char* serviceName = "org.freedesktop.systemd1";
constexpr const char* objectPath = "/org/freedesktop/systemd1";
constexpr const char* interfaceName = "org.freedesktop.systemd1.Manager";
constexpr const char* startService = "StartUnit";
constexpr const char* stopService = "StopUnit";
constexpr const char* listenerServiceName = "redfishevent-listener.service";
// the maximum response body size
constexpr unsigned int subscribeBodyLimit = 5 * 1024 * 1024; // 5MB

namespace redfish
{
#ifdef BMCWEB_ENABLE_REDFISH_AGGREGATION
// this is common function for http client retry
inline boost::system::error_code subscriptionRetryHandler(unsigned int respCode)
{
    BMCWEB_LOG_DEBUG(
        "Received {} response of the firmware update from satellite", respCode);
    return boost::system::errc::make_error_code(boost::system::errc::success);
}

// This is the policy of http client
inline crow::ConnectionPolicy getSubscriptionPolicy()
{
    return {.maxRetryAttempts = 1,
            .requestByteLimit = subscribeBodyLimit,
            .maxConnections = 20,
            .retryPolicyAction = "TerminateAfterRetries",
            .retryIntervalSecs = std::chrono::seconds(0),
            .invalidResp = subscriptionRetryHandler};
}

// This is common function for post/delete subscription.
void handleSubscribeResponse(crow::Response& resp)
{
    if (resp.resultInt() ==
        static_cast<unsigned>(boost::beast::http::status::created))
    {
        BMCWEB_LOG_DEBUG("The subscription is created");
        return;
    }
    if (resp.resultInt() ==
        static_cast<unsigned>(boost::beast::http::status::ok))
    {
        BMCWEB_LOG_DEBUG("The request is performed successfully.");
        return;
    }
    BMCWEB_LOG_ERROR("Response error code: {}", resp.resultInt());
}

// This is the response handler of get redfish subscription
template <typename Callback>
void handleGetSubscriptionResp(crow::Response& resp, Callback&& handler)
{
    if (resp.resultInt() !=
        static_cast<unsigned>(boost::beast::http::status::ok))
    {
        BMCWEB_LOG_ERROR(" GetSubscriptionResp err code: {}", resp.resultInt());
        return;
    }

    std::string_view contentType = resp.getHeaderValue("Content-Type");
    if (boost::iequals(contentType, "application/json") ||
        boost::iequals(contentType, "application/json; charset=utf-8"))
    {
        nlohmann::json jsonVal = nlohmann::json::parse(resp.body(), nullptr,
                                                       false);
        if (jsonVal.is_discarded())
        {
            BMCWEB_LOG_ERROR("Error parsing satellite response as JSON");
            return;
        }
        // handle JSON objects
        handler(jsonVal);
    }
}

void doSubscribe(std::shared_ptr<crow::HttpClient> client, boost::urls::url url,
                 crow::Response& resp)
{
    // subscribe EventService if there is no subscription in satellite BMC
    auto subscribe = [&client, &url](nlohmann::json& jsonVal) {
        if (jsonVal.contains("Members@odata.count"))
        {
            if (jsonVal["Members@odata.count"] == 0)
            {
                BMCWEB_LOG_DEBUG("No subscription. Subscribe directly!");

                boost::beast::http::fields httpHeader;
                std::function<void(crow::Response&)> cb =
                    std::bind_front(handleSubscribeResponse);

                std::string path("/redfish/v1/EventService/Subscriptions");
                std::string dest(rfaBmcHostURL);

                nlohmann::json postJson = {{"Destination", dest},
                                           {"Protocol", "Redfish"}};

                auto data = postJson.dump();
                url.set_path(path);
                client->sendDataWithCallback(std::move(data), url, httpHeader,
                                             boost::beast::http::verb::post,
                                             cb);
            }
        }
    };
    handleGetSubscriptionResp(resp, std::move(subscribe));
}

void doUnsubscribe(std::shared_ptr<crow::HttpClient> client,
                   boost::urls::url url, crow::Response& resp)
{
    // unsubscribe EventService if there is subscriptions in satellite BMC
    auto unSubscribe = [&client, &url](nlohmann::json& jsonVal) {
        if (jsonVal.contains("Members"))
        {
            auto& satMembers = jsonVal["Members"];
            for (auto& satMem : satMembers)
            {
                BMCWEB_LOG_DEBUG("unSubscribe: {}", satMem["@odata.id"]);
                std::function<void(crow::Response&)> cb =
                    std::bind_front(handleSubscribeResponse);

                std::string data;
                boost::beast::http::fields httpHeader;
                url.set_path(satMem["@odata.id"]);
                client->sendDataWithCallback(std::move(data), url, httpHeader,
                                             boost::beast::http::verb::delete_,
                                             cb);
            }
        }
    };
    handleGetSubscriptionResp(resp, std::move(unSubscribe));
}

inline void invokeRedfishEventListener()
{
    crow::connections::systemBus->async_method_call(
        [](const boost::system::error_code& ec) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error: {}", ec);
            return;
        }
    }, serviceName, objectPath, interfaceName, startService,
        listenerServiceName, mode);
}

std::unique_ptr<boost::asio::steady_timer> subscribeTimer;
inline void querySubscriptionList(std::shared_ptr<crow::HttpClient> client,
                                  boost::urls::url url,
                                  const boost::system::error_code& ec)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("timer code:{}", ec);
        return;
    }
    std::string data;
    boost::beast::http::fields httpHeader;

    std::function<void(crow::Response&)> cb = std::bind_front(doSubscribe,
                                                              client, url);

    std::string path("/redfish/v1/EventService/Subscriptions");
    url.set_path(path);
    client->sendDataWithCallback(std::move(data), url, httpHeader,
                                 boost::beast::http::verb::get, cb);
    // check HMC subscription periodically in case of HMC
    // reset-to-default
    subscribeTimer->expires_after(std::chrono::seconds(rfaDeferSubscribeTime));
    subscribeTimer->async_wait(
        std::bind_front(querySubscriptionList, client, url));
}

inline void getSatBMCInfo(
    boost::asio::io_context& ioc, const uint8_t deferTime,
    const boost::system::error_code& ec,
    const std::unordered_map<std::string, boost::urls::url>& satelliteInfo)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Dbus query error for satellite BMC.");
        return;
    }

    const auto& sat = satelliteInfo.find(redfishAggregationPrefix);
    if (sat == satelliteInfo.end())
    {
        BMCWEB_LOG_ERROR("satellite BMC is not there.");
        return;
    }

    invokeRedfishEventListener();

    auto client = std::make_shared<crow::HttpClient>(
        ioc, std::make_shared<crow::ConnectionPolicy>(getSubscriptionPolicy()));

    boost::urls::url url(sat->second);

    subscribeTimer->expires_after(std::chrono::seconds(deferTime));
    subscribeTimer->async_wait(
        std::bind_front(querySubscriptionList, client, url));
}

inline int initRedfishEventListener(boost::asio::io_context& ioc)
{
    const uint8_t deferTime = rfaDeferSubscribeTime;
    RedfishAggregator::getSatelliteConfigs(
        std::bind_front(getSatBMCInfo, std::ref(ioc), deferTime));

    return 0;
}

inline int startRedfishEventListener(
    __attribute__((unused)) boost::asio::io_context& ioc)
{
    const uint8_t immediateTime = 1;

    RedfishAggregator::getSatelliteConfigs(
        std::bind_front(getSatBMCInfo, std::ref(ioc), immediateTime));

    return 0;
}

inline void unSubscribe(
    boost::asio::io_context& ioc, const boost::system::error_code& ec,
    const std::unordered_map<std::string, boost::urls::url>& satelliteInfo)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Dbus query error for satellite BMC.");
        return;
    }

    const auto& sat = satelliteInfo.find(redfishAggregationPrefix);
    if (sat == satelliteInfo.end())
    {
        BMCWEB_LOG_ERROR("satellite BMC is not there.");
        return;
    }
    auto client = std::make_shared<crow::HttpClient>(
        ioc, std::make_shared<crow::ConnectionPolicy>(getSubscriptionPolicy()));

    boost::urls::url url(sat->second);
    std::string path("/redfish/v1/EventService/Subscriptions");
    url.set_path(path);

    std::string data;
    boost::beast::http::fields httpHeader;

    std::function<void(crow::Response&)> cb = std::bind_front(doUnsubscribe,
                                                              client, url);
    client->sendDataWithCallback(std::move(data), url, httpHeader,
                                 boost::beast::http::verb::get, cb);
}

inline int stopRedfishEventListener(boost::asio::io_context& ioc)
{
    // stop the timer.
    subscribeTimer->cancel();

    RedfishAggregator::getSatelliteConfigs(
        std::bind_front(unSubscribe, std::ref(ioc)));

    // stop redfish event listener
    crow::connections::systemBus->async_method_call(
        [](const boost::system::error_code& ec) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
            return;
        }
    }, serviceName, objectPath, interfaceName, stopService, listenerServiceName,
        mode);
    return 0;
}
#endif
} // namespace redfish
