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
#include "generated/enums/log_entry.hpp"
#include "log_services.hpp"

namespace redfish
{

namespace fs = std::filesystem;

using GetManagedPropertyType =
    boost::container::flat_map<std::string, dbus::utility::DbusVariantType>;

using GetManagedObjectsType = boost::container::flat_map<
    sdbusplus::message::object_path,
    boost::container::flat_map<std::string, GetManagedPropertyType>>;

inline void requestRoutesSELLogService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/SEL/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/LogServices/SEL";
                asyncResp->res.jsonValue["@odata.type"] =
                    "#LogService.v1_1_0.LogService";
                asyncResp->res.jsonValue["Name"] = "SEL Log Service";
                asyncResp->res.jsonValue["Description"] = "IPMI SEL Service";
                asyncResp->res.jsonValue["Id"] = "SEL";
                asyncResp->res.jsonValue["OverWritePolicy"] = "WrapsWhenFull";

                std::pair<std::string, std::string> redfishDateTimeOffset =
                    redfish::time_utils::getDateTimeOffsetNow();

                asyncResp->res.jsonValue["DateTime"] =
                    redfishDateTimeOffset.first;
                asyncResp->res.jsonValue["DateTimeLocalOffset"] =
                    redfishDateTimeOffset.second;

                asyncResp->res.jsonValue["Entries"] = {
                    {"@odata.id",
                     "/redfish/v1/Systems/" +
                         std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                         "/LogServices/SEL/Entries"}};
                asyncResp->res.jsonValue["Actions"]["#LogService.ClearLog"] = {

                    {"target", "/redfish/v1/Systems/" +
                                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                   "/LogServices/SEL/"
                                   "Actions/LogService.ClearLog"}};
            });
}

inline bool isSelEntry(const std::string* message,
                       const std::vector<std::string>* additionalData)
{
    if ((message != nullptr) &&
        (*message == "xyz.openbmc_project.Logging.SEL.Error.Created"))
    {
        return true;
    }
    if (additionalData != nullptr)
    {
        AdditionalData additional(*additionalData);
        if (additional.count("namespace") > 0 &&
            additional["namespace"] == "SEL")
        {
            return true;
        }
    }
    return false;
}
inline void populateRedfishSELEntry(GetManagedPropertyType& resp,
                                    nlohmann::json& thisEntry)
{
    uint32_t* id = nullptr;
    std::time_t timestamp{};
    std::time_t updateTimestamp{};
    std::string* severity = nullptr;
    std::string* eventId = nullptr;
    std::string* message = nullptr;
    std::vector<std::string>* additionalDataVectorString = nullptr;
    std::string generatorId;
    std::string messageId;
    bool resolved = false;
    bool isMessageRegistry = false;
    std::string sensorData;
    std::string deviceName;
    std::ostringstream hexCodeEventDir;
    std::string messageArgs;
    std::string originOfCondition;
    std::string sensorNumber;
    log_entry::SensorType sensorType = log_entry::SensorType::Invalid;
    log_entry::LogEntryCode entryCode = log_entry::LogEntryCode::Invalid;
    const std::string* resolution = nullptr;

    for (auto& propertyMap : resp)
    {
        if (propertyMap.first == "Id")
        {
            id = std::get_if<uint32_t>(&propertyMap.second);
        }
        else if (propertyMap.first == "Timestamp")
        {
            const uint64_t* millisTimeStamp =
                std::get_if<uint64_t>(&propertyMap.second);
            if (millisTimeStamp != nullptr)
            {
                timestamp = redfish::time_utils::getTimestamp(*millisTimeStamp);
            }
        }
        else if (propertyMap.first == "UpdateTimestamp")
        {
            const uint64_t* millisTimeStamp =
                std::get_if<uint64_t>(&propertyMap.second);
            if (millisTimeStamp != nullptr)
            {
                updateTimestamp =
                    redfish::time_utils::getTimestamp(*millisTimeStamp);
            }
        }
        else if (propertyMap.first == "Severity")
        {
            severity = std::get_if<std::string>(&propertyMap.second);
        }
        else if (propertyMap.first == "EventId")
        {
            eventId = std::get_if<std::string>(&propertyMap.second);
        }
        else if (propertyMap.first == "Message")
        {
            message = std::get_if<std::string>(&propertyMap.second);
        }
        else if (propertyMap.first == "Resolved")
        {
            bool* resolveptr = std::get_if<bool>(&propertyMap.second);
            if (resolveptr == nullptr)
            {
                throw std::runtime_error("Invalid SEL Entry");
                return;
            }
            resolved = *resolveptr;
        }
        else if (propertyMap.first == "Resolution")
        {
            resolution = std::get_if<std::string>(&propertyMap.second);
        }
        else if (propertyMap.first == "AdditionalData")
        {
            std::string eventDir;
            std::string recordType;
            additionalDataVectorString =
                std::get_if<std::vector<std::string>>(&propertyMap.second);
            if (additionalDataVectorString != nullptr)
            {
                AdditionalData additional(*additionalDataVectorString);
                if (additional.count("REDFISH_MESSAGE_ID") > 0)
                {
                    isMessageRegistry = true;
                    messageId = additional["REDFISH_MESSAGE_ID"];
                    BMCWEB_LOG_DEBUG("RedFish MessageId: [{}]", messageId);

                    if (additional.count("REDFISH_MESSAGE_ARGS") > 0)
                    {
                        messageArgs = additional["REDFISH_MESSAGE_ARGS"];
                    }
                }
                else
                {
                    if (additional.count("EVENT_DIR") > 0)
                    {
                        hexCodeEventDir
                            << "0x" << std::setfill('0') << std::setw(2)
                            << std::hex << std::stoi(additional["EVENT_DIR"]);
                    }
                    if (additional.count("GENERATOR_ID") > 0)
                    {
                        std::ostringstream hexCodeGeneratorId;
                        if (!additional["GENERATOR_ID"].empty())
                        {
                            hexCodeGeneratorId
                                << "0x" << std::setfill('0') << std::setw(4)
                                << std::hex
                                << std::stoi(additional["GENERATOR_ID"]);
                            generatorId = hexCodeGeneratorId.str();
                        }
                    }
                    if (additional.count("RECORD_TYPE") > 0)
                    {
                        recordType = additional["RECORD_TYPE"];
                    }
                    if (additional.count("SENSOR_DATA") > 0)
                    {
                        sensorData = additional["SENSOR_DATA"];
                        boost::algorithm::to_lower(sensorData);
                    }
                    // MessageId for SEL is of the form 0xNNaabbcc
                    // where 'NN' is the EventDir/EventType byte, aa is first
                    // byte sensor data, bb is second byte sensor data, cc is
                    // third byte sensor data
                    messageId = hexCodeEventDir.str() + sensorData;
                    BMCWEB_LOG_DEBUG("SEL MessageId: [{}]", messageId);
                }
                if (additional.end() != additional.find("EVENT_DIR"))
                {
                    uint8_t eventDir = 0;
                    std::string_view eventDirView = additional["EVENT_DIR"];
                    std::from_chars_result result = std::from_chars(
                        eventDirView.begin(), eventDirView.end(), eventDir);
                    if (result.ec != std::errc{} ||
                        result.ptr != eventDirView.end())
                    {
                        BMCWEB_LOG_DEBUG("SEL EVENT_DIR value not found");
                    }
                    else
                    {
                        if (eventDir)
                        {
                            entryCode = log_entry::LogEntryCode::Assert;
                        }
                        else
                        {
                            entryCode = log_entry::LogEntryCode::Deassert;
                        }
                    }
                }
                if (additional.end() != additional.find("SENSOR_TYPE"))
                {
                    nlohmann::json sensorTypeJson = additional["SENSOR_TYPE"];
                    sensorType = sensorTypeJson.get<log_entry::SensorType>();
                }
                if (additional.end() != additional.find("SENSOR_NUMBER"))
                {
                    sensorNumber = additional["SENSOR_NUMBER"];
                }
                if (additional.count("REDFISH_ORIGIN_OF_CONDITION") > 0)
                {
                    originOfCondition =
                        additional["REDFISH_ORIGIN_OF_CONDITION"];
                }
                if (additional.count("DEVICE_NAME") > 0)
                {
                    deviceName = additional["DEVICE_NAME"];
                }
            }
        }
    }
    if (id == nullptr || message == nullptr || severity == nullptr)
    {
        throw std::runtime_error("Invalid SEL Entry");
        return;
    }
    if (!isSelEntry(message, additionalDataVectorString))
    {
        return;
    }
    if (isMessageRegistry)
    {
        message_registries::generateMessageRegistry(
            thisEntry,
            "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                "/LogServices/"
                "SEL/Entries/",
            "v1_13_0", std::to_string(*id), "System Event Log Entry",
            redfish::time_utils::getDateTimeStdtime(timestamp), messageId,
            messageArgs, *resolution, resolved,
            (eventId == nullptr) ? "" : *eventId, deviceName, *severity);
        thisEntry["EntryType"] = "SEL";
    }

    // generateMessageRegistry will not create the entry if
    // the messageId can't be found in message registries.
    // So check the entry 'Id' anyway to cover that case.
    if (thisEntry["Id"].size() == 0)
    {
        thisEntry["@odata.type"] = "#LogEntry.v1_15_0.LogEntry";
        thisEntry["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/LogServices/SEL/"
            "Entries/" +
            std::to_string(*id);
        thisEntry["Name"] = "System Event Log Entry";
        thisEntry["Id"] = std::to_string(*id);
        if (!generatorId.empty())
        {
            thisEntry["GeneratorId"] = generatorId;
        }
        if (!messageId.empty())
        {
            thisEntry["MessageId"] = messageId;
        }
        thisEntry["Message"] = *message;
        thisEntry["Resolved"] = resolved;
        thisEntry["EntryType"] = "SEL";
        thisEntry["Severity"] = translateSeverityDbusToRedfish(*severity);
        thisEntry["Created"] =
            redfish::time_utils::getDateTimeStdtime(timestamp);
        thisEntry["Modified"] =
            redfish::time_utils::getDateTimeStdtime(updateTimestamp);
        if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
        {
            if ((eventId != nullptr && !eventId->empty()) ||
                !deviceName.empty())
            {
                nlohmann::json oem = {
                    {"Oem",
                     {{"Nvidia",
                       {{"@odata.type",
                         "#NvidiaLogEntry.v1_1_0.NvidiaLogEntry"}}}}}};
                if (!deviceName.empty())
                {
                    oem["Oem"]["Nvidia"]["Device"] = deviceName;
                }
                if (eventId != nullptr && !eventId->empty())
                {
                    oem["Oem"]["Nvidia"]["ErrorId"] = std::string(*eventId);
                }
                thisEntry.update(oem);
            }
        } // BMCWEB_NVIDIA_OEM_PROPERTIES
    }
    if (log_entry::SensorType::Invalid != sensorType)
    {
        thisEntry["SensorType"] = sensorType;
    }
    if (!sensorNumber.empty())
    {
        uint8_t number = 0;
        std::string_view sensorNumberView = sensorNumber;
        std::from_chars_result result = std::from_chars(
            sensorNumberView.begin(), sensorNumberView.end(), number);
        if (result.ec != std::errc{} || result.ptr != sensorNumberView.end())
        {
            BMCWEB_LOG_DEBUG("SEL sensor number value not found");
        }
        else
        {
            thisEntry["SensorNumber"] = number;
        }
    }
    if (log_entry::LogEntryCode::Invalid != entryCode)
    {
        thisEntry["EntryCode"] = entryCode;
    }
}

inline void requestRoutesDBusSELLogEntryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/SEL/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            [[maybe_unused]] const std::string& systemName) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            // Collections don't include the static data added by
            // SubRoute because it has a duplicate entry for members
            asyncResp->res.jsonValue["@odata.type"] =
                "#LogEntryCollection.LogEntryCollection";
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                "/LogServices/SEL/Entries";
            asyncResp->res.jsonValue["Name"] = "System Event Log Entries";
            asyncResp->res.jsonValue["Description"] =
                "Collection of System Event Log Entries";

            // DBus implementation of SEL/Entries
            // Make call to Logging Service to find all log entry
            // objects
            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec,
                            GetManagedObjectsType& resp) {
                    if (ec)
                    {
                        // TODO Handle for specific error code
                        BMCWEB_LOG_ERROR(
                            "getLogEntriesIfaceData resp_handler got error {}",
                            ec);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    nlohmann::json& entriesArray =
                        asyncResp->res.jsonValue["Members"];
                    entriesArray = nlohmann::json::array();
                    for (auto& objectPath : resp)
                    {
                        nlohmann::json thisEntry = nlohmann::json::object();
                        for (auto& interfaceMap : objectPath.second)
                        {
                            if (interfaceMap.first ==
                                "xyz.openbmc_project.Logging.Entry")
                            {
                                try
                                {
                                    populateRedfishSELEntry(interfaceMap.second,
                                                            thisEntry);
                                    if (!thisEntry.empty())
                                    {
                                        entriesArray.push_back(thisEntry);
                                    }
                                }
                                catch (
                                    [[maybe_unused]] const std::runtime_error&
                                        e)
                                {
                                    messages::internalError(asyncResp->res);
                                    continue;
                                }
                            }
                        }
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
                    asyncResp->res.jsonValue["Members@odata.count"] =
                        entriesArray.size();
                },
                "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
                "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
        });
}

inline void
    deleteDbusLogEntry(const std::string& entryId,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    auto respHandler = [asyncResp](const boost::system::error_code ec) {
        if (ec)
        {
            asyncResp->res.result(
                boost::beast::http::status::internal_server_error);
            return;
        }
        asyncResp->res.result(boost::beast::http::status::no_content);
    };
    crow::connections::systemBus->async_method_call(
        respHandler, "xyz.openbmc_project.Logging",
        "/xyz/openbmc_project/logging/entry/" + entryId,
        "xyz.openbmc_project.Object.Delete", "Delete");
}

inline void deleteDbusSELEntry(
    std::string& entryID, const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, entryID](const boost::system::error_code ec,
                             GetManagedPropertyType& resp) {
            if (ec.value() == EBADR)
            {
                messages::resourceNotFound(asyncResp->res, "SELLogEntry",
                                           entryID);

                return;
            }
            if (ec)
            {
                BMCWEB_LOG_ERROR("SELLogEntry (DBus) resp_handler got error {}",
                                 ec);
                messages::internalError(asyncResp->res);
                return;
            }
            uint32_t* id = nullptr;
            std::string* message = nullptr;
            const std::vector<std::string>* additionalData = nullptr;

            for (auto& propertyMap : resp)
            {
                if (propertyMap.first == "Id")
                {
                    id = std::get_if<uint32_t>(&propertyMap.second);
                }

                else if (propertyMap.first == "Message")
                {
                    message = std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "AdditionalData")
                {
                    additionalData = std::get_if<std::vector<std::string>>(
                        &propertyMap.second);
                }
            }
            if (id == nullptr || message == nullptr)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            if (isSelEntry(message, additionalData))
            {
                deleteDbusLogEntry(entryID, asyncResp);
                return;
            }
            messages::resourceNotFound(asyncResp->res, "SELLogEntry", entryID);
            return;
        },
        "xyz.openbmc_project.Logging",
        "/xyz/openbmc_project/logging/entry/" + entryID,
        "org.freedesktop.DBus.Properties", "GetAll", "");
}

inline void requestRoutesDBusSELLogEntry(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/LogServices/SEL/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& param)

            {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
                {
                    return;
                }
                std::string entryID = param;
                dbus::utility::escapePathForDbus(entryID);

                // DBus implementation of EventLog/Entries
                // Make call to Logging Service to find all log entry
                // objects
                crow::connections::systemBus->async_method_call(
                    [asyncResp, entryID](const boost::system::error_code ec,
                                         GetManagedPropertyType& resp) {
                        if (ec.value() == EBADR)
                        {
                            messages::resourceNotFound(asyncResp->res,
                                                       "SELLogEntry", entryID);
                            return;
                        }
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR(
                                "SELLogEntry (DBus) resp_handler got error {}",
                                ec);
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        nlohmann::json& thisEntry = asyncResp->res.jsonValue;
                        thisEntry = nlohmann::json::object();
                        try
                        {
                            populateRedfishSELEntry(resp, thisEntry);
                        }
                        catch ([[maybe_unused]] const std::runtime_error& e)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                    },
                    "xyz.openbmc_project.Logging",
                    "/xyz/openbmc_project/logging/entry/" + entryID,
                    "org.freedesktop.DBus.Properties", "GetAll", "");
            });

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/LogServices/SEL/Entries/<str>/")
        .privileges(redfish::privileges::deleteLogEntry)
        .methods(boost::beast::http::verb::delete_)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& param) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::string entryID = param;

                dbus::utility::escapePathForDbus(entryID);
                deleteDbusSELEntry(entryID, asyncResp);
            });
}

inline void requestRoutesDBusSELLogServiceActionsClear(App& app)
{
    /**
     * Function handles POST method request.
     * The Clear Log actions does not require any parameter.The action deletes
     * all entries found in the Entries collection for this Log Service.
     */

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/SEL/Actions/"
                      "LogService.ClearLog/")
        .privileges(redfish::privileges::postLogService)
        .methods(
            boost::beast::http::verb::
                post)([&app](
                          const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          [[maybe_unused]] const std::string& systemName) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec,
                            GetManagedObjectsType& resp) {
                    if (ec)
                    {
                        // TODO Handle for specific error code
                        BMCWEB_LOG_ERROR(
                            "getLogEntriesIfaceData resp_handler got error {}",
                            ec);
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (auto& objectPath : resp)
                    {
                        uint32_t* id = nullptr;
                        std::string* message = nullptr;
                        const std::vector<std::string>* additionalData =
                            nullptr;

                        for (auto& interfaceMap : objectPath.second)
                        {
                            if (interfaceMap.first ==
                                "xyz.openbmc_project.Logging.Entry")
                            {
                                for (auto& propertyMap : interfaceMap.second)
                                {
                                    if (propertyMap.first == "Id")
                                    {
                                        id = std::get_if<uint32_t>(
                                            &propertyMap.second);
                                    }
                                    else if (propertyMap.first == "Message")
                                    {
                                        message = std::get_if<std::string>(
                                            &propertyMap.second);
                                    }
                                    else if (propertyMap.first ==
                                             "AdditionalData")
                                    {
                                        additionalData = std::get_if<
                                            std::vector<std::string>>(
                                            &propertyMap.second);
                                    }
                                }
                                if (id == nullptr || message == nullptr)
                                {
                                    messages::internalError(asyncResp->res);
                                    continue;
                                }
                                if (isSelEntry(message, additionalData))
                                {
                                    std::string entryId = std::to_string(*id);
                                    deleteDbusLogEntry(entryId, asyncResp);
                                }
                            }
                        }
                    }
                },
                "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
                "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
        });
}
} // namespace redfish
