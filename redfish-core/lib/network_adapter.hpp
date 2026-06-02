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

#include <asm-generic/errno.h>

#include <utils/chassis_utils.hpp>
#include <utils/collection.hpp>
#include <utils/conditions_utils.hpp>
#include <utils/json_utils.hpp>
#include <utils/nvidia_network_adapters_utils.hpp>
#include <utils/nvidia_pcie_utils.hpp>
#include <utils/nvidia_utils.hpp>
#include <utils/pcie_util.hpp>
#include <utils/port_utils.hpp>

#include <algorithm>
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

    if (std::ranges::find(chassisIntfList, networkInterface) !=
        chassisIntfList.end())
    {
        // networkInterface at the same chassis objPath
        const std::array<std::string_view, 1> interfaces = {
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
        dbus::utility::getSubTreePaths("/xyz/openbmc_project/inventory", 0,
                                       interfaces, respHandler);
    }
    else
    {
        dbus::utility::getProperty<std::vector<std::string>>(
            "xyz.openbmc_project.ObjectMapper",
            chassisObjPath + "/network_adapters",
            "xyz.openbmc_project.Association", "endpoints",
            [callback{std::forward<Callback>(callback)}, asyncResp,
             chassisObjPath,
             networkAdapterId](const boost::system::error_code ec,
                               const std::vector<std::string>& resp) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "getValidNetworkAdapterPath respHandler DBUS error: {}",
                        ec);
                    messages::internalError(asyncResp->res);
                    return;
                }

                std::optional<std::string> validNetworkAdapterPath;
                for (const std::string& networkAdapterPath : resp)
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
            });
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
    const std::string& path = *validChassisPath;

    if (std::ranges::find(chassisIntfList, networkInterface) !=
        chassisIntfList.end())
    {
        // networkInterface at the same chassis objPath
        dbus::utility::getSubTreePaths(
            "/xyz/openbmc_project/inventory/", 0,
            std::array<std::string_view, 1>{
                "xyz.openbmc_project.Inventory.Item.NetworkInterface"},
            [chassisId, asyncResp](
                const boost::system::error_code ec,
                const dbus::utility::MapperGetSubTreePathsResponse& objects) {
                if (ec == boost::system::errc::io_error)
                {
                    asyncResp->res.jsonValue["Members"] =
                        nlohmann::json::array();
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
                    sdbusplus::message::object_path path2(object);
                    std::string parentPath = path2.parent_path();

                    if (parentPath.find(chassisId) != std::string::npos ||
                        path2.filename() == chassisId)
                    {
                        nlohmann::json::object_t member;
                        member["@odata.id"] = boost::urls::format(
                            "/redfish/v1/Chassis/{}/NetworkAdapters/{}",
                            chassisId, path2.filename());
                        members.push_back(std::move(member));
                    }
                }

                asyncResp->res.jsonValue["Members@odata.count"] =
                    members.size();
                return;
            });
    }

    // get network adapter on chassis by association
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", path + "/network_adapters",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, chassisId](const boost::system::error_code ec,
                               const std::vector<std::string>& resp) {
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
            nlohmann::json& members = asyncResp->res.jsonValue["Members"];
            members = nlohmann::json::array();
            for (const std::string& networkAdapterPath : resp)
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
                member["@odata.id"] = boost::urls::format(
                    "/redfish/v1/Chassis/{}/NetworkAdapters/{}", chassisId,
                    networkAdapterId);
                members.push_back(std::move(member));
            }
            asyncResp->res.jsonValue["Members@odata.count"] = members.size();
            return;
        });
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
    // Get interface properties
    dbus::utility::getAllProperties(
        service, objPath, "xyz.openbmc_project.State.Decorator.Health",
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::DBusPropertiesMap& properties) {
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
        });
}

inline void getHealthByAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath, const std::string& networkAdapterId)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, objPath,
         networkAdapterId](const boost::system::error_code& ec,
                           const std::vector<std::string>& response) {
            std::string objectPathOfChassis = objPath;
            if (!ec)
            {
                for (const std::string& parentChassisPath : response)
                {
                    objectPathOfChassis = parentChassisPath;
                }
            }
            dbus::utility::getProperty<std::vector<std::string>>(
                "xyz.openbmc_project.ObjectMapper",
                objectPathOfChassis + "/all_states",
                "xyz.openbmc_project.Association", "endpoints",
                [asyncResp,
                 networkAdapterId](const boost::system::error_code& ec1,
                                   const std::vector<std::string>& resp) {
                    if (ec1)
                    {
                        // no state sensors attached.
                        BMCWEB_LOG_DEBUG("DBUS response error");
                        return;
                    }

                    for (const std::string& sensorPath : resp)
                    {
                        if (!sensorPath.ends_with(networkAdapterId))
                        {
                            continue;
                        }
                        // Check Interface in Object or not
                        dbus::utility::getDbusObject(
                            sensorPath,
                            std::array<std::string_view, 1>{
                                "xyz.openbmc_project.State.Decorator.Health"},
                            [asyncResp, sensorPath, networkAdapterId](
                                const boost::system::error_code ec2,
                                const dbus::utility::MapperGetObject& object) {
                                if (ec2)
                                {
                                    // the path does not implement Decorator
                                    // Health interfaces
                                    BMCWEB_LOG_DEBUG(
                                        "No Health interface found");
                                    return;
                                }
                                getHealthData(asyncResp, object.front().first,
                                              sensorPath);
                            });
                    }
                });
        });
}

inline void getAssetData(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& objPath,
                         const std::string& networkAdapterId)
{
    dbus::utility::getDbusObject(
        objPath,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Inventory.Decorator.Asset"},
        [asyncResp, objPath,
         networkAdapterId](const boost::system::error_code& ec,
                           const dbus::utility::MapperGetObject& object) {
            if (ec)
            {
                // the path does not implement Decorator Asset
                // interfaces
                return;
            }

            std::string service = object.front().first;

            // Get interface properties
            dbus::utility::getAllProperties(
                service, objPath,
                "xyz.openbmc_project.Inventory.Decorator.Asset",
                [asyncResp,
                 service](const boost::system::error_code ec1,
                          const dbus::utility::DBusPropertiesMap& properties) {
                    if (ec1)
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
                                asyncResp->res.jsonValue["Manufacturer"] =
                                    *value;
                            }
                        }
                        else if (propertyName == "SerialNumber")
                        {
                            const std::string* value =
                                std::get_if<std::string>(&property.second);
                            if (value != nullptr)
                            {
                                asyncResp->res.jsonValue["SerialNumber"] =
                                    *value;
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
                });
        });
}

inline void getPCIeInterfaceData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& deviceId, const std::string& path,
    const std::shared_ptr<nlohmann::json>& controllerObject)
{
    dbus::utility::getDbusObject(
        path,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Inventory.Item.PCIeDevice"},
        [asyncResp, deviceId, path, controllerObject](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                object) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Error no PCIeDevice interface on {} path",
                                 path);
                messages::internalError(asyncResp->res);
                return;
            }
            auto service = object.front().first;

            dbus::utility::getAllProperties(
                service, path, "",
                [asyncResp, deviceId, controllerObject](
                    const boost::system::error_code ec1,
                    const dbus::utility::DBusPropertiesMap& propertiesList) {
                    if (ec1)
                    {
                        BMCWEB_LOG_ERROR(
                            "Error no getting data from interface on {}",
                            deviceId);
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (const auto& property : propertiesList)
                    {
                        const std::string& propertyName = property.first;
                        if (propertyName == "MaxLanes")
                        {
                            const size_t* value =
                                std::get_if<size_t>(&property.second);
                            if (value != nullptr)
                            {
                                (*controllerObject)["PCIeInterface"]
                                                   [propertyName] = *value;
                            }
                        }
                        else if (propertyName == "LanesInUse")
                        {
                            const size_t* value =
                                std::get_if<size_t>(&property.second);
                            if (value != nullptr)
                            {
                                if (*value == INT_MAX)
                                {
                                    (*controllerObject)["PCIeInterface"]
                                                       [propertyName] = 0;
                                }
                                else
                                {
                                    (*controllerObject)["PCIeInterface"]
                                                       [propertyName] = *value;
                                }
                            }
                        }
                        else if ((propertyName == "PCIeType") ||
                                 (propertyName == "MaxPCIeType"))
                        {
                            const std::string* value =
                                std::get_if<std::string>(&property.second);
                            if (value != nullptr)
                            {
                                std::optional<std::string> propValue =
                                    pcie_util::redfishPcieTypeStringFromDbus(
                                        *value);
                                if (!propValue)
                                {
                                    (*controllerObject)["PCIeInterface"]
                                                       [propertyName] = nullptr;
                                }
                                else
                                {
                                    (*controllerObject)["PCIeInterface"]
                                                       [propertyName] =
                                                           *propValue;
                                }
                            }
                        }
                        else if (propertyName == "GenerationInUse")
                        {
                            const std::string* value =
                                std::get_if<std::string>(&property.second);
                            std::optional<std::string> generationInUse =
                                pcie_util::redfishPcieGenerationStringFromDbus(
                                    *value);
                            if (!generationInUse)
                            {
                                (*controllerObject)["PCIeInterface"]
                                                   ["PCIeType"] = nullptr;
                            }
                            else
                            {
                                (*controllerObject)["PCIeInterface"]
                                                   ["PCIeType"] =
                                                       *generationInUse;
                            }
                        }
                    }
                    asyncResp->res.jsonValue["Controllers"].emplace_back(
                        *controllerObject);
                });
        });
}

inline void getPCIeData(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& devicePath,
                        const std::string& chassisId,
                        const std::string& networkAdapterId,
                        const std::shared_ptr<nlohmann::json>& controllerObject)
{
    BMCWEB_LOG_DEBUG("Get PCIe interface data and PCIe device on {}",
                     networkAdapterId);
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", devicePath + "/parent_chassis",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, chassisId, networkAdapterId,
         controllerObject](const boost::system::error_code ec,
                           const std::vector<std::string>& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Get chassis failed on{}", networkAdapterId);
                return;
            }

            for (const std::string& chassisPath : resp)
            {
                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper",
                    chassisPath + "/pciedevice",
                    "xyz.openbmc_project.Association", "endpoints",
                    [asyncResp, chassisId, networkAdapterId, controllerObject](
                        const boost::system::error_code ec1,
                        const std::vector<std::string>& pcieResp) {
                        if (ec1)
                        {
                            BMCWEB_LOG_DEBUG(
                                "Get PCIe interface data and PCIe device failed on {}",
                                networkAdapterId);
                            return;
                        }
                        std::string pcieDeviceId;
                        std::string pcieDevicePath;
                        for (const std::string& path : pcieResp)
                        {
                            sdbusplus::message::object_path objectPath(path);
                            std::string deviceId = objectPath.filename();
                            if (deviceId.empty())
                            {
                                BMCWEB_LOG_ERROR(
                                    "PCIe device id on path empty");
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            nlohmann::json thisPort = nlohmann::json::object();
                            pcieDevicePath = path;
                            pcieDeviceId = deviceId;
                            std::string portUri =
                                "/redfish/v1/Chassis/" + chassisId;
                            portUri += "/PCIeDevices/";
                            portUri += deviceId;
                            thisPort["@odata.id"] = portUri;
                            (*controllerObject)["Links"]["PCIeDevices"]
                                .push_back(thisPort);
                        }

                        getPCIeInterfaceData(asyncResp, pcieDeviceId,
                                             pcieDevicePath, controllerObject);
                    });
            }
        });
}

inline void getControllersData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& devicePath, const std::string& chassisId,
    const std::string& networkAdapterId)
{
    asyncResp->res.jsonValue["Controllers"] = nlohmann::json::array();
    std::shared_ptr<nlohmann::json> controllerObject =
        std::make_shared<nlohmann::json>();
    (*controllerObject)["Links"]["PCIeDevices"] = nlohmann::json::array();
    (*controllerObject)["Links"]["Ports"] = nlohmann::json::array();
    (*controllerObject)["PCIeInterface"] = nlohmann::json::object();

    BMCWEB_LOG_DEBUG("Get ports available on {}", networkAdapterId);
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", devicePath + "/all_states",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, chassisId, devicePath, networkAdapterId,
         controllerObject](const boost::system::error_code ec,
                           const std::vector<std::string>& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Get ports failed on{}", networkAdapterId);
                return;
            }

            for (const std::string& portPath : resp)
            {
                sdbusplus::message::object_path objectPath(portPath);
                std::string portId = objectPath.filename();
                if (portId.empty())
                {
                    BMCWEB_LOG_ERROR("Port id on port path empty");
                    messages::internalError(asyncResp->res);
                    return;
                }
                nlohmann::json thisPort = nlohmann::json::object();
                std::string portUri = "/redfish/v1/Chassis/" + chassisId;
                portUri += "/NetworkAdapters/" + networkAdapterId + "/Ports/";
                portUri += portId;
                thisPort["@odata.id"] = portUri;
                (*controllerObject)["Links"]["Ports"].push_back(thisPort);
            }
            getPCIeData(asyncResp, devicePath, chassisId, networkAdapterId,
                        controllerObject);
        });
}

inline void populateNDFURI(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& networkAdapterPath,
                           const std::string& chassisId,
                           const std::string& networkAdapterId)
{
    std::string ndfAssociationPath =
        networkAdapterPath + "/network_device_functions";

    dbus::utility::findAssociations(ndfAssociationPath, [asyncResp, chassisId,
                                                         networkAdapterId](
                                                            const boost::
                                                                system::
                                                                    error_code&
                                                                        ec,
                                                            const std::vector<
                                                                std::string>&
                                                            /*resp*/) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG(
                "network_device_functions association not found for network adapter {} : {}",
                networkAdapterId, ec.message());
            return;
        }

        asyncResp->res.jsonValue["NetworkDeviceFunctions"]
                                ["@odata.id"] = boost::urls::format(
            "/redfish/v1/Chassis/{}/NetworkAdapters/{}/NetworkDeviceFunctions",
            chassisId, networkAdapterId);
    });
}

inline void doNetworkAdapter(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
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
        "#NetworkAdapter.v1_11_0.NetworkAdapter";
    asyncResp->res.jsonValue["Name"] = networkAdapterId;
    asyncResp->res.jsonValue["Id"] = networkAdapterId;

    asyncResp->res.jsonValue["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}/NetworkAdapters/{}",
                            chassisId, networkAdapterId);

    asyncResp->res.jsonValue["Ports"]["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}/NetworkAdapters/{}/Ports",
                            chassisId, networkAdapterId);

    asyncResp->res.jsonValue["Actions"]["#NetworkAdapter.Reset"] = {
        {"target",
         boost::urls::format(
             "/redfish/v1/Chassis/{}/NetworkAdapters/{}/Actions/NetworkAdapter.Reset",
             chassisId, networkAdapterId)},
        {"ResetType@Redfish.AllowableValues", {"ForceRestart"}}};

    asyncResp->res.jsonValue["Status"]["State"] = "Enabled";

    if constexpr (!BMCWEB_DISABLE_HEALTH_ROLLUP)
    {
        asyncResp->res.jsonValue["Status"]["HealthRollup"] = "OK";
    } // BMCWEB_DISABLE_HEALTH_ROLLUP
    if constexpr (!BMCWEB_DISABLE_CONDITIONS_ARRAY)
    {
        asyncResp->res.jsonValue["Status"]["Conditions"] =
            nlohmann::json::array();
    } // BMCWEB_DISABLE_CONDITIONS_ARRAY

    populateNDFURI(asyncResp, *validNetworkAdapterPath, chassisId,
                   networkAdapterId);
    getControllersData(asyncResp, *validNetworkAdapterPath, chassisId,
                       networkAdapterId);
    getAssetData(asyncResp, *validNetworkAdapterPath, networkAdapterId);
    getHealthByAssociation(asyncResp, *validNetworkAdapterPath,
                           networkAdapterId);
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        redfish::nvidia_network_adapters_utils::populateErrorInjectionLink(
            asyncResp, chassisId, networkAdapterId, *validNetworkAdapterPath);
        redfish::nvidia_network_adapters_utils::populateProtectionOptions(
            asyncResp, chassisId, networkAdapterId, *validNetworkAdapterPath);
        redfish::nvidia_network_adapters_utils::populateDeviceModeSettings(
            asyncResp, chassisId, networkAdapterId, *validNetworkAdapterPath,
            true);
    }
}

inline void doPortCollection(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
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
    const std::string& path = *validNetworkAdapterPath;

    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Chassis/" + chassisId + "/NetworkAdapters/" +
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

    getValidNetworkAdapterPath(
        asyncResp, networkAdapterId, chassisIntfList, *validChassisPath,
        std::bind_front(doNetworkAdapter, asyncResp, chassisId,
                        networkAdapterId));
}

inline void handleNetworkAdapterGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId)
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

inline void doNetworkAdapterPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& networkAdapterId,
    const std::optional<std::string>& protectionOption,
    const std::optional<std::string>& validNetworkAdapterPath)
{
    if (!validNetworkAdapterPath)
    {
        BMCWEB_LOG_ERROR("Not a valid networkAdapter ID{}", networkAdapterId);
        messages::resourceNotFound(asyncResp->res, "NetworkAdapter",
                                   networkAdapterId);
        return;
    }

    if (protectionOption)
    {
        dbus::utility::getDbusObject(
            *validNetworkAdapterPath, std::array<std::string_view, 0>{},
            [asyncResp, networkAdapterId, protectionOption,
             validNetworkAdapterPath](
                const boost::system::error_code ec,
                const dbus::utility::MapperServiceMap& serviceMap) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "GetObject failed for NetworkAdapter {}: {}",
                        networkAdapterId, ec.message());
                    messages::internalError(asyncResp->res);
                    return;
                }

                redfish::nvidia_network_adapters_utils::patchProtectionOption(
                    asyncResp, *protectionOption, *validNetworkAdapterPath,
                    serviceMap);
            });
    }
}

inline void handleNetworkAdapterPatchNext(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
    const std::optional<std::string>& protectionOption,
    const std::vector<std::string>& chassisIntfList,
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
        std::bind_front(doNetworkAdapterPatch, asyncResp, networkAdapterId,
                        protectionOption));
}

inline void handleNetworkAdapterPatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::optional<std::string> protectionOption;
    if (!redfish::json_util::readJsonPatch(req, asyncResp->res,
                                           "Oem/Nvidia/ProtectionOption",
                                           protectionOption))
    {
        return;
    }

    if (!protectionOption)
    {
        BMCWEB_LOG_ERROR("No ProtectionOption field provided for PATCH");
        return;
    }

    redfish::chassis_utils::getValidChassisPathAndInterfaces(
        asyncResp, chassisId,
        std::bind_front(handleNetworkAdapterPatchNext, asyncResp, chassisId,
                        networkAdapterId, protectionOption));
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

    getValidNetworkAdapterPath(
        asyncResp, networkAdapterId, chassisIntfList, *validChassisPath,
        std::bind_front(doPortCollection, asyncResp, chassisId,
                        networkAdapterId));
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

    // Get interface properties
    dbus::utility::getAllProperties(
        service, objPath, "",
        [asyncResp,
         networkAdapterId](const boost::system::error_code ec,
                           const dbus::utility::DBusPropertiesMap& properties) {
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
                    asyncResp->res.jsonValue["LinkNetworkTechnology"] =
                        port_utils::getLinkNetworkTechnology(*value);
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
                        asyncResp->res.jsonValue["Status"]["Health"] =
                            "Critical";
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
                        asyncResp->res.jsonValue["Status"]["State"] =
                            "Disabled";
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
        });

    if constexpr (!BMCWEB_DISABLE_HEALTH_ROLLUP)
    {
        asyncResp->res.jsonValue["Status"]["HealthRollup"] = "OK";
    } // BMCWEB_DISABLE_HEALTH_ROLLUP
      // update health rollup
    if constexpr (BMCWEB_HEALTH_ROLLUP_ALTERNATIVE)
    {
        std::shared_ptr<HealthRollup> health = std::make_shared<HealthRollup>(
            objPath, [asyncResp](const std::string& rootHealth,
                                 const std::string& healthRollup) {
                asyncResp->res.jsonValue["Status"]["Health"] = rootHealth;
                if constexpr (!BMCWEB_DISABLE_HEALTH_ROLLUP)
                {
                    asyncResp->res.jsonValue["Status"]["HealthRollup"] =
                        healthRollup;
                } // BMCWEB_DISABLE_HEALTH_ROLLUP
            });
        health->start();

    } // ifdef BMCWEB_HEALTH_ROLLUP_ALTERNATIVE
    if constexpr (!BMCWEB_DISABLE_CONDITIONS_ARRAY)
    {
        redfish::conditions_utils::populateServiceConditions(asyncResp,
                                                             networkAdapterId);
    } // BMCWEB_DISABLE_CONDITIONS_ARRAY
}

inline void getSwitchPorts(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& portPath,
                           const std::string& fabricId,
                           const std::string& switchName)
{
    BMCWEB_LOG_DEBUG("Get connected switch ports on {}", switchName);
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", portPath + "/switch_port",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, portPath, fabricId,
         switchName](const boost::system::error_code ec,
                     const std::vector<std::string>& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Get connected switch failed on{}",
                                 switchName);
                return;
            }

            nlohmann::json& switchlinksArray =
                asyncResp->res.jsonValue["Links"]["ConnectedSwitchPorts"];
            for (const std::string& portPath1 : resp)
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
        });
}

inline void getConnectedSwitch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& switchPath, const std::string& portPath,
    const std::string& switchName)
{
    BMCWEB_LOG_DEBUG("Get connected switch on{}", switchName);
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", switchPath + "/fabrics",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, switchPath, portPath,
         switchName](const boost::system::error_code ec,
                     const std::vector<std::string>& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Dbus response error");
                return;
            }
            for (const std::string& fabricPath : resp)
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
        });
}

inline void updatePortLink(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& chassisId, const std::string& networkAdapterId,
    const std::string& portId)
{
    BMCWEB_LOG_DEBUG("Get associated Port Links");
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/associated_switch",
        "xyz.openbmc_project.Association", "endpoints",
        [aResp, objPath, chassisId, networkAdapterId,
         portId](const boost::system::error_code ec,
                 const std::vector<std::string>& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Get associated switch failed on: {}",
                                 objPath);
                return;
            }
            nlohmann::json& switchlinksArray =
                aResp->res.jsonValue["Links"]["ConnectedSwitches"];
            switchlinksArray = nlohmann::json::array();
            nlohmann::json& portlinksArray =
                aResp->res.jsonValue["Links"]["ConnectedSwitchPorts"];
            portlinksArray = nlohmann::json::array();
            for (const std::string& switchPath : resp)
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
        });
}

inline void getPortDataByAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath, const std::string& chassisId,
    const std::string& networkAdapterId, const std::string& portId)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_states",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, chassisId, networkAdapterId,
         portId](const boost::system::error_code& ec,
                 const std::vector<std::string>& resp) {
            if (ec)
            {
                if (ec.value() == EBADR)
                {
                    // no state sensors attached.
                    messages::resourceNotFound(asyncResp->res, "Port", portId);
                }
                else
                {
                    BMCWEB_LOG_ERROR("DBUS response error");
                    messages::internalError(asyncResp->res);
                }
                return;
            }

            for (const std::string& sensorPath : resp)
            {
                sdbusplus::message::object_path pPath(sensorPath);
                if (pPath.filename() != portId)
                {
                    continue;
                }

                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper",
                    sensorPath + "/associated_port",
                    "xyz.openbmc_project.Association", "endpoints",
                    [asyncResp, chassisId, networkAdapterId, portId,
                     sensorPath](const boost::system::error_code& ec1,
                                 const std::vector<std::string>& response) {
                        std::string objectPathToGetPortData = sensorPath;
                        if (!ec1)
                        {
                            for (const std::string& associatedPortPath :
                                 response)
                            {
                                objectPathToGetPortData = associatedPortPath;
                            }
                        }
                        // Check Interface in Object or not
                        dbus::utility::getDbusObject(
                            objectPathToGetPortData,
                            std::array<std::string_view, 1>{
                                "xyz.openbmc_project.Inventory.Item.Port"},
                            [asyncResp, objectPathToGetPortData, chassisId,
                             networkAdapterId, portId](
                                const boost::system::error_code ec2,
                                const std::vector<std::pair<
                                    std::string, std::vector<std::string>>>&
                                    object) {
                                if (ec2)
                                {
                                    // the path does not implement item port
                                    // interfaces
                                    BMCWEB_LOG_DEBUG(
                                        "no port interface on object path {}",
                                        objectPathToGetPortData);
                                    return;
                                }

                                sdbusplus::message::object_path path(
                                    objectPathToGetPortData);
                                if (path.filename() != portId ||
                                    object.size() != 1)
                                {
                                    return;
                                }

                                getPortData(asyncResp, object.front().first,
                                            objectPathToGetPortData, chassisId,
                                            networkAdapterId, portId);
                            });
                    });

                updatePortLink(asyncResp, sensorPath, chassisId,
                               networkAdapterId, portId);
                return;
            }
        });
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

    getValidNetworkAdapterPath(
        asyncResp, networkAdapterId, chassisIntfList, *validChassisPath,
        std::bind_front(doPort, asyncResp, chassisId, networkAdapterId,
                        portId));
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

inline void populateEthPortMetrics(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& propertyName,
    const dbus::utility::DbusVariantType& propertyValue)
{
    using PropertiesNameMap =
        boost::container::flat_map<std::string, std::string>;
    static const PropertiesNameMap ethernetMetricsProperties{
        {"RXBroadcastPkts", "RXBroadcastFrames"},
        {"TXBroadcastPkts", "TXBroadcastFrames"},
        {"RXFCSErrors", "RXFCSErrors"},
        {"RXAlignmentErrors", "RXFrameAlignmentErrors"},
        {"RXFalseCarrierDetections", "RXFalseCarrierErrors"},
        {"RXRuntPkts", "RXUndersizeFrames"},
        {"RXJabberPkts", "RXOversizeFrames"},
        {"RXXONFrames", "RXPauseXONFrames"},
        {"RXXOFFFrames", "RXPauseXOFFFrames"},
        {"TXXONFrames", "TXPauseXONFrames"},
        {"TXXOFFFrames", "TXPauseXOFFFrames"},
        {"TXSingleCollisionFrames", "TXSingleCollisions"},
        {"TXMultipleCollisionFrames", "TXMultipleCollisions"},
        {"TXLateCollisionFrames", "TXLateCollisions"},
        {"TXExcessCollisionFrames", "TXExcessiveCollisions"}};

    auto it = ethernetMetricsProperties.find(propertyName);
    if (it != ethernetMetricsProperties.end())
    {
        const auto* value = std::get_if<uint64_t>(&propertyValue);
        if (value == nullptr)
        {
            BMCWEB_LOG_ERROR("Null value returned for Port Metric {}",
                             it->second);
            return;
        }
        asyncResp->res.jsonValue["Networking"][it->second] = *value;
    }
}

inline void populateIBPortMetrics(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& propertyName,
    const dbus::utility::DbusVariantType& propertyValue, bool& addNvidiaType)
{
    if (propertyName == "RXErrors")
    {
        const auto* value = std::get_if<uint64_t>(&propertyValue);
        if (value == nullptr)
        {
            BMCWEB_LOG_ERROR("Null value returned "
                             "for receive error");
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["RXErrors"] = *value;
    }
    else if (propertyName == "RXPkts")
    {
        const auto* value = std::get_if<uint64_t>(&propertyValue);
        if (value == nullptr)
        {
            BMCWEB_LOG_ERROR("Null value returned "
                             "for receive packets");
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["Networking"]["RXFrames"] = *value;
    }
    else if (propertyName == "TXPkts")
    {
        const auto* value = std::get_if<uint64_t>(&propertyValue);
        if (value == nullptr)
        {
            BMCWEB_LOG_ERROR("Null value returned "
                             "for transmit packets");
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["Networking"]["TXFrames"] = *value;
    }
    else if (propertyName == "TXDiscardPkts")
    {
        const auto* value = std::get_if<uint64_t>(&propertyValue);
        if (value == nullptr)
        {
            BMCWEB_LOG_ERROR("Null value returned "
                             "for transmit discard packets");
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["Networking"]["TXDiscards"] = *value;
    }
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        if (propertyName == "VL15DroppedPkts")
        {
            const auto* value = std::get_if<uint64_t>(&propertyValue);
            if (value == nullptr)
            {
                BMCWEB_LOG_ERROR("Null value returned "
                                 "for VL15 dropped packets");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["VL15Dropped"] = *value;
            addNvidiaType = true;
        }
        else if (propertyName == "LinkErrorRecoveryCounter")
        {
            const auto* value = std::get_if<uint64_t>(&propertyValue);
            if (value == nullptr)
            {
                BMCWEB_LOG_ERROR("Null value returned "
                                 "for link error recovery count");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["LinkErrorRecoveryCount"] = *value;
            addNvidiaType = true;
        }
        else if (propertyName == "RXRemotePhysicalErrorPkts")
        {
            const auto* value = std::get_if<uint64_t>(&propertyValue);
            if (value == nullptr)
            {
                BMCWEB_LOG_ERROR("Null value returned "
                                 "for receive remote physical error");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["RXRemotePhysicalErrors"] = *value;
            addNvidiaType = true;
        }
        else if (propertyName == "RXSwitchRelayErrorPkts")
        {
            const auto* value = std::get_if<uint64_t>(&propertyValue);
            if (value == nullptr)
            {
                BMCWEB_LOG_ERROR("Null value returned "
                                 "for receive switch replay error");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["RXSwitchRelayErrors"] =
                *value;
            addNvidiaType = true;
        }
        else if (propertyName == "LinkDownCount")
        {
            const auto* value = std::get_if<uint64_t>(&propertyValue);
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
        else if (propertyName == "SymbolErrors")
        {
            const auto* value = std::get_if<uint64_t>(&propertyValue);
            if (value == nullptr)
            {
                BMCWEB_LOG_ERROR("Null value returned "
                                 " for symbol errors");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["SymbolErrors"] = *value;
            addNvidiaType = true;
        }
        else if (propertyName == "EffectiveError")
        {
            const auto* value = std::get_if<uint64_t>(&propertyValue);
            if (value == nullptr)
            {
                BMCWEB_LOG_ERROR("Null value returned "
                                 " for effective error");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["EffectiveError"] =
                *value;
            addNvidiaType = true;
        }
        else if (propertyName == "EffectiveBER")
        {
            const auto* value = std::get_if<double>(&propertyValue);
            if (value == nullptr)
            {
                BMCWEB_LOG_ERROR("Null value returned "
                                 " for effective BER");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["EffectiveBER"] = *value;
            addNvidiaType = true;
        }
        else if (propertyName == "TotalRawBER")
        {
            const auto* value = std::get_if<double>(&propertyValue);
            if (value == nullptr)
            {
                BMCWEB_LOG_ERROR("Null value returned "
                                 " for total raw BER");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["TotalRawBER"] = *value;
            addNvidiaType = true;
        }
        else if (propertyName == "TotalRawError")
        {
            const auto* value = std::get_if<uint64_t>(&propertyValue);
            if (value == nullptr)
            {
                BMCWEB_LOG_ERROR("Null value returned "
                                 " for total raw error");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["TotalRawError"] = *value;
            addNvidiaType = true;
        }
        else if (propertyName == "IntentionalLinkDownCount")
        {
            const auto* value = std::get_if<uint64_t>(&propertyValue);
            if (value == nullptr)
            {
                BMCWEB_LOG_ERROR("Null value returned "
                                 " for intentional link down count");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["IntentionalLinkDownCount"] =
                *value;
            addNvidiaType = true;
        }
        else if (propertyName == "UnintentionalLinkDownCount")
        {
            const auto* value = std::get_if<uint64_t>(&propertyValue);
            if (value == nullptr)
            {
                BMCWEB_LOG_ERROR("Null value returned "
                                 " for unintentional link down count");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["UnintentionalLinkDownCount"] =
                *value;
            addNvidiaType = true;
        }
    }
}

/**
 * @brief Get all port metric info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getPortMetricsData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Port Metric Data");

    // Get interface properties
    dbus::utility::getAllProperties(
        service, objPath, "",
        [asyncResp{asyncResp}](
            const boost::system::error_code ec,
            const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(asyncResp->res);
                return;
            }

            auto addNvidiaType = BMCWEB_NVIDIA_OEM_PROPERTIES;
            auto linkTypePtr =
                std::ranges::find_if(properties, [](const auto& property) {
                    return property.first == "LinkType";
                });
            std::string linkType;
            if (linkTypePtr != properties.end())
            {
                linkType = std::get<std::string>(linkTypePtr->second);
                linkType = port_utils::getLinkType(linkType);
            }

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
                if ((property.first == "TXBytes") ||
                    (property.first == "RXBytes"))
                {
                    const auto* value = std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for TX/RX bytes");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue[property.first] = *value;
                }
                else if (property.first == "RXMulticastPkts")
                {
                    const auto* value = std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for receive multicast packets");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res
                        .jsonValue["Networking"]["RXMulticastFrames"] = *value;
                }
                else if (property.first == "TXMulticastPkts")
                {
                    const auto* value = std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for transmit multicast packets");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res
                        .jsonValue["Networking"]["TXMulticastFrames"] = *value;
                }
                else if (property.first == "RXUnicastPkts")
                {
                    const auto* value = std::get_if<uint64_t>(&property.second);
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
                    const auto* value = std::get_if<uint64_t>(&property.second);
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

                if (linkType == "Ethernet")
                {
                    populateEthPortMetrics(asyncResp, property.first,
                                           property.second);
                }
                else
                {
                    populateIBPortMetrics(asyncResp, property.first,
                                          property.second, addNvidiaType);
                }

                if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                {
                    if (property.first == "SymbolErrorRXBytes")
                    {
                        const auto* value =
                            std::get_if<uint64_t>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR(
                                "Null value returned for ECC Counter SymbolErrors");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res.jsonValue["Oem"]["Nvidia"]["ECC"]
                                                ["SymbolErrorRXBytes"] = *value;
                        addNvidiaType = true;
                    }
                    else if (property.first == "CorrectedBits")
                    {
                        const auto* value =
                            std::get_if<uint64_t>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR(
                                "Null value returned for ECC Counter CorrectedBits");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res.jsonValue["Oem"]["Nvidia"]["ECC"]
                                                ["CorrectedBits"] = *value;
                        addNvidiaType = true;
                    }
                    else if (property.first == "RawErrorsPerLane")
                    {
                        const auto* value = std::get_if<std::vector<uint64_t>>(
                            &property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR(
                                "Null value returned for ECC Counter"
                                " RawErrorsPerLane");
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        asyncResp->res.jsonValue["Oem"]["Nvidia"]["ECC"]
                                                ["RawErrorsPerLane"] = *value;
                        addNvidiaType = true;
                    }
                }

                for (const auto& [pdiPropertyName, fixedPropertyName] :
                     pcieErrorsProperties)
                {
                    if (property.first == pdiPropertyName)
                    {
                        const auto propertyName = fixedPropertyName
                                                      ? *fixedPropertyName
                                                      : pdiPropertyName;
                        const auto* value =
                            std::get_if<double>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Null value returned for {}",
                                             propertyName);
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res.jsonValue["PCIeErrors"][propertyName] =
                            nvidia::nsm_utils::tryConvertToInt64(*value);
                    }
                }
            }
            if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
            {
                if (addNvidiaType)
                {
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                        "#NvidiaPortMetrics.v1_7_0.NvidiaNVLinkPortMetrics";
                }
            }
        });
}

inline void getPortMetricsDataByAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath, const std::string& chassisId,
    const std::string& networkAdapterId, const std::string& portId)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_states",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, chassisId, networkAdapterId,
         portId](const boost::system::error_code& ec,
                 const std::vector<std::string>& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(asyncResp->res);
                return;
            }

            for (const std::string& sensorPath : resp)
            {
                // Check Interface in Object or not
                dbus::utility::getDbusObject(
                    sensorPath,
                    std::array<std::string_view, 1>{
                        "xyz.openbmc_project.Inventory.Item.Port"},
                    [asyncResp, sensorPath, chassisId, networkAdapterId,
                     portId](
                        const boost::system::error_code ec1,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& object) {
                        if (ec1)
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
                        asyncResp->res.jsonValue["Name"] =
                            portId + " Port Metrics";
                        asyncResp->res
                            .jsonValue["@odata.id"] = boost::urls::format(
                            "/redfish/v1/Chassis/{}/NetworkAdapters/{}/Ports/{}/Metrics",
                            chassisId, networkAdapterId, portId);

                        getPortMetricsData(asyncResp, object.front().first,
                                           sensorPath);
                    });
            }
        });
}

inline void doPortMetrics(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
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

inline void handlePortMetricsGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
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

inline std::string getNetworkAdapterResetType(const std::string& type)
{
    if (type == "xyz.openbmc_project.Control.Reset.ResetTypes.ForceOff")
    {
        return "ForceOff";
    }
    if (type == "xyz.openbmc_project.Control.Reset.ResetTypes.ForceOn")
    {
        return "ForceOn";
    }
    if (type == "xyz.openbmc_project.Control.Reset.ResetTypes.ForceRestart")
    {
        return "ForceRestart";
    }
    if (type == "xyz.openbmc_project.Control.Reset.ResetTypes.GracefulRestart")
    {
        return "GracefulRestart";
    }
    if (type == "xyz.openbmc_project.Control.Reset.ResetTypes.GracefulShutdown")
    {
        return "GracefulShutdown";
    }
    // Unknown or others
    return "";
}

inline void networkAdapterPostResetType(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& networkAdapterId, const std::string& objectPath,
    const std::string& resetType,
    const std::vector<std::pair<std::string, std::vector<std::string>>>&
        serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::ranges::find(interfaceList,
                              "xyz.openbmc_project.Control.ResetAsync") !=
            interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }

    if (inventoryService == nullptr)
    {
        BMCWEB_LOG_ERROR(
            "networkAdapterPostResetType error service not implementing reset interface");
        messages::internalError(resp->res);
        return;
    }

    const std::string conName = *inventoryService;
    dbus::utility::getProperty<std::string>(
        conName, objectPath, "xyz.openbmc_project.Control.Reset", "ResetType",
        [resp, resetType, networkAdapterId, conName, objectPath](
            const boost::system::error_code ec, const std::string& property) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBus response, error for ResetType ");
                BMCWEB_LOG_ERROR("{}", ec.message());
                messages::internalError(resp->res);
                return;
            }

            const std::string ntwAdpResetType =
                getNetworkAdapterResetType(property);
            if (ntwAdpResetType != resetType)
            {
                BMCWEB_LOG_DEBUG(
                    "Property Value Incorrect {} while allowed is {}",
                    resetType, ntwAdpResetType);
                messages::actionParameterNotSupported(resp->res, "ResetType",
                                                      resetType);
                return;
            }

            nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<
                int>(
                resp, std::chrono::seconds(60), conName, objectPath,
                "xyz.openbmc_project.Control.ResetAsync", "Reset",
                [resp](const std::string& status,
                       [[maybe_unused]] const int* retValue) {
                    if (status ==
                        nvidia_async_operation_utils::asyncStatusValueSuccess)
                    {
                        BMCWEB_LOG_DEBUG("Network adapter Reset Succeeded");
                        messages::success(resp->res);
                        return;
                    }
                    BMCWEB_LOG_ERROR("Network adapter reset error {}", status);
                    messages::internalError(resp->res);
                });
        });
}

inline void doNetworkAdapterReset(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& networkAdapterId, const std::string& resetType,
    const std::optional<std::string>& validNetworkAdapterPath)
{
    if (!validNetworkAdapterPath)
    {
        BMCWEB_LOG_ERROR("Not a valid networkAdapter ID{}", networkAdapterId);
        messages::resourceNotFound(asyncResp->res, "NetworkAdapter",
                                   networkAdapterId);
        return;
    }

    dbus::utility::getDbusObject(
        *validNetworkAdapterPath, std::array<std::string_view, 0>{},
        [asyncResp, networkAdapterId, resetType, validNetworkAdapterPath](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                obj) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error while getting service");
                messages::internalError(asyncResp->res);
                return;
            }

            networkAdapterPostResetType(asyncResp, networkAdapterId,
                                        *validNetworkAdapterPath, resetType,
                                        obj);
        });
}

inline void handleNetworkAdapterResetNext(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
    const std::string& resetType,
    const std::vector<std::string>& chassisIntfList,
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
        std::bind_front(doNetworkAdapterReset, asyncResp, networkAdapterId,
                        resetType));
}

inline void handleNetworkAdapterReset(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::optional<std::string> resetType;
    if (!redfish::json_util::readJsonAction(req, asyncResp->res, "ResetType",
                                            resetType))
    {
        return;
    }

    if (resetType)
    {
        redfish::chassis_utils::getValidChassisPathAndInterfaces(
            asyncResp, chassisId,
            std::bind_front(handleNetworkAdapterResetNext, asyncResp, chassisId,
                            networkAdapterId, *resetType));
    }
}

inline void doNDFCollection(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
    const std::optional<std::string>& validNetworkAdapterPath)
{
    if (!validNetworkAdapterPath)
    {
        BMCWEB_LOG_ERROR("doNDFCollection: Not a valid NetworkAdapter ID {}",
                         networkAdapterId);
        messages::resourceNotFound(asyncResp->res, "NetworkAdapter",
                                   networkAdapterId);
        return;
    }

    boost::urls::url collectionUrl = boost::urls::format(
        "/redfish/v1/Chassis/{}/NetworkAdapters/{}/NetworkDeviceFunctions",
        chassisId, networkAdapterId);
    std::string collectionOdataId = collectionUrl.buffer();

    asyncResp->res.jsonValue["@odata.type"] =
        "#NetworkDeviceFunctionCollection.NetworkDeviceFunctionCollection";
    asyncResp->res.jsonValue["Name"] =
        chassisId + " NetworkAdapters " + networkAdapterId +
        " NetworkDeviceFunctions";
    asyncResp->res.jsonValue["@odata.id"] = collectionOdataId;

    const std::string& adapterDbusPath = *validNetworkAdapterPath;

    BMCWEB_LOG_DEBUG("Get collection members by association for: {}",
                     collectionOdataId);
    std::string ndfAssociationPath =
        adapterDbusPath + "/network_device_functions";
    dbus::utility::findAssociations(
        ndfAssociationPath,
        [asyncResp, collectionOdataId](const boost::system::error_code& e,
                                       const std::vector<std::string>& resp) {
            if (e)
            {
                asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
                asyncResp->res.jsonValue["Members@odata.count"] = 0;
                return;
            }

            nlohmann::json& members = asyncResp->res.jsonValue["Members"];
            members = nlohmann::json::array();
            for (const std::string& sensorpath : resp)
            {
                sdbusplus::message::object_path ndfPath(sensorpath);
                if (!ndfPath.filename().empty())
                {
                    std::string dbusNDFId = ndfPath.filename();
                    size_t lastUnderscorePos = dbusNDFId.rfind('_');
                    BMCWEB_LOG_ERROR(
                        "Testlog: dbusNDFId: {}, lastUnderscorePos: {}",
                        dbusNDFId, lastUnderscorePos);
                    if (lastUnderscorePos != std::string::npos &&
                        lastUnderscorePos < dbusNDFId.length() - 1)
                    {
                        dbusNDFId = dbusNDFId.substr(lastUnderscorePos + 1);
                        std::string odataId = collectionOdataId;
                        odataId += "/";
                        odataId += dbusNDFId;
                        members.push_back({{"@odata.id", std::move(odataId)}});
                    }
                }
            }
            asyncResp->res.jsonValue["Members@odata.count"] = members.size();
        });
}

inline void doNDFCollectionWithValidChassisId(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
    const std::vector<std::string>& chassisIntfList,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("Valid chassis path not found");
        messages::internalError(asyncResp->res);
        return;
    }

    getValidNetworkAdapterPath(
        asyncResp, networkAdapterId, chassisIntfList, *validChassisPath,
        std::bind_front(doNDFCollection, asyncResp, chassisId,
                        networkAdapterId));
}

inline void handleNetworkDeviceFunctionsCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    BMCWEB_LOG_DEBUG(
        "handleNetworkDeviceFunctionsCollectionGet: chassisId={}, adapterId={}",
        chassisId, networkAdapterId);

    redfish::chassis_utils::getValidChassisPathAndInterfaces(
        asyncResp, chassisId,
        std::bind_front(doNDFCollectionWithValidChassisId, asyncResp, chassisId,
                        networkAdapterId));
}

inline void getPortAddressData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objPath)
{
    dbus::utility::getAllProperties(
        service, objPath, "",
        [asyncResp,
         objPath](const boost::system::error_code& e,
                  const dbus::utility::DBusPropertiesMap& properties) {
            if (e)
            {
                BMCWEB_LOG_ERROR(
                    "getPortAddressData: D-Bus error getting properties : {}",
                    e.message());
                messages::internalError(asyncResp->res);
                return;
            }

            const std::string* permanentMACAddress = nullptr;
            const std::string* nodeGUID = nullptr;

            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), properties, "MACAddress",
                permanentMACAddress, "GUID", nodeGUID);

            if (!success)
            {
                BMCWEB_LOG_ERROR("Failed to unpack properties");
                messages::internalError(asyncResp->res);
                return;
            }

            if (permanentMACAddress != nullptr)
            {
                asyncResp->res.jsonValue["Ethernet"]["PermanentMACAddress"] =
                    *permanentMACAddress;
            }
            if (nodeGUID != nullptr)
            {
                asyncResp->res.jsonValue["InfiniBand"]["PermanentNodeGUID"] =
                    *nodeGUID;
            }
        });
}

inline void getPortAddressDataByAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& sensorPath,
    const std::string& associationName)
{
    dbus::utility::findAssociations(
        sensorPath + "/" + associationName,
        [asyncResp, connectionName](const boost::system::error_code& e,
                                    const std::vector<std::string>& resp) {
            if (e)
            {
                return;
            }
            for (const std::string& portAddressPath : resp)
            {
                getPortAddressData(asyncResp, connectionName, portAddressPath);
            }
        });
}

inline void getNDFData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& sensorPath, const std::string& chassisId,
    const std::string& networkAdapterId, const std::string& ndfId)
{
    static const std::string linkTypeInterface{
        "xyz.openbmc_project.Network.LinkType"};
    dbus::utility::getDbusObject(
        sensorPath, std::array<std::string_view, 1>{linkTypeInterface},
        [asyncResp, sensorPath, chassisId, networkAdapterId, ndfId](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                object) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("no LinkType interface on object path {}",
                                 sensorPath);
                return;
            }

            asyncResp->res.jsonValue["@odata.type"] =
                "#NetworkDeviceFunction.v1_10_0.NetworkDeviceFunction";
            asyncResp->res.jsonValue["Id"] = ndfId;
            asyncResp->res.jsonValue["Name"] =
                chassisId + " NetworkAdapters " + networkAdapterId +
                " NetworkDeviceFunctions " + ndfId;
            asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
                "/redfish/v1/Chassis/{}/NetworkAdapters/{}/NetworkDeviceFunctions/{}",
                chassisId, networkAdapterId, ndfId);

            sdbusplus::message::object_path path(sensorPath);
            std::string portId = path.filename();
            nlohmann::json& assignablePhysicalNetworkPorts =
                asyncResp->res.jsonValue["AssignablePhysicalNetworkPorts"];
            assignablePhysicalNetworkPorts = nlohmann::json::array();
            std::string assignablePhysicalNetworkPortsOdataId =
                boost::urls::format(
                    "/redfish/v1/Chassis/{}/NetworkAdapters/{}/Ports/{}",
                    chassisId, networkAdapterId, portId)
                    .buffer();
            asyncResp->res.jsonValue["AssignablePhysicalNetworkPorts"]
                .push_back(
                    {{"@odata.id", assignablePhysicalNetworkPortsOdataId}});

            std::string connectionName = object.front().first;
            dbus::utility::getAllProperties(
                connectionName, sensorPath, "",
                [asyncResp, connectionName, sensorPath](
                    const boost::system::error_code& e,
                    const dbus::utility::DBusPropertiesMap& properties) {
                    if (e)
                    {
                        BMCWEB_LOG_ERROR(
                            "getNDFData: D-Bus error getting properties : {}",
                            e.message());
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    const std::string* linkType = nullptr;
                    const std::string* portGUID = nullptr;

                    const bool success = sdbusplus::unpackPropertiesNoThrow(
                        dbus_utils::UnpackErrorPrinter(), properties,
                        "LinkType", linkType, "GUID", portGUID);

                    if (!success)
                    {
                        BMCWEB_LOG_ERROR("Failed to unpack properties");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    std::string netDevFuncType;
                    if (linkType != nullptr)
                    {
                        netDevFuncType = port_utils::getLinkType(*linkType);
                        asyncResp->res.jsonValue["NetDevFuncType"] =
                            netDevFuncType;
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("Null value returned for link type");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if (netDevFuncType == "InfiniBand")
                    {
                        if (portGUID != nullptr)
                        {
                            asyncResp->res
                                .jsonValue["InfiniBand"]["PermanentPortGUID"] =
                                *portGUID;
                        }
                        else
                        {
                            BMCWEB_LOG_ERROR(
                                "Null value returned for port GUID");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        getPortAddressDataByAssociation(
                            asyncResp, connectionName, sensorPath,
                            "associated_infiniband_port_address");
                    }
                    else if (netDevFuncType == "Ethernet")
                    {
                        getPortAddressDataByAssociation(
                            asyncResp, connectionName, sensorPath,
                            "associated_ethernet_port_address");
                    }
                });
        });
}

inline void getNDFDataByAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& networkAdapterDbusPath, const std::string& chassisId,
    const std::string& networkAdapterId, const std::string& ndfId)
{
    std::string ndfAssociationPath =
        networkAdapterDbusPath + "/network_device_functions";

    dbus::utility::findAssociations(ndfAssociationPath, [asyncResp,
                                                         networkAdapterDbusPath,
                                                         chassisId,
                                                         networkAdapterId,
                                                         ndfId](
                                                            const boost::
                                                                system::
                                                                    error_code&
                                                                        ec,
                                                            const std::vector<
                                                                std::string>&
                                                                resp) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error");
            messages::internalError(asyncResp->res);
            return;
        }

        for (const std::string& sensorPath : resp)
        {
            sdbusplus::message::object_path ndfPath(sensorPath);
            if (!ndfPath.filename().empty())
            {
                std::string dbusNDFId = ndfPath.filename();
                size_t lastUnderscorePos = dbusNDFId.rfind('_');

                //  Dbus NDF IDs are formatted as "<portPrefix>_<ndfId>"
                if (lastUnderscorePos != std::string::npos &&
                    lastUnderscorePos < dbusNDFId.length() - 1 &&
                    dbusNDFId.substr(lastUnderscorePos + 1) == ndfId)
                {
                    getNDFData(asyncResp, sensorPath, chassisId,
                               networkAdapterId, ndfId);
                    return;
                }
            }
        }

        BMCWEB_LOG_WARNING(
            "getNDFDataByAssociation: NDF {} not found in associations for adapter {}",
            ndfId, networkAdapterId);
        messages::resourceNotFound(asyncResp->res, "NetworkDeviceFunction",
                                   ndfId);
    });
}

inline void doNDF(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                  const std::string& chassisId,
                  const std::string& networkAdapterId, const std::string& ndfId,
                  const std::optional<std::string>& validNetworkAdapterPath)
{
    if (!validNetworkAdapterPath)
    {
        BMCWEB_LOG_ERROR("doNDF: Not a valid networkAdapter ID {}",
                         networkAdapterId);
        messages::resourceNotFound(asyncResp->res, "NetworkAdapter",
                                   networkAdapterId);
        return;
    }

    getNDFDataByAssociation(asyncResp, *validNetworkAdapterPath, chassisId,
                            networkAdapterId, ndfId);
}

inline void doNDFWithValidChassisId(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
    const std::string& ndfId, const std::vector<std::string>& chassisIntfList,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("doNDFWithValidChassisId: Not a valid chassis ID {}",
                         chassisId);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }
    getValidNetworkAdapterPath(
        asyncResp, networkAdapterId, chassisIntfList, *validChassisPath,
        std::bind_front(doNDF, asyncResp, chassisId, networkAdapterId, ndfId));
}

inline void handleNetworkDeviceFunctionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
    const std::string& ndfId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPathAndInterfaces(
        asyncResp, chassisId,
        std::bind_front(doNDFWithValidChassisId, asyncResp, chassisId,
                        networkAdapterId, ndfId));
}

/**
 * @brief GET .../NetworkAdapters/{id}/Settings — pending device mode values
 * (Oem.Nvidia).
 */
inline void doNetworkAdapterSettingsGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
    const std::optional<std::string>& validNetworkAdapterPath)
{
    if (!validNetworkAdapterPath)
    {
        messages::resourceNotFound(asyncResp->res, "NetworkAdapter",
                                   networkAdapterId);
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] =
        "#NetworkAdapter.v1_11_0.NetworkAdapter";
    asyncResp->res.jsonValue["Id"] = "Settings";
    asyncResp->res.jsonValue["Name"] =
        chassisId + " NetworkAdapters " + networkAdapterId + " Settings";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/NetworkAdapters/{}/Settings", chassisId,
        networkAdapterId);

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        redfish::nvidia_network_adapters_utils::populateDeviceModeSettings(
            asyncResp, chassisId, networkAdapterId, *validNetworkAdapterPath,
            false);
    }
}

inline void handleNetworkAdapterSettingsGetNext(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
    const std::vector<std::string>& chassisIntfList,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }
    getValidNetworkAdapterPath(
        asyncResp, networkAdapterId, chassisIntfList, *validChassisPath,
        std::bind_front(doNetworkAdapterSettingsGet, asyncResp, chassisId,
                        networkAdapterId));
}

inline void handleNetworkAdapterSettingsGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    redfish::chassis_utils::getValidChassisPathAndInterfaces(
        asyncResp, chassisId,
        std::bind_front(handleNetworkAdapterSettingsGetNext, asyncResp,
                        chassisId, networkAdapterId));
}

/**
 * @brief PATCH .../NetworkAdapters/{id}/Settings — device mode PendingMode
 * fields.
 */
inline void doNetworkAdapterSettingsPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& networkAdapterId,
    const std::optional<std::string>& dpuOperationMode,
    const std::optional<int64_t>& numberOfUpstreamSockets,
    const std::optional<bool>& eastWestControlEnabled,
    const std::optional<int64_t>& pcieBifurcationLinkCount,
    const std::optional<std::string>& validNetworkAdapterPath)
{
    if (!validNetworkAdapterPath)
    {
        messages::resourceNotFound(asyncResp->res, "NetworkAdapter",
                                   networkAdapterId);
        return;
    }

    if (dpuOperationMode)
    {
        if (*dpuOperationMode != "DPU" && *dpuOperationMode != "NIC")
        {
            messages::propertyValueIncorrect(asyncResp->res, "DPUOperationMode",
                                             *dpuOperationMode);
            return;
        }
        nvidia_network_adapters_utils::patchDpuOperationMode(
            asyncResp, networkAdapterId, *validNetworkAdapterPath,
            *dpuOperationMode);
    }

    std::vector<std::tuple<std::string, uint32_t>> pciePatches;
    if (numberOfUpstreamSockets)
    {
        auto raw = nvidia_network_adapters_utils::socketModeRedfishToRaw(
            *numberOfUpstreamSockets);
        if (!raw)
        {
            messages::propertyValueIncorrect(
                asyncResp->res, "NumberOfUpstreamSockets",
                std::to_string(*numberOfUpstreamSockets));
            return;
        }
        pciePatches.emplace_back("PCIeMultiSockets", *raw);
    }

    if (eastWestControlEnabled)
    {
        pciePatches.emplace_back(
            "PCIeControlledEWTraffic",
            nvidia_network_adapters_utils::ewTrafficModeRedfishToRaw(
                *eastWestControlEnabled));
    }

    if (pcieBifurcationLinkCount)
    {
        auto raw = nvidia_network_adapters_utils::bifurcationModeRedfishToRaw(
            *pcieBifurcationLinkCount);
        if (!raw)
        {
            messages::propertyValueIncorrect(
                asyncResp->res, "PCIeBifurcationLinkCount",
                std::to_string(*pcieBifurcationLinkCount));
            return;
        }
        pciePatches.emplace_back("PCIeBifurcation", *raw);
    }

    if (!pciePatches.empty())
    {
        nvidia_network_adapters_utils::patchPCIeDeviceMode(
            asyncResp, *validNetworkAdapterPath, pciePatches);
    }
}

inline void handleNetworkAdapterSettingsPatchNext(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
    const std::optional<std::string>& dpuOperationMode,
    const std::optional<int64_t>& numberOfUpstreamSockets,
    const std::optional<bool>& eastWestControlEnabled,
    const std::optional<int64_t>& pcieBifurcationLinkCount,
    const std::vector<std::string>& chassisIntfList,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }
    getValidNetworkAdapterPath(
        asyncResp, networkAdapterId, chassisIntfList, *validChassisPath,
        std::bind_front(doNetworkAdapterSettingsPatch, asyncResp,
                        networkAdapterId, dpuOperationMode,
                        numberOfUpstreamSockets, eastWestControlEnabled,
                        pcieBifurcationLinkCount));
}

inline void handleNetworkAdapterSettingsPatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::optional<std::string> dpuOperationMode;
    std::optional<int64_t> numberOfUpstreamSockets;
    std::optional<bool> eastWestControlEnabled;
    std::optional<int64_t> pcieBifurcationLinkCount;

    if (!redfish::json_util::readJsonPatch(
            req, asyncResp->res, "Oem/Nvidia/DPUOperationMode",
            dpuOperationMode, "Oem/Nvidia/NumberOfUpstreamSockets",
            numberOfUpstreamSockets, "Oem/Nvidia/EastWestControlEnabled",
            eastWestControlEnabled, "Oem/Nvidia/PCIeBifurcationLinkCount",
            pcieBifurcationLinkCount))
    {
        return;
    }

    if (!dpuOperationMode && !numberOfUpstreamSockets &&
        !eastWestControlEnabled && !pcieBifurcationLinkCount)
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPathAndInterfaces(
        asyncResp, chassisId,
        std::bind_front(handleNetworkAdapterSettingsPatchNext, asyncResp,
                        chassisId, networkAdapterId, dpuOperationMode,
                        numberOfUpstreamSockets, eastWestControlEnabled,
                        pcieBifurcationLinkCount));
}

inline void requestRoutesChassisNetworkAdapter(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/NetworkAdapters/")
        .privileges(redfish::privileges::getNetworkAdapterCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleNetworkAdaptersCollectionGet, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/")
        .privileges(redfish::privileges::getNetworkAdapter)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleNetworkAdapterGet, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/")
        .privileges(redfish::privileges::patchNetworkAdapter)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleNetworkAdapterPatch, std::ref(app)));
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/Settings/")
        .privileges(redfish::privileges::getNetworkAdapter)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleNetworkAdapterSettingsGet, std::ref(app)));
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/Settings/")
        .privileges(redfish::privileges::patchNetworkAdapter)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleNetworkAdapterSettingsPatch, std::ref(app)));
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/Actions/NetworkAdapter.Reset/")
        .privileges(redfish::privileges::getNetworkAdapter)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleNetworkAdapterReset, std::ref(app)));
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/NetworkDeviceFunctions/")
        .privileges(redfish::privileges::getNetworkDeviceFunctionCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleNetworkDeviceFunctionsCollectionGet, std::ref(app)));
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/NetworkDeviceFunctions/<str>/")
        .privileges(redfish::privileges::getNetworkDeviceFunction)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleNetworkDeviceFunctionGet, std::ref(app)));
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
