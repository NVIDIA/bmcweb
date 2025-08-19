// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
// SPDX-FileCopyrightText: Copyright 2020 Intel Corporation
#pragma once

#include "event_logs_object_type.hpp"
#include "event_service_store.hpp"
#include "filter_expr_parser_ast.hpp"
#include "http_client.hpp"
#include "http_response.hpp"
#include "metric_report.hpp"
#include "server_sent_event.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/url/url_view_base.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace redfish
{

static constexpr const char* subscriptionTypeSSE = "SSE";

static constexpr const uint8_t maxNoOfSubscriptions = 20;
static constexpr const uint8_t maxNoOfSSESubscriptions = 10;
struct TestEvent
{
    std::optional<int64_t> eventGroupId;
    std::optional<std::string> eventId;
    std::optional<std::string> eventTimestamp;
    std::optional<std::string> message;
    std::optional<std::vector<std::string>> messageArgs;
    std::optional<std::string> messageId;
    std::optional<std::string> originOfCondition;
    std::optional<std::string> resolution;
    std::optional<std::string> severity;
    // default constructor
    TestEvent() = default;
    // default assignment operator
    TestEvent& operator=(const TestEvent&) = default;
    // default copy constructor
    TestEvent(const TestEvent&) = default;
    // default move constructor
    TestEvent(TestEvent&&) = default;
    // default move assignment operator
    TestEvent& operator=(TestEvent&&) = default;
    // destructor
    ~TestEvent() = default;
    // constructor with all the aruments
    TestEvent(std::optional<int64_t> eventGroupIdIn,
              std::optional<std::string> eventIdIn,
              std::optional<std::string> eventTimestampIn,
              std::optional<std::string> messageIn,
              std::optional<std::vector<std::string>> messageArgsIn,
              std::optional<std::string> messageIdIn,
              std::optional<std::string> originOfConditionIn,
              std::optional<std::string> resolutionIn,
              std::optional<std::string> severityIn) :

        eventGroupId(eventGroupIdIn), eventId(std::move(eventIdIn)),
        eventTimestamp(std::move(eventTimestampIn)),
        message(std::move(messageIn)), messageArgs(std::move(messageArgsIn)),
        messageId(std::move(messageIdIn)),
        originOfCondition(std::move(originOfConditionIn)),
        resolution(std::move(resolutionIn)), severity(std::move(severityIn))
    {}
};

class Subscription : public std::enable_shared_from_this<Subscription>
{
  public:
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&&) = delete;
    Subscription& operator=(Subscription&&) = delete;

    Subscription(std::shared_ptr<persistent_data::UserSubscription> userSubIn,
                 const boost::urls::url_view_base& url,
                 boost::asio::io_context& ioc);

    explicit Subscription(crow::sse_socket::Connection& connIn);

    ~Subscription() = default;

    // callback for subscription sendData
    void resHandler(const std::shared_ptr<Subscription>& /*self*/,
                    const crow::Response& res);

    void sendHeartbeatEvent();
    void scheduleNextHeartbeatEvent();
    void heartbeatParametersChanged();
    void onHbTimeout(const std::weak_ptr<Subscription>& weakSelf,
                     const boost::system::error_code& ec);

    bool sendEventToSubscriber(uint64_t eventId, std::string&& msg);

    void filterAndSendEventLogs(
        uint64_t eventId, const std::vector<EventLogObjectsType>& eventRecords);

    void filterAndSendReports(uint64_t eventId, const std::string& reportId,
                              const telemetry::TimestampReadings& var);

    void updateRetryConfig(uint32_t retryAttempts,
                           uint32_t retryTimeoutInterval);

    bool matchSseId(const crow::sse_socket::Connection& thisConn);

    // Check used to indicate what response codes are valid as part of our retry
    // policy.  2XX is considered acceptable
    static boost::system::error_code retryRespHandler(unsigned int respCode);

    std::shared_ptr<persistent_data::UserSubscription> userSub;
    std::function<void()> deleter;

  private:
    boost::urls::url host;
    std::shared_ptr<crow::ConnectionPolicy> policy;
    crow::sse_socket::Connection* sseConn = nullptr;

    boost::asio::steady_timer hbTimer;
    std::optional<crow::HttpClient> client;

  public:
    std::optional<filter_ast::LogicalAnd> filter;
};

} // namespace redfish
