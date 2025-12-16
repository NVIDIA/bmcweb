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

#include "background_copy.hpp"
#include "credential_pipe.hpp"
#include "dot.hpp"
#include "health.hpp"
#include "in_band.hpp"
#include "lsp.hpp"
#include "manual_boot.hpp"
#include "nvidia_dbus_utility.hpp"
#include "nvidia_protected_component.hpp"
#include "query.hpp"
#include "trusted_components.hpp"

#include <openssl/bio.h>
#include <openssl/ec.h>

#include <app.hpp>
#include <boost/asio/connect_pipe.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/asio/writable_pipe.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>
#include <boost/url/format.hpp>
#include <boost/url/url.hpp>
#include <dbus_utility.hpp>
#include <openbmc_dbus_rest.hpp>
#include <registries/privilege_registry.hpp>
#include <sdbusplus/asio/property.hpp>
#include <utils/chassis_utils.hpp>
#include <utils/collection.hpp>
#include <utils/conditions_utils.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/json_utils.hpp>
#include <utils/nvidia_chassis_util.hpp>

#include <algorithm>

namespace redfish
{

namespace erot
{
constexpr const char* spdmObjectPath = "/xyz/openbmc_project/SPDM";
constexpr const char* spdmResponderIntf = "xyz.openbmc_project.SPDM.Responder";
constexpr const char* spdmServiceName = "xyz.openbmc_project.SPDM";
using SPDMCertificates = std::vector<std::tuple<uint8_t, std::string>>;

} // namespace erot

/**
 * @brief Populate the certificate collection members for a specific Chassis
 * @param asyncResp - Shared pointer to object holding response data
 * @param chassisID - ID of the chassis to match against SPDM objects
 * @param ec - Error code
 * @param objects - Managed objects
 * @return None
 */
static void getChassisCertificateCollection(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const boost::system::error_code& ec,
    const dbus::utility::ManagedObjectType& objects)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
        return;
    }

    nlohmann::json& members = asyncResp->res.jsonValue["Members"];
    if (!members.is_array())
    {
        members = nlohmann::json::array();
    }

    // Iterate over all the objects and add the certificate URL to the response.
    for (const auto& object : objects)
    {
        sdbusplus::message::object_path objPath(object.first);

        // Check if the SPDM object filename matches the Chassis ID
        // Example: /xyz/openbmc_project/SPDM/IRoT_CX7_1 matches IRoT_CX7_1
        if (objPath.filename() == chassisID)
        {
            boost::urls::url certUrl = boost::urls::format(
                "/redfish/v1/Chassis/{}/Certificates/CertChain", chassisID);
            nlohmann::json::object_t certObj;
            certObj["@odata.id"] = std::string(certUrl.buffer());
            members.emplace_back(std::move(certObj));
            break;
        }
    }

    asyncResp->res.jsonValue["Members@odata.count"] = members.size();
}

/**
 * @brief Find the certificate association of the SPDM managed objects
 * @param requestPath - Request URL encoded path
 * @param asyncResp - Shared pointer to object holding response data
 * @param object - Object holding the associations
 * @param objectPath - Path of the object
 * @param certificateID - Certificate ID
 * @param ec - Error code
 * @param resp - Response object
 * @return None
 */
static void checkAssociationEndpointsForMatch(
    const std::string& requestPath,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::pair<sdbusplus::message::object_path,
                    dbus::utility::DBusInterfacesMap>& object,
    const std::string& objectPath, const std::string& certificateID,
    const boost::system::error_code& ec,
    std::variant<std::vector<std::string>>& resp)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "Response error={} for object={} in checkAssociationEndpointsForMatch",
            ec, objectPath);
        return;
    }

    std::vector<std::string>* data =
        std::get_if<std::vector<std::string>>(&resp);
    if (data == nullptr)
    {
        BMCWEB_LOG_ERROR(
            "No associated inventory object found for object={} in checkAssociationEndpointsForMatch",
            objectPath);
        return;
    }

    const std::string& associatedInventoryPath = data->front();
    if (objectPath == associatedInventoryPath)
    {
        // Certificates is of collection of slot and it's
        // associated certificate.
        // Slot is the index of the slot which has to be
        // used by the SPDM.
        const uint8_t* slot = nullptr;
        const erot::SPDMCertificates* certs = nullptr;
        for (const auto& interface : object.second)
        {
            if (interface.first == erot::spdmResponderIntf)
            {
                for (const auto& property : interface.second)
                {
                    if (property.first == "Certificate")
                    {
                        certs = std::get_if<erot::SPDMCertificates>(
                            &property.second);
                    }
                    if (property.first == "Slot")
                    {
                        slot = std::get_if<uint8_t>(&property.second);
                        if (slot != nullptr)
                        {
                            BMCWEB_LOG_ERROR("Slot ID:{}", *slot);
                        }
                    }
                }
            }
        }

        // Get the desired certificated and convert it into PEM.
        std::string chassisID =
            std::filesystem::path(objectPath).filename().string();
        if (slot == nullptr)
        {
            BMCWEB_LOG_ERROR("No slot found");
            messages::resourceNotFound(
                asyncResp->res, "#Certificate.v1_5_0.Certificate", chassisID);
            return;
        }

        nlohmann::json::object_t certJson;
        certJson["@odata.id"] = requestPath;
        certJson["@odata.type"] = "#Certificate.v1_5_0.Certificate";
        certJson["Id"] = certificateID;
        certJson["Name"] = chassisID + " Certificate Chain";
        certJson["CertificateType"] = "PEMchain";

        nlohmann::json::array_t usageTypes;
        usageTypes.emplace_back("Device");
        certJson["CertificateUsageTypes"] = std::move(usageTypes);

        nlohmann::json::object_t spdmObj;
        spdmObj["SlotId"] = *slot;
        certJson["SPDM"] = std::move(spdmObj);

        asyncResp->res.jsonValue = std::move(certJson);

        // If the certificate is found, add it to the response.
        if (certs != nullptr && !certs->empty())
        {
            erot::SPDMCertificates::const_iterator it = std::ranges::find_if(
                certs->begin(), certs->end(),
                [slot](const std::tuple<uint8_t, std::string>& cert) {
                    return std::get<0>(cert) == *slot;
                });

            if (it != certs->end())
            {
                BMCWEB_LOG_DEBUG("Found certificate");
                asyncResp->res.jsonValue["CertificateString"] =
                    std::get<1>(*it);
            }
        }
    }
}

/**
 * @brief Retrieve the SPDM managed objects and collect the certificate
 * @param requestPath - Request URL encoded path
 * @param asyncResp - Shared pointer to object holding response data
 * @param objectPath - Path of the object
 * @param certificateID - Optional certificate ID
 * @param ec - Error code
 * @param objects - Managed objects
 * @return None
 */
static void getChassisCertificateInstanceHandler(
    const std::string& requestPath,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath, const std::string& certificateID,
    const boost::system::error_code& ec,
    const dbus::utility::ManagedObjectType& objects)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    if (objects.empty())
    {
        BMCWEB_LOG_ERROR("No objects found");
        messages::resourceNotFound(
            asyncResp->res, "#Certificate.v1_5_0.Certificate", certificateID);
        return;
    }

    for (const auto& object : objects)
    {
        const std::string assocPath =
            std::string(object.first) + "/inventory_object";
        auto cb =
            std::bind_front(checkAssociationEndpointsForMatch, requestPath,
                            asyncResp, object, objectPath, certificateID);
        dbus::utility::findAssociations(assocPath, cb);
    }
}

/**
 * @brief Get the certificate instance from the chassis
 * @param requestPath - Request URL encoded path
 * @param asyncResp - Shared pointer to object holding response data
 * @param chassisID - ID of the chassis
 * @param certificateID - Certificate ID
 * @param ec - Error code
 * @param subtree - Subtree response
 * @return None
 */
static void getChassisCertificateInstance(
    const std::string& requestPath,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& certificateID,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("D-Bus response error on GetSubTree {}", ec);
        return;
    }

    for (const auto& object : subtree)
    {
        const std::string& path = object.first;
        const auto& connectionNames = object.second;

        sdbusplus::message::object_path objectPath(path);
        if (objectPath.filename() != chassisID)
        {
            continue;
        }

        if (connectionNames.empty())
        {
            BMCWEB_LOG_ERROR("Got 0 Connection names");
            continue;
        }

        dbus::utility::getManagedObjects(
            erot::spdmServiceName,
            sdbusplus::message::object_path(erot::spdmObjectPath),
            std::bind_front(getChassisCertificateInstanceHandler, requestPath,
                            asyncResp, objectPath, certificateID));
        break;
    }
}

/* This function implements the OEM property under
 * chassis schema.
 * It first gets the associated ErotInventoryObject then
 * it gets the inventory backed by the Erot and finally converts
 * the Dbus inventory path to the Redfish URL.
 * path: Dbus object path
 * */

inline void getChassisOEMComponentProtected(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path)
{
    std::string objPath = path + "/inventory";
    chassis_utils::getAssociationEndpoint(objPath, [objPath, asyncResp](
                                                       const bool& status,
                                                       const std::string& ep) {
        if (!status)
        {
            BMCWEB_LOG_DEBUG("Unable to get the association endpoint for {}",
                             objPath);
            // inventory association is not created for
            // HMC and PcieSwitch
            // if we don't get the association
            // assumption is, it is hmc.
            asyncResp->res.jsonValue["Links"]["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaChassis.v1_3_0.NvidiaChassis";
            nlohmann::json& componentsProtectedArray =
                asyncResp->res
                    .jsonValue["Links"]["Oem"]["Nvidia"]["ComponentsProtected"];
            componentsProtectedArray = nlohmann::json::array();
            componentsProtectedArray.push_back({nlohmann::json::array(
                {"@odata.id",
                 "/redfish/v1/Managers/" +
                     std::string(BMCWEB_REDFISH_MANAGER_URI_NAME)})});

            return;
        }
        chassis_utils::getRedfishURL(ep, [ep,
                                          asyncResp](const bool& status1,
                                                     const std::string& url) {
            std::string redfishURL = url;
            if (!status1)
            {
                BMCWEB_LOG_DEBUG("Unable to get the Redfish URL for object={}",
                                 ep);
            }
            else
            {
                if (url.empty())
                {
                    redfishURL = std::string(
                        "/redfish/v1/Managers/" +
                        std::string(BMCWEB_REDFISH_MANAGER_URI_NAME));
                }
            }
            asyncResp->res.jsonValue["Links"]["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaChassis.v1_3_0.NvidiaChassis";
            nlohmann::json& componentsProtectedArray =
                asyncResp->res
                    .jsonValue["Links"]["Oem"]["Nvidia"]["ComponentsProtected"];
            componentsProtectedArray = nlohmann::json::array();
            componentsProtectedArray.push_back(
                {nlohmann::json::array({"@odata.id", redfishURL})});
        });
    });
}

/**
 * @brief handler for ERoT chassis resource.
 *
 * @param req - Pointer to object holding request data
 * @param asyncResp - Pointer to object holding response data
 * @param chassisId - chassis id
 */
inline void getEROTChassis(const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& chassisId, bool isCpuEROT)
{
    const std::array<const char*, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.SPDMResponder"};

    crow::connections::systemBus->async_method_call(
        [&req, asyncResp, chassisId(std::string(chassisId)),
         isCpuEROT](const boost::system::error_code& ec,
                    const dbus::utility::GetSubTreeType& subtree) {
            [[maybe_unused]] const auto& reqRef = req;
            [[maybe_unused]] const auto cpuEROT = isCpuEROT;

            if (ec)
            {
                messages::internalError(asyncResp->res);
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

                sdbusplus::message::object_path objPath(path);
                if (objPath.filename() != chassisId)
                {
                    continue;
                }

                if (connectionNames.empty())
                {
                    BMCWEB_LOG_ERROR("Got 0 Connection names");
                    continue;
                }

                if constexpr (BMCWEB_EROT_RESET)
                {
                    asyncResp->res
                        .jsonValue["Actions"]["#Chassis.Reset"]["target"] =
                        "/redfish/v1/Chassis/" + chassisId +
                        "/Actions/Chassis.Reset";
                    asyncResp->res.jsonValue["Actions"]["#Chassis.Reset"]
                                            ["@Redfish.ActionInfo"] =
                        "/redfish/v1/Chassis/" + chassisId + "/ResetActionInfo";
                }

                if constexpr (BMCWEB_DOT_SUPPORT)
                {
                    auto& oemActionsJsonDot =
                        asyncResp->res.jsonValue["Actions"]["Oem"];
                    std::string oemActionsRouteDot =
                        "/redfish/v1/Chassis/" + chassisId + "/Actions/Oem/";
                    oemActionsJsonDot["#CAKInstall"]["target"] =
                        oemActionsRouteDot + "CAKInstall";
                    oemActionsJsonDot["#CAKLock"]["target"] =
                        oemActionsRouteDot + "CAKLock";
                    oemActionsJsonDot["#CAKTest"]["target"] =
                        oemActionsRouteDot + "CAKTest";
                    oemActionsJsonDot["#DOTDisable"]["target"] =
                        oemActionsRouteDot + "DOTDisable";
                    oemActionsJsonDot["#DOTTokenInstall"]["target"] =
                        oemActionsRouteDot + "DOTTokenInstall";

                    if constexpr (BMCWEB_MANUAL_BOOT_MODE_SUPPORT)
                    {
                        auto& oemActionsJsonManualBoot =
                            asyncResp->res.jsonValue["Actions"]["Oem"];
                        oemActionsJsonManualBoot
                            ["#NvidiaChassis.BootProtectedDevice"]["target"] =
                                "/redfish/v1/Chassis/" + chassisId +
                                "/Actions/Oem/NvidiaChassis.BootProtectedDevice";
                    }
                }

                if constexpr (BMCWEB_HEALTH_ROLLUP_ALTERNATIVE)
                {
                    auto health = std::make_shared<HealthRollup>(
                        path,
                        [asyncResp](const std::string& rootHealth,
                                    const std::string& healthRollup) {
                            asyncResp->res.jsonValue["Status"]["Health"] =
                                rootHealth;
                            if constexpr (!BMCWEB_DISABLE_HEALTH_ROLLUP)
                            {
                                asyncResp->res
                                    .jsonValue["Status"]["HealthRollup"] =
                                    healthRollup;
                            } // BMCWEB_DISABLE_HEALTH_ROLLUP
                        },
                        &health_state::ok);
                    health->start();
                }
                else
                { // ifdef BMCWEB_HEALTH_ROLLUP_ALTERNATIVE
                    auto health = std::make_shared<HealthPopulate>(asyncResp);

                    sdbusplus::asio::getProperty<std::vector<std::string>>(
                        *crow::connections::systemBus,
                        "xyz.openbmc_project.ObjectMapper",
                        path + "/all_sensors",
                        "xyz.openbmc_project.Association", "endpoints",
                        [health](const boost::system::error_code& ec2,
                                 const std::vector<std::string>& resp) {
                            if (ec2)
                            {
                                return; // no sensors = no failures
                            }
                            health->inventory = resp;
                        });

                    health->populate();
                } // ifdef BMCWEB_HEALTH_ROLLUP_ALTERNATIVE

                asyncResp->res.jsonValue["Status"]["State"] = "Enabled";

                asyncResp->res.jsonValue["@odata.type"] =
                    "#Chassis.v1_22_0.Chassis";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Chassis/" + chassisId;
                asyncResp->res.jsonValue["Name"] = chassisId;
                asyncResp->res.jsonValue["Id"] = chassisId;
                if constexpr (!BMCWEB_NVIDIA_OEM_BF_PROPERTIES)
                {
                    auto certsObject = std::string("/redfish/v1/Chassis/") +
                                       chassisId + "/Certificates";

                    asyncResp->res.jsonValue["Certificates"]["@odata.id"] =
                        certsObject;
                }

                asyncResp->res.jsonValue["Links"]["ManagedBy"] = {
                    {{"@odata.id",
                      "/redfish/v1/Managers/" +
                          std::string(BMCWEB_REDFISH_MANAGER_URI_NAME)}}};

                asyncResp->res.jsonValue["Links"]["ComputerSystems"] = {
                    {{"@odata.id",
                      "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME)}}};

                // Only add TrustedComponents link for valid chassis
                getChassisAssociatedEndpoint(
                    asyncResp, chassisId,
                    [asyncResp,
                     chassisId]([[maybe_unused]] const std::string& endpoint,
                                bool exists) {
                        if (exists)
                        {
                            // SPDM endpoint exists, add TrustedComponents link
                            asyncResp->res
                                .jsonValue["TrustedComponents"]["@odata.id"] =
                                boost::urls::format(
                                    "/redfish/v1/Chassis/{}/TrustedComponents",
                                    chassisId);
                        }
                        else
                        {
                            redfish::chassis_utils::getValidChassisPath(
                                asyncResp, chassisId,
                                [asyncResp, chassisId](
                                    const std::optional<std::string>&
                                        validChassisPath) {
                                    checkTPMComponentsAndAddLink(
                                        asyncResp, chassisId, validChassisPath);
                                });
                        }
                    });

                firmware_info::updateProtectedComponentLink(asyncResp,
                                                            chassisId);
                firmware_info::updateIrreversibleConfigEnabled(asyncResp,
                                                               chassisId);

                redfish::nvidia_chassis_utils::getBackgroundCopyAndInBandInfo(
                    asyncResp, chassisId);

                // Might have 2+ services to support different properties
                for (const auto& connectionName : connectionNames)
                {
                    // Check if the interface exists, then go ahead getting the
                    // property value to prevent getting an internal error
                    for (const auto& interface : connectionName.second)
                    {
                        if (interface == "xyz.openbmc_project.Common.UUID")
                        {
                            redfish::nvidia_chassis_utils::getChassisUUID(
                                asyncResp, connectionName.first, path);
                        }
                        else if (
                            interface ==
                            "xyz.openbmc_project.Inventory.Decorator.Location")
                        {
                            redfish::chassis_utils::getChassisLocationType(
                                asyncResp, connectionName.first, path);
                        }
                        else if (
                            interface ==
                            "xyz.openbmc_project.Inventory.Decorator.LocationCode")
                        {
                            redfish::chassis_utils::getChassisLocationCode(
                                asyncResp, connectionName.first, path);
                        }
                        else if (
                            interface ==
                            "xyz.openbmc_project.Inventory.Decorator.LocationContext")
                        {
                            redfish::chassis_utils::getChassisLocationContext(
                                asyncResp, connectionName.first, path);
                        }
                        else if (interface ==
                                 "xyz.openbmc_project.Inventory.Item.Chassis")
                        {
                            redfish::nvidia_chassis_utils::getChassisType(
                                asyncResp, connectionName.first, path);
                        }
                        else if (
                            interface ==
                            "xyz.openbmc_project.Inventory.Decorator.Asset")
                        {
                            redfish::nvidia_chassis_utils::
                                getChassisManufacturer(
                                    asyncResp, connectionName.first, path);

                            redfish::nvidia_chassis_utils::
                                getChassisSerialNumber(
                                    asyncResp, connectionName.first, path);

                            redfish::nvidia_chassis_utils::getChassisSKU(
                                asyncResp, connectionName.first, path);
                        }
                        else if (
                            interface ==
                            "xyz.openbmc_project.Inventory.Decorator.Replaceable")
                        {
                            redfish::chassis_utils::getChassisReplaceable(
                                asyncResp, connectionName.first, path);
                        }
                    }
                }

                getChassisOEMComponentProtected(asyncResp, path);

                // Link association to parent chassis
                redfish::chassis_utils::getChassisLinksContainedBy(asyncResp,
                                                                   objPath);
                if constexpr (!BMCWEB_DISABLE_CONDITIONS_ARRAY)
                {
                    redfish::conditions_utils::populateServiceConditions(
                        asyncResp, chassisId);
                } // BMCWEB_DISABLE_CONDITIONS_ARRAY

                if constexpr (BMCWEB_MANUAL_BOOT_MODE_SUPPORT)
                {
                    if (isCpuEROT)
                    {
                        manual_boot::bootModeQuery(req, asyncResp, chassisId);
                    }
                }
                return;
            }

            // Couldn't find an object with that name.  return an error
            messages::resourceNotFound(asyncResp->res,
                                       "#Chassis.v1_22_0.Chassis", chassisId);
        },

        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0, interfaces);
}

/**
 * Certificate resource for a chassis
 */
inline void requestRoutesEROTChassisCertificate(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Certificates/<str>/")
        .privileges(redfish::privileges::getCertificate)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& chassisID,
                   const std::string& certificateID) -> void {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                std::string requestPath = std::string(req.url().encoded_path());
                BMCWEB_LOG_DEBUG("URL={}", requestPath);

                redfish::nvidia_chassis_utils::isEROTChassis(
                    chassisID,
                    [asyncResp, chassisID, certificateID,
                     requestPath{std::move(requestPath)}](
                        bool isEROT, [[maybe_unused]] bool isCpuEROT) {
                        if (!isEROT)
                        {
                            BMCWEB_LOG_DEBUG("Not a EROT chassis");
                            messages::resourceNotFound(
                                asyncResp->res, "Certificate", chassisID);
                            return;
                        }

                        if (certificateID != "CertChain")
                        {
                            BMCWEB_LOG_ERROR("No objects found");
                            messages::resourceNotFound(
                                asyncResp->res, "Certificate", certificateID);
                            return;
                        }

                        constexpr std::array<std::string_view, 2> interfaces = {
                            "xyz.openbmc_project.Inventory.Item.Chassis",
                            "xyz.openbmc_project.Inventory.Item.SPDMResponder"};

                        dbus::utility::getSubTree(
                            "/xyz/openbmc_project/inventory", 0, interfaces,
                            std::bind_front(getChassisCertificateInstance,
                                            requestPath, asyncResp, chassisID,
                                            certificateID));
                    });
            });

    /**
     * Collection of Chassis(EROT) certificates
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Certificates/")
        .privileges(redfish::privileges::getCertificateCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& chassisID) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                std::string requestPath = std::string(req.url().encoded_path());
                BMCWEB_LOG_DEBUG("URL={}", requestPath);

                nlohmann::json::object_t collectionJson;
                collectionJson["@odata.id"] = requestPath;
                collectionJson["@odata.type"] =
                    "#CertificateCollection.CertificateCollection";
                collectionJson["Name"] = "Certificates Collection";
                asyncResp->res.jsonValue = std::move(collectionJson);

                redfish::nvidia_chassis_utils::isEROTChassis(
                    chassisID,
                    [asyncResp,
                     chassisID](bool isEROT, [[maybe_unused]] bool isCpuEROT) {
                        if (!isEROT)
                        {
                            BMCWEB_LOG_DEBUG("Not a EROT chassis");
                            messages::resourceNotFound(asyncResp->res,
                                                       "CertificateCollection",
                                                       chassisID);
                            return;
                        }

                        sdbusplus::message::object_path spdmObjPath(
                            erot::spdmObjectPath);

                        dbus::utility::getManagedObjects(
                            erot::spdmServiceName, spdmObjPath,
                            std::bind_front(getChassisCertificateCollection,
                                            asyncResp, chassisID));
                    });
            });
}

/**
 * @brief Handles request PATCH
 * The function set all delivered properties
 * in request body on chassis defined in chassisId
 * The function is designed only for chassis
 * which is ERoT
 *
 * @param resp Async HTTP response.
 * @param asyncResp Pointer to object holding response data
 * @param chassisId  Chassis ID
 */
inline void handleEROTChassisPatch(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, [[maybe_unused]] bool isCpuEROT)
{
    if (chassisId.empty())
    {
        return;
    }
    std::optional<nlohmann::json> oemObject;

    if (!json_util::readJsonPatch(req, asyncResp->res, "Oem", oemObject))
    {
        return;
    }

    if (!oemObject.has_value())
    {
        return;
    }

    std::optional<nlohmann::json> oemNvidiaObject;
    if (!json_util::readJson(*oemObject, asyncResp->res, "Nvidia",
                             oemNvidiaObject))
    {
        return;
    }

    if (!oemNvidiaObject.has_value())
    {
        return;
    }

    std::optional<bool> backgroundCopyEnabled;
    std::optional<bool> inBandEnabled;
    std::optional<bool> manualBootModeEnabled;
    if (!json_util::readJson(
            *oemNvidiaObject, asyncResp->res, "ManualBootModeEnabled",
            manualBootModeEnabled, "AutomaticBackgroundCopyEnabled",
            backgroundCopyEnabled, "InbandUpdatePolicyEnabled", inBandEnabled))
    {
        return;
    }

    if constexpr (BMCWEB_MANUAL_BOOT_MODE_SUPPORT)
    {
        if (manualBootModeEnabled.has_value())
        {
            if (!isCpuEROT)
            {
                messages::actionNotSupported(asyncResp->res,
                                             "ERoT manualBootModeEnabled");
                return;
            }
            manual_boot::bootModeSet(req, asyncResp, chassisId,
                                     *manualBootModeEnabled);
        }
    }

    if (!backgroundCopyEnabled.has_value() && !inBandEnabled.has_value())
    {
        return;
    }

    if (backgroundCopyEnabled.has_value())
    {
        updateBackgroundCopyPolicy(asyncResp, backgroundCopyEnabled.value(),
                                   chassisId);
    }

    if (inBandEnabled.has_value())
    {
        redfish::nvidia_chassis_utils::setInBandEnabled(asyncResp, chassisId,
                                                        inBandEnabled.value());
    }
}

inline void requestRoutesEROTChassisDOT(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Actions/Oem/CAKInstall/")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& chassisID) -> void {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::string cakKey;
                std::optional<bool> lockDisable;
                std::optional<std::string> apFirmwareSignature;
                if (!redfish::json_util::readJsonAction(
                        req, asyncResp->res, "CAKKey", cakKey,
                        "APFirmwareSignature", apFirmwareSignature,
                        "LockDisable", lockDisable))
                {
                    return;
                }
                std::vector<uint8_t> binaryKey;
                if (!dot::getBinaryKeyFromPem(cakKey, binaryKey))
                {
                    messages::actionParameterValueFormatError(
                        asyncResp->res, cakKey, "CAKKey", "CAKInstall");
                    return;
                }
                if (binaryKey.size() != dot::dotKeySize)
                {
                    messages::propertyValueOutOfRange(
                        asyncResp->res, std::to_string(binaryKey.size()),
                        "CAKKey size");
                    return;
                }
                std::string binarySignature;
                if (apFirmwareSignature)
                {
                    if (!crow::utility::base64Decode(*apFirmwareSignature,
                                                     binarySignature))
                    {
                        messages::actionParameterValueFormatError(
                            asyncResp->res, *apFirmwareSignature,
                            "APFirmwareSignature", "CAKInstall");
                        return;
                    }
                    if (binarySignature.size() !=
                        (dot::dotCakInstallDataSize - dot::dotKeySize - 1))
                    {
                        messages::propertyValueOutOfRange(
                            asyncResp->res,
                            std::to_string(binarySignature.size()),
                            "APFirmwareSignature size");
                        return;
                    }
                }
                std::vector<uint8_t> data;
                data.reserve(binaryKey.size() + binarySignature.size() + 1);
                data.insert(data.begin(), binaryKey.begin(), binaryKey.end());
                // lockDisable is optional and false by default
                data.emplace_back((lockDisable && *lockDisable) ? 1 : 0);
                if (!binarySignature.empty())
                {
                    data.insert(data.end(), binarySignature.begin(),
                                binarySignature.end());
                }
                dot::executeDotCommand(asyncResp, chassisID,
                                       dot::DotMctpVdmUtilCommand::CAKInstall,
                                       data);
            });

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Actions/Oem/CAKLock/")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& chassisID) -> void {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::string key;
                if (!redfish::json_util::readJsonAction(req, asyncResp->res,
                                                        "Key", key))
                {
                    return;
                }
                std::vector<uint8_t> binaryKey;
                if (!dot::getBinaryKeyFromPem(key, binaryKey))
                {
                    messages::actionParameterValueFormatError(
                        asyncResp->res, key, "Key", "CAKLock");
                    return;
                }
                if (binaryKey.size() != dot::dotKeySize)
                {
                    messages::propertyValueOutOfRange(
                        asyncResp->res, std::to_string(binaryKey.size()),
                        "Key size");
                    return;
                }
                dot::executeDotCommand(asyncResp, chassisID,
                                       dot::DotMctpVdmUtilCommand::CAKLock,
                                       binaryKey);
            });

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Actions/Oem/CAKTest/")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& chassisID) -> void {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::vector<uint8_t> data;
                dot::executeDotCommand(asyncResp, chassisID,
                                       dot::DotMctpVdmUtilCommand::CAKTest,
                                       data);
            });

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Actions/Oem/DOTDisable/")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& chassisID) -> void {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::string key;
                if (!redfish::json_util::readJsonAction(req, asyncResp->res,
                                                        "Key", key))
                {
                    return;
                }
                std::vector<uint8_t> binaryKey;
                if (!dot::getBinaryKeyFromPem(key, binaryKey))
                {
                    messages::actionParameterValueFormatError(
                        asyncResp->res, key, "Key", "DOTDisable");
                    return;
                }
                if (binaryKey.size() != dot::dotKeySize)
                {
                    messages::propertyValueOutOfRange(
                        asyncResp->res, std::to_string(binaryKey.size()),
                        "Key size");
                    return;
                }
                dot::executeDotCommand(asyncResp, chassisID,
                                       dot::DotMctpVdmUtilCommand::DOTDisable,
                                       binaryKey);
            });

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Actions/Oem/DOTTokenInstall/")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& chassisID) -> void {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                if (req.body().size() != dot::dotTokenSize)
                {
                    BMCWEB_LOG_ERROR("Invalid DOT token size: {}",
                                     req.body().size());
                    messages::invalidUpload(
                        asyncResp->res, "DOT token install",
                        "filesize has to be equal to " +
                            std::to_string(dot::dotTokenSize));
                    return;
                }
                std::vector<uint8_t> data(req.body().begin(), req.body().end());
                dot::executeDotCommand(
                    asyncResp, chassisID,
                    dot::DotMctpVdmUtilCommand::DOTTokenInstall, data);
            });
}

inline void requestRoutesEROTChassisManualBootMode(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Actions/Oem/NvidiaChassis.BootProtectedDevice/")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& chassisId) -> void {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                manual_boot::bootAp(req, asyncResp, chassisId);
            });
}

/**
@brief - Performs ERoT chassis graceful reset using /usr/bin/erot_reset_pre.sh
and /usr/bin/erot_reset.sh scripts. The scripts are platform-specific and need
to be installed separately. Upon successful reset, the ERoT reset will also
reset the BMC by toggling the AP_reset pin. There are three cases of failure:
         1. An update procedure is already in progress.
         2. There is no EC firmware pending.
         3. The command is not supported by the current ERoT firmware.
@param[in] - asyncResp: Response const variable
             endpointId: ERoT endpoint ID
*/
inline void gracefulRestart(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            uint32_t endpointId)
{
    enum EROTRstErr
    {
        NoErr = 0,
        UpdateInProgress = 1,
        NoFwPending = 2,
        CmdNotSupported = 3
    };
    std::string erotResetPrePath = "/usr/bin/erot_reset_pre.sh";
    std::string erotResetPath = "/usr/bin/erot_reset.sh";

    if ((!std::filesystem::exists(erotResetPrePath)) ||
        (!std::filesystem::exists(erotResetPath)))
    {
        BMCWEB_LOG_DEBUG(
            "ERROR Cannot perform ERoT self reset: The action is not supported by the current BMC version");
        messages::actionNotSupported(asyncResp->res, "ERoT self-reset");
        return;
    }

    std::string command = erotResetPrePath + " " + std::to_string(endpointId);
    auto exitCallback = [asyncResp, erotResetPath,
                         endpointId](const boost::system::error_code& ec,
                                     int errorCode) mutable {
        BMCWEB_LOG_DEBUG("ec: {}  errorCode {}", ec, errorCode);
        if (ec)
        {
            BMCWEB_LOG_DEBUG("ERROR DBUS response error {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }

        if (errorCode == EROTRstErr::UpdateInProgress)
        {
            BMCWEB_LOG_DEBUG(
                "ERROR Cannot perform ERoT self reset: An update is in progress");
            messages::updateInProgressMsg(
                asyncResp->res,
                "Retry the operation once firmware update operation is complete.");
            return;
        }

        if (errorCode == EROTRstErr::NoFwPending)
        {
            BMCWEB_LOG_DEBUG(
                "ERROR Cannot perform ERoT self reset: There is no EC FW pending");
            messages::resourceNotFound(asyncResp->res, "ERoT FW",
                                       "Pending-ERoT-FW");
            return;
        }

        if (errorCode == EROTRstErr::CmdNotSupported)
        {
            BMCWEB_LOG_DEBUG(
                "ERROR Cannot perform ERoT self reset: The action is not supported by the current ERoT version");
            messages::actionNotSupported(asyncResp->res, "ERoT self-reset");
            return;
        }

        std::string resetCommand =
            erotResetPath + " " + std::to_string(endpointId);
        BMCWEB_LOG_DEBUG("Sending ERoT self-reset command");

        /* During the erotReset script, ERoT performs a self reset which leads
        to BMC external reset. Hence it is unnecessary to check its results */
        messages::success(asyncResp->res);

        namespace bpv2 = boost::process::v2;
        auto& io = crow::connections::systemBus->get_io_context();
        boost::asio::readable_pipe outRead(io);
        boost::asio::writable_pipe outWrite(io);
        boost::asio::connect_pipe(outRead, outWrite);
        boost::asio::readable_pipe errRead(io);
        boost::asio::writable_pipe errWrite(io);
        boost::asio::connect_pipe(errRead, errWrite);

        bpv2::process child{
            io,
            resetCommand,
            {},
            bpv2::process_stdio{.in = nullptr,
                                .out = std::move(outWrite),
                                .err = std::move(errWrite)},
        };
        child.async_wait(
            [outRead = std::move(outRead), errRead = std::move(errRead)](
                const std::error_code&, int) mutable {
                // Ignore output; pipes kept alive until exit
            });
    };
    namespace bpv2 = boost::process::v2;
    auto& io = crow::connections::systemBus->get_io_context();
    boost::asio::readable_pipe preOutRead(io);
    boost::asio::writable_pipe preOutWrite(io);
    boost::asio::connect_pipe(preOutRead, preOutWrite);
    boost::asio::readable_pipe preErrRead(io);
    boost::asio::writable_pipe preErrWrite(io);
    boost::asio::connect_pipe(preErrRead, preErrWrite);

    bpv2::process preChild{
        io,
        command,
        {},
        bpv2::process_stdio{.in = nullptr,
                            .out = std::move(preOutWrite),
                            .err = std::move(preErrWrite)},
    };
    preChild.async_wait([exitCallback = std::move(exitCallback),
                         preOutRead = std::move(preOutRead),
                         preErrRead = std::move(preErrRead)](
                            const std::error_code& ec, int code) mutable {
        // Keep pipes alive; then invoke original callback
        exitCallback(boost::system::error_code(
                         ec.value(), boost::system::generic_category()),
                     code);
    });
}

/**
@brief - Finds the endpoint ID associated with the given chassis UUID.
@param[in] - req: Request const variable
             asyncResp: Response const variable
             chassisUUID: ERoT chassis UUID
             isPCIe: Indicates if ERoT is connected via PCIe or SPI
*/
inline void findEIDforEROTReset(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisUUID, bool isPCIe = true)
{
    std::string serviceName = "xyz.openbmc_project.MCTP.Control.PCIe";
    if (!isPCIe)
    {
        serviceName = "xyz.openbmc_project.MCTP.Control.SPI";
    }

    crow::connections::systemBus->async_method_call(
        [&req, asyncResp, chassisUUID,
         isPCIe](const boost::system::error_code ec,
                 const dbus::utility::ManagedObjectType& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("ERROR DBUS response error for MCTP.Control");
                messages::internalError(asyncResp->res);
                return;
            }

            const uint32_t* eid = nullptr;
            const std::string* uuid = nullptr;
            bool foundEID = false;

            for (const auto& objectPath : resp)
            {
                for (const auto& interfaceMap : objectPath.second)
                {
                    if (interfaceMap.first == "xyz.openbmc_project.Common.UUID")
                    {
                        for (const auto& propertyMap : interfaceMap.second)
                        {
                            if (propertyMap.first == "UUID")
                            {
                                uuid = std::get_if<std::string>(
                                    &propertyMap.second);
                            }
                        }
                    }

                    if (interfaceMap.first ==
                        "xyz.openbmc_project.MCTP.Endpoint")
                    {
                        for (const auto& propertyMap : interfaceMap.second)
                        {
                            if (propertyMap.first == "EID")
                            {
                                eid =
                                    std::get_if<uint32_t>(&propertyMap.second);
                            }
                        }
                    }
                }

                if ((*uuid) == chassisUUID)
                {
                    foundEID = true;
                    break;
                }
            }

            if (foundEID)
            {
                gracefulRestart(asyncResp, *eid);
            }
            else
            {
                if (isPCIe)
                {
                    findEIDforEROTReset(req, asyncResp, chassisUUID, false);
                }
                else
                {
                    BMCWEB_LOG_DEBUG(
                        "ERROR Can not find relevant MCTP endpoint for chassis {}",
                        chassisUUID);
                }
            }
        },
        serviceName, "/xyz/openbmc_project/mctp",
        "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
}

/**
@brief - Performs ERoT chassis reset action. Currently GracefulRestart is
supported.
@param[in] - req: Request const variable
             asyncResp: Response const variable
             chassisId: ERoT chassis ID
*/
inline void handleEROTChassisResetAction(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    BMCWEB_LOG_DEBUG("Post ERoT Chassis Reset.");

    std::string resetType;

    if (!json_util::readJsonAction(req, asyncResp->res, "ResetType", resetType))
    {
        return;
    }

    if (resetType != "GracefulRestart")
    {
        BMCWEB_LOG_DEBUG("ERROR Invalid property value for ResetType: {}",
                         resetType);
        messages::actionParameterNotSupported(asyncResp->res, resetType,
                                              "ResetType");
        return;
    }

    const std::array<const char*, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.SPDMResponder"};

    crow::connections::systemBus->async_method_call(
        [&req, asyncResp, chassisId(std::string(chassisId))](
            const boost::system::error_code ec,
            const dbus::utility::GetSubTreeType& subtree) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            bool chassisIdFound = false;

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

                sdbusplus::message::object_path objPath(path);
                if (objPath.filename() != chassisId)
                {
                    continue;
                }

                if (connectionNames.empty())
                {
                    BMCWEB_LOG_ERROR("ERROR Got 0 Connection names");
                    continue;
                }

                chassisIdFound = true;

                sdbusplus::asio::getProperty<std::string>(
                    *crow::connections::systemBus, connectionNames[0].first,
                    path, "xyz.openbmc_project.Common.UUID", "UUID",
                    [&req, asyncResp](const boost::system::error_code& ec2,
                                      const std::string& chassisUUID) {
                        if (ec2)
                        {
                            BMCWEB_LOG_DEBUG(
                                "ERROR DBUS response error for UUID");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        findEIDforEROTReset(req, asyncResp, chassisUUID, false);
                    });
            }

            /* Couldn't find an object with that name. Return an error */
            if (!chassisIdFound)
            {
                messages::resourceNotFound(
                    asyncResp->res, "#Chassis.v1_17_0.Chassis", chassisId);
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0, interfaces);
}
} // namespace redfish
