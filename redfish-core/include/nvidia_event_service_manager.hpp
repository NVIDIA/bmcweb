#pragma once
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "event_matches_filter.hpp"
#include "event_service_store.hpp"
#include "filter_expr_executor.hpp"
#include "generated/enums/event.hpp"
#include "generated/enums/log_entry.hpp"
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
#include <string_view>
namespace redfish
{
/*
 * Structure for an event which is based on Event v1.7.0 in "Redfish Schema
 * Supplement(DSP0268)".
 */
enum redfish_bool
{
    redfishBoolNa, // NOT APPLICABLE
    redfishBoolTrue,
    redfishBoolFalse
};

// Error constants of class Event
static constexpr int redfishInvalidEvent = -1;
static constexpr int redfishInvalidArgs = -2;
class DsEvent
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
    DsEvent(const std::string& messageId) : messageId(messageId)
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
#ifdef BMCWEB_REDFISH_AGGREGATION
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
    DsEvent createEventPropertyModified(const std::string& arg1,
                                        const std::string& arg2,
                                        const std::string& resourceType)
    {
        DsEvent event(propertyModified);
        std::vector<std::string> messageArgs;
        messageArgs.push_back(arg1);
        messageArgs.push_back(arg2);
        event.setRegistryMsg(messageArgs);

        formBaseEvent(event, resourceType);

        return event;
    }

    // This function is used to form event message
    DsEvent createEventResourceCreated(const std::string& resourceType)
    {
        DsEvent event(resorceCreated);
        formBaseEvent(event, resourceType);
        return event;
    }

    // This function is used to form event message
    DsEvent createEventResourceRemoved(const std::string& resourceType)
    {
        DsEvent event(resourceDeleted);
        formBaseEvent(event, resourceType);
        return event;
    }

    // This function is used to form event message
    DsEvent createEventRebootReason(const std::string& arg,
                                    const std::string& resourceType)
    {
        DsEvent event(rebootReason);

        std::vector<std::string> messageArgs;
        messageArgs.push_back(arg);
        event.setRegistryMsg(messageArgs);
        formBaseEvent(event, resourceType);

        return event;
    }

  private:
    void formBaseEvent(DsEvent& event, const std::string& resourceType)
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
} // namespace redfish
