/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION &
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
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/nvidia_cable_util.hpp"

#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace nvidia_cables
{

constexpr std::array<std::string_view, 2> cableAssemblyInterfaces = {
    "xyz.openbmc_project.Inventory.Item.Cable",
    "xyz.openbmc_project.Inventory.Item.CableCartridge"};
constexpr std::array<std::string_view, 1> vendorInformationInterfaces = {
    "xyz.openbmc_project.Inventory.Decorator.VendorInformation"};

/**
 * @brief Convert D-Bus CableClass enum to Redfish CableClass string.
 *
 * D-Bus uses fully-qualified enum paths like
 * "xyz.openbmc_project.Inventory.Item.Cable.CableClass.General"
 * while Redfish expects short strings like "General".
 * Maps all known D-Bus CableClass values to Redfish Cable schema values
 * defined in generated/enums/cable.hpp.
 */
inline std::string dbusToRedfishCableClass(const std::string& dbusCableClass)
{
    static constexpr std::string_view prefix =
        "xyz.openbmc_project.Inventory.Item.Cable.CableClass.";

    if (!dbusCableClass.starts_with(prefix))
    {
        BMCWEB_LOG_DEBUG("Unknown D-Bus CableClass: {}", dbusCableClass);
        return "";
    }

    std::string suffix(dbusCableClass.substr(prefix.size()));

    // Redfish Cable.CableClass enum values
    static constexpr std::array<std::string_view, 10> validValues = {
        "General", "Power", "Network", "Storage", "Fan",
        "PCIe",    "USB",   "Video",   "Fabric",  "Serial"};

    for (const auto& valid : validValues)
    {
        if (suffix == valid)
        {
            return suffix;
        }
    }

    BMCWEB_LOG_DEBUG("Unknown D-Bus CableClass suffix: {}", suffix);
    return "";
}

/**
 * @brief Convert D-Bus ConnectorType enum to Redfish ConnectorType string.
 *
 * D-Bus uses fully-qualified enum paths like
 * "xyz.openbmc_project.Inventory.Item.Connector.ConnectorType.Proprietary"
 * while Redfish expects short strings like "Proprietary".
 * Maps all known D-Bus ConnectorType values to Redfish Cable schema values
 * defined in generated/enums/cable.hpp.
 */
inline std::string dbusToRedfishConnectorType(
    const std::string& dbusConnectorType)
{
    static constexpr std::string_view prefix =
        "xyz.openbmc_project.Inventory.Item.Connector.ConnectorType.";

    if (!dbusConnectorType.starts_with(prefix))
    {
        BMCWEB_LOG_DEBUG("Unknown D-Bus ConnectorType: {}", dbusConnectorType);
        return "";
    }

    std::string suffix(dbusConnectorType.substr(prefix.size()));

    // Redfish Cable.ConnectorType enum values
    static constexpr std::array<std::string_view, 20> validValues = {
        "Proprietary", "ACPower", "DB9",     "DCPower", "DisplayPort",
        "HDMI",        "ICI",     "IPASS",   "PCIe",    "RJ45",
        "SATA",        "SCSI",    "SlimSAS", "SFP",     "SFPPlus",
        "USBA",        "USBC",    "QSFP",    "CDFP",    "OSFP"};

    for (const auto& valid : validValues)
    {
        if (suffix == valid)
        {
            return suffix;
        }
    }

    BMCWEB_LOG_DEBUG("Unknown D-Bus ConnectorType suffix: {}", suffix);
    return "";
}

/**
 * @brief Fill Nvidia-specific cable properties from D-Bus data.
 *
 * Converts D-Bus enum paths for CableClass and ConnectorTypes to
 * Redfish strings and populates the response. Called from
 * fillCableProperties in cable.hpp.
 */
inline void fillNvidiaCableProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string* cableClass,
    const std::vector<std::string>* downstreamConnectorTypes,
    const std::vector<std::string>* upstreamConnectorTypes)
{
    if (cableClass != nullptr)
    {
        asyncResp->res.jsonValue["CableClass"] =
            dbusToRedfishCableClass(*cableClass);
    }

    if (downstreamConnectorTypes != nullptr)
    {
        nlohmann::json::array_t downstreamArray;
        for (const auto& connectorType : *downstreamConnectorTypes)
        {
            downstreamArray.emplace_back(
                dbusToRedfishConnectorType(connectorType));
        }
        asyncResp->res.jsonValue["DownstreamConnectorTypes"] = downstreamArray;
    }

    if (upstreamConnectorTypes != nullptr)
    {
        nlohmann::json::array_t upstreamArray;
        for (const auto& connectorType : *upstreamConnectorTypes)
        {
            upstreamArray.emplace_back(
                dbusToRedfishConnectorType(connectorType));
        }
        asyncResp->res.jsonValue["UpstreamConnectorTypes"] = upstreamArray;
    }
}

inline void handleVendorInformationServices(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& cableObjectPath, const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& mapperResponse)
{
    if (ec || mapperResponse.empty())
    {
        return;
    }

    for (const auto& [service, interfaces] : mapperResponse)
    {
        redfish::fetchCBCOemProperties(asyncResp, service, cableObjectPath);
    }
}

/**
 * @brief Fetch Nvidia-specific cable properties from D-Bus interfaces.
 *
 * Handles Asset, Location, LocationContext, and VendorInformation
 * interfaces. Called from getCableProperties in cable.hpp.
 */
inline void getNvidiaCableProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& cableObjectPath,
    const dbus::utility::MapperServiceMap& serviceMap)
{
    for (const auto& [service, interfaces] : serviceMap)
    {
        for (const auto& interface : interfaces)
        {
            if (interface == "xyz.openbmc_project.Inventory.Decorator.Asset" ||
                interface ==
                    "xyz.openbmc_project.Inventory.Decorator.LocationCode" ||
                interface ==
                    "xyz.openbmc_project.Inventory.Decorator.LocationContext")
            {
                redfish::fetchCableInventoryProperties(asyncResp, service,
                                                       cableObjectPath);
            }
            else if (
                interface ==
                "xyz.openbmc_project.Inventory.Decorator.VendorInformation")
            {
                redfish::fetchCBCOemProperties(asyncResp, service,
                                               cableObjectPath);
            }
        }
    }

    dbus::utility::getDbusObject(
        cableObjectPath, vendorInformationInterfaces,
        std::bind_front(handleVendorInformationServices, asyncResp,
                        cableObjectPath));
}

inline void handleDownstreamChassisAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& cableId, const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& resp)
{
    if (ec || resp.empty())
    {
        BMCWEB_LOG_DEBUG(
            "No downstream_chassis associations found for Cable {}", cableId);
        return;
    }

    nlohmann::json::array_t downstreamChassis;
    for (const std::string& chassisPath : resp)
    {
        sdbusplus::message::object_path chassisObjPath(chassisPath);
        std::string chassisId = chassisObjPath.filename();

        nlohmann::json::object_t chassis;
        chassis["@odata.id"] =
            boost::urls::format("/redfish/v1/Chassis/{}", chassisId);
        downstreamChassis.emplace_back(std::move(chassis));
    }
    asyncResp->res.jsonValue["Links"]["DownstreamChassis"] =
        std::move(downstreamChassis);
}

/**
 * @brief Add Nvidia-specific links and properties to a Cable response.
 *
 * Adds the Assembly link and DownstreamChassis association to the Cable
 * Redfish response. Called from afterHandleCableGet in cable.hpp.
 */
inline void addNvidiaCableLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& cableId, const std::string& objectPath)
{
    asyncResp->res.jsonValue["Assembly"]["@odata.id"] =
        boost::urls::format("/redfish/v1/Cables/{}/Assembly", cableId);

    dbus::utility::getAssociationEndPoints(
        objectPath + "/downstream_chassis",
        [asyncResp, cableId](const boost::system::error_code& ec,
                             const dbus::utility::MapperEndPoints& resp) {
            handleDownstreamChassisAssociation(asyncResp, cableId, ec, resp);
        });
}

inline int assemblySuffixNumber(std::string_view assemblyName)
{
    size_t pos = assemblyName.find_last_not_of("0123456789");
    if (pos != std::string::npos && pos < assemblyName.length() - 1)
    {
        return std::stoi(std::string(assemblyName.substr(pos + 1)));
    }
    return 0;
}

inline bool assemblySuffixLess(const std::string& a, const std::string& b)
{
    return assemblySuffixNumber(a) < assemblySuffixNumber(b);
}

inline std::string assemblyMemberId(std::string_view assemblyName)
{
    size_t lastDigitPos = assemblyName.find_last_not_of("0123456789");
    if (lastDigitPos != std::string::npos &&
        lastDigitPos < assemblyName.length() - 1)
    {
        return std::string(assemblyName.substr(lastDigitPos + 1));
    }
    return "0";
}

inline void setCableAssemblyResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& cableId)
{
    asyncResp->res.jsonValue["@odata.type"] = "#Assembly.v1_3_0.Assembly";
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Cables/" + cableId + "/Assembly";
    asyncResp->res.jsonValue["Id"] = "Assembly";
    asyncResp->res.jsonValue["Name"] = "Assembly data for " + cableId;
}

inline void handleCableAssemblyProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& assembly, const std::string& cableId,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("Error getting assembly properties: {}", ec);
        return;
    }

    nlohmann::json assemblyObj = nlohmann::json::object();

    const std::string* name = nullptr;
    const std::string* model = nullptr;
    const std::string* partNumber = nullptr;
    const std::string* serialNumber = nullptr;
    const std::string* manufacturer = nullptr;
    const std::string* version = nullptr;
    const std::string* buildDate = nullptr;
    const std::string* locationContext = nullptr;
    const std::string* locationType = nullptr;
    const std::string* locationCode = nullptr;
    const std::string* physicalContext = nullptr;

    for (const auto& [key, value] : properties)
    {
        if (key == "Name")
        {
            name = std::get_if<std::string>(&value);
        }
        else if (key == "Model")
        {
            model = std::get_if<std::string>(&value);
        }
        else if (key == "PartNumber")
        {
            partNumber = std::get_if<std::string>(&value);
        }
        else if (key == "SerialNumber")
        {
            serialNumber = std::get_if<std::string>(&value);
        }
        else if (key == "Manufacturer")
        {
            manufacturer = std::get_if<std::string>(&value);
        }
        else if (key == "Version")
        {
            version = std::get_if<std::string>(&value);
        }
        else if (key == "BuildDate")
        {
            buildDate = std::get_if<std::string>(&value);
        }
        else if (key == "LocationContext")
        {
            locationContext = std::get_if<std::string>(&value);
        }
        else if (key == "LocationType")
        {
            locationType = std::get_if<std::string>(&value);
        }
        else if (key == "LocationCode")
        {
            locationCode = std::get_if<std::string>(&value);
        }
        else if (key == "PhysicalContext")
        {
            physicalContext = std::get_if<std::string>(&value);
        }
    }

    sdbusplus::message::object_path assemblyPath(assembly);
    std::string assemblyName = assemblyPath.filename();
    std::string memberId = assemblyMemberId(assemblyName);
    // Use Name from DBus if available, otherwise fall back to path filename
    std::string displayName = (name != nullptr) ? *name : assemblyName;

    assemblyObj["@odata.id"] = boost::urls::format(
        "/redfish/v1/Cables/{}/Assembly#/Assemblies/{}", cableId, memberId);
    assemblyObj["MemberId"] = memberId;
    assemblyObj["Name"] = displayName;

    if (model != nullptr)
    {
        assemblyObj["Model"] = *model;
    }
    if (partNumber != nullptr)
    {
        assemblyObj["PartNumber"] = *partNumber;
    }
    if (serialNumber != nullptr)
    {
        assemblyObj["SerialNumber"] = *serialNumber;
    }
    if (manufacturer != nullptr)
    {
        assemblyObj["Vendor"] = *manufacturer;
    }
    if (version != nullptr)
    {
        assemblyObj["Version"] = *version;
    }
    if (buildDate != nullptr)
    {
        assemblyObj["ProductionDate"] = *buildDate;
    }
    if (locationContext != nullptr)
    {
        assemblyObj["Location"]["PartLocationContext"] = *locationContext;
    }
    if (locationType != nullptr)
    {
        assemblyObj["Location"]["PartLocation"]["LocationType"] =
            redfish::dbus_utils::toLocationType(*locationType);
    }
    if (locationCode != nullptr)
    {
        assemblyObj["Location"]["PartLocation"]["ServiceLabel"] = *locationCode;
    }
    if (physicalContext != nullptr)
    {
        assemblyObj["PhysicalContext"] =
            redfish::dbus_utils::toPhysicalContext(*physicalContext);
    }

    asyncResp->res.jsonValue["Assemblies"].push_back(std::move(assemblyObj));
}

inline void handleCableAssemblyAssociations(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& cableId, const std::string& connectionName,
    const boost::system::error_code& ec,
    const std::variant<std::vector<std::string>>& resp)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("No assembly associations found");
        asyncResp->res.jsonValue["Assemblies"] = nlohmann::json::array();
        return;
    }

    const std::vector<std::string>* assemblyList =
        std::get_if<std::vector<std::string>>(&resp);
    if (assemblyList == nullptr || assemblyList->empty())
    {
        asyncResp->res.jsonValue["Assemblies"] = nlohmann::json::array();
        return;
    }

    std::vector<std::string> sortedAssemblies = *assemblyList;
    std::ranges::sort(sortedAssemblies, assemblySuffixLess);

    asyncResp->res.jsonValue["Assemblies"] = nlohmann::json::array();
    asyncResp->res.jsonValue["Assemblies@odata.count"] =
        sortedAssemblies.size();

    for (const std::string& assembly : sortedAssemblies)
    {
        BMCWEB_LOG_DEBUG("Found Assembly Path: {}", assembly);
        // GetAll with empty interface ("") returns properties from ALL
        // interfaces on this object in a single DBus call. This avoids
        // making separate async calls to each interface and then merging:
        //   - Inventory.Decorator.Asset: Model, PartNumber, SerialNumber,
        //                                Manufacturer (-> Vendor)
        //   - Inventory.Decorator.Revision: Version
        //   - Inventory.Item.Assembly: Name, BuildDate (-> ProductionDate)
        dbus::utility::async_method_call(
            [asyncResp, assembly,
             cableId](const boost::system::error_code& ec2,
                      const dbus::utility::DBusPropertiesMap& properties) {
                handleCableAssemblyProperties(asyncResp, assembly, cableId, ec2,
                                              properties);
            },
            connectionName, assembly, "org.freedesktop.DBus.Properties",
            "GetAll", "");
    }
}

inline void handleCableAssemblySubtree(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& cableId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("DBUS response error");
        redfish::messages::internalError(asyncResp->res);
        return;
    }

    for (const auto& [objectPath, serviceMap] : subtree)
    {
        sdbusplus::message::object_path path(objectPath);
        if (path.filename() != cableId)
        {
            continue;
        }

        if (serviceMap.empty())
        {
            BMCWEB_LOG_ERROR("Got 0 Connection names");
            continue;
        }

        const std::string& connectionName = serviceMap[0].first;
        setCableAssemblyResponse(asyncResp, cableId);

        dbus::utility::async_method_call(
            [asyncResp, cableId, connectionName](
                const boost::system::error_code& ec1,
                const std::variant<std::vector<std::string>>& resp) {
                handleCableAssemblyAssociations(asyncResp, cableId,
                                                connectionName, ec1, resp);
            },
            "xyz.openbmc_project.ObjectMapper", objectPath + "/assembly",
            "org.freedesktop.DBus.Properties", "Get",
            "xyz.openbmc_project.Association", "endpoints");
        return;
    }

    redfish::messages::resourceNotFound(asyncResp->res, "Cable", cableId);
}

inline void handleCableAssemblyGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& cableId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    BMCWEB_LOG_DEBUG("Cable Assembly doGet enter for {}", cableId);
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, cableAssemblyInterfaces,
        [asyncResp,
         cableId](const boost::system::error_code& ec,
                  const dbus::utility::MapperGetSubTreeResponse& subtree) {
            handleCableAssemblySubtree(asyncResp, cableId, ec, subtree);
        });
}

/**
 * Cable Assembly endpoint
 */
inline void requestRoutesCableAssembly(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Cables/<str>/Assembly/")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleCableAssemblyGet, std::ref(app)));
}

} // namespace nvidia_cables
