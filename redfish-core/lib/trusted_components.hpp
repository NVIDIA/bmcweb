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

#include "utils/certificate_utils.hpp"
#include "nvidia_dbus_utility.hpp"

#include <app.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <utils/chassis_utils.hpp>
#include <utils/collection.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/json_utils.hpp>
#include <utils/nvidia_time_utils.hpp>
#include <utils/time_utils.hpp>

#include <iostream>
#include <map>
#include <regex>
#include <unordered_map>
#include <variant>
#include <vector>

namespace redfish
{

static void isComponentEnabled(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const dbus::utility::MapperGetSubTreeResponse& subtree,
    const std::string& endpoint, const bool& isCollection,
    const std::function<void()>& callback);

const std::array<std::string_view, 1> trustedComponentInterfaces = {
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
 * @brief Gets the associated endpoint for a chassis
 * @param asyncResp Response object
 * @param chassisID ID of the chassis
 * @param callback Function to call with the endpoint if found and a boolean
 * indicating existence
 */
inline void getChassisAssociatedEndpoint(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID,
    const std::function<void(const std::string&, bool)>& callback)
{
    std::string associatedChassisPath =
        "/xyz/openbmc_project/inventory/system/chassis/";
    associatedChassisPath.append(chassisID);
    associatedChassisPath.append("/associated_chassis");

    chassis_utils::getAssociationEndpoint(
        associatedChassisPath,
        [asyncResp, chassisID,
         callback](const bool& status, const std::string& ep) {
            if (!status)
            {
                BMCWEB_LOG_ERROR(
                    "No associated_chassis endpoint found for chassis: {}",
                    chassisID);
                callback("", false);
                return;
            }
            callback(ep, true);
        });
}

/**
 * @brief Helper function to check if TPM components exist and add
 * TrustedComponents link
 * @param asyncResp Response object
 * @param chassisID ID of the chassis
 * @param validChassisPath Valid chassis path for TPM discovery
 */
inline void checkTPMComponentsAndAddLink(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        return;
    }

    crow::connections::systemBus->async_method_call(
        [asyncResp,
         chassisID](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (!ec && !subtree.empty())
            {
                asyncResp->res.jsonValue["TrustedComponents"]["@odata.id"] =
                    boost::urls::format(
                        "/redfish/v1/Chassis/{}/TrustedComponents", chassisID);
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", *validChassisPath,
        static_cast<int32_t>(0), trustedComponentInterfaces);
}

/**
 * @brief Validates if a component ID follows the required format and matches
 * the endpoint
 * @param componentID The component ID to validate
 * @param endpoint The endpoint to compare against
 * @param asyncResp Response object
 * @return true if the component ID is valid and matches the endpoint, false
 * otherwise
 */
static bool validateComponentID(
    const std::string& componentID, const std::string& endpoint,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    const std::string& endpointComponent =
        std::filesystem::path(endpoint).filename().string();
    if (endpointComponent.empty())
    {
        BMCWEB_LOG_ERROR("Invalid endpoint format: {}", endpoint);
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentID);
        return false;
    }

    std::string transformedComponent = endpointComponent;
    if (endpointComponent.starts_with(PLATFORMDEVICEPREFIX))
    {
        transformedComponent =
            endpointComponent.substr(PLATFORMDEVICEPREFIX.size());
    }

    if (transformedComponent != componentID)
    {
        BMCWEB_LOG_ERROR("Component ID mismatch. Expected: {}, Got: {}",
                         transformedComponent, componentID);
        messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                   componentID);
        return false;
    }

    return true;
}

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
        [asyncResp](const boost::system::error_code& ec,
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
    if (!validChassisPath.has_value())
    {
        BMCWEB_LOG_ERROR("validChassisPath is not set");
        messages::internalError(asyncResp->res);
        return;
    }

    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID,
         &memberArray](const boost::system::error_code& ec,
                       const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Failed to get TPM components: {}",
                                 ec.message());
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

                std::string odataId = "/redfish/v1/Chassis/";
                odataId += chassisID;
                odataId += "/TrustedComponents/";
                odataId += componentID;
                memberArray.push_back({{"@odata.id", odataId}});
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
inline void updateTPMCollection(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID,
    const std::optional<std::string>& validChassisPath,
    nlohmann::json& memberArray)
{
    if (!validChassisPath.has_value())
    {
        BMCWEB_LOG_ERROR("validChassisPath is not set");
        messages::internalError(asyncResp->res);
        return;
    }

    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID, validChassisPath,
         &memberArray](const boost::system::error_code& ec,
                       const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_INFO("Failed to get TPM components: {}",
                                ec.message());
                return;
            }
            bool foundComponents = false;
            for (const auto& [path, services] : subtree)
            {
                sdbusplus::message::object_path objPath(path);
                const std::string componentID = objPath.filename();
                if (componentID.empty())
                {
                    continue;
                }

                foundComponents = true;
                std::string odataId = "/redfish/v1/Chassis/";
                odataId += chassisID;
                odataId += "/TrustedComponents/";
                odataId += componentID;
                memberArray.push_back({{"@odata.id", odataId}});
                asyncResp->res.jsonValue["Members@odata.count"] =
                    memberArray.size();
            }

            if (!foundComponents)
            {
                // No TPM components found, return 404
                asyncResp->res.jsonValue.clear();
                messages::resourceNotFound(asyncResp->res, "TrustedComponent",
                                           chassisID);
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
 * @param endpoint Endpoint to validate component ID against
 * @param memberArray JSON array to add members to
 */
inline void updateSPDMTrustedComponents(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& endpoint,
    nlohmann::json& memberArray)
{
    const bool& isCollection = true;
    const std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.SPDM.Responder"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/SPDM", 0, interfaces,
        [asyncResp, chassisID, endpoint, &memberArray,
         isCollection](const boost::system::error_code& ec,
                       const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Failed to get SPDM subtree: {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }

            isComponentEnabled(
                asyncResp, subtree, endpoint, isCollection,
                [asyncResp, chassisID, endpoint, &memberArray] {
                    std::string componentID =
                        std::filesystem::path(endpoint).filename().string();
                    if (componentID.starts_with(PLATFORMDEVICEPREFIX))
                    {
                        componentID =
                            componentID.substr(PLATFORMDEVICEPREFIX.size());
                    }

                    std::string odataId = "/redfish/v1/Chassis/";
                    odataId += chassisID;
                    odataId += "/TrustedComponents/";
                    odataId += componentID;
                    memberArray.push_back({{"@odata.id", odataId}});
                    asyncResp->res.jsonValue["Members@odata.count"] =
                        memberArray.size();
                });
        });
}

/**
 * @brief Updates the collection with TPM trusted components
 * @param asyncResp Response object
 * @param chassisID ID of the chassis containing trusted components
 */
inline void setupTrustedComponentsResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID)
{
    const std::string collectionPath =
        "/redfish/v1/Chassis/" + chassisID + "/TrustedComponents";
    asyncResp->res.jsonValue["@odata.id"] = collectionPath;
    asyncResp->res.jsonValue["@odata.type"] =
        "#TrustedComponentCollection.TrustedComponentCollection";
    asyncResp->res.jsonValue["Name"] = chassisID + "_TrustedComponents";
    asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
    asyncResp->res.jsonValue["Members@odata.count"] = 0;
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

    getChassisAssociatedEndpoint(
        asyncResp, chassisID,
        [asyncResp, chassisID](const std::string& endpoint, bool exists) {
            if (exists)
            {
                setupTrustedComponentsResponse(asyncResp, chassisID);
                nlohmann::json& memberArray =
                    asyncResp->res.jsonValue["Members"];

                updateSPDMTrustedComponents(asyncResp, chassisID, endpoint,
                                            memberArray);
            }
            else
            {
                redfish::chassis_utils::getValidChassisPath(
                    asyncResp, chassisID,
                    [asyncResp, chassisID](
                        const std::optional<std::string>& validChassisPath) {
                        if (validChassisPath)
                        {
                            setupTrustedComponentsResponse(asyncResp,
                                                           chassisID);
                            nlohmann::json& memberArray =
                                asyncResp->res.jsonValue["Members"];

                            updateTPMCollection(asyncResp, chassisID,
                                                validChassisPath, memberArray);
                        }
                        else
                        {
                            messages::resourceNotFound(asyncResp->res,
                                                       "Chassis", chassisID);
                        }
                    });
            }
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
    const std::string& chassisID, const std::string& componentInventoryPath)
{
    if (chassisID.empty() || componentInventoryPath.empty())
    {
        BMCWEB_LOG_ERROR(
            "Invalid input parameters for fetchInventoryProperties");
        messages::internalError(asyncResp->res);
        return;
    }

    const std::array<const char*, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Decorator.Asset"};

    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID, componentInventoryPath](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                servicesToInterfaces) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("ObjectMapper GetObject error for {}: {}",
                                 componentInventoryPath, ec);
                messages::internalError(asyncResp->res);
                return;
            }

            if (servicesToInterfaces.empty())
            {
                BMCWEB_LOG_ERROR("No services found for path: {}",
                                 componentInventoryPath);
                messages::internalError(asyncResp->res);
                return;
            }

            const std::string& service = servicesToInterfaces[0].first;

            crow::connections::systemBus->async_method_call(
                [asyncResp, chassisID, service, componentInventoryPath](
                    const boost::system::error_code ecInv,
                    const std::vector<std::pair<
                        std::string, dbus::utility::DbusVariantType>>& props) {
                    if (ecInv)
                    {
                        BMCWEB_LOG_ERROR(
                            "GetAll inventory properties failed for service {} path {}: {}",
                            service, componentInventoryPath, ecInv);
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
                                asyncResp->res.jsonValue["Manufacturer"] =
                                    *sVal;
                            }
                            else if (propName == "SerialNumber")
                            {
                                asyncResp->res.jsonValue["SerialNumber"] =
                                    *sVal;
                            }
                            else if (propName == "UUID")
                            {
                                asyncResp->res.jsonValue["UUID"] = *sVal;
                            }
                        }
                    }
                },
                service, componentInventoryPath,
                "org.freedesktop.DBus.Properties", "GetAll", "");
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", componentInventoryPath,
        interfaces);
}

/**
 * @brief Sets up Links section for a trusted component
 * @param asyncResp Response object
 * @param chassisID ID of the chassis
 * @param inventoryPath D-Bus path to the inventory object
 */
inline void fetchTrustedComponentLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& chassisID,
    const std::string& componentInventoryPath)
{
    const std::string& endpointComponent =
        std::filesystem::path(componentInventoryPath).filename().string();
    asyncResp->res.jsonValue["Links"]["ComponentIntegrity"] = {
        {{"@odata.id", "/redfish/v1/ComponentIntegrity/" + endpointComponent}}};

    std::string objPath = componentInventoryPath + "/inventory";

    chassis_utils::getAssociationEndpoint(objPath, [objPath, asyncResp](
                                                       const bool& status,
                                                       const std::string& ep) {
        if (!status)
        {
            BMCWEB_LOG_DEBUG("Unable to get the association endpoint for {}",
                             objPath);
            nlohmann::json& componentsProtectedArray =
                asyncResp->res.jsonValue["Links"]["ComponentsProtected"];
            componentsProtectedArray = nlohmann::json::array();

            return;
        }

        chassis_utils::getRedfishURL(ep, [ep,
                                          asyncResp](const bool& urlStatus,
                                                     const std::string& url) {
            nlohmann::json& componentsProtectedArray =
                asyncResp->res.jsonValue["Links"]["ComponentsProtected"];
            componentsProtectedArray = nlohmann::json::array();

            if (!urlStatus)
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
                    asyncResp->res.jsonValue["Links"]["ComponentsProtected"][0];
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
    const std::string& chassisID, const std::string& spdmService,
    const std::string& spdmPath)
{
    if (chassisID.empty() || spdmService.empty() || spdmPath.empty())
    {
        BMCWEB_LOG_ERROR("Invalid input parameters for fetchAssociations");
        messages::internalError(asyncResp->res);
        return;
    }

    std::string inventoryObjectPath = spdmPath + "/inventory_object";
    chassis_utils::getAssociationEndpoint(
        inventoryObjectPath,
        [asyncResp, chassisID, spdmService,
         spdmPath](const bool& status, const std::string& endpoint) {
            if (!status)
            {
                BMCWEB_LOG_ERROR("No 'inventory_object' association found");
                messages::internalError(asyncResp->res);
                return;
            }

            fetchInventoryProperties(asyncResp, chassisID, endpoint);
            fetchTrustedComponentLinks(asyncResp, chassisID, endpoint);
        });
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
    const std::string& chassisID, const std::string& endpoint,
    const std::string& componentID,
    const std::vector<std::pair<std::string, std::vector<std::string>>>&
        services)
{
    if (chassisID.empty() || componentID.empty() || services.empty() ||
        endpoint.empty())
    {
        BMCWEB_LOG_ERROR(
            "Invalid input parameters for fetchComponentTypeAndAssociations");
        messages::internalError(asyncResp->res);
        return;
    }

    const std::string& spdmService = services.front().first;
    std::string spdmPath = "/xyz/openbmc_project/SPDM/";
    spdmPath.append(endpoint);

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
                BMCWEB_LOG_ERROR("Error reading property 'Type': {}", ecType);
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

            fetchAssociations(asyncResp, chassisID, spdmService, spdmPath);
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
    const std::string& endpoint,
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
    fetchComponentTypeAndAssociations(asyncResp, chassisID, endpoint,
                                      componentID, services);
}

/**
 * @brief Handles TPM implementation of GET request for a specific
 * TrustedComponent
 * @param asyncResp Response object
 * @param chassisID ID of the chassis
 * @param componentID ID of the component
 * @param endpoint Endpoint of the component
 */
inline void handleTpmComponentGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& componentID,
    const std::string& endpoint)
{
    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisID,
        [asyncResp, chassisID, componentID,
         endpoint](const std::optional<std::string>& validChassisPath) {
            if (!validChassisPath)
            {
                BMCWEB_LOG_ERROR("Cannot get validChassisPath");
                messages::internalError(asyncResp->res);
                return;
            }
            crow::connections::systemBus->async_method_call(
                [asyncResp, chassisID, componentID](
                    const boost::system::error_code& ec,
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
                        asyncResp->res.jsonValue["TrustedComponentType"] =
                            "Discrete";
                        for (const auto& [service, interfaces] : services)
                        {
                            for (const auto& interface : interfaces)
                            {
                                if (interface ==
                                        "xyz.openbmc_project.Inventory.Decorator.Asset" ||
                                    interface ==
                                        "xyz.openbmc_project.Inventory.Item" ||
                                    interface ==
                                        "xyz.openbmc_project.Software.Version")
                                {
                                    trustedComponentGetAllProperties(
                                        asyncResp, service, path, interface);
                                }
                            }
                        }
                        return;
                    }

                    messages::resourceNotFound(asyncResp->res,
                                               "TrustedComponent", componentID);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                *validChassisPath, static_cast<int32_t>(0),
                trustedComponentInterfaces);
        });
}

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
        [&req, asyncResp, chassisID, componentID, certificateID, certPath,
         certData](
            const boost::system::error_code& ec,
            const boost::container::flat_map<
                std::string, dbus::utility::DbusVariantType>& responderProps) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error for Responder interface: {}", ec);
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

            asyncResp->res.jsonValue = {
                {"@odata.id",
                 "/redfish/v1/Chassis/" + chassisID + "/TrustedComponents/" +
                     componentID + "/Certificates/" + certificateID},
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

            if (certData.keyUsage.has_value() &&
                !certData.keyUsage.value().empty())
            {
                asyncResp->res.jsonValue["KeyUsage"] =
                    certData.keyUsage.value();
            }

            if (certData.issuer.has_value())
            {
                cert_utils::updateCertIssuerOrSubject(
                    asyncResp->res.jsonValue["Issuer"],
                    certData.issuer.value());
            }

            if (certData.subject.has_value())
            {
                cert_utils::updateCertIssuerOrSubject(
                    asyncResp->res.jsonValue["Subject"],
                    certData.subject.value());
            }

            if (certData.notAfter.has_value() && certData.notAfter.value() != 0)
            {
                asyncResp->res.jsonValue["ValidNotAfter"] =
                    redfish::time_utils::getDateTimeUintMs(
                        certData.notAfter.value());
            }

            if (certData.notBefore.has_value() &&
                certData.notBefore.value() != 0)
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
    const boost::system::error_code& ec,
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
    const auto* certificateString =
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
        if (const auto* value =
                std::get_if<std::vector<std::string>>(&itKeyUsage->second))
        {
            certData.keyUsage = *value;
        }
    }

    auto itIssuer = certProps.find("Issuer");
    if (itIssuer != certProps.end())
    {
        if (const auto* value = std::get_if<std::string>(&itIssuer->second))
        {
            certData.issuer = *value;
        }
    }

    auto itSubject = certProps.find("Subject");
    if (itSubject != certProps.end())
    {
        if (const auto* value = std::get_if<std::string>(&itSubject->second))
        {
            certData.subject = *value;
        }
    }

    auto itValidNotAfter = certProps.find("ValidNotAfter");
    if (itValidNotAfter != certProps.end())
    {
        if (const auto* value = std::get_if<uint64_t>(&itValidNotAfter->second))
        {
            certData.notAfter = *value;
        }
    }

    auto itValidNotBefore = certProps.find("ValidNotBefore");
    if (itValidNotBefore != certProps.end())
    {
        if (const auto* value =
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
inline void getTrustedComponentCertificate(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& componentID, const std::string& endpointComponent,
    const std::string& chassisID, const std::string& certificateID)
{
    std::string certPath = "/xyz/openbmc_project/SPDM/";
    certPath.append(endpointComponent);
    // Construct the D-Bus path for the SPDM certificate object
    CertificateData certData;
    crow::connections::systemBus->async_method_call(
        [&req, asyncResp, chassisID, componentID, certificateID, certPath,
         certData](const boost::system::error_code& ec,
                   const boost::container::flat_map<
                       std::string, dbus::utility::DbusVariantType>&
                       certProps) mutable {
            handleCertificateProperties(req, asyncResp, chassisID, componentID,
                                        certificateID, certPath, ec, certProps,
                                        certData);
        },
        "xyz.openbmc_project.SPDM", certPath, "org.freedesktop.DBus.Properties",
        "GetAll", "xyz.openbmc_project.Certs.Certificate");
}

/**
 * @brief Common function to check if a component is enabled and matches
 * endpoint
 * @param asyncResp Response object
 * @param subtree Subtree containing paths and services to check
 * @param endpoint Endpoint to validate against
 * @param callback Function to call if component is enabled and matches
 * @param isCollection Flag to indicate if this is a collection request
 */
inline void isComponentEnabled(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const dbus::utility::GetSubTreeType& subtree, const std::string& endpoint,
    const bool& isCollection, const std::function<void()>& callback)

{
    if (subtree.empty())
    {
        BMCWEB_LOG_ERROR("Empty subtree provided");
        messages::internalError(asyncResp->res);
        return;
    }

    const std::string& endpointComponent =
        std::filesystem::path(endpoint).filename().string();
    BMCWEB_LOG_ERROR("endpointComponent: {}", endpointComponent);
    if (endpointComponent.empty())
    {
        BMCWEB_LOG_ERROR("Invalid endpoint format: {}", endpoint);
        return;
    }
    for (const auto& [path, services] : subtree)
    {
        if (services.empty())
        {
            continue;
        }

        std::string pathComponent =
            std::filesystem::path(path).filename().string();
        if (pathComponent.empty())
        {
            BMCWEB_LOG_ERROR("Invalid path format: {}", path);
            continue;
        }

        if (pathComponent != endpointComponent)
        {
            continue;
        }

        // For collections, we don't need to check if the component is enabled
        if (isCollection)
        {
            callback();
            return;
        }

        sdbusplus::asio::getProperty<bool>(
            *crow::connections::systemBus, services[0].first, path,
            "xyz.openbmc_project.Object.Enable", "Enabled",
            [asyncResp, endpointComponent, callback](
                const boost::system::error_code& ec, const bool& enabled) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("Error reading Enabled property: {}", ec);
                    messages::internalError(asyncResp->res);
                    return;
                }
                if (!enabled)
                {
                    messages::resourceNotFound(
                        asyncResp->res, "TrustedComponent", endpointComponent);
                    return;
                }
                callback();
            });
        return;
    }
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
    const bool& isCollection = false;
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

    getChassisAssociatedEndpoint(
        asyncResp, chassisID,
        [&req, asyncResp, chassisID, componentID, certificateID,
         isCollection](const std::string& endpoint, bool exists) {
            if (!exists)
            {
                messages::resourceNotFound(asyncResp->res, "Chassis",
                                           chassisID);
                return;
            }
            if (!validateComponentID(componentID, endpoint, asyncResp))
            {
                return;
            }

            const std::array<std::string_view, 1> interfaces = {
                "xyz.openbmc_project.SPDM.Responder"};

            dbus::utility::getSubTree(
                "/xyz/openbmc_project/SPDM", 0, interfaces,
                [&req, asyncResp, chassisID, componentID, certificateID,
                 endpoint, isCollection](
                    const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("GetSubTree error: {}", ec);
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    isComponentEnabled(
                        asyncResp, subtree, endpoint, isCollection,
                        [&req, asyncResp, chassisID, componentID, certificateID,
                         endpoint] {
                            std::string endpointComponent =
                                std::filesystem::path(endpoint)
                                    .filename()
                                    .string();
                            getTrustedComponentCertificate(
                                req, asyncResp, componentID, endpointComponent,
                                chassisID, certificateID);
                        });
                });
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
    const bool& isCollection = true;
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    getChassisAssociatedEndpoint(
        asyncResp, chassisID,
        [&req, asyncResp, chassisID, componentID,
         isCollection](const std::string& endpoint, bool exists) {
            if (!exists)
            {
                messages::resourceNotFound(asyncResp->res, "Chassis",
                                           chassisID);
                return;
            }
            if (!validateComponentID(componentID, endpoint, asyncResp))
            {
                return;
            }

            const std::array<std::string_view, 1> interfaces = {
                "xyz.openbmc_project.SPDM.Responder"};

            dbus::utility::getSubTree(
                "/xyz/openbmc_project/SPDM", 0, interfaces,
                [&req, asyncResp, chassisID, componentID, endpoint,
                 isCollection](
                    const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("GetSubTree error: {}", ec);
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    isComponentEnabled(
                        asyncResp, subtree, endpoint, isCollection,
                        [asyncResp, chassisID, componentID] {
                            std::string url = "/redfish/v1/Chassis/";
                            url += chassisID;
                            url += "/TrustedComponents/";
                            url += componentID;
                            url += "/Certificates";
                            asyncResp->res.jsonValue = {
                                {"@odata.id", url},
                                {"@odata.type",
                                 "#CertificateCollection.CertificateCollection"},
                                {"Name", "Chassis Certificate Collection"},
                                {"Description",
                                 "A Collection of Certificate instances"}};

                            nlohmann::json& members =
                                asyncResp->res.jsonValue["Members"];
                            members = nlohmann::json::array();
                            members.push_back(
                                {{"@odata.id", url + "/CertChain"}});

                            asyncResp->res.jsonValue["Members@odata.count"] =
                                members.size();
                        });
                });
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
    const bool& isCollection = false;
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    getChassisAssociatedEndpoint(
        asyncResp, chassisID,
        [asyncResp, chassisID, componentID,
         isCollection](const std::string& endpoint, bool exists) {
            if (!exists)
            {
                handleTpmComponentGet(asyncResp, chassisID, componentID,
                                      endpoint);
                return;
            }
            if (!validateComponentID(componentID, endpoint, asyncResp))
            {
                return;
            }

            const std::array<std::string_view, 1> interfaces = {
                "xyz.openbmc_project.SPDM.Responder"};

            dbus::utility::getSubTree(
                "/xyz/openbmc_project/SPDM", 0, interfaces,
                [asyncResp, chassisID, componentID, endpoint, isCollection](
                    const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "GetSubTree for SPDM objects failed: {}", ec);
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    isComponentEnabled(
                        asyncResp, subtree, endpoint, isCollection,
                        [asyncResp, chassisID, componentID, endpoint, subtree] {
                            for (const auto& [path, services] : subtree)
                            {
                                if (services.empty())
                                {
                                    continue;
                                }
                                std::string endpointComponent =
                                    std::filesystem::path(endpoint)
                                        .filename()
                                        .string();

                                finalizeTrustedComponent(
                                    asyncResp, chassisID, componentID,
                                    endpointComponent, services);
                                return;
                            }
                        });
                });
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
