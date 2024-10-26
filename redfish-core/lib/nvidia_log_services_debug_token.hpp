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
#include "debug_token.hpp"
#include "nvidia_messages.hpp"

namespace redfish
{
// vector containing debug token-related functionalities'
// (GetDebugTokenRequest, GetDebugTokenStatus) output data
static std::vector<std::tuple<std::string, std::string>> debugTokenData;
static constexpr const uint32_t debugTokenTaskTimeoutSec{300};

inline void requestRoutesDebugToken(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/DebugTokenService")
        .privileges(redfish::privileges::getLogEntry)
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
        asyncResp->res.jsonValue["@odata.type"] =
            "#LogEntryCollection.LogEntryCollection";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/LogServices/DebugTokenService/Entries";
        asyncResp->res.jsonValue["Name"] = "Debug Token Service Entries";
        asyncResp->res.jsonValue["Description"] =
            "Collection of Debug Token Service Entries";
        asyncResp->res.jsonValue["Members@odata.count"] = debugTokenData.size();

        nlohmann::json& entriesArray = asyncResp->res.jsonValue["Members"];
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
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& idstr) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
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

        uint32_t id = static_cast<uint32_t>(stoi(idstr));
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
        asyncResp->res.jsonValue["@odata.type"] = "#LogEntry.v1_15_0.LogEntry";
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
        std::string diagnosticDataType;
        std::string oemDiagnosticDataType;
        if (!redfish::json_util::readJsonAction(
                req, asyncResp->res, "DiagnosticDataType", diagnosticDataType,
                "OEMDiagnosticDataType", oemDiagnosticDataType))
        {
            return;
        }
        if (diagnosticDataType != "OEM")
        {
            BMCWEB_LOG_ERROR("Only OEM DiagnosticDataType supported "
                             "for DebugTokenService");
            messages::actionParameterValueFormatError(
                asyncResp->res, diagnosticDataType, "DiagnosticDataType",
                "CollectDiagnosticData");
            return;
        }

        debug_token::RequestType type;
        if (oemDiagnosticDataType != "DebugTokenStatus")
        {
            if (oemDiagnosticDataType == "GetDebugTokenRequest")
            {
                type = debug_token::RequestType::DebugTokenRequest;
            }
            if constexpr (BMCWEB_DOT_SUPPORT)
            {
                if (oemDiagnosticDataType == "GetDOTCAKUnlockTokenRequest")
                {
                    type = debug_token::RequestType::DOTCAKUnlockTokenRequest;
                }
                else if (oemDiagnosticDataType == "GetDOTEnableTokenRequest")
                {
                    type = debug_token::RequestType::DOTEnableTokenRequest;
                }
                else if (oemDiagnosticDataType == "GetDOTSignTestToken")
                {
                    type = debug_token::RequestType::DOTSignTestToken;
                }
                else if (oemDiagnosticDataType == "GetDOTOverrideTokenRequest")
                {
                    type = debug_token::RequestType::DOTOverrideTokenRequest;
                }
            }
            else
            {
                BMCWEB_LOG_ERROR("Unsupported OEMDiagnosticDataType: {}",
                                 oemDiagnosticDataType);
                messages::actionParameterValueFormatError(
                    asyncResp->res, oemDiagnosticDataType,
                    "OEMDiagnosticDataType", "CollectDiagnosticData");
            }
        }

        static std::unique_ptr<debug_token::OperationHandler> op;
        if (op)
        {
            messages::serviceTemporarilyUnavailable(
                asyncResp->res, std::to_string(debugTokenTaskTimeoutSec));
            return;
        }

        std::shared_ptr<task::TaskData> task = task::TaskData::createTask(
            [](boost::system::error_code ec, sdbusplus::message_t&,
               const std::shared_ptr<task::TaskData>& taskData) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Debug token operation task error: {}",
                                 ec.message());
                if (ec != boost::asio::error::operation_aborted)
                {
                    taskData->messages.emplace_back(
                        messages::resourceErrorsDetectedFormatError(
                            "Debug token task", ec.message()));
                }
                op.reset();
            }
            return op == nullptr ? task::completed : !task::completed;
        },
            "0");

        auto resultHandler =
            [oemDiagnosticDataType,
             task](const std::shared_ptr<std::vector<std::unique_ptr<
                       debug_token::DebugTokenEndpoint>>>& endpoints) {
            std::string result;
            int totalEpCount = 0;
            int validEpCount = 0;
            if (op && endpoints)
            {
                op->getResult(result);
                totalEpCount = static_cast<int>(endpoints->size());
                for (const auto& ep : *endpoints)
                {
                    const auto& state = ep->getState();
                    BMCWEB_LOG_DEBUG(
                        "endpoint object:{} ",
                        sdbusplus::message::object_path(ep->getObject())
                            .filename());
                    BMCWEB_LOG_DEBUG("oemDiagnosticDataType:{}",
                                     oemDiagnosticDataType);
                    if (state == debug_token::EndpointState::RequestAcquired ||
                        state == debug_token::EndpointState::TokenInstalled ||
                        state ==
                            debug_token::EndpointState::DebugTokenUnsupported ||
                        state == debug_token::EndpointState::StatusAcquired)
                    {
                        ++validEpCount;
                    }
                    const auto& objectName = ep->getObject();
                    const auto deviceName =
                        sdbusplus::message::object_path(objectName).filename();
                    std::string stateDesc;
                    switch (state)
                    {
                        case debug_token::EndpointState::DebugTokenUnsupported:
                            task->messages.emplace_back(
                                messages::debugTokenUnsupported(deviceName));
                            break;
                        case debug_token::EndpointState::StatusAcquired:
                            task->messages.emplace_back(
                                messages::debugTokenStatusSuccess(deviceName));
                            break;
                        case debug_token::EndpointState::TokenInstalled:
                            task->messages.emplace_back(
                                messages::debugTokenAlreadyInstalled(
                                    deviceName));
                            break;
                        case debug_token::EndpointState::RequestAcquired:
                            task->messages.emplace_back(
                                messages::debugTokenRequestSuccess(deviceName));
                            break;
                        case debug_token::EndpointState::Error:
                            stateDesc = "Error";
                            task->messages.emplace_back(
                                messages::resourceErrorsDetectedFormatError(
                                    deviceName, stateDesc));
                            break;
                        default:
                            stateDesc = "Invalid state";
                            task->messages.emplace_back(
                                messages::resourceErrorsDetectedFormatError(
                                    objectName, stateDesc));
                            break;
                    }
                }
            }
            if (result.size() != 0)
            {
                debugTokenData.emplace_back(
                    make_tuple(oemDiagnosticDataType, result));
                std::string path = "/redfish/v1/Systems/" +
                                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                   "/LogServices/DebugTokenService/"
                                   "Entries/" +
                                   std::to_string(debugTokenData.size() - 1) +
                                   "/attachment";
                std::string location = "Location: " + path;
                task->payload->httpHeaders.emplace_back(std::move(location));
            }
            if (validEpCount == 0 || totalEpCount == 0)
            {
                task->state = "Stopping";
                task->messages.emplace_back(
                    messages::taskAborted(std::to_string(task->index)));
            }
            else
            {
                if (validEpCount == totalEpCount)
                {
                    task->state = "Completed";
                    task->messages.emplace_back(
                        messages::taskCompletedOK(std::to_string(task->index)));
                }
                else
                {
                    task->state = "Exception";
                    task->messages.emplace_back(messages::taskCompletedWarning(
                        std::to_string(task->index)));
                }
                task->percentComplete = 100 * validEpCount / totalEpCount;
            }
            task->timer.cancel();
            task->finishTask();
            task->sendTaskEvent(task->state, task->index);
            boost::asio::post(crow::connections::systemBus->get_io_context(),
                              [task] { op.reset(); });
        };
        auto errorHandler = [task](bool critical, const std::string& desc,
                                   const std::string& error) {
            task->messages.emplace_back(
                messages::resourceErrorsDetectedFormatError(desc, error));
            if (critical)
            {
                task->state = "Stopping";
                task->messages.emplace_back(
                    messages::taskAborted(std::to_string(task->index)));
                task->timer.cancel();
                task->finishTask();
                task->sendTaskEvent(task->state, task->index);
                boost::asio::post(
                    crow::connections::systemBus->get_io_context(),
                    [] { op.reset(); });
            }
        };

        if (oemDiagnosticDataType == "DebugTokenStatus")
        {
            op = std::make_unique<debug_token::StatusQueryHandler>(
                resultHandler, errorHandler);
        }
        else
        {
            op = std::make_unique<debug_token::RequestHandler>(
                resultHandler, errorHandler, type);
        }
        task->payload.emplace(req);
        task->populateResp(asyncResp->res);
        task->startTimer(std::chrono::seconds(debugTokenTaskTimeoutSec));
    });
}

inline void requestRoutesDebugTokenServiceDiagnosticDataEntryDownload(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/DebugTokenService"
                      "/Entries/<str>/attachment/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& idstr) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
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

        uint32_t id = static_cast<uint32_t>(stoi(idstr));

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

        asyncResp->res.addHeader("Content-Type", "application/octet-stream");
        asyncResp->res.addHeader("Content-Transfer-Encoding", "Binary");
        std::string data = std::get<1>(debugTokenData[id]);
        asyncResp->res.write(std::move(data));
    });
}
} // namespace redfish
