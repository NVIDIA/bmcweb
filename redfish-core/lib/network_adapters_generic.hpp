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

#include <utils/chassis_utils.hpp>
#include <utils/collection.hpp>
#include <utils/json_utils.hpp>
#include <utils/port_utils.hpp>

#include <map>
#include <optional>
#include <string>

namespace redfish
{

/**
 * @brief Retrieves valid getValidNetworkAdapter path
 * @param asyncResp   Pointer to object holding response data
 * @param callback  Callback for next step to get valid NetworkInterface path
 */
template <typename Callback>
void getValidNetworkAdapterPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& networkAdapterId,
    const std::vector<std::string>& chassisIntfList,
    const std::string& chassisObjPath, Callback&& callback)
{
    const std::string networkInterface =
        "xyz.openbmc_project.Inventory.Item.NetworkInterface";

    if (std::find(chassisIntfList.begin(), chassisIntfList.end(),
                  networkInterface) != chassisIntfList.end())
    {
        // networkInterface at the same chassis objPath
        const std::array<const char*, 1> interfaces = {
            "xyz.openbmc_project.Inventory.Item.NetworkInterface"};

        auto respHandler =
            [callback{std::forward<Callback>(callback)}, asyncResp,
             networkAdapterId](
                const boost::system::error_code ec,
                const dbus::utility::MapperGetSubTreePathsResponse&
                    networkAdapterPaths) mutable {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "getValidNetworkAdapterPath respHandler DBUS error: {}",
                    ec);
                messages::internalError(asyncResp->res);
                return;
            }

            std::optional<std::string> networkAdapterPath;
            std::string networkAdapterName;
            for (const std::string& networkAdapter : networkAdapterPaths)
            {
                sdbusplus::message::object_path path(networkAdapter);
                networkAdapterName = path.filename();
                if (networkAdapterName.empty())
                {
                    BMCWEB_LOG_ERROR("Failed to find '/' in {}",
                                     networkAdapter);
                    continue;
                }
                if (networkAdapterName == networkAdapterId)
                {
                    networkAdapterPath = networkAdapter;
                    break;
                }
            }
            callback(networkAdapterPath);
            return;
        };

        // Get the NetworkAdatper Collection
        crow::connections::systemBus->async_method_call(
            respHandler, "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
            "/xyz/openbmc_project/inventory", 0, interfaces);
    }
    else
    {
        crow::connections::systemBus->async_method_call(
            [callback{std::forward<Callback>(callback)}, asyncResp,
             chassisObjPath,
             networkAdapterId](const boost::system::error_code ec,
                               std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "getValidNetworkAdapterPath respHandler DBUS error: {}",
                    ec);
                messages::internalError(asyncResp->res);
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_ERROR("no network_adapter found {}", chassisObjPath);
                messages::internalError(asyncResp->res);
                return;
            }

            std::optional<std::string> validNetworkAdapterPath;
            for (const std::string& networkAdapterPath : *data)
            {
                sdbusplus::message::object_path networkAdapterObjPath(
                    networkAdapterPath);
                const std::string& networkAdapterName =
                    networkAdapterObjPath.filename();
                if (networkAdapterName.empty())
                {
                    BMCWEB_LOG_ERROR("Failed to find '/' in {}",
                                     networkAdapterPath);
                    continue;
                }
                if (networkAdapterName == networkAdapterId)
                {
                    validNetworkAdapterPath = networkAdapterPath;
                    break;
                }
            }
            callback(validNetworkAdapterPath);
        },
            "xyz.openbmc_project.ObjectMapper",
            chassisObjPath + "/network_adapters",
            "org.freedesktop.DBus.Properties", "Get",
            "xyz.openbmc_project.Association", "endpoints");
    }
}

inline void doNetworkAdaptersCollection(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, std::vector<std::string>& chassisIntfList,
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

    const std::string networkInterface =
        "xyz.openbmc_project.Inventory.Item.NetworkInterface";
    std::string path = *validChassisPath;

    if (std::find(chassisIntfList.begin(), chassisIntfList.end(),
                  networkInterface) != chassisIntfList.end())
    {
        // networkInterface at the same chassis objPath
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

            nlohmann::json& members = asyncResp->res.jsonValue["Members"];
            members = nlohmann::json::array();
            for (const auto& object : objects)
            {
                sdbusplus::message::object_path path(object);
                std::string parentPath = path.parent_path();

                if (parentPath.find(chassisId) != std::string::npos ||
                    path.filename() == chassisId)
                {
                    nlohmann::json::object_t member;
                    member["@odata.id"] = boost::urls::format(
                        "/redfish/v1/Chassis/{}/NetworkAdapters/{}", chassisId,
                        path.filename());
                    members.push_back(std::move(member));
                }
            }

            asyncResp->res.jsonValue["Members@odata.count"] = members.size();
            return;
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
            "/xyz/openbmc_project/inventory/", 0,
            std::array<std::string, 1>{
                "xyz.openbmc_project.Inventory.Item.NetworkInterface"});
    }

    // get network adapter on chassis by association
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisId](const boost::system::error_code ec,
                               std::variant<std::vector<std::string>>& resp) {
        if (ec == boost::system::errc::io_error)
        {
            asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
            asyncResp->res.jsonValue["Members@odata.count"] = 0;
            return;
        }

        if (ec)
        {
            return;
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            BMCWEB_LOG_ERROR("DBUS response error");
            messages::internalError(asyncResp->res);
            return;
        }

        nlohmann::json& members = asyncResp->res.jsonValue["Members"];
        members = nlohmann::json::array();
        for (const std::string& networkAdapterPath : *data)
        {
            sdbusplus::message::object_path networkAdapterObjPath(
                networkAdapterPath);
            const std::string& networkAdapterId =
                networkAdapterObjPath.filename();
            if (networkAdapterId.empty())
            {
                messages::internalError(asyncResp->res);
                return;
            }
            nlohmann::json::object_t member;
            member["@odata.id"] =
                boost::urls::format("/redfish/v1/Chassis/{}/NetworkAdapters/{}",
                                    chassisId, networkAdapterId);
            members.push_back(std::move(member));
        }
        asyncResp->res.jsonValue["Members@odata.count"] = members.size();
        return;
    },
        "xyz.openbmc_project.ObjectMapper", path + "/network_adapters",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline std::string convertHealthToRF(const std::string& health)
{
    if (health == "xyz.openbmc_project.State.Decorator.Health.HealthType.OK")
    {
        return "OK";
    }
    if (health ==
        "xyz.openbmc_project.State.Decorator.Health.HealthType.Warning")
    {
        return "Warning";
    }
    if (health ==
        "xyz.openbmc_project.State.Decorator.Health.HealthType.Critical")
    {
        return "Critical";
    }
    // Unknown or others
    return "";
}

inline void getHealthData(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& service,
                          const std::string& objPath)
{
    using PropertiesMap =
        boost::container::flat_map<std::string,
                                   std::variant<std::string, size_t>>;

    // Get interface properties
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const PropertiesMap& properties) {
        if (ec)
        {
            messages::internalError(asyncResp->res);
            return;
        }

        for (const auto& property : properties)
        {
            const std::string& propertyName = property.first;
            if (propertyName == "Health")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for port type");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["Status"]["Health"] =
                    convertHealthToRF(*value);
            }
        }
    },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.State.Decorator.Health");
}

inline void
    getHealthByAssociation(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& objPath,
                           const std::string& networkAdapterId)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath,
         networkAdapterId](const boost::system::error_code& ec,
                           std::variant<std::vector<std::string>>& response) {
        std::string objectPathOfChassis = objPath;
        if (!ec)
        {
            std::vector<std::string>* pathData =
                std::get_if<std::vector<std::string>>(&response);
            if (pathData != nullptr)
            {
                for (const std::string& parentChassisPath : *pathData)
                {
                    objectPathOfChassis = parentChassisPath;
                }
            }
        }
        crow::connections::systemBus->async_method_call(
            [asyncResp,
             networkAdapterId](const boost::system::error_code& ec,
                               std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                // no state sensors attached.
                BMCWEB_LOG_DEBUG("DBUS response error");
                return;
            }

            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error while getting all_states");
                messages::internalError(asyncResp->res);
                return;
            }

            for (const std::string& sensorPath : *data)
            {
                if (!boost::ends_with(sensorPath, networkAdapterId))
                {
                    continue;
                }
                // Check Interface in Object or not
                crow::connections::systemBus->async_method_call(
                    [asyncResp, sensorPath, networkAdapterId](
                        const boost::system::error_code ec,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& object) {
                    if (ec)
                    {
                        // the path does not implement Decorator Health
                        // interfaces
                        BMCWEB_LOG_DEBUG("No Health interface found");
                        return;
                    }
                    getHealthData(asyncResp, object.front().first, sensorPath);
                },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetObject", sensorPath,
                    std::array<std::string, 1>(
                        {"xyz.openbmc_project.State.Decorator.Health"}));
            }
        },
            "xyz.openbmc_project.ObjectMapper",
            objectPathOfChassis + "/all_states",
            "org.freedesktop.DBus.Properties", "Get",
            "xyz.openbmc_project.Association", "endpoints");
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getAssetData(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& objPath,
                         const std::string& networkAdapterId)
{
    using PropertyType = std::variant<std::string>;
    using PropertiesMap = boost::container::flat_map<std::string, PropertyType>;

    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath, networkAdapterId](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                object) {
        if (ec)
        {
            // the path does not implement Decorator Asset
            // interfaces
            return;
        }

        std::string service = object.front().first;

        // Get interface properties
        crow::connections::systemBus->async_method_call(
            [asyncResp, service](const boost::system::error_code ec,
                                 const PropertiesMap& properties) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            for (const auto& property : properties)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "Manufacturer")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        asyncResp->res.jsonValue["Manufacturer"] = *value;
                    }
                }
                else if (propertyName == "SerialNumber")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        asyncResp->res.jsonValue["SerialNumber"] = *value;
                    }
                }
                else if (propertyName == "PartNumber")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        asyncResp->res.jsonValue["PartNumber"] = *value;
                    }
                }
                else if (propertyName == "Model")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        asyncResp->res.jsonValue["Model"] = *value;
                    }
                }
            }
        },
            service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
            "xyz.openbmc_project.Inventory.Decorator.Asset");
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
        std::array<std::string, 1>(
            {"xyz.openbmc_project.Inventory.Decorator.Asset"}));
}

inline void
    doNetworkAdapter(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& chassisId,
                     const std::string& networkAdapterId,
                     const std::optional<std::string>& validNetworkAdapterPath)
{
    if (!validNetworkAdapterPath)
    {
        BMCWEB_LOG_ERROR("Not a valid networkAdapter ID{}", networkAdapterId);
        messages::resourceNotFound(asyncResp->res, "NetworkAdapter",
                                   networkAdapterId);
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] =
        "#NetworkAdapter.v1_9_0.NetworkAdapter";
    asyncResp->res.jsonValue["Name"] = networkAdapterId;
    asyncResp->res.jsonValue["Id"] = networkAdapterId;

    asyncResp->res.jsonValue["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}/NetworkAdapters/{}",
                            chassisId, networkAdapterId);

    asyncResp->res.jsonValue["Ports"]["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}/NetworkAdapters/{}/Ports",
                            chassisId, networkAdapterId);
    asyncResp->res.jsonValue["Status"]["State"] = "Enabled";

#ifndef BMCWEB_DISABLE_HEALTH_ROLLUP
    asyncResp->res.jsonValue["Status"]["HealthRollup"] = "OK";
#endif // BMCWEB_DISABLE_HEALTH_ROLLUP
#ifndef BMCWEB_DISABLE_CONDITIONS_ARRAY
    asyncResp->res.jsonValue["Status"]["Conditions"] = nlohmann::json::array();
#endif // BMCWEB_DISABLE_CONDITIONS_ARRAY

    asyncResp->res.jsonValue["Controllers"] = nlohmann::json::array();

    nlohmann::json controllerObject;
    controllerObject["Links"]["PCIeDevices"] = nlohmann::json::array();
    controllerObject["Links"]["Ports"] = nlohmann::json::array();
    controllerObject["PCIeInterface"] = nlohmann::json::object();

    asyncResp->res.jsonValue["Controllers"].emplace_back(controllerObject);

    getAssetData(asyncResp, *validNetworkAdapterPath, networkAdapterId);
    getHealthByAssociation(asyncResp, *validNetworkAdapterPath,
                           networkAdapterId);
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
    auto& nvidiaJson = asyncResp->res.jsonValue["Oem"]["Nvidia"];
    nvidiaJson["@odata.type"] =
        "#NvidiaNetworkAdapter.v1_0_0.NvidiaNetworkAdapter";
    nvidiaJson["ErrorInjection"] = {
        {"@odata.id", "/redfish/v1/Chassis/" + chassisId + "/NetworkAdapters/" +
                          networkAdapterId + "/Oem/Nvidia/ErrorInjection"}};
#endif // BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
}

inline void
    doPortCollection(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& chassisId,
                     const std::string& networkAdapterId,
                     const std::optional<std::string>& validNetworkAdapterPath)
{
    if (!validNetworkAdapterPath)
    {
        BMCWEB_LOG_ERROR("Not a valid chassis ID{}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] = "#PortCollection.PortCollection";
    asyncResp->res.jsonValue["Name"] = "Port Collection";
    asyncResp->res.jsonValue["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}/NetworkAdapters/{}/Ports",
                            chassisId, networkAdapterId);
    std::string path = *validNetworkAdapterPath;

    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/Chassis/" + chassisId +
                                            "/NetworkAdapters/" +
                                            networkAdapterId + "/Ports";
    asyncResp->res.jsonValue["@odata.type"] = "#PortCollection.PortCollection";
    asyncResp->res.jsonValue["Name"] = "Port Collection";

    collection_util::getCollectionMembersByAssociation(
        asyncResp,
        "/redfish/v1/Chassis/" + chassisId + "/NetworkAdapters/" +
            networkAdapterId + "/Ports",
        path + "/all_states", {"xyz.openbmc_project.Inventory.Item.Port"});
}

inline void handleNetworkAdaptersCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPathAndInterfaces(
        asyncResp, chassisId,
        std::bind_front(doNetworkAdaptersCollection, asyncResp, chassisId));
}

inline void handleNetworkAdapterGetNext(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
    const std::vector<std::string>& chassisIntfList,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("Not a valid chassis ID{}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    getValidNetworkAdapterPath(asyncResp, networkAdapterId, chassisIntfList,
                               *validChassisPath,
                               std::bind_front(doNetworkAdapter, asyncResp,
                                               chassisId, networkAdapterId));
}

inline void
    handleNetworkAdapterGet(App& app, const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisId,
                            const std::string& networkAdapterId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPathAndInterfaces(
        asyncResp, chassisId,
        std::bind_front(handleNetworkAdapterGetNext, asyncResp, chassisId,
                        networkAdapterId));
}

inline void doPortCollectionWithValidChassisId(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
    const std::vector<std::string>& chassisIntfList,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("Not a valid chassis ID{}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    getValidNetworkAdapterPath(asyncResp, networkAdapterId, chassisIntfList,
                               *validChassisPath,
                               std::bind_front(doPortCollection, asyncResp,
                                               chassisId, networkAdapterId));
}

inline void handlePortsCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId,
    [[maybe_unused]] const std::string& networkAdapterId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPathAndInterfaces(
        asyncResp, chassisId,
        std::bind_front(doPortCollectionWithValidChassisId, asyncResp,
                        chassisId, networkAdapterId));
}

inline void getPortData(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& service, const std::string& objPath,
                        const std::string& chassisId,
                        const std::string& networkAdapterId,
                        const std::string& portId)
{
    asyncResp->res.jsonValue["@odata.type"] = "#Port.v1_6_0.Port";
    asyncResp->res.jsonValue["Id"] = portId;
    asyncResp->res.jsonValue["Name"] = "Port";
    asyncResp->res.jsonValue["LinkNetworkTechnology"] = "Ethernet";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/NetworkAdapters/{}/Ports/{}", chassisId,
        networkAdapterId, portId);
    asyncResp->res.jsonValue["Metrics"]["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/NetworkAdapters/{}/Ports/{}/Metrics", chassisId,
        networkAdapterId, portId);

    using PropertiesMap =
        boost::container::flat_map<std::string,
                                   std::variant<std::string, size_t, double>>;
    // Get interface properties
    crow::connections::systemBus->async_method_call(
        [asyncResp, networkAdapterId](const boost::system::error_code ec,
                                      const PropertiesMap& properties) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error");
            messages::internalError(asyncResp->res);
            return;
        }

        for (const auto& property : properties)
        {
            const std::string& propertyName = property.first;
            if (propertyName == "Type")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for port type");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["PortType"] =
                    port_utils::getPortType(*value);
            }
            else if (propertyName == "CurrentSpeed")
            {
                const double* value = std::get_if<double>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for current speed");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["CurrentSpeedGbps"] = *value;
            }
            else if (propertyName == "MaxSpeed")
            {
                const double* value = std::get_if<double>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for MaxSpeed");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["MaxSpeedGbps"] = *value;
            }
            else if ((propertyName == "Width") ||
                     (propertyName == "ActiveWidth"))
            {
                const auto* value = std::get_if<size_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for Width or ActiveWidth");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue[propertyName] = *value;
            }
            else if (propertyName == "Protocol")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for protocol type");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["PortProtocol"] =
                    port_utils::getPortProtocol(*value);
            }
            else if (propertyName == "LinkStatus")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for link status");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["LinkStatus"] =
                    port_utils::getLinkStatusType(*value);
                if (*value ==
                        "xyz.openbmc_project.Inventory.Decorator.PortState.LinkStatusType.LinkDown" ||
                    *value ==
                        "xyz.openbmc_project.Inventory.Decorator.PortState.LinkStatusType.LinkUp")
                {
                    asyncResp->res.jsonValue["Status"]["Health"] = "OK";
                }
                else if (
                    *value ==
                    "xyz.openbmc_project.Inventory.Decorator.PortState.LinkStatusType.NoLink")
                {
                    asyncResp->res.jsonValue["Status"]["Health"] = "Critical";
                }
            }
            else if (propertyName == "LinkState")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for link state");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["LinkState"] =
                    port_utils::getLinkStates(*value);
                if (*value ==
                    "xyz.openbmc_project.Inventory.Decorator.PortState.LinkStates.Enabled")
                {
                    asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
                }
                else if (
                    *value ==
                    "xyz.openbmc_project.Inventory.Decorator.PortState.LinkStates.Disabled")
                {
                    asyncResp->res.jsonValue["Status"]["State"] = "Disabled";
                }
                else if (
                    *value ==
                    "xyz.openbmc_project.Inventory.Decorator.PortState.LinkStates.Error")
                {
                    asyncResp->res.jsonValue["Status"]["State"] =
                        "UnavailableOffline";
                }
                else
                {
                    asyncResp->res.jsonValue["Status"]["State"] = "Absent";
                }
            }
        }
    },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll", "");

#ifndef BMCWEB_DISABLE_HEALTH_ROLLUP
    asyncResp->res.jsonValue["Status"]["HealthRollup"] = "OK";
#endif // BMCWEB_DISABLE_HEALTH_ROLLUP
       // update health rollup
#ifdef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE
    std::shared_ptr<HealthRollup> health = std::make_shared<HealthRollup>(
        objPath, [asyncResp](const std::string& rootHealth,
                             const std::string& healthRollup) {
        asyncResp->res.jsonValue["Status"]["Health"] = rootHealth;
#ifndef BMCWEB_DISABLE_HEALTH_ROLLUP
        asyncResp->res.jsonValue["Status"]["HealthRollup"] = healthRollup;
#endif // BMCWEB_DISABLE_HEALTH_ROLLUP
    });
    health->start();

#endif // ifdef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE
#ifndef BMCWEB_DISABLE_CONDITIONS_ARRAY
    redfish::conditions_utils::populateServiceConditions(asyncResp,
                                                         networkAdapterId);
#endif // BMCWEB_DISABLE_CONDITIONS_ARRAY
}

inline void getSwitchPorts(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& portPath,
                           const std::string& fabricId,
                           const std::string& switchName)
{
    BMCWEB_LOG_DEBUG("Get connected switch ports on {}", switchName);
    crow::connections::systemBus->async_method_call(
        [asyncResp, portPath, fabricId,
         switchName](const boost::system::error_code ec,
                     std::variant<std::vector<std::string>>& resp) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("Get connected switch failed on{}", switchName);
            return;
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            BMCWEB_LOG_DEBUG("No response data on {} switch_port association",
                             portPath);
            return;
        }
        nlohmann::json& switchlinksArray =
            asyncResp->res.jsonValue["Links"]["ConnectedSwitchPorts"];
        for (const std::string& portPath1 : *data)
        {
            sdbusplus::message::object_path objectPath(portPath1);
            std::string portId = objectPath.filename();
            if (portId.empty())
            {
                BMCWEB_LOG_ERROR("Unable to fetch port");
                messages::internalError(asyncResp->res);
                return;
            }
            nlohmann::json thisPort = nlohmann::json::object();
            std::string portUri = "/redfish/v1/Fabrics/" + fabricId;
            portUri += "/Switches/" + switchName + "/Ports/";
            portUri += portId;
            thisPort["@odata.id"] = portUri;
            switchlinksArray.push_back(std::move(thisPort));
        }
    },
        "xyz.openbmc_project.ObjectMapper", portPath + "/switch_port",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void
    getConnectedSwitch(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& switchPath,
                       const std::string& portPath,
                       const std::string& switchName)
{
    BMCWEB_LOG_DEBUG("Get connected switch on{}", switchName);
    crow::connections::systemBus->async_method_call(
        [asyncResp, switchPath, portPath,
         switchName](const boost::system::error_code ec,
                     std::variant<std::vector<std::string>>& resp) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("Dbus response error");
            return;
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            BMCWEB_LOG_DEBUG("Get connected switch failed on: {}", switchName);
            return;
        }
        for (const std::string& fabricPath : *data)
        {
            sdbusplus::message::object_path objectPath(fabricPath);
            std::string fabricId = objectPath.filename();
            if (fabricId.empty())
            {
                BMCWEB_LOG_ERROR("Empty fabrics Id");
                messages::internalError(asyncResp->res);
                return;
            }
            nlohmann::json& switchlinksArray =
                asyncResp->res.jsonValue["Links"]["ConnectedSwitches"];
            nlohmann::json thisSwitch = nlohmann::json::object();
            std::string switchUri = "/redfish/v1/Fabrics/";
            switchUri += fabricId;
            switchUri += "/Switches/";
            switchUri += switchName;
            thisSwitch["@odata.id"] = switchUri;
            switchlinksArray.push_back(std::move(thisSwitch));
            getSwitchPorts(asyncResp, portPath, fabricId, switchName);
        }
    },
        "xyz.openbmc_project.ObjectMapper", switchPath + "/fabrics",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void updatePortLink(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const std::string& objPath,
                           const std::string& chassisId,
                           const std::string& networkAdapterId,
                           const std::string& portId)
{
    BMCWEB_LOG_DEBUG("Get associated Port Links");
    crow::connections::systemBus->async_method_call(
        [aResp, objPath, chassisId, networkAdapterId,
         portId](const boost::system::error_code ec,
                 std::variant<std::vector<std::string>>& resp) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("Get associated switch failed on: {}", objPath);
            return;
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            BMCWEB_LOG_DEBUG("No data when getting associated switch on: {}",
                             objPath);
            return;
        }
        nlohmann::json& switchlinksArray =
            aResp->res.jsonValue["Links"]["ConnectedSwitches"];
        switchlinksArray = nlohmann::json::array();
        nlohmann::json& portlinksArray =
            aResp->res.jsonValue["Links"]["ConnectedSwitchPorts"];
        portlinksArray = nlohmann::json::array();
        for (const std::string& switchPath : *data)
        {
            sdbusplus::message::object_path objectPath(switchPath);
            std::string switchName = objectPath.filename();
            if (switchName.empty())
            {
                BMCWEB_LOG_ERROR("Empty switch name");
                messages::internalError(aResp->res);
                return;
            }
            getConnectedSwitch(aResp, switchPath, objPath, switchName);
        }
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/associated_switch",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getPortDataByAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath, const std::string& chassisId,
    const std::string& networkAdapterId, const std::string& portId)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisId, networkAdapterId,
         portId](const boost::system::error_code& ec,
                 std::variant<std::vector<std::string>>& resp) {
        if (ec)
        {
            // no state sensors attached.
            BMCWEB_LOG_ERROR("DBUS response error");
            messages::internalError(asyncResp->res);
            return;
        }

        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            BMCWEB_LOG_ERROR("DBUS response error while getting ports");
            messages::internalError(asyncResp->res);
            return;
        }

        for (const std::string& sensorPath : *data)
        {
            sdbusplus::message::object_path pPath(sensorPath);
            if (pPath.filename() != portId)
            {
                continue;
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp, chassisId, networkAdapterId, portId,
                 sensorPath](const boost::system::error_code& ec,
                             std::variant<std::vector<std::string>>& response) {
                std::string objectPathToGetPortData = sensorPath;
                if (!ec)
                {
                    std::vector<std::string>* pathData =
                        std::get_if<std::vector<std::string>>(&response);
                    if (pathData != nullptr)
                    {
                        for (const std::string& associatedPortPath : *pathData)
                        {
                            objectPathToGetPortData = associatedPortPath;
                        }
                    }
                }
                // Check Interface in Object or not
                crow::connections::systemBus->async_method_call(
                    [asyncResp, objectPathToGetPortData, chassisId,
                     networkAdapterId, portId](
                        const boost::system::error_code ec,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& object) {
                    if (ec)
                    {
                        // the path does not implement item port
                        // interfaces
                        BMCWEB_LOG_DEBUG("no port interface on object path {}",
                                         objectPathToGetPortData);
                        return;
                    }

                    sdbusplus::message::object_path path(
                        objectPathToGetPortData);
                    if (path.filename() != portId || object.size() != 1)
                    {
                        return;
                    }

                    getPortData(asyncResp, object.front().first,
                                objectPathToGetPortData, chassisId,
                                networkAdapterId, portId);
                },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetObject",
                    objectPathToGetPortData,
                    std::array<std::string, 1>(
                        {"xyz.openbmc_project.Inventory.Item.Port"}));
            },
                "xyz.openbmc_project.ObjectMapper",
                sensorPath + "/associated_port",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Association", "endpoints");

            updatePortLink(asyncResp, sensorPath, chassisId, networkAdapterId,
                           portId);
            return;
        }
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_states",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void doPort(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& chassisId,
                   const std::string& networkAdapterId,
                   const std::string& portId,
                   const std::optional<std::string>& validNetworkAdapterPath)
{
    if (!validNetworkAdapterPath)
    {
        BMCWEB_LOG_ERROR("Not a valid networkAdapter ID{}", networkAdapterId);
        messages::resourceNotFound(asyncResp->res, "networkAdapter",
                                   networkAdapterId);
        return;
    }

    getPortDataByAssociation(asyncResp, *validNetworkAdapterPath, chassisId,
                             networkAdapterId, portId);
}

inline void doPortWithValidChassisId(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
    const std::string& portId, const std::vector<std::string>& chassisIntfList,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("Not a valid chassis ID{}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    getValidNetworkAdapterPath(asyncResp, networkAdapterId, chassisIntfList,
                               *validChassisPath,
                               std::bind_front(doPort, asyncResp, chassisId,
                                               networkAdapterId, portId));
}

inline void handlePortGet(App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& chassisId,
                          const std::string& networkAdapterId,
                          const std::string& portId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPathAndInterfaces(
        asyncResp, chassisId,
        std::bind_front(doPortWithValidChassisId, asyncResp, chassisId,
                        networkAdapterId, portId));
}

/**
 * @brief Get all port metric info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void
    getPortMetricsData(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Port Metric Data");
    using PropertiesMap =
        boost::container::flat_map<std::string, dbus::utility::DbusVariantType>;
    // Get interface properties
    crow::connections::systemBus->async_method_call(
        [asyncResp{asyncResp}](const boost::system::error_code ec,
                               const PropertiesMap& properties) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error");
            messages::internalError(asyncResp->res);
            return;
        }

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
        auto addNvidiaType = false;
#endif // BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
        static const std::map<std::string, std::optional<std::string>>
            pcieErrorsProperties{
                {"ceCount", "CorrectableErrorCount"},
                {"feCount", "FatalErrorCount"},
                {"nonfeCount", "NonFatalErrorCount"},
                {"UnsupportedRequestCount", std::nullopt},
                {"L0ToRecoveryCount", std::nullopt},
                {"ReplayCount", std::nullopt},
                {"ReplayRolloverCount", std::nullopt},
                {"NAKSentCount", std::nullopt},
                {"NAKReceivedCount", std::nullopt},
            };
        for (const auto& property : properties)
        {
            if ((property.first == "TXBytes") || (property.first == "RXBytes"))
            {
                const size_t* value = std::get_if<size_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for TX/RX bytes");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue[property.first] = *value;
            }
            else if (property.first == "RXErrors")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for receive error");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["RXErrors"] = *value;
            }
            else if (property.first == "RXPkts")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for receive packets");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["Networking"]["RXFrames"] = *value;
            }
            else if (property.first == "TXPkts")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for transmit packets");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["Networking"]["TXFrames"] = *value;
            }
            else if (property.first == "RXMulticastPkts")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for receive multicast packets");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["Networking"]["RXMulticastFrames"] =
                    *value;
            }
            else if (property.first == "TXMulticastPkts")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for transmit multicast packets");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["Networking"]["TXMulticastFrames"] =
                    *value;
            }
            else if (property.first == "RXUnicastPkts")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for receive unicast packets");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["Networking"]["RXUnicastFrames"] =
                    *value;
            }
            else if (property.first == "TXUnicastPkts")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for transmit unicast packets");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["Networking"]["TXUnicastFrames"] =
                    *value;
            }
            else if (property.first == "TXDiscardPkts")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for transmit discard packets");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["Networking"]["TXDiscards"] = *value;
            }
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
            else if (property.first == "VL15DroppedPkts")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for VL15 dropped packets");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["Oem"]["Nvidia"]["VL15Dropped"] =
                    *value;
                addNvidiaType = true;
            }
            else if (property.first == "SymbolError")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for symbol error");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["Oem"]["Nvidia"]["SymbolErrors"] =
                    *value;
                addNvidiaType = true;
            }
            else if (property.first == "LinkErrorRecoveryCounter")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for link error recovery count");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                        ["LinkErrorRecoveryCount"] = *value;
                addNvidiaType = true;
            }
            else if (property.first == "RXRemotePhysicalErrorPkts")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for receive remote physical error");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                        ["RXRemotePhysicalErrors"] = *value;
                addNvidiaType = true;
            }
            else if (property.first == "RXSwitchRelayErrorPkts")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for receive switch replay error");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res
                    .jsonValue["Oem"]["Nvidia"]["RXSwitchRelayErrors"] = *value;
                addNvidiaType = true;
            }
            else if (property.first == "LinkDownCount")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for link down count");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["Oem"]["Nvidia"]["LinkDownedCount"] =
                    *value;
                addNvidiaType = true;
            }
#endif // BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
            for (const auto& [pdiPropertyName, fixedPropertyName] :
                 pcieErrorsProperties)
            {
                if (property.first == pdiPropertyName)
                {
                    const auto propertyName = fixedPropertyName
                                                  ? *fixedPropertyName
                                                  : pdiPropertyName;
                    const auto* value = std::get_if<int64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned for {}",
                                         propertyName);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["PCIeErrors"][propertyName] =
                        *value;
                }
            }
        }
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
        if (addNvidiaType)
        {
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaPortMetrics.v1_3_0.NvidiaNVLinkPortMetrics";
        }
#endif // BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
    },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll", "");
}

inline void getPortMetricsDataByAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath, const std::string& chassisId,
    const std::string& networkAdapterId, const std::string& portId)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisId, networkAdapterId,
         portId](const boost::system::error_code& ec,
                 std::variant<std::vector<std::string>>& resp) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error");
            messages::internalError(asyncResp->res);
            return;
        }

        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            BMCWEB_LOG_ERROR("No response data while getting ports");
            messages::internalError(asyncResp->res);
            return;
        }

        for (const std::string& sensorPath : *data)
        {
            // Check Interface in Object or not
            crow::connections::systemBus->async_method_call(
                [asyncResp, sensorPath, chassisId, networkAdapterId,
                 portId](const boost::system::error_code ec,
                         const std::vector<std::pair<
                             std::string, std::vector<std::string>>>& object) {
                if (ec)
                {
                    // the path does not implement item port metric
                    // interfaces
                    BMCWEB_LOG_DEBUG(
                        "Port interface not present on object path {}",
                        sensorPath);
                    return;
                }

                sdbusplus::message::object_path path(sensorPath);
                if (path.filename() != portId || object.size() != 1)
                {
                    return;
                }
                asyncResp->res.jsonValue["@odata.type"] =
                    "#PortMetrics.v1_6_1.PortMetrics";
                asyncResp->res.jsonValue["Id"] = portId;
                asyncResp->res.jsonValue["Name"] = portId + " Port Metrics";
                asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
                    "/redfish/v1/Chassis/{}/NetworkAdapters/{}/Ports/{}/Metrics",
                    chassisId, networkAdapterId, portId);

                getPortMetricsData(asyncResp, object.front().first, sensorPath);
            },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetObject", sensorPath,
                std::array<std::string, 1>(
                    {"xyz.openbmc_project.Inventory.Item.Port"}));
        }
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_states",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void
    doPortMetrics(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                  const std::string& chassisId,
                  const std::string& networkAdapterId,
                  const std::string& portId,
                  const std::optional<std::string>& validNetworkAdapterPath)
{
    if (!validNetworkAdapterPath)
    {
        BMCWEB_LOG_ERROR("Not a valid networkAdapter ID{}", networkAdapterId);
        messages::resourceNotFound(asyncResp->res, "networkAdapter",
                                   networkAdapterId);
        return;
    }

    getPortMetricsDataByAssociation(asyncResp, *validNetworkAdapterPath,
                                    chassisId, networkAdapterId, portId);
}

inline void doPortMetricsWithValidChassisId(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
    const std::string& portId, const std::vector<std::string>& chassisIntfList,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("Not a valid chassis ID{}", chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    getValidNetworkAdapterPath(
        asyncResp, networkAdapterId, chassisIntfList, *validChassisPath,
        std::bind_front(doPortMetrics, asyncResp, chassisId, networkAdapterId,
                        portId));
}

inline void
    handlePortMetricsGet(App& app, const crow::Request& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& chassisId,
                         const std::string& networkAdapterId,
                         const std::string& portId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPathAndInterfaces(
        asyncResp, chassisId,
        std::bind_front(doPortMetricsWithValidChassisId, asyncResp, chassisId,
                        networkAdapterId, portId));
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
            std::bind_front(handleNetworkAdapterGet, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/Ports/")
        .privileges(redfish::privileges::getPortCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePortsCollectionGet, std::ref(app)));
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/Ports/<str>/")
        .privileges(redfish::privileges::getPort)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePortGet, std::ref(app)));
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/Ports/<str>/Metrics/")
        .privileges(redfish::privileges::getPortMetrics)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handlePortMetricsGet, std::ref(app)));
}

} // namespace redfish
