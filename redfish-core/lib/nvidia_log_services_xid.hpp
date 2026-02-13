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
#include "dbus_utility.hpp"
#include "log_services.hpp"
#include "message_registries.hpp"
#include "nvidia_log_services_sel.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/log_services_util.hpp"
#include "utils/nvidia_time_utils.hpp"
#include "utils/origin_utils.hpp"
#include "utils/time_utils.hpp"
namespace redfish
{

inline void getXIDLogEntries(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& chassisName)
{
    BMCWEB_LOG_DEBUG("Getting XID log entries for chassis: {}", chassisName);

    // Collections don't include the static data
    // added by SubRoute because it has a
    // duplicate entry for members
    asyncResp->res.jsonValue["@odata.type"] =
        "#LogEntryCollection.LogEntryCollection";
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Chassis/" + chassisId + "/LogServices/XID/Entries";
    asyncResp->res.jsonValue["Name"] = "XID Log Entries";
    asyncResp->res.jsonValue["Description"] = "Collection of XID Log Entries";
    asyncResp->res.jsonValue["Members@odata.count"] = 0;

    std::string namespaceName = chassisName + "_XID";
    BMCWEB_LOG_DEBUG("Namespace: {}", namespaceName);

    sdbusplus::message::object_path path("/xyz/openbmc_project/logging");
    const std::string interface = "xyz.openbmc_project.Logging.Namespace";
    dbus::utility::getAllNameSpaceObjects(
        "xyz.openbmc_project.Logging", path, interface, namespaceName,
        "xyz.openbmc_project.Logging.Namespace.ResolvedFilterType.Both",
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::ManagedObjectType& resp) {
            afterLogEntriesGetManagedObjects(asyncResp, ec, resp);
        });
}

inline void populateXIDLogServiceFromSubtree(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, const std::string& chassisId,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec || subtree.empty())
    {
        messages::internalError(asyncResp->res);
        return;
    }
    // Iterate over all retrieved ObjectPaths.
    for (const std::pair<
             std::string,
             std::vector<std::pair<std::string, std::vector<std::string>>>>&
             object : subtree)
    {
        const std::string& path = object.first;
        const std::vector<std::pair<std::string, std::vector<std::string>>>&
            connectionNames = object.second;

        const std::string& connectionName = connectionNames[0].first;

        sdbusplus::message::object_path objPath(path);
        if (objPath.filename() != chassisId)
        {
            continue;
        }
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Chassis/" + chassisId + "/LogServices/XID";
        asyncResp->res.jsonValue["@odata.type"] =
            "#LogService.v1_1_0.LogService";
        asyncResp->res.jsonValue["Name"] = "XID Log Service";
        asyncResp->res.jsonValue["Description"] = "XID Log Service";
        asyncResp->res.jsonValue["Id"] = "XID";
        asyncResp->res.jsonValue["OverWritePolicy"] = "WrapsWhenFull";
        std::pair<std::string, std::string> redfishDateTimeOffset =
            redfish::time_utils::getDateTimeOffsetNow();

        asyncResp->res.jsonValue["DateTime"] = redfishDateTimeOffset.first;
        asyncResp->res.jsonValue["DateTimeLocalOffset"] =
            redfishDateTimeOffset.second;

        const std::string inventoryItemInterface =
            "xyz.openbmc_project.Inventory.Item";

        sdbusplus::asio::getProperty<std::string>(
            *crow::connections::systemBus, connectionName, path,
            inventoryItemInterface, "PrettyName",
            [asyncResp, chassisId(std::string(chassisId))](
                const boost::system::error_code& ec1,
                const std::string& chassisName) {
                if (ec1)
                {
                    BMCWEB_LOG_DEBUG("DBus response error for PrettyName");
                    messages::internalError(asyncResp->res);
                    return;
                }
                BMCWEB_LOG_DEBUG("PrettyName: {}", chassisName);
                // Call Phosphor-logging GetStats method to get
                // LatestEntryTimestamp and LatestEntryID
                crow::connections::systemBus->async_method_call(
                    [asyncResp](
                        const boost::system::error_code& latestEntryError,
                        const std::tuple<uint32_t, uint64_t>& reqData) {
                        if (latestEntryError)
                        {
                            BMCWEB_LOG_ERROR(
                                "Failed to get Data from xyz.openbmc_project.Logging GetStats: {}",
                                latestEntryError.message());
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        auto lastTimeStamp = redfish::time_utils::getTimestamp(
                            std::get<1>(reqData));
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                            "#NvidiaLogService.v1_4_0.NvidiaLogService";
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["LatestEntryID"] =
                            std::to_string(std::get<0>(reqData));
                        asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                                ["LatestEntryTimeStamp"] =
                            redfish::time_utils::getDateTimeStdtime(
                                lastTimeStamp);
                    },
                    "xyz.openbmc_project.Logging",
                    "/xyz/openbmc_project/logging",
                    "xyz.openbmc_project.Logging.Namespace", "GetStats",
                    chassisName + "_XID");
            });
        if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
        {
            if (BMCWEB_NVIDIA_BOOTENTRYID)
            {
                populateBootEntryId(asyncResp->res);
            }
        } // BMCWEB_NVIDIA_OEM_PROPERTIES
        asyncResp->res.jsonValue["Entries"] = {
            {"@odata.id",
             "/redfish/v1/Chassis/" + chassisId + "/LogServices/XID/Entries"}};
        return;
    }
    // Couldn't find an object with that name.  return an
    // error
    messages::resourceNotFound(asyncResp->res, "#Chassis.v1_17_0.Chassis",
                               chassisId);
}

inline void requestRoutesChassisXIDLogService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/LogServices/XID/")
        .privileges(redfish::privileges::getLogService)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            BMCWEB_LOG_DEBUG("chassisId: {}", chassisId);
            BMCWEB_LOG_DEBUG("requestRoutesChassisXIDLogService enter");
            const std::array<std::string_view, 2> interfaces = {
                "xyz.openbmc_project.Inventory.Item.Board",
                "xyz.openbmc_project.Inventory.Item.Chassis"};
            dbus::utility::getSubTree(
                "/xyz/openbmc_project/inventory", 0, interfaces,
                [asyncResp, chassisId(std::string(chassisId))](
                    const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
                    BMCWEB_LOG_DEBUG(
                        "requestRoutesChassisXIDLogService respHandler enter");
                    populateXIDLogServiceFromSubtree(asyncResp, ec, chassisId,
                                                     subtree);
                });
        });
}

inline void requestRoutesChassisXIDLogEntryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/LogServices/XID/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            const std::array<std::string_view, 2> interfaces = {
                "xyz.openbmc_project.Inventory.Item.Board",
                "xyz.openbmc_project.Inventory.Item.Chassis"};

            dbus::utility::getSubTree(
                "/xyz/openbmc_project/inventory", 0, interfaces,
                [asyncResp, chassisId(std::string(chassisId))](
                    const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
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
                                    const boost::system::error_code&
                                        prettyNameError,
                                    const std::string& chassisName) {
                                    if (prettyNameError)
                                    {
                                        BMCWEB_LOG_DEBUG(
                                            "DBus response error for PrettyName");
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }
                                    BMCWEB_LOG_DEBUG("PrettyName: {}",
                                                     chassisName);
                                    // Call the function to get XID log entries
                                    getXIDLogEntries(asyncResp, chassisId,
                                                     chassisName);
                                    return;
                                });
                            return;
                        }
                    }
                    messages::resourceNotFound(
                        asyncResp->res, "#Chassis.v1_15_0.Chassis", chassisId);
                });
        });
}
} // namespace redfish
