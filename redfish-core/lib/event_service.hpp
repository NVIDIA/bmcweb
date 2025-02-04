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
#include "app.hpp"
#include "event_service_manager.hpp"
#include "http/utility.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "snmp_trap_event_clients.hpp"

#include <boost/beast/http/fields.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/parse.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/dbus_utils.hpp>

#include <charconv>
#include <memory>
#include <ranges>
#include <span>
#include <string>

namespace redfish
{

static constexpr const std::array<const char*, 2> supportedEvtFormatTypes = {
    eventFormatType, metricReportFormatType};
static constexpr const std::array<const char*, 4> supportedRegPrefixes = {
    "Base", "OpenBMC", "TaskEvent", "ResourceEvent"};
static constexpr const std::array<const char*, 3> supportedRetryPolicies = {
    "TerminateAfterRetries", "SuspendRetries", "RetryForever"};

static constexpr const std::array<const char*, 11> supportedResourceTypes = {
    "Task",         "AccountService",     "ManagerAccount", "SessionService",
    "EventService", "UpdateService",      "Chassis",        "Systems",
    "Managers",     "CertificateService", "VirtualMedia"};

inline void requestRoutesEventService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/EventService/")
        .privileges(redfish::privileges::getEventService)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/EventService";
        asyncResp->res.jsonValue["@odata.type"] =
            "#EventService.v1_7_2.EventService";
        asyncResp->res.jsonValue["Id"] = "EventService";
        asyncResp->res.jsonValue["Name"] = "Event Service";
        asyncResp->res.jsonValue["ServerSentEventUri"] =
            "/redfish/v1/EventService/SSE";

        asyncResp->res.jsonValue["Subscriptions"]["@odata.id"] =
            "/redfish/v1/EventService/Subscriptions";
        asyncResp->res
            .jsonValue["Actions"]["#EventService.SubmitTestEvent"]["target"] =
            "/redfish/v1/EventService/Actions/EventService.SubmitTestEvent";

        const persistent_data::EventServiceConfig eventServiceConfig =
            persistent_data::EventServiceStore::getInstance()
                .getEventServiceConfig();

        asyncResp->res.jsonValue["Status"]["State"] =
            (eventServiceConfig.enabled ? "Enabled" : "Disabled");
        asyncResp->res.jsonValue["ServiceEnabled"] = eventServiceConfig.enabled;
        asyncResp->res.jsonValue["DeliveryRetryAttempts"] =
            eventServiceConfig.retryAttempts;
        asyncResp->res.jsonValue["DeliveryRetryIntervalSeconds"] =
            eventServiceConfig.retryTimeoutInterval;
        asyncResp->res.jsonValue["EventFormatTypes"] = supportedEvtFormatTypes;
        asyncResp->res.jsonValue["RegistryPrefixes"] = supportedRegPrefixes;
        asyncResp->res.jsonValue["ResourceTypes"] = supportedResourceTypes;

        nlohmann::json::object_t supportedSSEFilters;
        supportedSSEFilters["EventFormatType"] = true;
        supportedSSEFilters["MessageId"] = true;
        supportedSSEFilters["MetricReportDefinition"] = true;
        supportedSSEFilters["RegistryPrefix"] = true;
        supportedSSEFilters["OriginResource"] = false;
        supportedSSEFilters["ResourceType"] = false;

        asyncResp->res.jsonValue["SSEFilterPropertiesSupported"] =
            std::move(supportedSSEFilters);
    });

    BMCWEB_ROUTE(app, "/redfish/v1/EventService/")
        .privileges(redfish::privileges::patchEventService)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::optional<bool> serviceEnabled;
        std::optional<uint32_t> retryAttemps;
        std::optional<uint32_t> retryInterval;

        if (!json_util::readJsonPatch(
                req, asyncResp->res, "ServiceEnabled", serviceEnabled,
                "DeliveryRetryAttempts", retryAttemps,
                "DeliveryRetryIntervalSeconds", retryInterval))
        {
            return;
        }

        persistent_data::EventServiceConfig eventServiceConfig =
            persistent_data::EventServiceStore::getInstance()
                .getEventServiceConfig();

        if (serviceEnabled)
        {
            eventServiceConfig.enabled = *serviceEnabled;
        }

        if (retryAttemps)
        {
            // Supported range [1-3]
            if ((*retryAttemps < 1) || (*retryAttemps > 3))
            {
                messages::queryParameterOutOfRange(
                    asyncResp->res, std::to_string(*retryAttemps),
                    "DeliveryRetryAttempts", "[1-3]");
            }
            else
            {
                eventServiceConfig.retryAttempts = *retryAttemps;
            }
        }

        if (retryInterval)
        {
            // Supported range [5 - 180]
            if ((*retryInterval < 5) || (*retryInterval > 180))
            {
                messages::queryParameterOutOfRange(
                    asyncResp->res, std::to_string(*retryInterval),
                    "DeliveryRetryIntervalSeconds", "[5-180]");
            }
            else
            {
                eventServiceConfig.retryTimeoutInterval = *retryInterval;
            }
        }

#ifdef BMCWEB_ENABLE_REDFISH_DBUS_EVENT_PUSH
        EventServiceManager::getInstance().setEventServiceConfig(
            eventServiceConfig, req.target());
#else
        EventServiceManager::getInstance().setEventServiceConfig(
            eventServiceConfig);
#endif
    });
}

inline void requestRoutesSubmitTestEvent(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/EventService/Actions/EventService.SubmitTestEvent/")
        .privileges(redfish::privileges::postEventService)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        // From the redfish spec on EventId
        // A service can ignore this value and replace it with its own.
        // note that his parameter is intentionally ignored.
        std::optional<std::string> eventId;

        TestEvent testEvent;
        // clang-format off
        if (!json_util::readJsonAction(
                req, asyncResp->res,
                "EventGroupId", testEvent.eventGroupId,
                "EventId", eventId,
                "EventTimestamp", testEvent.eventTimestamp,
                "Message", testEvent.message,
                "MessageArgs", testEvent.messageArgs,
                "MessageId", testEvent.messageId,
                "OriginOfCondition", testEvent.originOfCondition,
                "Resolution", testEvent.resolution,
                "Severity", testEvent.severity))
        {
            return;
        }
        // clang-format on

        if (!EventServiceManager::getInstance().sendTestEventLog(testEvent))
        {
            messages::serviceDisabled(asyncResp->res,
                                      "/redfish/v1/EventService/");
            return;
        }
        asyncResp->res.result(boost::beast::http::status::no_content);
    });
}

inline void doSubscriptionCollection(
    const boost::system::error_code& ec,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const dbus::utility::ManagedObjectType& resp)
{
    if (ec)
    {
        if (ec.value() == EBADR || ec.value() == EHOSTUNREACH)
        {
            // This is an optional process so just return if it isn't there
            return;
        }

        BMCWEB_LOG_ERROR("D-Bus response error on GetManagedObjects {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }
    nlohmann::json& memberArray = asyncResp->res.jsonValue["Members"];
    for (const auto& objpath : resp)
    {
        sdbusplus::message::object_path path(objpath.first);
        const std::string snmpId = path.filename();
        if (snmpId.empty())
        {
            BMCWEB_LOG_ERROR("The SNMP client ID is wrong");
            messages::internalError(asyncResp->res);
            return;
        }

        getSnmpSubscriptionList(asyncResp, snmpId, memberArray);
    }
}

inline void requestRoutesEventDestinationCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/EventService/Subscriptions/")
        .privileges(redfish::privileges::getEventDestinationCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        asyncResp->res.jsonValue["@odata.type"] =
            "#EventDestinationCollection.EventDestinationCollection";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/EventService/Subscriptions";
        asyncResp->res.jsonValue["Name"] = "Event Destination Collections";

        nlohmann::json& memberArray = asyncResp->res.jsonValue["Members"];

        std::vector<std::string> subscripIds =
            EventServiceManager::getInstance().getAllIDs();
        memberArray = nlohmann::json::array();
        asyncResp->res.jsonValue["Members@odata.count"] = subscripIds.size();

        for (const std::string& id : subscripIds)
        {
            nlohmann::json::object_t member;
            member["@odata.id"] = boost::urls::format(
                "/redfish/v1/EventService/Subscriptions/{}" + id);
            memberArray.emplace_back(std::move(member));
        }
        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code& ec,
                        const dbus::utility::ManagedObjectType& resp) {
            doSubscriptionCollection(ec, asyncResp, resp);
        },
            "xyz.openbmc_project.Network.SNMP",
            "/xyz/openbmc_project/network/snmp/manager",
            "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    });

    BMCWEB_ROUTE(app, "/redfish/v1/EventService/Subscriptions/")
        .privileges(redfish::privileges::postEventDestinationCollection)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if (EventServiceManager::getInstance().getNumberOfSubscriptions() >=
            maxNoOfSubscriptions)
        {
            messages::eventSubscriptionLimitExceeded(asyncResp->res);
            return;
        }
        std::string destUrl;
        std::string protocol;
        std::optional<std::string> context;
        std::optional<std::string> subscriptionType;
        std::optional<std::string> eventFormatType2;
        std::optional<std::string> retryPolicy;
        std::optional<std::vector<std::string>> msgIds;
        std::optional<std::vector<std::string>> regPrefixes;
        std::optional<std::vector<std::string>> resTypes;
        std::optional<std::vector<nlohmann::json::object_t>> headers;
        std::optional<std::vector<nlohmann::json::object_t>> mrdJsonArray;
        std::optional<std::vector<std::string>> originResources;

        if (!json_util::readJsonPatch(
                req, asyncResp->res, "Destination", destUrl, "Context", context,
                "Protocol", protocol, "SubscriptionType", subscriptionType,
                "EventFormatType", eventFormatType2, "HttpHeaders", headers,
                "RegistryPrefixes", regPrefixes, "MessageIds", msgIds,
                "OriginResources", originResources, "DeliveryRetryPolicy",
                retryPolicy, "MetricReportDefinitions", mrdJsonArray,
                "ResourceTypes", resTypes))
        {
            return;
        }

        // https://stackoverflow.com/questions/417142/what-is-the-maximum-length-of-a-url-in-different-browsers
        static constexpr const uint16_t maxDestinationSize = 2000;
        if (destUrl.size() > maxDestinationSize)
        {
            messages::stringValueTooLong(asyncResp->res, "Destination",
                                         maxDestinationSize);
            return;
        }

        if (regPrefixes && msgIds)
        {
            if (!regPrefixes->empty() && !msgIds->empty())
            {
                messages::propertyValueConflict(asyncResp->res, "MessageIds",
                                                "RegistryPrefixes");
                return;
            }
        }

        boost::system::result<boost::urls::url> url =
            boost::urls::parse_absolute_uri(destUrl);
        if (!url)
        {
            BMCWEB_LOG_WARNING("Failed to validate and split destination url");
            messages::propertyValueFormatError(asyncResp->res, destUrl,
                                               "Destination");
            return;
        }
        url->normalize();
        crow::utility::setProtocolDefaults(*url, protocol);
        crow::utility::setPortDefaults(*url);

        if (url->path().empty())
        {
            url->set_path("/");
        }

        if (url->has_userinfo())
        {
            messages::propertyValueFormatError(asyncResp->res, destUrl,
                                               "Destination");
            return;
        }

        if (protocol == "SNMPv2c")
        {
            if (context)
            {
                messages::propertyValueConflict(asyncResp->res, "Context",
                                                "Protocol");
                return;
            }
            if (eventFormatType2)
            {
                messages::propertyValueConflict(asyncResp->res,
                                                "EventFormatType", "Protocol");
                return;
            }

            if (retryPolicy)
            {
                messages::propertyValueConflict(asyncResp->res, "RetryPolicy",
                                                "Protocol");
                return;
            }
            if (msgIds)
            {
                messages::propertyValueConflict(asyncResp->res, "MessageIds",
                                                "Protocol");
                return;
            }
            if (regPrefixes)
            {
                messages::propertyValueConflict(asyncResp->res,
                                                "RegistryPrefixes", "Protocol");
                return;
            }
            if (resTypes)
            {
                messages::propertyValueConflict(asyncResp->res, "ResourceTypes",
                                                "Protocol");
                return;
            }
            if (headers)
            {
                messages::propertyValueConflict(asyncResp->res, "HttpHeaders",
                                                "Protocol");
                return;
            }
            if (mrdJsonArray)
            {
                messages::propertyValueConflict(
                    asyncResp->res, "MetricReportDefinitions", "Protocol");
                return;
            }
            if (url->scheme() != "snmp")
            {
                messages::propertyValueConflict(asyncResp->res, "Destination",
                                                "Protocol");
                return;
            }

            addSnmpTrapClient(asyncResp, url->host_address(),
                              url->port_number());
            return;
        }

        std::shared_ptr<Subscription> subValue =
            std::make_shared<Subscription>(*url, app.ioContext());

        subValue->destinationUrl = std::move(*url);

        if (subscriptionType)
        {
            if (*subscriptionType != "RedfishEvent")
            {
                messages::propertyValueNotInList(
                    asyncResp->res, *subscriptionType, "SubscriptionType");
                return;
            }
            subValue->subscriptionType = *subscriptionType;
        }
        else
        {
            subValue->subscriptionType = "RedfishEvent"; // Default
        }

        if (protocol != "Redfish")
        {
            messages::propertyValueNotInList(asyncResp->res, protocol,
                                             "Protocol");
            return;
        }
        subValue->protocol = protocol;

        if (eventFormatType2)
        {
            if (std::ranges::find(supportedEvtFormatTypes, *eventFormatType2) ==
                supportedEvtFormatTypes.end())
            {
                messages::propertyValueNotInList(
                    asyncResp->res, *eventFormatType2, "EventFormatType");
                return;
            }
            subValue->eventFormatType = *eventFormatType2;
        }
        else
        {
            // If not specified, use default "Event"
            subValue->eventFormatType = "Event";
        }

        if (context)
        {
            // This value is selected arbitrarily.
            constexpr const size_t maxContextSize = 256;
            if (context->size() > maxContextSize)
            {
                messages::stringValueTooLong(asyncResp->res, "Context",
                                             maxContextSize);
                return;
            }
            subValue->customText = *context;
        }

        if (headers)
        {
            size_t cumulativeLen = 0;

            for (const nlohmann::json::object_t& headerChunk : *headers)
            {
                for (const auto& item : headerChunk)
                {
                    const std::string* value =
                        item.second.get_ptr<const std::string*>();
                    if (value == nullptr)
                    {
                        messages::propertyValueFormatError(
                            asyncResp->res, item.second,
                            "HttpHeaders/" + item.first);
                        return;
                    }
                    // Adding a new json value is the size of the key, +
                    // the size of the value + 2 * 2 quotes for each, +
                    // the colon and space between. example:
                    // "key": "value"
                    cumulativeLen += item.first.size() + value->size() + 6;
                    // This value is selected to mirror http_connection.hpp
                    constexpr const uint16_t maxHeaderSizeED = 8096;
                    if (cumulativeLen > maxHeaderSizeED)
                    {
                        messages::arraySizeTooLong(
                            asyncResp->res, "HttpHeaders", maxHeaderSizeED);
                        return;
                    }
                    subValue->httpHeaders.set(item.first, *value);
                }
            }
        }

        if (regPrefixes)
        {
            for (const std::string& it : *regPrefixes)
            {
                if (std::ranges::find(supportedRegPrefixes, it) ==
                    supportedRegPrefixes.end())
                {
                    messages::propertyValueNotInList(asyncResp->res, it,
                                                     "RegistryPrefixes");
                    return;
                }
            }
            subValue->registryPrefixes = *regPrefixes;
        }

        if (originResources)
        {
            subValue->originResources = *originResources;
        }

        if (resTypes)
        {
            for (const std::string& it : *resTypes)
            {
                if (std::ranges::find(supportedResourceTypes, it) ==
                    supportedResourceTypes.end())
                {
                    messages::propertyValueNotInList(asyncResp->res, it,
                                                     "ResourceTypes");
                    return;
                }
            }
            subValue->resourceTypes = *resTypes;
        }

        if (msgIds)
        {
            std::vector<std::string> registryPrefix;

            // If no registry prefixes are mentioned, consider all
            // supported prefixes
            if (subValue->registryPrefixes.empty())
            {
                registryPrefix.assign(supportedRegPrefixes.begin(),
                                      supportedRegPrefixes.end());
            }
            else
            {
                registryPrefix = subValue->registryPrefixes;
            }

            for (const std::string& id : *msgIds)
            {
                bool validId = false;

                // Check for Message ID in each of the selected Registry
                for (const std::string& it : registryPrefix)
                {
                    const std::span<const redfish::registries::MessageEntry>
                        registry =
                            redfish::registries::getRegistryFromPrefix(it);

                    if (std::ranges::any_of(
                            registry,
                            [&id](const redfish::registries::MessageEntry&
                                      messageEntry) {
                        return id == messageEntry.first;
                    }))
                    {
                        validId = true;
                        break;
                    }
                }

                if (!validId)
                {
                    messages::propertyValueNotInList(asyncResp->res, id,
                                                     "MessageIds");
                    return;
                }
            }

            subValue->registryMsgIds = *msgIds;
        }

        if (retryPolicy)
        {
            if (std::ranges::find(supportedRetryPolicies, *retryPolicy) ==
                supportedRetryPolicies.end())
            {
                messages::propertyValueNotInList(asyncResp->res, *retryPolicy,
                                                 "DeliveryRetryPolicy");
                return;
            }
            subValue->retryPolicy = *retryPolicy;
        }
        else
        {
            // Default "TerminateAfterRetries"
            subValue->retryPolicy = "TerminateAfterRetries";
        }

        if (mrdJsonArray)
        {
            for (nlohmann::json::object_t& mrdObj : *mrdJsonArray)
            {
                std::string mrdUri;

                if (!json_util::readJsonObject(mrdObj, asyncResp->res,
                                               "@odata.id", mrdUri))

                {
                    return;
                }
                subValue->metricReportDefinitions.emplace_back(mrdUri);
            }
        }

        std::string id =
            EventServiceManager::getInstance().addPushSubscription(subValue);
        if (id.empty())
        {
            messages::internalError(asyncResp->res);
            return;
        }
#ifdef BMCWEB_ENABLE_REDFISH_AGGREGATION
        // new subscription is added so start redfish event listener.
        if (EventServiceManager::getInstance().getNumberOfSubscriptions() == 1)
        {
            startRedfishEventListener(*req.ioService);
        }
#endif
        messages::created(asyncResp->res);
        asyncResp->res.addHeader(
            "Location", "/redfish/v1/EventService/Subscriptions/" + id);
    });
}

inline void requestRoutesEventDestination(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/EventService/Subscriptions/<str>/")
        .privileges(redfish::privileges::getEventDestination)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& param) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        if (param.starts_with("snmp"))
        {
            getSnmpTrapClient(asyncResp, param);
            return;
        }

        std::shared_ptr<Subscription> subValue =
            EventServiceManager::getInstance().getSubscription(param);
        if (subValue == nullptr)
        {
            asyncResp->res.result(boost::beast::http::status::not_found);
            return;
        }
        const std::string& id = param;

        asyncResp->res.jsonValue["@odata.type"] =
            "#EventDestination.v1_8_0.EventDestination";
        asyncResp->res.jsonValue["Protocol"] = "Redfish";
        asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
            "/redfish/v1/EventService/Subscriptions/{}", id);
        asyncResp->res.jsonValue["Id"] = id;
        asyncResp->res.jsonValue["Name"] = "Event Destination " + id;
        asyncResp->res.jsonValue["Destination"] = subValue->destinationUrl;
        asyncResp->res.jsonValue["Context"] = subValue->customText;
        asyncResp->res.jsonValue["SubscriptionType"] =
            subValue->subscriptionType;
        asyncResp->res.jsonValue["HttpHeaders"] = nlohmann::json::array();
        asyncResp->res.jsonValue["EventFormatType"] = subValue->eventFormatType;
        asyncResp->res.jsonValue["RegistryPrefixes"] =
            subValue->registryPrefixes;
        asyncResp->res.jsonValue["ResourceTypes"] = subValue->resourceTypes;
        asyncResp->res.jsonValue["MessageIds"] = subValue->registryMsgIds;
        asyncResp->res.jsonValue["DeliveryRetryPolicy"] = subValue->retryPolicy;

        nlohmann::json::array_t mrdJsonArray;
        for (const auto& mdrUri : subValue->metricReportDefinitions)
        {
            nlohmann::json::object_t mdr;
            mdr["@odata.id"] = mdrUri;
            mrdJsonArray.emplace_back(std::move(mdr));
        }
        asyncResp->res.jsonValue["MetricReportDefinitions"] = mrdJsonArray;
    });
    BMCWEB_ROUTE(app, "/redfish/v1/EventService/Subscriptions/<str>/")
        // The below privilege is wrong, it should be ConfigureManager OR
        // ConfigureSelf
        // https://github.com/openbmc/bmcweb/issues/220
        //.privileges(redfish::privileges::patchEventDestination)
        .privileges({{"ConfigureManager"}})
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& param) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::shared_ptr<Subscription> subValue =
            EventServiceManager::getInstance().getSubscription(param);
        if (subValue == nullptr)
        {
            asyncResp->res.result(boost::beast::http::status::not_found);
            return;
        }

        std::optional<std::string> context;
        std::optional<std::string> retryPolicy;
        std::optional<std::vector<nlohmann::json::object_t>> headers;

        if (!json_util::readJsonPatch(req, asyncResp->res, "Context", context,
                                      "DeliveryRetryPolicy", retryPolicy,
                                      "HttpHeaders", headers))
        {
            return;
        }

        if (context)
        {
            subValue->customText = *context;
#ifdef BMCWEB_ENABLE_REDFISH_DBUS_EVENT_PUSH
            // Send an event for property change
            Event event =
                redfish::EventUtil::getInstance().createEventPropertyModified(
                    "Context", *context, "EventService");
            redfish::EventServiceManager::getInstance().sendEventWithOOC(
                std::string(req.target()), event);
#endif
        }

        if (headers)
        {
            std::string keyValues;
            boost::beast::http::fields fields;
            for (const nlohmann::json::object_t& headerChunk : *headers)
            {
                for (const auto& it : headerChunk)
                {
                    const std::string* value =
                        it.second.get_ptr<const std::string*>();
                    if (value == nullptr)
                    {
                        messages::propertyValueFormatError(
                            asyncResp->res, it.second,
                            "HttpHeaders/" + it.first);
                        return;
                    }
                    fields.set(it.first, *value);
                    keyValues += it.first;
                    keyValues.push_back(':');
                    keyValues += *value;
                    keyValues.push_back(' ');
                }
            }
            subValue->httpHeaders = std::move(fields);
#ifdef BMCWEB_ENABLE_REDFISH_DBUS_EVENT_PUSH
            // Send an event for property change
            Event event =
                redfish::EventUtil::getInstance().createEventPropertyModified(
                    "Headers", keyValues, "EventService");
            redfish::EventServiceManager::getInstance().sendEventWithOOC(
                std::string(req.target()), event);
#endif
        }

        if (retryPolicy)
        {
            if (std::ranges::find(supportedRetryPolicies, *retryPolicy) ==
                supportedRetryPolicies.end())
            {
                messages::propertyValueNotInList(asyncResp->res, *retryPolicy,
                                                 "DeliveryRetryPolicy");
                return;
            }
            subValue->retryPolicy = *retryPolicy;
#ifdef BMCWEB_ENABLE_REDFISH_DBUS_EVENT_PUSH
            // Send an event for property change
            Event event =
                redfish::EventUtil::getInstance().createEventPropertyModified(
                    "RetryPolicy", *retryPolicy, "EventService");
            redfish::EventServiceManager::getInstance().sendEventWithOOC(
                std::string(req.target()), event);
#endif
        }

        EventServiceManager::getInstance().updateSubscriptionData();
    });
    BMCWEB_ROUTE(app, "/redfish/v1/EventService/Subscriptions/<str>/")
        // The below privilege is wrong, it should be ConfigureManager OR
        // ConfigureSelf
        // https://github.com/openbmc/bmcweb/issues/220
        //.privileges(redfish::privileges::deleteEventDestination)
        .privileges({{"ConfigureManager"}})
        .methods(boost::beast::http::verb::delete_)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& param) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        if (param.starts_with("snmp"))
        {
            deleteSnmpTrapClient(asyncResp, param);
            EventServiceManager::getInstance().deleteSubscription(param);
            return;
        }

        if (!EventServiceManager::getInstance().isSubscriptionExist(param))
        {
            asyncResp->res.result(boost::beast::http::status::not_found);
            return;
        }
#ifdef BMCWEB_ENABLE_REDFISH_AGGREGATION
        // there will be no subscription after the deletion
        // stop redfish event listener
        if (EventServiceManager::getInstance().getNumberOfSubscriptions() == 1)
        {
            stopRedfishEventListener(*req.ioService);
        }
#endif
        EventServiceManager::getInstance().deleteSubscription(param);
    });
}

} // namespace redfish
