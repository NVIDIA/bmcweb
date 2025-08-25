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
#include "server_sent_event.hpp"
#include "subscription.hpp"
#include "utils/nvidia_utils.hpp"
#include "utils/origin_utils.hpp"
#include "utils/time_utils.hpp"

#include <boost/circular_buffer.hpp>
#include <boost/circular_buffer/base.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/format.hpp>
#include <boost/system/result.hpp>
#include <boost/url/parse.hpp>
#include <boost/url/url_view_base.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
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
    ~EventServiceManager() = default;

    explicit EventServiceManager()
    {
        // Load config from persist store.
        initConfig();
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
            BMCWEB_LOG_INFO("Attempting to find message for last id {}",
                            lastEventId);
            boost::circular_buffer<Event>::iterator lastEvent =
                std::ranges::find_if(
                    messages, [&lastEventId](const Event& event) {
                        return std::to_string(event.id) == lastEventId;
                    });
            // Can't find a matching ID
            if (lastEvent == messages.end())
            {
                nlohmann::json msg = messages::eventBufferExceeded();

                std::string strMsg = msg.dump(
                    2, ' ', true, nlohmann::json::error_handler_t::replace);
                eventId++;
                subValue->sendEventToSubscriber(eventId, std::move(strMsg));
            }
            else
            {
                // Skip the last event the user already has
                lastEvent++;

                for (boost::circular_buffer<Event>::const_iterator event =
                         lastEvent;
                     event != messages.end(); event++)
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
        nlohmann::json::array_t logEntryArray;
        nlohmann::json& logEntryJson =
            logEntryArray.emplace_back(nlohmann::json::object());

        logEntryJson["EventId"] = std::to_string(eventId);

        if (testEvent.eventGroupId)
        {
            logEntryJson["EventGroupId"] = *testEvent.eventGroupId;
        }
        eventId++;
        logEntryJson["EventId"] = std::to_string(eventId);

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

        nlohmann::json::object_t msg;
        msg["@odata.type"] = "#Event.v1_4_0.Event";
        msg["Id"] = std::to_string(eventId);
        msg["Name"] = "Event Log";
        msg["Events"] = logEntryArray;

        std::string strMsg = nlohmann::json(msg).dump(
            2, ' ', true, nlohmann::json::error_handler_t::replace);

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
            std::string strMsg = nlohmann::json(msg).dump(
                2, ' ', true, nlohmann::json::error_handler_t::replace);
            entry->sendEventToSubscriber(eventId, std::move(strMsg));
        }

        return true;
    }

    static void sendEventsToSubs(
        const std::vector<EventLogObjectsType>& eventRecords)
    {
        EventServiceManager& mgr = EventServiceManager::getInstance();
        mgr.eventId++;
        for (const auto& it : mgr.subscriptionsMap)
        {
            Subscription& entry = *it.second;
            entry.filterAndSendEventLogs(mgr.eventId, eventRecords);
        }
    }

    static void sendTelemetryReportToSubs(
        const std::string& reportId, const telemetry::TimestampReadings& var)
    {
        EventServiceManager& mgr = EventServiceManager::getInstance();
        mgr.eventId++;

        for (const auto& it : mgr.subscriptionsMap)
        {
            Subscription& entry = *it.second;
            entry.filterAndSendReports(mgr.eventId, reportId, var);
        }
    }

    void sendEvent(nlohmann::json::object_t eventMessage,
                   std::string_view origin, std::string_view resourceType)
    {
        eventId++;
        eventMessage["EventId"] = eventId;

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
            std::string strMsg = nlohmann::json(msg).dump(
                2, ' ', true, nlohmann::json::error_handler_t::replace);
            entry->sendEventToSubscriber(eventId, std::move(strMsg));
        }
        eventId++; // increament the eventId
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
     *        The map @a dBusToRedfishURI is used for that purpose
     * @param path  orginal path that came from Phosphor Logging
     * @param event  the event to be sent out
     */
    void eventServiceOOC(const std::string& path, const std::string& devName,
                         NvEvent& event)
    {
        if constexpr (BMCWEB_REDFISH_AGGREGATION)
        {
            // OOC Path in HMC events is already converted to Redfish path.
            if (path.starts_with("/redfish/v1/"))
            {
                std::string oocPath(path);
                addPrefixToStringItem(oocPath, redfishAggregationPrefix);
                sendEventWithOOC(oocPath, event);
                return;
            }
        }
        sdbusplus::message::object_path objPath(path);
        std::string deviceName = objPath.filename();
        if (!deviceName.empty())
        {
            for (const auto& it : origin_utils::dBusToRedfishURI)
            {
                if (path.find(it.first) != std::string::npos)
                {
                    std::string newPath;
                    if (it.first == origin_utils::sensorSubTree)
                    {
                        std::string chassisName(PLATFORMDEVICEPREFIX);
                        chassisName += devName;
                        std::string sensorName;
                        dbus::utility::getNthStringFromPath(path, 4,
                                                            sensorName);
                        newPath = chassisName + "/Sensors/";
                        newPath += sensorName;
                    }
                    else
                    {
                        newPath = path.substr(it.first.length(), path.length());
                    }
                    sendEventWithOOC(it.second + newPath, event);
                    return;
                }
            }
        }

        BMCWEB_LOG_WARNING(
            "No Matching prefix found for OriginOfCondition Object Path: '{}' sending empty OriginOfCondition",
            path);

        sendEventWithOOC(std::string{""}, event);
    }
};

} // namespace redfish
