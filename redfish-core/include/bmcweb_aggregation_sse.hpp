/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "http_response.hpp"
#include "persistent_data.hpp"
#include "sse_connection.hpp"
#include "utils/json_utils.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/url.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace redfish
{

void addPrefixes(nlohmann::json& json, std::string_view prefix);

using SatelliteEventCallback =
    std::function<void(const std::string& eventJson, uint64_t eventId)>;

class SseEventAggregator
{
  public:
    // Set the maximum buffer size to 1 MB
    static constexpr size_t maxBufferSize = 1024UL * 1024UL;

    /** Base delay (seconds) for exponential retries in scheduleSatConfigLoad().
     * Backoff sequence: 5s, 10s, 20s, 40s, 80s, 150s (capped), 150s...
     */
    static constexpr int satConfigLoadBackoffBaseSec = 5;
    static constexpr std::chrono::minutes satConfigLoadTimeoutMins{3};

    explicit SseEventAggregator(boost::asio::io_context& iocIn) :
        ioc(iocIn), reconnectTimer(iocIn), persistTimer(iocIn),
        satConfigLoadDeadlineTimer(iocIn)
    {
        // Start periodic persist timer
        schedulePersistTimer();
    }

    virtual ~SseEventAggregator()
    {
        shuttingDown = true;
        satConfigLoadDeadlineTimer.cancel();
        reconnectTimer.cancel();
        persistTimer.cancel();
        stopSatConfigLoadTimer();
        // Save any pending changes on shutdown
        persistIfDirty();
    }

    SseEventAggregator(const SseEventAggregator&) = delete;
    SseEventAggregator& operator=(const SseEventAggregator&) = delete;
    SseEventAggregator(SseEventAggregator&&) = delete;
    SseEventAggregator& operator=(SseEventAggregator&&) = delete;

    void start(const std::unordered_map<std::string, boost::urls::url>& configs)
    {
        satConfigRefresher = std::move(refresh);
        satConfigLoadCancelled = false;
        satConfigLoadDeadlineTimer.expires_after(satConfigLoadTimeoutMins);
        satConfigLoadDeadlineTimer.async_wait([this](const boost::system::
                                                         error_code& ec) {
            if (ec || shuttingDown)
            {
                return;
            }
            satConfigLoadCancelled = true;
            BMCWEB_LOG_WARNING(
                "Satellite config load timed out; giving up after {} minutes",
                satConfigLoadTimeoutMins.count());
            stopSatConfigLoadTimer();
        });
        initSatConfigLoad(0);
    }

    void stop()
    {
        BMCWEB_LOG_INFO("SSE aggregator stopping...");
        reconnectTimer.cancel();

        if (sseConnection)
        {
            sseConnection.reset();
        }

        state = State::Idle;
    }

    void setSatelliteEventCallback(SatelliteEventCallback callback)
    {
        satelliteEventCallback_ = std::move(callback);
    }

    enum class State
    {
        Idle,
        Configuring,
        Connecting,
        Connected,
        Retrying
    };

    State state = State::Idle;
    boost::asio::io_context& ioc;
    boost::asio::steady_timer reconnectTimer;
    boost::asio::steady_timer persistTimer;
    boost::asio::steady_timer satConfigLoadDeadlineTimer;
    std::shared_ptr<boost::asio::steady_timer> satConfigLoadTimer;
    SatelliteConfigRefresher satConfigRefresher;
    bool shuttingDown{false};
    bool satConfigLoadCancelled{false};
    boost::urls::url url;
    std::string buffer;
    std::shared_ptr<crow::SSEConnection> sseConnection;
    std::string host;
    uint16_t port = 80;
    std::string aggregationPrefix{redfishAggregationPrefix};
    std::string lastEventId;
    bool lastEventIdDirty =
        false; // Track if lastEventId changed since last persist
    SatelliteEventCallback satelliteEventCallback_;
    bool skipLastEventIdOnReconnect = false;
    bool sentLastEventIdThisAttempt = false;

    // SSE retry configuration
    static constexpr std::chrono::seconds retryIntervalSecs{60};

    // Persist configuration - save every 5 minutes if changed
    static constexpr std::chrono::minutes persistIntervalMins{5};

    void setAggregationPrefix(const std::string& prefix)
    {
        aggregationPrefix = prefix;
    }

    std::string getAggregationPrefix() const
    {
        return aggregationPrefix;
    }

    // Set last event ID for this satellite (marks dirty for periodic persist)
    void setLastEventId(const std::string& eventId)
    {
        if (eventId.empty())
        {
            return;
        }

        if (lastEventId != eventId)
        {
            lastEventId = eventId;
            lastEventIdDirty = true; // Mark for periodic persist
            BMCWEB_LOG_DEBUG("Updated Last-Event-Id for satellite {}: {}", host,
                             eventId);
        }
    }

    // Persist lastEventId to storage if it has changed
    void persistIfDirty()
    {
        if (lastEventIdDirty && !lastEventId.empty() && !host.empty())
        {
            persistent_data::getConfig().sseAggregatorLastEventIds[host] =
                lastEventId;
            persistent_data::getConfig().writeData();
            lastEventIdDirty = false;
            BMCWEB_LOG_DEBUG("Persisted Last-Event-Id for {}: {}", host,
                             lastEventId);
        }
    }

    void handlePersistTimer(const boost::system::error_code& ec)
    {
        if (ec)
        {
            return;
        }
        persistIfDirty();
        schedulePersistTimer();
    }

    void schedulePersistTimer()
    {
        persistTimer.expires_after(persistIntervalMins);
        persistTimer.async_wait(
            std::bind_front(&SseEventAggregator::handlePersistTimer, this));
    }

    // Handle the data from the event stream
    void handleSseData(std::string_view data)
    {
        if (data.empty())
        {
            return;
        }

        // Check buffer size limit before appending
        if (buffer.size() + data.size() > maxBufferSize)
        {
            BMCWEB_LOG_ERROR(
                "Buffer size limit exceeded ({} bytes). Disconnecting from satellite BMC to prevent memory exhaustion.",
                maxBufferSize);
            buffer.clear();
            stop();
            scheduleReconnect();
            return;
        }

        // Append incoming data to buffer
        buffer.append(data);
        processBufferData();
    }

    void processBufferData()
    {
        BMCWEB_LOG_DEBUG("Processing Buffer Data");

        constexpr std::string_view delimiter = "\n\n";
        size_t processedUpTo = 0;

        // Process complete events in the buffer
        while (true)
        {
            size_t endPos = buffer.find(delimiter, processedUpTo);
            if (endPos == std::string::npos)
            {
                break; // No more complete events
            }

            if (endPos > processedUpTo)
            {
                std::string_view event = std::string_view(buffer).substr(
                    processedUpTo, endPos - processedUpTo);
                if (!event.empty())
                {
                    extractAndProcessEvent(event);
                }
            }

            processedUpTo = endPos + delimiter.size();
        }

        // Remove processed data from buffer
        if (processedUpTo > 0)
        {
            if (processedUpTo >= buffer.size())
            {
                buffer.clear();
            }
            else
            {
                buffer.erase(0, processedUpTo);
            }
        }
    }

    struct ParsedSseEvent
    {
        std::string eventId;
        std::string eventContent;
    };

    static ParsedSseEvent parseSseEvent(std::string_view eventRaw)
    {
        ParsedSseEvent result;
        constexpr std::string_view idPrefix = "id: ";
        constexpr std::string_view dataPrefix = "data: ";

        result.eventContent.reserve(eventRaw.size());

        size_t lineStart = 0;
        bool firstDataLine = true;

        while (lineStart < eventRaw.size())
        {
            size_t lineEnd = eventRaw.find('\n', lineStart);
            std::string_view line =
                eventRaw.substr(lineStart, lineEnd - lineStart);

            if (!line.empty() && line.back() == '\r')
            {
                line.remove_suffix(1);
            }

            if (line.starts_with(idPrefix))
            {
                result.eventId = line.substr(idPrefix.size());
            }
            else if (line.starts_with(dataPrefix))
            {
                if (!firstDataLine)
                {
                    result.eventContent += '\n';
                }
                result.eventContent.append(line.substr(dataPrefix.size()));
                firstDataLine = false;
            }

            if (lineEnd == std::string_view::npos)
            {
                break;
            }
            lineStart = lineEnd + 1;
        }

        return result;
    }

    void extractAndProcessEvent(std::string_view eventRaw)
    {
        ParsedSseEvent parsed = parseSseEvent(eventRaw);

        if (!parsed.eventId.empty())
        {
            setLastEventId(parsed.eventId);
        }

        if (parsed.eventContent.empty())
        {
            BMCWEB_LOG_ERROR("Received SSE event with no data content");
            stop();
            scheduleReconnect();
            return;
        }

        processAndForwardEvent(parsed.eventContent);
    }

    static void extractOrigin(const nlohmann::json& eventObj,
                              std::string& origin)
    {
        auto originIt = eventObj.find("OriginOfCondition");
        if (originIt == eventObj.end())
        {
            BMCWEB_LOG_WARNING(
                "Event has no valid OriginOfCondition field - forwarding with empty origin");
            origin = "";
            return;
        }

        const nlohmann::json::object_t* originObj =
            originIt->get_ptr<const nlohmann::json::object_t*>();
        if (originObj == nullptr)
        {
            BMCWEB_LOG_WARNING(
                "OriginOfCondition is not an object - forwarding with empty origin");
            origin = "";
            return;
        }

        auto odataIdIt = originObj->find("@odata.id");
        if (odataIdIt == originObj->end())
        {
            BMCWEB_LOG_WARNING(
                "OriginOfCondition missing @odata.id - forwarding with empty origin");
            origin = "";
            return;
        }

        const std::string* odataIdStr =
            odataIdIt->second.get_ptr<const std::string*>();
        if (odataIdStr == nullptr)
        {
            BMCWEB_LOG_WARNING(
                "@odata.id is not a string - forwarding with empty origin");
            origin = "";
            return;
        }

        origin = *odataIdStr;
    }

    static nlohmann::json::object_t* parseJsonEvent(std::string_view eventData,
                                                    nlohmann::json& jsonOut)
    {
        jsonOut = nlohmann::json::parse(eventData, nullptr, false);
        if (jsonOut.is_discarded())
        {
            return nullptr;
        }
        return jsonOut.get_ptr<nlohmann::json::object_t*>();
    }

    void processAndForwardEvent(std::string_view eventData)
    {
        nlohmann::json jsonData;
        nlohmann::json::object_t* obj = parseJsonEvent(eventData, jsonData);
        if (obj == nullptr)
        {
            return;
        }

        auto eventsIt = obj->find("Events");
        if (eventsIt == obj->end())
        {
            return;
        }

        nlohmann::json::array_t* eventsArray =
            eventsIt->second.get_ptr<nlohmann::json::array_t*>();
        if (eventsArray == nullptr)
        {
            return;
        }

        const std::string& prefix = getAggregationPrefix();
        for (nlohmann::json& event : *eventsArray)
        {
            const nlohmann::json::object_t* eventObj =
                event.get_ptr<const nlohmann::json::object_t*>();
            if (eventObj == nullptr)
            {
                BMCWEB_LOG_ERROR("Event entry is not an object");
                continue;
            }

            auto eventTypeIt = event.find("EventType");
            auto messageIdIt = event.find("MessageId");
            if (eventTypeIt == event.end() || messageIdIt == event.end())
            {
                BMCWEB_LOG_ERROR("Event missing required fields");
                continue;
            }

            redfish::addPrefixes(event, prefix);

            std::string origin;
            extractOrigin(event, origin);

            // Forward event to subscribers - pass the object_t directly
            forwardSseEvent(*eventObj, origin, "Event");
        }
    }

    virtual void forwardSseEvent(const nlohmann::json::object_t& eventObject,
                                 const std::string& origin,
                                 const std::string& resourceType)
    {
        BMCWEB_LOG_DEBUG(
            "Forwarding satellite event to local subscribers - origin: {}, resourceType: {}",
            origin, resourceType);

        (void)origin;
        (void)resourceType;

        if (!satelliteEventCallback_)
        {
            BMCWEB_LOG_WARNING(
                "Satellite event callback not set, event not forwarded to local subscribers");
            return;
        }

        // Build the event message to send to subscribers
        nlohmann::json::array_t eventRecord;
        eventRecord.emplace_back(eventObject);

        nlohmann::json msgJson;
        msgJson["@odata.type"] = "#Event.v1_4_0.Event";
        msgJson["Name"] = "Event Log";
        msgJson["Events"] = std::move(eventRecord);

        std::string strMsg = msgJson.dump(
            2, ' ', true, nlohmann::json::error_handler_t::replace);

        uint64_t eventId = 0;
        if (!lastEventId.empty())
        {
            std::string_view idView(lastEventId);
            auto [ptr,
                  ec] = std::from_chars(idView.begin(), idView.end(), eventId);
            if (ec != std::errc{} || ptr != idView.end())
            {
                BMCWEB_LOG_WARNING("Failed to parse satellite event ID: {}",
                                   lastEventId);
                eventId = 0;
            }
        }
        satelliteEventCallback_(strMsg, eventId);
    }

    void scheduleReconnect()
    {
        if (state == State::Connected)
        {
            return;
        }

        state = State::Retrying;
        reconnectTimer.expires_after(retryIntervalSecs);
        reconnectTimer.async_wait(
            std::bind_front(&SseEventAggregator::handleReconnectTimer, this));
    }

  private:
    void cancelSatConfigRetryTimer()
    {
        if (satConfigLoadTimer)
        {
            satConfigLoadTimer->cancel();
            satConfigLoadTimer.reset();
        }
    }

    void stopSatConfigLoadTimer()
    {
        satConfigLoadCancelled = true;
        satConfigLoadDeadlineTimer.cancel();
        cancelSatConfigRetryTimer();
    }

    [[nodiscard]] bool isSatConfigRefresherAvailable() const
    {
        return !shuttingDown && !satConfigLoadCancelled && satConfigRefresher;
    }

    void applySatConfig(
        const std::unordered_map<std::string, boost::urls::url>& configs)
    {
        if (state != State::Idle)
        {
            BMCWEB_LOG_DEBUG("SSE connection already started or active");
            return;
        }

        // Callers must pass a non-empty map
        assert(!configs.empty());

        BMCWEB_LOG_DEBUG("SSE aggregator starting...");
        state = State::Configuring;

        initializeFromConfig(configs);
    }

    void commitSatConfigApply(
        const std::unordered_map<std::string, boost::urls::url>& satConfig,
        bool stopLoadTimer)
    {
        applySatConfig(satConfig);
        if (stopLoadTimer)
        {
            satConfigLoadDeadlineTimer.cancel();
            cancelSatConfigRetryTimer();
        }
    }

    void scheduleSatConfigLoad(int attempt)
    {
        cancelSatConfigRetryTimer();

        constexpr int maxDelaySec = 150;
        const int shiftAttempt = std::min(attempt, 10);
        const int delay = std::min(
            satConfigLoadBackoffBaseSec * (1 << shiftAttempt), maxDelaySec);

        BMCWEB_LOG_DEBUG(
            "Satellite config not available (attempt {}), retrying in {}s",
            attempt + 1, delay);

        satConfigLoadTimer = std::make_shared<boost::asio::steady_timer>(ioc);
        satConfigLoadTimer->expires_after(std::chrono::seconds(delay));
        satConfigLoadTimer->async_wait(
            [this, attempt](const boost::system::error_code& ec) {
                onSatConfigLoadTimer(attempt + 1, ec);
            });
    }

    void initSatConfigLoad(int attempt)
    {
        if (!isSatConfigRefresherAvailable())
        {
            return;
        }

        satConfigRefresher(
            std::bind_front(&SseEventAggregator::onLoadedSatConfig, this,
                            attempt),
            false);
    }

    void onLoadedSatConfig(
        int attempt, const boost::system::error_code& ec,
        const std::unordered_map<std::string, boost::urls::url>& satConfig)
    {
        if (shuttingDown || satConfigLoadCancelled)
        {
            return;
        }

        if (!ec && !satConfig.empty())
        {
            BMCWEB_LOG_INFO("Starting SSE Event Aggregator");
            commitSatConfigApply(satConfig, true);
            return;
        }

        scheduleSatConfigLoad(attempt);
    }

    void onSatConfigLoadTimer(int nextAttempt,
                              const boost::system::error_code& timerEc)
    {
        if (timerEc == boost::asio::error::operation_aborted)
        {
            return;
        }

        if (timerEc)
        {
            BMCWEB_LOG_ERROR("Satellite config retry timer error: {}",
                             timerEc.message());
            // Schedule retry despite timer error to avoid getting stuck
            if (!shuttingDown && !satConfigLoadCancelled)
            {
                initSatConfigLoad(nextAttempt);
            }
            return;
        }

        if (shuttingDown || satConfigLoadCancelled)
        {
            return;
        }

        initSatConfigLoad(nextAttempt);
    }

    void stopSseConnectionOnly()
    {
        reconnectTimer.cancel();
        if (sseConnection)
        {
            sseConnection.reset();
        }
        state = State::Idle;
    }

    void connectToEventStream()
    {
        if (url.empty())
        {
            BMCWEB_LOG_ERROR("URL not initialized");
            scheduleReconnect();
            return;
        }

        BMCWEB_LOG_DEBUG("Connecting to event stream: {}", url.buffer());

        boost::beast::http::fields headers;
        sentLastEventIdThisAttempt = false;
        if (!skipLastEventIdOnReconnect && !lastEventId.empty())
        {
            headers.set("Last-Event-Id", lastEventId);
            sentLastEventIdThisAttempt = true;
            BMCWEB_LOG_INFO("Reconnecting with Last-Event-Id: {}", lastEventId);
        }
        else if (skipLastEventIdOnReconnect)
        {
            BMCWEB_LOG_INFO(
                "Skipping Last-Event-Id on reconnect (previous attempt failed), starting fresh");
            skipLastEventIdOnReconnect = false;
        }

        sseConnection = std::make_shared<crow::SSEConnection>(
            ioc, "sse-aggregator", url, headers,
            std::bind_front(&SseEventAggregator::handleSseCallback, this),
            std::bind_front(&SseEventAggregator::handleInitialResponse, this));

        sseConnection->connect();
    }

    void handleInitialResponse(crow::Response& response)
    {
        if (response.result() != boost::beast::http::status::ok)
        {
            BMCWEB_LOG_ERROR("SSE connection failed with status: {}",
                             static_cast<unsigned>(response.result()));
            scheduleReconnect();
            return;
        }

        BMCWEB_LOG_DEBUG("SSE connection established successfully");
        skipLastEventIdOnReconnect = false;
        sentLastEventIdThisAttempt = false;
        state = State::Connected;

        if ((response.body() != nullptr) && !response.body()->empty())
        {
            handleSseData(*response.body());
        }
    }

    void handleSseCallback(const boost::system::error_code& ec,
                           std::string_view data)
    {
        if (ec)
        {
            BMCWEB_LOG_ERROR("SSE stream error: {}", ec.message());

            if (sentLastEventIdThisAttempt)
            {
                skipLastEventIdOnReconnect = true;
                BMCWEB_LOG_INFO(
                    "Connection closed after Last-Event-Id was sent, will retry without it");
            }

            state = State::Idle;
            scheduleReconnect();
            return;
        }

        handleSseData(data);
    }

    void handleReconnectTimer(const boost::system::error_code& ec)
    {
        if (ec)
        {
            if (ec != boost::asio::error::operation_aborted)
            {
                BMCWEB_LOG_ERROR("Reconnect timer error: {}", ec.message());
            }
            return;
        }

        connectToEventStream();
    }

    void initializeFromConfig(
        const std::unordered_map<std::string, boost::urls::url>& configs)
    {
        if (configs.empty())
        {
            BMCWEB_LOG_ERROR(
                "No satellite configuration found - SSE event aggregation will not be available");
            stop();
            return;
        }

        // Use the satellite configuration from redfishAggregator configs
        const auto& [name, configUrl] = *configs.begin();

        BMCWEB_LOG_INFO("Configuring SSE aggregation for satellite: {}", name);
        BMCWEB_LOG_DEBUG("Satellite URL: {}", configUrl.buffer());

        url = configUrl;
        url.set_path("/redfish/v1/EventService/SSE");

        BMCWEB_LOG_DEBUG("SSE aggregator will connect to: {}", url.buffer());

        // Extract host and port for logging and Last-Event-Id tracking
        this->host = std::string(configUrl.host());
        uint16_t portNumber = configUrl.port_number();
        if (portNumber != 0)
        {
            this->port = portNumber;
        }
        else
        {
            this->port = (configUrl.scheme() == "https") ? 443 : 80;
        }

        // Restore lastEventId from persistent storage for this satellite
        auto& eventIds = persistent_data::getConfig().sseAggregatorLastEventIds;
        auto it = eventIds.find(host);
        if (it != eventIds.end())
        {
            lastEventId = it->second;
            BMCWEB_LOG_DEBUG(
                "Restored satellite lastEventId from persistent storage: {}",
                lastEventId);
        }

        state = State::Connecting;
        // start the SSE connection
        connectToEventStream();
    }
};
} // namespace redfish
