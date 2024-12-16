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
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/processor.hpp"
#include "generated/enums/resource.hpp"
#include "health.hpp"

#include <boost/container/flat_map.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/message/native_types.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <sdbusplus/utility/dedup_variant.hpp>
#include <utils/chassis_utils.hpp>
#include <utils/collection.hpp>
#include <utils/conditions_utils.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/json_utils.hpp>
#include <utils/nvidia_async_set_utils.hpp>
#include <utils/nvidia_processor_utils.hpp>
#include <utils/processor_utils.hpp>
#include <utils/time_utils.hpp>

#include <array>
#include <filesystem>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>

namespace redfish
{
using GetSubTreeType = std::vector<
    std::pair<std::string,
              std::vector<std::pair<std::string, std::vector<std::string>>>>>;
using GetManagedPropertyType =
    boost::container::flat_map<std::string, dbus::utility::DbusVariantType>;
// Map of service name to list of interfaces
using MapperServiceMap =
    std::vector<std::pair<std::string, std::vector<std::string>>>;

// Map of object paths to MapperServiceMaps
using MapperGetSubTreeResponse =
    std::vector<std::pair<std::string, MapperServiceMap>>;

inline void requestRoutesProcessorPortCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Ports")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                BMCWEB_LOG_DEBUG("Get available system processor resource");
                crow::connections::systemBus->async_method_call(
                    [processorId,
                     asyncResp](const boost::system::error_code ec,
                                const boost::container::flat_map<
                                    std::string,
                                    boost::container::flat_map<
                                        std::string, std::vector<std::string>>>&
                                    subtree) {
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG("DBUS response error");
                            messages::internalError(asyncResp->res);

                            return;
                        }
                        for (const auto& [path, object] : subtree)
                        {
                            if (!path.ends_with(processorId))
                            {
                                continue;
                            }
                            asyncResp->res.jsonValue["@odata.id"] =
                                "/redfish/v1/Systems/" +
                                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                "/Processors/" + processorId + "/Ports";
                            asyncResp->res.jsonValue["@odata.type"] =
                                "#PortCollection.PortCollection";
                            asyncResp->res.jsonValue["Name"] =
                                "NVLink Port Collection";

                            collection_util::getCollectionMembersByAssociation(
                                asyncResp,
                                "/redfish/v1/Systems/" +
                                    std::string(
                                        BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                    "/Processors/" + processorId + "/Ports",
                                path + "/all_states",
                                {"xyz.openbmc_project.Inventory.Item.Port"});
                            return;
                        }
                        // Object not found
                        messages::resourceNotFound(
                            asyncResp->res, "#Processor.v1_20_0.Processor",
                            processorId);
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                    "/xyz/openbmc_project/inventory", 0,
                    std::array<const char*, 2>{
                        "xyz.openbmc_project.Inventory.Item.Cpu",
                        "xyz.openbmc_project.Inventory.Item.Accelerator"});
            });
}

inline void getConnectedSwitchPorts(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& portPath, const std::string& fabricId,
    const std::string& switchName)
{
    BMCWEB_LOG_DEBUG("Get connected switch ports on {}", switchName);
    crow::connections::systemBus->async_method_call(
        [asyncResp, portPath, fabricId,
         switchName](const boost::system::error_code ec,
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
                    BMCWEB_LOG_DEBUG("Unable to fetch port");
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

inline void getConnectedSwitches(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& switchPath, const std::string& portPath,
    const std::string& switchName)
{
    BMCWEB_LOG_DEBUG("Get connected switch on{}", switchName);
    crow::connections::systemBus->async_method_call(
        [asyncResp, switchPath, portPath,
         switchName](const boost::system::error_code ec,
                     std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_DEBUG("Get connected switch failed on: {}",
                                 switchName);
                return;
            }
            for (const std::string& fabricPath : *data)
            {
                sdbusplus::message::object_path objectPath(fabricPath);
                std::string fabricId = objectPath.filename();
                if (fabricId.empty())
                {
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
                getConnectedSwitchPorts(asyncResp, portPath, fabricId,
                                        switchName);
            }
        },
        "xyz.openbmc_project.ObjectMapper", switchPath + "/fabrics",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getConnectedProcessorPorts(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& portPath, std::vector<std::string>& portNames)
{
    // This is for when the ports are connected to another processor
    crow::connections::systemBus->async_method_call(
        [asyncResp, portPath,
         portNames](const boost::system::error_code ec,
                    std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Get connected processor ports failed on: {}",
                                 portPath);
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }

            nlohmann::json& connectedPortslinksArray =
                asyncResp->res.jsonValue["Links"]["ConnectedPorts"];

            unsigned int i = 0;

            for (const std::string& processorPath : *data)
            {
                if (!processorPath.empty())
                {
                    sdbusplus::message::object_path connectedProcessorPath(
                        processorPath);
                    std::string processorName =
                        connectedProcessorPath.filename();
                    if (processorName.empty())
                    {
                        BMCWEB_LOG_DEBUG(
                            "Get connected processor path failed on: {}",
                            portPath);
                        return;
                    }

                    nlohmann::json thisProcessorPort = nlohmann::json::object();

                    std::string processorPortUri =
                        "/redfish/v1/Systems/" +
                        std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                        "/Processors/";
                    processorPortUri += processorName;
                    processorPortUri += "/Ports/";
                    processorPortUri += portNames[i];

                    thisProcessorPort["@odata.id"] = processorPortUri;
                    // Check if thisProcessorPort is not already in the array
                    if (std::find(connectedPortslinksArray.begin(),
                                  connectedPortslinksArray.end(),
                                  thisProcessorPort) ==
                        connectedPortslinksArray.end())
                    {
                        // If not found, push back the value
                        connectedPortslinksArray.push_back(
                            std::move(thisProcessorPort));
                    }
                    i++;
                }
            }
        },
        "xyz.openbmc_project.ObjectMapper", portPath + "/associated_processor",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getProcessorPortLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& portPath, const std::string& processorId,
    const std::string& port)
{
    BMCWEB_LOG_DEBUG("Get associated ports on{}", port);

    // This is for when the ports are connected to a switch
    crow::connections::systemBus->async_method_call(
        [asyncResp, portPath, processorId,
         port](const boost::system::error_code ec,
               std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Get associated switch failed on: {}", port);
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            nlohmann::json& switchlinksArray =
                asyncResp->res.jsonValue["Links"]["ConnectedSwitches"];
            switchlinksArray = nlohmann::json::array();
            nlohmann::json& portlinksArray =
                asyncResp->res.jsonValue["Links"]["ConnectedSwitchPorts"];
            portlinksArray = nlohmann::json::array();
            for (const std::string& switchPath : *data)
            {
                sdbusplus::message::object_path objectPath(switchPath);
                std::string switchName = objectPath.filename();
                if (switchName.empty())
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                getConnectedSwitches(asyncResp, switchPath, portPath,
                                     switchName);
            }
        },
        "xyz.openbmc_project.ObjectMapper", portPath + "/associated_switch",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");

    // This is for when the ports are connected to another processor
    crow::connections::systemBus->async_method_call(
        [asyncResp, portPath, processorId,
         port](const boost::system::error_code ec,
               std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Get associated processor ports failed on: {}",
                                 port);
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            nlohmann::json& connectedPortslinksArray =
                asyncResp->res.jsonValue["Links"]["ConnectedPorts"];
            connectedPortslinksArray = nlohmann::json::array();
            std::vector<std::string> portNames;
            for (const std::string& connectedPort : *data)
            {
                sdbusplus::message::object_path connectedPortPath(
                    connectedPort);
                std::string portName = connectedPortPath.filename();
                if (portName.empty())
                {
                    messages::internalError(asyncResp->res);
                    return;
                }

                portNames.push_back(portName);
            }
            getConnectedProcessorPorts(asyncResp, portPath, portNames);
        },
        "xyz.openbmc_project.ObjectMapper",
        portPath + "/associated_processor_ports",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getProcessorPortData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& processorId, const std::string& portId)
{
    BMCWEB_LOG_DEBUG("Get processor port data");
    crow::connections::systemBus->async_method_call(
        [aResp, processorId,
         portId](const boost::system::error_code& e,
                 std::variant<std::vector<std::string>>& resp) {
            if (e)
            {
                // no state sensors attached.
                messages::internalError(aResp->res);
                return;
            }

            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }

            for (const std::string& sensorpath : *data)
            {
                // Check Interface in Object or not
                BMCWEB_LOG_DEBUG("processor state sensor object path {}",
                                 sensorpath);
                crow::connections::systemBus->async_method_call(
                    [aResp, sensorpath, processorId, portId](
                        const boost::system::error_code ec,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& object) {
                        if (ec)
                        {
                            // the path does not implement port interfaces
                            BMCWEB_LOG_DEBUG(
                                "no port interface on object path {}",
                                sensorpath);
                            return;
                        }

                        sdbusplus::message::object_path path(sensorpath);
                        if (path.filename() != portId || object.size() != 1)
                        {
                            return;
                        }

                        std::string portUri =
                            "/redfish/v1/Systems/" +
                            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                            "/Processors/";
                        portUri += processorId;
                        portUri += "/Ports/";
                        portUri += portId;
                        aResp->res.jsonValue["@odata.id"] = portUri;
                        aResp->res.jsonValue["@odata.type"] =
                            "#Port.v1_4_0.Port";
                        std::string portName = processorId + " ";
                        portName += portId + " Port";
                        aResp->res.jsonValue["Name"] = portName;
                        aResp->res.jsonValue["Id"] = portId;

                        redfish::port_utils::getCpuPortData(
                            aResp, object.front().first, sensorpath);
                        getProcessorPortLinks(aResp, sensorpath, processorId,
                                              portId);
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetObject", sensorpath,
                    std::array<std::string, 1>(
                        {"xyz.openbmc_project.Inventory.Item.Port"}));
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_states",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getProcessorAcceleratorPortData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& processorId, const std::string& portId)
{
    BMCWEB_LOG_DEBUG("Get processor port data");
    crow::connections::systemBus->async_method_call(
        [aResp, objPath, processorId,
         portId](const boost::system::error_code e,
                 std::variant<std::vector<std::string>>& resp) {
            if (e)
            {
                // no state sensors attached.
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }

            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_ERROR("No response error while getting ports");
                messages::internalError(aResp->res);
                return;
            }

            for (const std::string& sensorpath : *data)
            {
                // Check Interface in Object or not
                BMCWEB_LOG_DEBUG("processor state sensor object path {}",
                                 sensorpath);
                sdbusplus::message::object_path path(sensorpath);
                if (path.filename() != portId)
                {
                    continue;
                }

                crow::connections::systemBus->async_method_call(
                    [aResp, sensorpath, processorId,
                     portId](const boost::system::error_code ec,
                             const boost::container::flat_map<
                                 std::string,
                                 boost::container::flat_map<
                                     std::string, std::vector<std::string>>>&
                                 subtree1) {
                        if (ec)
                        {
                            // the path does not implement port interfaces
                            BMCWEB_LOG_DEBUG(
                                "no port interface on object path {}",
                                sensorpath);
                            return;
                        }

                        for (const auto& [portPath, object1] : subtree1)
                        {
                            sdbusplus::message::object_path pPath(portPath);
                            if (pPath.filename() != portId)
                            {
                                continue;
                            }

                            std::string portUri =
                                "/redfish/v1/Systems/" +
                                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                "/Processors/";
                            portUri += processorId;
                            portUri += "/Ports/";
                            portUri += portId;
                            aResp->res.jsonValue["@odata.id"] = portUri;
                            aResp->res.jsonValue["@odata.type"] =
                                "#Port.v1_4_0.Port";
                            aResp->res.jsonValue["Name"] = portId + " Resource";
                            aResp->res.jsonValue["Id"] = portId;
                            std::string metricsURI = portUri + "/Metrics";
                            aResp->res.jsonValue["Metrics"]["@odata.id"] =
                                metricsURI;

                            std::string portSettingURI = portUri + "/Settings";
                            aResp->res
                                .jsonValue["@Redfish.Settings"]["@odata.type"] =
                                "#Settings.v1_3_3.Settings";
                            aResp->res
                                .jsonValue["@Redfish.Settings"]
                                          ["SettingsObject"]["@odata.id"] =
                                portSettingURI;
                            if constexpr (!BMCWEB_DISABLE_CONDITIONS_ARRAY)
                            {
                                aResp->res.jsonValue["Status"]["Conditions"] =
                                    nlohmann::json::array();
                            }
                            for (const auto& [service, interfaces] : object1)
                            {
                                redfish::port_utils::getPortData(aResp, service,
                                                                 sensorpath);
                                getProcessorPortLinks(aResp, sensorpath,
                                                      processorId, portId);
                            }
                            return;
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTree", objPath,
                    0,
                    std::array<const char*, 1>{
                        "xyz.openbmc_project.Inventory.Item.Port"});
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_states",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void requestRoutesProcessorPort(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Ports/<str>")
        .privileges(redfish::privileges::getProcessor)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            [[maybe_unused]] const std::string& systemName,
                            const std::string& processorId,
                            const std::string& port) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            BMCWEB_LOG_DEBUG("Get available system processor resource");
            crow::connections::systemBus->async_method_call(
                [processorId, port, asyncResp](
                    const boost::system::error_code ec,
                    const boost::container::flat_map<
                        std::string,
                        boost::container::flat_map<
                            std::string, std::vector<std::string>>>& subtree) {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG("DBUS response error");
                        messages::internalError(asyncResp->res);

                        return;
                    }
                    for (const auto& [path, object] : subtree)
                    {
                        if (!path.ends_with(processorId))
                        {
                            continue;
                        }
                        for (const auto& [serviceName, interfacesList] : object)
                        {
                            if (std::find(
                                    interfacesList.begin(),
                                    interfacesList.end(),
                                    "xyz.openbmc_project.Inventory.Item.Cpu") !=
                                interfacesList.end())
                            {
                                getProcessorPortData(asyncResp, path,
                                                     processorId, port);
                            }
                            else if (
                                std::find(
                                    interfacesList.begin(),
                                    interfacesList.end(),
                                    "xyz.openbmc_project.Inventory.Item.Accelerator") !=
                                interfacesList.end())
                            {
                                getProcessorAcceleratorPortData(
                                    asyncResp, path, processorId, port);
                            }
                        }
                        return;
                    }
                    // Object not found
                    messages::resourceNotFound(asyncResp->res,
                                               "#Processor.v1_20_0.Processor",
                                               processorId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/inventory", 0,
                std::array<const char*, 2>{
                    "xyz.openbmc_project.Inventory.Item.Cpu",
                    "xyz.openbmc_project.Inventory.Item.Accelerator"});
        });
}

inline void getProcessorPortMetricsData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, service,
         path](const boost::system::error_code ec,
               // const boost::container::flat_map<std::string,
               // std::variant<size_t,uint16_t,uint32_t,uint64_t>>&
               const boost::container::flat_map<
                   std::string, dbus::utility::DbusVariantType>& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(asyncResp->res);
                return;
            }
            for (const auto& property : properties)
            {
                if ((property.first == "TXBytes") ||
                    (property.first == "RXBytes"))
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
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
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
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
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
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for transmit packets");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Networking"]["TXFrames"] = *value;
                }
                else if (property.first == "TXDiscardPkts")
                {
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for transmit discard packets");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Networking"]["TXDiscards"] =
                        *value;
                }
                if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                {
                    if (property.first == "MalformedPkts")
                    {
                        const uint64_t* value =
                            std::get_if<uint64_t>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Null value returned "
                                             "for malformed packets");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["MalformedPackets"] =
                            *value;
                    }
                    else if (property.first == "VL15DroppedPkts")
                    {
                        const uint64_t* value =
                            std::get_if<uint64_t>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Null value returned "
                                             "for VL15 dropped packets");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["VL15Dropped"] = *value;
                    }
                    else if (property.first == "VL15TXPkts")
                    {
                        const uint64_t* value =
                            std::get_if<uint64_t>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Null value returned "
                                             "for VL15 dropped packets");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["VL15TXPackets"] =
                            *value;
                    }
                    else if (property.first == "VL15TXData")
                    {
                        const uint64_t* value =
                            std::get_if<uint64_t>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Null value returned "
                                             "for VL15 dropped packets");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["VL15TXBytes"] = *value;
                    }
                    else if (property.first == "LinkErrorRecoveryCounter")
                    {
                        const uint64_t* value =
                            std::get_if<uint64_t>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Null value returned "
                                             "for link error recovery count");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                                ["LinkErrorRecoveryCount"] =
                            *value;
                    }
                    else if (property.first == "LinkDownCount")
                    {
                        const uint64_t* value =
                            std::get_if<uint64_t>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Null value returned "
                                             "for link down count");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["LinkDownedCount"] =
                            *value;
                    }
                    else if (property.first == "TXWait")
                    {
                        const uint64_t* value =
                            std::get_if<uint64_t>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Null value returned "
                                             "for transmit wait");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res.jsonValue["Oem"]["Nvidia"]["TXWait"] =
                            *value;
                    }
                    else if (property.first == "RXNoProtocolBytes")
                    {
                        const uint64_t* value =
                            std::get_if<uint64_t>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Null value returned "
                                             "for RXNoProtocolBytes");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                            "#NvidiaPortMetrics.v1_3_0.NvidiaNVLinkPortMetrics";
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["RXNoProtocolBytes"] =
                            *value;
                    }
                    else if (property.first == "TXNoProtocolBytes")
                    {
                        const uint64_t* value =
                            std::get_if<uint64_t>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Null value returned "
                                             "for TXNoProtocolBytes");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["TXNoProtocolBytes"] =
                            *value;
                    }
                    else if (property.first == "BitErrorRate")
                    {
                        const double* value =
                            std::get_if<double>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Null value returned "
                                             "for bit error rate");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["BitErrorRate"] =
                            *value;
                    }
                    else if (property.first == "DataCRCCount")
                    {
                        const uint32_t* value =
                            std::get_if<uint32_t>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Null value returned "
                                             "for DataCRCCount");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]
                                      ["DataCRCCount"] = *value;
                    }
                    else if (property.first == "FlitCRCCount")
                    {
                        const uint32_t* value =
                            std::get_if<uint32_t>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Null value returned "
                                             "for FlitCRCCount");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]
                                      ["FlitCRCCount"] = *value;
                    }
                    else if (property.first == "RecoveryCount")
                    {
                        const uint32_t* value =
                            std::get_if<uint32_t>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Null value returned "
                                             "for RecoveryCount");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]
                                      ["RecoveryCount"] = *value;
                    }
                    else if (property.first == "ReplayErrorsCount")
                    {
                        const uint32_t* value =
                            std::get_if<uint32_t>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Null value returned "
                                             "for ReplayCount");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]
                                      ["ReplayCount"] = *value;
                    }
                    else if (property.first == "RuntimeError")
                    {
                        const uint16_t* value =
                            std::get_if<uint16_t>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR(
                                "Null value returned for RuntimeError");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        if (*value != 0)
                        {
                            asyncResp->res
                                .jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]
                                          ["RuntimeError"] = true;
                        }
                        else
                        {
                            asyncResp->res
                                .jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]
                                          ["RuntimeError"] = false;
                        }
                    }
                    else if (property.first == "TrainingError")
                    {
                        const uint16_t* value =
                            std::get_if<uint16_t>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR(
                                "Null value returned for TrainingError");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        if (*value != 0)
                        {
                            asyncResp->res
                                .jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]
                                          ["TrainingError"] = true;
                        }
                        else
                        {
                            asyncResp->res
                                .jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]
                                          ["TrainingError"] = false;
                        }
                    }
                    else if (property.first == "NVLinkDataRxBandwidthGbps")
                    {
                        const double* value =
                            std::get_if<double>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_DEBUG("Null value returned "
                                             "for NVLinkDataRxBandwidthGbps");
                        }
                        else
                        {
                            asyncResp->res
                                .jsonValue["Oem"]["Nvidia"]
                                          ["NVLinkDataRxBandwidthGbps"] =
                                *value;
                        }
                    }
                    else if (property.first == "NVLinkDataTxBandwidthGbps")
                    {
                        const double* value =
                            std::get_if<double>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_DEBUG("Null value returned "
                                             "for NVLinkDataTxBandwidthGbps");
                        }
                        else
                        {
                            asyncResp->res
                                .jsonValue["Oem"]["Nvidia"]
                                          ["NVLinkDataTxBandwidthGbps"] =
                                *value;
                        }
                    }
                    else if (property.first == "NVLinkRawRxBandwidthGbps")
                    {
                        const double* value =
                            std::get_if<double>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_DEBUG("Null value returned "
                                             "for NVLinkRawRxBandwidthGbps");
                        }
                        else
                        {
                            asyncResp->res
                                .jsonValue["Oem"]["Nvidia"]
                                          ["NVLinkRawRxBandwidthGbps"] = *value;
                        }
                    }
                    else if (property.first == "NVLinkRawTxBandwidthGbps")
                    {
                        const double* value =
                            std::get_if<double>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_DEBUG("Null value returned "
                                             "for NVLinkRawTxBandwidthGbps");
                        }
                        else
                        {
                            asyncResp->res
                                .jsonValue["Oem"]["Nvidia"]
                                          ["NVLinkRawTxBandwidthGbps"] = *value;
                        }
                    }
                    else if (property.first == "RXErrorsPerLane")
                    {
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                            "#NvidiaPortMetrics.v1_4_0.NvidiaPortMetrics";

                        const std::vector<uint32_t>* value =
                            std::get_if<std::vector<uint32_t>>(
                                &property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_DEBUG("Null value returned "
                                             "for RXErrorsPerLane");
                        }
                        else
                        {
                            asyncResp->res
                                .jsonValue["Oem"]["Nvidia"]["RXErrorsPerLane"] =
                                *value;
                        }
                    }
                }
                if (property.first == "ceCount")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    // harsh - we need to fix redfish namespace here.
                    asyncResp->res
                        .jsonValue["PCIeErrors"]["CorrectableErrorCount"] =
                        ::nvidia::nsm_utils::tryConvertToInt64(*value);
                }
                else if (property.first == "nonfeCount")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res
                        .jsonValue["PCIeErrors"]["NonFatalErrorCount"] =
                        ::nvidia::nsm_utils::tryConvertToInt64(*value);
                }
                else if (property.first == "feCount")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["PCIeErrors"]["FatalErrorCount"] =
                        ::nvidia::nsm_utils::tryConvertToInt64(*value);
                }
                else if (property.first == "L0ToRecoveryCount")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res
                        .jsonValue["PCIeErrors"]["L0ToRecoveryCount"] =
                        ::nvidia::nsm_utils::tryConvertToInt64(*value);
                }
                else if (property.first == "ReplayCount")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["PCIeErrors"]["ReplayCount"] =
                        ::nvidia::nsm_utils::tryConvertToInt64(*value);
                }
                else if (property.first == "ReplayRolloverCount")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res
                        .jsonValue["PCIeErrors"]["ReplayRolloverCount"] =
                        ::nvidia::nsm_utils::tryConvertToInt64(*value);
                }
                else if (property.first == "NAKSentCount")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["PCIeErrors"]["NAKSentCount"] =
                        ::nvidia::nsm_utils::tryConvertToInt64(*value);
                }
                else if (property.first == "NAKReceivedCount")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["PCIeErrors"]["NAKReceivedCount"] =
                        ::nvidia::nsm_utils::tryConvertToInt64(*value);
                }
                else if (property.first == "UnsupportedRequestCount")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Invalid Data Type");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res
                        .jsonValue["PCIeErrors"]["UnsupportedRequestCount"] =
                        ::nvidia::nsm_utils::tryConvertToInt64(*value);
                }
            }
        },
        service, path, "org.freedesktop.DBus.Properties", "GetAll", "");
}

inline void requestRoutesProcessorPortMetrics(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Ports/<str>/Metrics")
        .privileges(redfish::privileges::getProcessor)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            [[maybe_unused]] const std::string& systemName,
                            const std::string& processorId,
                            const std::string& portId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            BMCWEB_LOG_DEBUG("Get available system processor resource");
            crow::connections::systemBus->async_method_call(
                [processorId, portId, asyncResp](
                    const boost::system::error_code ec,
                    const boost::container::flat_map<
                        std::string,
                        boost::container::flat_map<
                            std::string, std::vector<std::string>>>& subtree) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("DBUS response error");
                        messages::internalError(asyncResp->res);

                        return;
                    }
                    for (const auto& [path, object] : subtree)
                    {
                        if (!path.ends_with(processorId))
                        {
                            continue;
                        }
                        crow::connections::systemBus->async_method_call(
                            [asyncResp, processorId, portId](
                                const boost::system::error_code& e,
                                std::variant<std::vector<std::string>>& resp) {
                                if (e)
                                {
                                    // no state sensors attached.
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

                                for (const std::string& sensorpath : *data)
                                {
                                    // Check Interface in Object or not
                                    BMCWEB_LOG_DEBUG(
                                        "processor state sensor object path {}",
                                        sensorpath);
                                    crow::connections::systemBus->async_method_call(
                                        [asyncResp, sensorpath, processorId,
                                         portId](
                                            const boost::system::error_code ec,
                                            const std::vector<std::pair<
                                                std::string,
                                                std::vector<std::string>>>&
                                                object) {
                                            if (ec)
                                            {
                                                // the path does not implement
                                                // port interfaces
                                                BMCWEB_LOG_DEBUG(
                                                    "no port interface on object path {}",
                                                    sensorpath);
                                                return;
                                            }

                                            sdbusplus::message::object_path
                                                path(sensorpath);
                                            if (path.filename() != portId)
                                            {
                                                return;
                                            }

                                            std::string portMetricUri =
                                                "/redfish/v1/Systems/" +
                                                std::string(
                                                    BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                                "/Processors/";
                                            portMetricUri += processorId;
                                            portMetricUri += "/Ports/";
                                            portMetricUri +=
                                                portId + "/Metrics";
                                            asyncResp->res
                                                .jsonValue["@odata.id"] =
                                                portMetricUri;
                                            asyncResp->res
                                                .jsonValue["@odata.type"] =
                                                "#PortMetrics.v1_3_0.PortMetrics";
                                            asyncResp->res.jsonValue["Name"] =
                                                portId + " Port Metrics";
                                            asyncResp->res.jsonValue["Id"] =
                                                "Metrics";

                                            for (const auto& [service,
                                                              interfaces] :
                                                 object)
                                            {
                                                getProcessorPortMetricsData(
                                                    asyncResp, service,
                                                    sensorpath);
                                                if constexpr (
                                                    BMCWEB_NVIDIA_OEM_PROPERTIES)
                                                {
                                                    if (std::find(
                                                            interfaces.begin(),
                                                            interfaces.end(),
                                                            "xyz.openbmc_project.PCIe.ClearPCIeCounters") !=
                                                        interfaces.end())
                                                    {
                                                        asyncResp->res.jsonValue
                                                            ["Actions"]["Oem"]
                                                            ["#NvidiaPortMetrics.ClearPCIeCounters"]
                                                            ["target"] =
                                                            portMetricUri +
                                                            "/Actions/Oem/NvidiaPortMetrics.ClearPCIeCounters";
                                                        asyncResp->res.jsonValue
                                                            ["Actions"]["Oem"]
                                                            ["#NvidiaPortMetrics.ClearPCIeCounters"]
                                                            ["@Redfish.ActionInfo"] =
                                                            portMetricUri +
                                                            "/Oem/Nvidia/ClearPCIeCountersActionInfo";
                                                    }
                                                }
                                            }
                                        },
                                        "xyz.openbmc_project.ObjectMapper",
                                        "/xyz/openbmc_project/object_mapper",
                                        "xyz.openbmc_project.ObjectMapper",
                                        "GetObject", sensorpath,
                                        std::array<std::string, 1>(
                                            {"xyz.openbmc_project.Inventory.Item.Port"}));
                                }
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            path + "/all_states",
                            "org.freedesktop.DBus.Properties", "Get",
                            "xyz.openbmc_project.Association", "endpoints");
                        return;
                    }
                    // Object not found
                    messages::resourceNotFound(asyncResp->res,
                                               "#Processor.v1_20_0.Processor",
                                               processorId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/inventory", 0,
                std::array<const char*, 2>{
                    "xyz.openbmc_project.Inventory.Item.Cpu",
                    "xyz.openbmc_project.Inventory.Item.Accelerator"});
        });
}

inline void requestRoutesClearPCIeCountersActionInfo(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/Processors/<str>/"
                 "Ports/<str>/Metrics/Oem/Nvidia/ClearPCIeCountersActionInfo")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemId,
                   const std::string& processorId, const std::string& portId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                redfish::nvidia_processor_utils::getClearPCIeCountersActionInfo(
                    asyncResp, processorId, portId);
            });
}

inline void requestRoutesPCIeClearCounter(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Processors/<str>/"
        "Ports/<str>/Metrics/Actions/Oem/NvidiaPortMetrics.ClearPCIeCounters/")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemId,
                   const std::string& processorId, const std::string& portId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::optional<std::string> counterType;
                if (!json_util::readJsonAction(req, asyncResp->res,
                                               "CounterType", counterType))
                {
                    return;
                }

                redfish::nvidia_processor_utils::postPCIeClearCounter(
                    asyncResp, processorId, portId, *counterType);
            });
}

inline void getPortDisableFutureStatus(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& processorId, const std::string& objectPath,
    const dbus::utility::MapperServiceMap& serviceMap,
    const std::string& portId)
{
    BMCWEB_LOG_DEBUG("Get getPortDisableFutureStatus data");
    redfish::nvidia_processor_utils::getPortDisableFutureStatus(
        aResp, processorId, objectPath, serviceMap, portId);
}

inline void requestRoutesProcessorPortSettings(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Ports/<str>/Settings")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemId,
                   const std::string& processorId, const std::string& portId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                BMCWEB_LOG_DEBUG("Setting for {} Processor and {} PortID.",
                                 processorId, portId);

                std::string processorPortSettingsURI =
                    std::format("/redfish/v1/Systems/{}/Processors/",
                                BMCWEB_REDFISH_SYSTEM_URI_NAME);
                processorPortSettingsURI += processorId;
                processorPortSettingsURI += "/Ports/";
                processorPortSettingsURI += portId;
                processorPortSettingsURI += "/Settings";

                asyncResp->res.jsonValue["@odata.type"] = "#Port.v1_4_0.Port";
                asyncResp->res.jsonValue["@odata.id"] =
                    processorPortSettingsURI;
                asyncResp->res.jsonValue["Id"] = "Settings";
                asyncResp->res.jsonValue["Name"] =
                    processorId + " " + portId + " Pending Settings";

                if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                {
                    redfish::processor_utils::getProcessorObject(
                        asyncResp, processorId,
                        [portId](
                            const std::shared_ptr<bmcweb::AsyncResp>&
                                asyncResp1,
                            const std::string& processorId1,
                            const std::string& objectPath,
                            const MapperServiceMap& serviceMap,
                            [[maybe_unused]] const std::string& deviceType) {
                            getPortDisableFutureStatus(asyncResp1, processorId1,
                                                       objectPath, serviceMap,
                                                       portId);
                        });
                }
            });

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Ports/<str>/Settings")
        .privileges(redfish::privileges::patchProcessor)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemId,
                   const std::string& processorId, const std::string& portId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                BMCWEB_LOG_DEBUG("Setting for {} Processor and {} PortID.",
                                 processorId, portId);
                std::optional<std::string> linkState;
                if (!redfish::json_util::readJsonAction(req, asyncResp->res,
                                                        "LinkState", linkState))
                {
                    return;
                }

                if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                {
                    if (linkState)
                    {
                        redfish::processor_utils::getProcessorObject(
                            asyncResp, processorId,
                            [linkState,
                             portId](const std::shared_ptr<bmcweb::AsyncResp>&
                                         asyncResp1,
                                     const std::string& processorId1,
                                     const std::string& objectPath,
                                     const MapperServiceMap& serviceMap,
                                     [[maybe_unused]] const std::string&
                                         deviceType) {
                                redfish::nvidia_processor_utils::
                                    patchPortDisableFuture(
                                        asyncResp1, processorId1, portId,
                                        *linkState, "PortDisableFuture",
                                        objectPath, serviceMap);
                            });
                    }
                }
            });
}

} // namespace redfish
