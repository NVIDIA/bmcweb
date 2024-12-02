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

inline void requestRoutesSystemFaultLogService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/FaultLog/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName)

            {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/LogServices/FaultLog";
                asyncResp->res.jsonValue["@odata.type"] =
                    "#LogService.v1_2_0.LogService";
                asyncResp->res.jsonValue["Name"] = "FaultLog LogService";
                asyncResp->res.jsonValue["Description"] =
                    "System FaultLog LogService";
                asyncResp->res.jsonValue["Id"] = "FaultLog";
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
                         "/LogServices/FaultLog/Entries"}};
                asyncResp->res.jsonValue["Actions"] = {
                    {"#LogService.ClearLog",
                     {{"target",
                       "/redfish/v1/Systems/" +
                           std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                           "/LogServices/FaultLog/Actions/LogService.ClearLog"}}}};
            });
}

inline void requestRoutesSystemFaultLogEntryCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/FaultLog/Entries/")
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
                    "/LogServices/FaultLog/Entries";
                asyncResp->res.jsonValue["Name"] = "System FaultLog Entries";
                asyncResp->res.jsonValue["Description"] =
                    "Collection of System FaultLog Entries";

                getDumpEntryCollection(asyncResp, "FaultLog");
            });
}

inline void requestRoutesSystemFaultLogEntry(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/LogServices/FaultLog/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)

        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& param) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                getDumpEntryById(asyncResp, param, "FaultLog");
            });

    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/LogServices/FaultLog/Entries/<str>/")
        .privileges(redfish::privileges::deleteLogEntry)
        .methods(boost::beast::http::verb::delete_)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               [[maybe_unused]] const std::string& systemName,
               const std::string& param) {
                deleteDumpEntry(asyncResp, param, "FaultLog");
            });
}

inline void requestRoutesSystemFaultLogClear(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/FaultLog/Actions/LogService.ClearLog/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName)

            {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                clearDump(asyncResp, "FaultLog");
            });
}

} // namespace redfish
