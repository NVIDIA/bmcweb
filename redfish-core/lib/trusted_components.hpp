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

#include "certificate_service.hpp"

#include <app.hpp>
#include <boost/algorithm/string/split.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/json_utils.hpp>
#include <utils/time_utils.hpp>

#include <iostream>
#include <map>
#include <regex>
#include <unordered_map>
#include <variant>
#include <vector>

namespace redfish
{

const std::vector<const char*> trustedComponentInterfaces = {
    "xyz.openbmc_project.Inventory.Item.Tpm"};

/**
 * @brief Structure to hold certificate-related data
 */
struct CertificateData
{
    std::optional<std::string> certString;
    std::optional<std::vector<std::string>> keyUsage;
    std::optional<std::string> issuer;
    std::optional<std::string> subject;
    std::optional<uint64_t> notAfter;
    std::optional<uint64_t> notBefore;
};

/**
 * @brief Fetches all properties for a trusted component
 * @param asyncResp Response object to update with fetched data
 * @param service D-Bus service name
 * @param path D-Bus object path
 * @param interface D-Bus interface to retrieve properties from
 */
inline void trustedComponentGetAllProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path,
    const std::string& interface)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const std::vector<
                        std::pair<std::string, dbus::utility::DbusVariantType>>&
                        propertiesList) {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "DBUS response error for trustedComponent properties");
            messages::internalError(asyncResp->res);
            return;
        }
        for (const auto& [propertyName, propertyVariant] : propertiesList)
        {
            if (propertyName == "Manufacturer" &&
                std::holds_alternative<std::string>(propertyVariant))
            {
                asyncResp->res.jsonValue["Manufacturer"] =
                    std::get<std::string>(propertyVariant);
            }
            else if (propertyName == "PrettyName" &&
                     std::holds_alternative<std::string>(propertyVariant))
            {
                asyncResp->res.jsonValue["Description"] =
                    std::get<std::string>(propertyVariant);
            }
            else if (propertyName == "Version" &&
                     std::holds_alternative<std::string>(propertyVariant))
            {
                asyncResp->res.jsonValue["FirmwareVersion"] =
                    std::get<std::string>(propertyVariant);
            }
        }
    },
        service, path, "org.freedesktop.DBus.Properties", "GetAll", interface);
}

/**
 * @brief Handles TPM implementation of GET requests for TrustedComponents
 * collection
 * @param asyncResp Response object
 * @param chassisID ID of the chassis containing trusted components
 * @param validChassisPath Valid path to the chassis
 * @param memberArray JSON array to add members to
 */
inline void handleTpmComponentsCollectionGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID,
    const std::optional<std::string>& validChassisPath,
    nlohmann::json& memberArray)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID,
         &memberArray](const boost::system::error_code ec,
                       const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to get TPM components: {}", ec.message());
            return;
        }

        for (const auto& [path, services] : subtree)
        {
            sdbusplus::message::object_path objPath(path);
            const std::string componentID = objPath.filename();
            if (componentID.empty())
            {
                continue;
            }

            memberArray.push_back(
                {{"@odata.id", "/redfish/v1/Chassis/" + chassisID +
                                   "/TrustedComponents/" + componentID}});
            asyncResp->res.jsonValue["Members@odata.count"] =
                memberArray.size();
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", *validChassisPath,
        static_cast<int32_t>(0), trustedComponentInterfaces);
}

/**
 * @brief Updates the collection with TPM components
 * @param asyncResp Response object
 * @param chassisID ID of the chassis containing trusted components
 * @param validChassisPath Valid path to the chassis
 * @param memberArray JSON array to add members to
 */
inline void
    updateTPMCollection(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& chassisID,
                        const std::optional<std::string>& validChassisPath,
                        nlohmann::json& memberArray)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID, validChassisPath,
         &memberArray](const boost::system::error_code ec,
                       const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_INFO("Failed to get TPM components: {}", ec.message());
        }
        else
        {
            for (const auto& [path, services] : subtree)
            {
                sdbusplus::message::object_path objPath(path);
                const std::string componentID = objPath.filename();
                if (componentID.empty())
                {
                    continue;
                }

                memberArray.push_back(
                    {{"@odata.id", "/redfish/v1/Chassis/" + chassisID +
                                       "/TrustedComponents/" + componentID}});
                asyncResp->res.jsonValue["Members@odata.count"] =
                    memberArray.size();
            }
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", *validChassisPath,
        static_cast<int32_t>(0), trustedComponentInterfaces);
}

/**
 * @brief Updates the collection with SPDM trusted components
 * @param asyncResp Response object
 * @param chassisID ID of the chassis containing trusted components
 * @param memberArray JSON array to add members to
 */
inline void updateSPDMTrustedComponents(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, nlohmann::json& memberArray)
{
    const std::array<const char*, 1> interfaces = {
        "xyz.openbmc_project.SPDM.Responder"};

    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID,
         &memberArray](const boost::system::error_code ec,
                       const crow::openbmc_mapper::GetSubTreeType& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to get SPDM subtree: {}", ec.message());
            messages::internalError(asyncResp->res);
            return;
        }

        for (const auto& [path, services] : subtree)
        {
            if (services.empty())
            {
                continue;
            }
            sdbusplus::asio::getProperty<bool>(
                *crow::connections::systemBus, services[0].first, path,
                "xyz.openbmc_project.Object.Enable", "Enabled",
                [asyncResp, chassisID, path, &memberArray](
                    const boost::system::error_code ec2, const bool& enabled) {
                if (ec2)
                {
                    BMCWEB_LOG_ERROR("Error reading Enabled property: {}",
                                     ec2.message());
                    return;
                }
                if (!enabled)
                {
                    BMCWEB_LOG_ERROR(
                        "SPDM Responder is not enabled for path: {}", path);
                    return;
                }
                sdbusplus::message::object_path objPath(path);
                const std::string componentID = objPath.filename();
                if (componentID.empty())
                {
                    return;
                }
                memberArray.push_back(
                    {{"@odata.id", "/redfish/v1/Chassis/" + chassisID +
                                       "/TrustedComponents/" + componentID}});
                asyncResp->res.jsonValue["Members@odata.count"] =
                    memberArray.size();
            });
        }
    },
        dbus_utils::mapperBusName, dbus_utils::mapperObjectPath,
        dbus_utils::mapperIntf, "GetSubTree", "/xyz/openbmc_project/SPDM", 0,
        interfaces);
}

/**
 * @brief Handles GET requests for TrustedComponents collection
 * @param app Crow app
 * @param req HTTP request
 * @param asyncResp Response object
 * @param chassisID ID of the chassis containing trusted components
 */
inline void handleTrustedComponentsCollectionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisID,
        [asyncResp,
         chassisID](const std::optional<std::string>& validChassisPath) {
        if (!validChassisPath)
        {
            BMCWEB_LOG_ERROR("Cannot get validChassisPath");
            messages::internalError(asyncResp->res);
            return;
        }
        const std::string collectionPath = "/redfish/v1/Chassis/" + chassisID +
                                           "/TrustedComponents";

        asyncResp->res.jsonValue["@odata.id"] = collectionPath;
        asyncResp->res.jsonValue["@odata.type"] =
            "#TrustedComponentCollection.TrustedComponentCollection";
        asyncResp->res.jsonValue["Name"] = chassisID + "_TrustedComponents";
        asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
        asyncResp->res.jsonValue["Members@odata.count"] = 0;

        nlohmann::json& memberArray = asyncResp->res.jsonValue["Members"];

        updateTPMCollection(asyncResp, chassisID, validChassisPath,
                            memberArray);

        if (chassisID.find(PLATFORMCHASSISNAME) == std::string::npos)
        {
            BMCWEB_LOG_DEBUG(
                "Chassis ID '{}' does not contain platform name '{}'; skipping SPDM resources",
                chassisID, PLATFORMCHASSISNAME);
            return;
        }

        updateSPDMTrustedComponents(asyncResp, chassisID, memberArray);
    });
}

/**
 * @brief Fetches inventory properties for a trusted component
 * @param asyncResp Response object
 * @param chassisID ID of the chassis
 * @param componentID ID of the component
 * @param inventoryPath D-Bus path to the inventory object
 */
inline void fetchInventoryProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& componentID,
    const std::string& inventoryPath)
{
    if (chassisID.empty() || componentID.empty() || inventoryPath.empty())
    {
        BMCWEB_LOG_ERROR(
            "Invalid input parameters for fetchInventoryProperties");
        messages::internalError(asyncResp->res);
        return;
    }

    const std::array<const char*, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Decorator.Asset"};

    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID, componentID, inventoryPath](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                servicesToInterfaces) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("ObjectMapper GetObject error for {}: {}",
                             inventoryPath, ec.message());
            messages::internalError(asyncResp->res);
            return;
        }

        if (servicesToInterfaces.empty())
        {
            BMCWEB_LOG_ERROR("No services found for path: {}", inventoryPath);
            messages::internalError(asyncResp->res);
            return;
        }

        const std::string& service = servicesToInterfaces[0].first;

        crow::connections::systemBus->async_method_call(
            [asyncResp, chassisID, componentID, service, inventoryPath](
                const boost::system::error_code ecInv,
                const std::vector<std::pair<
                    std::string, dbus::utility::DbusVariantType>>& props) {
            if (ecInv)
            {
                BMCWEB_LOG_ERROR(
                    "GetAll inventory properties failed for service {} path {}: {}",
                    service, inventoryPath, ecInv.message());
                messages::internalError(asyncResp->res);
                return;
            }

            for (const auto& [propName, propVal] : props)
            {
                if (const std::string* sVal =
                        std::get_if<std::string>(&propVal))
                {
                    if (propName == "Manufacturer")
                    {
                        asyncResp->res.jsonValue["Manufacturer"] = *sVal;
                    }
                    else if (propName == "SerialNumber")
                    {
                        asyncResp->res.jsonValue["SerialNumber"] = *sVal;
                    }
                    else if (propName == "UUID")
                    {
                        asyncResp->res.jsonValue["UUID"] = *sVal;
                    }
                }
            }
        },
            service, inventoryPath, "org.freedesktop.DBus.Properties", "GetAll",
            "");
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", inventoryPath,
        interfaces);
}

/**
 * @brief Sets up Links section for a trusted component
 * @param asyncResp Response object
 * @param chassisID ID of the chassis
 * @param componentID ID of the component
 * @param inventoryPath D-Bus path to the inventory object
 */
inline void fetchTrustedComponentLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& chassisID,
    const std::string& componentID, const std::string& inventoryPath)
{
    asyncResp->res.jsonValue["Links"]["ComponentIntegrity"] = {
        {{"@odata.id", "/redfish/v1/ComponentIntegrity/" + componentID}}};

    std::string objPath = inventoryPath + "/inventory";

    chassis_utils::getAssociationEndpoint(
        objPath,
        [objPath, asyncResp](const bool& status, const std::string& ep) {
        if (!status)
        {
            BMCWEB_LOG_DEBUG("Unable to get the association endpoint for {}",
                             objPath);
            nlohmann::json& componentsProtectedArray =
                asyncResp->res.jsonValue["Links"]["ComponentsProtected"];
            componentsProtectedArray = nlohmann::json::array();

            return;
        }

        chassis_utils::getRedfishURL(
            ep, [ep, asyncResp](const bool& status, const std::string& url) {
            nlohmann::json& componentsProtectedArray =
                asyncResp->res.jsonValue["Links"]["ComponentsProtected"];
            componentsProtectedArray = nlohmann::json::array();

            if (!status)
            {
                BMCWEB_LOG_DEBUG("Unable to get the Redfish URL for {}", ep);
                return;
            }

            componentsProtectedArray.push_back({{"@odata.id", url}});

            if (asyncResp->res.jsonValue.contains("TrustedComponentType") &&
                asyncResp->res.jsonValue["TrustedComponentType"] ==
                    "Integrated")
            {
                asyncResp->res.jsonValue["Links"]["IntegratedInto"] =
                    asyncResp->res.jsonValue["Links"]["ComponentsProtected"];
            }
        });
    });
}

/**
 * @brief Fetches associations for a trusted component
 * @param asyncResp Response object
 * @param chassisID ID of the chassis
 * @param componentID ID of the component
 * @param spdmService D-Bus service name
 * @param spdmPath D-Bus path to the component
 */
inline void fetchAssociations(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& componentID,
    const std::string& spdmService, const std::string& spdmPath)
{
    if (chassisID.empty() || componentID.empty() || spdmService.empty() ||
        spdmPath.empty())
    {
        BMCWEB_LOG_ERROR("Invalid input parameters for fetchAssociations");
        messages::internalError(asyncResp->res);
        return;
    }

    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID, componentID, spdmPath,
         spdmService](const boost::system::error_code ecAssoc,
                      const dbus::utility::DbusVariantType& assocVar) {
        if (ecAssoc)
        {
            BMCWEB_LOG_ERROR("GetProperty(Associations) failed: {}",
                             ecAssoc.message());
            messages::internalError(asyncResp->res);
            return;
        }

        using AssociationsType =
            std::vector<std::tuple<std::string, std::string, std::string>>;
        const AssociationsType* assocPtr =
            std::get_if<AssociationsType>(&assocVar);
        if (!assocPtr)
        {
            BMCWEB_LOG_ERROR("Associations property invalid or missing");
            messages::internalError(asyncResp->res);
            return;
        }

        std::optional<std::string> inventoryPath;
        for (const auto& [forward, reverse, endpoint] : *assocPtr)
        {
            if (forward == "inventory_object")
            {
                inventoryPath = endpoint;
                break;
            }
        }
        if (!inventoryPath)
        {
            BMCWEB_LOG_ERROR("No 'inventory_object' association found");
            messages::internalError(asyncResp->res);
            return;
        }

        fetchInventoryProperties(asyncResp, chassisID, componentID,
                                 *inventoryPath);
        fetchTrustedComponentLinks(asyncResp, chassisID, componentID,
                                   *inventoryPath);
    },
        spdmService, spdmPath, "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association.Definitions", "Associations");
}

/**
 * @brief Fetches component type and associations for a trusted component
 * @param asyncResp Response object
 * @param chassisID ID of the chassis
 * @param componentID ID of the component
 * @param services Available D-Bus services and interfaces
 */
inline void fetchComponentTypeAndAssociations(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& componentID,
    const std::vector<std::pair<std::string, std::vector<std::string>>>&
        services)
{
    if (chassisID.empty() || componentID.empty() || services.empty())
    {
        BMCWEB_LOG_ERROR(
            "Invalid input parameters for fetchComponentTypeAndAssociations");
        messages::internalError(asyncResp->res);
        return;
    }

    const std::string& spdmService = services.front().first;
    std::string spdmPath = "/xyz/openbmc_project/SPDM/" + componentID;

    if (spdmService.empty())
    {
        BMCWEB_LOG_ERROR("Empty service name in services vector");
        messages::internalError(asyncResp->res);
        return;
    }

    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID, componentID, spdmService,
         spdmPath](const boost::system::error_code ecType,
                   const dbus::utility::DbusVariantType& typeVal) {
        if (ecType)
        {
            BMCWEB_LOG_ERROR("Error reading property 'Type': {}",
                             ecType.message());
            messages::internalError(asyncResp->res);
            return;
        }
        const std::string* strVal = std::get_if<std::string>(&typeVal);
        if (!strVal)
        {
            BMCWEB_LOG_ERROR("'Type' property not returned as string");
            messages::internalError(asyncResp->res);
            return;
        }

        size_t pos = strVal->rfind('.');
        if (pos != std::string::npos && pos + 1 < strVal->size())
        {
            asyncResp->res.jsonValue["TrustedComponentType"] =
                strVal->substr(pos + 1);
        }
        else
        {
            asyncResp->res.jsonValue["TrustedComponentType"] = *strVal;
        }

        asyncResp->res.jsonValue["Certificates"]["@odata.id"] =
            "/redfish/v1/Chassis/" + chassisID + "/TrustedComponents/" +
            componentID + "/Certificates";

        fetchAssociations(asyncResp, chassisID, componentID, spdmService,
                          spdmPath);
    },
        spdmService, spdmPath, "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Inventory.Item.TrustedComponent",
        "TrustedComponentType");
}

/**
 * @brief Prepares the response structure for a trusted component
 * @param asyncResp Response object
 * @param chassisID ID of the chassis
 * @param componentID ID of the component
 * @param services Available D-Bus services and interfaces
 */
inline void finalizeTrustedComponent(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& componentID,
    const std::vector<std::pair<std::string, std::vector<std::string>>>&
        services)
{
    if (chassisID.empty() || componentID.empty() || services.empty())
    {
        BMCWEB_LOG_ERROR(
            "Invalid input parameters for finalizeTrustedComponent");
        messages::internalError(asyncResp->res);
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] =
        "#TrustedComponent.v1_0_0.TrustedComponent";
    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/Chassis/" + chassisID +
                                            "/TrustedComponents/" + componentID;
    asyncResp->res.jsonValue["Id"] = componentID;
    asyncResp->res.jsonValue["Name"] = componentID;
    fetchComponentTypeAndAssociations(asyncResp, chassisID, componentID,
                                      services);
}

/**
 * @brief Handles TPM implementation of GET request for a specific
 * TrustedComponent
 * @param asyncResp Response object
 * @param chassisID ID of the chassis
 * @param componentID ID of the component
 * @param validChassisPath Valid path to the chassis
 */
inline void
    handleTpmComponentGet(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& chassisID,
                          const std::string& componentID,
                          const std::optional<std::string>& validChassisPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID,
         componentID](const boost::system::error_code ec,
                      const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("error_code = {}", ec);
            BMCWEB_LOG_ERROR("error msg = {}", ec.message());
            messages::internalError(asyncResp->res);
            return;
        }

        for (const auto& [path, services] : subtree)
        {
            sdbusplus::message::object_path opath(path);
            std::string componentName = opath.filename();
            if (componentName != componentID)
            {
                continue;
            }

            asyncResp->res.jsonValue["@odata.type"] =
                "#TrustedComponent.v1_0_0.TrustedComponent";
            asyncResp->res.jsonValue["@odata.id"] =
                std::string("/redfish/v1/Chassis/")
                    .append(chassisID)
                    .append("/TrustedComponents/")
                    .append(componentID);
            asyncResp->res.jsonValue["Id"] = componentID;
            asyncResp->res.jsonValue["Name"] = componentID;
            asyncResp->res.jsonValue["TrustedComponentType"] = "Discrete";
            for (const auto& [service, interfaces] : services)
            {
                for (const auto& interface : interfaces)
                {
                    if (interface ==
                            "xyz.openbmc_project.Inventory.Decorator.Asset" ||
                        interface == "xyz.openbmc_project.Inventory.Item" ||
                        interface == "xyz.openbmc_project.Software.Version")
                    {
                        trustedComponentGetAllProperties(asyncResp, service,
                                                         path, interface);
                    }
                }
            }
            return;
        }

        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentID);
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", *validChassisPath,
        static_cast<int32_t>(0), trustedComponentInterfaces);
}

/**
 * @brief Trim leading and trailing spaces from a string
 * @param str The string to trim
 * @return Trimmed string
 */
static std::string trimSpaces(std::string str)
{
    if (str.empty())
    {
        return "";
    }

    str.erase(0, str.find_first_not_of(" "));
    str.erase(str.find_last_not_of(" ") + 1);
    return str;
}

/**
 * @brief Extract a specific field value from a certificate string
 * @param str Certificate string containing fields in format "FIELD=value"
 * @param fieldName Name of the field to extract
 * @return Value of the field, or empty string if not found
 */
static std::string extractField(const std::string& str,
                                const std::string& fieldName)
{
    std::string searchStr = fieldName + "=";
    size_t pos = str.find(searchStr);
    if (pos == std::string::npos)
    {
        return "";
    }
    pos += searchStr.length();
    size_t endPos = str.find(',', pos);
    if (endPos == std::string::npos)
    {
        return str.substr(pos);
    }
    return str.substr(pos, endPos - pos);
}

/**
 * @brief Parse certificate fields into structured format
 * @param certStr Certificate string containing fields
 * @return Map of field names to their values
 */
static std::unordered_map<std::string, std::string>
    parseCertificateFields(const std::string& certStr)
{
    std::unordered_map<std::string, std::string> fields;
    std::string remaining = trimSpaces(certStr);

    while (!remaining.empty())
    {
        size_t equalsPos = remaining.find('=');
        if (equalsPos == std::string::npos)
        {
            break;
        }

        std::string fieldName(remaining.substr(0, equalsPos));
        remaining = trimSpaces(remaining.substr(equalsPos + 1));

        size_t commaPos = remaining.find(',');
        std::string value;
        if (commaPos == std::string::npos)
        {
            value = std::string(remaining);
            remaining.clear();
        }
        else
        {
            value = std::string(remaining.substr(0, commaPos));
            remaining = trimSpaces(remaining.substr(commaPos + 1));
        }

        if (!fieldName.empty())
        {
            fields[fieldName] = value;
        }
    }

    return fields;
}

/**
 * @brief Map of mbedtls field names to Redfish field names
 */
static const std::unordered_map<std::string, std::string> certFieldMap = {
    {"C", "Country"},
    {"ST", "State"},
    {"L", "City"},
    {"O", "Organization"},
    {"OU", "OrganizationalUnit"},
    {"CN", "CommonName"}};

/**
 * @brief Constructs the complete certificate response in Redfish format
 *
 * This function builds the final certificate response by combining SPDM
 * responder properties (like slot information) with certificate details. It
 * formats all certificate data into the proper Redfish JSON structure.
 *
 * @param req The HTTP request object
 * @param asyncResp The asynchronous response object to populate
 * @param chassisID The ID of the chassis containing the component
 * @param componentID The ID of the trusted component
 * @param certificateID The ID of the certificate being processed
 * @param certPath The D-Bus path to the certificate object
 * @param certData Structure containing all certificate-related data
 */
static void constructCertificateResponse(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& componentID,
    const std::string& certificateID, const std::string& certPath,
    const CertificateData& certData)
{
    crow::connections::systemBus->async_method_call(
        [req, asyncResp, chassisID, componentID, certificateID, certPath,
         certData](
            const boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, dbus::utility::DbusVariantType>& responderProps) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error for Responder interface: {}",
                             ec);
            messages::internalError(asyncResp->res);
            return;
        }

        const uint8_t* slot = nullptr;
        auto itSlot = responderProps.find("Slot");
        if (itSlot != responderProps.end())
        {
            slot = std::get_if<uint8_t>(&itSlot->second);
            if (slot != nullptr)
            {
                BMCWEB_LOG_DEBUG("Slot ID:{}", *slot);
            }
        }

        // Construct response with basic properties
        asyncResp->res.jsonValue = {
            {"@odata.id", "/redfish/v1/Chassis/" + chassisID +
                              "/TrustedComponents/" + componentID +
                              "/Certificates/" + certificateID},
            {"@odata.type", "#Certificate.v1_5_0.Certificate"},
            {"Id", certificateID},
            {"Name", componentID + "_Certificate_" + certificateID},
            {"CertificateType", "PEMchain"},
            {"CertificateUsageTypes", nlohmann::json::array({"Device"})}};

        if (certData.certString.has_value())
        {
            asyncResp->res.jsonValue["CertificateString"] =
                certData.certString.value();
        }

        if (slot != nullptr)
        {
            asyncResp->res.jsonValue["SPDM"] = {{"SlotId", *slot}};
        }

        if (certData.keyUsage.has_value() && !certData.keyUsage.value().empty())
        {
            asyncResp->res.jsonValue["KeyUsage"] = certData.keyUsage.value();
        }

        if (certData.issuer.has_value())
        {
            auto issuerFields = parseCertificateFields(certData.issuer.value());
            nlohmann::json issuerJson;

            // Only add fields that are present in the certificate
            for (const auto& [mbedtlsField, redfishField] : certFieldMap)
            {
                std::string value = extractField(certData.issuer.value(),
                                                 mbedtlsField);
                if (!value.empty())
                {
                    issuerJson[redfishField] = value;
                }
            }

            if (!issuerJson.empty())
            {
                asyncResp->res.jsonValue["Issuer"] = issuerJson;
            }
        }

        if (certData.subject.has_value())
        {
            auto subjectFields =
                parseCertificateFields(certData.subject.value());
            nlohmann::json subjectJson;

            // Add fields that are present in the certificate
            for (const auto& [mbedtlsField, redfishField] : certFieldMap)
            {
                std::string value = extractField(certData.subject.value(),
                                                 mbedtlsField);
                if (!value.empty())
                {
                    subjectJson[redfishField] = value;
                }
            }

            if (!subjectJson.empty())
            {
                asyncResp->res.jsonValue["Subject"] = subjectJson;
            }
        }

        if (certData.notAfter.has_value() && certData.notAfter.value() != 0)
        {
            asyncResp->res.jsonValue["ValidNotAfter"] =
                redfish::time_utils::getDateTimeUintMs(
                    certData.notAfter.value());
        }

        if (certData.notBefore.has_value() && certData.notBefore.value() != 0)
        {
            asyncResp->res.jsonValue["ValidNotBefore"] =
                redfish::time_utils::getDateTimeUintMs(
                    certData.notBefore.value());
        }
    },
        "xyz.openbmc_project.SPDM", certPath, "org.freedesktop.DBus.Properties",
        "GetAll", "xyz.openbmc_project.SPDM.Responder");
}

/**
 * @brief Handles the certificate properties from D-Bus
 *
 * This function processes the certificate properties retrieved from D-Bus and
 * extracts key information such as the certificate string, key usage, issuer,
 * subject, and validity dates. It validates the properties and prepares them
 * for the response.
 *
 * @param req The HTTP request object
 * @param asyncResp The asynchronous response object to populate with
 * certificate data
 * @param chassisID The ID of the chassis containing the component
 * @param componentID The ID of the trusted component
 * @param certificateID The ID of the certificate being processed
 * @param certPath The D-Bus path to the certificate object
 * @param ec Error code from the D-Bus call
 * @param certProps Map of certificate properties retrieved from D-Bus
 * @param certData Structure to store the processed certificate data
 */
static void handleCertificateProperties(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& componentID,
    const std::string& certificateID, const std::string& certPath,
    const boost::system::error_code ec,
    const boost::container::flat_map<std::string,
                                     dbus::utility::DbusVariantType>& certProps,
    CertificateData& certData)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error for Certificate interface: {}",
                         ec);
        messages::internalError(asyncResp->res);
        return;
    }

    auto itCertString = certProps.find("CertificateString");
    if (itCertString == certProps.end())
    {
        BMCWEB_LOG_ERROR(
            "CertificateString property not found for component: {}",
            componentID);
        messages::resourceNotFound(asyncResp->res, "Certificate",
                                   certificateID);
        return;
    }
    const std::string* certificateString =
        std::get_if<std::string>(&itCertString->second);
    if (certificateString == nullptr)
    {
        BMCWEB_LOG_ERROR(
            "CertificateString property has wrong type for component: {}",
            componentID);
        messages::internalError(asyncResp->res);
        return;
    }
    certData.certString = *certificateString;

    auto itKeyUsage = certProps.find("KeyUsage");
    if (itKeyUsage != certProps.end())
    {
        if (const std::vector<std::string>* value =
                std::get_if<std::vector<std::string>>(&itKeyUsage->second))
        {
            certData.keyUsage = *value;
        }
    }

    auto itIssuer = certProps.find("Issuer");
    if (itIssuer != certProps.end())
    {
        if (const std::string* value =
                std::get_if<std::string>(&itIssuer->second))
        {
            certData.issuer = *value;
        }
    }

    auto itSubject = certProps.find("Subject");
    if (itSubject != certProps.end())
    {
        if (const std::string* value =
                std::get_if<std::string>(&itSubject->second))
        {
            certData.subject = *value;
        }
    }

    auto itValidNotAfter = certProps.find("ValidNotAfter");
    if (itValidNotAfter != certProps.end())
    {
        if (const uint64_t* value =
                std::get_if<uint64_t>(&itValidNotAfter->second))
        {
            certData.notAfter = *value;
        }
    }

    auto itValidNotBefore = certProps.find("ValidNotBefore");
    if (itValidNotBefore != certProps.end())
    {
        if (const uint64_t* value =
                std::get_if<uint64_t>(&itValidNotBefore->second))
        {
            certData.notBefore = *value;
        }
    }

    constructCertificateResponse(req, asyncResp, chassisID, componentID,
                                 certificateID, certPath, certData);
}

/**
 * @brief Retrieves certificate information for a trusted component
 *
 * This function retrieves the certificate details for a specific trusted
 * component, including the certificate string, key usage, issuer, subject, and
 * validity dates. It makes D-Bus calls to fetch both certificate and SPDM
 * responder properties.
 *
 * @param req The HTTP request object
 * @param asyncResp The asynchronous response object to populate with
 * certificate data
 * @param componentID The ID of the trusted component
 * @param chassisID The ID of the chassis containing the component
 * @param certificateID The ID of the certificate to retrieve
 */
static void getTrustedComponentCertificate(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& componentID, const std::string& chassisID,
    const std::string& certificateID)
{
    std::string certPath = "/xyz/openbmc_project/SPDM/" + componentID;
    // Construct the D-Bus path for the SPDM certificate object
    CertificateData certData;
    crow::connections::systemBus->async_method_call(
        [req, asyncResp, chassisID, componentID, certificateID, certPath,
         certData](
            const boost::system::error_code ec,
            const boost::container::flat_map<std::string,
                                             dbus::utility::DbusVariantType>&
                certProps) mutable {
        handleCertificateProperties(req, asyncResp, chassisID, componentID,
                                    certificateID, certPath, ec, certProps,
                                    certData);
    },
        "xyz.openbmc_project.SPDM", certPath, "org.freedesktop.DBus.Properties",
        "GetAll", "xyz.openbmc_project.Certs.Certificate");
}

/**
 * @brief Common function to check if a component is enabled
 * @param asyncResp Response object
 * @param path D-Bus object path
 * @param services Vector of D-Bus services and their interfaces
 * @param callback Function to call if component is enabled
 */
static void checkComponentEnabled(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path,
    const std::vector<std::pair<std::string, std::vector<std::string>>>&
        services,
    std::function<void()> callback)
{
    if (services.empty())
    {
        BMCWEB_LOG_ERROR("No services available for path: {}", path);
        messages::internalError(asyncResp->res);
        return;
    }

    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, services[0].first, path,
        "xyz.openbmc_project.Object.Enable", "Enabled",
        [asyncResp, path, callback](const boost::system::error_code ec,
                                    const bool& enabled) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Error reading Enabled property: {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }
        if (!enabled)
        {
            messages::resourceNotFound(
                asyncResp->res, "TrustedComponent",
                std::filesystem::path(path).filename().string());
            return;
        }
        callback();
    });
}

/**
 * @brief Searches for a component in the subtree by its ID
 * @param subtree The D-Bus subtree to search in
 * @param componentID The ID of the component to find
 * @return An optional pair containing the component's path and its associated
 * services if found, std::nullopt if the component is not found
 */
static std::optional<std::pair<
    std::string, std::vector<std::pair<std::string, std::vector<std::string>>>>>
    findComponentInSubtree(const crow::openbmc_mapper::GetSubTreeType& subtree,
                           const std::string& componentID)
{
    for (const auto& [path, services] : subtree)
    {
        if (services.empty())
        {
            continue;
        }

        sdbusplus::message::object_path objPath(path);
        if (objPath.filename() == componentID)
        {
            return std::make_pair(path, services);
        }
    }

    BMCWEB_LOG_ERROR("Component not found: {}", componentID);
    return std::nullopt;
}

/**
 * @brief Common function to validate chassis and platform name
 * @param asyncResp Response object
 * @param chassisID ID of the chassis
 * @param callback Function to call if validation succeeds
 */
static void validateChassisAndPlatform(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, std::function<void()> callback)
{
    bool hasPlatformName =
        (chassisID.find(PLATFORMCHASSISNAME) != std::string::npos);

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisID,
        [asyncResp, chassisID, hasPlatformName,
         callback](const std::optional<std::string>& validChassisPath) {
        if (!validChassisPath)
        {
            BMCWEB_LOG_ERROR("Cannot get validChassisPath");
            messages::resourceNotFound(asyncResp->res, "Chassis", chassisID);
            return;
        }

        if (!hasPlatformName)
        {
            BMCWEB_LOG_DEBUG(
                "Chassis ID '{}' does not contain platform name '{}'",
                chassisID, PLATFORMCHASSISNAME);
            messages::resourceNotFound(asyncResp->res, "Chassis", chassisID);
            return;
        }

        callback();
    });
}

/**
 * @brief Handles GET request for a trusted component's certificate
 *
 * This function processes requests to retrieve certificate information for a
 * specific trusted component. It validates the chassis and component IDs,
 * checks if the component is enabled, and retrieves the certificate details if
 * found.
 *
 * @param app The Crow application instance
 * @param req The HTTP request object
 * @param asyncResp The asynchronous response object
 * @param chassisID The ID of the chassis containing the component
 * @param componentID The ID of the trusted component
 * @param certificateID The ID of the certificate to retrieve (must be
 * "CertChain")
 */
inline void handleTrustedComponentCertificateGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& componentID,
    const std::string& certificateID)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (certificateID != "CertChain")
    {
        BMCWEB_LOG_DEBUG("Not a valid Certificate ID");
        messages::resourceNotFound(asyncResp->res, "Certificate",
                                   certificateID);
        return;
    }

    validateChassisAndPlatform(
        asyncResp, chassisID,
        [req, asyncResp, chassisID, componentID, certificateID]() {
        const std::array<const char*, 1> interfaces = {
            "xyz.openbmc_project.SPDM.Responder"};

        crow::connections::systemBus->async_method_call(
            [req, asyncResp, chassisID, componentID, certificateID](
                const boost::system::error_code ec,
                const crow::openbmc_mapper::GetSubTreeType& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("GetSubTree error: {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }

            auto result = findComponentInSubtree(subtree, componentID);
            if (!result.has_value())
            {
                messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                           componentID);
                return;
            }

            checkComponentEnabled(
                asyncResp, result->first, result->second,
                [req, asyncResp, componentID, chassisID, certificateID]() {
                getTrustedComponentCertificate(req, asyncResp, componentID,
                                               chassisID, certificateID);
            });
        },
            dbus_utils::mapperBusName, dbus_utils::mapperObjectPath,
            dbus_utils::mapperIntf, "GetSubTree", "/xyz/openbmc_project/SPDM",
            0, interfaces);
    });
}

/**
 * @brief Handles collection of certificates for a trusted component
 * @param app Crow app
 * @param req HTTP request
 * @param asyncResp Response object
 * @param chassisID ID of the chassis containing trusted components
 * @param componentID ID of the trusted component
 */
inline void handleTrustedComponentCertificatesCollectionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& componentID)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    validateChassisAndPlatform(asyncResp, chassisID,
                               [req, asyncResp, chassisID, componentID]() {
        const std::array<const char*, 1> interfaces = {
            "xyz.openbmc_project.SPDM.Responder"};

        crow::connections::systemBus->async_method_call(
            [req, asyncResp, chassisID,
             componentID](const boost::system::error_code ec,
                          const crow::openbmc_mapper::GetSubTreeType& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("GetSubTree error: {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }

            auto result = findComponentInSubtree(subtree, componentID);
            if (!result.has_value())
            {
                messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                           componentID);
                return;
            }

            checkComponentEnabled(asyncResp, result->first, result->second,
                                  [asyncResp, chassisID, componentID]() {
                std::string url = "/redfish/v1/Chassis/";
                url.append(chassisID)
                    .append("/TrustedComponents/")
                    .append(componentID)
                    .append("/Certificates");
                asyncResp->res.jsonValue = {
                    {"@odata.id", url},
                    {"@odata.type",
                     "#CertificateCollection.CertificateCollection"},
                    {"Name", "Chassis Certificate Collection"},
                    {"Description", "A Collection of Certificate instances"}};

                nlohmann::json& members = asyncResp->res.jsonValue["Members"];
                members = nlohmann::json::array();
                members.push_back({{"@odata.id", url + "/CertChain"}});

                asyncResp->res.jsonValue["Members@odata.count"] =
                    members.size();
            });
        },
            dbus_utils::mapperBusName, dbus_utils::mapperObjectPath,
            dbus_utils::mapperIntf, "GetSubTree", "/xyz/openbmc_project/SPDM",
            0, interfaces);
    });
}

/**
 * @brief Handles GET requests for a specific TrustedComponent
 * @param app Crow app
 * @param req HTTP request
 * @param asyncResp Response object
 * @param chassisID ID of the chassis
 * @param componentID ID of the component to retrieve
 */
inline void handleTrustedComponentGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& componentID)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    bool hasPlatformName =
        (chassisID.find(PLATFORMCHASSISNAME) != std::string::npos);

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisID,
        [asyncResp, chassisID, componentID,
         hasPlatformName](const std::optional<std::string>& validChassisPath) {
        if (!validChassisPath)
        {
            BMCWEB_LOG_ERROR("Cannot get validChassisPath");
            messages::internalError(asyncResp->res);
            return;
        }

        if (!hasPlatformName)
        {
            handleTpmComponentGet(asyncResp, chassisID, componentID,
                                  validChassisPath);
            return;
        }

        static const std::array<const char*, 1> interfaces = {
            "xyz.openbmc_project.SPDM.Responder"};

        crow::connections::systemBus->async_method_call(
            [asyncResp, chassisID, componentID, validChassisPath](
                const boost::system::error_code ec,
                const crow::openbmc_mapper::GetSubTreeType& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("GetSubTree for SPDM objects failed: {}",
                                 ec.message());
                messages::internalError(asyncResp->res);
                return;
            }

            bool foundMatchingComponent = false;
            std::string matchingPath;
            std::vector<std::pair<std::string, std::vector<std::string>>>
                matchingServices;

            for (const auto& [path, services] : subtree)
            {
                if (services.empty())
                {
                    continue;
                }

                sdbusplus::message::object_path opath(path);
                if (opath.filename() == componentID)
                {
                    foundMatchingComponent = true;
                    matchingPath = path;
                    matchingServices = services;
                    break;
                }
            }

            if (!foundMatchingComponent)
            {
                handleTpmComponentGet(asyncResp, chassisID, componentID,
                                      validChassisPath);
                return;
            }

            sdbusplus::asio::getProperty<bool>(
                *crow::connections::systemBus, matchingServices[0].first,
                matchingPath, "xyz.openbmc_project.Object.Enable", "Enabled",
                [asyncResp, chassisID, componentID, matchingServices](
                    const boost::system::error_code ec2, const bool& enabled) {
                if (ec2)
                {
                    BMCWEB_LOG_ERROR("Error reading Enabled property: {}",
                                     ec2.message());
                    messages::internalError(asyncResp->res);
                    return;
                }
                if (!enabled)
                {
                    messages::resourceNotFound(asyncResp->res,
                                               "TrustedComponent", componentID);
                    return;
                }

                finalizeTrustedComponent(asyncResp, chassisID, componentID,
                                         matchingServices);
            });
        },
            dbus_utils::mapperBusName, dbus_utils::mapperObjectPath,
            dbus_utils::mapperIntf, "GetSubTree", "/xyz/openbmc_project/SPDM",
            0, interfaces);
    });
}

/**
 * @brief Registers REST routes for TrustedComponents
 * @param app Crow app to register routes on
 */
inline void requestRoutesTrustedComponents(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/TrustedComponents/")
        .privileges(redfish::privileges::privilegeSetLogin)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleTrustedComponentsCollectionGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/")
        .privileges(redfish::privileges::privilegeSetLogin)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleTrustedComponentGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Certificates/<str>/")
        .privileges(redfish::privileges::getCertificate)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleTrustedComponentCertificateGet, std::ref(app)));

    BMCWEB_ROUTE(
        app, "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/Certificates/")
        .privileges(redfish::privileges::getCertificateCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleTrustedComponentCertificatesCollectionGet, std::ref(app)));
}

} // namespace redfish
