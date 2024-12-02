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

#include "app.hpp"

namespace redfish
{

inline void requestRoutesChassisXIDLogService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/LogServices/XID/")
        .privileges(redfish::privileges::getLogService)
        .methods(
            boost::beast::http::verb::get)([](const crow::Request&,
                                              const std::shared_ptr<
                                                  bmcweb::AsyncResp>& asyncResp,
                                              const std::string& chassisId) {
            const std::array<const char*, 2> interfaces = {
                "xyz.openbmc_project.Inventory.Item.Board",
                "xyz.openbmc_project.Inventory.Item.Chassis"};
            crow::connections::systemBus->async_method_call(
                [asyncResp, chassisId(std::string(chassisId))](
                    const boost::system::error_code ec,
                    const crow::openbmc_mapper::GetSubTreeType& subtree) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    // Iterate over all retrieved ObjectPaths.
                    for (const std::pair<
                             std::string,
                             std::vector<std::pair<std::string,
                                                   std::vector<std::string>>>>&
                             object : subtree)
                    {
                        const std::string& path = object.first;
                        const std::vector<
                            std::pair<std::string, std::vector<std::string>>>&
                            connectionNames = object.second;

                        const std::string& connectionName =
                            connectionNames[0].first;

                        sdbusplus::message::object_path objPath(path);
                        if (objPath.filename() != chassisId)
                        {
                            continue;
                        }
                        asyncResp->res.jsonValue["@odata.id"] =
                            "/redfish/v1/Chassis/" + chassisId +
                            "/LogServices/XID";
                        asyncResp->res.jsonValue["@odata.type"] =
                            "#LogService.v1_1_0.LogService";
                        asyncResp->res.jsonValue["Name"] = "XID Log Service";
                        asyncResp->res.jsonValue["Description"] =
                            "XID Log Service";
                        asyncResp->res.jsonValue["Id"] = "XID";
                        asyncResp->res.jsonValue["OverWritePolicy"] =
                            "WrapsWhenFull";

                        std::pair<std::string, std::string>
                            redfishDateTimeOffset =
                                redfish::time_utils::getDateTimeOffsetNow();

                        asyncResp->res.jsonValue["DateTime"] =
                            redfishDateTimeOffset.first;
                        asyncResp->res.jsonValue["DateTimeLocalOffset"] =
                            redfishDateTimeOffset.second;

                        const std::string inventoryItemInterface =
                            "xyz.openbmc_project.Inventory.Item";

                        sdbusplus::asio::getProperty<std::string>(
                            *crow::connections::systemBus, connectionName, path,
                            inventoryItemInterface, "PrettyName",
                            [asyncResp, chassisId(std::string(chassisId))](
                                const boost::system::error_code ec,
                                const std::string& chassisName) {
                                if (ec)
                                {
                                    BMCWEB_LOG_DEBUG(
                                        "DBus response error for PrettyName");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                // Call Phosphor-logging GetStats method to get
                                // LatestEntryTimestamp and LatestEntryID
                                crow::connections::systemBus->async_method_call(
                                    [asyncResp](
                                        const boost::system::error_code ec,
                                        const std::tuple<uint32_t, uint64_t>&
                                            reqData) {
                                        if (ec)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Failed to get Data from xyz.openbmc_project.Logging GetStats: {}",
                                                ec);
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }
                                        auto lastTimeStamp =
                                            redfish::time_utils::getTimestamp(
                                                std::get<1>(reqData));
                                        asyncResp->res
                                            .jsonValue["Oem"]["Nvidia"]
                                                      ["@odata.type"] =
                                            "#NvidiaLogService.v1_3_0.NvidiaLogService";
                                        asyncResp->res
                                            .jsonValue["Oem"]["Nvidia"]
                                                      ["LatestEntryID"] =
                                            std::to_string(
                                                std::get<0>(reqData));
                                        asyncResp->res
                                            .jsonValue["Oem"]["Nvidia"]
                                                      ["LatestEntryTimeStamp"] =
                                            redfish::time_utils::
                                                getDateTimeStdtime(
                                                    lastTimeStamp);
                                    },
                                    "xyz.openbmc_project.Logging",
                                    "/xyz/openbmc_project/logging",
                                    "xyz.openbmc_project.Logging.Namespace",
                                    "GetStats", chassisName + "_XID");
                            });
                        if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                        {
                            if (BMCWEB_NVIDIA_BOOTENTRYID)
                            {
                                populateBootEntryId(asyncResp->res);
                            }
                        } // BMCWEB_NVIDIA_OEM_PROPERTIES
                        asyncResp->res.jsonValue["Entries"] = {
                            {"@odata.id", "/redfish/v1/Chassis/" + chassisId +
                                              "/LogServices/XID/Entries"}};
                        return;
                    }
                    // Couldn't find an object with that name.  return an
                    // error
                    messages::resourceNotFound(
                        asyncResp->res, "#Chassis.v1_17_0.Chassis", chassisId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/inventory", 0, interfaces);
        });
}

inline void requestRoutesChassisXIDLogEntryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/LogServices/XID/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(
            boost::beast::http::verb::get)([](const crow::Request&,
                                              const std::shared_ptr<
                                                  bmcweb::AsyncResp>& asyncResp,
                                              const std::string& chassisId) {
            const std::array<const char*, 2> interfaces = {
                "xyz.openbmc_project.Inventory.Item.Board",
                "xyz.openbmc_project.Inventory.Item.Chassis"};

            crow::connections::systemBus->async_method_call(
                [asyncResp, chassisId(std::string(chassisId))](
                    const boost::system::error_code ec,
                    const crow::openbmc_mapper::GetSubTreeType& subtree) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    // Iterate over all retrieved ObjectPaths.
                    for (const std::pair<
                             std::string,
                             std::vector<std::pair<std::string,
                                                   std::vector<std::string>>>>&
                             object : subtree)
                    {
                        const std::string& path = object.first;
                        const std::vector<
                            std::pair<std::string, std::vector<std::string>>>&
                            connectionNames = object.second;

                        sdbusplus::message::object_path objPath(path);
                        if (objPath.filename() != chassisId)
                        {
                            continue;
                        }

                        const std::string& connectionName =
                            connectionNames[0].first;
                        const std::vector<std::string>& interfaces2 =
                            connectionNames[0].second;

                        const std::string inventoryItemInterface =
                            "xyz.openbmc_project.Inventory.Item";
                        if (std::find(interfaces2.begin(), interfaces2.end(),
                                      inventoryItemInterface) !=
                            interfaces2.end())
                        {
                            sdbusplus::asio::getProperty<std::string>(
                                *crow::connections::systemBus, connectionName,
                                path, inventoryItemInterface, "PrettyName",
                                [asyncResp, chassisId(std::string(chassisId))](
                                    const boost::system::error_code ec,
                                    const std::string& chassisName) {
                                    if (ec)
                                    {
                                        BMCWEB_LOG_DEBUG(
                                            "DBus response error for PrettyName");
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }

                                    BMCWEB_LOG_DEBUG("PrettyName: {}",
                                                     chassisName);
                                    // Collections don't include the static data
                                    // added by SubRoute because it has a
                                    // duplicate entry for members
                                    asyncResp->res.jsonValue["@odata.type"] =
                                        "#LogEntryCollection.LogEntryCollection";
                                    asyncResp->res.jsonValue["@odata.id"] =
                                        "/redfish/v1/Chassis/" + chassisId +
                                        "/LogServices/XID/Entries";
                                    asyncResp->res.jsonValue["Name"] =
                                        "XID Log Entries";
                                    asyncResp->res.jsonValue["Description"] =
                                        "Collection of XID Log Entries";
                                    asyncResp->res
                                        .jsonValue["Members@odata.count"] = 0;

                                    BMCWEB_LOG_DEBUG("Namespace: {}_XID",
                                                     chassisName);

                                    // DBus implementation of EventLog/Entries
                                    // Make call to Logging Service to find all
                                    // log entry objects
                                    crow::
                                        connections::
                                            systemBus
                                                ->async_method_call(
                                                    [asyncResp,
                                                     chassisId(
                                                         std::string(
                                                             chassisId))](const boost::system::error_code ec, const GetManagedObjectsType& resp) {
                                                        if (ec)
                                                        {
                                                            // TODO Handle for
                                                            // specific error
                                                            // code
                                                            BMCWEB_LOG_ERROR(
                                                                "getLogEntriesIfaceData resp_handler got error {}",
                                                                ec.message());
                                                            messages::
                                                                internalError(
                                                                    asyncResp
                                                                        ->res);
                                                            return;
                                                        }

                                                        const uint32_t* id =
                                                            nullptr;
                                                        std::time_t timestamp{};
                                                        std::time_t
                                                            updateTimestamp{};
                                                        const std::string*
                                                            severity = nullptr;
                                                        const std::string*
                                                            eventId = nullptr;
                                                        const std::string*
                                                            message = nullptr;
                                                        const std::string*
                                                            filePath = nullptr;
                                                        bool resolved = false;
                                                        const std::string*
                                                            resolution =
                                                                nullptr;
                                                        const std::vector<
                                                            std::string>*
                                                            additionalDataRaw =
                                                                nullptr;

                                                        nlohmann::json&
                                                            entriesArray =
                                                                asyncResp->res.jsonValue
                                                                    ["Members"];
                                                        entriesArray =
                                                            nlohmann::json::
                                                                array();

                                                        for (auto& objectPath :
                                                             resp)
                                                        {
                                                            nlohmann::json
                                                                thisEntry =
                                                                    nlohmann::json::
                                                                        object();

                                                            for (
                                                                auto&
                                                                    interfaceMap :
                                                                objectPath
                                                                    .second)
                                                            {
                                                                if (interfaceMap
                                                                        .first ==
                                                                    "xyz.openbmc_project.Logging.Entry")
                                                                {
                                                                    nlohmann::json thisEntry =
                                                                        nlohmann::json::
                                                                            object();

                                                                    for (
                                                                        auto&
                                                                            propertyMap :
                                                                        interfaceMap
                                                                            .second)
                                                                    {
                                                                        if (propertyMap
                                                                                .first ==
                                                                            "Id")
                                                                        {
                                                                            id = std::get_if<
                                                                                uint32_t>(
                                                                                &propertyMap
                                                                                     .second);
                                                                        }
                                                                        else if (
                                                                            propertyMap
                                                                                .first ==
                                                                            "Timestamp")
                                                                        {
                                                                            const uint64_t* millisTimeStamp =
                                                                                std::get_if<
                                                                                    uint64_t>(
                                                                                    &propertyMap
                                                                                         .second);
                                                                            if (millisTimeStamp !=
                                                                                nullptr)
                                                                            {
                                                                                timestamp = redfish::
                                                                                    time_utils::getTimestamp(
                                                                                        *millisTimeStamp);
                                                                            }
                                                                        }
                                                                        else if (
                                                                            propertyMap
                                                                                .first ==
                                                                            "UpdateTimestamp")
                                                                        {
                                                                            const uint64_t* millisTimeStamp =
                                                                                std::get_if<
                                                                                    uint64_t>(
                                                                                    &propertyMap
                                                                                         .second);
                                                                            if (millisTimeStamp !=
                                                                                nullptr)
                                                                            {
                                                                                updateTimestamp =
                                                                                    redfish::time_utils::
                                                                                        getTimestamp(
                                                                                            *millisTimeStamp);
                                                                            }
                                                                        }
                                                                        else if (
                                                                            propertyMap
                                                                                .first ==
                                                                            "Severity")
                                                                        {
                                                                            severity = std::get_if<
                                                                                std::
                                                                                    string>(
                                                                                &propertyMap
                                                                                     .second);
                                                                        }
                                                                        else if (
                                                                            propertyMap
                                                                                .first ==
                                                                            "Message")
                                                                        {
                                                                            message = std::get_if<
                                                                                std::
                                                                                    string>(
                                                                                &propertyMap
                                                                                     .second);
                                                                        }
                                                                        else if (
                                                                            propertyMap
                                                                                .first ==
                                                                            "Resolved")
                                                                        {
                                                                            const bool* resolveptr =
                                                                                std::get_if<
                                                                                    bool>(
                                                                                    &propertyMap
                                                                                         .second);
                                                                            if (resolveptr ==
                                                                                nullptr)
                                                                            {
                                                                                messages::internalError(
                                                                                    asyncResp
                                                                                        ->res);
                                                                                return;
                                                                            }
                                                                            resolved =
                                                                                *resolveptr;
                                                                        }
                                                                        else if (
                                                                            propertyMap
                                                                                .first ==
                                                                            "Resolution")
                                                                        {
                                                                            resolution = std::get_if<
                                                                                std::
                                                                                    string>(
                                                                                &propertyMap
                                                                                     .second);
                                                                        }
                                                                        else if (
                                                                            propertyMap
                                                                                .first ==
                                                                            "AdditionalData")
                                                                        {
                                                                            additionalDataRaw = std::get_if<
                                                                                std::vector<
                                                                                    std::
                                                                                        string>>(
                                                                                &propertyMap
                                                                                     .second);
                                                                        }
                                                                        else if (
                                                                            propertyMap
                                                                                .first ==
                                                                            "Path")
                                                                        {
                                                                            filePath = std::get_if<
                                                                                std::
                                                                                    string>(
                                                                                &propertyMap
                                                                                     .second);
                                                                        }
                                                                        else if (
                                                                            propertyMap
                                                                                .first ==
                                                                            "EventId")
                                                                        {
                                                                            eventId = std::get_if<
                                                                                std::
                                                                                    string>(
                                                                                &propertyMap
                                                                                     .second);
                                                                        }
                                                                    }
                                                                    if (id ==
                                                                            nullptr ||
                                                                        message ==
                                                                            nullptr ||
                                                                        severity ==
                                                                            nullptr)
                                                                    {
                                                                        messages::internalError(
                                                                            asyncResp
                                                                                ->res);
                                                                        return;
                                                                    }

                                                                    // Determine
                                                                    // if it's a
                                                                    // message
                                                                    // registry
                                                                    // format or
                                                                    // not.
                                                                    bool isMessageRegistry =
                                                                        false;
                                                                    std::string
                                                                        messageId;
                                                                    std::string
                                                                        messageArgs;
                                                                    std::string
                                                                        originOfCondition;
                                                                    std::string
                                                                        deviceName;
                                                                    if (additionalDataRaw !=
                                                                        nullptr)
                                                                    {
                                                                        AdditionalData
                                                                            additional(
                                                                                *additionalDataRaw);
                                                                        if (additional
                                                                                .count(
                                                                                    "REDFISH_MESSAGE_ID") >
                                                                            0)
                                                                        {
                                                                            isMessageRegistry =
                                                                                true;
                                                                            messageId = additional
                                                                                ["REDFISH_MESSAGE_ID"];
                                                                            BMCWEB_LOG_DEBUG(
                                                                                "MessageId: [{}]",
                                                                                messageId);

                                                                            if (additional
                                                                                    .count(
                                                                                        "REDFISH_MESSAGE_ARGS") >
                                                                                0)
                                                                            {
                                                                                messageArgs = additional
                                                                                    ["REDFISH_MESSAGE_ARGS"];
                                                                            }
                                                                        }
                                                                        if (additional
                                                                                .count(
                                                                                    "REDFISH_ORIGIN_OF_CONDITION") >
                                                                            0)
                                                                        {
                                                                            originOfCondition = additional
                                                                                ["REDFISH_ORIGIN_OF_CONDITION"];
                                                                        }
                                                                        if (additional
                                                                                .count(
                                                                                    "DEVICE_NAME") >
                                                                            0)
                                                                        {
                                                                            deviceName = additional
                                                                                ["DEVICE_NAME"];
                                                                        }
                                                                    }
                                                                    if (isMessageRegistry)
                                                                    {
                                                                        message_registries::generateMessageRegistry(
                                                                            thisEntry,
                                                                            "/redfish/v1/Systems/" +
                                                                                std::string(
                                                                                    BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                                                                "/LogServices/"
                                                                                "EventLog/Entries/",
                                                                            "v1_13_0",
                                                                            std::to_string(
                                                                                *id),
                                                                            "System Event Log Entry",
                                                                            redfish::time_utils::
                                                                                getDateTimeStdtime(
                                                                                    timestamp),
                                                                            messageId,
                                                                            messageArgs,
                                                                            *resolution,
                                                                            resolved,
                                                                            (eventId ==
                                                                             nullptr)
                                                                                ? ""
                                                                                : *eventId,
                                                                            deviceName,
                                                                            *severity);

                                                                        if constexpr (
                                                                            !BMCWEB_DISABLE_HEALTH_ROLLUP)
                                                                        {
                                                                            origin_utils::convertDbusObjectToOriginOfCondition(
                                                                                originOfCondition,
                                                                                std::to_string(
                                                                                    *id),
                                                                                asyncResp,
                                                                                thisEntry,
                                                                                deviceName);
                                                                        }
                                                                    } // BMCWEB_DISABLE_HEALTH_ROLLUP

                                                                    // generateMessageRegistry
                                                                    // will not
                                                                    // create
                                                                    // the entry
                                                                    // if the
                                                                    // messageId
                                                                    // can't be
                                                                    // found in
                                                                    // message
                                                                    // registries.
                                                                    // So check
                                                                    // the entry
                                                                    // 'Id'
                                                                    // anyway to
                                                                    // cover
                                                                    // that
                                                                    // case.
                                                                    if (thisEntry["Id"]
                                                                            .size() ==
                                                                        0)
                                                                    {
                                                                        thisEntry
                                                                            ["@odata.type"] =
                                                                                "#LogEntry.v1_13_0.LogEntry";
                                                                        thisEntry["@odata.id"] =
                                                                            "/redfish/v1/Systems/" +
                                                                            std::string(
                                                                                BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                                                            "/LogServices/"
                                                                            "EventLog/"
                                                                            "Entries/" +
                                                                            std::to_string(
                                                                                *id);
                                                                        thisEntry
                                                                            ["Name"] =
                                                                                "System Event Log Entry";
                                                                        thisEntry["Id"] =
                                                                            std::to_string(
                                                                                *id);
                                                                        thisEntry
                                                                            ["Message"] =
                                                                                *message;
                                                                        thisEntry
                                                                            ["Resolved"] =
                                                                                resolved;
                                                                        thisEntry
                                                                            ["EntryType"] =
                                                                                "Event";
                                                                        thisEntry["Severity"] =
                                                                            translateSeverityDbusToRedfish(
                                                                                *severity);
                                                                        thisEntry["Created"] =
                                                                            redfish::time_utils::
                                                                                getDateTimeStdtime(
                                                                                    timestamp);
                                                                        thisEntry["Modified"] =
                                                                            redfish::time_utils::
                                                                                getDateTimeStdtime(
                                                                                    updateTimestamp);
                                                                    }
                                                                    if constexpr (
                                                                        BMCWEB_NVIDIA_OEM_PROPERTIES)
                                                                    {
                                                                        if ((eventId !=
                                                                                 nullptr &&
                                                                             !eventId
                                                                                  ->empty()) ||
                                                                            !deviceName
                                                                                 .empty())
                                                                        {
                                                                            nlohmann::json oem = {
                                                                                {"Oem",
                                                                                 {{"Nvidia",
                                                                                   {{"@odata.type",
                                                                                     "#NvidiaLogEntry.v1_1_0.NvidiaLogEntry"}}}}}};
                                                                            if (!deviceName
                                                                                     .empty())
                                                                            {
                                                                                oem["Oem"]
                                                                                   ["Nvidia"]
                                                                                   ["Device"] =
                                                                                       deviceName;
                                                                            }
                                                                            if (eventId !=
                                                                                    nullptr &&
                                                                                !eventId
                                                                                     ->empty())
                                                                            {
                                                                                oem["Oem"]
                                                                                   ["Nvidia"]
                                                                                   ["ErrorId"] =
                                                                                       std::string(
                                                                                           *eventId);
                                                                            }
                                                                            thisEntry
                                                                                .update(
                                                                                    oem);
                                                                        }
                                                                    } // BMCWEB_NVIDIA_OEM_PROPERTIES
                                                                    if (filePath !=
                                                                        nullptr)
                                                                    {
                                                                        thisEntry["AdditionalDataURI"] =
                                                                            getLogEntryAdditionalDataURI(
                                                                                std::to_string(
                                                                                    *id));
                                                                    }
                                                                    entriesArray
                                                                        .push_back(
                                                                            thisEntry);
                                                                    asyncResp
                                                                        ->res
                                                                        .jsonValue
                                                                            ["Members@odata.count"] =
                                                                        entriesArray
                                                                            .size();
                                                                }
                                                            }
                                                        }
                                                        std::sort(
                                                            entriesArray
                                                                .begin(),
                                                            entriesArray.end(),
                                                            [](const nlohmann::
                                                                   json& left,
                                                               const nlohmann::
                                                                   json&
                                                                       right) {
                                                                return (
                                                                    left["Id"] <=
                                                                    right
                                                                        ["Id"]);
                                                            });
                                                    },
                                                    "xyz.openbmc_project.Logging",
                                                    "/xyz/openbmc_project/logging",
                                                    "xyz.openbmc_project.Logging.Namespace",
                                                    "GetAll",
                                                    chassisName + "_XID",
                                                    "xyz.openbmc_project.Logging.Namespace.ResolvedFilterType.Both");
                                    return;
                                });
                            return;
                        }
                    }
                    messages::resourceNotFound(
                        asyncResp->res, "#Chassis.v1_15_0.Chassis", chassisId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/inventory", 0, interfaces);
        });
}
} // namespace redfish
