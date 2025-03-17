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

const std::vector<const char*> trustedComponentInterfaces = {
    "xyz.openbmc_project.Inventory.Item.Tpm"};

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
            "/redfish/v1/Chassis/" + componentID + "/Certificates/CertChain";

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
}

} // namespace redfish
