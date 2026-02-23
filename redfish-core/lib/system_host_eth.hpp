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
#include "nvidia_dbus_utility.hpp"
#include "query.hpp"
#include "redfish_util.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/collection.hpp"
#include "utils/json_utils.hpp"
namespace redfish
{

/**
 * @brief Process MAC address properties from port address object
 */
inline void processMACAddressProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "getNetworkAdapterMACAddress: Error getting port address properties");
        return;
    }

    const std::string* macAddress = nullptr;
    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties, "MACAddress", macAddress);
    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    if (macAddress != nullptr)
    {
        asyncResp->res.jsonValue["MACAddress"] = *macAddress;
    }
}

/**
 * @brief Process ethernet port address association for MAC address
 */
inline void processEthernetPortAddress(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const boost::system::error_code& ec,
    const std::vector<std::string>& portData)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG(
            "getNetworkAdapterMACAddress: No ethernet port address association");
        return;
    }

    if (portData.empty())
    {
        return;
    }

    const std::string& portAddressPath = portData[0];

    dbus::utility::getAllProperties(
        connectionName, portAddressPath, "",
        [asyncResp](const boost::system::error_code& ec2,
                    const dbus::utility::DBusPropertiesMap& properties) {
            processMACAddressProperties(asyncResp, ec2, properties);
        });
}

/**
 * @brief Process DBus object lookup for NetworkDeviceFunction
 */
inline void processNDFDbusObject(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& ndfPath, const boost::system::error_code& ec,
    const std::vector<std::pair<std::string, std::vector<std::string>>>& object)
{
    if (ec || object.empty())
    {
        BMCWEB_LOG_DEBUG(
            "getNetworkAdapterMACAddress: No LinkType interface for {}",
            ndfPath);
        return;
    }

    const std::string& connectionName = object.front().first;
    std::string portAddressAssocPath =
        ndfPath + "/associated_ethernet_port_address";

    dbus::utility::findAssociations(
        portAddressAssocPath,
        [asyncResp, connectionName](const boost::system::error_code& ec2,
                                    const std::vector<std::string>& portResp) {
            processEthernetPortAddress(asyncResp, connectionName, ec2,
                                       portResp);
        });
}

/**
 * @brief Process NetworkDeviceFunction association for MAC address lookup
 */
inline void processNDFAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& networkAdapterPath, const boost::system::error_code& ec,
    const std::vector<std::string>& data)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG(
            "getNetworkAdapterMACAddress: No NDF association for {}",
            networkAdapterPath);
        return;
    }

    if (data.empty())
    {
        BMCWEB_LOG_DEBUG("getNetworkAdapterMACAddress: No NDF found for {}",
                         networkAdapterPath);
        return;
    }

    const std::string& ndfPath = data[0];
    constexpr std::string_view linkTypeInterface =
        "xyz.openbmc_project.Network.LinkType";

    dbus::utility::getDbusObject(
        ndfPath, std::array<std::string_view, 1>{linkTypeInterface},
        [asyncResp, ndfPath](
            const boost::system::error_code& ec2,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                object) {
            processNDFDbusObject(asyncResp, ndfPath, ec2, object);
        });
}

/**
 * @brief Get MAC address from NetworkDeviceFunction for a given NetworkAdapter
 *
 * Retrieves the MAC address from the associated port address of the first
 * NetworkDeviceFunction under a NetworkAdapter. On any lookup failure the MAC
 * property is simply omitted from the response (it is optional per D-Bus usage
 * guidelines).
 */
inline void getNetworkAdapterMACAddress(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& networkAdapterPath)
{
    std::string ndfAssociationPath =
        networkAdapterPath + "/network_device_functions";

    dbus::utility::findAssociations(
        ndfAssociationPath,
        [asyncResp, networkAdapterPath](const boost::system::error_code& ec,
                                        const std::vector<std::string>& resp) {
            processNDFAssociation(asyncResp, networkAdapterPath, ec, resp);
        });
}

/**
 * @brief Process NetworkAdapter paths and populate EthernetInterface response
 *
 * Searches for a matching NetworkAdapter and populates the EthernetInterface
 * data if found. Must be defined before onNetworkAdapterPathsForGet.
 */
inline void processNetworkAdapterEthInterface(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& ifaceId,
    const dbus::utility::MapperGetSubTreePathsResponse& networkAdapterPaths)
{
    std::optional<std::string> matchedAdapterPath;
    for (const std::string& networkAdapterPath : networkAdapterPaths)
    {
        sdbusplus::message::object_path path(networkAdapterPath);
        std::string networkAdapterId = path.filename();
        if (networkAdapterId == ifaceId)
        {
            matchedAdapterPath = networkAdapterPath;
            break;
        }
    }

    if (!matchedAdapterPath)
    {
        messages::resourceNotFound(asyncResp->res, "EthernetInterface",
                                   ifaceId);
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] =
        "#EthernetInterface.v1_6_0.EthernetInterface";
    asyncResp->res.jsonValue["@odata.id"] =
        boost::urls::format("/redfish/v1/Systems/{}/EthernetInterfaces/{}",
                            BMCWEB_REDFISH_SYSTEM_URI_NAME, ifaceId);
    asyncResp->res.jsonValue["Id"] = ifaceId;
    asyncResp->res.jsonValue["Name"] = "Network Adapter Ethernet Interface";
    asyncResp->res.jsonValue["Description"] =
        "Ethernet Interface mapped from Network Adapter " + ifaceId;
    asyncResp->res.jsonValue["Status"]["State"] = resource::State::Enabled;
    asyncResp->res.jsonValue["Status"]["Health"] = resource::Health::OK;
    asyncResp->res.jsonValue["InterfaceEnabled"] = true;

    getNetworkAdapterMACAddress(asyncResp, *matchedAdapterPath);
}

/**
 * @brief Named callback for GetSubTreePaths used by
 * handleNetworkAdapterEthInterface.
 */
inline void onNetworkAdapterPathsForGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& ifaceId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& networkAdapterPaths)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG(
            "handleNetworkAdapterEthInterface: Error getting network adapters");
        messages::resourceNotFound(asyncResp->res, "EthernetInterface",
                                   ifaceId);
        return;
    }
    processNetworkAdapterEthInterface(asyncResp, ifaceId, networkAdapterPaths);
}

/**
 * @brief Named callback for GetSubTreePaths used by
 * addNetworkAdapterEthInterfaces.
 *
 * This is the final step of the collection-build chain. By the time this
 * callback fires, the host ethernet interfaces have already been appended to
 * Members (the adapter D-Bus call is launched only after the host-list callback
 * completes). Writing Members@odata.count here captures the true combined
 * total, eliminating the race where two async flows each overwrote the count.
 */
inline void onNetworkAdapterSubTreePaths(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& networkAdapterPaths)
{
    nlohmann::json& ifaceArray = asyncResp->res.jsonValue["Members"];

    if (!ec)
    {
        for (const std::string& networkAdapterPath : networkAdapterPaths)
        {
            sdbusplus::message::object_path path(networkAdapterPath);
            std::string networkAdapterId = path.filename();
            if (networkAdapterId.empty())
            {
                continue;
            }
            nlohmann::json::object_t iface;
            iface["@odata.id"] = boost::urls::format(
                "/redfish/v1/Systems/{}/EthernetInterfaces/{}",
                BMCWEB_REDFISH_SYSTEM_URI_NAME, networkAdapterId);
            ifaceArray.push_back(std::move(iface));
        }
    }
    else
    {
        BMCWEB_LOG_DEBUG(
            "addNetworkAdapterEthInterfaces: No network adapters found");
    }

    asyncResp->res.jsonValue["Members@odata.count"] = ifaceArray.size();
}

/**
 * @brief Append NetworkAdapter-based EthernetInterfaces to the collection.
 *
 * Uses dbus::utility::getSubTreePaths (CE-15) instead of raw async_method_call.
 * Members@odata.count is written by onNetworkAdapterSubTreePaths after both
 * host and adapter entries have been appended.
 */
inline void addNetworkAdapterEthInterfaces(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    constexpr std::string_view networkInterfaceIface =
        "xyz.openbmc_project.Inventory.Item.NetworkInterface";
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory", 0,
        std::array<std::string_view, 1>{networkInterfaceIface},
        std::bind_front(onNetworkAdapterSubTreePaths, asyncResp));
}

/**
 * @brief Handle EthernetInterface GET for NetworkAdapter-based interfaces.
 *
 * Uses dbus::utility::getSubTreePaths (CE-15) instead of raw async_method_call.
 */
inline void handleNetworkAdapterEthInterface(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& ifaceId)
{
    constexpr std::string_view networkInterfaceIface =
        "xyz.openbmc_project.Inventory.Item.NetworkInterface";
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory", 0,
        std::array<std::string_view, 1>{networkInterfaceIface},
        std::bind_front(onNetworkAdapterPathsForGet, asyncResp, ifaceId));
}

template <typename CallbackFunc>
void getEthernetIfaceListHost(CallbackFunc&& callback,
                              const std::vector<std::string_view>& interfaces)
{
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/network/host0", 0, interfaces,
        [callback{std::forward<CallbackFunc>(callback)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreePathsResponse& objects) {
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
        });
}

template <typename CallbackFunc>
void getEthernetIfaceService(const std::string& ethifaceId,
                             CallbackFunc&& callback,
                             const std::span<const std::string_view> interfaces)
{
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/network/host0", 0, interfaces,
        [ethifaceId{std::string{ethifaceId}},
         callback{std::forward<CallbackFunc>(callback)}](
            const boost::system::error_code& ec,
            const dbus::utility::GetSubTreeType& subtree) {
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
        });
}

template <typename CallbackFunc>
void getEthernetIfaceDataHost(
    const std::string& ethifaceId, CallbackFunc&& callback,
    const std::vector<std::string_view>& interfaces = {
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
            dbus::utility::async_method_call(
                [ethifaceId,
                 callback](const boost::system::error_code& ec,
                           const dbus::utility::ManagedObjectType& resp) {
                    EthernetInterfaceData ethData{};
                    std::vector<IPv4AddressData> ipv4Data;
                    std::vector<IPv6AddressData> ipv6Data;

                    if (ec)
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
        .methods(
            boost::beast::http::verb::
                get)([&app](
                         const crow::Request& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            asyncResp->res.jsonValue["@odata.type"] =
                "#EthernetInterfaceCollection.EthernetInterfaceCollection";
            asyncResp->res.jsonValue["@odata.id"] =
                boost::urls::format("/redfish/v1/Systems/{}/EthernetInterfaces",
                                    BMCWEB_REDFISH_SYSTEM_URI_NAME);
            asyncResp->res.jsonValue["Name"] =
                "Ethernet Network Interface Collection";
            asyncResp->res.jsonValue["Description"] =
                "Collection of EthernetInterfaces of the host";

            // Get eth interface list, and call the below callback for JSON
            // preparation
            getEthernetIfaceListHost(
                [asyncResp](
                    const bool& success,
                    const boost::container::flat_set<std::string>& ifaceList) {
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
                            iface["@odata.id"] = boost::urls::format(
                                "/redfish/v1/Systems/{}/EthernetInterfaces/{}",
                                BMCWEB_REDFISH_SYSTEM_URI_NAME, ifaceItem);
                            ifaceArray.push_back(std::move(iface));
                        }
                    }

                    if constexpr (BMCWEB_NETWORK_ADAPTERS_GENERIC)
                    {
                        // Members@odata.count is set by
                        // onNetworkAdapterSubTreePaths after both host and
                        // adapter entries are appended, avoiding the race
                        // where two async flows each wrote their own count.
                        addNetworkAdapterEthInterfaces(asyncResp);
                    }
                    else
                    {
                        asyncResp->res.jsonValue["Members@odata.count"] =
                            ifaceArray.size();
                    }
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
                            // Try NetworkAdapter-based EthernetInterface
                            if constexpr (BMCWEB_NETWORK_ADAPTERS_GENERIC)
                            {
                                handleNetworkAdapterEthInterface(asyncResp,
                                                                 ifaceId);
                                return;
                            }

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
