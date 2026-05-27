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

#include "dbus_singleton.hpp"
#include "nvidia_dbus_utility.hpp"
#include "nvidia_fabric_config_update.hpp"
#include "redfish_util.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/nvidia_async_set_callbacks.hpp"
#include "utils/pcie_util.hpp"

#include <asm-generic/errno.h>

#include <app.hpp>
#include <utils/collection.hpp>
#include <utils/conditions_utils.hpp>
#include <utils/nvidia_async_set_utils.hpp>
#include <utils/nvidia_fabric_utils.hpp>
#include <utils/nvidia_histogram_utils.hpp>
#include <utils/nvidia_utils.hpp>
#include <utils/port_utils.hpp>
#include <utils/processor_utils.hpp>
#include <utils/redfish_response_utils.hpp>

#include <cstdint>
#include <variant>

namespace redfish
{

constexpr const char* inventoryRootPath =
    "/xyz/openbmc_project/inventory/system";
constexpr const char* inventoryFabricStr = "/fabrics/";
constexpr const char* inventorySwitchStr = "/Switches/";
constexpr const char* inventoryPortStr = "/Ports/";

inline std::string getSwitchType(const std::string& switchType)
{
    if (switchType ==
        "xyz.openbmc_project.Inventory.Item.Switch.SwitchType.Ethernet")
    {
        return "Ethernet";
    }
    if (switchType == "xyz.openbmc_project.Inventory.Item.Switch.SwitchType.FC")
    {
        return "FC";
    }
    if (switchType ==
        "xyz.openbmc_project.Inventory.Item.Switch.SwitchType.NVLink")
    {
        return "NVLink";
    }
    if (switchType ==
        "xyz.openbmc_project.Inventory.Item.Switch.SwitchType.OEM")
    {
        return "OEM";
    }
    if (switchType ==
        "xyz.openbmc_project.Inventory.Item.Switch.SwitchType.PCIe")
    {
        return "PCIe";
    }

    // Unknown or others
    return "";
}

inline std::string getFabricType(const std::string& fabricType)
{
    if (fabricType ==
        "xyz.openbmc_project.Inventory.Item.Fabric.FabricType.Ethernet")
    {
        return "Ethernet";
    }
    if (fabricType == "xyz.openbmc_project.Inventory.Item.Fabric.FabricType.FC")
    {
        return "FC";
    }
    if (fabricType ==
        "xyz.openbmc_project.Inventory.Item.Fabric.FabricType.NVLink")
    {
        return "NVLink";
    }
    if (fabricType ==
        "xyz.openbmc_project.Inventory.Item.Fabric.FabricType.OEM")
    {
        return "OEM";
    }
    if (fabricType ==
        "xyz.openbmc_project.Inventory.Item.Fabric.FabricType.PCIe")
    {
        return "PCIe";
    }

    // Unknown or others
    return "";
}

inline std::string getZoneType(const std::string& zoneType)
{
    if (zoneType == "xyz.openbmc_project.Inventory.Item.Zone.ZoneType.Default")
    {
        return "Default";
    }
    if (zoneType ==
        "xyz.openbmc_project.Inventory.Item.Zone.ZoneType.ZoneOfEndpoints")
    {
        return "ZoneOfEndpoints";
    }
    if (zoneType ==
        "xyz.openbmc_project.Inventory.Item.Zone.ZoneType.ZoneOfZones")
    {
        return "ZoneOfZones";
    }
    if (zoneType ==
        "xyz.openbmc_project.Inventory.Item.Zone.ZoneType.ZoneOfResourceBlocks")
    {
        return "ZoneOfResourceBlocks";
    }
    // Unknown or others
    return "";
}

/**
 * @brief Get all switch info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   aResp   Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 * @param[in]       processorId    processor id for redfish URI.
 */
inline void getConnectedPortLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& processorId)
{
    BMCWEB_LOG_DEBUG("Get Connected Port Links");
    BMCWEB_LOG_DEBUG("{}", objPath);
    dbus::utility::async_method_call(
        [aResp, processorId](const boost::system::error_code& ec,
                             std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no endpoint = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            nlohmann::json& linksArray =
                aResp->res.jsonValue["Links"]["ConnectedPorts"];
            BMCWEB_LOG_DEBUG("populating ConnectedPorts");
            linksArray = nlohmann::json::array();
            for (const std::string& portPath : *data)
            {
                sdbusplus::message::object_path portObjPath(portPath);
                const std::string& endpointId = portObjPath.filename();
                BMCWEB_LOG_DEBUG("{}", endpointId);
                std::string endpointURI =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/Processors/";
                endpointURI += processorId;
                endpointURI += "/Ports/";
                endpointURI += endpointId;
                linksArray.push_back({{"@odata.id", endpointURI}});
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/processor_port",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}
/**
 * @brief Get all switch info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   aResp   Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 * @param[in]       fabricId    fabric id for redfish URI.
 */
inline void updateProcessorPortLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& fabricId)
{
    BMCWEB_LOG_DEBUG("Get Processor Port Links");
    dbus::utility::async_method_call(
        [aResp, objPath,
         fabricId](const boost::system::error_code& ec,
                   std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no endpoint = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            nlohmann::json& linksArray =
                aResp->res.jsonValue["Links"]["AssociatedEndpoints"];
            linksArray = nlohmann::json::array();
            for (const std::string& portPath : *data)
            {
                sdbusplus::message::object_path portObjPath(portPath);
                const std::string& endpointId = portObjPath.filename();
                std::string endpointURI = "/redfish/v1/Fabrics/";
                endpointURI += fabricId;
                endpointURI += "/Endpoints/";
                endpointURI += endpointId;
                linksArray.push_back({{"@odata.id", endpointURI}});
                getConnectedPortLinks(aResp, objPath, endpointId);
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/associated_endpoint",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getNetworkAdapterPorts(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& portPath, const std::string& networkAdapterChassisId,
    const std::string& networkAdapterName)
{
    BMCWEB_LOG_DEBUG("Get connected network adapter ports on {}",
                     networkAdapterName);
    dbus::utility::async_method_call(
        [asyncResp, portPath, networkAdapterChassisId,
         networkAdapterName](const boost::system::error_code& ec,
                             std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Get connected network adapter failed on{}",
                                 networkAdapterName);
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_DEBUG(
                    "No data in response when getting PCIe bridge ports {}",
                    portPath);
                return;
            }
            nlohmann::json& networkAdapterLinksArray =
                asyncResp->res.jsonValue["Links"]["ConnectedPorts"];
            for (const std::string& networkAdapterPortPath : *data)
            {
                sdbusplus::message::object_path objectPath(
                    networkAdapterPortPath);
                std::string networkAdapterPortId = objectPath.filename();
                if (networkAdapterPortId.empty())
                {
                    BMCWEB_LOG_ERROR("Unable to fetch port");
                    messages::internalError(asyncResp->res);
                    return;
                }
                nlohmann::json thisPort = nlohmann::json::object();
                std::string portUri =
                    "/redfish/v1/Chassis/" + networkAdapterChassisId;
                portUri += "/NetworkAdapters/" + networkAdapterName + "/Ports/";
                portUri += networkAdapterPortId;
                thisPort["@odata.id"] = portUri;
                networkAdapterLinksArray.push_back(std::move(thisPort));
            }
        },
        "xyz.openbmc_project.ObjectMapper", portPath + "/pcie_bridge_port",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getConnectedNetworkAdapter(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& networkAdapterPath, const std::string& portPath,
    const std::string& networkAdapterName)
{
    BMCWEB_LOG_DEBUG("Get connected network adapter on{}", networkAdapterName);
    dbus::utility::async_method_call(
        [asyncResp, networkAdapterPath, portPath,
         networkAdapterName](const boost::system::error_code& ec,
                             std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Get parent chassis failed on {}",
                                 networkAdapterPath);
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_DEBUG("Get connected network adapter failed on: {}",
                                 networkAdapterName);
                return;
            }
            for (const std::string& networkAdapterChassisPath : *data)
            {
                sdbusplus::message::object_path objectPath(
                    networkAdapterChassisPath);
                std::string networkAdapterChassisId = objectPath.filename();
                if (networkAdapterChassisId.empty())
                {
                    BMCWEB_LOG_ERROR("Empty network adapter chassisId");
                    messages::internalError(asyncResp->res);
                    return;
                }
                getNetworkAdapterPorts(asyncResp, portPath,
                                       networkAdapterChassisId,
                                       networkAdapterName);
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        networkAdapterPath + "/parent_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Get network adapter link info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   aResp   Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void updateNetworkAdapterPortLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get NetworkAdapter Port Links");
    dbus::utility::async_method_call(
        [aResp, objPath](const boost::system::error_code& ec,
                         std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Dbus resource error on {}", objPath);
                return; // no endpoint = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_DEBUG("No data received on {}", objPath);
                return;
            }
            for (const std::string& networkAdapterPath : *data)
            {
                sdbusplus::message::object_path networkAdapterObjPath(
                    networkAdapterPath);
                const std::string& networkAdapterName =
                    networkAdapterObjPath.filename();
                if (networkAdapterName.empty())
                {
                    BMCWEB_LOG_ERROR("Empty network adapter name");
                    messages::internalError(aResp->res);
                    return;
                }
                getConnectedNetworkAdapter(aResp, networkAdapterPath, objPath,
                                           networkAdapterName);
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/associated_pcie_bridge",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getConnectedSwitchPort(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& portPath, const std::string& fabricId,
    const std::string& switchName)
{
    BMCWEB_LOG_DEBUG("Get connected switch ports on {}", switchName);
    dbus::utility::async_method_call(
        [asyncResp, portPath, fabricId,
         switchName](const boost::system::error_code& ec,
                     std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Get connected switch failed on{}",
                                 switchName);
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_DEBUG(
                    "No response data on{} switch_port association", portPath);
                return;
            }
            nlohmann::json& switchlinksArray =
                asyncResp->res.jsonValue["Links"]["ConnectedPorts"];
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

/**
 * @brief Get switch link info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   aResp   Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 * @param[in]       fabricId    fabric id for redfish URI.
 */
inline void updateSwitchPortLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& fabricId)
{
    BMCWEB_LOG_DEBUG("Get Switch Port Links");
    dbus::utility::async_method_call(
        [aResp, objPath,
         fabricId](const boost::system::error_code& ec,
                   std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Dbus response error");
                return; // no endpoint = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_DEBUG(
                    "No response data on {} associated_switch association",
                    objPath);
                return;
            }
            for (const std::string& switchPath : *data)
            {
                sdbusplus::message::object_path switchObjPath(switchPath);
                const std::string& switchName = switchObjPath.filename();
                if (switchName.empty())
                {
                    BMCWEB_LOG_ERROR("Empty switch name");
                    messages::internalError(aResp->res);
                    return;
                }
                nlohmann::json& switchLinksArray =
                    aResp->res.jsonValue["Links"]["ConnectedSwitches"];
                nlohmann::json thisSwitch = nlohmann::json::object();
                std::string switchUri = "/redfish/v1/Fabrics/";
                switchUri += fabricId;
                switchUri += "/Switches/";
                switchUri += switchName;
                thisSwitch["@odata.id"] = switchUri;
                switchLinksArray.push_back(std::move(thisSwitch));
                getConnectedSwitchPort(aResp, objPath, fabricId, switchName);
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/associated_switch",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Get PCIe Equalization info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   aResp   Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 * @param[in]       fabricId    fabric id for redfish URI.
 * @param[in]       switchId    switch id for redfish URI.
 * @param[in]       portId      port id for redfish URI.
 */
inline void updatePCIeEqualization(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& fabricId, const std::string& switchId,
    const std::string& portId)
{
    BMCWEB_LOG_DEBUG("Get PCIe Equalization");
    dbus::utility::async_method_call(
        [aResp, objPath, fabricId, switchId, portId](
            const boost::system::error_code& ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                object) {
            if (ec || object.empty())
            {
                BMCWEB_LOG_DEBUG("No PCIe Equalization found {}", objPath);
                return;
            }

            std::string portEqualizationURI = "/redfish/v1/Fabrics/";
            portEqualizationURI += fabricId;
            portEqualizationURI += "/Switches/";
            portEqualizationURI += switchId;
            portEqualizationURI += "/Ports/";
            portEqualizationURI += portId;
            portEqualizationURI += "/Oem/Nvidia/PCIeEqualization";
            aResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaPort.v1_5_0.NvidiaPCIePort";
            aResp->res
                .jsonValue["Oem"]["Nvidia"]["PCIeEqualization"]["@odata.id"] =
                portEqualizationURI;
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
        std::array<std::string, 1>(
            {"xyz.openbmc_project.PCIe.PCIePortConfigurationInfo"}));
}

/**
 * @brief Get all switch info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   aResp   Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void updateSwitchDataByAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Switch Data by associate object");

    using PropertyType = std::variant<std::string, bool, double, size_t,
                                      std::vector<std::string>>;
    using PropertiesMap = boost::container::flat_map<std::string, PropertyType>;

    dbus::utility::async_method_call(
        [aResp, objPath](const boost::system::error_code& ec,
                         std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Dbus response error: associated switch");
                return; // no endpoint = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_DEBUG(
                    "No response data on {} associated_switch association",
                    objPath);
                return;
            }
            for (const std::string& switchPath : *data)
            {
                // make onject call to get service
                //  then get all the data
                dbus::utility::async_method_call(
                    [aResp, switchPath](
                        const boost::system::error_code& ec1,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& object) {
                        if (ec1)
                        {
                            BMCWEB_LOG_ERROR(
                                "Error no Switch interface on {} path",
                                switchPath);
                            messages::internalError(aResp->res);
                            return;
                        }
                        auto service = object.front().first;

                        // Get interface properties
                        dbus::utility::async_method_call(
                            [aResp,
                             switchPath](const boost::system::error_code& ec2,
                                         const PropertiesMap& properties) {
                                if (ec2)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "Error while fetching peoperties on {} path",
                                        switchPath);
                                    messages::internalError(aResp->res);
                                    return;
                                }

                                for (const auto& property : properties)
                                {
                                    const std::string& propertyName =
                                        property.first;
                                    if (propertyName == "Type")
                                    {
                                        const std::string* value =
                                            std::get_if<std::string>(
                                                &property.second);
                                        if (value == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Null value returned "
                                                "for switch type");
                                            messages::internalError(aResp->res);
                                            return;
                                        }
                                        aResp->res.jsonValue["SwitchType"] =
                                            getSwitchType(*value);
                                    }
                                    else if (propertyName ==
                                             "SupportedProtocols")
                                    {
                                        nlohmann::json& protoArray =
                                            aResp->res.jsonValue
                                                ["SupportedProtocols"];
                                        protoArray = nlohmann::json::array();
                                        const std::vector<std::string>*
                                            protocols = std::get_if<
                                                std::vector<std::string>>(
                                                &property.second);
                                        if (protocols == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Null value returned "
                                                "for supported protocols");
                                            messages::internalError(aResp->res);
                                            return;
                                        }
                                        for (const std::string& protocol :
                                             *protocols)
                                        {
                                            protoArray.push_back(
                                                getSwitchType(protocol));
                                        }
                                    }
                                    else if (propertyName == "Enabled")
                                    {
                                        const bool* value =
                                            std::get_if<bool>(&property.second);
                                        if (value == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Null value returned "
                                                "for enabled");
                                            messages::internalError(aResp->res);
                                            return;
                                        }
                                        aResp->res.jsonValue["Enabled"] =
                                            *value;
                                    }
                                    else if (propertyName == "CurrentBandwidth")
                                    {
                                        const double* value =
                                            std::get_if<double>(
                                                &property.second);
                                        if (value == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Null value returned "
                                                "for CurrentBandwidth");
                                            messages::internalError(aResp->res);
                                            return;
                                        }
                                        aResp->res
                                            .jsonValue["CurrentBandwidthGbps"] =
                                            *value;
                                    }
                                    else if (propertyName == "MaxBandwidth")
                                    {
                                        const double* value =
                                            std::get_if<double>(
                                                &property.second);
                                        if (value == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Null value returned "
                                                "for MaxBandwidth");
                                            messages::internalError(aResp->res);
                                            return;
                                        }
                                        aResp->res
                                            .jsonValue["MaxBandwidthGbps"] =
                                            *value;
                                    }
                                }
                            },
                            service, switchPath,
                            "org.freedesktop.DBus.Properties", "GetAll", "");
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetObject", switchPath,
                    std::array<std::string, 1>(
                        {"xyz.openbmc_project.Inventory.Item.Switch"}));
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/associated_switch_obj",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Get all switch info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void updateSwitchData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Switch Data");
    using PropertyType = std::variant<std::string, bool, double, size_t,
                                      std::vector<std::string>>;
    using PropertiesMap = boost::container::flat_map<std::string, PropertyType>;
    // Get interface properties
    dbus::utility::async_method_call(
        [asyncResp, objPath](const boost::system::error_code& ec,
                             const PropertiesMap& properties) {
            if (ec)
            {
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
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for switch type");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["SwitchType"] =
                        getSwitchType(*value);
                }

                if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                {
                    if (propertyName == "DeviceId")
                    {
                        const std::string* value =
                            std::get_if<std::string>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_DEBUG("Null value returned "
                                             "for DeviceId");
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        if (!value->empty())
                        {
                            asyncResp->res
                                .jsonValue["Oem"]["Nvidia"]["DeviceId"] =
                                *value;
                        }
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                            "#NvidiaSwitch.v1_4_0.NvidiaSwitch";
                    }
                    else if (propertyName == "VendorId")
                    {
                        const std::string* value =
                            std::get_if<std::string>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_DEBUG("Null value returned "
                                             "for VendorId");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        if (!value->empty())
                        {
                            asyncResp->res
                                .jsonValue["Oem"]["Nvidia"]["VendorId"] =
                                *value;
                        }
                    }
                    else if (propertyName == "PCIeReferenceClockEnabled")
                    {
                        const bool* value = std::get_if<bool>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_DEBUG("Null value returned "
                                             "for PCIeReferenceClockEnabled");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                                ["PCIeReferenceClockEnabled"] =
                            *value;
                    }
                }
                if (propertyName == "SupportedProtocols")
                {
                    nlohmann::json& protoArray =
                        asyncResp->res.jsonValue["SupportedProtocols"];
                    protoArray = nlohmann::json::array();
                    const std::vector<std::string>* protocols =
                        std::get_if<std::vector<std::string>>(&property.second);
                    if (protocols == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for supported protocols");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    for (const std::string& protocol : *protocols)
                    {
                        protoArray.push_back(getSwitchType(protocol));
                    }
                }
                else if (propertyName == "Enabled")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for enabled");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Enabled"] = *value;
                }
                else if ((propertyName == "Model") ||
                         (propertyName == "PartNumber") ||
                         (propertyName == "SerialNumber") ||
                         (propertyName == "Manufacturer"))
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for asset properties");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    redfish::mapStringOrNull(asyncResp->res.jsonValue,
                                             propertyName, value);
                }
                else if (propertyName == "CurrentBandwidth")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for CurrentBandwidth");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["CurrentBandwidthGbps"] = *value;
                }
                else if (propertyName == "MaxBandwidth")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for MaxBandwidth");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["MaxBandwidthGbps"] = *value;
                }
                else if (propertyName == "TotalSwitchWidth")
                {
                    const size_t* value = std::get_if<size_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for TotalSwitchWidth");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["TotalSwitchWidth"] = *value;
                }
                else if (propertyName == "CurrentPowerState")
                {
                    const std::string* state =
                        std::get_if<std::string>(&property.second);
                    if (*state ==
                        "xyz.openbmc_project.State.Chassis.PowerState.On")
                    {
                        asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
                    }
                    else if (*state ==
                             "xyz.openbmc_project.State.Chassis.PowerState.Off")
                    {
                        asyncResp->res.jsonValue["Status"]["State"] =
                            "StandbyOffline";
                    }
                }
                else if (propertyName == "UUID")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for UUID");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["UUID"] = *value;
                }
            }

            getComponentFirmwareVersion(asyncResp, objPath);
            updateSwitchDataByAssociation(asyncResp, objPath);
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll", "");

    asyncResp->res.jsonValue["Status"]["Health"] = "OK";
    asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
    if constexpr (!BMCWEB_DISABLE_HEALTH_ROLLUP)
    {
        asyncResp->res.jsonValue["Status"]["HealthRollup"] = "OK";
    }

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
                }
            });
        health->start();
    }
}

inline void updateZoneData(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& service,
                           const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Zone Data");
    using PropertyType =
        std::variant<std::string, bool, size_t, std::vector<std::string>>;
    using PropertiesMap = boost::container::flat_map<std::string, PropertyType>;
    // Get interface properties
    dbus::utility::async_method_call(
        [asyncResp](const boost::system::error_code& ec,
                    const PropertiesMap& properties) {
            if (ec)
            {
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
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for zone type");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["ZoneType"] = getZoneType(*value);
                }

                else if (propertyName == "RoutingEnabled")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for enabled");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["DefaultRoutingEnabled"] = *value;
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll", "");
}

/**
 * FabricCollection derived class for delivering Fabric Collection Schema
 */
inline void requestRoutesFabricCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                asyncResp->res.jsonValue["@odata.type"] =
                    "#FabricCollection.FabricCollection";
                asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/Fabrics";
                asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
                asyncResp->res.jsonValue["Name"] = "Fabric Collection";
                constexpr std::array<std::string_view, 1> interface{
                    "xyz.openbmc_project.Inventory.Item.Fabric"};

                collection_util::getCollectionMembers(
                    asyncResp, boost::urls::format("/redfish/v1/Fabrics"),
                    interface, "/xyz/openbmc_project/inventory");
            });
}

/**
 * Fabric override class for delivering Fabric Schema
 */
inline void requestRoutesFabric(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/")
        .privileges({{"Login"}})
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& fabricId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            dbus::utility::async_method_call(
                [asyncResp, fabricId(std::string(fabricId))](
                    const boost::system::error_code& ec,
                    const dbus::utility::GetSubTreeType& subtree) {
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
                        if (objPath.filename() != fabricId)
                        {
                            continue;
                        }
                        if (connectionNames.empty())
                        {
                            BMCWEB_LOG_ERROR("Got 0 Connection names");
                            continue;
                        }

                        asyncResp->res.jsonValue["@odata.type"] =
                            "#Fabric.v1_2_0.Fabric";
                        asyncResp->res.jsonValue["@odata.id"] =
                            "/redfish/v1/Fabrics/" + fabricId;
                        asyncResp->res.jsonValue["Id"] = fabricId;
                        asyncResp->res.jsonValue["Name"] =
                            fabricId + " Resource";

                        if constexpr (BMCWEB_REDFISH_ROUTES_ENDPOINT)
                        {
                            asyncResp->res.jsonValue["Endpoints"] = {
                                {"@odata.id", "/redfish/v1/Fabrics/" +
                                                  fabricId + "/Endpoints"}};
                        }

                        asyncResp->res.jsonValue["Switches"] = {
                            {"@odata.id",
                             "/redfish/v1/Fabrics/" + fabricId + "/Switches"}};

                        addSwitchConfigPushURI(asyncResp, fabricId);

                        if constexpr (BMCWEB_REDFISH_ROUTES_ZONE)
                        {
                            asyncResp->res.jsonValue["Zones"] = {
                                {"@odata.id",
                                 "/redfish/v1/Fabrics/" + fabricId + "/Zones"}};
                        }

                        const std::string& connectionName =
                            connectionNames[0].first;

                        // Fabric item properties
                        dbus::utility::async_method_call(
                            [asyncResp](
                                const boost::system::error_code ec3,
                                const std::vector<
                                    std::pair<std::string,
                                              dbus::utility::DbusVariantType>>&
                                    propertiesList) {
                                if (ec3)
                                {
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                for (const std::pair<
                                         std::string,
                                         dbus::utility::DbusVariantType>&
                                         property : propertiesList)
                                {
                                    if (property.first == "Type")
                                    {
                                        const std::string* value =
                                            std::get_if<std::string>(
                                                &property.second);
                                        if (value == nullptr)
                                        {
                                            BMCWEB_LOG_DEBUG(
                                                "Null value returned "
                                                "for fabric type");
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }
                                        asyncResp->res.jsonValue["FabricType"] =
                                            getFabricType(*value);
                                    }
                                }
                            },
                            connectionName, path,
                            "org.freedesktop.DBus.Properties", "GetAll",
                            "xyz.openbmc_project.Inventory.Item.Fabric");

                        return;
                    }
                    // Couldn't find an object with that name. Return an error
                    messages::resourceNotFound(
                        asyncResp->res, "#Fabric.v1_2_0.Fabric", fabricId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/inventory", 0,
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Inventory.Item.Fabric"});
        });
}

/**
 * SwitchCollection derived class for delivering Switch Collection Schema
 */
inline void requestRoutesSwitchCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Switches/")
        .privileges({{"Login"}})
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& fabricId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            asyncResp->res.jsonValue["@odata.type"] =
                "#SwitchCollection.SwitchCollection";
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/Fabrics/" + fabricId + "/Switches";
            asyncResp->res.jsonValue["Name"] = "Switch Collection";

            dbus::utility::async_method_call(
                [asyncResp, fabricId](const boost::system::error_code& ec,
                                      const std::vector<std::string>& objects) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (const std::string& object : objects)
                    {
                        // Get the fabricId object
                        if (!object.ends_with(fabricId))
                        {
                            continue;
                        }
                        collection_util::getCollectionMembersByAssociation(
                            asyncResp,
                            "/redfish/v1/Fabrics/" + fabricId + "/Switches",
                            object + "/all_switches",
                            {"xyz.openbmc_project.Inventory.Item.Switch",
                             "xyz.openbmc_project.Inventory.Item.NvSwitch"});
                        return;
                    }
                    // Couldn't find an object with that name. Return an
                    // error
                    messages::resourceNotFound(
                        asyncResp->res, "#Fabric.v1_2_0.Fabric", fabricId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                "/xyz/openbmc_project/inventory", 0,
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Inventory.Item.Fabric"});
        });
}

/**
 * @brief Fill out links for parent chassis PCIeDevice by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 * @param[in]       chassisName D-Bus object chassisName.
 */
inline void getSwitchParentChassisPCIeDeviceLink(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& chassisName)
{
    dbus::utility::async_method_call(
        [aResp, chassisName](const boost::system::error_code& ec,
                             std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no chassis = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr || data->size() > 1)
            {
                // Chassis must have single parent chassis
                return;
            }
            const std::string& parentChassisPath = data->front();
            sdbusplus::message::object_path objectPath(parentChassisPath);
            std::string parentChassisName = objectPath.filename();
            if (parentChassisName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            dbus::utility::async_method_call(
                [aResp, chassisName, parentChassisName](
                    const boost::system::error_code& ec2,
                    const dbus::utility::GetSubTreeType& subtree) {
                    if (ec2)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    for (const auto& [objectPath2, serviceMap] : subtree)
                    {
                        // Process same device
                        if (!objectPath2.ends_with(chassisName))
                        {
                            continue;
                        }
                        std::string pcieDeviceLink = "/redfish/v1/Chassis/";
                        pcieDeviceLink += parentChassisName;
                        pcieDeviceLink += "/PCIeDevices/";
                        pcieDeviceLink += chassisName;
                        aResp->res.jsonValue["Links"]["PCIeDevice"] = {
                            {"@odata.id", pcieDeviceLink}};
                    }
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                parentChassisPath, 0,
                std::array<const char*, 1>{"xyz.openbmc_project.Inventory.Item."
                                           "PCIeDevice"});
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Fill out links association to parent chassis by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getSwitchChassisLink(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get parent chassis link");
    dbus::utility::async_method_call(
        [aResp, objPath](const boost::system::error_code& ec,
                         std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no chassis = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr || data->size() > 1)
            {
                // Switch must have single parent chassis
                return;
            }
            const std::string& chassisPath = data->front();
            sdbusplus::message::object_path objectPath(chassisPath);
            std::string chassisName = objectPath.filename();
            if (chassisName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            aResp->res.jsonValue["Links"]["Chassis"] = {
                {"@odata.id", "/redfish/v1/Chassis/" + chassisName}};

            // Get PCIeDevice on this chassis
            dbus::utility::async_method_call(
                [aResp,
                 chassisName](const boost::system::error_code& ec1,
                              std::variant<std::vector<std::string>>& resp2) {
                    if (ec1)
                    {
                        BMCWEB_LOG_ERROR(
                            "Chassis {} has no connected PCIe devices",
                            chassisName);
                        return; // no pciedevices = no failures
                    }
                    std::vector<std::string>* data1 =
                        std::get_if<std::vector<std::string>>(&resp2);
                    if (data1 == nullptr || data1->size() > 1)
                    {
                        // Chassis must have single pciedevice
                        BMCWEB_LOG_ERROR("chassis must have single pciedevice");
                        return;
                    }

                    for (const std::string& pcieDevicePath : *data1)
                    {
                        sdbusplus::message::object_path objectPath2(
                            pcieDevicePath);
                        std::string pcieDeviceName = objectPath2.filename();
                        if (pcieDeviceName.empty())
                        {
                            BMCWEB_LOG_ERROR("chassis pciedevice name empty");
                            return;
                        }
                        std::string pcieDeviceLink = "/redfish/v1/Chassis/";
                        pcieDeviceLink += chassisName;
                        pcieDeviceLink += "/PCIeDevices/";
                        pcieDeviceLink += pcieDeviceName;
                        aResp->res.jsonValue["Links"]["PCIeDevice"] = {
                            {"@odata.id", pcieDeviceLink}};
                    }
                },
                "xyz.openbmc_project.ObjectMapper", chassisPath + "/pciedevice",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Association", "endpoints");
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Fill out links association to parent chassis by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 * @param[in]       fabricId    Fabric Id.
 */
inline void getSwitchEndpointsLink(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& fabricId)
{
    dbus::utility::async_method_call(
        [aResp, fabricId](const boost::system::error_code ec,
                          std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no endpoints = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            nlohmann::json& linksArray =
                aResp->res.jsonValue["Links"]["Endpoints"];
            linksArray = nlohmann::json::array();
            for (const std::string& endpointPath : *data)
            {
                sdbusplus::message::object_path objPath1(endpointPath);
                const std::string& endpointId = objPath1.filename();
                std::string endpointURI = "/redfish/v1/Fabrics/";
                endpointURI += fabricId;
                endpointURI += "/Endpoints/";
                endpointURI += endpointId;
                linksArray.push_back({{"@odata.id", endpointURI}});
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_endpoints",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Fill out managed by links association to manager service by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getManagerLink(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get managed_by links");
    dbus::utility::async_method_call(
        [aResp](const boost::system::error_code ec,
                std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no managed_by association = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            nlohmann::json& linksArray =
                aResp->res.jsonValue["Links"]["ManagedBy"];
            linksArray = nlohmann::json::array();
            for (const std::string& endpointPath : *data)
            {
                sdbusplus::message::object_path objPath2(endpointPath);
                const std::string& endpointId = objPath2.filename();
                std::string endpointURI = "/redfish/v1/Managers/";
                endpointURI += endpointId;
                linksArray.push_back({{"@odata.id", endpointURI}});
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/managed_by",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Fill the health by association
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getHealthByAssociatedChassis(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& objId)
{
    BMCWEB_LOG_DEBUG("Get health by association");
    dbus::utility::async_method_call(
        [aResp, objId](const boost::system::error_code ec,
                       std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no managed_by association = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            for (const std::string& path : *data)
            {
                redfish::nvidia_chassis_utils::getHealthByAssociation(
                    aResp, path, "all_states", objId);
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Fill out links association to parent chassis by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 * @param[in]       fabricId    Fabric Id.
 */
inline void getZoneEndpointsLink(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& fabricId)
{
    BMCWEB_LOG_DEBUG("Get zone endpoint links");
    dbus::utility::async_method_call(
        [aResp, fabricId](const boost::system::error_code ec,
                          std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no endpoints = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            nlohmann::json& linksArray =
                aResp->res.jsonValue["Links"]["Endpoints"];
            linksArray = nlohmann::json::array();
            for (const std::string& endpointPath : *data)
            {
                sdbusplus::message::object_path objPath2(endpointPath);
                const std::string& endpointId = objPath2.filename();
                std::string endpointURI = "/redfish/v1/Fabrics/";
                endpointURI += fabricId;
                endpointURI += "/Endpoints/";
                endpointURI += endpointId;
                linksArray.push_back({{"@odata.id", endpointURI}});
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/zone_endpoints",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}
/**
 * Switch override class for delivering Switch Schema
 */
inline void requestRoutesSwitch(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Switches/<str>/")
        .privileges({{"Login"}})
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& fabricId,
                            const std::string& switchId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            dbus::utility::async_method_call(
                [asyncResp, fabricId,
                 switchId](const boost::system::error_code ec,
                           const std::vector<std::string>& objects) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (const std::string& object : objects)
                    {
                        // Get the fabricId object
                        if (!object.ends_with(fabricId))
                        {
                            continue;
                        }
                        dbus::utility::async_method_call(
                            [asyncResp, fabricId, switchId](
                                const boost::system::error_code ec1,
                                std::variant<std::vector<std::string>>& resp) {
                                if (ec1)
                                {
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                std::vector<std::string>* data =
                                    std::get_if<std::vector<std::string>>(
                                        &resp);
                                if (data == nullptr)
                                {
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                // Iterate over all retrieved ObjectPaths.
                                for (const std::string& path : *data)
                                {
                                    sdbusplus::message::object_path objPath2(
                                        path);
                                    if (objPath2.filename() != switchId)
                                    {
                                        continue;
                                    }

                                    std::string switchURI =
                                        "/redfish/v1/Fabrics/";
                                    switchURI += fabricId;
                                    switchURI += "/Switches/";
                                    switchURI += switchId;
                                    std::string portsURI = switchURI;
                                    portsURI += "/Ports";
                                    std::string switchMetricURI = switchURI;
                                    switchMetricURI += "/SwitchMetrics";
                                    asyncResp->res.jsonValue["@odata.type"] =
                                        "#Switch.v1_8_0.Switch";
                                    asyncResp->res.jsonValue["@odata.id"] =
                                        switchURI;
                                    asyncResp->res.jsonValue["Id"] = switchId;
                                    asyncResp->res.jsonValue["Name"] =
                                        switchId + " Resource";

                                    if constexpr (BMCWEB_REDFISH_ROUTES_PORT)
                                    {
                                        asyncResp->res.jsonValue["Ports"] = {
                                            {"@odata.id", portsURI}};
                                    }

                                    asyncResp->res.jsonValue["Metrics"] = {
                                        {"@odata.id", switchMetricURI}};
                                    std::string switchResetURI =
                                        "/redfish/v1/Fabrics/";
                                    switchResetURI += fabricId;
                                    switchResetURI += "/Switches/";
                                    switchResetURI += switchId;
                                    switchResetURI += "/Actions/Switch.Reset";
                                    asyncResp->res
                                        .jsonValue["Actions"]["#Switch.Reset"] =
                                        {{"target", switchResetURI},
                                         {"ResetType@Redfish.AllowableValues",
                                          {"ForceRestart"}}};

                                    dbus::utility::async_method_call(
                                        [asyncResp, switchURI, path](
                                            const boost::system::error_code ec2,
                                            const std::vector<std::pair<
                                                std::string,
                                                std::vector<std::string>>>&
                                                object2) {
                                            if (ec2)
                                            {
                                                // the path does not implement
                                                // Item Switch interfaces
                                                messages::internalError(
                                                    asyncResp->res);
                                                return;
                                            }
                                            if constexpr (
                                                BMCWEB_NVIDIA_OEM_PROPERTIES)
                                            {
                                                redfish::nvidia_fabric_utils::
                                                    getSwitchPowerModeLink(
                                                        asyncResp, path,
                                                        switchURI);
                                                if (std::find(
                                                        object2.front()
                                                            .second.begin(),
                                                        object2.front()
                                                            .second.end(),
                                                        "com.nvidia.SwitchIsolation") !=
                                                    object2.front()
                                                        .second.end())
                                                {
                                                    redfish::nvidia_fabric_utils::
                                                        getSwitchIsolationMode(
                                                            asyncResp,
                                                            object2.front()
                                                                .first,
                                                            path,
                                                            "com.nvidia.SwitchIsolation");
                                                }
                                                if (std::find(
                                                        object2.front()
                                                            .second.begin(),
                                                        object2.front()
                                                            .second.end(),
                                                        "com.nvidia.State.FabricManager") !=
                                                    object2.front()
                                                        .second.end())
                                                {
                                                    redfish::nvidia_fabric_utils::
                                                        getFabricManagerState(
                                                            asyncResp,
                                                            object2.front()
                                                                .first,
                                                            path,
                                                            "com.nvidia.State.FabricManager");
                                                }
                                            }
                                            updateSwitchData(
                                                asyncResp,
                                                object2.front().first, path);
                                        },
                                        "xyz.openbmc_project.ObjectMapper",
                                        "/xyz/openbmc_project/object_mapper",
                                        "xyz.openbmc_project.ObjectMapper",
                                        "GetObject", path,
                                        std::array<const char*, 0>());

                                    // Link association to parent chassis
                                    getSwitchChassisLink(asyncResp, path);
                                    // Link association to endpoints
                                    getSwitchEndpointsLink(asyncResp, path,
                                                           fabricId);
                                    // Link association to manager
                                    getManagerLink(asyncResp, path);
                                    if constexpr (
                                        BMCWEB_NVIDIA_DEVICE_STATUS_FROM_ASSOCIATION)
                                    {
                                        // get health by association
                                        getHealthByAssociatedChassis(
                                            asyncResp, path, switchId);
                                    }

                                    if constexpr (
                                        !BMCWEB_DISABLE_CONDITIONS_ARRAY)
                                    {
                                        redfish::conditions_utils::
                                            populateServiceConditions(asyncResp,
                                                                      switchId);
                                    }
                                    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                                    {
                                        redfish::nvidia_fabric_utils::
                                            populateErrorInjectionData(
                                                asyncResp, fabricId, switchId);

                                        redfish::nvidia_histogram_utils::
                                            getHistogramLink(
                                                asyncResp, switchURI, path,
                                                "#NvidiaSwitch.v1_4_0.NvidiaSwitch");
                                    }
                                    return;
                                }
                                // Couldn't find an object with that name.
                                // Return an error
                                messages::resourceNotFound(
                                    asyncResp->res, "#Switch.v1_8_0.Switch",
                                    switchId);
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            object + "/all_switches",
                            "org.freedesktop.DBus.Properties", "Get",
                            "xyz.openbmc_project.Association", "endpoints");
                        return;
                    }
                    // Couldn't find an object with that name. Return an error
                    messages::resourceNotFound(
                        asyncResp->res, "#Fabric.v1_2_0.Fabric", fabricId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                "/xyz/openbmc_project/inventory", 0,
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Inventory.Item.Fabric"});
        });

    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Switches/<str>/")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& fabricId,
                   [[maybe_unused]] const std::string& switchId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::optional<nlohmann::json> oemObject;
                if (!redfish::json_util::readJsonAction(req, asyncResp->res,
                                                        "Oem", oemObject))
                {
                    return;
                }
                std::optional<nlohmann::json> oemNvidiaObject;
                if (oemObject &&
                    redfish::json_util::readJson(*oemObject, asyncResp->res,
                                                 "Nvidia", oemNvidiaObject))
                {
                    std::optional<std::string> isolationMode;
                    if (oemNvidiaObject &&
                        redfish::json_util::readJson(
                            *oemNvidiaObject, asyncResp->res,
                            "SwitchIsolationMode", isolationMode))
                    {
                        if (isolationMode)
                        {
                            if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                            {
                                redfish::nvidia_fabric_utils::getSwitchObject(
                                    asyncResp, fabricId, switchId,
                                    [isolationMode](
                                        const std::shared_ptr<
                                            bmcweb::AsyncResp>& asyncResp1,
                                        const std::string& fabricId1,
                                        const std::string& switchId1,
                                        const std::string& objectPath,
                                        [[maybe_unused]] const dbus::utility::
                                            MapperServiceMap& serviceMap) {
                                        redfish::nvidia_fabric_utils::
                                            patchSwitchIsolationMode(
                                                asyncResp1, fabricId1,
                                                switchId1, *isolationMode,
                                                objectPath, serviceMap);
                                    });
                            }
                        }
                    }
                }
            });
}

using DimmProperties =
    boost::container::flat_map<std::string, dbus::utility::DbusVariantType>;
inline void getInternalMemoryMetrics(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get memory ecc data.");
    dbus::utility::async_method_call(
        [aResp](const boost::system::error_code ec,
                const DimmProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& property : properties)
            {
                if (property.first == "ceCount")
                {
                    const int64_t* value =
                        std::get_if<int64_t>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["InternalMemoryMetrics"]["LifeTime"]
                                        ["CorrectableECCErrorCount"] = *value;
                }
                else if (property.first == "ueCount")
                {
                    const int64_t* value =
                        std::get_if<int64_t>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["InternalMemoryMetrics"]["LifeTime"]
                                        ["UncorrectableECCErrorCount"] = *value;
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Memory.MemoryECC");
}

inline void requestRoutesSwitchMetrics(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Switches/<str>/SwitchMetrics/")
        .privileges({{"Login"}})
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& fabricId,
                            const std::string& switchId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            dbus::utility::async_method_call(
                [asyncResp, fabricId,
                 switchId](const boost::system::error_code ec,
                           const std::vector<std::string>& objects) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (const std::string& object : objects)
                    {
                        // Get the fabricId object
                        if (!object.ends_with(fabricId))
                        {
                            continue;
                        }
                        dbus::utility::async_method_call(
                            [asyncResp, fabricId, switchId](
                                const boost::system::error_code ec1,
                                std::variant<std::vector<std::string>>& resp) {
                                if (ec1)
                                {
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                std::vector<std::string>* data =
                                    std::get_if<std::vector<std::string>>(
                                        &resp);
                                if (data == nullptr)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "DBUS response error while getting switches");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                for (const std::string& path : *data)
                                {
                                    // Get the switchId object
                                    if (!path.ends_with(switchId))
                                    {
                                        continue;
                                    }

                                    dbus::utility::async_method_call(
                                        [asyncResp, fabricId, switchId, path](
                                            const boost::system::error_code ec2,
                                            const std::vector<std::pair<
                                                std::string,
                                                std::vector<std::string>>>&
                                                object2) {
                                            if (ec2)
                                            {
                                                BMCWEB_LOG_ERROR(
                                                    "Error while fetching service for {}",
                                                    path);
                                                messages::internalError(
                                                    asyncResp->res);
                                                return;
                                            }

                                            if (object2.empty())
                                            {
                                                BMCWEB_LOG_ERROR(
                                                    "Empty response received");
                                                messages::internalError(
                                                    asyncResp->res);
                                                return;
                                            }

                                            std::string switchURI =
                                                "/redfish/v1/Fabrics/";
                                            switchURI += fabricId;
                                            switchURI += "/Switches/";
                                            switchURI += switchId;
                                            std::string switchMetricURI =
                                                switchURI;
                                            switchMetricURI += "/SwitchMetrics";
                                            asyncResp->res
                                                .jsonValue["@odata.type"] =
                                                "#SwitchMetrics.v1_0_0.SwitchMetrics";
                                            asyncResp->res
                                                .jsonValue["@odata.id"] =
                                                switchMetricURI;
                                            asyncResp->res.jsonValue["Id"] =
                                                switchId;
                                            asyncResp->res.jsonValue["Name"] =
                                                switchId + " Metrics";
                                            const std::string& connectionName =
                                                object2.front().first;
                                            const std::vector<std::string>&
                                                interfaces =
                                                    object2.front().second;
                                            if (std::find(
                                                    interfaces.begin(),
                                                    interfaces.end(),
                                                    "xyz.openbmc_project.Memory.MemoryECC") !=
                                                interfaces.end())
                                            {
                                                getInternalMemoryMetrics(
                                                    asyncResp, connectionName,
                                                    path);
                                            }
                                            if (std::find(
                                                    interfaces.begin(),
                                                    interfaces.end(),
                                                    "xyz.openbmc_project.PCIe.PCIeECC") !=
                                                interfaces.end())
                                            {
                                                redfish::processor_utils::
                                                    getPCIeErrorData(
                                                        asyncResp,
                                                        connectionName, path);
                                            }
                                        },
                                        "xyz.openbmc_project.ObjectMapper",
                                        "/xyz/openbmc_project/object_mapper",
                                        "xyz.openbmc_project.ObjectMapper",
                                        "GetObject", path,
                                        std::array<const char*, 0>());

                                    return;
                                }
                                // Couldn't find an object with that name.
                                // Return an error
                                messages::resourceNotFound(
                                    asyncResp->res, "#Switch.v1_8_0.Switch",
                                    switchId);
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            object + "/all_switches",
                            "org.freedesktop.DBus.Properties", "Get",
                            "xyz.openbmc_project.Association", "endpoints");
                        return;
                    }
                    // Couldn't find an object with that name. Return an error
                    messages::resourceNotFound(
                        asyncResp->res, "#Fabric.v1_2_0.Fabric", fabricId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                "/xyz/openbmc_project/inventory", 0,
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Inventory.Item.Fabric"});
        });
}

inline std::string getNVSwitchResetType(const std::string& processorType)
{
    if (processorType ==
        "xyz.openbmc_project.Control.Processor.Reset.ResetTypes.ForceOff")
    {
        return "ForceOff";
    }
    if (processorType ==
        "xyz.openbmc_project.Control.Processor.Reset.ResetTypes.ForceOn")
    {
        return "ForceOn";
    }
    if (processorType ==
        "xyz.openbmc_project.Control.Processor.Reset.ResetTypes.ForceRestart")
    {
        return "ForceRestart";
    }
    if (processorType ==
        "xyz.openbmc_project.Control.Processor.Reset.ResetTypes.GracefulRestart")
    {
        return "GracefulRestart";
    }
    if (processorType ==
        "xyz.openbmc_project.Control.Processor.Reset.ResetTypes.GracefulShutdown")
    {
        return "GracefulShutdown";
    }
    // Unknown or others
    return "";
}

inline std::string getSwitchResetType(const std::string& processorType)
{
    if (processorType ==
        "xyz.openbmc_project.Control.Reset.ResetTypes.ForceOff")
    {
        return "ForceOff";
    }
    if (processorType == "xyz.openbmc_project.Control.Reset.ResetTypes.ForceOn")
    {
        return "ForceOn";
    }
    if (processorType ==
        "xyz.openbmc_project.Control.Reset.ResetTypes.ForceRestart")
    {
        return "ForceRestart";
    }
    if (processorType ==
        "xyz.openbmc_project.Control.Reset.ResetTypes.GracefulRestart")
    {
        return "GracefulRestart";
    }
    if (processorType ==
        "xyz.openbmc_project.Control.Reset.ResetTypes.GracefulShutdown")
    {
        return "GracefulShutdown";
    }
    // Unknown or others
    return "";
}

inline void switchPostResetType(
    const std::shared_ptr<bmcweb::AsyncResp>& resp, const std::string& switchId,
    const std::string& objectPath, const std::string& resetType,
    const std::vector<std::pair<std::string, std::vector<std::string>>>&
        serviceMap)
{
    std::vector<std::string> resetInterfaces = {
        "xyz.openbmc_project.Control.ResetAsync",
        "xyz.openbmc_project.Control.Processor.Reset"};

    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    bool resetIntfImp = false;
    bool resetAsyncIntfImp = false;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        for (const auto& iface : resetInterfaces)
        {
            auto it =
                std::find(interfaceList.begin(), interfaceList.end(), iface);
            if (it != interfaceList.end())
            {
                inventoryService = &serviceName;
                if (iface == "xyz.openbmc_project.Control.ResetAsync")
                {
                    resetAsyncIntfImp = true;
                }
                if (iface == "xyz.openbmc_project.Control.Processor.Reset")
                {
                    resetIntfImp = true;
                }
            }
        }
        if (resetIntfImp || resetAsyncIntfImp)
        {
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        BMCWEB_LOG_ERROR(
            "switchPostResetType error service not implementing reset interface");
        messages::internalError(resp->res);
        return;
    }

    const std::string conName = *inventoryService;
    if (resetAsyncIntfImp)
    {
        dbus::utility::getProperty<std::string>(
            conName, objectPath, "xyz.openbmc_project.Control.Reset",
            "ResetType",
            [resp, resetType, switchId, conName,
             objectPath](const boost::system::error_code ec,
                         const std::string& property) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("DBus response, error for ResetType ");
                    BMCWEB_LOG_ERROR("{}", ec.message());
                    messages::internalError(resp->res);
                    return;
                }

                const std::string switchResetType =
                    getSwitchResetType(property);
                if (switchResetType != resetType)
                {
                    BMCWEB_LOG_DEBUG(
                        "Property Value Incorrect {} while allowed is {}",
                        resetType, switchResetType);
                    messages::actionParameterNotSupported(
                        resp->res, "ResetType", resetType);
                    return;
                }

                BMCWEB_LOG_DEBUG("Performing Post using Async Method Call");

                nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<
                    int>(resp, std::chrono::seconds(60), conName, objectPath,
                         "xyz.openbmc_project.Control.ResetAsync", "Reset",
                         [resp](const std::string& status,
                                [[maybe_unused]] const int* retValue) {
                             if (status == nvidia_async_operation_utils::
                                               asyncStatusValueSuccess)
                             {
                                 BMCWEB_LOG_DEBUG("Switch Reset Succeeded");
                                 messages::success(resp->res);
                                 return;
                             }
                             BMCWEB_LOG_ERROR("Switch reset error {}", status);
                             messages::internalError(resp->res);
                         });
            });
    }
    else if (resetIntfImp)
    {
        dbus::utility::getProperty<std::string>(
            conName, objectPath, "xyz.openbmc_project.Control.Processor.Reset",
            "ResetType",
            [resp, resetType, switchId, conName,
             objectPath](const boost::system::error_code ec,
                         const std::string& property) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("DBus response, error for ResetType ");
                    BMCWEB_LOG_ERROR("{}", ec.message());
                    messages::internalError(resp->res);
                    return;
                }

                const std::string switchResetType =
                    getNVSwitchResetType(property);
                if (switchResetType != resetType)
                {
                    BMCWEB_LOG_DEBUG(
                        "Property Value Incorrect {} while allowed is {}",
                        resetType, switchResetType);
                    messages::actionParameterNotSupported(
                        resp->res, "ResetType", resetType);
                    return;
                }

                BMCWEB_LOG_DEBUG("Performing Post using Sync Method Call");

                // Set the property, with handler to check error responses
                dbus::utility::async_method_call(
                    [resp, switchId](boost::system::error_code ec2,
                                     const int retValue) {
                        if (!ec2)
                        {
                            if (retValue != 0)
                            {
                                BMCWEB_LOG_ERROR("{}", retValue);
                                messages::internalError(resp->res);
                            }
                            BMCWEB_LOG_DEBUG("Switch:{} Reset Succeded",
                                             switchId);
                            messages::success(resp->res);
                            return;
                        }
                        BMCWEB_LOG_ERROR("Error: {}", ec2);
                        messages::internalError(resp->res);
                        return;
                    },
                    conName, objectPath,
                    "xyz.openbmc_project.Control.Processor.Reset", "Reset");
            });
    }
    else
    {
        BMCWEB_LOG_ERROR("No reset interface implemented.");
        messages::internalError(resp->res);
        return;
    }
}

/**
 * Functions triggers appropriate NVSwitch Reset requests on DBus
 */
inline void requestRoutesNVSwitchReset(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Switches/<str>/"
                      "Actions/Switch.Reset/")
        .privileges({{"Login"}})
        .methods(
            boost::beast::http::verb::
                post)([&app](
                          const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& fabricId,
                          const std::string& switchId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            std::optional<std::string> resetType;
            if (!redfish::json_util::readJsonAction(req, asyncResp->res,
                                                    "ResetType", resetType))
            {
                return;
            }
            if (resetType)
            {
                dbus::utility::async_method_call(
                    [asyncResp, fabricId, switchId,
                     resetType](const boost::system::error_code ec,
                                const std::vector<std::string>& objects) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR("DBUS response error");
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        for (const std::string& object : objects)
                        {
                            // Get the fabricId object
                            if (!object.ends_with(fabricId))
                            {
                                continue;
                            }
                            dbus::utility::async_method_call(
                                [asyncResp, fabricId, switchId, resetType](
                                    const boost::system::error_code ec1,
                                    std::variant<std::vector<std::string>>&
                                        resp) {
                                    if (ec1)
                                    {
                                        BMCWEB_LOG_ERROR("DBUS response error");
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }
                                    std::vector<std::string>* data =
                                        std::get_if<std::vector<std::string>>(
                                            &resp);
                                    if (data == nullptr)
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "DBUS response error while getting switches");
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }
                                    for (const std::string& objectPath : *data)
                                    {
                                        // Get the switchId object
                                        if (!objectPath.ends_with(switchId))
                                        {
                                            continue;
                                        }

                                        dbus::utility::async_method_call(
                                            [asyncResp, switchId, resetType,
                                             objectPath](
                                                const boost::system::error_code
                                                    ec2,
                                                const std::vector<std::pair<
                                                    std::string,
                                                    std::vector<std::string>>>&
                                                    obj) {
                                                if (ec2)
                                                {
                                                    BMCWEB_LOG_ERROR(
                                                        "DBUS response error while getting service");
                                                    messages::internalError(
                                                        asyncResp->res);
                                                    return;
                                                }
                                                switchPostResetType(
                                                    asyncResp, switchId,
                                                    objectPath, *resetType,
                                                    obj);
                                            },
                                            "xyz.openbmc_project.ObjectMapper",
                                            "/xyz/openbmc_project/object_mapper",
                                            "xyz.openbmc_project.ObjectMapper",
                                            "GetObject", objectPath,
                                            std::array<const char*, 0>());
                                        return;
                                    }
                                    // Couldn't find an object with that name.
                                    // Return an error
                                    messages::resourceNotFound(
                                        asyncResp->res, "#Switch.v1_8_0.Switch",
                                        switchId);
                                },
                                "xyz.openbmc_project.ObjectMapper",
                                object + "/all_switches",
                                "org.freedesktop.DBus.Properties", "Get",
                                "xyz.openbmc_project.Association", "endpoints");
                            return;
                        }
                        // Couldn't find an object with that name. Return an
                        // error
                        messages::resourceNotFound(
                            asyncResp->res, "#Fabric.v1_2_0.Fabric", fabricId);
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                    "/xyz/openbmc_project/inventory", 0,
                    std::array<const char*, 1>{
                        "xyz.openbmc_project.Inventory.Item.Fabric"});
            }
        });
}

/**
 * PortCollection derived class for delivering Port Collection Schema
 */
inline void requestRoutesPortCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Switches/<str>/Ports/")
        .privileges({{"Login"}})
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& fabricId,
                            const std::string& switchId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            std::string portsURI = "/redfish/v1/Fabrics/";
            portsURI += fabricId;
            portsURI += "/Switches/";
            portsURI += switchId;
            portsURI += "/Ports";
            asyncResp->res.jsonValue["@odata.type"] =
                "#PortCollection.PortCollection";
            asyncResp->res.jsonValue["@odata.id"] = portsURI;
            asyncResp->res.jsonValue["Name"] = "Port Collection";

            dbus::utility::async_method_call(
                [asyncResp, fabricId, switchId,
                 portsURI](const boost::system::error_code ec,
                           const std::vector<std::string>& objects) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (const std::string& object : objects)
                    {
                        // Get the fabricId object
                        if (!object.ends_with(fabricId))
                        {
                            continue;
                        }
                        dbus::utility::async_method_call(
                            [asyncResp, fabricId, switchId, portsURI](
                                const boost::system::error_code ec2,
                                std::variant<std::vector<std::string>>& resp2) {
                                if (ec2)
                                {
                                    BMCWEB_LOG_ERROR("DBUS response error");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                std::vector<std::string>* data2 =
                                    std::get_if<std::vector<std::string>>(
                                        &resp2);
                                if (data2 == nullptr)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "DBUS response error while getting switches");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                for (const std::string& object2 : *data2)
                                {
                                    // Get the switchId object
                                    if (!object2.ends_with(switchId))
                                    {
                                        continue;
                                    }
                                    collection_util::getCollectionMembersByAssociation(
                                        asyncResp, portsURI,
                                        object2 + "/all_states",
                                        {"xyz.openbmc_project.Inventory.Item.Port"});
                                    return;
                                }
                                // Couldn't find an object with that name.
                                // Return an error
                                messages::resourceNotFound(
                                    asyncResp->res, "#Switch.v1_8_0.Switch",
                                    switchId);
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            object + "/all_switches",
                            "org.freedesktop.DBus.Properties", "Get",
                            "xyz.openbmc_project.Association", "endpoints");
                        return;
                    }
                    // Couldn't find an object with that name. Return an error
                    messages::resourceNotFound(
                        asyncResp->res, "#Fabric.v1_2_0.Fabric", fabricId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                "/xyz/openbmc_project/inventory", 0,
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Inventory.Item.Fabric"});
        });
}

/**
 * @brief Populate Port response JSON and invoke link-update helpers.
 */
inline void populateSwitchPortResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId,
    const std::string& portId, const std::string& objectPathToGetPortData,
    const std::string& portPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, fabricId, switchId, portId, objectPathToGetPortData,
         portPath](
            const boost::system::error_code ec6,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                object) {
            if (ec6)
            {
                BMCWEB_LOG_DEBUG("No switch interface found {}",
                                 objectPathToGetPortData);
                return;
            }

            std::string portURI = "/redfish/v1/Fabrics/";
            portURI += fabricId;
            portURI += "/Switches/";
            portURI += switchId;
            portURI += "/Ports/";
            portURI += portId;
            asyncResp->res.jsonValue = {{"@odata.type", "#Port.v1_4_0.Port"},
                                        {"@odata.id", portURI},
                                        {"Name", portId + " Resource"},
                                        {"Id", portId}};
            std::string portMetricsURI = portURI + "/Metrics";
            asyncResp->res.jsonValue["Metrics"]["@odata.id"] = portMetricsURI;

#ifndef BMCWEB_DISABLE_CONDITIONS_ARRAY
            asyncResp->res.jsonValue["Status"]["Conditions"] =
                nlohmann::json::array();
#endif // BMCWEB_DISABLE_CONDITIONS_ARRAY

            if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
            {
                redfish::nvidia_histogram_utils::getHistogramLink(
                    asyncResp, portURI, portPath,
                    "#NvidiaPort.v1_2_0.NvidiaNVLinkPort");
            }

            redfish::port_utils::getPortData(asyncResp, object.front().first,
                                             objectPathToGetPortData);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject",
        objectPathToGetPortData,
        std::array<std::string, 1>(
            {"xyz.openbmc_project.Inventory.Item.Port"}));

    updateProcessorPortLinks(asyncResp, portPath, fabricId);
    updateNetworkAdapterPortLinks(asyncResp, portPath);
    updateSwitchPortLinks(asyncResp, portPath, fabricId);
    updatePCIeEqualization(asyncResp, portPath, fabricId, switchId, portId);
}

/**
 * @brief Resolve the associated_port endpoint and populate the Port response.
 */
inline void resolveAssociatedPortAndPopulate(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId,
    const std::string& portId, const std::string& portPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, fabricId, switchId, portId,
         portPath](const boost::system::error_code& ec5,
                   std::variant<std::vector<std::string>>& response) {
            std::string objectPathToGetPortData = portPath;
            if (!ec5)
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

            populateSwitchPortResponse(asyncResp, fabricId, switchId, portId,
                                       objectPathToGetPortData, portPath);
        },
        "xyz.openbmc_project.ObjectMapper", portPath + "/associated_port",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Get port endpoints under a switch and match the requested portId.
 */
inline void getPortOnSwitch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId,
    const std::string& portId, const std::string& switchPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, fabricId, switchId,
         portId](const boost::system::error_code ec4,
                 std::variant<std::vector<std::string>>& resp4) {
            if (ec4)
            {
                if (ec4.value() == EBADR)
                {
                    messages::resourceNotFound(asyncResp->res, "Port", portId);
                }
                else
                {
                    BMCWEB_LOG_ERROR("DBUS response error");
                    messages::internalError(asyncResp->res);
                }
                return;
            }
            std::vector<std::string>* data4 =
                std::get_if<std::vector<std::string>>(&resp4);
            if (data4 == nullptr)
            {
                BMCWEB_LOG_ERROR("DBUS response error while getting ports");
                messages::internalError(asyncResp->res);
                return;
            }
            for (const std::string& portPath : *data4)
            {
                sdbusplus::message::object_path pPath(portPath);
                if (pPath.filename() != portId)
                {
                    continue;
                }
                resolveAssociatedPortAndPopulate(asyncResp, fabricId, switchId,
                                                 portId, portPath);
                return;
            }
            messages::resourceNotFound(asyncResp->res, "#Port.v1_0_0.Port",
                                       portId);
        },
        "xyz.openbmc_project.ObjectMapper", switchPath + "/all_states",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Get switch endpoints under a fabric and match the requested switchId.
 */
inline void getSwitchOnFabric(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId,
    const std::string& portId, const std::string& fabricPath)
{
    dbus::utility::async_method_call(
        [asyncResp, fabricId, switchId,
         portId](const boost::system::error_code ec3,
                 std::variant<std::vector<std::string>>& resp3) {
            if (ec3)
            {
                if (ec3.value() == EBADR)
                {
                    messages::resourceNotFound(asyncResp->res, "Switch",
                                               switchId);
                }
                else
                {
                    BMCWEB_LOG_ERROR("DBUS response error");
                    messages::internalError(asyncResp->res);
                }
                return;
            }
            std::vector<std::string>* data3 =
                std::get_if<std::vector<std::string>>(&resp3);
            if (data3 == nullptr)
            {
                BMCWEB_LOG_ERROR("DBUS response error while getting switches");
                messages::internalError(asyncResp->res);
                return;
            }
            for (const std::string& switchPath : *data3)
            {
                if (!switchPath.ends_with(switchId))
                {
                    continue;
                }
                getPortOnSwitch(asyncResp, fabricId, switchId, portId,
                                switchPath);
                return;
            }
            messages::resourceNotFound(asyncResp->res, "#Switch.v1_8_0.Switch",
                                       switchId);
        },
        "xyz.openbmc_project.ObjectMapper", fabricPath + "/all_switches",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Find the fabric path matching fabricId, then resolve switch and port.
 */
inline void getFabricSwitchPort(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId,
    const std::string& portId)
{
    dbus::utility::async_method_call(
        [asyncResp, fabricId, switchId,
         portId](const boost::system::error_code ec,
                 const std::vector<std::string>& objects) {
            if (ec)
            {
                if (ec.value() == EBADR)
                {
                    messages::resourceNotFound(asyncResp->res, "Fabric",
                                               fabricId);
                }
                else
                {
                    BMCWEB_LOG_ERROR("DBUS response error");
                    messages::internalError(asyncResp->res);
                }
                return;
            }
            for (const std::string& fabricPath : objects)
            {
                if (!fabricPath.ends_with(fabricId))
                {
                    continue;
                }
                getSwitchOnFabric(asyncResp, fabricId, switchId, portId,
                                  fabricPath);
                return;
            }
            messages::resourceNotFound(asyncResp->res, "#Fabric.v1_2_0.Fabric",
                                       fabricId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Item.Fabric"});
}

/**
 * Port override class for delivering Port Schema
 */
inline void requestRoutesPort(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Switches/<str>/Ports/<str>/")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& fabricId, const std::string& switchId,
                   const std::string& portId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                getFabricSwitchPort(asyncResp, fabricId, switchId, portId);
            });
}

inline void requestRoutesZoneCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Zones/")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& fabricId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                asyncResp->res.jsonValue["@odata.type"] =
                    "#ZoneCollection.ZoneCollection";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Fabrics/" + fabricId + "/Zones";
                asyncResp->res.jsonValue["Name"] = "Zone Collection";

                dbus::utility::async_method_call(
                    [asyncResp,
                     fabricId](const boost::system::error_code& ec,
                               const std::vector<std::string>& objects) {
                        if (ec)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        for (const std::string& object : objects)
                        {
                            // Get the fabricId object
                            if (!object.ends_with(fabricId))
                            {
                                continue;
                            }
                            constexpr std::array<std::string_view, 1> interface{
                                "xyz.openbmc_project.Inventory.Item.Zone"};
                            collection_util::getCollectionMembers(
                                asyncResp,
                                boost::urls::format("/redfish/v1/Fabrics/" +
                                                    fabricId + "/Zones"),
                                interface, object);
                            return;
                        }
                        // Couldn't find an object with that name. Return an
                        // error
                        messages::resourceNotFound(
                            asyncResp->res, "#Fabric.v1_2_0.Fabric", fabricId);
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                    "/xyz/openbmc_project/inventory", 0,
                    std::array<const char*, 1>{
                        "xyz.openbmc_project.Inventory.Item.Fabric"});
            });
}

inline void requestRoutesZone(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Zones/<str>/")
        .privileges({{"Login"}})
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& fabricId,
                            const std::string& zoneId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            dbus::utility::async_method_call(
                [asyncResp, fabricId,
                 zoneId](const boost::system::error_code& ec,
                         const std::vector<std::string>& objects) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (const std::string& object : objects)
                    {
                        // Get the fabricId object
                        if (!object.ends_with(fabricId))
                        {
                            continue;
                        }
                        dbus::utility::async_method_call(
                            [asyncResp, fabricId, zoneId](
                                const boost::system::error_code ec1,
                                const dbus::utility::GetSubTreeType& subtree) {
                                if (ec1)
                                {
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                // Iterate over all retrieved ObjectPaths.
                                for (const std::pair<
                                         std::string,
                                         std::vector<std::pair<
                                             std::string,
                                             std::vector<std::string>>>>&
                                         object2 : subtree)
                                {
                                    // Get the zoneId object
                                    const std::string& path = object2.first;
                                    const std::vector<std::pair<
                                        std::string, std::vector<std::string>>>&
                                        connectionNames = object2.second;
                                    sdbusplus::message::object_path objPath(
                                        path);
                                    if (objPath.filename() != zoneId)
                                    {
                                        continue;
                                    }
                                    if (connectionNames.empty())
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "Got 0 Connection names");
                                        continue;
                                    }
                                    std::string zoneURI =
                                        "/redfish/v1/Fabrics/";
                                    zoneURI += fabricId;
                                    zoneURI += "/Zones/";
                                    zoneURI += zoneId;
                                    asyncResp->res.jsonValue["@odata.type"] =
                                        "#Zone.v1_6_1.Zone";
                                    asyncResp->res.jsonValue["@odata.id"] =
                                        zoneURI;
                                    asyncResp->res.jsonValue["Id"] = zoneId;
                                    asyncResp->res.jsonValue["Name"] =
                                        " Zone " + zoneId;
                                    const std::string& connectionName =
                                        connectionNames[0].first;
                                    updateZoneData(asyncResp, connectionName,
                                                   path);

                                    // Link association to endpoints
                                    getZoneEndpointsLink(asyncResp, path,
                                                         fabricId);
                                    return;
                                }
                                // Couldn't find an object with that name.
                                // Return an error
                                messages::resourceNotFound(asyncResp->res,
                                                           "#Zone.v1_6_1.Zone",
                                                           zoneId);
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            "/xyz/openbmc_project/object_mapper",
                            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                            object, 0,
                            std::array<const char*, 1>{
                                "xyz.openbmc_project.Inventory.Item.Zone"});
                        return;
                    }
                    // Couldn't find an object with that name. Return an error
                    messages::resourceNotFound(
                        asyncResp->res, "#Fabric.v1_2_0.Fabric", fabricId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                "/xyz/openbmc_project/inventory", 0,
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Inventory.Item.Fabric"});
        });
}

/**
 * Endpoint derived class for delivering Endpoint Collection Schema
 */
inline void requestRoutesEndpointCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Endpoints/")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& fabricId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                asyncResp->res.jsonValue["@odata.type"] =
                    "#EndpointCollection.EndpointCollection";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Fabrics/" + fabricId + "/Endpoints";
                asyncResp->res.jsonValue["Name"] = "Endpoint Collection";

                dbus::utility::async_method_call(
                    [asyncResp,
                     fabricId](const boost::system::error_code& ec,
                               const std::vector<std::string>& objects) {
                        if (ec)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        for (const std::string& object : objects)
                        {
                            // Get the fabricId object
                            if (!object.ends_with(fabricId))
                            {
                                continue;
                            }
                            constexpr std::array<std::string_view, 1> interface{
                                "xyz.openbmc_project.Inventory.Item.Endpoint"};
                            collection_util::getCollectionMembers(
                                asyncResp,
                                boost::urls::format("/redfish/v1/Fabrics/" +
                                                    fabricId + "/Endpoints"),
                                interface, object);
                            return;
                        }
                        // Couldn't find an object with that name. Return an
                        // error
                        messages::resourceNotFound(
                            asyncResp->res, "#Fabric.v1_2_0.Fabric", fabricId);
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                    "/xyz/openbmc_project/inventory", 0,
                    std::array<const char*, 1>{
                        "xyz.openbmc_project.Inventory.Item.Fabric"});
            });
}

/**
 * @brief Get all endpoint pcie device info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   aResp   Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 * @param[in]       entityLink  redfish entity link.
 */
inline void getProcessorPCIeDeviceData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath, const std::string& entityLink)
{
    dbus::utility::async_method_call(
        [aResp, entityLink](
            const boost::system::error_code& ec,
            const boost::container::flat_map<
                std::string, std::variant<std::string>>& pcieDevProperties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            // Get the device data from single function
            const std::string& function = "0";
            std::string deviceId;
            std::string vendorId;
            std::string subsystemId;
            std::string subsystemVendorId;
            for (const auto& property : pcieDevProperties)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "Function" + function + "DeviceId")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        deviceId = *value;
                    }
                }
                else if (propertyName == "Function" + function + "VendorId")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        vendorId = *value;
                    }
                }
                else if (propertyName == "Function" + function + "SubsystemId")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        subsystemId = *value;
                    }
                }
                else if (propertyName ==
                         "Function" + function + "SubsystemVendorId")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        subsystemVendorId = *value;
                    }
                }
            }
            nlohmann::json& connectedEntitiesArray =
                aResp->res.jsonValue["ConnectedEntities"];
            connectedEntitiesArray.push_back(
                {{"EntityType", "Processor"},
                 {"EntityPciId",
                  {{"DeviceId", deviceId},
                   {"VendorId", vendorId},
                   {"SubsystemId", subsystemId},
                   {"SubsystemVendorId", subsystemVendorId}}},
                 {"EntityLink", {{"@odata.id", entityLink}}}});
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Inventory.Item.PCIeDevice");
}

inline void getProcessorEndpointHealth(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath)
{
    // Set the default value of state
    aResp->res.jsonValue["Status"]["State"] = "Enabled";
    aResp->res.jsonValue["Status"]["Health"] = "OK";

    dbus::utility::async_method_call(
        [aResp](const boost::system::error_code& ec,
                const boost::container::flat_map<
                    std::string, std::variant<std::string, uint32_t, uint16_t,
                                              bool>>& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            for (const auto& property : properties)
            {
                if (property.first == "Present")
                {
                    const bool* isPresent = std::get_if<bool>(&property.second);
                    if (isPresent == nullptr)
                    {
                        // Important property not in desired type
                        messages::internalError(aResp->res);
                        return;
                    }
                    if (!*isPresent)
                    {
                        aResp->res.jsonValue["Status"]["State"] = "Absent";
                    }
                }
                else if (property.first == "Functional")
                {
                    const bool* isFunctional =
                        std::get_if<bool>(&property.second);
                    if (isFunctional == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    if (!*isFunctional)
                    {
                        aResp->res.jsonValue["Status"]["Health"] = "Critical";
                    }
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll", "");
}

/**
 * @brief Fill out links for parent chassis PCIeDevice by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 * @param[in]       chassisName D-Bus object chassisName.
 * @param[in]       entityLink  redfish entity link.
 */
inline void getProcessorParentEndpointData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& chassisName, const std::string& entityLink,
    const std::string& processorPath)
{
    dbus::utility::async_method_call(
        [aResp, chassisName, entityLink,
         processorPath](const boost::system::error_code& ec,
                        std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no chassis = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr || data->size() > 1)
            {
                // Chassis must have single parent chassis
                return;
            }
            const std::string& parentChassisPath = data->front();
            sdbusplus::message::object_path objectPath(parentChassisPath);
            std::string parentChassisName = objectPath.filename();
            if (parentChassisName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            dbus::utility::async_method_call(
                [aResp, chassisName, parentChassisName, entityLink,
                 processorPath](const boost::system::error_code ec1,
                                const dbus::utility::GetSubTreeType& subtree) {
                    if (ec1)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    for (const auto& [objectPath2, serviceMap2] : subtree)
                    {
                        // Process same device
                        if (!objectPath2.ends_with(chassisName))
                        {
                            continue;
                        }
                        if (serviceMap2.empty())
                        {
                            BMCWEB_LOG_ERROR("Got 0 service "
                                             "names");
                            messages::internalError(aResp->res);
                            return;
                        }
                        const std::string& serviceName = serviceMap2[0].first;
                        // Get PCIeDevice Data
                        getProcessorPCIeDeviceData(aResp, serviceName,
                                                   objectPath2, entityLink);
                        // Update processor health
                        getProcessorEndpointHealth(aResp, serviceName,
                                                   processorPath);
                    }
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                parentChassisPath, 0,
                std::array<const char*, 1>{"xyz.openbmc_project.Inventory.Item."
                                           "PCIeDevice"});
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Get all endpoint pcie device info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       processorPath     D-Bus service to query.
 * @param[in]       entityLink  redfish entity link.
 */
inline void getEndpointData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            const std::string& processorPath,
                            const std::string& entityLink,
                            const std::string& serviceName)
{
    dbus::utility::async_method_call(
        [aResp, processorPath, serviceName,
         entityLink](const boost::system::error_code& ec,
                     std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no chassis = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr || data->size() > 1)
            {
                // Processor must have single parent chassis
                return;
            }
            const std::string& chassisPath = data->front();
            sdbusplus::message::object_path objectPath(chassisPath);
            std::string chassisName = objectPath.filename();
            if (chassisName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }

            dbus::utility::async_method_call(
                [aResp, chassisName, processorPath, serviceName,
                 entityLink](const boost::system::error_code& ec1,
                             std::variant<std::vector<std::string>>& resp2) {
                    if (ec1)
                    {
                        BMCWEB_LOG_ERROR(
                            "Chassis {} has no connected PCIe devices",
                            chassisName);
                        return; // no pciedevices = no failures
                    }
                    std::vector<std::string>* data2 =
                        std::get_if<std::vector<std::string>>(&resp2);
                    if (data2 == nullptr || data2->size() > 1)
                    {
                        // Chassis must have single pciedevice
                        BMCWEB_LOG_ERROR("chassis must have single pciedevice");
                        return;
                    }
                    const std::string& pcieDevicePath = data2->front();
                    sdbusplus::message::object_path objectPath2(pcieDevicePath);
                    std::string pcieDeviceName = objectPath2.filename();
                    if (pcieDeviceName.empty())
                    {
                        BMCWEB_LOG_ERROR("chassis pciedevice name empty");
                        messages::internalError(aResp->res);
                        return;
                    }
                    // Get PCIeDevice Data
                    getProcessorPCIeDeviceData(aResp, serviceName,
                                               pcieDevicePath, entityLink);
                    // Update processor health
                    getProcessorEndpointHealth(aResp, serviceName,
                                               processorPath);
                },
                "xyz.openbmc_project.ObjectMapper", chassisPath + "/pciedevice",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Association", "endpoints");
        },
        "xyz.openbmc_project.ObjectMapper", processorPath + "/parent_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Get all endpoint pcie device info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   aResp   Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getPortData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                        const std::string& objPath)
{
    dbus::utility::async_method_call(
        [aResp, objPath](const boost::system::error_code& ec,
                         std::variant<std::vector<std::string>>& response) {
            std::string objectPathToGetPortData = objPath;
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
            dbus::utility::async_method_call(
                [aResp, objectPathToGetPortData](
                    const boost::system::error_code& ec1,
                    const std::vector<std::pair<
                        std::string, std::vector<std::string>>>& object3) {
                    if (ec1)
                    {
                        BMCWEB_LOG_DEBUG("No port interface found {}",
                                         objectPathToGetPortData);
                        return;
                    }
                    dbus::utility::async_method_call(
                        [aResp](const boost::system::error_code& ec2,
                                const boost::container::flat_map<
                                    std::string,
                                    std::variant<std::string, bool, size_t,
                                                 std::vector<std::string>>>&
                                    properties) {
                            if (ec2)
                            {
                                BMCWEB_LOG_ERROR("DBUS response error");
                                messages::internalError(aResp->res);
                                return;
                            }
                            // Get port protocol
                            for (const auto& property : properties)
                            {
                                if (property.first == "Protocol")
                                {
                                    const std::string* value =
                                        std::get_if<std::string>(
                                            &property.second);
                                    if (value == nullptr)
                                    {
                                        BMCWEB_LOG_ERROR("Null value returned "
                                                         "for protocol type");
                                        messages::internalError(aResp->res);
                                        return;
                                    }
                                    aResp->res.jsonValue["EndpointProtocol"] =
                                        redfish::port_utils::getPortProtocol(
                                            *value);
                                }
                            }
                        },
                        object3.front().first, objectPathToGetPortData,
                        "org.freedesktop.DBus.Properties", "GetAll",
                        "xyz.openbmc_project.Inventory.Decorator.PortInfo");
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetObject",
                objectPathToGetPortData,
                std::array<std::string, 1>(
                    {"xyz.openbmc_project.Inventory.Item.Port"}));
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/associated_port",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Get all endpoint connected port info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       portPath    D-Bus object to query.
 * @param[in]       fabricId    Fabric Id.
 * @param[in]       switchId    Switch Id.
 */
inline void getConnectedPortsLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::vector<std::string>& portPaths, const std::string& fabricId,
    const std::string& switchPath)
{
    dbus::utility::async_method_call(
        [aResp, portPaths, fabricId,
         switchPath](const boost::system::error_code& ec,
                     const std::vector<std::string>& objects) {
            if (ec)
            {
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json& linksConnectedPortsArray =
                aResp->res.jsonValue["Links"]["ConnectedPorts"];

            sdbusplus::message::object_path objPath(switchPath);
            const std::string& switchId = objPath.filename();
            // Add port link if exists in switch ports
            for (const std::string& portPath : portPaths)
            {
                if (std::find(objects.begin(), objects.end(), portPath) !=
                    objects.end())
                {
                    sdbusplus::message::object_path portObjPath(portPath);
                    const std::string& portId = portObjPath.filename();
                    {
                        boost::urls::url portURI = boost::urls::format(
                            "/redfish/v1/Fabrics/{}/Switches/{}/Ports/{}",
                            fabricId, switchId, portId);
                        linksConnectedPortsArray.push_back(
                            {{"@odata.id", portURI}});
                    }
                }
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths", switchPath, 0,
        std::array<const char*, 1>{"xyz.openbmc_project.Inventory.Item.Port"});
}

/**
 * @brief Get all endpoint zone info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   aResp           Async HTTP response.
 * @param[in]       endpointPath    D-Bus object to query.
 * @param[in]       fabricId        Fabric Id
 */
inline void getEndpointZoneData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                                const std::string& endpointPath,
                                const std::string& fabricId)
{
    // Get connected zone link
    dbus::utility::async_method_call(
        [aResp, fabricId](const boost::system::error_code& ec,
                          std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no zones = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            nlohmann::json& linksZonesArray =
                aResp->res.jsonValue["Links"]["Zones"];
            linksZonesArray = nlohmann::json::array();
            std::string zoneURI;
            for (const std::string& zonePath : *data)
            {
                // Get subtree for switchPath link path
                sdbusplus::message::object_path dbusObjPath(zonePath);
                const std::string& zoneId = dbusObjPath.filename();
                if (zonePath.find(fabricId) != std::string::npos)
                {
                    zoneURI = "/redfish/v1/Fabrics/" + fabricId + "/Zones/";
                }
                zoneURI += zoneId;
                linksZonesArray.push_back({{"@odata.id", zoneURI}});
            }
        },
        "xyz.openbmc_project.ObjectMapper", endpointPath + "/zone",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Get all endpoint port info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   aResp           Async HTTP response.
 * @param[in]       endpointPath    D-Bus object to query.
 * @param[in]       processorPath   D-Bus object to query.
 * @param[in]       fabricId        Fabric Id
 */
inline void getEndpointPortData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                                const std::string& endpointPath,
                                const std::string& processorPath,
                                const std::string& fabricId)
{
    // Endpoint protocol
    dbus::utility::async_method_call(
        [aResp, processorPath,
         fabricId](const boost::system::error_code& ec,
                   std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no endpoint port = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            for (const std::string& portPath : *data)
            {
                // Get port protocol data
                getPortData(aResp, portPath);
            }
            const std::vector<std::string> portPaths = *data;
            // Get connected switches port links
            dbus::utility::async_method_call(
                [aResp, portPaths,
                 fabricId](const boost::system::error_code& ec1,
                           std::variant<std::vector<std::string>>& resp1) {
                    if (ec1)
                    {
                        return; // no switches = no failures
                    }
                    std::vector<std::string>* data1 =
                        std::get_if<std::vector<std::string>>(&resp1);
                    if (data1 == nullptr)
                    {
                        return;
                    }
                    nlohmann::json& linksConnectedPortsArray =
                        aResp->res.jsonValue["Links"]["ConnectedPorts"];
                    linksConnectedPortsArray = nlohmann::json::array();
                    std::sort(data1->begin(), data1->end());
                    for (const std::string& switchPath : *data1)
                    {
                        getConnectedPortsLinks(aResp, portPaths, fabricId,
                                               switchPath);
                    }
                },
                "xyz.openbmc_project.ObjectMapper",
                processorPath + "/all_switches",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Association", "endpoints");
        },
        "xyz.openbmc_project.ObjectMapper", endpointPath + "/connected_port",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Get all endpoint info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   aResp   Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 * @param[in]       fabricId    Fabric Id.
 */
inline void updateEndpointData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const std::string& objPath,
                               const std::string& fabricId)
{
    BMCWEB_LOG_DEBUG("Get Endpoint Data");
    dbus::utility::async_method_call(
        [aResp, objPath,
         fabricId](const boost::system::error_code& ec,
                   std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no entity link = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            for (const std::string& entityPath : *data)
            {
                // Get subtree for entity link parent path
                size_t separator = entityPath.rfind('/');
                if (separator == std::string::npos)
                {
                    BMCWEB_LOG_ERROR("Invalid entity link path");
                    continue;
                }
                std::string entityInventoryPath =
                    entityPath.substr(0, separator);
                // Get entity subtree
                dbus::utility::async_method_call(
                    [aResp, objPath, entityPath,
                     fabricId](const boost::system::error_code& ec1,
                               const dbus::utility::GetSubTreeType& subtree) {
                        if (ec1)
                        {
                            messages::internalError(aResp->res);
                            return;
                        }
                        // Iterate over all retrieved ObjectPaths.
                        for (const std::pair<
                                 std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                                 object : subtree)
                        {
                            // Filter entity link object
                            if (object.first != entityPath)
                            {
                                continue;
                            }
                            const std::vector<std::pair<
                                std::string, std::vector<std::string>>>&
                                connectionNames = object.second;
                            if (connectionNames.empty())
                            {
                                BMCWEB_LOG_ERROR("Got 0 Connection names");
                                continue;
                            }

                            for (const auto& connectionName : connectionNames)
                            {
                                const std::vector<std::string>& interfaces =
                                    connectionName.second;
                                const std::string acceleratorInterface =
                                    "xyz.openbmc_project.Inventory.Item."
                                    "Accelerator";
                                if (std::find(interfaces.begin(),
                                              interfaces.end(),
                                              acceleratorInterface) !=
                                    interfaces.end())
                                {
                                    std::string servName = connectionName.first;
                                    sdbusplus::message::object_path objectPath(
                                        entityPath);
                                    const std::string& entityLink =
                                        "/redfish/v1/Systems/" +
                                        std::string(
                                            BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                        "/Processors/" + objectPath.filename();
                                    // Get processor PCIe device data
                                    getEndpointData(aResp, entityPath,
                                                    entityLink, servName);
                                    // Get port endpoint data
                                    getEndpointPortData(aResp, objPath,
                                                        entityPath, fabricId);
                                    // Get zone links
                                    getEndpointZoneData(aResp, objPath,
                                                        fabricId);
                                }
                                const std::string switchInterface =
                                    "xyz.openbmc_project.Inventory.Item.Switch";

                                if (std::find(
                                        interfaces.begin(), interfaces.end(),
                                        switchInterface) != interfaces.end())
                                {
                                    BMCWEB_LOG_DEBUG("Item type switch ");
                                    std::string servName = connectionName.first;

                                    sdbusplus::message::object_path objectPath(
                                        entityPath);
                                    std::string entityName =
                                        objectPath.filename();
                                    // get switch type endpint
                                    dbus::utility::async_method_call(
                                        [aResp, objPath, entityPath, entityName,
                                         servName, fabricId](
                                            const boost::system::error_code&
                                                ec2,
                                            std::variant<std::vector<
                                                std::string>>& resp5) {
                                            if (ec2)
                                            {
                                                BMCWEB_LOG_ERROR(
                                                    "fabric not found for switch entity");
                                                return; // no processors
                                                        // identified for
                                                        // pcieslotpath
                                            }

                                            std::vector<std::string>* data2 =
                                                std::get_if<
                                                    std::vector<std::string>>(
                                                    &resp5);
                                            if (data2 == nullptr)
                                            {
                                                BMCWEB_LOG_ERROR(
                                                    "processor data null for pcieslot ");
                                                return;
                                            }

                                            std::string fabricName;
                                            for (const std::string& fabricPath :
                                                 *data2)
                                            {
                                                sdbusplus::message::object_path
                                                    dbusObjPath(fabricPath);
                                                fabricName =
                                                    dbusObjPath.filename();
                                            }
                                            std::string entityLink =
                                                "/redfish/v1/Fabrics/";
                                            entityLink += fabricName;
                                            entityLink += "/Switches/";
                                            entityLink += entityName;
                                            // Get processor/switch PCIe device
                                            // data
                                            getEndpointData(aResp, entityPath,
                                                            entityLink,
                                                            servName);
                                            // Get port endpoint data
                                            getEndpointPortData(aResp, objPath,
                                                                entityPath,
                                                                fabricId);
                                            // Get zone links
                                            getEndpointZoneData(aResp, objPath,
                                                                fabricId);
                                        },
                                        "xyz.openbmc_project.ObjectMapper",
                                        entityPath + "/fabrics",
                                        "org.freedesktop.DBus.Properties",
                                        "Get",
                                        "xyz.openbmc_project.Association",
                                        "endpoints");
                                }
                                const std::string cpuInterface =
                                    "xyz.openbmc_project.Inventory.Item.Cpu";
                                if (std::find(interfaces.begin(),
                                              interfaces.end(), cpuInterface) !=
                                    interfaces.end())
                                {
                                    std::string servName = connectionName.first;
                                    sdbusplus::message::object_path objectPath(
                                        entityPath);
                                    const std::string& entityLink =
                                        "/redfish/v1/Systems/" +
                                        std::string(
                                            BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                        "/Processors/" + objectPath.filename();
                                    // Add EntityLink
                                    nlohmann::json& connectedEntitiesArray =
                                        aResp->res
                                            .jsonValue["ConnectedEntities"];
                                    connectedEntitiesArray.push_back(
                                        {{"EntityType", "Processor"},
                                         {"EntityLink",
                                          {{"@odata.id", entityLink}}}});

                                    // Get port endpoint data
                                    getEndpointPortData(aResp, objPath,
                                                        entityPath, fabricId);
                                    // Get zone links
                                    getEndpointZoneData(aResp, objPath,
                                                        fabricId);

                                    // Update processor health
                                    getProcessorEndpointHealth(aResp, servName,
                                                               entityPath);
                                }
                            }
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                    entityInventoryPath, 0, std::array<const char*, 0>());
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/entity_link",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * Endpoint override class for delivering Endpoint Schema
 */
inline void requestRoutesEndpoint(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Endpoints/<str>/")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& fabricId, const std::string& endpointId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                dbus::utility::async_method_call(
                    [asyncResp, fabricId,
                     endpointId](const boost::system::error_code& ec,
                                 const std::vector<std::string>& objects) {
                        if (ec)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        for (const std::string& object : objects)
                        {
                            // Get the fabricId object
                            if (!object.ends_with(fabricId))
                            {
                                continue;
                            }
                            dbus::utility::async_method_call(
                                [asyncResp, fabricId, endpointId](
                                    const boost::system::error_code& ec1,
                                    const dbus::utility::GetSubTreeType&
                                        subtree) {
                                    if (ec1)
                                    {
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }
                                    // Iterate over all retrieved ObjectPaths.
                                    for (const std::pair<
                                             std::string,
                                             std::vector<std::pair<
                                                 std::string,
                                                 std::vector<std::string>>>>&
                                             object1 : subtree)
                                    {
                                        // Get the endpointId object
                                        const std::string& path = object1.first;
                                        sdbusplus::message::object_path objPath(
                                            path);
                                        if (objPath.filename() != endpointId)
                                        {
                                            continue;
                                        }
                                        asyncResp->res
                                            .jsonValue["@odata.type"] =
                                            "#Endpoint.v1_6_0.Endpoint";
                                        asyncResp->res.jsonValue["@odata.id"] =
                                            std::string("/redfish/v1/Fabrics/")
                                                .append(fabricId)
                                                .append("/Endpoints/")
                                                .append(endpointId);
                                        asyncResp->res.jsonValue["Id"] =
                                            endpointId;
                                        asyncResp->res.jsonValue["Name"] =
                                            endpointId + " Endpoint Resource";
                                        nlohmann::json& connectedEntitiesArray =
                                            asyncResp->res
                                                .jsonValue["ConnectedEntities"];
                                        connectedEntitiesArray =
                                            nlohmann::json::array();
                                        updateEndpointData(asyncResp, path,
                                                           fabricId);
                                        return;
                                    }
                                    // Couldn't find an object with that name.
                                    // Return an error
                                    messages::resourceNotFound(
                                        asyncResp->res,
                                        "#Endpoint.v1_6_0.Endpoint",
                                        endpointId);
                                },
                                "xyz.openbmc_project.ObjectMapper",
                                "/xyz/openbmc_project/object_mapper",
                                "xyz.openbmc_project.ObjectMapper",
                                "GetSubTree", object, 0,
                                std::array<const char*, 1>{
                                    "xyz.openbmc_project.Inventory.Item."
                                    "Endpoint"});
                            return;
                        }
                        // Couldn't find an object with that name. Return an
                        // error
                        messages::resourceNotFound(
                            asyncResp->res, "#Fabric.v1_2_0.Fabric", fabricId);
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                    "/xyz/openbmc_project/inventory", 0,
                    std::array<const char*, 1>{
                        "xyz.openbmc_project.Inventory.Item.Fabric"});
            });
}

/**
 * @brief Handle CDR error count callback for a single lane.
 *
 * @param[in,out]   asyncResp       Async HTTP response.
 * @param[in]       remainingCount   Shared pointer to remaining lane count.
 * @param[in]       cdrErrors       Shared pointer to CDR errors vector.
 * @param[in]       laneIndex       Index of the lane.
 * @param[in]       totalLanes      Total number of lanes.
 * @param[in]       ec2             Error code from property get.
 * @param[in]       cdrErrorCount   CDR error count value.
 */
inline void handleLaneCDRErrorCallback(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<size_t>& remainingCount,
    const std::shared_ptr<std::vector<uint64_t>>& cdrErrors, size_t laneIndex,
    size_t totalLanes, const boost::system::error_code& ec2,
    const uint64_t& cdrErrorCount)
{
    if (!ec2 && laneIndex < totalLanes)
    {
        (*cdrErrors)[laneIndex] = cdrErrorCount;
    }
    else if (ec2)
    {
        BMCWEB_LOG_DEBUG("Failed to get CDRErrorCount for lane {}: {}",
                         laneIndex, ec2.message());
    }

    (*remainingCount)--;
    if (*remainingCount == 0)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["CDRErrorsPerLane"] =
            *cdrErrors;
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
            "#NvidiaPortMetrics.v1_8_0.NvidiaPortMetrics";
    }
}

/**
 * @brief Process lane paths and fetch CDR errors for each lane.
 *
 * @param[in,out]   asyncResp       Async HTTP response.
 * @param[in]       service         D-Bus service name for lane objects.
 * @param[in]       lanePaths       Vector of lane paths.
 */
inline void processLanePaths(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::vector<std::string>& lanePaths)
{
    size_t totalLanes = lanePaths.size();
    auto remainingCount = std::make_shared<size_t>(totalLanes);
    auto cdrErrors = std::make_shared<std::vector<uint64_t>>(totalLanes, 0);

    for (const std::string& lanePath : lanePaths)
    {
        sdbusplus::message::object_path laneObjPath(lanePath);
        std::string laneIdStr = laneObjPath.filename();
        size_t laneIndex = 0;
        try
        {
            laneIndex = std::stoul(laneIdStr);
        }
        catch (const std::exception& e)
        {
            BMCWEB_LOG_ERROR("Failed to parse lane index from {}: {}", lanePath,
                             e.what());
            (*remainingCount)--;
            continue;
        }

        dbus::utility::getProperty<uint64_t>(
            service, lanePath, "xyz.openbmc_project.PCIe.PCIeLaneError",
            "CDRErrorCount",
            [asyncResp, remainingCount, cdrErrors, laneIndex,
             totalLanes](const boost::system::error_code& ec2,
                         const uint64_t& cdrErrorCount) {
                handleLaneCDRErrorCallback(asyncResp, remainingCount, cdrErrors,
                                           laneIndex, totalLanes, ec2,
                                           cdrErrorCount);
            });
    }
}

/**
 * @brief Fetch CDR errors from associated lane objects and populate array.
 *
 * This function retrieves Per-lane telemetry data for Port Metrics
 */
inline void getPerLaneTelemetryData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& portObjPath)
{
    BMCWEB_LOG_DEBUG("Fetching Per-lane telemetry data for {}", portObjPath);

    asyncResp->res.jsonValue["Oem"]["Nvidia"]["CDRErrorsPerLane"] =
        std::vector<uint64_t>(0);
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
        "#NvidiaPortMetrics.v1_8_0.NvidiaPortMetrics";

    dbus::utility::async_method_call(
        [asyncResp, service,
         portObjPath](const boost::system::error_code& ec,
                      std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("No associated_lanes for port {}: {}",
                                 portObjPath, ec.message());
                return;
            }

            std::vector<std::string>* lanePaths =
                std::get_if<std::vector<std::string>>(&resp);
            if (lanePaths == nullptr || lanePaths->empty())
            {
                BMCWEB_LOG_DEBUG("No lane paths found for port {}",
                                 portObjPath);
                return;
            }

            processLanePaths(asyncResp, service, *lanePaths);
        },
        "xyz.openbmc_project.ObjectMapper", portObjPath + "/associated_lanes",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Structure to hold PCIeMetrics property list.
 */
struct PCIeMetricsPropertyList
{
    const uint64_t* outboundReadPktCount = nullptr;
    const uint64_t* outboundWritePktCount = nullptr;
    const uint64_t* outboundTLPCount = nullptr;
    const uint64_t* outboundReadTransfer = nullptr;
    const uint64_t* outboundWriteTransfer = nullptr;
    const uint64_t* outboundTLPsTransfer = nullptr;
    const uint64_t* reqDroppedTag = nullptr;
    const uint64_t* reqDroppedCreditCompletion = nullptr;
    const uint64_t* reqDroppedNonPostCredit = nullptr;
};

/**
 * @brief Structure to hold OEM PCIeMetrics property list (inbound counters).
 */
struct OemPCIeMetricsPropertyList
{
    const uint64_t* inboundTLPCount = nullptr;
    const uint64_t* inboundTLPsTransfer = nullptr;
};

/**
 * @brief Structure to hold PCIeErrors property list.
 */
struct PCIeErrorsPropertyList
{
    const double* receiverErrorCount = nullptr;
    const double* ceCount = nullptr;
    const double* nonfeCount = nullptr;
    const double* feCount = nullptr;
    const double* l0ToRecoveryCount = nullptr;
    const double* replayCount = nullptr;
    const double* replayRolloverCount = nullptr;
    const double* nakSentCount = nullptr;
    const double* nakReceivedCount = nullptr;
    const double* unsupportedRequestCount = nullptr;
    const double* badTLPCount = nullptr;
    const double* fcTimeoutErrors = nullptr;
};

/**
 * @brief Structure to hold OEM properties property list for Fabrics Port
 * Metrics.
 */
struct FabricsPortMetricsOemPropertyList
{
    const uint64_t* rxNoProtocolBytes = nullptr;
    const uint64_t* txNoProtocolBytes = nullptr;
    const uint32_t* dataCRCCount = nullptr;
    const uint32_t* flitCRCCount = nullptr;
    const uint32_t* recoveryCount = nullptr;
    const uint32_t* replayErrorsCount = nullptr;
    const uint16_t* runtimeError = nullptr;
    const uint16_t* trainingError = nullptr;
    const uint64_t* malformedPkts = nullptr;
    const uint64_t* vl15DroppedPkts = nullptr;
    const uint64_t* vl15TXPkts = nullptr;
    const uint64_t* vl15TXData = nullptr;
    const uint64_t* mtuDiscard = nullptr;
    const double* bitErrorRate = nullptr;
    const uint64_t* linkErrorRecoveryCounter = nullptr;
    const uint64_t* linkDownCount = nullptr;
    const uint64_t* rxRemotePhysicalErrorPkts = nullptr;
    const uint64_t* rxSwitchRelayErrorPkts = nullptr;
    const uint64_t* qp1DroppedPkts = nullptr;
    const uint64_t* txWait = nullptr;
    const uint64_t* effectiveError = nullptr;
    const double* effectiveBER = nullptr;
    const uint64_t* symbolErrors = nullptr;
    const double* totalRawBER = nullptr;
    const uint64_t* totalRawError = nullptr;
    const uint64_t* intentionalLinkDownCount = nullptr;
    const uint64_t* unintentionalLinkDownCount = nullptr;
    const double* trainingSequenceErrorCount = nullptr;
    const double* framingErrorCount = nullptr;
    const double* linkDownedCount = nullptr;
    const double* dllpCRCErrorCount = nullptr;
};

/**
 * @brief Populate root-level PCIeMetrics.
 *
 * @param[in,out]   asyncResp       Async HTTP response.
 * @param[in]       propertyList    PCIeMetrics property list.
 */
inline void populatePCIeMetrics(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const PCIeMetricsPropertyList& propertyList)
{
    if (propertyList.outboundReadPktCount != nullptr)
    {
        asyncResp->res.jsonValue["PCIeMetrics"]["OutboundReadTLPCount"] =
            *propertyList.outboundReadPktCount;
    }
    if (propertyList.outboundWritePktCount != nullptr)
    {
        asyncResp->res.jsonValue["PCIeMetrics"]["OutboundWriteTLPCount"] =
            *propertyList.outboundWritePktCount;
    }
    if (propertyList.outboundTLPCount != nullptr)
    {
        asyncResp->res.jsonValue["PCIeMetrics"]["OutboundCompletionTLPCount"] =
            *propertyList.outboundTLPCount;
    }
    if (propertyList.outboundReadTransfer != nullptr)
    {
        asyncResp->res.jsonValue["PCIeMetrics"]["OutboundReadTLPBytes"] =
            *propertyList.outboundReadTransfer;
    }
    if (propertyList.outboundWriteTransfer != nullptr)
    {
        asyncResp->res.jsonValue["PCIeMetrics"]["OutboundWriteTLPBytes"] =
            *propertyList.outboundWriteTransfer;
    }
    if (propertyList.outboundTLPsTransfer != nullptr)
    {
        asyncResp->res.jsonValue["PCIeMetrics"]["OutboundCompletionTLPBytes"] =
            *propertyList.outboundTLPsTransfer;
    }
    if (propertyList.reqDroppedTag != nullptr)
    {
        asyncResp->res.jsonValue["PCIeMetrics"]["TagUnavailabilityDrops"] =
            *propertyList.reqDroppedTag;
    }
    if (propertyList.reqDroppedCreditCompletion != nullptr)
    {
        asyncResp->res
            .jsonValue["PCIeMetrics"]["CompletionCreditExhaustionDrops"] =
            *propertyList.reqDroppedCreditCompletion;
    }
    if (propertyList.reqDroppedNonPostCredit != nullptr)
    {
        asyncResp->res.jsonValue["PCIeMetrics"]["NPCreditExhaustionDrops"] =
            *propertyList.reqDroppedNonPostCredit;
    }
}

/**
 * @brief Populate OEM PCIeMetrics (inbound counters).
 *
 * @param[in,out]   asyncResp       Async HTTP response.
 * @param[in]       propertyList    OEM PCIeMetrics property list.
 * @return          True if any inbound metrics were set.
 */
inline bool populateOemPCIeMetrics(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const OemPCIeMetricsPropertyList& propertyList)
{
    bool hasInboundMetrics = false;
    if (propertyList.inboundTLPCount != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["PCIeMetrics"]
                                ["InboundCompletionTLPCount"] =
            *propertyList.inboundTLPCount;
        hasInboundMetrics = true;
    }
    if (propertyList.inboundTLPsTransfer != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["PCIeMetrics"]
                                ["InboundCompletionTLPBytes"] =
            *propertyList.inboundTLPsTransfer;
        hasInboundMetrics = true;
    }
    return hasInboundMetrics;
}

/**
 * @brief Populate PCIeErrors metrics.
 *
 * @param[in,out]   asyncResp       Async HTTP response.
 * @param[in]       propertyList    PCIeErrors property list.
 */
inline void populatePCIeErrors(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const PCIeErrorsPropertyList& propertyList)
{
    if (propertyList.receiverErrorCount != nullptr)
    {
        asyncResp->res.jsonValue["RXErrors"] =
            nvidia::nsm_utils::tryConvertToInt64(
                *propertyList.receiverErrorCount);
    }
    if (propertyList.ceCount != nullptr)
    {
        asyncResp->res.jsonValue["PCIeErrors"]["CorrectableErrorCount"] =
            nvidia::nsm_utils::tryConvertToInt64(*propertyList.ceCount);
    }
    if (propertyList.nonfeCount != nullptr)
    {
        asyncResp->res.jsonValue["PCIeErrors"]["NonFatalErrorCount"] =
            nvidia::nsm_utils::tryConvertToInt64(*propertyList.nonfeCount);
    }
    if (propertyList.feCount != nullptr)
    {
        asyncResp->res.jsonValue["PCIeErrors"]["FatalErrorCount"] =
            nvidia::nsm_utils::tryConvertToInt64(*propertyList.feCount);
    }
    if (propertyList.l0ToRecoveryCount != nullptr)
    {
        asyncResp->res.jsonValue["PCIeErrors"]["L0ToRecoveryCount"] =
            nvidia::nsm_utils::tryConvertToInt64(
                *propertyList.l0ToRecoveryCount);
    }
    if (propertyList.replayCount != nullptr)
    {
        asyncResp->res.jsonValue["PCIeErrors"]["ReplayCount"] =
            nvidia::nsm_utils::tryConvertToInt64(*propertyList.replayCount);
    }
    if (propertyList.replayRolloverCount != nullptr)
    {
        asyncResp->res.jsonValue["PCIeErrors"]["ReplayRolloverCount"] =
            nvidia::nsm_utils::tryConvertToInt64(
                *propertyList.replayRolloverCount);
    }
    if (propertyList.nakSentCount != nullptr)
    {
        asyncResp->res.jsonValue["PCIeErrors"]["NAKSentCount"] =
            nvidia::nsm_utils::tryConvertToInt64(*propertyList.nakSentCount);
    }
    if (propertyList.nakReceivedCount != nullptr)
    {
        asyncResp->res.jsonValue["PCIeErrors"]["NAKReceivedCount"] =
            nvidia::nsm_utils::tryConvertToInt64(
                *propertyList.nakReceivedCount);
    }
    if (propertyList.unsupportedRequestCount != nullptr)
    {
        asyncResp->res.jsonValue["PCIeErrors"]["UnsupportedRequestCount"] =
            nvidia::nsm_utils::tryConvertToInt64(
                *propertyList.unsupportedRequestCount);
    }
    if (propertyList.badTLPCount != nullptr)
    {
        asyncResp->res.jsonValue["PCIeErrors"]["BadTLPCount"] =
            nvidia::nsm_utils::tryConvertToInt64(*propertyList.badTLPCount);
    }
    if (propertyList.fcTimeoutErrors != nullptr)
    {
        asyncResp->res.jsonValue["PCIeErrors"]["FlowControlTimeoutErrors"] =
            nvidia::nsm_utils::tryConvertToInt64(*propertyList.fcTimeoutErrors);
    }
}

/**
 * @brief Populate remaining OEM NVIDIA properties.
 *
 * @param[in,out]   asyncResp       Async HTTP response.
 * @param[in]       propertyList    OEM NVIDIA properties property list.
 */
inline void populateOemNvidiaProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const FabricsPortMetricsOemPropertyList& propertyList)
{
    if (propertyList.rxNoProtocolBytes != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["RXNoProtocolBytes"] =
            *propertyList.rxNoProtocolBytes;
    }
    if (propertyList.txNoProtocolBytes != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["TXNoProtocolBytes"] =
            *propertyList.txNoProtocolBytes;
    }
    if (propertyList.dataCRCCount != nullptr)
    {
        asyncResp->res
            .jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]["DataCRCCount"] =
            *propertyList.dataCRCCount;
    }
    if (propertyList.flitCRCCount != nullptr)
    {
        asyncResp->res
            .jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]["FlitCRCCount"] =
            *propertyList.flitCRCCount;
    }
    if (propertyList.recoveryCount != nullptr)
    {
        asyncResp->res
            .jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]["RecoveryCount"] =
            *propertyList.recoveryCount;
    }
    if (propertyList.replayErrorsCount != nullptr)
    {
        asyncResp->res
            .jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]["ReplayCount"] =
            *propertyList.replayErrorsCount;
    }
    if (propertyList.runtimeError != nullptr)
    {
        asyncResp->res
            .jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]["RuntimeError"] =
            (*propertyList.runtimeError != 0);
    }
    if (propertyList.trainingError != nullptr)
    {
        asyncResp->res
            .jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]["TrainingError"] =
            (*propertyList.trainingError != 0);
    }
    if (propertyList.malformedPkts != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["MalformedPackets"] =
            *propertyList.malformedPkts;
    }
    if (propertyList.vl15DroppedPkts != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["VL15Dropped"] =
            *propertyList.vl15DroppedPkts;
    }
    if (propertyList.vl15TXPkts != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["VL15TXPackets"] =
            *propertyList.vl15TXPkts;
    }
    if (propertyList.vl15TXData != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["VL15TXBytes"] =
            *propertyList.vl15TXData;
    }
    if (propertyList.mtuDiscard != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["NeighborMTUDiscards"] =
            *propertyList.mtuDiscard;
    }
    if (propertyList.bitErrorRate != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["BitErrorRate"] =
            *propertyList.bitErrorRate;
    }
    if (propertyList.linkErrorRecoveryCounter != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["LinkErrorRecoveryCount"] =
            *propertyList.linkErrorRecoveryCounter;
    }
    if (propertyList.linkDownCount != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["LinkDownedCount"] =
            *propertyList.linkDownCount;
    }
    if (propertyList.rxRemotePhysicalErrorPkts != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["RXRemotePhysicalErrors"] =
            *propertyList.rxRemotePhysicalErrorPkts;
    }
    if (propertyList.rxSwitchRelayErrorPkts != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["RXSwitchRelayErrors"] =
            *propertyList.rxSwitchRelayErrorPkts;
    }
    if (propertyList.qp1DroppedPkts != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["QP1Dropped"] =
            *propertyList.qp1DroppedPkts;
    }
    if (propertyList.txWait != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["TXWait"] =
            *propertyList.txWait;
    }
    if (propertyList.effectiveError != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["EffectiveError"] =
            *propertyList.effectiveError;
    }
    if (propertyList.effectiveBER != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["EffectiveBER"] =
            *propertyList.effectiveBER;
    }
    if (propertyList.symbolErrors != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["SymbolErrors"] =
            *propertyList.symbolErrors;
    }
    if (propertyList.totalRawBER != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["TotalRawBER"] =
            *propertyList.totalRawBER;
    }
    if (propertyList.totalRawError != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["TotalRawError"] =
            *propertyList.totalRawError;
    }
    if (propertyList.intentionalLinkDownCount != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["IntentionalLinkDownCount"] =
            *propertyList.intentionalLinkDownCount;
    }
    if (propertyList.unintentionalLinkDownCount != nullptr)
    {
        asyncResp->res
            .jsonValue["Oem"]["Nvidia"]["UnintentionalLinkDownCount"] =
            *propertyList.unintentionalLinkDownCount;
    }
    if (propertyList.trainingSequenceErrorCount != nullptr)
    {
        asyncResp->res
            .jsonValue["Oem"]["Nvidia"]["TrainingSequenceErrorCount"] =
            nvidia::nsm_utils::tryConvertToInt64(
                *propertyList.trainingSequenceErrorCount);
    }
    if (propertyList.framingErrorCount != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["FramingErrorCount"] =
            nvidia::nsm_utils::tryConvertToInt64(
                *propertyList.framingErrorCount);
    }
    if (propertyList.linkDownedCount != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["LinkDownedCount"] =
            nvidia::nsm_utils::tryConvertToInt64(*propertyList.linkDownedCount);
    }
    if (propertyList.dllpCRCErrorCount != nullptr)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["DLLPCRCErrorCount"] =
            nvidia::nsm_utils::tryConvertToInt64(
                *propertyList.dllpCRCErrorCount);
    }
}

/**
 * @brief Get all port info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       service     Service.
 * @param[in]       objPath     D-Bus object to query.
 * @param[in]       fabricId    Fabric Id.
 * @param[in]       switchId    Switch Id.
 * @param[in]       portId      Port Id.
 */
inline void getFabricsPortMetricsData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objPath,
    const std::string& fabricId, const std::string& switchId,
    const std::string& portId)
{
    BMCWEB_LOG_DEBUG("Access port metrics data");

    boost::urls::url portMetricsURI = boost::urls::format(
        "/redfish/v1/Fabrics/{}/Switches/{}/Ports/{}/Metrics", fabricId,
        switchId, portId);
    asyncResp->res.jsonValue = {
        {"@odata.type", "#PortMetrics.v1_8_0.PortMetrics"},
        {"@odata.id", portMetricsURI},
        {"Name", portId + " Port Metrics"},
        {"Id", "Metrics"}};

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        getPerLaneTelemetryData(asyncResp, service, objPath);
    }

    dbus::utility::getAllProperties(
        service, objPath, "",
        [asyncResp](const boost::system::error_code ec,
                    const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(asyncResp->res);
                return;
            }

            const uint64_t* txBytes = nullptr;
            const uint64_t* rxBytes = nullptr;
            const uint64_t* rxErrors = nullptr;
            const uint64_t* rxPkts = nullptr;
            const uint64_t* txPkts = nullptr;
            const uint64_t* rxMulticastPkts = nullptr;
            const uint64_t* txMulticastPkts = nullptr;
            const uint64_t* rxUnicastPkts = nullptr;
            const uint64_t* txUnicastPkts = nullptr;
            const uint64_t* txDiscardPkts = nullptr;

            PCIeMetricsPropertyList pcieMetricsPropertyList;
            FabricsPortMetricsOemPropertyList oemPropertyList;
            OemPCIeMetricsPropertyList oemPCIePropertyList;
            PCIeErrorsPropertyList pcieErrorsPropertyList;

            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), properties, "TXBytes",
                txBytes, "RXBytes", rxBytes, "RXErrors", rxErrors, "RXPkts",
                rxPkts, "TXPkts", txPkts, "RXMulticastPkts", rxMulticastPkts,
                "TXMulticastPkts", txMulticastPkts, "RXUnicastPkts",
                rxUnicastPkts, "TXUnicastPkts", txUnicastPkts, "TXDiscardPkts",
                txDiscardPkts, "RXNoProtocolBytes",
                oemPropertyList.rxNoProtocolBytes, "TXNoProtocolBytes",
                oemPropertyList.txNoProtocolBytes, "DataCRCCount",
                oemPropertyList.dataCRCCount, "FlitCRCCount",
                oemPropertyList.flitCRCCount, "RecoveryCount",
                oemPropertyList.recoveryCount, "ReplayErrorsCount",
                oemPropertyList.replayErrorsCount, "RuntimeError",
                oemPropertyList.runtimeError, "TrainingError",
                oemPropertyList.trainingError, "MalformedPkts",
                oemPropertyList.malformedPkts, "VL15DroppedPkts",
                oemPropertyList.vl15DroppedPkts, "VL15TXPkts",
                oemPropertyList.vl15TXPkts, "VL15TXData",
                oemPropertyList.vl15TXData, "MTUDiscard",
                oemPropertyList.mtuDiscard, "BitErrorRate",
                oemPropertyList.bitErrorRate, "LinkErrorRecoveryCounter",
                oemPropertyList.linkErrorRecoveryCounter, "LinkDownCount",
                oemPropertyList.linkDownCount, "RXRemotePhysicalErrorPkts",
                oemPropertyList.rxRemotePhysicalErrorPkts,
                "RXSwitchRelayErrorPkts",
                oemPropertyList.rxSwitchRelayErrorPkts, "QP1DroppedPkts",
                oemPropertyList.qp1DroppedPkts, "TXWait",
                oemPropertyList.txWait, "EffectiveError",
                oemPropertyList.effectiveError, "EffectiveBER",
                oemPropertyList.effectiveBER, "SymbolErrors",
                oemPropertyList.symbolErrors, "TotalRawBER",
                oemPropertyList.totalRawBER, "TotalRawError",
                oemPropertyList.totalRawError, "IntentionalLinkDownCount",
                oemPropertyList.intentionalLinkDownCount,
                "UnintentionalLinkDownCount",
                oemPropertyList.unintentionalLinkDownCount,
                "TrainingSequenceErrorCount",
                oemPropertyList.trainingSequenceErrorCount, "FramingErrorCount",
                oemPropertyList.framingErrorCount, "LinkDownedCount",
                oemPropertyList.linkDownedCount, "DLLPCRCErrorCount",
                oemPropertyList.dllpCRCErrorCount, "OutboundReadPktCount",
                pcieMetricsPropertyList.outboundReadPktCount,
                "OutboundWritePktCount",
                pcieMetricsPropertyList.outboundWritePktCount,
                "OutboundTLPCount", pcieMetricsPropertyList.outboundTLPCount,
                "OutboundReadTransfer",
                pcieMetricsPropertyList.outboundReadTransfer,
                "OutboundWriteTransfer",
                pcieMetricsPropertyList.outboundWriteTransfer,
                "OutboundTLPsTransfer",
                pcieMetricsPropertyList.outboundTLPsTransfer, "ReqDroppedTag",
                pcieMetricsPropertyList.reqDroppedTag,
                "ReqDroppedCreditCompletion",
                pcieMetricsPropertyList.reqDroppedCreditCompletion,
                "ReqDroppedNonPostCredit",
                pcieMetricsPropertyList.reqDroppedNonPostCredit,
                "InboundTLPCount", oemPCIePropertyList.inboundTLPCount,
                "InboundTLPsTransfer", oemPCIePropertyList.inboundTLPsTransfer,
                "ceCount", pcieErrorsPropertyList.ceCount, "nonfeCount",
                pcieErrorsPropertyList.nonfeCount, "feCount",
                pcieErrorsPropertyList.feCount, "L0ToRecoveryCount",
                pcieErrorsPropertyList.l0ToRecoveryCount, "ReplayCount",
                pcieErrorsPropertyList.replayCount, "ReplayRolloverCount",
                pcieErrorsPropertyList.replayRolloverCount, "NAKSentCount",
                pcieErrorsPropertyList.nakSentCount, "NAKReceivedCount",
                pcieErrorsPropertyList.nakReceivedCount,
                "UnsupportedRequestCount",
                pcieErrorsPropertyList.unsupportedRequestCount, "BadTLPCount",
                pcieErrorsPropertyList.badTLPCount, "ReceiverErrorCount",
                pcieErrorsPropertyList.receiverErrorCount, "FCTimeoutErrors",
                pcieErrorsPropertyList.fcTimeoutErrors);

            if (!success)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
            {
                asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaPortMetrics.v1_6_0.NvidiaNVLinkPortMetrics";
            }

            if (txBytes != nullptr)
            {
                asyncResp->res.jsonValue["TXBytes"] = *txBytes;
            }
            if (rxBytes != nullptr)
            {
                asyncResp->res.jsonValue["RXBytes"] = *rxBytes;
            }
            if (rxErrors != nullptr)
            {
                asyncResp->res.jsonValue["RXErrors"] = *rxErrors;
            }
            if (rxPkts != nullptr)
            {
                asyncResp->res.jsonValue["Networking"]["RXFrames"] = *rxPkts;
            }
            if (txPkts != nullptr)
            {
                asyncResp->res.jsonValue["Networking"]["TXFrames"] = *txPkts;
            }
            if (rxMulticastPkts != nullptr)
            {
                asyncResp->res.jsonValue["Networking"]["RXMulticastFrames"] =
                    *rxMulticastPkts;
            }
            if (txMulticastPkts != nullptr)
            {
                asyncResp->res.jsonValue["Networking"]["TXMulticastFrames"] =
                    *txMulticastPkts;
            }
            if (rxUnicastPkts != nullptr)
            {
                asyncResp->res.jsonValue["Networking"]["RXUnicastFrames"] =
                    *rxUnicastPkts;
            }
            if (txUnicastPkts != nullptr)
            {
                asyncResp->res.jsonValue["Networking"]["TXUnicastFrames"] =
                    *txUnicastPkts;
            }
            if (txDiscardPkts != nullptr)
            {
                asyncResp->res.jsonValue["Networking"]["TXDiscards"] =
                    *txDiscardPkts;
            }
            populatePCIeMetrics(asyncResp, pcieMetricsPropertyList);

            if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
            {
                populateOemNvidiaProperties(asyncResp, oemPropertyList);

                bool oemPCIeMetricFlag =
                    populateOemPCIeMetrics(asyncResp, oemPCIePropertyList);

                if (oemPCIeMetricFlag)
                {
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                        "#NvidiaPortMetrics.v1_8_0.NvidiaPortMetrics";
                }
            }

            populatePCIeErrors(asyncResp, pcieErrorsPropertyList);
        });
}

/**
 * Port Metrics override class for delivering Port Metrics Schema
 */
inline void requestRoutesPortMetrics(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/Fabrics/<str>/Switches/<str>/Ports/<str>/Metrics/")
        .privileges({{"Login"}})
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& fabricId,
                            const std::string& switchId,
                            const std::string& portId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            dbus::utility::async_method_call(
                [asyncResp{asyncResp}, fabricId, switchId,
                 portId](const boost::system::error_code& ec,
                         const std::vector<std::string>& objects) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("DBUS response error");
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (const std::string& fabricPath : objects)
                    {
                        // Get the fabricId object
                        if (!fabricPath.ends_with(fabricId))
                        {
                            continue;
                        }
                        dbus::utility::async_method_call(
                            [asyncResp, fabricId, switchId, portId](
                                const boost::system::error_code& ec1,
                                std::variant<std::vector<std::string>>& resp1) {
                                if (ec1)
                                {
                                    BMCWEB_LOG_ERROR("DBUS response error");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                std::vector<std::string>* data1 =
                                    std::get_if<std::vector<std::string>>(
                                        &resp1);
                                if (data1 == nullptr)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "DBUS response error while getting switches");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                for (const std::string& switchPath : *data1)
                                {
                                    // Get the switchId object
                                    if (!switchPath.ends_with(switchId))
                                    {
                                        continue;
                                    }

                                    dbus::utility::async_method_call(
                                        [asyncResp, fabricId, switchId, portId](
                                            const boost::system::error_code&
                                                ec2,
                                            std::variant<std::vector<
                                                std::string>>& resp2) {
                                            if (ec2)
                                            {
                                                BMCWEB_LOG_ERROR(
                                                    "DBUS response error");
                                                messages::internalError(
                                                    asyncResp->res);
                                                return;
                                            }
                                            std::vector<std::string>* data2 =
                                                std::get_if<
                                                    std::vector<std::string>>(
                                                    &resp2);
                                            if (data2 == nullptr)
                                            {
                                                BMCWEB_LOG_ERROR(
                                                    "DBUS response error while getting ports");
                                                messages::internalError(
                                                    asyncResp->res);
                                                return;
                                            }
                                            for (const std::string& portPath :
                                                 *data2)
                                            {
                                                // Get the portId object
                                                sdbusplus::message::object_path
                                                    pPath(portPath);
                                                if (pPath.filename() != portId)
                                                {
                                                    continue;
                                                }
                                                dbus::utility::async_method_call(
                                                    [asyncResp, fabricId,
                                                     switchId, portId,
                                                     portPath](
                                                        const boost::system::
                                                            error_code& ec3,
                                                        const std::vector<
                                                            std::pair<
                                                                std::string,
                                                                std::vector<
                                                                    std::
                                                                        string>>>&
                                                            object3) {
                                                        if (ec3)
                                                        {
                                                            // the path does not
                                                            // implement Item
                                                            // Switch interfaces
                                                            BMCWEB_LOG_DEBUG(
                                                                "No switch interface on {}",
                                                                portPath);
                                                            return;
                                                        }
                                                        boost::urls::url
                                                            portMetricsURI = boost::
                                                                urls::format(
                                                                    "/redfish/v1/Fabrics/{}/Switches/{}/Ports/{}/Metrics",
                                                                    fabricId,
                                                                    switchId,
                                                                    portId);
                                                        asyncResp->res
                                                            .jsonValue = {
                                                            {"@odata.type",
                                                             "#PortMetrics.v1_3_0.PortMetrics"},
                                                            {"@odata.id",
                                                             portMetricsURI},
                                                            {"Name",
                                                             portId +
                                                                 " Port Metrics"},
                                                            {"Id", portId}};

                                                        getFabricsPortMetricsData(
                                                            asyncResp,
                                                            object3.front()
                                                                .first,
                                                            portPath, fabricId,
                                                            switchId, portId);
                                                    },
                                                    "xyz.openbmc_project.ObjectMapper",
                                                    "/xyz/openbmc_project/object_mapper",
                                                    "xyz.openbmc_project.ObjectMapper",
                                                    "GetObject", portPath,
                                                    std::array<std::string, 1>(
                                                        {"xyz.openbmc_project.Inventory.Item.Port"}));
                                                return;
                                            }
                                            // Couldn't find an object with that
                                            // name. Return an error
                                            messages::resourceNotFound(
                                                asyncResp->res,
                                                "#Port.v1_0_0.Port", switchId);
                                        },
                                        "xyz.openbmc_project.ObjectMapper",
                                        switchPath + "/all_states",
                                        "org.freedesktop.DBus.Properties",
                                        "Get",
                                        "xyz.openbmc_project.Association",
                                        "endpoints");
                                    return;
                                }
                                // Couldn't find an object with that name.
                                // Return an error
                                messages::resourceNotFound(
                                    asyncResp->res, "#Switch.v1_8_0.Switch",
                                    switchId);
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            fabricPath + "/all_switches",
                            "org.freedesktop.DBus.Properties", "Get",
                            "xyz.openbmc_project.Association", "endpoints");
                        return;
                    }
                    // Couldn't find an object with that name. Return an error
                    messages::resourceNotFound(
                        asyncResp->res, "#Fabric.v1_2_0.Fabric", fabricId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                "/xyz/openbmc_project/inventory", 0,
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Inventory.Item.Fabric"});
        });
}

inline void requestRoutesSwitchPowerMode(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */

    BMCWEB_ROUTE(
        app, "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/PowerMode")
        .privileges(redfish::privileges::getSwitch)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& fabricId,
                            const std::string& switchId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            dbus::utility::async_method_call(
                [asyncResp, fabricId,
                 switchId](const boost::system::error_code& ec,
                           const std::vector<std::string>& objects) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error while getting fabrics");
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (const std::string& object : objects)
                    {
                        // Get the fabricId object
                        if (!object.ends_with(fabricId))
                        {
                            continue;
                        }
                        dbus::utility::async_method_call(
                            [asyncResp, fabricId, switchId](
                                const boost::system::error_code& ec1,
                                std::variant<std::vector<std::string>>& resp1) {
                                if (ec1)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "DBUS response error while getting switch on fabric");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                std::vector<std::string>* data1 =
                                    std::get_if<std::vector<std::string>>(
                                        &resp1);
                                if (data1 == nullptr)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "Null data response while getting switch on fabric");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                // Iterate over all retrieved ObjectPaths.
                                for (const std::string& path : *data1)
                                {
                                    sdbusplus::message::object_path objPath(
                                        path);
                                    if (objPath.filename() != switchId)
                                    {
                                        continue;
                                    }

                                    std::string switchPowerModeURI =
                                        "/redfish/v1/Fabrics/";
                                    switchPowerModeURI += fabricId;
                                    switchPowerModeURI += "/Switches/";
                                    switchPowerModeURI += switchId;
                                    switchPowerModeURI +=
                                        "/Oem/Nvidia/PowerMode";
                                    asyncResp->res.jsonValue["@odata.type"] =
                                        "#NvidiaSwitchPowerMode.v1_0_0.NvidiaSwitchPowerMode";
                                    asyncResp->res.jsonValue["@odata.id"] =
                                        switchPowerModeURI;
                                    asyncResp->res.jsonValue["Id"] =
                                        "PowerMode";
                                    asyncResp->res.jsonValue["Name"] =
                                        switchId + " PowerMode Resource";

                                    dbus::utility::async_method_call(
                                        [asyncResp,
                                         path](const boost::system::error_code&
                                                   ec2,
                                               const std::vector<std::pair<
                                                   std::string,
                                                   std::vector<std::string>>>&
                                                   object2) {
                                            if (ec2)
                                            {
                                                BMCWEB_LOG_ERROR(
                                                    "Dbus response error while getting service name for switch");
                                                messages::internalError(
                                                    asyncResp->res);
                                                return;
                                            }
                                            redfish::nvidia_fabric_utils::
                                                updateSwitchPowerModeData(
                                                    asyncResp,
                                                    object2.front().first,
                                                    path);
                                        },
                                        "xyz.openbmc_project.ObjectMapper",
                                        "/xyz/openbmc_project/object_mapper",
                                        "xyz.openbmc_project.ObjectMapper",
                                        "GetObject", path,
                                        std::array<const char*, 0>());

                                    return;
                                }
                                // Couldn't find an object with that name.
                                // Return an error
                                messages::resourceNotFound(
                                    asyncResp->res, "#Switch.v1_8_0.Switch",
                                    switchId);
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            object + "/all_switches",
                            "org.freedesktop.DBus.Properties", "Get",
                            "xyz.openbmc_project.Association", "endpoints");
                        return;
                    }
                    // Couldn't find an object with that name. Return an error
                    messages::resourceNotFound(
                        asyncResp->res, "#Fabric.v1_2_0.Fabric", fabricId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                "/xyz/openbmc_project/inventory", 0,
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Inventory.Item.Fabric"});
        });

    BMCWEB_ROUTE(
        app, "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/PowerMode")
        .privileges(redfish::privileges::patchSwitch)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& fabricId, const std::string& switchId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::optional<bool> l1PredictionModeEnabled;
                if (!redfish::json_util::readJsonAction(
                        req, asyncResp->res, "L1PredictionModeEnabled",
                        l1PredictionModeEnabled))
                {
                    return;
                }
                if (l1PredictionModeEnabled)
                {
                    // Get switch object and update properties
                    redfish::nvidia_fabric_utils::getSwitchObject(
                        asyncResp, fabricId, switchId,
                        [l1PredictionModeEnabled](
                            const std::shared_ptr<bmcweb::AsyncResp>&
                                asyncResp1,
                            [[maybe_unused]] const std::string& fabricId1,
                            [[maybe_unused]] const std::string& switchId1,
                            const std::string& objectPath,
                            [[maybe_unused]] const dbus::utility::
                                MapperServiceMap& serviceMap) {
                            redfish::nvidia_fabric_utils::patchL1PowerMode(
                                asyncResp1, *l1PredictionModeEnabled,
                                objectPath, serviceMap);
                        });
                }
            });
}

} // namespace redfish
