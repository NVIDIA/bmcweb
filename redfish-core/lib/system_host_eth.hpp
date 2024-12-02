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
#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "dbus_singleton.hpp"
#include "ethernet.hpp"
#include "health.hpp"
#include "query.hpp"
#include "redfish_util.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/collection.hpp"
#include "utils/json_utils.hpp"

namespace redfish
{
template <typename CallbackFunc>
void getEthernetIfaceListHost(CallbackFunc&& callback,
                              const std::vector<const char*>& interfaces)
{
    crow::connections::systemBus->async_method_call(
        [callback{std::forward<CallbackFunc>(callback)}](
            const boost::system::error_code ec,
            const std::vector<std::string>& objects) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                callback(false, {}); // Invoke callback with failure
                return;
            }

            // Convert object paths to interface IDs or names
            boost::container::flat_set<std::string> ifaceList;
            for (const auto& objectPath : objects)
            {
                // Extract the interface ID or name from the object path
                std::string ifaceId =
                    sdbusplus::message::object_path(objectPath).filename();
                if (!ifaceId.empty())
                {
                    ifaceList.emplace(std::move(ifaceId));
                }
            }

            // Invoke callback with success and the list of Ethernet interface
            // IDs
            callback(true, std::move(ifaceList));
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/network/host0", 0, interfaces);
}

template <typename CallbackFunc>
void getEthernetIfaceService(
    const std::string& ethifaceId, CallbackFunc&& callback,
    const std::vector<const char*>& interfaces = {
        "xyz.openbmc_project.Network.EthernetInterface"})
{
    crow::connections::systemBus->async_method_call(
        [ethifaceId{std::string{ethifaceId}},
         callback{std::forward<CallbackFunc>(callback)}](
            const boost::system::error_code ec,
            const crow::openbmc_mapper::GetSubTreeType& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error ", ec);
                callback(false, ""); // Invoke callback with failure
                return;
            }
            // Iterate over all retrieved ObjectPaths.
            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                     object : subtree)
            {
                const std::string& path = object.first;
                const auto& connectionNames = object.second;

                if (path.find(ethifaceId) != std::string::npos)
                {
                    std::string serviceName = connectionNames[0].first;
                    BMCWEB_LOG_ERROR("Service name: {}", serviceName);
                    callback(true, std::move(serviceName));
                    return;
                }
            }
            BMCWEB_LOG_ERROR("Service for ETH Iface {} not found", ethifaceId);
            callback(false, ""); // Invoke callback with failure
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/network/host0", 0, interfaces);
}

template <typename CallbackFunc>
void getEthernetIfaceDataHost(
    const std::string& ethifaceId, CallbackFunc&& callback,
    const std::vector<const char*>& interfaces = {
        "xyz.openbmc_project.Network.EthernetInterface"})
{
    // First, call getEthernetIfaceService to get the serviceName
    getEthernetIfaceService(
        ethifaceId,
        [ethifaceId, callback,
         interfaces](bool success, const std::string& serviceName) {
            if (!success || serviceName.empty())
            {
                // Handle error
                EthernetInterfaceData ethData{};
                std::vector<IPv4AddressData> ipv4Data;
                std::vector<IPv6AddressData> ipv6Data;
                callback(false, ethData, ipv4Data, ipv6Data);
                return;
            }
            crow::connections::systemBus->async_method_call(
                [ethifaceId,
                 callback](const boost::system::error_code errorCode,
                           const dbus::utility::ManagedObjectType& resp) {
                    EthernetInterfaceData ethData{};
                    std::vector<IPv4AddressData> ipv4Data;
                    std::vector<IPv6AddressData> ipv6Data;

                    if (errorCode)
                    {
                        callback(false, ethData, ipv4Data, ipv6Data);
                        return;
                    }
                    const std::string& ethifacePath = "host0/" + ethifaceId;
                    bool found = extractEthernetInterfaceData(ethifacePath,
                                                              resp, ethData);
                    if (!found)
                    {
                        callback(false, ethData, ipv4Data, ipv6Data);
                        return;
                    }

                    extractIPData(ethifacePath, resp, ipv4Data);
                    // Fix global GW
                    for (IPv4AddressData& ipv4 : ipv4Data)
                    {
                        if (((ipv4.linktype == LinkType::Global) &&
                             (ipv4.gateway == "0.0.0.0")) ||
                            (ipv4.origin == "DHCP") ||
                            (ipv4.origin == "Static"))
                        {
                            ipv4.gateway = ethData.defaultGateway;
                        }
                    }

                    extractIPV6Data(ethifacePath, resp, ipv6Data);
                    // Finally make a callback with useful data
                    callback(true, ethData, ipv4Data, ipv6Data);
                },
                serviceName, "/xyz/openbmc_project/network/host0",
                "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
        },
        interfaces);
}

inline void requestHostEthernetInterfacesRoutes(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/EthernetInterfaces/")
        .privileges(redfish::privileges::getEthernetInterfaceCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                asyncResp->res.jsonValue["@odata.type"] =
                    "#EthernetInterfaceCollection.EthernetInterfaceCollection";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/EthernetInterfaces";
                asyncResp->res.jsonValue["Name"] =
                    "Ethernet Network Interface Collection";
                asyncResp->res.jsonValue["Description"] =
                    "Collection of EthernetInterfaces of the host";

                // Get eth interface list, and call the below callback for JSON
                // preparation
                getEthernetIfaceListHost(
                    [asyncResp](const bool& success,
                                const boost::container::flat_set<std::string>&
                                    ifaceList) {
                        if (!success)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        nlohmann::json& ifaceArray =
                            asyncResp->res.jsonValue["Members"];
                        ifaceArray = nlohmann::json::array();
                        std::string tag = "_";
                        for (const std::string& ifaceItem : ifaceList)
                        {
                            std::size_t found = ifaceItem.find(tag);
                            if (found == std::string::npos)
                            {
                                nlohmann::json::object_t iface;
                                iface["@odata.id"] =
                                    "/redfish/v1/Systems/" +
                                    std::string(
                                        BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                    "/EthernetInterfaces/" + ifaceItem;
                                ifaceArray.push_back(std::move(iface));
                            }
                        }

                        asyncResp->res.jsonValue["Members@odata.count"] =
                            ifaceArray.size();
                        asyncResp->res.jsonValue["@odata.id"] =
                            "/redfish/v1/Systems/" +
                            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                            "/EthernetInterfaces";
                    },
                    {"xyz.openbmc_project.Network.EthernetInterface"});
            });

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/EthernetInterfaces/<str>/")
        .privileges(redfish::privileges::getEthernetInterface)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& ifaceId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                getEthernetIfaceDataHost(
                    ifaceId,
                    [asyncResp,
                     ifaceId](const bool& success,
                              const EthernetInterfaceData& ethData,
                              const std::vector<IPv4AddressData>& ipv4Data,
                              const std::vector<IPv6AddressData>& ipv6Data) {
                        if (!success)
                        {
                            // TODO(Pawel)consider distinguish between non
                            // existing object, and other errors
                            messages::resourceNotFound(
                                asyncResp->res, "EthernetInterface", ifaceId);
                            return;
                        }

                        // Keep using the v1.6.0 schema here as currently bmcweb
                        // have to use "VLANs" property deprecated in v1.7.0 for
                        // VLAN creation/deletion.
                        asyncResp->res.jsonValue["@odata.type"] =
                            "#EthernetInterface.v1_6_0.EthernetInterface";
                        asyncResp->res.jsonValue["Name"] =
                            "Host Ethernet Interface";
                        asyncResp->res.jsonValue["Description"] =
                            "Host Network Interface for port " + ifaceId;
                        const std::vector<StaticGatewayData> ipv6GatewayData;
                        parseInterfaceData(asyncResp, ifaceId, ethData,
                                           ipv4Data, ipv6Data, ipv6GatewayData);
                    });
            });
}

} // namespace redfish
