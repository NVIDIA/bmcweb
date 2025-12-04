// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "event_service_manager.hpp"
#include "event_service_store.hpp"
#include "http_request.hpp"
#include "io_context_singleton.hpp"
#include "logging.hpp"
#include "subscriber.hpp"
#include "subscription.hpp"

#include <string>
#include <string_view>

namespace redfish
{
inline void nvidiaSetEventServiceConfig(
    const persistent_data::EventServiceConfig& eventServiceConfig,
    const std::string_view& target)
{
    if (target.empty())
    {
        BMCWEB_LOG_DEBUG("nvidiaSetEventServiceConfig: target is empty");
    }
    if constexpr (BMCWEB_REDFISH_DBUS_LOG)
    {
        EventServiceManager::getInstance().setEventServiceConfig(
            eventServiceConfig, target);
    }
    else
    {
        EventServiceManager::getInstance().setEventServiceConfig(
            eventServiceConfig);
    }
}

inline void enableRedfishEventListener(const crow::Request& req)
{
    BMCWEB_LOG_DEBUG("nvidiaSetEventServiceConfig: req target: {}",
                     req.target());
    if constexpr (BMCWEB_REDFISH_AGGREGATION)
    {
        // new subscription is added so start redfish event listener.
        if (EventServiceManager::getInstance().getNumberOfSubscriptions() == 1)
        {
            startRedfishEventListener(getIoContext());
        }
    }
}

inline void disableRedfishEventListener(const crow::Request& req)
{
    BMCWEB_LOG_DEBUG("disableRedfishEventListener: req target: {}",
                     req.target());
    if constexpr (BMCWEB_REDFISH_AGGREGATION)
    {
        // there will be no subscription after the deletion
        // stop redfish event listener
        if (EventServiceManager::getInstance().getNumberOfSubscriptions() == 0)
        {
            stopRedfishEventListener(getIoContext());
        }
    }
}

inline void sendPropertyModifiedEvent(
    const std::string_view& target, const std::string& serviceName,
    const std::string& property, const std::string& value)
{
    BMCWEB_LOG_DEBUG(
        "sendPropertyModifiedEvent: target: {}, property: {}, value: {}",
        target, property, value);
    if constexpr (BMCWEB_REDFISH_DBUS_LOG)
    {
        // Send an event for property change
        NvEvent event = redfish::EventUtil::createEventPropertyModified(
            property, value, serviceName);
        redfish::EventServiceManager::getInstance().sendEventWithOOC(
            std::string(target), event);
    }
}

inline void sendResourceCreatedEvent(const std::string_view& target,
                                     const std::string& serviceName)
{
    BMCWEB_LOG_DEBUG("resourceCreatedEvent: target: {}, serviceName: {}",
                     target, serviceName);
    if constexpr (BMCWEB_REDFISH_DBUS_LOG)
    {
        // Send an event for resource creation
        NvEvent event =
            redfish::EventUtil::createEventResourceCreated(serviceName);
        redfish::EventServiceManager::getInstance().sendEventWithOOC(
            std::string(target), event);
    }
}

inline void sendResourceDeletedEvent(const std::string_view& target,
                                     const std::string& serviceName)
{
    BMCWEB_LOG_DEBUG("resourceDeletedEvent: target: {}, serviceName: {}",
                     target, serviceName);
    if constexpr (BMCWEB_REDFISH_DBUS_LOG)
    {
        NvEvent event =
            redfish::EventUtil::createEventResourceRemoved(serviceName);
        redfish::EventServiceManager::getInstance().sendEventWithOOC(
            std::string(target), event);
    }
}

} // namespace redfish
