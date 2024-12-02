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

#include "app.hpp"
#include "network_adapters_generic.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/collection.hpp"
#include "utils/json_utils.hpp"

#include <boost/url/format.hpp>

namespace redfish
{

inline void getNetworkAdapterCollectionMembers(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& collectionPath,
    const bool& isNDF, const std::vector<const char*>& interfaces,
    const char* subtree = "/xyz/openbmc_project/inventory")
{
    BMCWEB_LOG_DEBUG("Get collection members for: {}", collectionPath);
    crow::connections::systemBus->async_method_call(
        [collectionPath, isNDF, aResp{std::move(aResp)}](
            const boost::system::error_code ec,
            const dbus::utility::MapperGetSubTreePathsResponse& objects) {
            // currently host name is hard coded. We will add support for
            // multiple hosts through
            // https://redmine.mellanox.com/issues/3461409
            std::string dpuString = "host0";
            if (ec == boost::system::errc::io_error)
            {
                aResp->res.jsonValue["Members"] = nlohmann::json::array();
                aResp->res.jsonValue["Members@odata.count"] = 0;
                return;
            }

            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
                messages::internalError(aResp->res);
                return;
            }

            std::vector<std::string> pathNames;
            for (const auto& object : objects)
            {
                std::string p = object;
                if (p.find(dpuString) == std::string::npos)
                {
                    continue;
                }
                sdbusplus::message::object_path path(object);
                std::string leaf = path.filename();
                if (leaf.empty())
                {
                    continue;
                }
                if (isNDF)
                {
                    leaf += "f0";
                }
                pathNames.push_back(leaf);
            }
            std::sort(pathNames.begin(), pathNames.end(),
                      AlphanumLess<std::string>());

            nlohmann::json& members = aResp->res.jsonValue["Members"];
            members = nlohmann::json::array();
            for (const std::string& leaf : pathNames)
            {
                std::string newPath = collectionPath;
                newPath += '/';
                newPath += leaf;
                nlohmann::json::object_t member;
                member["@odata.id"] = std::move(newPath);
                members.push_back(std::move(member));
            }
            aResp->res.jsonValue["Members@odata.count"] = members.size();
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths", subtree, 0,
        interfaces);
}

inline void doNetworkAdaptersCollection(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("Not a valid chassis ID{}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] =
        "#NetworkAdapterCollection.NetworkAdapterCollection";
    asyncResp->res.jsonValue["Name"] = "Network Adapter Collection";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/NetworkAdapters", chassisId);

    crow::connections::systemBus->async_method_call(
        [chassisId, asyncResp](
            const boost::system::error_code ec,
            const dbus::utility::MapperGetSubTreePathsResponse& objects) {
            if (ec == boost::system::errc::io_error)
            {
                asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
                asyncResp->res.jsonValue["Members@odata.count"] = 0;
                return;
            }

            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
                messages::internalError(asyncResp->res);
                return;
            }
            std::string dpuString = "host0";
            int networkAdaptersCount = 0;
            std::map<std::string, int> networkAdaptersCollectionMap;
            std::vector<std::string> pathNames;
            for (const auto& object : objects)
            {
                std::string p = object;
                if (p.find(dpuString) == std::string::npos)
                {
                    continue;
                }
                networkAdaptersCount = 1;
                break;
            }
            nlohmann::json& members = asyncResp->res.jsonValue["Members"];
            members = nlohmann::json::array();
            asyncResp->res.jsonValue["Members@odata.count"] =
                networkAdaptersCount;
            if (networkAdaptersCount)
            {
                nlohmann::json::object_t member;
                member["@odata.id"] = boost::urls::format(
                    "/redfish/v1/Chassis/{}/NetworkAdapters/{}", chassisId,
                    BMCWEB_PLATFORM_NETWORK_ADAPTER);
                members.push_back(std::move(member));
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/network/", 0,
        std::array<std::string, 1>{
            "xyz.openbmc_project.Network.EthernetInterface"});
}

inline void
    doNetworkAdapter(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& chassisId,
                     const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("Not a valid chassis ID{}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] =
        "#NetworkAdapter.v1_9_0.NetworkAdapter";
    // Support for reading the values from backend will be done through
    // https://redmine.mellanox.com/issues/3461424
    asyncResp->res.jsonValue["Name"] = BMCWEB_PLATFORM_NETWORK_ADAPTER;
    asyncResp->res.jsonValue["Manufacturer"] = "Nvidia";
    asyncResp->res.jsonValue["Id"] = BMCWEB_PLATFORM_NETWORK_ADAPTER;

    asyncResp->res.jsonValue["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}/NetworkAdapters/",
                            chassisId, BMCWEB_PLATFORM_NETWORK_ADAPTER);
    asyncResp->res.jsonValue["Ports"]["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}/NetworkAdapters/{}/Ports",
                            chassisId, BMCWEB_PLATFORM_NETWORK_ADAPTER);
    asyncResp->res.jsonValue["NetworkDeviceFunctions"]["@odata.id"] =
        boost::urls::format(
            "/redfish/v1/Chassis/{}/NetworkAdapters/{}/NetworkDeviceFunctions",
            chassisId, BMCWEB_PLATFORM_NETWORK_ADAPTER);
}

inline void
    doPortNDFCollection(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& chassisId, bool isPort,
                        const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("Not a valid chassis ID{}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }
    std::string collectionName;
    bool isNDF = true;
    if (isPort)
    {
        collectionName = "/Ports";
        asyncResp->res.jsonValue["@odata.type"] =
            "#PortCollection.PortCollection";
        asyncResp->res.jsonValue["Name"] = "Port Collection";
        asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
            "/redfish/v1/Chassis/{}/NetworkAdapters/{}/Ports", chassisId,
            BMCWEB_PLATFORM_NETWORK_ADAPTER);
        isNDF = false;
    }
    else
    {
        collectionName = "/NetworkDeviceFunctions";
        asyncResp->res.jsonValue["@odata.type"] =
            "#NetworkDeviceFunctionCollection.NetworkDeviceFunctionCollection";
        asyncResp->res.jsonValue["Name"] = "Network Device Function Collection";
        asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
            "/redfish/v1/Chassis/{}/NetworkAdapters/{}/NetworkDeviceFunctions",
            chassisId, BMCWEB_PLATFORM_NETWORK_ADAPTER);
    }
    getNetworkAdapterCollectionMembers(
        asyncResp,
        std::format("/redfish/v1/Chassis/{}/NetworkAdapters/{}{}", chassisId,
                    BMCWEB_PLATFORM_NETWORK_ADAPTER, collectionName),
        isNDF, {"xyz.openbmc_project.Network.EthernetInterface"},
        "/xyz/openbmc_project/network/");
}

inline void handleNetworkAdaptersCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& param)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    const std::string& chassisId = param;

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(&doNetworkAdaptersCollection, asyncResp, chassisId));
}

inline void handleNetworkAdapterGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& param, const std::string& networkId [[maybe_unused]])
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    const std::string& chassisId = param;

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(&doNetworkAdapter, asyncResp, chassisId));
}

inline void handleNetworkDeviceFunctionsCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& param, const std::string& networkId [[maybe_unused]])
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    const std::string& chassisId = param;

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(&doPortNDFCollection, asyncResp, chassisId, false));
}

inline void handlePortsCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& param, const std::string& networkId [[maybe_unused]])
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    const std::string& chassisId = param;

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(&doPortNDFCollection, asyncResp, chassisId, true));
}

inline void doPort(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& objPath, const std::string& service,
                   const std::string& chassisId, const std::string& portId)
{
    asyncResp->res.jsonValue["@odata.type"] = "#Port.v1_6_0.Port";
    asyncResp->res.jsonValue["Id"] = portId;
    asyncResp->res.jsonValue["Name"] = "Port";
    asyncResp->res.jsonValue["LinkNetworkTechnology"] = "Ethernet";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/NetworkAdapters/{}/Ports/{}", chassisId,
        BMCWEB_PLATFORM_NETWORK_ADAPTER, portId);
    using GetManagedPropertyType = boost::container::flat_map<
        std::string,
        std::variant<std::string, bool, double, uint64_t, uint32_t>>;
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const GetManagedPropertyType& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
                messages::internalError(asyncResp->res);
                return;
            }
            for (const auto& property : properties)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "LinkUp")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Cannot read LinkUp property");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if (*value)
                    {
                        asyncResp->res.jsonValue["LinkStatus"] = "LinkUp";
                    }
                    else
                    {
                        asyncResp->res.jsonValue["LinkStatus"] = "LinkDown";
                    }
                }
                if (propertyName == "Speed")
                {
                    const uint32_t* value =
                        std::get_if<uint32_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Cannot read Speed property");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    uint32_t valueInGbps = (*value) / 1000;
                    asyncResp->res.jsonValue["CurrentSpeedGbps"] = valueInGbps;
                }
                if (propertyName == "LinkType")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Cannot read LinkType property");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if (value->find("InfiniBand") != std::string::npos)
                        asyncResp->res.jsonValue["LinkNetworkTechnology"] =
                            "InfiniBand";
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll", "");
}

inline void doNDF(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                  const std::string& objPath, const std::string& service,
                  const std::string& chassisId, const std::string& ndfId,
                  const std::string& portId)
{
    nlohmann::json& links = asyncResp->res.jsonValue["Links"];
    asyncResp->res.jsonValue["@odata.type"] =
        "#NetworkDeviceFunction.v1_9_0.NetworkDeviceFunction";
    links["PhysicalPortAssignment"]["@odata.id"] =
        "/redfish/v1/Chassis/" + chassisId + "/NetworkAdapters/" +
        std::string(BMCWEB_PLATFORM_NETWORK_ADAPTER) + "/Ports/" + portId;
    links["OffloadSystem"]["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME);
    asyncResp->res.jsonValue["Name"] = "NetworkDeviceFunction";
    asyncResp->res.jsonValue["NetDevFuncType"] = "Ethernet";
    asyncResp->res.jsonValue["NetDevFuncCapabilities"] =
        nlohmann::json::array({"Ethernet"});
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Chassis/" + chassisId + "/NetworkAdapters/" +
        std::string(BMCWEB_PLATFORM_NETWORK_ADAPTER) +
        "/NetworkDeviceFunctions/" + ndfId;
    asyncResp->res.jsonValue["Id"] = ndfId;
    using GetManagedPropertyType = boost::container::flat_map<
        std::string,
        std::variant<std::string, bool, double, uint64_t, uint32_t>>;
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const GetManagedPropertyType& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
                messages::internalError(asyncResp->res);
                return;
            }

            for (const auto& property : properties)
            {
                const std::string& propertyName = property.first;

                if (propertyName == "MTU")
                {
                    const uint32_t* value =
                        std::get_if<uint32_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Cannot read MTU property");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Ethernet"]["MTUSize"] = *value;
                }
                if (propertyName == "MACAddress")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Cannot read MACAddress property");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Ethernet"]["MACAddress"] = *value;
                }
                if (propertyName == "InterfaceName")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Cannot read InterfaceName property");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if (value->find("oob") != 0)
                    {
                        auto& capabilitiesArray =
                            asyncResp->res.jsonValue["NetDevFuncCapabilities"];
                        if (std::find(capabilitiesArray.begin(),
                                      capabilitiesArray.end(), "InfiniBand") ==
                            capabilitiesArray.end())
                        {
                            capabilitiesArray.push_back("InfiniBand");
                        }
                    }
                }
                if (propertyName == "LinkType")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Cannot read LinkType property");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if (value->find("InfiniBand") != std::string::npos)
                        asyncResp->res.jsonValue["NetDevFuncType"] =
                            "InfiniBand";
                    else
                        asyncResp->res.jsonValue["NetDevFuncType"] = "Ethernet";
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll", "");
}

inline void handleGet(App& app, const crow::Request& req,
                      const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& chassisId, const std::string& id,
                      bool isNDF = true)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    const std::array<const char*, 1> interfaces = {
        "xyz.openbmc_project.Network.EthernetInterface"};

    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisId, id,
         isNDF](const boost::system::error_code ec,
                const crow::openbmc_mapper::GetSubTreeType& subtree) {
            std::string dpuString = "host0";
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
                messages::internalError(asyncResp->res);
                return;
            }
            // Iterate over all retrieved ObjectPaths.
            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                     object : subtree)
            {
                const std::string& path = object.first;
                const std::vector<
                    std::pair<std::string, std::vector<std::string>>>&
                    connectionNames = object.second;

                if (path.find(dpuString) == std::string::npos)
                {
                    continue;
                }
                sdbusplus::message::object_path objPath(path);
                const std::string& connectionName = connectionNames[0].first;
                if (objPath.filename() != id && objPath.filename() + "f0" != id)
                {
                    continue;
                }
                if (objPath.filename() + "f0" == id && isNDF)
                {
                    doNDF(asyncResp, path, connectionName, chassisId, id,
                          objPath.filename());
                }
                else
                {
                    doPort(asyncResp, path, connectionName, chassisId, id);
                }
                return;
            }
            // Couldn't find an object with that name.  return an error
            messages::resourceNotFound(
                asyncResp->res,
                "#NetworkDeviceFunction.v1_9_0.NetworkDeviceFunction",
                chassisId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/network/", 0, interfaces);
}

inline void handleNDFGet(App& app, const crow::Request& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& chassisId,
                         [[maybe_unused]] const std::string& networkId,
                         const std::string& ndfId)
{
    handleGet(app, req, asyncResp, chassisId, ndfId, true);
}

inline void handlePortGet(App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& chassisId,
                          [[maybe_unused]] const std::string& networkId,
                          const std::string& portId)
{
    handleGet(app, req, asyncResp, chassisId, portId, false);
}

inline void requestRoutesNetworkAdapters(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/NetworkAdapters/")
        .privileges(redfish::privileges::getNetworkAdapterCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleNetworkAdaptersCollectionGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/")
        .privileges(redfish::privileges::getNetworkAdapter)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(&handleNetworkAdapterGet, std::ref(app)));
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/NetworkDeviceFunctions/")
        .privileges(redfish::privileges::getNetworkDeviceFunctionCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleNetworkDeviceFunctionsCollectionGet, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/Ports/")
        .privileges(redfish::privileges::getPortCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePortsCollectionGet, std::ref(app)));
}

inline void requestRoutesNetworkDeviceFunctions(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/NetworkDeviceFunctions/<str>/")
        .privileges(redfish::privileges::getNetworkDeviceFunction)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleNDFGet, std::ref(app)));
}

inline void requestRoutesACDPort(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/Ports/<str>/")
        .privileges(redfish::privileges::getPort)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePortGet, std::ref(app)));
}

} // namespace redfish
