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
#ifdef BMCWEB_REDFISH_AGGREGATION
class subscribeSatBmc
{
  public:
    subscribeSatBmc(const subscribeSatBmc&) = delete;
    subscribeSatBmc& operator=(const subscribeSatBmc&) = delete;
    subscribeSatBmc(subscribeSatBmc&&) = delete;
    subscribeSatBmc& operator=(subscribeSatBmc&&) = delete;
    ~subscribeSatBmc() = default;
    static subscribeSatBmc& getInstance()
    {
        static subscribeSatBmc handler;
        return handler;
    }

    void createSubscribeTimer()
    {
        subscribeTimer = std::make_shared<boost::asio::steady_timer>(
            crow::connections::systemBus->get_io_context(),
            std::chrono::seconds(rfaDeferSubscribeTime));
    }

    std::shared_ptr<boost::asio::steady_timer> getTimer()
    {
        return subscribeTimer;
    }

  private:
    std::shared_ptr<boost::asio::steady_timer> subscribeTimer;
    subscribeSatBmc() = default;
};

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
inline void handleSubscribeResponse(crow::Response& resp)
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
        nlohmann::json jsonVal = nlohmann::json::parse(*resp.body(), nullptr,
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

inline void doSubscribe(std::shared_ptr<crow::HttpClient> client,
                        boost::urls::url url, crow::Response& resp)
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
                std::string dest(BMCWEB_RFA_BMC_HOST_URL);

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

inline void doUnsubscribe(std::shared_ptr<crow::HttpClient> client,
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
    },
        serviceName, objectPath, interfaceName, startService,
        listenerServiceName, mode);
}

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
    auto subscribeTimer = subscribeSatBmc::getInstance().getTimer();
    // check HMC subscription periodically in case of HMC
    // reset-to-default
    subscribeTimer->expires_after(
        std::chrono::seconds(BMCWEB_RFA_DELAY_SUBSCRIBE_TIME));
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

    const auto& sat =
        satelliteInfo.find(std::string(BMCWEB_REDFISH_AGGREGATION_PREFIX));

    if (sat == satelliteInfo.end())
    {
        BMCWEB_LOG_ERROR("satellite BMC is not there.");
        return;
    }

    invokeRedfishEventListener();

    auto client = std::make_shared<crow::HttpClient>(
        ioc, std::make_shared<crow::ConnectionPolicy>(getSubscriptionPolicy()));

    boost::urls::url url(sat->second);

    auto subscribeTimer = subscribeSatBmc::getInstance().getTimer();
    subscribeTimer->expires_after(std::chrono::seconds(deferTime));
    subscribeTimer->async_wait(
        std::bind_front(querySubscriptionList, client, url));
}

inline int initRedfishEventListener(boost::asio::io_context& ioc)
{
    const uint8_t deferTime = BMCWEB_RFA_DELAY_SUBSCRIBE_TIME;
    RedfishAggregator::getSatelliteConfigs(
        std::bind_front(getSatBMCInfo, std::ref(ioc), deferTime));

    return 0;
}

inline int startRedfishEventListener(__attribute__((unused))
                                     boost::asio::io_context& ioc)
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

    const auto& sat =
        satelliteInfo.find(std::string(BMCWEB_REDFISH_AGGREGATION_PREFIX));

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
    auto subscribeTimer = subscribeSatBmc::getInstance().getTimer();
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
    },
        serviceName, objectPath, interfaceName, stopService,
        listenerServiceName, mode);
    return 0;
}

#endif
} // namespace redfish
