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
#include "debug_token/dot_request.hpp"
#include "debug_token/request.hpp"
#include "debug_token/status.hpp"
#include "debug_token/task_utils.hpp"
#include "nvidia_messages.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/hex_utils.hpp"

namespace redfish
{
// vector containing debug token-related functionalities'
// (GetDebugTokenRequest, GetDebugTokenStatus) output data
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::vector<std::tuple<std::string, std::string>> debugTokenData;
static constexpr const uint32_t debugTokenTaskTimeoutSec{300};

inline void requestRoutesDebugToken(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/DebugTokenService")
        .privileges(redfish::privileges::getLogEntry)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            [[maybe_unused]] const std::string& systemName) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
            {
                messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                           systemName);
                return;
            }
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                "/LogServices/DebugTokenService";
            asyncResp->res.jsonValue["@odata.type"] =
                "#LogService.v1_2_0.LogService";
            asyncResp->res.jsonValue["Name"] = "Debug Token Service";
            asyncResp->res.jsonValue["Description"] = "Debug Token Service";
            asyncResp->res.jsonValue["Id"] = "DebugTokenService";

            std::pair<std::string, std::string> redfishDateTimeOffset =
                redfish::time_utils::getDateTimeOffsetNow();
            asyncResp->res.jsonValue["DateTime"] = redfishDateTimeOffset.first;
            asyncResp->res.jsonValue["DateTimeLocalOffset"] =
                redfishDateTimeOffset.second;
            asyncResp->res.jsonValue["Entries"] = {
                {"@odata.id", "/redfish/v1/Systems/" +
                                  std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                  "/LogServices/DebugTokenService/Entries"}};
            asyncResp->res.jsonValue["Actions"] = {
                {"#LogService.CollectDiagnosticData",
                 {{"target",
                   "/redfish/v1/Systems/" +
                       std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                       "/LogServices/DebugTokenService/LogService.CollectDiagnosticData"}}}};
        });
}

inline void requestRoutesDebugTokenServiceEntryCollection(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/LogServices/DebugTokenService/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
                {
                    messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                               systemName);
                    return;
                }
                asyncResp->res.jsonValue["@odata.type"] =
                    "#LogEntryCollection.LogEntryCollection";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/LogServices/DebugTokenService/Entries";
                asyncResp->res.jsonValue["Name"] =
                    "Debug Token Service Entries";
                asyncResp->res.jsonValue["Description"] =
                    "Collection of Debug Token Service Entries";
                asyncResp->res.jsonValue["Members@odata.count"] =
                    debugTokenData.size();

                nlohmann::json& entriesArray =
                    asyncResp->res.jsonValue["Members"];
                entriesArray = nlohmann::json::array();
                auto entryID = 0;
                for (auto& objects : debugTokenData)
                {
                    nlohmann::json::object_t thisEntry;

                    thisEntry["@odata.type"] = "#LogEntry.v1_15_0.LogEntry";
                    thisEntry["@odata.id"] =
                        "/redfish/v1/Systems/" +
                        std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                        "/LogServices/DebugTokenService/Entries/" +
                        std::to_string(entryID);
                    thisEntry["Id"] = std::to_string(entryID);
                    thisEntry["EntryType"] = "Oem";
                    thisEntry["Name"] = "Debug Token Entry";
                    thisEntry["DiagnosticDataType"] = "OEM";
                    thisEntry["OEMDiagnosticDataType"] = std::get<0>(objects);
                    thisEntry["AdditionalDataSizeBytes"] =
                        std::get<1>(objects).length();
                    thisEntry["AdditionalDataURI"] =
                        "/redfish/v1/Systems/" +
                        std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                        "/LogServices/DebugTokenService/Entries/" +
                        std::to_string(entryID) + "/attachment";
                    entriesArray.push_back(std::move(thisEntry));
                    entryID++;
                }
            });
}

inline void requestRoutesDebugTokenServiceEntry(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/DebugTokenService/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            [[maybe_unused]] const std::string& systemName,
                            const std::string& idstr) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
            {
                messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                           systemName);
                return;
            }
            std::string_view accept = req.getHeaderValue("Accept");
            if (!accept.empty() &&
                !http_helpers::isContentTypeAllowed(
                    req.getHeaderValue("Accept"),
                    http_helpers::ContentType::OctetStream, true))
            {
                asyncResp->res.result(boost::beast::http::status::bad_request);
                return;
            }

            std::optional<uint64_t> parsedId = stringToUint64(idstr);
            if (!parsedId)
            {
                messages::resourceNotFound(asyncResp->res, "LogEntry", idstr);
                return;
            }
            uint32_t id = static_cast<uint32_t>(*parsedId);
            auto dataCount = debugTokenData.size();
            if (dataCount == 0 || id > dataCount - 1)
            {
                messages::resourceMissingAtURI(
                    asyncResp->res,
                    boost::urls::format(
                        "/redfish/v1/Systems/{}/LogServices/DebugTokenService/Entries/{}",
                        BMCWEB_REDFISH_SYSTEM_URI_NAME, std::to_string(id)));
                asyncResp->res.result(boost::beast::http::status::not_found);
                return;
            }
            asyncResp->res.jsonValue["@odata.type"] =
                "#LogEntry.v1_15_0.LogEntry";
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                "/LogServices/DebugTokenService/Entries/" + std::to_string(id);
            asyncResp->res.jsonValue["Id"] = std::to_string(id);
            asyncResp->res.jsonValue["EntryType"] = "Oem";
            asyncResp->res.jsonValue["Name"] = "Debug Token Entry";
            asyncResp->res.jsonValue["DiagnosticDataType"] = "OEM";
            asyncResp->res.jsonValue["OEMDiagnosticDataType"] =
                std::get<0>(debugTokenData.at(id));
            asyncResp->res.jsonValue["AdditionalDataSizeBytes"] =
                std::get<1>(debugTokenData.at(id)).length();
            asyncResp->res.jsonValue["AdditionalDataURI"] =
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                "/LogServices/DebugTokenService/Entries/" + std::to_string(id) +
                "/attachment";
        });
}

inline void resultHandler(const std::string& requestType,
                          const std::shared_ptr<task::TaskData>& task,
                          const std::string& result)
{
    debugTokenData.emplace_back(requestType, result);
    std::string path = std::format(
        "/redfish/v1/Systems/{}/LogServices/DebugTokenService/Entries/{}/attachment",
        BMCWEB_REDFISH_SYSTEM_URI_NAME, debugTokenData.size() - 1);
    debug_token::addDataLocation(task, path);
    debug_token::finishTask(task);
}

inline void requestRoutesDebugTokenServiceDiagnosticDataCollect(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/DebugTokenService/LogService.CollectDiagnosticData")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
                {
                    messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                               systemName);
                    return;
                }
                std::string diagnosticDataType;
                std::string oemDiagnosticDataType;
                if (!redfish::json_util::readJsonAction(
                        req, asyncResp->res, "DiagnosticDataType",
                        diagnosticDataType, "OEMDiagnosticDataType",
                        oemDiagnosticDataType))
                {
                    return;
                }
                if (diagnosticDataType != "OEM")
                {
                    BMCWEB_LOG_ERROR("Only OEM DiagnosticDataType supported "
                                     "for DebugTokenService");
                    messages::actionParameterValueFormatError(
                        asyncResp->res, diagnosticDataType,
                        "DiagnosticDataType", "CollectDiagnosticData");
                    return;
                }
                if (oemDiagnosticDataType == "DebugTokenStatus")
                {
                    std::shared_ptr<task::TaskData> task =
                        debug_token::createTask(req, asyncResp,
                                                debugTokenTaskTimeoutSec);
                    debug_token::status::Handler::startOperation(
                        task,
                        std::bind_front(&resultHandler, oemDiagnosticDataType));
                    return;
                }
                debug_token::TokenType tokenType =
                    debug_token::stringToTokenType(oemDiagnosticDataType);
                if (tokenType == debug_token::TokenType::Invalid)
                {
                    BMCWEB_LOG_ERROR("Unsupported OEMDiagnosticDataType: {}",
                                     oemDiagnosticDataType);
                    messages::actionParameterValueFormatError(
                        asyncResp->res, oemDiagnosticDataType,
                        "OEMDiagnosticDataType", "CollectDiagnosticData");
                    return;
                }
                std::shared_ptr<task::TaskData> task = debug_token::createTask(
                    req, asyncResp, debugTokenTaskTimeoutSec);
                if (debug_token::isDOTTokenType(tokenType))
                {
                    debug_token::dot_request::Handler::startOperation(
                        tokenType, task,
                        std::bind_front(&resultHandler, oemDiagnosticDataType));
                }
                else
                {
                    debug_token::request::Handler::startOperation(
                        tokenType, task,
                        std::bind_front(&resultHandler, oemDiagnosticDataType));
                }
            });
}

inline void requestRoutesDebugTokenServiceDiagnosticDataEntryDownload(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/DebugTokenService"
                      "/Entries/<str>/attachment/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            [[maybe_unused]] const std::string& systemName,
                            const std::string& idstr) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
            {
                messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                           systemName);
                return;
            }
            std::string_view accept = req.getHeaderValue("Accept");
            if (!accept.empty() &&
                !http_helpers::isContentTypeAllowed(
                    req.getHeaderValue("Accept"),
                    http_helpers::ContentType::OctetStream, true))
            {
                asyncResp->res.result(boost::beast::http::status::bad_request);
                return;
            }

            std::optional<uint64_t> parsedId = stringToUint64(idstr);
            if (!parsedId)
            {
                messages::resourceNotFound(asyncResp->res, "LogEntry", idstr);
                return;
            }
            uint32_t id = static_cast<uint32_t>(*parsedId);

            auto dataCount = debugTokenData.size();
            if (dataCount == 0 || id > dataCount - 1)
            {
                messages::resourceMissingAtURI(
                    asyncResp->res,
                    boost::urls::format(
                        "/redfish/v1/Systems/{}/LogServices/DebugTokenService/Entries/{}/attachment",
                        BMCWEB_REDFISH_SYSTEM_URI_NAME, std::to_string(id)));
                asyncResp->res.result(boost::beast::http::status::not_found);
                return;
            }

            asyncResp->res.addHeader("Content-Type",
                                     "application/octet-stream");
            asyncResp->res.addHeader("Content-Transfer-Encoding", "Binary");
            std::string data = std::get<1>(debugTokenData[id]);
            asyncResp->res.write(std::move(data));
        });
}
} // namespace redfish
