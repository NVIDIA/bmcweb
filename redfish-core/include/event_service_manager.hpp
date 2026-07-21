// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
// SPDX-FileCopyrightText: Copyright 2020 Intel Corporation
#pragma once
#include "bmcweb_config.h"

#include "cper_utils.hpp"
#include "dbus_log_watcher.hpp"
#include "error_messages.hpp"
#include "event_logs_object_type.hpp"
#include "event_matches_filter.hpp"
#include "event_service_store.hpp"
#include "filesystem_log_watcher.hpp"
#include "io_context_singleton.hpp"
#include "logging.hpp"
#include "metric_report.hpp"
#include "nvidia_event_service_manager.hpp"
#include "ossl_random.hpp"
#include "persistent_data.hpp"
#include "redfish_aggregator.hpp"
#include "server_sent_event.hpp"
#include "subscription.hpp"
#include "telemetry_readings.hpp"
#include "utils/nvidia_time_utils.hpp"
#include "utils/nvidia_utils.hpp"
#include "utils/origin_utils.hpp"
#include "utils/time_utils.hpp"

#include <boost/circular_buffer.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/system/result.hpp>
#include <boost/url/parse.hpp>
#include <boost/url/url_view_base.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace redfish
{

static constexpr const char* eventFormatType = "Event";
static constexpr const char* metricReportFormatType = "MetricReport";

class EventServiceManager
{
  private:
    bool serviceEnabled = false;
    uint32_t retryAttempts = 0;
    uint32_t retryTimeoutInterval = 0;

    size_t noOfEventLogSubscribers{0};
    size_t noOfMetricReportSubscribers{0};
    std::optional<DbusEventLogMonitor> dbusEventLogMonitor;
    std::optional<DbusTelemetryMonitor> matchTelemetryMonitor;
    std::optional<FilesystemLogWatcher> filesystemLogMonitor;
    boost::container::flat_map<std::string, std::shared_ptr<Subscription>>
        subscriptionsMap;

    uint64_t eventId{1};
    bool eventIdDirty = false; // Track if eventId changed since last persist
    std::unique_ptr<boost::asio::steady_timer> persistTimer;

    // Persist configuration - save every 5 minutes if changed
    static constexpr std::chrono::minutes persistIntervalMins{5};

    // Increment eventId (marks dirty for periodic persist)
    uint64_t getNextEventId()
    {
        eventId++;
        eventIdDirty = true; // Mark for periodic persist
        return eventId;
    }

    // Persist eventId to storage if it has changed
    void persistEventIdIfDirty()
    {
        if (eventIdDirty)
        {
            persistent_data::getConfig().eventServiceEventId = eventId;
            persistent_data::getConfig().writeData();
            eventIdDirty = false;
            BMCWEB_LOG_DEBUG("Persisted eventId to storage: {}", eventId);
        }
    }

    // Schedule periodic persist timer (every 5 minutes)
    void schedulePersistTimer()
    {
        if (!persistTimer)
        {
            return;
        }
        persistTimer->expires_after(persistIntervalMins);
        persistTimer->async_wait([this](const boost::system::error_code& ec) {
            if (ec == boost::asio::error::operation_aborted)
            {
                return;
            }
            if (ec)
            {
                BMCWEB_LOG_ERROR("EventService persist timer error: {}",
                                 ec.message());
                return;
            }
            persistEventIdIfDirty();
            schedulePersistTimer(); // Reschedule
        });
    }

    struct Event
    {
        uint64_t id;
        nlohmann::json::object_t message;
    };

    constexpr static size_t maxMessages = 200;
    boost::circular_buffer<Event> messages{maxMessages};

  public:
    EventServiceManager(const EventServiceManager&) = delete;
    EventServiceManager& operator=(const EventServiceManager&) = delete;
    EventServiceManager(EventServiceManager&&) = delete;
    EventServiceManager& operator=(EventServiceManager&&) = delete;

    ~EventServiceManager()
    {
        // Save any pending changes
        persistEventIdIfDirty();
    }

    explicit EventServiceManager()
    {
        // Initialize persist timer
        persistTimer =
            std::make_unique<boost::asio::steady_timer>(getIoContext());

        // Load config from persist store.
        initConfig();

        // Start periodic persist timer
        schedulePersistTimer();
    }

    static EventServiceManager& getInstance()
    {
        static EventServiceManager handler;
        return handler;
    }

    void initConfig()
    {
        persistent_data::EventServiceConfig eventServiceConfig =
            persistent_data::EventServiceStore::getInstance()
                .getEventServiceConfig();

        serviceEnabled = eventServiceConfig.enabled;
        retryAttempts = eventServiceConfig.retryAttempts;
        retryTimeoutInterval = eventServiceConfig.retryTimeoutInterval;

        // Restore eventId from persistent storage for Last-Event-Id support
        eventId = persistent_data::getConfig().eventServiceEventId;
        BMCWEB_LOG_DEBUG("Restored eventId from persistent storage: {}",
                         eventId);
        // Note: eventId will be persisted on next periodic timer if it changes

        for (const auto& it : persistent_data::EventServiceStore::getInstance()
                                  .subscriptionsConfigMap)
        {
            std::shared_ptr<persistent_data::UserSubscription> newSub =
                it.second;

            boost::system::result<boost::urls::url> url =
                boost::urls::parse_absolute_uri(newSub->destinationUrl);

            if (!url)
            {
                BMCWEB_LOG_ERROR(
                    "Failed to validate and split destination url");
                continue;
            }
            std::shared_ptr<Subscription> subValue =
                std::make_shared<Subscription>(newSub, *url, getIoContext());
            std::string id = subValue->userSub->id;
            subValue->deleter = [id]() {
                EventServiceManager::getInstance().deleteSubscription(id);
            };

            subscriptionsMap.emplace(id, subValue);

            updateNoOfSubscribersCount();

            // Update retry configuration.
            subValue->updateRetryConfig(retryAttempts, retryTimeoutInterval);

            // schedule a heartbeat if sendHeartbeat was set to true
            if (subValue->userSub->sendHeartbeat)
            {
                subValue->scheduleNextHeartbeatEvent();
            }
        }
        if constexpr (BMCWEB_REDFISH_AGGREGATION)
        {
            // SSE aggregator callback for handling satellite events
            RedfishAggregator::getInstance().setSatelliteEventCallback(
                [](const std::string& eventJson, uint64_t satEventId) {
                    getInstance().handleSatelliteEvent(eventJson, satEventId);
                });

            redfish::SubscribeSatBmc::getInstance().createSubscribeTimer();
            if (getNumberOfSubscriptions() > 0)
            {
                // start RF event listener and subscribe HMC eventService.
                initRedfishEventListener(getIoContext());
            }
        }
    }

    void updateSubscriptionData() const
    {
        persistent_data::EventServiceStore::getInstance()
            .eventServiceConfig.enabled = serviceEnabled;
        persistent_data::EventServiceStore::getInstance()
            .eventServiceConfig.retryAttempts = retryAttempts;
        persistent_data::EventServiceStore::getInstance()
            .eventServiceConfig.retryTimeoutInterval = retryTimeoutInterval;

        persistent_data::getConfig().writeData();
    }

    void setEventServiceConfig(const persistent_data::EventServiceConfig& cfg,
                               const std::string_view url = "")
    {
        bool updateConfig = false;
        bool updateRetryCfg = false;
        if (url.empty())
        {
            BMCWEB_LOG_DEBUG("empty URL");
        }

        if (serviceEnabled)
        {
            if (noOfEventLogSubscribers > 0U)
            {
                if constexpr (BMCWEB_REDFISH_DBUS_LOG)
                {
                    if (!dbusEventLogMonitor)
                    {
                        if constexpr (
                            BMCWEB_EXPERIMENTAL_REDFISH_DBUS_LOG_SUBSCRIPTION)
                        {
                            dbusEventLogMonitor.emplace();
                        }
                    }
                }
                else
                {
                    if (!filesystemLogMonitor)
                    {
                        filesystemLogMonitor.emplace(getIoContext());
                    }
                }
            }
            else
            {
                dbusEventLogMonitor.reset();
                filesystemLogMonitor.reset();
            }

            if (noOfMetricReportSubscribers > 0U)
            {
                if (!matchTelemetryMonitor)
                {
                    matchTelemetryMonitor.emplace();
                }
            }
            else
            {
                matchTelemetryMonitor.reset();
            }
        }
        else
        {
            matchTelemetryMonitor.reset();
            dbusEventLogMonitor.reset();
            filesystemLogMonitor.reset();
        }

        if (serviceEnabled != cfg.enabled)
        {
            serviceEnabled = cfg.enabled;
            updateConfig = true;
            if constexpr (BMCWEB_REDFISH_DBUS_EVENT)
            {
                // Send an NvEvent for session creation
                NvEvent event = redfish::EventUtil::createEventPropertyModified(
                    "ServiceEnabled",
                    std::to_string(static_cast<int>(serviceEnabled)),
                    "EventService");
                redfish::EventServiceManager::getInstance().sendEventWithOOC(
                    std::string(url), event);
            }
        }

        if (retryAttempts != cfg.retryAttempts)
        {
            retryAttempts = cfg.retryAttempts;
            updateConfig = true;
            updateRetryCfg = true;
            if constexpr (BMCWEB_REDFISH_DBUS_LOG)
            {
                // Send an NvEvent for property change
                NvEvent event = redfish::EventUtil::createEventPropertyModified(
                    "DeliveryRetryAttempts", std::to_string(retryAttempts),
                    "EventService");
                redfish::EventServiceManager::getInstance().sendEventWithOOC(
                    std::string(url), event);
            }
        }

        if (retryTimeoutInterval != cfg.retryTimeoutInterval)
        {
            retryTimeoutInterval = cfg.retryTimeoutInterval;
            updateConfig = true;
            updateRetryCfg = true;
            if constexpr (BMCWEB_REDFISH_DBUS_LOG)
            {
                // Send an event for property change
                NvEvent event = redfish::EventUtil::createEventPropertyModified(
                    "DeliveryRetryIntervalSeconds",
                    std::to_string(retryTimeoutInterval), "EventService");
                redfish::EventServiceManager::getInstance().sendEventWithOOC(
                    std::string(url), event);
            }
        }

        if (updateConfig)
        {
            updateSubscriptionData();
        }

        if (updateRetryCfg)
        {
            // Update the changed retry config to all subscriptions
            for (const auto& it :
                 EventServiceManager::getInstance().subscriptionsMap)
            {
                Subscription& entry = *it.second;
                entry.updateRetryConfig(retryAttempts, retryTimeoutInterval);
            }
        }
    }

    void updateNoOfSubscribersCount()
    {
        size_t eventLogSubCount = 0;
        size_t metricReportSubCount = 0;
        for (const auto& it : subscriptionsMap)
        {
            std::shared_ptr<Subscription> entry = it.second;
            if (entry->userSub->eventFormatType == eventFormatType)
            {
                eventLogSubCount++;
            }
            else if (entry->userSub->eventFormatType == metricReportFormatType)
            {
                metricReportSubCount++;
            }
        }
        noOfEventLogSubscribers = eventLogSubCount;
        if (eventLogSubCount > 0U)
        {
            if constexpr (BMCWEB_REDFISH_DBUS_LOG)
            {
                if (!dbusEventLogMonitor &&
                    BMCWEB_EXPERIMENTAL_REDFISH_DBUS_LOG_SUBSCRIPTION)
                {
                    dbusEventLogMonitor.emplace();
                }
            }
            else
            {
                if (!filesystemLogMonitor)
                {
                    filesystemLogMonitor.emplace(getIoContext());
                }
            }
        }
        else
        {
            dbusEventLogMonitor.reset();
            filesystemLogMonitor.reset();
        }

        noOfMetricReportSubscribers = metricReportSubCount;
        if (metricReportSubCount > 0U)
        {
            if (!matchTelemetryMonitor)
            {
                matchTelemetryMonitor.emplace();
            }
        }
        else
        {
            matchTelemetryMonitor.reset();
        }
    }

    std::shared_ptr<Subscription> getSubscription(const std::string& id)
    {
        auto obj = subscriptionsMap.find(id);
        if (obj == subscriptionsMap.end())
        {
            BMCWEB_LOG_ERROR("No subscription exist with ID:{}", id);
            return nullptr;
        }
        std::shared_ptr<Subscription> subValue = obj->second;
        return subValue;
    }

    std::string addSubscriptionInternal(
        const std::shared_ptr<Subscription>& subValue)
    {
        std::uniform_int_distribution<uint32_t> dist(0);
        bmcweb::OpenSSLGenerator gen;

        std::string id;

        int retry = 3;
        while (retry != 0)
        {
            id = std::to_string(dist(gen));
            if (gen.error())
            {
                retry = 0;
                break;
            }
            auto inserted = subscriptionsMap.insert(std::pair(id, subValue));
            if (inserted.second)
            {
                break;
            }
            --retry;
        }

        if (retry <= 0)
        {
            BMCWEB_LOG_ERROR("Failed to generate random number");
            return "";
        }

        // Set Subscription ID for back trace
        subValue->userSub->id = id;

        persistent_data::EventServiceStore::getInstance()
            .subscriptionsConfigMap.emplace(id, subValue->userSub);

        updateNoOfSubscribersCount();

        // Update retry configuration.
        subValue->updateRetryConfig(retryAttempts, retryTimeoutInterval);

        return id;
    }

    std::string addSSESubscription(
        const std::shared_ptr<Subscription>& subValue,
        std::string_view lastEventId)
    {
        std::string id = addSubscriptionInternal(subValue);

        if (!lastEventId.empty())
        {
            BMCWEB_LOG_INFO("Attempting to find events after id {}",
                            lastEventId);

            // Parse lastEventId
            uint64_t requestedId = 0;
            std::string_view idView(lastEventId);
            auto result =
                std::from_chars(idView.begin(), idView.end(), requestedId);

            if (result.ec != std::errc{})
            {
                BMCWEB_LOG_WARNING("Invalid Last-Event-Id format: {}",
                                   lastEventId);
            }
            else if (messages.empty())
            {
                BMCWEB_LOG_INFO("Buffer is empty, no events to replay");
            }
            else if (requestedId >= messages.back().id)
            {
                // Client already has the latest event, nothing to replay
                BMCWEB_LOG_INFO(
                    "Client is up to date (requested {} >= latest {})",
                    requestedId, messages.back().id);
            }
            else if (requestedId < messages.front().id - 1)
            {
                // Requested ID is too old, events have been evicted
                BMCWEB_LOG_INFO(
                    "Event {} too old, buffer starts at {} - sending EventBufferExceeded",
                    requestedId, messages.front().id);
                nlohmann::json msg = messages::eventBufferExceeded();
                std::string strMsg = msg.dump(
                    2, ' ', true, nlohmann::json::error_handler_t::replace);
                subValue->sendEventToSubscriber(getNextEventId(),
                                                std::move(strMsg));
            }
            else
            {
                // Find first event with ID > requestedId and replay from there
                auto firstEvent = std::ranges::find_if(
                    messages, [requestedId](const Event& event) {
                        return event.id > requestedId;
                    });

                if (firstEvent != messages.end())
                {
                    BMCWEB_LOG_INFO("Replaying {} events starting from id {}",
                                    std::distance(firstEvent, messages.end()),
                                    firstEvent->id);

                    for (auto event = firstEvent; event != messages.end();
                         ++event)
                    {
                        std::string strMsg =
                            nlohmann::json(event->message)
                                .dump(2, ' ', true,
                                      nlohmann::json::error_handler_t::replace);
                        subValue->sendEventToSubscriber(event->id,
                                                        std::move(strMsg));
                    }
                }
            }
        }
        BMCWEB_LOG_INFO("addSSESubscription: returning id={}", id);
        return id;
    }

    std::string addPushSubscription(
        const std::shared_ptr<Subscription>& subValue)
    {
        std::string id = addSubscriptionInternal(subValue);
        subValue->deleter = [id]() {
            EventServiceManager::getInstance().deleteSubscription(id);
        };
        updateSubscriptionData();
        return id;
    }

    bool isSubscriptionExist(const std::string& id)
    {
        auto obj = subscriptionsMap.find(id);
        return obj != subscriptionsMap.end();
    }

    bool deleteSubscription(const std::string& id)
    {
        auto obj = subscriptionsMap.find(id);
        if (obj == subscriptionsMap.end())
        {
            BMCWEB_LOG_WARNING("Could not find subscription with id {}", id);
            return false;
        }
        subscriptionsMap.erase(obj);
        auto& event = persistent_data::EventServiceStore::getInstance();
        auto persistentObj = event.subscriptionsConfigMap.find(id);
        if (persistentObj == event.subscriptionsConfigMap.end())
        {
            BMCWEB_LOG_ERROR("Subscription {} wasn't in persistent data", id);
            return true;
        }
        persistent_data::EventServiceStore::getInstance()
            .subscriptionsConfigMap.erase(persistentObj);
        updateNoOfSubscribersCount();
        updateSubscriptionData();

        return true;
    }

    void deleteSseSubscription(const crow::sse_socket::Connection& thisConn)
    {
        for (auto it = subscriptionsMap.begin(); it != subscriptionsMap.end();)
        {
            std::shared_ptr<Subscription> entry = it->second;
            bool entryIsThisConn = entry->matchSseId(thisConn);
            if (entryIsThisConn)
            {
                persistent_data::EventServiceStore::getInstance()
                    .subscriptionsConfigMap.erase(entry->userSub->id);
                it = subscriptionsMap.erase(it);
                return;
            }
            it++;
        }
    }

    size_t getNumberOfSubscriptions() const
    {
        return subscriptionsMap.size();
    }

    size_t getNumberOfSSESubscriptions() const
    {
        auto size = std::ranges::count_if(
            subscriptionsMap,
            [](const std::pair<std::string, std::shared_ptr<Subscription>>&
                   entry) {
                return (entry.second->userSub->subscriptionType ==
                        subscriptionTypeSSE);
            });
        return static_cast<size_t>(size);
    }

    std::vector<std::string> getAllIDs()
    {
        std::vector<std::string> idList;
        for (const auto& it : subscriptionsMap)
        {
            idList.emplace_back(it.first);
        }
        return idList;
    }

    bool sendTestEventLog(TestEvent& testEvent)
    {
        nlohmann::json::object_t logEntryJson;

        if (testEvent.eventGroupId)
        {
            logEntryJson["EventGroupId"] = *testEvent.eventGroupId;
        }
        logEntryJson["EventId"] = std::to_string(getNextEventId());

        if (testEvent.eventTimestamp)
        {
            logEntryJson["EventTimestamp"] = *testEvent.eventTimestamp;
        }

        if (testEvent.originOfCondition)
        {
            logEntryJson["OriginOfCondition"]["@odata.id"] =
                *testEvent.originOfCondition;
        }
        if (testEvent.severity)
        {
            logEntryJson["Severity"] = *testEvent.severity;
        }

        if (testEvent.message)
        {
            logEntryJson["Message"] = *testEvent.message;
        }

        if (testEvent.resolution)
        {
            logEntryJson["Resolution"] = *testEvent.resolution;
        }

        if (testEvent.messageId)
        {
            logEntryJson["MessageId"] = *testEvent.messageId;
        }

        if (testEvent.messageArgs)
        {
            logEntryJson["MessageArgs"] = *testEvent.messageArgs;
        }
        // MemberId is 0 : since we are sending one event record.
        logEntryJson["MemberId"] = "0";

        nlohmann::json::array_t logEntryArray;
        logEntryArray.emplace_back(logEntryJson);

        nlohmann::json::object_t msg;
        msg["@odata.type"] = "#Event.v1_4_0.Event";
        msg["Id"] = std::to_string(eventId);
        msg["Name"] = "Event Log";
        msg["Events"] = logEntryArray;

        // Prepare and send events to matching subscribers below

        messages.push_back(Event(eventId, msg));
        msg["Id"] = std::to_string(eventId);

        for (const auto& it : subscriptionsMap)
        {
            std::shared_ptr<Subscription> entry = it.second;
            if (!eventMatchesFilter(*entry->userSub, logEntryJson, "Event"))
            {
                BMCWEB_LOG_DEBUG("Filter didn't match");
                continue;
            }

            if (!entry->userSub->customText.empty())
            {
                msg["Context"] = entry->userSub->customText;
            }
            else
            {
                msg.erase("Context");
            }

            std::string strMsg2 = nlohmann::json(msg).dump(
                2, ' ', true, nlohmann::json::error_handler_t::replace);
            entry->sendEventToSubscriber(eventId, std::move(strMsg2));
        }

        return true;
    }

    static void sendEventsToSubs(
        const std::vector<EventLogObjectsType>& eventRecords)
    {
        EventServiceManager& mgr = EventServiceManager::getInstance();
        uint64_t currentEventId = mgr.getNextEventId();
        for (const auto& it : mgr.subscriptionsMap)
        {
            Subscription& entry = *it.second;
            entry.filterAndSendEventLogs(currentEventId, eventRecords);
        }
    }

    static void sendTelemetryReportToSubs(
        const std::string& reportId, const telemetry::TimestampReadings& var)
    {
        EventServiceManager& mgr = EventServiceManager::getInstance();
        uint64_t currentEventId = mgr.getNextEventId();

        for (const auto& it : mgr.subscriptionsMap)
        {
            Subscription& entry = *it.second;
            entry.filterAndSendReports(currentEventId, reportId, var);
        }
    }

    void handleSatelliteEvent(const std::string& eventJson, uint64_t satEventId)
    {
        nlohmann::json parsedEvent =
            nlohmann::json::parse(eventJson, nullptr, false);
        if (!parsedEvent.is_discarded() && parsedEvent.is_object())
        {
            nlohmann::json::object_t* msgObj =
                parsedEvent.get_ptr<nlohmann::json::object_t*>();
            if (msgObj != nullptr)
            {
                messages.push_back(Event(satEventId, *msgObj));
            }
        }

        for (const auto& [subId, subPtr] : subscriptionsMap)
        {
            if (subPtr->userSub->subscriptionType == subscriptionTypeSSE)
            {
                subPtr->sendEventToSubscriber(satEventId,
                                              std::string(eventJson));
            }
        }
    }

    void sendEvent(nlohmann::json::object_t eventMessage,
                   std::string_view origin, std::string_view resourceType)
    {
        uint64_t currentEventId = getNextEventId();
        eventMessage["EventId"] = currentEventId;

        eventMessage["EventTimestamp"] =
            redfish::time_utils::getDateTimeOffsetNow().first;

        if (!origin.empty())
        {
            eventMessage["OriginOfCondition"] = origin;
        }

        // MemberId is 0 : since we are sending one event record.
        eventMessage["MemberId"] = "0";

        messages.push_back(Event(eventId, eventMessage));

        for (auto& it : subscriptionsMap)
        {
            std::shared_ptr<Subscription>& entry = it.second;
            if (!eventMatchesFilter(*entry->userSub, eventMessage,
                                    resourceType))
            {
                BMCWEB_LOG_DEBUG("Filter didn't match");
                continue;
            }

            nlohmann::json::array_t eventRecord;
            eventRecord.emplace_back(eventMessage);

            nlohmann::json msgJson;

            msgJson["@odata.type"] = "#Event.v1_4_0.Event";
            msgJson["Name"] = "Event Log";
            msgJson["Id"] = eventId;
            msgJson["Events"] = std::move(eventRecord);
            if (!entry->userSub->customText.empty())
            {
                msgJson["Context"] = entry->userSub->customText;
            }

            std::string strMsg = msgJson.dump(
                2, ' ', true, nlohmann::json::error_handler_t::replace);
            entry->sendEventToSubscriber(eventId, std::move(strMsg));
        }
    }

    /*!
     * @brief   Send the event to all subscribers.
     * @param[in] event   The event to be sent.
     * @return  Void
     */
    void sendEvent(NvEvent& event)
    {
        nlohmann::json::object_t logEntry;
        if (event.formatEventLogEntry(logEntry) != 0)
        {
            BMCWEB_LOG_ERROR("Failed to format the event log entry");
        }
        logEntry["EventId"] = std::to_string(eventId);
        nlohmann::json eventsArray = nlohmann::json::array();
        eventsArray.push_back(logEntry);
        nlohmann::json::object_t msg;
        msg["@odata.type"] = "#Event.v1_9_0.Event";
        msg["Id"] = std::to_string(eventId);
        msg["Name"] = "Event Log";
        msg["Events"] = eventsArray;
        messages.push_back(Event(eventId, msg));
        for (const auto& it : this->subscriptionsMap)
        {
            std::shared_ptr<Subscription> entry = it.second;
            if (!eventMatchesFilter(*entry->userSub, logEntry, "Event"))
            {
                BMCWEB_LOG_DEBUG("Filter didn't match");
                continue;
            }

            if (!entry->userSub->customText.empty())
            {
                msg["Context"] = entry->userSub->customText;
            }
            else
            {
                msg.erase("Context");
            }

            std::string strMsg = nlohmann::json(msg).dump(
                2, ' ', true, nlohmann::json::error_handler_t::replace);
            entry->sendEventToSubscriber(eventId, std::move(strMsg));
        }
        getNextEventId(); // increment and persist for next event
    }

    /**
     * Populates event with origin of condition
     * then sends the event for Redfish Event Listener
     * to pick up
     */
    void sendEventWithOOC(const std::string& ooc, NvEvent& event)
    {
        event.originOfCondition = ooc;
        sendEvent(event);
    }

    /**
     * @brief Finds the right OriginOfCondition for @a path and sends the Event
     *        Uses origin_utils::resolveDbusPathToRedfishUri for the mapping.
     * @param path  original path that came from Phosphor Logging
     * @param event  the event to be sent out
     */
    void eventServiceOOC(const std::string& path, const std::string& devName,
                         NvEvent& event)
    {
        if constexpr (BMCWEB_REDFISH_AGGREGATION)
        {
            // OOC Path in SatMC events is already converted to Redfish path.
            if (path.starts_with("/redfish/v1/"))
            {
                std::string oocPath(path);
                addPrefixToStringItem(oocPath, redfishAggregationPrefix);
                sendEventWithOOC(oocPath, event);
                return;
            }
        }
        std::string redfishUri =
            origin_utils::resolveDbusPathToRedfishUri(path, devName);
        if (redfishUri.empty())
        {
            BMCWEB_LOG_WARNING(
                "No matching prefix found for OriginOfCondition Object Path: '{}' sending empty OriginOfCondition",
                path);
        }
        sendEventWithOOC(redfishUri, event);
    }
};

} // namespace redfish
