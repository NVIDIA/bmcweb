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

inline void requestRoutesSystemFDRService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/FDR/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleFDRServiceGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/FDR/")
        .privileges(redfish::privileges::patchLogService)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleFDRServicePatch, std::ref(app)));
}

inline void requestRoutesSystemFDREntryCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/FDR/Entries/")
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
            "/LogServices/FDR/Entries";
        asyncResp->res.jsonValue["Name"] = "System FDR Entries";
        asyncResp->res.jsonValue["Description"] =
            "Collection of System FDR Entries";

        getDumpEntryCollection(asyncResp, "FDR");
    });
}

inline void requestRoutesSystemFDREntry(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/LogServices/FDR/Entries/<str>/")
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

        getDumpEntryById(asyncResp, param, "FDR");
    });

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/LogServices/FDR/Entries/<str>/")
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
        deleteDumpEntry(asyncResp, param, "FDR");
    });
}

inline void requestRoutesSystemFDREntryDownload(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/FDR/Entries/<str>/attachment/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& entryID) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        auto downloadDumpEntryHandler =
            [asyncResp, entryID](const boost::system::error_code& ec,
                                 const sdbusplus::message::unix_fd& unixfd) {
            downloadEntryCallback(asyncResp, entryID, "FDR", ec, unixfd);
        };

        sdbusplus::message::object_path entry(
            "/xyz/openbmc_project/dump/fdr/entry");
        entry /= entryID;
        crow::connections::systemBus->async_method_call(
            std::move(downloadDumpEntryHandler),
            "xyz.openbmc_project.Dump.Manager", entry,
            "xyz.openbmc_project.Dump.Entry", "GetFileHandle");
    });
}

inline void handleLogServicesFDRDumpCollectDiagnosticDataPost(
    crow::App& app, const std::string& dumpType, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& /*managerId*/)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    createDump(asyncResp, req, dumpType);
}

inline void requestRoutesSystemFDRCreate(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/FDR/Actions/LogService.CollectDiagnosticData/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleLogServicesFDRDumpCollectDiagnosticDataPost,
                            std::ref(app), "FDR"));
}

void inline requestRoutesSystemFDRClear(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/FDR/Actions/LogService.ClearLog/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        std::vector<std::pair<std::string, std::variant<std::string, uint64_t>>>
            createDumpParamVec;

        createDumpParamVec.emplace_back("DiagnosticType", "FDR");
        createDumpParamVec.emplace_back("Action", "Clean");

        crow::connections::systemBus->async_method_call(
            [asyncResp](
                const boost::system::error_code ec,
                const sdbusplus::message::message& msg,
                const sdbusplus::message::object_path& objPath) mutable {
            (void)msg;
            (void)objPath;

            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            messages::success(asyncResp->res);
        },
            "xyz.openbmc_project.Dump.Manager", "/xyz/openbmc_project/dump/fdr",
            "xyz.openbmc_project.Dump.Create", "CreateDump",
            createDumpParamVec);
    });
}

inline void getFDRServiceState(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    constexpr const char* serviceName = "org.freedesktop.systemd1";
    // change fdrServiceObjectPath accoridng to FDR service name
    constexpr const char* fdrServiceObjectPath =
        "/org/freedesktop/systemd1/unit/nvidia_2dfdr_2eservice";
    constexpr const char* interfaceName = "org.freedesktop.systemd1.Unit";
    constexpr const char* property = "SubState";

    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, serviceName, fdrServiceObjectPath,
        interfaceName, property,
        [aResp](const boost::system::error_code ec,
                const std::string& serviceState) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                messages::internalError(aResp->res);
                return;
            }

            BMCWEB_LOG_DEBUG("serviceState : {}", serviceState);

            if (serviceState == "running")
            {
                aResp->res.jsonValue["ServiceEnabled"] = true;
            }
            else
            {
                aResp->res.jsonValue["ServiceEnabled"] = false;
            }
        });
}

inline void
    handleFDRServiceGet(crow::App& app, const crow::Request& req,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        [[maybe_unused]] const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/LogServices/FDR";
    asyncResp->res.jsonValue["@odata.type"] = "#LogService.v1_2_0.LogService";
    asyncResp->res.jsonValue["Name"] = "FDR LogService";
    asyncResp->res.jsonValue["Description"] = "System FDR LogService";
    asyncResp->res.jsonValue["Id"] = "FDR";
    asyncResp->res.jsonValue["OverWritePolicy"] = "Unknown";

    std::pair<std::string, std::string> redfishDateTimeOffset =
        redfish::time_utils::getDateTimeOffsetNow();
    asyncResp->res.jsonValue["DateTime"] = redfishDateTimeOffset.first;
    asyncResp->res.jsonValue["DateTimeLocalOffset"] =
        redfishDateTimeOffset.second;
    asyncResp->res.jsonValue["Entries"] = {
        {"@odata.id",
         "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
             "/LogServices/FDR/Entries"}};
    asyncResp->res.jsonValue["Actions"]["#LogService.ClearLog"] = {
        {"target",
         "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
             "/LogServices/FDR/Actions/LogService.ClearLog"}};
    asyncResp->res.jsonValue["Actions"]["#LogService.CollectDiagnosticData"] = {
        {"target",
         "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
             "/LogServices/FDR/Actions/LogService.CollectDiagnosticData"}};

    getFDRServiceState(asyncResp);
}

inline void
    handleFDRServicePatch(crow::App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          [[maybe_unused]] const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::optional<bool> enabled;

    if (!json_util::readJsonPatch(req, asyncResp->res, "ServiceEnabled",
                                  enabled))
    {
        BMCWEB_LOG_ERROR("Failed to get ServiceEnabled property");
        return;
    }

    if (!enabled.has_value())
    {
        BMCWEB_LOG_ERROR("No value for ServiceEnabled property");
        return;
    }

    BMCWEB_LOG_DEBUG("enabled = {}", *enabled);

    constexpr const char* serviceName = "org.freedesktop.systemd1";
    constexpr const char* objectPath = "/org/freedesktop/systemd1";
    constexpr const char* interfaceName = "org.freedesktop.systemd1.Manager";
    constexpr const char* startService = "StartUnit";
    constexpr const char* stopService = "StopUnit";
    constexpr const char* enableService = "EnableUnitFiles";
    constexpr const char* disableService = "DisableUnitFiles";
    // change fdrServiceName accoridng to FDR service name
    constexpr const char* fdrServiceName = "nvidia-fdr.service";

    if (*enabled)
    {
        // Try to enable service persistently
        constexpr bool runtime = false;
        constexpr bool force = false;

        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code ec) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                    messages::internalError(asyncResp->res);
                    return;
                }
            },
            serviceName, objectPath, interfaceName, enableService,
            std::array<std::string, 1>{fdrServiceName}, runtime, force);

        // Try to start service
        constexpr const char* mode = "replace";

        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code ec) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                    messages::internalError(asyncResp->res);
                    return;
                }
            },
            serviceName, objectPath, interfaceName, startService,
            fdrServiceName, mode);
    }
    else
    {
        // Try to stop service
        constexpr const char* mode = "replace";

        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code ec) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                    messages::internalError(asyncResp->res);
                    return;
                }
            },
            serviceName, objectPath, interfaceName, stopService, fdrServiceName,
            mode);

        // Try to disable service persistently
        constexpr bool runtime = false;

        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code ec) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                    messages::internalError(asyncResp->res);
                    return;
                }
            },
            serviceName, objectPath, interfaceName, disableService,
            std::array<std::string, 1>{fdrServiceName}, runtime);
    }
}

void inline requestRoutesSystemFDRGenBirthCert(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/FDR/Actions/Oem/NvidiaLogService.GenerateBirthCert/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        std::vector<std::pair<std::string, std::variant<std::string, uint64_t>>>
            createDumpParamVec;

        createDumpParamVec.emplace_back("DiagnosticType", "FDR");
        createDumpParamVec.emplace_back("Action", "GenBirthCert");

        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code ec,
                        [[maybe_unused]] const sdbusplus::message::message& msg,
                        [[maybe_unused]] const sdbusplus::message::object_path&
                            objPath) mutable {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            messages::success(asyncResp->res);
        },
            "xyz.openbmc_project.Dump.Manager", "/xyz/openbmc_project/dump/fdr",
            "xyz.openbmc_project.Dump.Create", "CreateDump",
            createDumpParamVec);
    });
}

}