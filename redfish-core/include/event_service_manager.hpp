/*
// Copyright (c) 2020 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
#pragma once
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "event_service_store.hpp"
#include "filter_expr_executor.hpp"
#include "http_client.hpp"
#include "metric_report.hpp"
#include "ossl_random.hpp"
#include "persistent_data.hpp"
#include "registries.hpp"
#include "registries_selector.hpp"
#include "str_utility.hpp"
#include "subscriber.hpp"
#include "utility.hpp"
#include "utils/json_utils.hpp"
#include "utils/time_utils.hpp"

#include <sys/inotify.h>

#include <async_resp.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/url/format.hpp>
#include <boost/url/url_view_base.hpp>
#include <dbus_singleton.hpp>
#include <sdbusplus/bus/match.hpp>
#include <utils/dbus_log_utils.hpp>
#include <utils/json_utils.hpp>
#include <utils/log_services_util.hpp>
#include <utils/origin_utils.hpp>
#include <utils/registry_utils.hpp>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <format>
#include <fstream>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>

namespace redfish
{
const std::regex urlRegex("(http|https)://([^/\\x20\\x3f\\x23\\x3a]+):?([0-9]*)"
                          "((/[^\\x20\\x23\\x3f]*\\x3f?[^\\x20\\x23\\x3f]*)?)");

using ReadingsObjType =
    std::vector<std::tuple<std::string, std::string, double, int32_t>>;

static constexpr const char* eventFormatType = "Event";
static constexpr const char* metricReportFormatType = "MetricReport";

static constexpr const char* subscriptionTypeSSE = "SSE";
static constexpr const char* eventServiceFile =
    "/var/lib/bmcweb/eventservice_config.json";

static constexpr const uint8_t maxNoOfSubscriptions = 20;
static constexpr const uint8_t maxNoOfSSESubscriptions = 10;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::optional<boost::asio::posix::stream_descriptor> inotifyConn;
static constexpr const char* redfishEventLogDir = "/var/log";
static constexpr const char* redfishEventLogFile = "/var/log/redfish";
static constexpr const size_t iEventSize = sizeof(inotify_event);

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int inotifyFd = -1;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int dirWatchDesc = -1;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static int fileWatchDesc = -1;
struct EventLogObjectsType
{
    std::string id;
    std::string timestamp;
    std::string messageId;
    std::vector<std::string> messageArgs;
};

namespace event_log
{
inline bool getUniqueEntryID(const std::string& logEntry, std::string& entryID)
{
    static time_t prevTs = 0;
    static int index = 0;

    // Get the entry timestamp
    std::time_t curTs = 0;
    std::tm timeStruct = {};
    std::istringstream entryStream(logEntry);
    if (entryStream >> std::get_time(&timeStruct, "%Y-%m-%dT%H:%M:%S"))
    {
        curTs = std::mktime(&timeStruct);
        if (curTs == -1)
        {
            return false;
        }
    }
    // If the timestamp isn't unique, increment the index
    index = (curTs == prevTs) ? index + 1 : 0;

    // Save the timestamp
    prevTs = curTs;

    entryID = std::to_string(curTs);
    if (index > 0)
    {
        entryID += "_" + std::to_string(index);
    }
    return true;
}

inline int getEventLogParams(const std::string& logEntry,
                             std::string& timestamp, std::string& messageID,
                             std::vector<std::string>& messageArgs)
{
    // The redfish log format is "<Timestamp> <MessageId>,<MessageArgs>"
    // First get the Timestamp
    size_t space = logEntry.find_first_of(' ');
    if (space == std::string::npos)
    {
        return -EINVAL;
    }
    timestamp = logEntry.substr(0, space);
    // Then get the log contents
    size_t entryStart = logEntry.find_first_not_of(' ', space);
    if (entryStart == std::string::npos)
    {
        return -EINVAL;
    }
    std::string_view entry(logEntry);
    entry.remove_prefix(entryStart);
    // Use split to separate the entry into its fields
    std::vector<std::string> logEntryFields;
    bmcweb::split(logEntryFields, entry, ',');
    // We need at least a MessageId to be valid
    if (logEntryFields.empty())
    {
        return -EINVAL;
    }
    messageID = logEntryFields[0];

    // Get the MessageArgs from the log if there are any
    if (logEntryFields.size() > 1)
    {
        const std::string& messageArgsStart = logEntryFields[1];
        // If the first string is empty, assume there are no MessageArgs
        if (!messageArgsStart.empty())
        {
            messageArgs.assign(logEntryFields.begin() + 1,
                               logEntryFields.end());
        }
    }

    return 0;
}

inline void getRegistryAndMessageKey(const std::string& messageID,
                                     std::string& registryName,
                                     std::string& messageKey)
{
    // Redfish MessageIds are in the form
    // RegistryName.MajorVersion.MinorVersion.MessageKey, so parse it to find
    // the right Message
    std::vector<std::string> fields;
    fields.reserve(4);
    bmcweb::split(fields, messageID, '.');
    if (fields.size() == 4)
    {
        registryName = fields[0];
        messageKey = fields[3];
    }
}

inline int formatEventLogEntry(const std::string& logEntryID,
                               const std::string& messageID,
                               const std::span<std::string_view> messageArgs,
                               std::string timestamp,
                               const std::string& customText,
                               nlohmann::json::object_t& logEntryJson)
{
    // Get the Message from the MessageRegistry
    const registries::Message* message = registries::getMessage(messageID);

    if (message == nullptr)
    {
        return -1;
    }

    std::string msg = redfish::registries::fillMessageArgs(messageArgs,
                                                           message->message);
    if (msg.empty())
    {
        return -1;
    }

    // Get the Created time from the timestamp. The log timestamp is in
    // RFC3339 format which matches the Redfish format except for the
    // fractional seconds between the '.' and the '+', so just remove them.
    std::size_t dot = timestamp.find_first_of('.');
    std::size_t plus = timestamp.find_first_of('+', dot);
    if (dot != std::string::npos && plus != std::string::npos)
    {
        timestamp.erase(dot, plus - dot);
    }

    // Fill in the log entry with the gathered data
    logEntryJson["EventId"] = logEntryID;
    logEntryJson["EventType"] = "Event";
    logEntryJson["Severity"] = message->messageSeverity;
    logEntryJson["Message"] = std::move(msg);
    logEntryJson["MessageId"] = messageID;
    logEntryJson["MessageArgs"] = messageArgs;
    logEntryJson["EventTimestamp"] = std::move(timestamp);
    logEntryJson["Context"] = customText;
    return 0;
}

} // namespace event_log

enum redfish_bool
{
    redfishBoolNa, // NOT APPLICABLE
    redfishBoolTrue,
    redfishBoolFalse
};

// Error constants of class Event
static constexpr int redfishInvalidEvent = -1;
static constexpr int redfishInvalidArgs = -2;

void parseAdditionalDataForCPER(nlohmann::json::object_t& entry,
                                const nlohmann::json::object_t& oem,
                                const AdditionalData& additional,
                                std::string& origin);

/*
 * Structure for an event which is based on Event v1.7.0 in "Redfish Schema
 * Supplement(DSP0268)".
 */
class Event
{
  public:
    // required properties
    const std::string messageId;
    // optional properties
    std::vector<std::string> actions = {};
    int64_t eventGroupId = -1;
    std::string eventId = "";
    std::string eventTimestamp = "";
    std::string logEntry = "";
    std::string memberId = "";
    std::vector<std::string> messageArgs = {};
    std::string message = "";
    std::string messageSeverity = "";
    std::string originOfCondition = "";
    nlohmann::json::object_t oem;
    nlohmann::json::object_t cper;
    std::string eventResolution = "";
    std::string logEntryId = "";
    std::string satBMCLogEntryUrl = "";
    redfish_bool specificEventExistsInGroup = redfishBoolNa;

    // derived properties
    std::string registryPrefix;
    std::string resourceType;

  private:
    const registries::Message* registryMsg;
    bool valid;

  public:
    Event(const std::string& messageId) : messageId(messageId)
    {
        registryPrefix = message_registries::getPrefix(messageId);
        registryMsg = redfish::registries::getMessage(messageId);
        if (registryMsg == nullptr)
        {
            BMCWEB_LOG_ERROR("{}", "Message not found in registry with ID: " +
                                       messageId);
            valid = false;
            return;
        }
        valid = true;
        messageSeverity = registryMsg->messageSeverity;
        if (registryMsg->resolution != nullptr)
        {
            eventResolution = registryMsg->resolution;
        }
    }

    bool isValid()
    {
        return valid;
    }

    int setRegistryMsg(const std::vector<std::string>& messageArgs =
                           std::vector<std::string>{})
    {
        if (!valid)
        {
            BMCWEB_LOG_ERROR("Invalid Event instance.");
            return redfishInvalidEvent;
        }
        if (messageArgs.size() != registryMsg->numberOfArgs)
        {
            BMCWEB_LOG_ERROR("Message argument number mismatched.");
            return redfishInvalidArgs;
        }

        int i = 1;
        std::string argStr;

        message = registryMsg->message;
        // Fill the MessageArgs into the Message
        for (const std::string& messageArg : messageArgs)
        {
            argStr = "%" + std::to_string(i++);
            size_t argPos = message.find(argStr);
            if (argPos != std::string::npos)
            {
                message.replace(argPos, argStr.length(), messageArg);
            }
        }
        this->messageArgs = messageArgs;
        return 0;
    }

    int setCustomMsg(const std::string& message,
                     const std::vector<std::string>& messageArgs =
                         std::vector<std::string>{})
    {
        std::string msg = message;
        int i = 1;
        std::string argStr;

        if (!valid)
        {
            BMCWEB_LOG_ERROR("Invalid Event instance.");
            return redfishInvalidEvent;
        }
        // Fill the MessageArgs into the Message
        for (const std::string& messageArg : messageArgs)
        {
            argStr = "%" + std::to_string(i++);
            size_t argPos = msg.find(argStr);
            if (argPos != std::string::npos)
            {
                msg.replace(argPos, argStr.length(), messageArg);
            }
            else
            {
                BMCWEB_LOG_ERROR("Too many MessageArgs.");
                return redfishInvalidArgs;
            }
        }
        argStr = "%" + std::to_string(i);
        if (msg.find(argStr) != std::string::npos)
        {
            BMCWEB_LOG_ERROR("Too few MessageArgs.");
            return redfishInvalidArgs;
        }

        this->message = std::move(msg);
        this->messageArgs = messageArgs;
        return 0;
    }

    /*!
     * @brief   Construct the json format for the event log entry.
     * @param[out] eventLogEntry    The reference to the json event log entry.
     * @return  Return 0 for success. Otherwise, return error codes.
     */
    int formatEventLogEntry(nlohmann::json& eventLogEntry,
                            bool includeOriginOfCondition = true)
    {
        if (!valid)
        {
            BMCWEB_LOG_ERROR("Invalid Event instance.");
            return redfishInvalidEvent;
        }

        eventLogEntry["MessageId"] = messageId;
        eventLogEntry["EventType"] = "Event";
        if (!actions.empty())
        {
            eventLogEntry["Actions"] = actions;
        }
        if (eventGroupId >= 0)
        {
            eventLogEntry["EventGroupId"] = eventGroupId;
        }
        if (!eventId.empty())
        {
            eventLogEntry["EventId"] = eventId;
        }
        if (!eventTimestamp.empty())
        {
            eventLogEntry["EventTimestamp"] = eventTimestamp;
        }
        if (!logEntry.empty())
        {
            eventLogEntry["logEntry"] = nlohmann::json::object();
            eventLogEntry["logEntry"]["@odata.id"] = logEntry;
        }
        if (!memberId.empty())
        {
            eventLogEntry["MemberId"] = memberId;
        }
        if (!messageArgs.empty())
        {
            eventLogEntry["MessageArgs"] = messageArgs;
        }
        if (!message.empty())
        {
            eventLogEntry["Message"] = message;
        }
        if (!messageSeverity.empty())
        {
            eventLogEntry["MessageSeverity"] = messageSeverity;
        }
        if (!oem.empty())
        {
            eventLogEntry.update(oem);
        }
        if (!cper.empty())
        {
            eventLogEntry.update(cper);
        }
        if (!originOfCondition.empty() && includeOriginOfCondition)
        {
            eventLogEntry["OriginOfCondition"] = nlohmann::json::object();
            eventLogEntry["OriginOfCondition"]["@odata.id"] = originOfCondition;
        }
        if (specificEventExistsInGroup != redfishBoolNa)
        {
            eventLogEntry["SpecificEventExistsInGroup"] =
                specificEventExistsInGroup == redfishBoolFalse ? false : true;
        }
        if (!eventResolution.empty())
        {
            eventLogEntry["Resolution"] = eventResolution;
        }
        if (!logEntryId.empty())
        {
            eventLogEntry["LogEntry"] = nlohmann::json::object();
#ifdef BMCWEB_ENABLE_REDFISH_AGGREGATION
            if (!satBMCLogEntryUrl.empty())
            {
                // the URL is from the satellite BMC so URL fixup will be
                // performed.
                addPrefixToStringItem(satBMCLogEntryUrl,
                                      redfishAggregationPrefix);
                eventLogEntry["LogEntry"]["@odata.id"] = satBMCLogEntryUrl;
            }
            else
#endif
            {
                eventLogEntry["LogEntry"]["@odata.id"] =
                    redfish::getLogEntryDataId(logEntryId);
            }
        }
        return 0;
    }
};

class EventUtil
{
  public:
    EventUtil(const EventUtil&) = delete;
    EventUtil& operator=(const EventUtil&) = delete;
    EventUtil(EventUtil&&) = delete;
    EventUtil& operator=(EventUtil&&) = delete;
    ~EventUtil() = default;
    static EventUtil& getInstance()
    {
        static EventUtil handler;
        return handler;
    }
    // This function is used to form event message
    Event createEventPropertyModified(const std::string& arg1,
                                      const std::string& arg2,
                                      const std::string& resourceType)
    {
        Event event(propertyModified);
        std::vector<std::string> messageArgs;
        messageArgs.push_back(arg1);
        messageArgs.push_back(arg2);
        event.setRegistryMsg(messageArgs);

        formBaseEvent(event, resourceType);

        return event;
    }

    // This function is used to form event message
    Event createEventResourceCreated(const std::string& resourceType)
    {
        Event event(resorceCreated);
        formBaseEvent(event, resourceType);
        return event;
    }

    // This function is used to form event message
    Event createEventResourceRemoved(const std::string& resourceType)
    {
        Event event(resourceDeleted);
        formBaseEvent(event, resourceType);
        return event;
    }

    // This function is used to form event message
    Event createEventRebootReason(const std::string& arg,
                                  const std::string& resourceType)
    {
        Event event(rebootReason);

        std::vector<std::string> messageArgs;
        messageArgs.push_back(arg);
        event.setRegistryMsg(messageArgs);
        formBaseEvent(event, resourceType);

        return event;
    }

  private:
    void formBaseEvent(Event& event, const std::string& resourceType)
    {
        // Set message severity
        event.messageSeverity = "Informational";

        // Set message timestamp
        int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
        std::string timestamp = redfish::time_utils::getDateTimeStdtime(
            redfish::time_utils::getTimestamp(static_cast<uint64_t>(ms)));
        event.eventTimestamp = timestamp;

        // Set message resource
        event.resourceType = resourceType;
    }
    // Private constructor for singleton
    EventUtil() = default;
    // CI throwing build errors for using capital letters
    // PROPERTY_MODIFIED,RESORCE_CREATED RESOURCE_DELETED and REBOOT_REASON
    static constexpr const char* propertyModified =
        "Base.1.15.PropertyValueModified";
    static constexpr const char* resorceCreated =
        "ResourceEvent.1.2.ResourceCreated";
    static constexpr const char* resourceDeleted =
        "ResourceEvent.1.2.ResourceRemoved";
    static constexpr const char* rebootReason = "OpenBMC.0.4.BMCRebootReason";
};

inline bool isFilterQuerySpecialChar(char c)
{
    switch (c)
    {
        case '(':
        case ')':
        case '\'':
            return true;
        default:
            return false;
    }
}

inline bool
    readSSEQueryParams(std::string sseFilter, std::string& formatType,
                       std::vector<std::string>& messageIds,
                       std::vector<std::string>& registryPrefixes,
                       std::vector<std::string>& metricReportDefinitions)
{
    auto remove = std::ranges::remove_if(sseFilter, isFilterQuerySpecialChar);
    sseFilter.erase(std::ranges::begin(remove), sseFilter.end());

    std::vector<std::string> result;

    // NOLINTNEXTLINE
    bmcweb::split(result, sseFilter, ' ');

    BMCWEB_LOG_DEBUG("No of tokens in SEE query: {}", result.size());

    constexpr uint8_t divisor = 4;
    constexpr uint8_t minTokenSize = 3;
    if (result.size() % divisor != minTokenSize)
    {
        BMCWEB_LOG_ERROR("Invalid SSE filter specified.");
        return false;
    }

    for (std::size_t i = 0; i < result.size(); i += divisor)
    {
        const std::string& key = result[i];
        const std::string& op = result[i + 1];
        const std::string& value = result[i + 2];

        if ((i + minTokenSize) < result.size())
        {
            const std::string& separator = result[i + minTokenSize];
            // SSE supports only "or" and "and" in query params.
            if ((separator != "or") && (separator != "and"))
            {
                BMCWEB_LOG_ERROR(
                    "Invalid group operator in SSE query parameters");
                return false;
            }
        }

        // SSE supports only "eq" as per spec.
        if (op != "eq")
        {
            BMCWEB_LOG_ERROR(
                "Invalid assignment operator in SSE query parameters");
            return false;
        }

        BMCWEB_LOG_DEBUG("{} : {}", key, value);
        if (key == "EventFormatType")
        {
            formatType = value;
        }
        else if (key == "MessageId")
        {
            messageIds.push_back(value);
        }
        else if (key == "RegistryPrefix")
        {
            registryPrefixes.push_back(value);
        }
        else if (key == "MetricReportDefinition")
        {
            metricReportDefinitions.push_back(value);
        }
        else
        {
            BMCWEB_LOG_ERROR("Invalid property({})in SSE filter query.", key);
            return false;
        }
    }
    return true;
}

class Subscription : public persistent_data::UserSubscription
{
  public:
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&&) = delete;
    Subscription& operator=(Subscription&&) = delete;

    Subscription(const boost::urls::url_view_base& url,
                 boost::asio::io_context& ioc) :
        policy(std::make_shared<crow::ConnectionPolicy>())
    {
        destinationUrl = url;
        client.emplace(ioc, policy);
        // Subscription constructor
        policy->invalidResp = retryRespHandler;
    }

    explicit Subscription(crow::sse_socket::Connection& connIn) :
        sseConn(&connIn)
    {}

    ~Subscription() = default;

    bool sendEvent(std::string&& msg)
    {
        persistent_data::EventServiceConfig eventServiceConfig =
            persistent_data::EventServiceStore::getInstance()
                .getEventServiceConfig();
        if (!eventServiceConfig.enabled)
        {
            return false;
        }

        if (client)
        {
            boost::beast::http::fields httpHeadersCopy(httpHeaders);
            httpHeaders.set(boost::beast::http::field::content_type,
                            "application/json");
            client->sendData(std::move(msg), destinationUrl, httpHeaders,
                             boost::beast::http::verb::post);
            return true;
        }

        if (sseConn != nullptr)
        {
            eventSeqNum++;
            sseConn->sendEvent(std::to_string(eventSeqNum), msg);
        }
        return true;
    }

    bool eventMatchesFilter(const nlohmann::json::object_t& eventMessage,
                            std::string_view resType)
    {
        // If resourceTypes list is empty, assume all
        if (!resourceTypes.empty())
        {
            // Search the resourceTypes list for the subscription.
            auto resourceTypeIndex = std::ranges::find_if(
                resourceTypes, [resType](const std::string& rtEntry) {
                return rtEntry == resType;
            });
            if (resourceTypeIndex == resourceTypes.end())
            {
                BMCWEB_LOG_DEBUG("Not subscribed to this resource");
                return false;
            }
            BMCWEB_LOG_DEBUG("ResourceType {} found in the subscribed list",
                             resType);
        }

        // If registryPrefixes list is empty, don't filter events
        // send everything.
        if (!registryPrefixes.empty())
        {
            auto eventJson = eventMessage.find("MessageId");
            if (eventJson == eventMessage.end())
            {
                return false;
            }

            const std::string* messageId =
                eventJson->second.get_ptr<const std::string*>();
            if (messageId == nullptr)
            {
                BMCWEB_LOG_ERROR("MessageId wasn't a string???");
                return false;
            }

            std::string registry;
            std::string messageKey;
            event_log::getRegistryAndMessageKey(*messageId, registry,
                                                messageKey);

            auto obj = std::ranges::find(registryPrefixes, registry);
            if (obj == registryPrefixes.end())
            {
                return false;
            }
        }

        if (!originResources.empty())
        {
            auto eventJson = eventMessage.find("OriginOfCondition");
            if (eventJson == eventMessage.end())
            {
                return false;
            }

            const std::string* originOfCondition =
                eventJson->second.get_ptr<const std::string*>();
            if (originOfCondition == nullptr)
            {
                BMCWEB_LOG_ERROR("OriginOfCondition wasn't a string???");
                return false;
            }

            auto obj = std::ranges::find(originResources, *originOfCondition);

            if (obj == originResources.end())
            {
                return false;
            }
        }

        // If registryMsgIds list is empty, assume all
        if (!registryMsgIds.empty())
        {
            auto eventJson = eventMessage.find("MessageId");
            if (eventJson == eventMessage.end())
            {
                BMCWEB_LOG_DEBUG("'MessageId' not present");
                return false;
            }

            const std::string* messageId =
                eventJson->second.get_ptr<const std::string*>();
            if (messageId == nullptr)
            {
                BMCWEB_LOG_ERROR("EventType wasn't a string???");
                return false;
            }

            std::string registry;
            std::string messageKey;
            event_log::getRegistryAndMessageKey(*messageId, registry,
                                                messageKey);

            BMCWEB_LOG_DEBUG("extracted registry {}", registry);
            BMCWEB_LOG_DEBUG("extracted message key {}", messageKey);

            auto obj = std::ranges::find(
                registryMsgIds, std::format("{}.{}", registry, messageKey));
            if (obj == registryMsgIds.end())
            {
                BMCWEB_LOG_DEBUG("did not find registry {} in registryMsgIds",
                                 registry);
                BMCWEB_LOG_DEBUG("registryMsgIds has {} entries",
                                 registryMsgIds.size());
                return false;
            }
        }

        if (!originResources.empty())
        {
            auto eventJson = eventMessage.find("OriginOfCondition");
            if (eventJson == eventMessage.end())
            {
                return false;
            }

            const std::string* originOfCondition =
                eventJson->second.get_ptr<const std::string*>();
            if (originOfCondition == nullptr)
            {
                BMCWEB_LOG_ERROR("EventType wasn't a string???");
                return false;
            }

            auto obj = std::ranges::find(originResources, *originOfCondition);

            if (obj == originResources.end())
            {
                return false;
            }
        }

        // If registryMsgIds list is empty, assume all
        if (!registryMsgIds.empty())
        {
            auto eventJson = eventMessage.find("MessageId");
            if (eventJson == eventMessage.end())
            {
                return false;
            }

            const std::string* messageId =
                eventJson->second.get_ptr<const std::string*>();
            if (messageId == nullptr)
            {
                BMCWEB_LOG_ERROR("EventType wasn't a string???");
                return false;
            }

            std::string registry;
            std::string messageKey;
            event_log::getRegistryAndMessageKey(*messageId, registry,
                                                messageKey);

            auto obj = std::ranges::find(
                registryMsgIds, std::format("{}.{}", registry, messageKey));
            if (obj == registryMsgIds.end())
            {
                return false;
            }
        }

        if (filter)
        {
            if (!memberMatches(eventMessage, *filter))
            {
                BMCWEB_LOG_DEBUG("Filter didn't match");
                return false;
            }
        }

        return true;
    }

    bool sendTestEventLog()
    {
        nlohmann::json::array_t logEntryArray;
        nlohmann::json& logEntryJson = logEntryArray.emplace_back();
        uint64_t memberId = 0;

        logEntryJson = {{"EventId", "TestID"},
                        {"EventType", "Event"},
                        {"Severity", "OK"},
                        {"Message", "Generated test event"},
                        {"MessageId", "OpenBMC.0.2.TestEventLog"},
                        {"MemberId", memberId},
                        {"MessageArgs", nlohmann::json::array()},
                        {"EventTimestamp",
                         redfish::time_utils::getDateTimeOffsetNow().first},
                        {"Context", customText}};

        nlohmann::json msg;
        msg["@odata.type"] = "#Event.v1_4_0.Event";
        msg["Id"] = std::to_string(eventSeqNum);
        msg["Name"] = "Event Log";
        msg["Events"] = logEntryArray;

        std::string strMsg = msg.dump(2, ' ', true,
                                      nlohmann::json::error_handler_t::replace);
        return sendEvent(std::move(strMsg));
    }

    void filterAndSendEventLogs(
        const std::vector<EventLogObjectsType>& eventRecords)
    {
        nlohmann::json::array_t logEntryArray;
        for (const EventLogObjectsType& logEntry : eventRecords)
        {
            std::vector<std::string_view> messageArgsView(
                logEntry.messageArgs.begin(), logEntry.messageArgs.end());

            nlohmann::json::object_t bmcLogEntry;
            if (event_log::formatEventLogEntry(
                    logEntry.id, logEntry.messageId, messageArgsView,
                    logEntry.timestamp, customText, bmcLogEntry) != 0)
            {
                BMCWEB_LOG_DEBUG("Read eventLog entry failed");
                continue;
            }

            if (!eventMatchesFilter(bmcLogEntry, ""))
            {
                continue;
            }

            logEntryArray.emplace_back(std::move(bmcLogEntry));
        }

        if (logEntryArray.empty())
        {
            BMCWEB_LOG_DEBUG("No log entries available to be transferred.");
            return;
        }

        nlohmann::json msg;
        msg["@odata.type"] = "#Event.v1_4_0.Event";
        msg["Id"] = std::to_string(eventSeqNum);
        msg["Name"] = "Event Log";
        msg["Events"] = std::move(logEntryArray);
        std::string strMsg = msg.dump(2, ' ', true,
                                      nlohmann::json::error_handler_t::replace);
        sendEvent(std::move(strMsg));
        eventSeqNum++;
    }

    void filterAndSendReports(const std::string& reportId,
                              const telemetry::TimestampReadings& var)
    {
        boost::urls::url mrdUri = boost::urls::format(
            "/redfish/v1/TelemetryService/MetricReportDefinitions/{}",
            reportId);

        // Empty list means no filter. Send everything.
        if (!metricReportDefinitions.empty())
        {
            if (std::ranges::find(metricReportDefinitions, mrdUri.buffer()) ==
                metricReportDefinitions.end())
            {
                return;
            }
        }

        nlohmann::json msg;
        if (!telemetry::fillReport(msg, reportId, var))
        {
            BMCWEB_LOG_ERROR("Failed to fill the MetricReport for DBus "
                             "Report with id {}",
                             reportId);
            return;
        }

        // Context is set by user during Event subscription and it must be
        // set for MetricReport response.
        if (!customText.empty())
        {
            msg["Context"] = customText;
        }

        std::string strMsg = msg.dump(2, ' ', true,
                                      nlohmann::json::error_handler_t::replace);
        sendEvent(std::move(strMsg));
    }

    void updateRetryConfig(uint32_t retryAttempts,
                           uint32_t retryTimeoutInterval)
    {
        if (policy == nullptr)
        {
            BMCWEB_LOG_DEBUG("Retry policy was nullptr, ignoring set");
            return;
        }
        policy->maxRetryAttempts = retryAttempts;
        policy->retryIntervalSecs = std::chrono::seconds(retryTimeoutInterval);
    }

    uint64_t getEventSeqNum() const
    {
        return eventSeqNum;
    }

    void setSubscriptionId(const std::string& id2)
    {
        BMCWEB_LOG_DEBUG("Subscription ID: {}", id2);
        subId = id2;
    }

    std::string getSubscriptionId()
    {
        return subId;
    }

    bool matchSseId(const crow::sse_socket::Connection& thisConn)
    {
        return &thisConn == sseConn;
    }

    // Check used to indicate what response codes are valid as part of our retry
    // policy.  2XX is considered acceptable
    static boost::system::error_code retryRespHandler(unsigned int respCode)
    {
        BMCWEB_LOG_DEBUG(
            "Checking response code validity for SubscriptionEvent");
        if ((respCode < 200) || (respCode >= 300))
        {
            return boost::system::errc::make_error_code(
                boost::system::errc::result_out_of_range);
        }

        // Return 0 if the response code is valid
        return boost::system::errc::make_error_code(
            boost::system::errc::success);
    }

  private:
    std::string subId;
    uint64_t eventSeqNum = 1;
    boost::urls::url host;
    std::shared_ptr<crow::ConnectionPolicy> policy;
    crow::sse_socket::Connection* sseConn = nullptr;

    std::optional<crow::HttpClient> client;

  public:
    std::optional<filter_ast::LogicalAnd> filter;
};

class EventServiceManager
{
  private:
    bool serviceEnabled = false;
    uint32_t retryAttempts = 0;
    uint32_t retryTimeoutInterval = 0;

    std::streampos redfishLogFilePosition{0};
    size_t noOfEventLogSubscribers{0};
    size_t noOfMetricReportSubscribers{0};
    std::shared_ptr<sdbusplus::bus::match_t> matchTelemetryMonitor;
    std::shared_ptr<sdbusplus::bus::match_t> matchDbusLogging;
    boost::container::flat_map<std::string, std::shared_ptr<Subscription>>
        subscriptionsMap;

    uint64_t eventId{1};

    struct Event2
    {
        std::string id;
        nlohmann::json message;
    };

    constexpr static size_t maxMessages = 200;
    boost::circular_buffer<Event2> messages{maxMessages};

    boost::asio::io_context& ioc;

  public:
    EventServiceManager(const EventServiceManager&) = delete;
    EventServiceManager& operator=(const EventServiceManager&) = delete;
    EventServiceManager(EventServiceManager&&) = delete;
    EventServiceManager& operator=(EventServiceManager&&) = delete;
    ~EventServiceManager() = default;

    explicit EventServiceManager(boost::asio::io_context& iocIn) : ioc(iocIn)
    {
        // Load config from persist store.
        initConfig();
    }

    static EventServiceManager&
        getInstance(boost::asio::io_context* ioc = nullptr)
    {
        static EventServiceManager handler(*ioc);
        return handler;
    }

    void initConfig()
    {
        loadOldBehavior();

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
                std::make_shared<Subscription>(*url, ioc);

            subValue->id = newSub->id;
            subValue->destinationUrl = newSub->destinationUrl;
            subValue->protocol = newSub->protocol;
            subValue->retryPolicy = newSub->retryPolicy;
            subValue->customText = newSub->customText;
            subValue->eventFormatType = newSub->eventFormatType;
            subValue->subscriptionType = newSub->subscriptionType;
            subValue->registryMsgIds = newSub->registryMsgIds;
            subValue->registryPrefixes = newSub->registryPrefixes;
            subValue->resourceTypes = newSub->resourceTypes;
            subValue->httpHeaders = newSub->httpHeaders;
            subValue->metricReportDefinitions = newSub->metricReportDefinitions;
            subValue->originResources = newSub->originResources;
            subValue->includeOriginOfCondition =
                newSub->includeOriginOfCondition;

            if (subValue->id.empty())
            {
                BMCWEB_LOG_ERROR("Failed to add subscription");
            }
            subscriptionsMap.insert(std::pair(subValue->id, subValue));

            updateNoOfSubscribersCount();

            if constexpr (!BMCWEB_REDFISH_DBUS_LOG)
            {
                cacheRedfishLogFile();
            }

            // Update retry configuration.
            subValue->updateRetryConfig(retryAttempts, retryTimeoutInterval);
        }
#ifdef BMCWEB_ENABLE_REDFISH_AGGREGATION
        redfish::subscribeSatBmc::getInstance().createSubscribeTimer();

        if (getNumberOfSubscriptions() > 0)
        {
            // start RF event listener and subscribe HMC eventService.
            initRedfishEventListener(ioc);
        }
#endif

#ifdef BMCWEB_ENABLE_REDFISH_DBUS_EVENT_PUSH
        registerDbusLoggingSignal();
#endif
    }

    static void loadOldBehavior()
    {
        std::ifstream eventConfigFile(eventServiceFile);
        if (!eventConfigFile.good())
        {
            BMCWEB_LOG_DEBUG("Old eventService config not exist");
            return;
        }
        auto jsonData = nlohmann::json::parse(eventConfigFile, nullptr, false);
        if (jsonData.is_discarded())
        {
            BMCWEB_LOG_ERROR("Old eventService config parse error.");
            return;
        }

        const nlohmann::json::object_t* obj =
            jsonData.get_ptr<const nlohmann::json::object_t*>();
        for (const auto& item : *obj)
        {
            if (item.first == "Configuration")
            {
                persistent_data::EventServiceStore::getInstance()
                    .getEventServiceConfig()
                    .fromJson(item.second);
            }
            else if (item.first == "Subscriptions")
            {
                for (const auto& elem : item.second)
                {
                    std::shared_ptr<persistent_data::UserSubscription>
                        newSubscription =
                            persistent_data::UserSubscription::fromJson(elem,
                                                                        true);
                    if (newSubscription == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Problem reading subscription "
                                         "from old persistent store");
                        continue;
                    }

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
                        newSubscription->id = id;
                        auto inserted =
                            persistent_data::EventServiceStore::getInstance()
                                .subscriptionsConfigMap.insert(
                                    std::pair(id, newSubscription));
                        if (inserted.second)
                        {
                            break;
                        }
                        --retry;
                    }

                    if (retry <= 0)
                    {
                        BMCWEB_LOG_ERROR(
                            "Failed to generate random number from old "
                            "persistent store");
                        continue;
                    }
                }
            }

            persistent_data::getConfig().writeData();
            std::error_code ec;
            std::filesystem::remove(eventServiceFile, ec);
            if (ec)
            {
                BMCWEB_LOG_DEBUG(
                    "Failed to remove old event service file.  Ignoring");
            }
            else
            {
                BMCWEB_LOG_DEBUG("Remove old eventservice config");
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
#ifdef BMCWEB_ENABLE_REDFISH_DBUS_EVENT_PUSH
    void setEventServiceConfig(const persistent_data::EventServiceConfig& cfg,
                               const std::string_view url)
#else
    void setEventServiceConfig(const persistent_data::EventServiceConfig& cfg)
#endif
    {
        bool updateConfig = false;
        bool updateRetryCfg = false;

        if (serviceEnabled != cfg.enabled)
        {
            serviceEnabled = cfg.enabled;
#ifdef BMCWEB_ENABLE_REDFISH_DBUS_EVENT_PUSH
            // Send an event for session creation
            Event event =
                redfish::EventUtil::getInstance().createEventPropertyModified(
                    "ServiceEnabled", std::to_string(serviceEnabled),
                    "EventService");
            redfish::EventServiceManager::getInstance().sendEventWithOOC(
                std::string(url), event);
#endif
            if (serviceEnabled && noOfMetricReportSubscribers != 0U)
            {
                registerMetricReportSignal();
            }
            else
            {
                unregisterMetricReportSignal();
            }

#ifdef BMCWEB_ENABLE_REDFISH_DBUS_EVENT_PUSH
            if (serviceEnabled)
            {
                registerDbusLoggingSignal();
            }
            else
            {
                unregisterDbusLoggingSignal();
            }
#endif
            updateConfig = true;
        }

        if (retryAttempts != cfg.retryAttempts)
        {
            retryAttempts = cfg.retryAttempts;
            updateConfig = true;
            updateRetryCfg = true;
#ifdef BMCWEB_ENABLE_REDFISH_DBUS_EVENT_PUSH
            // Send an event for property change
            Event event =
                redfish::EventUtil::getInstance().createEventPropertyModified(
                    "DeliveryRetryAttempts", std::to_string(retryAttempts),
                    "EventService");
            redfish::EventServiceManager::getInstance().sendEventWithOOC(
                std::string(url), event);
#endif
        }

        if (retryTimeoutInterval != cfg.retryTimeoutInterval)
        {
            retryTimeoutInterval = cfg.retryTimeoutInterval;
            updateConfig = true;
            updateRetryCfg = true;
#ifdef BMCWEB_ENABLE_REDFISH_DBUS_EVENT_PUSH
            // Send an event for property change
            Event event =
                redfish::EventUtil::getInstance().createEventPropertyModified(
                    "DeliveryRetryIntervalSeconds",
                    std::to_string(retryTimeoutInterval), "EventService");
            redfish::EventServiceManager::getInstance().sendEventWithOOC(
                std::string(url), event);
#endif
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
            if (entry->eventFormatType == eventFormatType)
            {
                eventLogSubCount++;
            }
            else if (entry->eventFormatType == metricReportFormatType)
            {
                metricReportSubCount++;
            }
        }

        noOfEventLogSubscribers = eventLogSubCount;
        if (noOfMetricReportSubscribers != metricReportSubCount)
        {
            noOfMetricReportSubscribers = metricReportSubCount;
            if (noOfMetricReportSubscribers != 0U)
            {
                registerMetricReportSignal();
            }
            else
            {
                unregisterMetricReportSignal();
            }
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

    std::string
        addSubscriptionInternal(const std::shared_ptr<Subscription>& subValue)
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

        std::shared_ptr<persistent_data::UserSubscription> newSub =
            std::make_shared<persistent_data::UserSubscription>();
        newSub->id = id;
        newSub->destinationUrl = subValue->destinationUrl;
        newSub->protocol = subValue->protocol;
        newSub->retryPolicy = subValue->retryPolicy;
        newSub->customText = subValue->customText;
        newSub->eventFormatType = subValue->eventFormatType;
        newSub->subscriptionType = subValue->subscriptionType;
        newSub->registryMsgIds = subValue->registryMsgIds;
        newSub->registryPrefixes = subValue->registryPrefixes;
        newSub->resourceTypes = subValue->resourceTypes;
        newSub->httpHeaders = subValue->httpHeaders;
        newSub->metricReportDefinitions = subValue->metricReportDefinitions;
        newSub->originResources = subValue->originResources;
        newSub->includeOriginOfCondition = subValue->includeOriginOfCondition;

        persistent_data::EventServiceStore::getInstance()
            .subscriptionsConfigMap.emplace(newSub->id, newSub);

        updateNoOfSubscribersCount();
#ifndef BMCWEB_ENABLE_REDFISH_DBUS_LOG_ENTRIES
        {
            if (redfishLogFilePosition != 0)
            {
                cacheRedfishLogFile();
            }
        }
#endif

        // Update retry configuration.
        subValue->updateRetryConfig(retryAttempts, retryTimeoutInterval);

        // Set Subscription ID for back trace
        subValue->setSubscriptionId(id);

        return id;
    }

    std::string
        addSSESubscription(const std::shared_ptr<Subscription>& subValue,
                           std::string_view lastEventId)
    {
        std::string id = addSubscriptionInternal(subValue);

        if (!lastEventId.empty())
        {
            BMCWEB_LOG_INFO("Attempting to find message for last id {}",
                            lastEventId);
            boost::circular_buffer<Event2>::iterator lastEvent =
                std::find_if(messages.begin(), messages.end(),
                             [&lastEventId](const Event2& event) {
                return event.id == lastEventId;
            });
            // Can't find a matching ID
            if (lastEvent == messages.end())
            {
                nlohmann::json msg = messages::eventBufferExceeded();
                // If the buffer overloaded, send all messages.
                std::string strMsg = msg.dump(
                    2, ' ', false, nlohmann::json::error_handler_t::replace);
                subValue->sendEvent(std::move(strMsg));
                lastEvent = messages.begin();
            }
            else
            {
                // Skip the last event the user already has
                lastEvent++;
            }

            for (boost::circular_buffer<Event2>::const_iterator event =
                     lastEvent;
                 event != messages.end(); event++)
            {
                std::string strMsg = event->message.dump(
                    2, ' ', false, nlohmann::json::error_handler_t::replace);
                subValue->sendEvent(std::move(strMsg));
            }
        }
        return id;
    }

    std::string
        addPushSubscription(const std::shared_ptr<Subscription>& subValue)
    {
        std::string id = addSubscriptionInternal(subValue);

        updateSubscriptionData();
        return id;
    }

    bool isSubscriptionExist(const std::string& id)
    {
        auto obj = subscriptionsMap.find(id);
        return obj != subscriptionsMap.end();
    }

    void deleteSubscription(const std::string& id)
    {
        auto obj = subscriptionsMap.find(id);
        if (obj != subscriptionsMap.end())
        {
            subscriptionsMap.erase(obj);
            auto obj2 = persistent_data::EventServiceStore::getInstance()
                            .subscriptionsConfigMap.find(id);
            persistent_data::EventServiceStore::getInstance()
                .subscriptionsConfigMap.erase(obj2);
            updateNoOfSubscribersCount();
            updateSubscriptionData();
        }
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
                    .subscriptionsConfigMap.erase(
                        it->second->getSubscriptionId());
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
            return (entry.second->subscriptionType == subscriptionTypeSSE);
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

    bool sendTestEventLog()
    {
        for (const auto& it : subscriptionsMap)
        {
            std::shared_ptr<Subscription> entry = it.second;
            if (!entry->sendTestEventLog())
            {
                return false;
            }
        }
        return true;
    }

    void sendEvent(nlohmann::json::object_t eventMessage,
                   const std::string& origin, const std::string& resourceType)
    {
        eventMessage["EventId"] = eventId;

        eventMessage["EventTimestamp"] =
            redfish::time_utils::getDateTimeOffsetNow().first;
        eventMessage["OriginOfCondition"] = origin;

        // MemberId is 0 : since we are sending one event record.
        eventMessage["MemberId"] = 0;

        messages.push_back(Event2(std::to_string(eventId), eventMessage));

        for (auto& it : subscriptionsMap)
        {
            std::shared_ptr<Subscription>& entry = it.second;
            if (!entry->eventMatchesFilter(eventMessage, resourceType))
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
            entry->sendEvent(std::move(strMsg));
            eventId++; // increment the eventId
        }
    }

    void sendBroadcastMsg(const std::string& broadcastMsg)
    {
        for (const auto& it : this->subscriptionsMap)
        {
            std::shared_ptr<Subscription> entry = it.second;
            nlohmann::json msgJson = {
                {"Timestamp",
                 redfish::time_utils::getDateTimeOffsetNow().first},
                {"OriginOfCondition", "/ibm/v1/HMC/BroadcastService"},
                {"Name", "Broadcast Message"},
                {"Message", broadcastMsg}};

            std::string strMsg = msgJson.dump(
                2, ' ', true, nlohmann::json::error_handler_t::replace);
            entry->sendEvent(std::move(strMsg));
        }
    }

    /*!
     * @brief   Send the event to all subscribers.
     * @param[in] event   The event to be sent.
     * @return  Void
     */
    void sendEvent(Event& event)
    {
        nlohmann::json logEntry;
        if (event.formatEventLogEntry(logEntry) != 0)
        {
            BMCWEB_LOG_ERROR("Failed to format the event log entry");
        }
        nlohmann::json eventsArray = nlohmann::json::array();
        eventsArray.push_back(logEntry);
        nlohmann::json::object_t msg;
        msg["@odata.type"] = "#Event.v1_9_0.Event";
        msg["Id"] = std::to_string(eventId);
        msg["Name"] = "Event Log";
        msg["Events"] = eventsArray;
        messages.push_back(Event2(std::to_string(eventId), msg));
        for (const auto& it : this->subscriptionsMap)
        {
            std::shared_ptr<Subscription> entry = it.second;
            if (!entry->eventMatchesFilter(logEntry, "Event"))
            {
                BMCWEB_LOG_DEBUG("Filter didn't match");
                continue;
            }
            std::string strMsg = nlohmann::json(msg).dump(
                2, ' ', true, nlohmann::json::error_handler_t::replace);
            entry->sendEvent(std::move(strMsg));
        }
        eventId++; // increament the eventId
    }

    void resetRedfishFilePosition()
    {
        // Control would be here when Redfish file is created.
        // Reset File Position as new file is created
        redfishLogFilePosition = 0;
    }

    void cacheRedfishLogFile()
    {
        // Open the redfish file and read till the last record.

        std::ifstream logStream(redfishEventLogFile);
        if (!logStream.good())
        {
            BMCWEB_LOG_ERROR(" Redfish log file open failed ");
            return;
        }
        std::string logEntry;
        while (std::getline(logStream, logEntry))
        {
            redfishLogFilePosition = logStream.tellg();
        }
    }

    void readEventLogsFromFile()
    {
        std::ifstream logStream(redfishEventLogFile);
        if (!logStream.good())
        {
            BMCWEB_LOG_ERROR(" Redfish log file open failed");
            return;
        }

        std::vector<EventLogObjectsType> eventRecords;

        std::string logEntry;

        // Get the read pointer to the next log to be read.
        logStream.seekg(redfishLogFilePosition);

        while (std::getline(logStream, logEntry))
        {
            // Update Pointer position
            redfishLogFilePosition = logStream.tellg();

            std::string idStr;
            if (!event_log::getUniqueEntryID(logEntry, idStr))
            {
                continue;
            }

            if (!serviceEnabled || noOfEventLogSubscribers == 0)
            {
                // If Service is not enabled, no need to compute
                // the remaining items below.
                // But, Loop must continue to keep track of Timestamp
                continue;
            }

            std::string timestamp;
            std::string messageID;
            std::vector<std::string> messageArgs;
            if (event_log::getEventLogParams(logEntry, timestamp, messageID,
                                             messageArgs) != 0)
            {
                BMCWEB_LOG_DEBUG("Read eventLog entry params failed");
                continue;
            }

            eventRecords.emplace_back(idStr, timestamp, messageID, messageArgs);
        }

        if (!serviceEnabled || noOfEventLogSubscribers == 0)
        {
            BMCWEB_LOG_DEBUG("EventService disabled or no Subscriptions.");
            return;
        }

        if (eventRecords.empty())
        {
            // No Records to send
            BMCWEB_LOG_DEBUG("No log entries available to be transferred.");
            return;
        }

        for (const auto& it : subscriptionsMap)
        {
            std::shared_ptr<Subscription> entry = it.second;
            if (entry->eventFormatType == "Event")
            {
                entry->filterAndSendEventLogs(eventRecords);
            }
        }
    }

    static void watchRedfishEventLogFile()
    {
        if (!inotifyConn)
        {
            return;
        }

        static std::array<char, 1024> readBuffer;

        inotifyConn->async_read_some(boost::asio::buffer(readBuffer),
                                     [&](const boost::system::error_code& ec,
                                         const std::size_t& bytesTransferred) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Callback Error: {}", ec.message());
                return;
            }
            std::size_t index = 0;
            while ((index + iEventSize) <= bytesTransferred)
            {
                struct inotify_event event
                {};
                std::memcpy(&event, &readBuffer[index], iEventSize);
                if (event.wd == dirWatchDesc)
                {
                    if ((event.len == 0) ||
                        (index + iEventSize + event.len > bytesTransferred))
                    {
                        index += (iEventSize + event.len);
                        continue;
                    }

                    std::string fileName(&readBuffer[index + iEventSize]);
                    if (fileName != "redfish")
                    {
                        index += (iEventSize + event.len);
                        continue;
                    }

                    BMCWEB_LOG_DEBUG(
                        "Redfish log file created/deleted. event.name: {}",
                        fileName);
                    if (event.mask == IN_CREATE)
                    {
                        if (fileWatchDesc != -1)
                        {
                            BMCWEB_LOG_DEBUG(
                                "Remove and Add inotify watcher on "
                                "redfish event log file");
                            // Remove existing inotify watcher and add
                            // with new redfish event log file.
                            inotify_rm_watch(inotifyFd, fileWatchDesc);
                            fileWatchDesc = -1;
                        }

                        fileWatchDesc = inotify_add_watch(
                            inotifyFd, redfishEventLogFile, IN_MODIFY);
                        if (fileWatchDesc == -1)
                        {
                            BMCWEB_LOG_ERROR("inotify_add_watch failed for "
                                             "redfish log file.");
                            return;
                        }

                        EventServiceManager::getInstance()
                            .resetRedfishFilePosition();
                        EventServiceManager::getInstance()
                            .readEventLogsFromFile();
                    }
                    else if ((event.mask == IN_DELETE) ||
                             (event.mask == IN_MOVED_TO))
                    {
                        if (fileWatchDesc != -1)
                        {
                            inotify_rm_watch(inotifyFd, fileWatchDesc);
                            fileWatchDesc = -1;
                        }
                    }
                }
                else if (event.wd == fileWatchDesc)
                {
                    if (event.mask == IN_MODIFY)
                    {
                        EventServiceManager::getInstance()
                            .readEventLogsFromFile();
                    }
                }
                index += (iEventSize + event.len);
            }

            watchRedfishEventLogFile();
        });
    }

    static int startEventLogMonitor(boost::asio::io_context& ioc)
    {
        inotifyConn.emplace(ioc);
        inotifyFd = inotify_init1(IN_NONBLOCK);
        if (inotifyFd == -1)
        {
            BMCWEB_LOG_ERROR("inotify_init1 failed.");
            return -1;
        }

        // Add watch on directory to handle redfish event log file
        // create/delete.
        dirWatchDesc = inotify_add_watch(inotifyFd, redfishEventLogDir,
                                         IN_CREATE | IN_MOVED_TO | IN_DELETE);
        if (dirWatchDesc == -1)
        {
            BMCWEB_LOG_ERROR(
                "inotify_add_watch failed for event log directory.");
            return -1;
        }

        // Watch redfish event log file for modifications.
        fileWatchDesc = inotify_add_watch(inotifyFd, redfishEventLogFile,
                                          IN_MODIFY);
        if (fileWatchDesc == -1)
        {
            BMCWEB_LOG_ERROR("inotify_add_watch failed for redfish log file.");
            // Don't return error if file not exist.
            // Watch on directory will handle create/delete of file.
        }

        // monitor redfish event log file
        inotifyConn->assign(inotifyFd);
        watchRedfishEventLogFile();

        return 0;
    }

    static void getReadingsForReport(sdbusplus::message_t& msg)
    {
        if (msg.is_method_error())
        {
            BMCWEB_LOG_ERROR("TelemetryMonitor Signal error");
            return;
        }

        sdbusplus::message::object_path path(msg.get_path());
        std::string id = path.filename();
        if (id.empty())
        {
            BMCWEB_LOG_ERROR("Failed to get Id from path");
            return;
        }

        std::string interface;
        dbus::utility::DBusPropertiesMap props;
        std::vector<std::string> invalidProps;
        msg.read(interface, props, invalidProps);

        auto found = std::ranges::find_if(
            props, [](const auto& x) { return x.first == "Readings"; });
        if (found == props.end())
        {
            BMCWEB_LOG_INFO("Failed to get Readings from Report properties");
            return;
        }

        const telemetry::TimestampReadings* readings =
            std::get_if<telemetry::TimestampReadings>(&found->second);
        if (readings == nullptr)
        {
            BMCWEB_LOG_INFO("Failed to get Readings from Report properties");
            return;
        }

        for (const auto& it :
             EventServiceManager::getInstance().subscriptionsMap)
        {
            Subscription& entry = *it.second;
            if (entry.eventFormatType == metricReportFormatType)
            {
                entry.filterAndSendReports(id, *readings);
            }
        }
    }

    void unregisterMetricReportSignal()
    {
        if (matchTelemetryMonitor)
        {
            BMCWEB_LOG_DEBUG("Metrics report signal - Unregister");
            matchTelemetryMonitor.reset();
            matchTelemetryMonitor = nullptr;
        }
    }

    void registerMetricReportSignal()
    {
        if (!serviceEnabled || matchTelemetryMonitor)
        {
            BMCWEB_LOG_DEBUG("Not registering metric report signal.");
            return;
        }

        BMCWEB_LOG_DEBUG("Metrics report signal - Register");
        std::string matchStr = "type='signal',member='PropertiesChanged',"
                               "interface='org.freedesktop.DBus.Properties',"
                               "arg0=xyz.openbmc_project.Telemetry.Report";

        matchTelemetryMonitor = std::make_shared<sdbusplus::bus::match_t>(
            *crow::connections::systemBus, matchStr,
            [](sdbusplus::message::message& msg) {
            if (msg.is_method_error())
            {
                BMCWEB_LOG_ERROR("TelemetryMonitor Signal error");
                return;
            }

            getReadingsForReport(msg);
        });
    }

#ifdef BMCWEB_ENABLE_REDFISH_DBUS_EVENT_PUSH
    const std::string inventorySubTree = "/xyz/openbmc_project/inventory";
    const std::string sensorSubTree = "/xyz/openbmc_project/sensors";
    const std::string chassisPrefixDbus =
        "/xyz/openbmc_project/inventory/system/chassis/";
    const std::string chassisPrefix = "/redfish/v1/Chassis/";
    const std::string fabricsPrefixDbus =
        "/xyz/openbmc_project/inventory/system/fabrics/";
    const std::string fabricsPrefix = "/redfish/v1/Fabrics/";
    const std::string memoryPrefixDbus =
        "/xyz/openbmc_project/inventory/system/memory/";
    const std::string memoryPrefix =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Memory/";
    const std::string processorPrefixDbus =
        "/xyz/openbmc_project/inventory/system/processors/";
    const std::string processorPrefix =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Processors/";
    const std::string softwarePrefixDbus = "/xyz/openbmc_project/software/";
    const std::string firmwarePrefix =
        "/redfish/v1/UpdateService/FirmwareInventory/";
    const std::string userPrefixDbus = "/xyz/openbmc_project/user/";
    const std::string userPrefix = "/redfish/v1/AccountService/Accounts/";
    const std::string accountPolicyPrefixDbus = "/xyz/openbmc_project/user";
    const std::string accountPolicyPrefix = "/redfish/v1/AccountService";
    const std::string virtualMediaLegacyUSB1PrefixDbus =
        "/xyz/openbmc_project/VirtualMedia/Legacy/USB1";
    const std::string virtualMediaUSB1Prefix =
        "/redfish/v1/Managers/" + std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
        "/VirtualMedia/USB1/Actions/VirtualMedia.";
    const std::string virtualMediaLegacyUSB2PrefixDbus =
        "/xyz/openbmc_project/VirtualMedia/Legacy/USB2";
    const std::string virtualMediaUSB2Prefix =
        "/redfish/v1/Managers/" + std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
        "/VirtualMedia/USB2/Actions/VirtualMedia.";
    const std::string sessionServiceServicePrefix = "/redfish/v1/";
    const std::string networkPrefixDbus = "/xyz/openbmc_project/network/";
    const std::string networkPrefix =
        "/redfish/v1/Managers/" + std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
        "/EthernetInterfaces/";
    const std::string ldapCertificateDbusPrefix =
        "/xyz/openbmc_project/certs/client/ldap/";
    const std::string ldapCertificatePrefix =
        "/redfish/v1/AccountService/LDAP/Certificates/";
    const std::string authorityCertificateDbusPrefix =
        "/xyz/openbmc_project/certs/authority/ldap/";
    const std::string authorityCertificatePrefix =
        "/redfish/v1/Managers/" + std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
        "/Truststore/Certificates/";
    const std::string httpsCertificateDbusPrefix =
        "/xyz/openbmc_project/certs/server/https/";
    const std::string httpsCertificatePrefix =
        "/redfish/v1/Managers/" + std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
        "/NetworkProtocol/HTTPS/Certificates/";
    const std::string updateServiceDbusPrefix =
        "/xyz/openbmc_project/software/";
    const std::string updateServicePrefix = "/redfish/v1/UpdateService/";
    const std::string managerResetDbusPrefix =
        "/xyz/openbmc_project/state/bmc0/";
    const std::string managerResetPrefix =
        "/redfish/v1/Managers/" + std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
        "/Actions/";
    const std::string ledGroupsDbusPrefix =
        "/xyz/openbmc_project/led/groups/enclosure_identify";
    const std::string ledPrefix = "/redfish/v1/Systems/" +
                                  std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME);
    const std::string biosPwdPathDbusPrefix =
        "/xyz/openbmc_project/bios_config/password";
    const std::string biosPwdPrefix =
        std::format("/redfish/v1/Systems/{}/Bios/Actions/Bios.ChangePassword",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME);
    const std::string biosConfigDbusPrefix =
        "/xyz/openbmc_project/bios_config/manager";
    const std::string biosConfigPrefix = std::format(
        "/redfish/v1/Systems/{}/secureboot", BMCWEB_REDFISH_SYSTEM_URI_NAME);
    const std::string biosSettingsDbusPrefix =
        "/xyz/openbmc_project/bios_config/manager/bios/settings";
    const std::string biosSettingsPrefix =
        std::format("/redfish/v1/Systems/{}/Bios/Actions/Bios.ResetBios",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME);
    const std::string chassisResetDbusPrefix =
        "/xyz/openbmc_project/state/host0";
    const std::string chassisResetPrefix =
        std::format("/redfish/v1/Chassis/{}/Actions/Chassis.Reset",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME);
    /**
     *  @brief Table used to find OriginOfCondition
     */
    std::unordered_map<std::string, std::string> dBusToRedfishURI = {
        {chassisPrefixDbus, chassisPrefix},
        {fabricsPrefixDbus, fabricsPrefix},
        {processorPrefixDbus, processorPrefix},
        {memoryPrefixDbus, memoryPrefix},
        {softwarePrefixDbus, firmwarePrefix},
        {sensorSubTree, chassisPrefix},
        {userPrefixDbus, userPrefix},
        {virtualMediaLegacyUSB1PrefixDbus, virtualMediaUSB1Prefix},
        {virtualMediaLegacyUSB2PrefixDbus, virtualMediaUSB2Prefix},
        {accountPolicyPrefixDbus, accountPolicyPrefix},
        {networkPrefixDbus, networkPrefix},
        {ldapCertificateDbusPrefix, ldapCertificatePrefix},
        {authorityCertificateDbusPrefix, authorityCertificatePrefix},
        {httpsCertificateDbusPrefix, httpsCertificatePrefix},
        {updateServiceDbusPrefix, updateServicePrefix},
        {managerResetDbusPrefix, managerResetPrefix},
        {ledGroupsDbusPrefix, ledPrefix},
        {biosSettingsDbusPrefix, biosSettingsPrefix},
        {biosPwdPathDbusPrefix, biosPwdPrefix},
        {chassisResetDbusPrefix, chassisResetPrefix}};

    const std::string minpasswordLengthDbus = "MinPasswordLength";
    const std::string minpasswordLength = "MinPasswordLength";
    const std::string accountUnlockTimeoutDbus = "AccountUnlockTimeout";
    const std::string accountLockoutDuration = "AccountLockoutDuration";
    const std::string maxLoginAttemptBeforeLockoutDbus =
        "MaxLoginAttemptBeforeLockout";
    const std::string maxLoginAttemptBeforeLockout =
        "MaxLoginAttemptBeforeLockout";
    const std::string userEnabledDbus = "UserEnabled";
    const std::string userEnabled = "UserEnabled";
    const std::string userLockedForFailedAttemptDbus =
        "UserLockedForFailedAttempt";
    const std::string locked = "Locked";
    const std::string userPrivilegeDbus = "UserPrivilege";
    const std::string roleid = "RoleId";
    const std::string ldapbindDNPasswordDbus = "LDAPBindDNPassword";
    const std::string password = "Password";
    const std::string ldapBindDNDbus = "LDAPBindDN";
    const std::string usernameDbus = "UserName";
    const std::string username = "UserName";
    const std::string ldapServerURIDbus = "LDAPServerURI";
    const std::string serviceAddresses = "ServiceAddresses";
    const std::string enabledDbus = "Enabled";
    const std::string srvcEnabled = "ServiceEnabled";
    const std::string ldapBaseDNDbus = "LDAPBaseDN";
    const std::string baseDistinguishedNames = "BaseDistinguishedNames";
    const std::string groupNameAttributeDbus = "GroupNameAttribute";
    const std::string groupsAttribute = "GroupsAttribute";
    const std::string userNameAttributeDbus = "UserNameAttribute";
    const std::string userNameAttribute = "UsernameAttribute";
    const std::string privilageDbus = "Privilege";
    const std::string localRole = "LocalRole";
    const std::string groupNameDbus = "GroupName";
    const std::string remoteGroup = "RemoteGroup";
    const std::string modulePowercapDbus = "ModulePowerCap";
    const std::string setpoint = "SetPoint";
    const std::string nicEnabledDbus = "NICEnabled";
    const std::string vlanEnable = "VLANEnable";
    const std::string dhcbEnableDbus = "DHCPEnabled";
    const std::string dhcbEnabled = "DHCPEnabled";
    const std::string secureBootEnableDbus = "SecureBootEnable";
    const std::string secureBootEnable = "SecureBootEnable";
    const std::string secureBootModeDbus = "SecureBootMode";
    const std::string secureBootMode = "SecureBootMode";
    const std::string secureCurrentBootDbus = "ScureCurrentBoot";
    const std::string secureCurrentBoot = "ScureCurrentBoot";
    const std::string resetBIOSSettingsDbus = "ResetBIOSSettings";
    const std::string resetBIOSSettings = "ResetBIOSSettings";
    const std::string biosPassowrdDbus = "BIOSPassword";
    const std::string biosPassword = "NewPassword";
    const std::string hostPowerStateDbus = "RequestedHostTransition";
    const std::string hostPowerState = "ResetType";
    /**
     * @brief Map Dbus Property to Redfish Property
     */
    std::unordered_map<std::string, std::string> dBusToRedfishProperty = {
        {minpasswordLengthDbus, minpasswordLength},
        {accountUnlockTimeoutDbus, accountLockoutDuration},
        {maxLoginAttemptBeforeLockoutDbus, maxLoginAttemptBeforeLockout},
        {userEnabledDbus, userEnabled},
        {userLockedForFailedAttemptDbus, locked},
        {userPrivilegeDbus, roleid},
        {ldapbindDNPasswordDbus, password},
        {ldapBindDNDbus, username},
        {ldapServerURIDbus, serviceAddresses},
        {enabledDbus, srvcEnabled},
        {ldapBaseDNDbus, baseDistinguishedNames},
        {usernameDbus, username},
        {groupNameAttributeDbus, groupsAttribute},
        {userNameAttributeDbus, userNameAttribute},
        {privilageDbus, localRole},
        {groupNameDbus, remoteGroup},
        {modulePowercapDbus, setpoint},
        {nicEnabledDbus, vlanEnable},
        {dhcbEnableDbus, dhcbEnabled},
        {secureBootEnableDbus, secureBootEnable},
        {secureBootModeDbus, secureBootMode},
        {resetBIOSSettingsDbus, resetBIOSSettings},
        {biosPassowrdDbus, biosPassword},
        {secureCurrentBootDbus, secureCurrentBoot},
        {hostPowerStateDbus, hostPowerState}};

    const std::string certificateDbusPrefix = "/xyz/openbmc_project/certs";
    const std::string systemsDbusPrefix =
        "/xyz/openbmc_project/inventory/system";
    const std::string accountServiceDbusPrefix = "/xyz/openbmc_project/user";
    const std::string managerAccountDbusPrefix = "/xyz/openbmc_project/user/";
    const std::string virtualMediaDbusPrefix =
        "/xyz/openbmc_project/VirtualMedia";

    struct CompareKeys
    {
        bool operator()(const std::string& a, const std::string& b) const
        {
            // Using std::greater to sort in descending order
            return std::greater<std::string>()(a, b);
        }
    };
    /**
     * @brief Map dbuspath  to resourceType
     */
    std::map<std::string, std::string, CompareKeys> dBusToResourceType = {
        {certificateDbusPrefix, "CertificateService"},
        {systemsDbusPrefix, "Systems"},
        {accountServiceDbusPrefix, "AccountService"},
        {managerAccountDbusPrefix, "ManagerAccount"},
        {virtualMediaDbusPrefix, "VirtualMedia"}};

    void unregisterDbusLoggingSignal()
    {
        if (matchDbusLogging)
        {
            BMCWEB_LOG_DEBUG("Dbus logging signal - Unregister.");
            matchDbusLogging.reset();
        }
    }

    /**
     * Populates event with origin of condition
     * then sends the event for Redfish Event Listener
     * to pick up
     */
    void sendEventWithOOC(const std::string& ooc, Event& event)
    {
        event.originOfCondition = ooc;
        sendEvent(event);
    }

    void registerDbusLoggingSignal()
    {
        if (!serviceEnabled || matchDbusLogging)
        {
            BMCWEB_LOG_DEBUG("Not registering dbus logging signal.");
            return;
        }

        BMCWEB_LOG_DEBUG("Dbus logging signal - Register.");
        std::string matchStr("type='signal', "
                             "member='InterfacesAdded', "
                             "path_namespace='/xyz/openbmc_project/logging'");

        auto signalHandler = [this](sdbusplus::message::message& msg) {
            if (msg.get_type() != SD_BUS_MESSAGE_SIGNAL)
            {
                BMCWEB_LOG_ERROR("Dbus logging signal error.");
                return;
            }

            sdbusplus::message::object_path objPath;
            std::map<std::string,
                     std::map<std::string,
                              std::variant<std::string, uint32_t, uint64_t,
                                           bool, std::vector<std::string>>>>
                properties;

            std::string messageId = "";
            std::string eventId = "";
            std::string severity = "";
            std::string timestamp = "";
            std::string originOfCondition = "";
            std::string message;
            std::string deviceName;
            std::string resourceType;
            std::string logEntryId;
            // this variable will record the log entry from the satellite BMC.
            std::string satBMCLogEntryUrl;
            std::string resolution;
            std::vector<std::string> messageArgs = {};
            const std::vector<std::string>* additionalDataPtr;
            nlohmann::json::object_t cper;

            msg.read(objPath, properties);
            for (const auto& [key, val] :
                 properties["xyz.openbmc_project.Logging.Entry"])
            {
                if (key == "AdditionalData")
                {
                    additionalDataPtr =
                        std::get_if<std::vector<std::string>>(&val);
                    if (additionalDataPtr != nullptr)
                    {
                        AdditionalData additional(*additionalDataPtr);
                        if (additional.count("DEVICE_NAME") > 0)
                        {
                            deviceName = additional["DEVICE_NAME"];
                        }
                        // convert SEL SENSOR_PATH to RF OriginOfCondition
                        if (additional.count("SENSOR_PATH") == 1)
                        {
                            originOfCondition = additional["SENSOR_PATH"];
                        }
                        if (additional.count("REDFISH_ORIGIN_OF_CONDITION") ==
                            1)
                        {
                            originOfCondition =
                                additional["REDFISH_ORIGIN_OF_CONDITION"];
                        }
                        if (additional.count("REDFISH_LOGENTRY") == 1)
                        {
                            satBMCLogEntryUrl = additional["REDFISH_LOGENTRY"];
                        }
                        if (additional.count("REDFISH_MESSAGE_ID") == 1)
                        {
                            messageId = additional["REDFISH_MESSAGE_ID"];
                            if (additional.count("REDFISH_MESSAGE_ARGS") == 1)
                            {
                                std::string args =
                                    additional["REDFISH_MESSAGE_ARGS"];
                                boost::split(messageArgs, args,
                                             boost::is_any_of(","));
                                // Trim leading and tailing whitespace of each
                                // argument
                                for (auto& msgArg : messageArgs)
                                {
                                    boost::trim(msgArg);
                                }

                                if (!messageArgs[0].empty())
                                {
                                    // Map dbus property to redfish property
                                    if (dBusToRedfishProperty.find(
                                            messageArgs[0]) !=
                                        dBusToRedfishProperty.end())
                                    {
                                        messageArgs[0] = dBusToRedfishProperty
                                            [messageArgs[0]];
                                    }
                                    else
                                    {
                                        BMCWEB_LOG_WARNING(
                                            "property mapping not found for {}",
                                            messageArgs[0]);
                                    }
                                }
                            }
                            else if (additional.count("REDFISH_MESSAGE_ARGS") >
                                     0)
                            {
                                BMCWEB_LOG_ERROR(
                                    "Multiple "
                                    "REDFISH_MESSAGE_ARGS in the Dbus "
                                    "signal message.");
                                return;
                            }
                        }
                        else
                        {
                            auto counter =
                                additional.count("REDFISH_MESSAGE_ID");
                            // when removing entries counter will be 0
                            if (counter > 0)
                            {
                                BMCWEB_LOG_ERROR(
                                    "There should be exactly one MessageId in the Dbus signal message. Found {}",
                                    std::to_string(counter));
                                return;
                            }
                        }

                        nlohmann::json::object_t oem;
                        parseAdditionalDataForCPER(cper, oem, additional,
                                                   originOfCondition);
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("Invalid type of AdditionalData "
                                         "property.");
                        return;
                    }
                }
                else if (key == "EventId")
                {
                    const std::string* eventIdPtr;

                    eventIdPtr = std::get_if<std::string>(&val);
                    if (eventIdPtr != nullptr)
                    {
                        eventId = *eventIdPtr;
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("Invalid type of EventId property.");
                        return;
                    }
                }
                else if (key == "Id")
                {
                    const uint32_t* ipPtr = std::get_if<uint32_t>(&val);
                    if (ipPtr != nullptr)
                    {
                        logEntryId = std::to_string(*ipPtr);
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("Invalid type of Id property.");
                        return;
                    }
                }
                else if (key == "Resolution")
                {
                    const std::string* resolutionPtr;
                    resolutionPtr = std::get_if<std::string>(&val);
                    if (resolutionPtr != nullptr)
                    {
                        resolution = std::move(*resolutionPtr);
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR(
                            "Invalid type of Resolution property.");
                        return;
                    }
                }
                else if (key == "Severity")
                {
                    const std::string* severityPtr;

                    severityPtr = std::get_if<std::string>(&val);
                    if (severityPtr != nullptr)
                    {
                        severity = std::move(*severityPtr);
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("Invalid type of Severity "
                                         "property.");
                        return;
                    }
                }
                else if (key == "Timestamp")
                {
                    const uint64_t* timestampPtr;

                    timestampPtr = std::get_if<uint64_t>(&val);
                    if (timestampPtr != nullptr)
                    {
                        timestamp =
                            std::move(redfish::time_utils::getDateTimeStdtime(
                                redfish::time_utils::getTimestamp(
                                    *timestampPtr)));
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("Invalid type of Timestamp "
                                         "property.");
                        return;
                    }
                }
                else
                {
                    continue;
                }
            }

            if (messageId == "")
            {
                // it happens when removing entries
                BMCWEB_LOG_WARNING("Invalid Dbus log entry.");
                return;
            }
            else
            {
                Event event(messageId);
                if (!event.isValid())
                {
                    return;
                }
                event.messageSeverity =
                    translateSeverityDbusToRedfish(severity);
                event.eventTimestamp = timestamp;
                event.setRegistryMsg(messageArgs);
                event.messageArgs = messageArgs;
                if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                {
                    event.oem = {
                        {"Oem",
                         {{"Nvidia",
                           {{"@odata.type", "#NvidiaEvent.v1_0_0.EventRecord"},
                            {"Device", deviceName},
                            {"ErrorId", eventId}}}}}};
                }
                if (!cper.empty())
                {
                    event.cper = cper;
                }
                event.eventResolution = resolution;
                event.logEntryId = logEntryId;
                event.satBMCLogEntryUrl = satBMCLogEntryUrl;
                if (!originOfCondition.empty())
                {
                    for (auto& it : dBusToResourceType)
                    {
                        if (originOfCondition.find(it.first) !=
                            std::string::npos)
                        {
                            resourceType = it.second;
                            break;
                        }
                    }
                    // resourceType empty error case not handled because it will
                    // impact existing resourceErrordetected error messages
                    event.resourceType = resourceType;
                    eventServiceOOC(originOfCondition, deviceName, event);
                }
                else
                {
                    BMCWEB_LOG_WARNING(
                        "no OriginOfCondition in event log. MsgId: {}",
                        messageId);
                    sendEventWithOOC(std::string{""}, event);
                }
            }
        };

        matchDbusLogging = std::make_shared<sdbusplus::bus::match_t>(
            *crow::connections::systemBus, matchStr, signalHandler);
    }

    /**
     * @brief Finds the right OriginOfCondition for @a path and sends the Event
     *        The map @a dBusToRedfishURI is used for that purpose
     * @param path  orginal path that came from Phosphor Logging
     * @param event  the event to be sent out
     */
    inline void eventServiceOOC(const std::string& path,
                                const std::string& devName, Event& event)
    {
#ifdef BMCWEB_ENABLE_REDFISH_AGGREGATION
        // OOC Path in HMC events is already converted to Redfish path.
        if (path.starts_with("/redfish/v1/"))
        {
            std::string oocPath(path);
            addPrefixToStringItem(oocPath, redfishAggregationPrefix);
            sendEventWithOOC(oocPath, event);
            return;
        }
#endif
        sdbusplus::message::object_path objPath(path);
        std::string deviceName = objPath.filename();
        if (false == deviceName.empty())
        {
            for (auto& it : dBusToRedfishURI)
            {
                if (path.find(it.first) != std::string::npos)
                {
                    std::string newPath;
                    if (it.first == sensorSubTree)
                    {
                        std::string chassisName = PLATFORMDEVICEPREFIX +
                                                  devName;
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
#endif

    bool validateAndSplitUrl(const std::string& destUrl, std::string& urlProto,
                             std::string& host, std::string& port,
                             std::string& path)
    {
        // Validate URL using regex expression
        // Format: <protocol>://<host>:<port>/<path>
        // protocol: http/https
        std::cmatch match;
        if (!std::regex_match(destUrl.c_str(), match, urlRegex))
        {
            BMCWEB_LOG_INFO("Dest. url did not match ");
            return false;
        }

        urlProto = std::string(match[1].first, match[1].second);
        if (urlProto == "http")
        {
#ifndef BMCWEB_INSECURE_ENABLE_HTTP_PUSH_STYLE_EVENTING
            return false;
#endif
        }

        host = std::string(match[2].first, match[2].second);
        port = std::string(match[3].first, match[3].second);
        path = std::string(match[4].first, match[4].second);
        if (port.empty())
        {
            if (urlProto == "http")
            {
                port = "80";
            }
            else
            {
                port = "443";
            }
        }
        if (path.empty())
        {
            path = "/";
        }
        return true;
    }
};

} // namespace redfish
