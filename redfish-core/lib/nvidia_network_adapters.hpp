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
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/collection.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_histogram_utils.hpp"

#include <asm-generic/errno.h>

#include <boost/url/format.hpp>

#include <algorithm>
#include <functional>

namespace redfish
{
namespace nvidia
{

inline void applyPortLocationProperty(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& propertyName,
    const dbus::utility::DbusVariantType& propertyValue)
{
    if (propertyName != "LocationCode" && propertyName != "LocationType" &&
        propertyName != "LocationContext" &&
        propertyName != "LocationReference")
    {
        return;
    }

    const std::string* value = std::get_if<std::string>(&propertyValue);
    if (value == nullptr)
    {
        BMCWEB_LOG_DEBUG("Invalid type for optional port {} property",
                         propertyName);
        return;
    }

    if (propertyName == "LocationCode")
    {
        asyncResp->res.jsonValue["Location"]["PartLocation"]["ServiceLabel"] =
            *value;
    }
    else if (propertyName == "LocationType")
    {
        asyncResp->res.jsonValue["Location"]["PartLocation"]["LocationType"] =
            dbus_utils::toLocationType(*value);
    }
    else if (propertyName == "LocationContext")
    {
        asyncResp->res.jsonValue["Location"]["PartLocationContext"] = *value;
    }
    else
    {
        asyncResp->res.jsonValue["Location"]["PartLocation"]["Reference"] =
            dbus_utils::toReference(*value);
    }
}

inline void handlePortLocationData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("Unable to get optional port location properties: {}",
                         ec);
        return;
    }

    for (const auto& [propertyName, propertyValue] : properties)
    {
        applyPortLocationProperty(asyncResp, propertyName, propertyValue);
    }
}

inline void getPortLocationData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objPath)
{
    dbus::utility::getAllProperties(
        service, objPath, "",
        std::bind_front(handlePortLocationData, asyncResp));
}

// Location decorators are exposed on the inventory port. Dynamic port
// data can be exposed on a differently named associated state object.
inline void populatePortLocationData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath)
{
    if constexpr (!BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        return;
    }

    dbus::utility::getDbusObject(
        objPath,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Inventory.Item.Port"},
        [asyncResp, objPath](const boost::system::error_code ec,
                             const dbus::utility::MapperServiceMap& object) {
            if (ec || object.empty())
            {
                BMCWEB_LOG_DEBUG("Unable to resolve port location object {}",
                                 objPath);
                return;
            }
            getPortLocationData(asyncResp, object.front().first, objPath);
        });
}

} // namespace nvidia

inline void getNetworkAdapterCollectionMembersLegacy(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& collectionPath,
    const bool& isNDF, const std::vector<std::string_view>& interfaces,
    const char* subtree = "/xyz/openbmc_project/inventory")
{
    BMCWEB_LOG_DEBUG("Get collection members for: {}", collectionPath);
    dbus::utility::getSubTreePaths(
        subtree, 0, interfaces,
        [collectionPath, isNDF, aResp{std::move(aResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreePathsResponse& objects) {
            // currently host name is hard coded. We will add support for
            // multiple hosts through
            // https://redmine.mellanox.com/issues/3461409
            std::string dpuString = "host0";
            if (ec == boost::system::errc::io_error)
            {
                BMCWEB_LOG_DEBUG(
                    "getNetworkAdapterCollectionMembersLegacy: no objects found (io_error): {}",
                    ec.message());
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
                const std::string& p = object;
                if (p.find(dpuString) == std::string::npos)
                {
                    continue;
                }
                sdbusplus::object_path path(object);
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
            std::ranges::sort(pathNames, AlphanumLess<std::string>());

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
        });
}

inline void doNetworkAdaptersCollectionLegacy(
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
    asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
    asyncResp->res.jsonValue["Members@odata.count"] = 0;

    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/network/", 0,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Network.EthernetInterface"},
        [chassisId, asyncResp](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreePathsResponse& objects) {
            if (ec == boost::system::errc::io_error)
            {
                BMCWEB_LOG_DEBUG(
                    "doNetworkAdaptersCollectionLegacy: no EthernetInterface objects found (io_error): {}",
                    ec.message());
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
                const std::string& p = object;
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
            if (networkAdaptersCount != 0)
            {
                nlohmann::json::object_t member;
                member["@odata.id"] = boost::urls::format(
                    "/redfish/v1/Chassis/{}/NetworkAdapters/{}", chassisId,
                    BMCWEB_PLATFORM_NETWORK_ADAPTER);
                members.push_back(std::move(member));
            }
        });
}

inline void doNetworkAdapterLegacy(
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

inline void doPortNDFCollectionLegacy(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
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
    getNetworkAdapterCollectionMembersLegacy(
        asyncResp,
        std::format("/redfish/v1/Chassis/{}/NetworkAdapters/{}{}", chassisId,
                    BMCWEB_PLATFORM_NETWORK_ADAPTER, collectionName),
        isNDF, {"xyz.openbmc_project.Network.EthernetInterface"},
        "/xyz/openbmc_project/network/");
}

inline void handleNetworkAdaptersCollectionGetLegacy(
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
        std::bind_front(&doNetworkAdaptersCollectionLegacy, asyncResp,
                        chassisId));
}

inline void handleNetworkAdapterGetLegacy(
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
        std::bind_front(&doNetworkAdapterLegacy, asyncResp, chassisId));
}

inline void handleNetworkDeviceFunctionsCollectionGetLegacy(
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
        std::bind_front(&doPortNDFCollectionLegacy, asyncResp, chassisId,
                        false));
}

inline void handlePortsCollectionGetLegacy(
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
        std::bind_front(&doPortNDFCollectionLegacy, asyncResp, chassisId,
                        true));
}

inline void doPortLegacy(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& objPath, const std::string& service,
                         const std::string& chassisId,
                         const std::string& portId)
{
    asyncResp->res.jsonValue["@odata.type"] = "#Port.v1_6_0.Port";
    asyncResp->res.jsonValue["Id"] = portId;
    asyncResp->res.jsonValue["Name"] = "Port";
    asyncResp->res.jsonValue["LinkNetworkTechnology"] = "Ethernet";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/NetworkAdapters/{}/Ports/{}", chassisId,
        BMCWEB_PLATFORM_NETWORK_ADAPTER, portId);
    dbus::utility::getAllProperties(
        service, objPath, "",
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::DBusPropertiesMap& properties) {
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
                    {
                        asyncResp->res.jsonValue["LinkNetworkTechnology"] =
                            "InfiniBand";
                    }
                }
            }
        });
}

inline void doNDFLegacy(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
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
    dbus::utility::getAllProperties(
        service, objPath, "",
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::DBusPropertiesMap& properties) {
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
                    if (!value->starts_with("oob"))
                    {
                        auto& capabilitiesArray =
                            asyncResp->res.jsonValue["NetDevFuncCapabilities"];
                        if (std::ranges::find(capabilitiesArray,
                                              "InfiniBand") ==
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
                    {
                        asyncResp->res.jsonValue["NetDevFuncType"] =
                            "InfiniBand";
                    }
                    else
                    {
                        asyncResp->res.jsonValue["NetDevFuncType"] = "Ethernet";
                    }
                }
            }
        });
}

inline void handleGetLegacy(App& app, const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisId, const std::string& id,
                            bool isNDF = true)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    const std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Network.EthernetInterface"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/network/", 0, interfaces,
        [asyncResp, chassisId, id,
         isNDF](const boost::system::error_code& ec,
                const dbus::utility::GetSubTreeType& subtree) {
            std::string dpuString = "host0";
            if (ec)
            {
                if (ec.value() == EBADR)
                {
                    messages::resourceNotFound(asyncResp->res, "Port", id);
                }
                else
                {
                    BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
                    messages::internalError(asyncResp->res);
                }
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
                sdbusplus::object_path objPath(path);
                const std::string& connectionName = connectionNames[0].first;
                if (objPath.filename() != id && objPath.filename() + "f0" != id)
                {
                    continue;
                }
                if (objPath.filename() + "f0" == id && isNDF)
                {
                    doNDFLegacy(asyncResp, path, connectionName, chassisId, id,
                                objPath.filename());
                }
                else
                {
                    doPortLegacy(asyncResp, path, connectionName, chassisId,
                                 id);
                }
                return;
            }
            // Couldn't find an object with that name.  return an error
            messages::resourceNotFound(
                asyncResp->res,
                "#NetworkDeviceFunction.v1_9_0.NetworkDeviceFunction",
                chassisId);
        });
}

inline void handleNDFGetLegacy(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, [[maybe_unused]] const std::string& networkId,
    const std::string& ndfId)
{
    handleGetLegacy(app, req, asyncResp, chassisId, ndfId, true);
}

inline void handlePortGetLegacy(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, [[maybe_unused]] const std::string& networkId,
    const std::string& portId)
{
    handleGetLegacy(app, req, asyncResp, chassisId, portId, false);
}

inline void doNetworkAdapterPortHistogramCollectionLegacy(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkId,
    const std::string& portId,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("Not a valid chassis ID{}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }
    const std::string portPath = *validChassisPath + "/NetworkAdapters/" +
                                 networkId + "/Ports/" + portId;
    const std::string collectionUri =
        "/redfish/v1/Chassis/" + chassisId + "/NetworkAdapters/" + networkId +
        "/Ports/" + portId + "/Oem/Nvidia/Histograms";
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaHistogramCollection.NvidiaHistogramCollection";
    asyncResp->res.jsonValue["@odata.id"] = collectionUri;
    asyncResp->res.jsonValue["Name"] =
        networkId + "_" + portId + "_Histogram_Collection";
    collection_util::getCollectionMembersByAssociation(
        asyncResp, collectionUri, portPath + "/histograms", {});
}

inline void handleNetworkAdapterPortHistogramCollectionGetLegacy(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkId,
    const std::string& portId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(&doNetworkAdapterPortHistogramCollectionLegacy,
                        asyncResp, chassisId, networkId, portId));
}

inline void doNetworkAdapterPortHistogramLegacy(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkId,
    const std::string& portId, const std::string& histogramId,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("Not a valid chassis ID{}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }
    const std::string portPath = *validChassisPath + "/NetworkAdapters/" +
                                 networkId + "/Ports/" + portId;
    const std::string histoURI =
        "/redfish/v1/Chassis/" + chassisId + "/NetworkAdapters/" + networkId +
        "/Ports/" + portId + "/Oem/Nvidia/Histograms/" + histogramId;
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaHistogram.v1_1_0.NvidiaHistogram";
    asyncResp->res.jsonValue["@odata.id"] = histoURI;
    asyncResp->res.jsonValue["Id"] = histogramId;
    asyncResp->res.jsonValue["Name"] =
        networkId + "_" + portId + "_Histogram_" + histogramId;
    asyncResp->res.jsonValue["HistogramBuckets"]["@odata.id"] =
        histoURI + "/Buckets";
    redfish::nvidia_histogram_utils::getHistogramDataByAssociation(
        asyncResp, histogramId, portPath);
}

inline void handleNetworkAdapterPortHistogramGetLegacy(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkId,
    const std::string& portId, const std::string& histogramId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(&doNetworkAdapterPortHistogramLegacy, asyncResp,
                        chassisId, networkId, portId, histogramId));
}

inline void doNetworkAdapterPortHistogramBucketsLegacy(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkId,
    const std::string& portId, const std::string& histogramId,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("Not a valid chassis ID{}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }
    const std::string portPath = *validChassisPath + "/NetworkAdapters/" +
                                 networkId + "/Ports/" + portId;
    dbus::utility::getAssociationEndPoints(
        portPath + "/histograms",
        [asyncResp, chassisId, networkId, portId,
         histogramId](const boost::system::error_code& ec,
                      const dbus::utility::MapperEndPoints& histoPaths) {
            if (ec)
            {
                messages::resourceNotFound(
                    asyncResp->res, "#NvidiaHistogram.v1_1_0.NvidiaHistogram",
                    histogramId);
                return;
            }
            for (const std::string& histoPath : histoPaths)
            {
                sdbusplus::object_path histoObjPath(histoPath);
                if (histoObjPath.filename() != histogramId)
                {
                    continue;
                }
                const std::string histoURI = std::format(
                    "/redfish/v1/Chassis/{}/NetworkAdapters/{}/Ports/{}/Oem/Nvidia/Histograms/{}/Buckets",
                    chassisId, networkId, portId, histogramId);
                asyncResp->res.jsonValue["@odata.type"] =
                    "#NvidiaHistogramBuckets.v1_0_0.NvidiaHistogramBuckets";
                asyncResp->res.jsonValue["@odata.id"] = histoURI;
                asyncResp->res.jsonValue["Name"] =
                    std::format("{}_{}_Histogram_{}_Buckets", networkId, portId,
                                histogramId);
                asyncResp->res.jsonValue["Id"] = "Buckets";
                asyncResp->res.jsonValue["Buckets"] = nlohmann::json::array();
                redfish::nvidia_histogram_utils::updateHistogramBucketData(
                    asyncResp, histoPath);
                return;
            }
            messages::resourceNotFound(
                asyncResp->res, "#NvidiaHistogram.v1_1_0.NvidiaHistogram",
                histogramId);
        });
}

inline void handleNetworkAdapterPortHistogramBucketsGetLegacy(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkId,
    const std::string& portId, const std::string& histogramId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(&doNetworkAdapterPortHistogramBucketsLegacy, asyncResp,
                        chassisId, networkId, portId, histogramId));
}

inline void requestRoutesNetworkAdaptersLegacy(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/NetworkAdapters/")
        .privileges(redfish::privileges::getNetworkAdapterCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleNetworkAdaptersCollectionGetLegacy, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/")
        .privileges(redfish::privileges::getNetworkAdapter)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(&handleNetworkAdapterGetLegacy, std::ref(app)));
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/NetworkDeviceFunctions/")
        .privileges(redfish::privileges::getNetworkDeviceFunctionCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleNetworkDeviceFunctionsCollectionGetLegacy, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/Ports/")
        .privileges(redfish::privileges::getPortCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePortsCollectionGetLegacy, std::ref(app)));
}

inline void requestRoutesNetworkDeviceFunctionsLegacy(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/NetworkDeviceFunctions/<str>/")
        .privileges(redfish::privileges::getNetworkDeviceFunction)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleNDFGetLegacy, std::ref(app)));
}

inline void requestRoutesACDPortLegacy(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/Ports/<str>/")
        .privileges(redfish::privileges::getPort)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePortGetLegacy, std::ref(app)));
}

inline void requestRoutesNetworkAdapterPortHistogramLegacy(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/Ports/<str>/Oem/Nvidia/Histograms/")
        .privileges(redfish::privileges::getPort)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleNetworkAdapterPortHistogramCollectionGetLegacy,
            std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/Ports/<str>/Oem/Nvidia/Histograms/<str>/")
        .privileges(redfish::privileges::getPort)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleNetworkAdapterPortHistogramGetLegacy, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/Ports/<str>/Oem/Nvidia/Histograms/<str>/Buckets/")
        .privileges(redfish::privileges::getPort)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleNetworkAdapterPortHistogramBucketsGetLegacy, std::ref(app)));
}

} // namespace redfish
