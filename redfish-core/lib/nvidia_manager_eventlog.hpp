/*
 * Copyright (c) 2020-2021 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
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

#include "app.hpp"
#include "error_messages.hpp"
#include "generated/enums/log_entry.hpp"
#include "registries/base_message_registry.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/time_utils.hpp"

#include <boost/beast/http/verb.hpp>

#include <array>
#include <memory>
#include <string>
#include <string_view>

namespace redfish
{
struct DbusEventLogEntry
{
    // represents a subset of an instance of dbus interface
    // xyz.openbmc_project.Logging.Entry

    uint32_t Id = 0;
    std::string Message;
    const std::string* Path = nullptr;
    const std::string* Resolution = nullptr;
    bool Resolved = false;
    std::string ServiceProviderNotify;
    std::string Severity;
    const std::string* EventId = nullptr;
    const uint64_t* Timestamp = nullptr;
    const uint64_t* UpdateTimestamp = nullptr;
    const std::vector<std::string>* AdditionalData = nullptr;
};

inline std::optional<DbusEventLogEntry> fillDbusEventLogEntryFromPropertyMap(
    const dbus::utility::DBusPropertiesMap& resp)
{
    DbusEventLogEntry entry;

    // clang-format off
        bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), resp,
            "Id", entry.Id,
            "Message", entry.Message,
            "Path", entry.Path,
            "Resolution", entry.Resolution,
            "Resolved", entry.Resolved,
            "ServiceProviderNotify", entry.ServiceProviderNotify,
            "Severity", entry.Severity,
            "Timestamp", entry.Timestamp,
            "UpdateTimestamp", entry.UpdateTimestamp,
            "AdditionalData", entry.AdditionalData
        );

    // clang-format on
    if (!success)
    {
        return std::nullopt;
    }
    return entry;
}

inline void managerLogServiceEventLogGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    asyncResp->res.jsonValue["@odata.type"] = "#LogService.v1_2_0.LogService";
    asyncResp->res.jsonValue["@odata.id"] =
        boost::urls::format("/redfish/v1/Managers/{}/LogServices/EventLog",
                            BMCWEB_REDFISH_MANAGER_URI_NAME);
    asyncResp->res.jsonValue["Name"] = "Open BMC EventLog Log Service";
    asyncResp->res.jsonValue["Description"] = "Managers EventLog Log Service";
    asyncResp->res.jsonValue["Id"] = "EventLog";
    asyncResp->res.jsonValue["OverWritePolicy"] = "WrapsWhenFull";

    std::pair<std::string, std::string> redfishDateTimeOffset =
        redfish::time_utils::getDateTimeOffsetNow();
    asyncResp->res.jsonValue["DateTime"] = redfishDateTimeOffset.first;
    asyncResp->res.jsonValue["DateTimeLocalOffset"] =
        redfishDateTimeOffset.second;

    asyncResp->res.jsonValue["Entries"]["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/LogServices/EventLog/Entries",
        BMCWEB_REDFISH_MANAGER_URI_NAME);
}

inline void fillManagerEventLogLogEntryFromPropertyMap(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const dbus::utility::DBusPropertiesMap& resp,
    nlohmann::json& objectToFillOut)
{
    std::optional<DbusEventLogEntry> optEntry =
        fillDbusEventLogEntryFromPropertyMap(resp);

    if (!optEntry.has_value())
    {
        messages::internalError(asyncResp->res);
        return;
    }
    DbusEventLogEntry entry = optEntry.value();

    objectToFillOut["@odata.type"] = "#LogEntry.v1_15_0.LogEntry";
    objectToFillOut["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/LogServices/EventLog/Entries/{}",
        BMCWEB_REDFISH_MANAGER_URI_NAME, std::to_string(entry.Id));
    objectToFillOut["Name"] = "Manager Event Log Entry";
    objectToFillOut["Id"] = std::to_string(entry.Id);
    objectToFillOut["Message"] = entry.Message;
    objectToFillOut["Resolved"] = entry.Resolved;
    std::optional<bool> notifyAction =
        getProviderNotifyAction(entry.ServiceProviderNotify);
    if (notifyAction)
    {
        objectToFillOut["ServiceProviderNotified"] = *notifyAction;
    }
    if ((entry.Resolution != nullptr) && !entry.Resolution->empty())
    {
        objectToFillOut["Resolution"] = *entry.Resolution;
    }
    objectToFillOut["EntryType"] = "Event";
    objectToFillOut["Severity"] =
        translateSeverityDbusToRedfish(entry.Severity);

    if (entry.Timestamp != nullptr)
    {
        objectToFillOut["Created"] =
            redfish::time_utils::getDateTimeUintMs(*entry.Timestamp);
    }
    if (entry.UpdateTimestamp != nullptr)
    {
        objectToFillOut["Modified"] =
            redfish::time_utils::getDateTimeUintMs(*entry.UpdateTimestamp);
    }
    if (entry.Path != nullptr)
    {
        objectToFillOut["AdditionalDataURI"] = boost::urls::format(
            "/redfish/v1/Managers/{}/LogServices/EventLog/Entries/{}/attachment",
            BMCWEB_REDFISH_MANAGER_URI_NAME, std::to_string(entry.Id));
    }

    // Determine if it's a message registry format or not.
    bool isMessageRegistry = false;
    std::string messageId;
    std::string messageArgs;
    if (entry.AdditionalData != nullptr)
    {
        AdditionalData additional(*entry.AdditionalData);
        if (additional.count("REDFISH_MESSAGE_ID") > 0)
        {
            isMessageRegistry = true;
            messageId = additional["REDFISH_MESSAGE_ID"];
            BMCWEB_LOG_DEBUG("MessageId: [{}]", messageId);

            if (additional.count("REDFISH_MESSAGE_ARGS") > 0)
            {
                messageArgs = additional["REDFISH_MESSAGE_ARGS"];
            }
        }
    }

    if (isMessageRegistry)
    {
        message_registries::generateMessageRegistry(
            objectToFillOut,
            "/redfish/v1/Managers/" +
                std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                "/LogServices/"
                "EventLog/Entries/",
            "v1_15_0", std::to_string(entry.Id), "Manager Event Log Entry",
            (entry.Timestamp == nullptr)
                ? ""
                : redfish::time_utils::getDateTimeStdtime(
                      static_cast<time_t>(*entry.Timestamp)),
            messageId, messageArgs,
            (entry.Resolution == nullptr) ? "" : *entry.Resolution,
            entry.Resolved, (entry.EventId == nullptr) ? "" : *entry.EventId,
            "", entry.Severity);
    }
}

inline void afterLogEntriesGetManagedObjects(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::ManagedObjectType& resp)
{
    if (ec)
    {
        // TODO Handle for specific error code
        BMCWEB_LOG_ERROR("getLogEntriesIfaceData resp_handler got error {}",
                         ec);
        messages::internalError(asyncResp->res);
        return;
    }
    nlohmann::json::array_t entriesArray;
    for (const auto& objectPath : resp)
    {
        dbus::utility::DBusPropertiesMap propsFlattened;
        auto isEntry = std::ranges::find_if(objectPath.second,
                                            [](const auto& object) {
            return object.first == "xyz.openbmc_project.Logging.Entry";
        });
        if (isEntry == objectPath.second.end())
        {
            continue;
        }
        for (const auto& interfaceMap : objectPath.second)
        {
            for (const auto& propertyMap : interfaceMap.second)
            {
                BMCWEB_LOG_ERROR("propertyMap.first: {}", propertyMap.first);
                propsFlattened.emplace_back(propertyMap.first,
                                            propertyMap.second);
            }
        }
        fillManagerEventLogLogEntryFromPropertyMap(asyncResp, propsFlattened,
                                                   entriesArray.emplace_back());
    }

    if constexpr (BMCWEB_SORT_EVENT_LOG)
    {
        std::sort(entriesArray.begin(), entriesArray.end(),
                    [](const nlohmann::json& left,
                        const nlohmann::json& right) {
            int leftId = std::stoi(left["Id"].get<std::string>());
            int rightId = std::stoi(right["Id"].get<std::string>());
            return (leftId < rightId);
        });
    }
    else
    {
        std::sort(entriesArray.begin(), entriesArray.end(),
                    [](const nlohmann::json& left,
                        const nlohmann::json& right) {
            return (left["Id"] <= right["Id"]);
        });
    }
    asyncResp->res.jsonValue["Members@odata.count"] = entriesArray.size();
    asyncResp->res.jsonValue["Members"] = std::move(entriesArray);
}

inline void dbusManagerEventLogEntryCollection(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    // Collections don't include the static data added by SubRoute
    // because it has a duplicate entry for members
    asyncResp->res.jsonValue["@odata.type"] =
        "#LogEntryCollection.LogEntryCollection";
    asyncResp->res.jsonValue["@odata.id"] =
        std::format("/redfish/v1/Managers/{}/LogServices/EventLog/Entries",
                    BMCWEB_REDFISH_MANAGER_URI_NAME);
    asyncResp->res.jsonValue["Name"] = "Manager Event Log Entries";
    asyncResp->res.jsonValue["Description"] =
        "Collection of System Event Log Entries";

    // DBus implementation of EventLog/Entries
    // Make call to Logging Service to find all log entry objects
    sdbusplus::message::object_path path("/xyz/openbmc_project/logging");
    dbus::utility::getAllNameSpaceObjects(
        "xyz.openbmc_project.Logging", path, "Manager",
        "xyz.openbmc_project.Logging.Namespace.ResolvedFilterType.Both",
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::ManagedObjectType& resp) {
        afterLogEntriesGetManagedObjects(asyncResp, ec, resp);
    });
}

inline void
    managerEventLogEntryGet(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            std::string entryID)
{
    dbus::utility::escapePathForDbus(entryID);

    // DBus implementation of EventLog/Entries
    // Make call to Logging Service to find all log entry objects
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, "xyz.openbmc_project.Logging",
        "/xyz/openbmc_project/logging/entry/" + entryID, "",
        [asyncResp, entryID](const boost::system::error_code& ec,
                             const dbus::utility::DBusPropertiesMap& resp) {
        if (ec.value() == EBADR)
        {
            messages::resourceNotFound(asyncResp->res, "EventLogEntry",
                                       entryID);
            return;
        }
        if (ec)
        {
            BMCWEB_LOG_ERROR("EventLogEntry (DBus) resp_handler got error {}",
                             ec);
            messages::internalError(asyncResp->res);
            return;
        }

        fillManagerEventLogLogEntryFromPropertyMap(asyncResp, resp,
                                                   asyncResp->res.jsonValue);
    });
}

inline void requestRoutesMangersEventLogService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/LogServices/EventLog/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& managerId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
        {
            messages::resourceNotFound(asyncResp->res, "Manager", managerId);
            return;
        }

        managerLogServiceEventLogGet(asyncResp);
    });

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Managers/<str>/LogServices/EventLog/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& managerId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
        {
            messages::resourceNotFound(asyncResp->res, "Manager", managerId);
            return;
        }

        dbusManagerEventLogEntryCollection(asyncResp);
    });

    BMCWEB_ROUTE(
        app, "/redfish/v1/Managers/<str>/LogServices/EventLog/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& managerId, const std::string& entryId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
        {
            messages::resourceNotFound(asyncResp->res, "Manager", managerId);
            return;
        }

        managerEventLogEntryGet(asyncResp, entryId);
    });
}

} // namespace redfish
