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

#include <app.hpp>
#include <boost/algorithm/string/split.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/json_utils.hpp>

#include <iostream>
#include <regex>
#include <variant>
#include <vector>

namespace redfish
{

using propertyTypes = std::variant<
    int, std::string, uint32_t, bool,
    std::vector<std::tuple<std::string, std::string, std::string>>>;

inline std::string dbusSlotTypesToRedfish(const std::string& slotType)
{
    if (slotType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.FullLength")
    {
        return "FullLength";
    }
    if (slotType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.HalfLength")
    {
        return "HalfLength";
    }
    if (slotType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.LowProfile")
    {
        return "LowProfile";
    }
    if (slotType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.Mini")
    {
        return "Mini";
    }
    if (slotType == "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.M_2")
    {
        return "M2";
    }
    if (slotType == "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.OEM")
    {
        return "OEM";
    }
    if (slotType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.OCP3Small")
    {
        return "OCP3Small";
    }
    if (slotType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.OCP3Large")
    {
        return "OCP3Large";
    }
    if (slotType == "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes.U_2")
    {
        return "U2";
    }

    // Unknown or others
    return "";
}

inline std::string dbusPcieTypesToRedfish(const std::string& pcieType)
{
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen1")
    {
        return "Gen1";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen2")
    {
        return "Gen2";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen3")
    {
        return "Gen3";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen4")
    {
        return "Gen4";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen5")
    {
        return "Gen5";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations.Gen6")
    {
        return "Gen6";
    }

    // Unknown or others
    return "";
}

/**
 * @brief Fill properties into json object
 *
 * @param[in,out]   json           Json object
 * @param[in]       dbusProperties Properties of Slot.
 */
inline void
    fillProperties(nlohmann::json& json,
                   const std::map<std::string, propertyTypes>& dbusProperties)
{
    for (const auto& [key, val] : dbusProperties)
    {
        if (key.compare("ServiceLabel") == 0 &&
            std::holds_alternative<std::string>(val))
        {
            json["Location"]["PartLocation"]["ServiceLabel"] =
                std::get<std::string>(val);
        }
        else if (std::holds_alternative<uint32_t>(val))
        {
            json[key] = std::get<uint32_t>(val);
        }
        else if (std::holds_alternative<int>(val))
        {
            json[key] = std::get<int>(val);
        }
        else if (std::holds_alternative<std::string>(val))
        {
            json[key] = std::get<std::string>(val);
        }
        else
        {
            BMCWEB_LOG_ERROR("Unknwon value type for key {}", key);
        }
    }
}

/**
 * @brief Get all pcieslot  processor links info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 * @param[in]       chassisId   Chassis that contains the pcieslot.
 */
inline void updatePCIeSlotsProcessorLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::map<std::string, propertyTypes>& dbusProperties,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("updatePCIeSlotsPrcoessorLinks ");
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath,
         dbusProperties](const boost::system::error_code ec,
                         std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("processor port not found for  pcieslot ");
                return; // no processors identified for pcieslotpath
            }

            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_ERROR("processor data null for pcieslot ");
                return;
            }

            for (const std::string& processorPath : *data)
            {
                sdbusplus::message::object_path dbusObjPath(processorPath);
                const std::string& processorId = dbusObjPath.filename();

                // Get Port links using associtaions
                crow::connections::systemBus->async_method_call(
                    [asyncResp, processorId, dbusProperties](
                        const boost::system::error_code ec,
                        std::variant<std::vector<std::string>>& resp) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR("port not found for pcieslot ");
                            return;
                        }

                        std::vector<std::string>* data =
                            std::get_if<std::vector<std::string>>(&resp);
                        if (data == nullptr)
                        {
                            BMCWEB_LOG_ERROR("port data null for pcieslot ");
                            return;
                        }

                        nlohmann::json pcieSlotRes;
                        fillProperties(pcieSlotRes, dbusProperties);

                        // declaring processors array
                        nlohmann::json& prcoessorsLinkArray =
                            pcieSlotRes["Links"]["Processors"];
                        prcoessorsLinkArray = nlohmann::json::array();

                        std::string processorURI =
                            "/redfish/v1/Systems/" +
                            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                            "/Processors/";

                        processorURI += processorId;
                        prcoessorsLinkArray.push_back(
                            {{"@odata.id", processorURI}});

                        // declaring connected ports array
                        nlohmann::json& connectedPortsLinkArray =
                            pcieSlotRes["Links"]["Oem"]["Nvidia"]
                                       ["ConnectedPorts"];
                        connectedPortsLinkArray = nlohmann::json::array();

                        std::string connectedPortsURI;

                        for (const std::string& portPath : *data)
                        {
                            sdbusplus::message::object_path dbusObjPath(
                                portPath);
                            const std::string& portId = dbusObjPath.filename();

                            connectedPortsURI =
                                "/redfish/v1/Systems/" +
                                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                "/Processors/";
                            connectedPortsURI += processorId;
                            connectedPortsURI += "/Ports/";
                            connectedPortsURI += portId;
                            connectedPortsLinkArray.push_back(
                                {{"@odata.id", connectedPortsURI}});
                        }

                        nlohmann::json& jResp =
                            asyncResp->res.jsonValue["Slots"];
                        jResp.push_back(pcieSlotRes);
                    },
                    "xyz.openbmc_project.ObjectMapper", objPath + "/port_link",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/processor_link",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Get all pcieslot  swithc links info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 * @param[in]       chassisId   Chassis that contains the pcieslot.
 */
inline void updatePCIeSlotsSwitchLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::map<std::string, propertyTypes>& dbusProperties,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("updatePCIeSlotsSwitchLinks ");

    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath,
         dbusProperties](const boost::system::error_code ec,
                         std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("fabric data not found for pcieslot");
                return;
            }
            // fabric identified for pcieslot
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_ERROR("fabric data null  for pcieslot ");
                return;
            }
            std::string fabricId; // pcieslot fabric id
            for (const std::string& fabricPath : *data)
            {
                sdbusplus::message::object_path dbusObjPath(fabricPath);
                fabricId = dbusObjPath.filename();

                // Get Switch links using associtaions
                crow::connections::systemBus->async_method_call(
                    [asyncResp, objPath, dbusProperties,
                     fabricId](const boost::system::error_code ec,
                               std::variant<std::vector<std::string>>& resp) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR("switch not found for pcieslot ");
                            return;
                        }

                        std::vector<std::string>* data =
                            std::get_if<std::vector<std::string>>(&resp);

                        if (data == nullptr)
                        {
                            BMCWEB_LOG_ERROR("switch data null for pcieslot ");
                            return;
                        }

                        for (const std::string& switchPath : *data)
                        {
                            sdbusplus::message::object_path dbusObjPath(
                                switchPath);
                            const std::string& switchId =
                                dbusObjPath.filename();

                            // Get Port links using associtaions
                            crow::connections::systemBus->async_method_call(
                                [asyncResp, dbusProperties, fabricId, switchId](
                                    const boost::system::error_code ec,
                                    std::variant<std::vector<std::string>>&
                                        resp) {
                                    if (ec)
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "port not found for pcieslot ");
                                        return;
                                    }

                                    std::vector<std::string>* data =
                                        std::get_if<std::vector<std::string>>(
                                            &resp);
                                    nlohmann::json pcieSlotRes;

                                    if (data == nullptr)
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "port data null for pcieslot ");
                                        return;
                                    }

                                    // update dbus properties to json object
                                    fillProperties(pcieSlotRes, dbusProperties);

                                    pcieSlotRes
                                        ["Links"]["Oem"]["Nvidia"]
                                        ["@odata.type"] =
                                            "#NvidiaPCIeSlots.v1_0_0.NvidiaPCIeSlots";

                                    // declaring connected ports array
                                    nlohmann::json& connectedPortsLinkArray =
                                        pcieSlotRes["Links"]["Oem"]["Nvidia"]
                                                   ["ConnectedPorts"];
                                    connectedPortsLinkArray =
                                        nlohmann::json::array();
                                    std::string connectedPortsURI;

                                    for (const std::string& portPath : *data)
                                    {
                                        sdbusplus::message::object_path
                                            dbusObjPath(portPath);
                                        const std::string& portId =
                                            dbusObjPath.filename();

                                        connectedPortsURI =
                                            "/redfish/v1/Fabrics/" + fabricId +
                                            "/Switches/";
                                        connectedPortsURI += switchId;
                                        connectedPortsURI += "/Ports/";
                                        connectedPortsURI += portId;
                                        connectedPortsLinkArray.push_back(
                                            {{"@odata.id", connectedPortsURI}});
                                    }
                                    // sending response
                                    nlohmann::json& jResp =
                                        asyncResp->res.jsonValue["Slots"];
                                    jResp.push_back(pcieSlotRes);
                                },
                                "xyz.openbmc_project.ObjectMapper",
                                objPath + "/port_link",
                                "org.freedesktop.DBus.Properties", "Get",
                                "xyz.openbmc_project.Association", "endpoints");
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    objPath + "/switch_link", "org.freedesktop.DBus.Properties",
                    "Get", "xyz.openbmc_project.Association", "endpoints");
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/fabric_link",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Get all pcieslot networkAdapter links info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 * @param[in]       chassisId   Chassis that contains the pcieslot.
 */
inline void updatePCIeSlotsNetworkAdapterLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::map<std::string, propertyTypes>& dbusProperties,
    const std::string& objPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath,
         dbusProperties](const boost::system::error_code ec,
                         std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("chassis data not found for pcieslot");
                return;
            }
            // chassis identified for pcieslot
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_ERROR("chassis data null for pcieslot");
                messages::internalError(asyncResp->res);
                return;
            }
            std::string chassisId; // pcieslot chassis id
            for (const std::string& chassisPath : *data)
            {
                sdbusplus::message::object_path dbusObjPath(chassisPath);
                chassisId = dbusObjPath.filename();

                // Get NetworkAdapter links using associtaions
                crow::connections::systemBus->async_method_call(
                    [asyncResp, objPath, dbusProperties,
                     chassisId](const boost::system::error_code ec,
                                std::variant<std::vector<std::string>>& resp) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR(
                                "network adapter not found for pcieslot");
                            return;
                        }

                        std::vector<std::string>* data =
                            std::get_if<std::vector<std::string>>(&resp);

                        if (data == nullptr)
                        {
                            BMCWEB_LOG_ERROR(
                                "network adapter data null for pcieslot");
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        for (const std::string& networkAdapterPath : *data)
                        {
                            sdbusplus::message::object_path dbusObjPath(
                                networkAdapterPath);
                            const std::string& networkAdapterId =
                                dbusObjPath.filename();

                            // Get Port links using associtaions
                            crow::connections::systemBus->async_method_call(
                                [asyncResp, dbusProperties, chassisId,
                                 networkAdapterId](
                                    const boost::system::error_code ec,
                                    std::variant<std::vector<std::string>>&
                                        resp) {
                                    if (ec)
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "port not found for pcieslot");
                                        return;
                                    }

                                    std::vector<std::string>* data =
                                        std::get_if<std::vector<std::string>>(
                                            &resp);
                                    nlohmann::json pcieSlotRes;

                                    if (data == nullptr)
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "port data null for pcieslot");
                                        return;
                                    }

                                    // update dbus properties to json object
                                    fillProperties(pcieSlotRes, dbusProperties);

                                    pcieSlotRes
                                        ["Links"]["Oem"]["Nvidia"]
                                        ["@odata.type"] =
                                            "#NvidiaPCIeSlots.v1_0_0.NvidiaPCIeSlots";

                                    // declaring connected ports array
                                    nlohmann::json& connectedPortsLinkArray =
                                        pcieSlotRes["Links"]["Oem"]["Nvidia"]
                                                   ["ConnectedPorts"];
                                    connectedPortsLinkArray =
                                        nlohmann::json::array();
                                    std::string connectedPortsURI;

                                    for (const std::string& portPath : *data)
                                    {
                                        sdbusplus::message::object_path
                                            dbusObjPath(portPath);
                                        const std::string& portId =
                                            dbusObjPath.filename();

                                        connectedPortsURI =
                                            "/redfish/v1/Chassis/" + chassisId +
                                            "/NetworkAdapters/";
                                        connectedPortsURI += networkAdapterId;
                                        connectedPortsURI += "/Ports/";
                                        connectedPortsURI += portId;
                                        connectedPortsLinkArray.push_back(
                                            {{"@odata.id", connectedPortsURI}});
                                    }
                                    // sending response
                                    nlohmann::json& jResp =
                                        asyncResp->res.jsonValue["Slots"];
                                    jResp.push_back(pcieSlotRes);
                                },
                                "xyz.openbmc_project.ObjectMapper",
                                objPath + "/port_link",
                                "org.freedesktop.DBus.Properties", "Get",
                                "xyz.openbmc_project.Association", "endpoints");
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    objPath + "/network_adapter_link",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/chassis_link",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Add Slot without links into Slots array of Resp
 *
 * @param[in,out]   asyncResp      Async HTTP response.
 * @param[in]       dbusProperties Properties of Slot.
 */
inline void updatePCIeSlotsNoLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::map<std::string, propertyTypes>& dbusProperties)
{
    BMCWEB_LOG_DEBUG("updatePCIeSlotsNoLinks ");

    nlohmann::json pcieSlotRes;
    fillProperties(pcieSlotRes, dbusProperties);

    nlohmann::json& jResp = asyncResp->res.jsonValue["Slots"];
    jResp.push_back(pcieSlotRes);
}

/**
 * @brief Get all cpieslot info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 * @param[in]       chassisId   Chassis that contains the pcieslot.
 */
inline void updatePCIeSlots(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& service,
                            const std::string& objPath,
                            const std::string& chassisId)
{
    BMCWEB_LOG_DEBUG("updatePCIeSlots ");

    // Get interface properties
    crow::connections::systemBus->async_method_call(
        [asyncResp{asyncResp}, chassisId,
         objPath](const boost::system::error_code ec,
                  const std::vector<std::pair<std::string, propertyTypes>>&
                      propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error for pcieslot properties");
                messages::internalError(asyncResp->res);
                return;
            }

            std::map<std::string, propertyTypes>
                dbusProperties; // map of all pcislot dbus properties
            // pcieslots  data
            for (const std::pair<std::string, propertyTypes>& property :
                 propertiesList)
            {
                // Store DBus properties that are also Redfish
                // properties with same name and a string value
                const std::string& propertyName = property.first;
                if (propertyName == "Generation")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for Generation ");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    std::string pcieType = *value;
                    if (pcieType.starts_with(
                            "xyz.openbmc_project.Inventory.Item.PCIeSlot.Generations"))
                    {
                        pcieType = dbusPcieTypesToRedfish(pcieType);
                    }
                    if (!pcieType.empty())
                    {
                        dbusProperties.insert(
                            std::pair<std::string, propertyTypes>("PCIeType",
                                                                  pcieType));
                    }
                }
                else if (propertyName == "SlotType")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for SlotType");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    std::string slotType = *value;
                    if (slotType.starts_with(
                            "xyz.openbmc_project.Inventory.Item.PCIeSlot.SlotTypes"))
                    {
                        slotType = dbusSlotTypesToRedfish(slotType);
                    }
                    if (!slotType.empty())
                    {
                        dbusProperties.insert(
                            std::pair<std::string, propertyTypes>(propertyName,
                                                                  slotType));
                    }
                }
                else if (propertyName == "Lanes")
                {
                    if (std::holds_alternative<uint32_t>(property.second))
                    {
                        const uint32_t* value =
                            std::get_if<uint32_t>(&property.second);
                        dbusProperties.insert(
                            std::pair<std::string, propertyTypes>(propertyName,
                                                                  *value));
                    }
                    else if (std::holds_alternative<int>(property.second))
                    {
                        const int* value = std::get_if<int>(&property.second);
                        dbusProperties.insert(
                            std::pair<std::string, propertyTypes>(propertyName,
                                                                  *value));
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for Lanes");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                }
                else if (propertyName == "LocationCode")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for LocationCode");
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    dbusProperties.insert(std::pair<std::string, propertyTypes>(
                        "ServiceLabel", *value));
                }
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp, objPath,
                 dbusProperties](const boost::system::error_code ec,
                                 std::vector<std::string>& resp) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("errno = {}, \"{}\"", ec,
                                         ec.message());
                        return; // no links found for this pcie slot
                    }

                    for (auto& linkPash : resp)
                    {
                        if (linkPash == (objPath + "/processor_link"))
                        {
                            // update processor links
                            updatePCIeSlotsProcessorLinks(
                                asyncResp, dbusProperties, objPath);
                            return;
                        }
                        if (linkPash == (objPath + "/fabric_link"))
                        {
                            // Update switch links
                            updatePCIeSlotsSwitchLinks(asyncResp,
                                                       dbusProperties, objPath);
                            return;
                        }
                        if (linkPash == (objPath + "/chassis_link"))
                        {
                            // Update chassis links
                            updatePCIeSlotsNetworkAdapterLinks(
                                asyncResp, dbusProperties, objPath);
                            return;
                        }
                    }

                    // No link found
                    updatePCIeSlotsNoLinks(asyncResp, dbusProperties);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths", objPath,
                1, std::array<const char*, 0>{});
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll", "");
}

/**
 * PCIeSlots override class for delivering Chassis/PCIeSlots Schema
 * Functions triggers appropriate requests on DBus
 */
inline void requestPcieSlotsRoutes(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/PCIeSlots/")
        .privileges({{"Login"}})
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisId) {
            BMCWEB_LOG_DEBUG("PCIeSlot doGet enter");
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            const std::array<const char*, 1> interface = {
                "xyz.openbmc_project.Inventory.Item.Chassis"};
            // Get chassis collection
            crow::connections::systemBus->async_method_call(
                [asyncResp, chassisId(std::string(chassisId))](
                    const boost::system::error_code ec,
                    const crow::openbmc_mapper::GetSubTreeType& subtree) {
                    const std::array<const char*, 1> pcieslotIntf = {
                        "xyz.openbmc_project.Inventory.Item.PCIeSlot"};
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG("DBUS response error");
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
                        sdbusplus::message::object_path objPath(path);
                        if (objPath.filename() != chassisId)
                        {
                            continue;
                        }
                        // Chassis pcieslot properties
                        asyncResp->res.jsonValue["@odata.type"] =
                            "#PCIeSlots.v1_5_0.PCIeSlots";
                        asyncResp->res.jsonValue["@odata.id"] =
                            "/redfish/v1/Chassis/" + chassisId + "/PCIeSlots";
                        asyncResp->res.jsonValue["Id"] = "PCIeSlots";
                        asyncResp->res.jsonValue["Name"] =
                            "PCIeSlots for " + chassisId;
                        // Get chassis pcieSlots
                        crow::connections::systemBus->async_method_call(
                            [asyncResp, chassisId(std::string(chassisId))](
                                const boost::system::error_code ec,
                                const crow::openbmc_mapper::GetSubTreeType&
                                    pcieSlotSubtree) {
                                if (ec)
                                {
                                    BMCWEB_LOG_DEBUG("DBUS response error");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                // Update the pcieslots
                                for (const std::pair<
                                         std::string,
                                         std::vector<std::pair<
                                             std::string,
                                             std::vector<std::string>>>>&
                                         object : pcieSlotSubtree)
                                {
                                    const std::string& pcieslot = object.first;
                                    const std::vector<std::pair<
                                        std::string, std::vector<std::string>>>&
                                        connectionNames = object.second;
                                    if (connectionNames.size() < 1)
                                    {
                                        continue;
                                    }
                                    const std::string& connectionName =
                                        connectionNames[0].first;
                                    updatePCIeSlots(asyncResp, connectionName,
                                                    pcieslot, chassisId);
                                }
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            "/xyz/openbmc_project/object_mapper",
                            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                            path + "/", 0, pcieslotIntf);
                        return;
                    }
                    // Couldn't find an object with that name. return an error
                    messages::resourceNotFound(
                        asyncResp->res, "#Chassis.v1_15_0.Chassis", chassisId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/inventory", 0, interface);
        });
}

} // namespace redfish
