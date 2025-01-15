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
#define OPENSSL_SUPPRESS_DEPRECATED 1

#include "background_copy.hpp"
#include "dot.hpp"
#include "erot_chassis.hpp"
#include "in_band.hpp"
#include "lsp.hpp"
#include "manual_boot.hpp"
#include "nvidia_protected_component.hpp"
#include "query.hpp"

#include <openssl/bio.h>
#include <openssl/ec.h>
#include <health.hpp>
#include <app.hpp>
#include <boost/container/flat_map.hpp>
#include <dbus_utility.hpp>
#include <openbmc_dbus_rest.hpp>
#include <registries/privilege_registry.hpp>
#include <sdbusplus/asio/property.hpp>
#include <utils/chassis_utils.hpp>
#include <utils/collection.hpp>
#include <utils/conditions_utils.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/json_utils.hpp>
#include "utils/health_utils.hpp"

namespace redfish
{
namespace bp = boost::process;

namespace erot
{
constexpr const char* spdmObjectPath = "/xyz/openbmc_project/SPDM";
constexpr const char* spdmResponderIntf = "xyz.openbmc_project.SPDM.Responder";
constexpr const char* spdmServiceName = "xyz.openbmc_project.SPDM";
using SPDMCertificates = std::vector<std::tuple<uint8_t, std::string>>;

} // namespace erot

/**
 * @brief Retrieve the certificate and append to the response
 * message
 *
 * @param[in] asyncResp Shared pointer to the response message
 * @param[in] objectPath  Path of the D-Bus service object
 * @return None
 */
inline void getChassisCertificate(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath, const std::string& certificateID)
{
    // 1) Get all the measurement object
    // 2) Measurement object have the association to the inventory object
    // 3) Check this is the inventory object which we are interested
    // 4) If yes, Get the certificate object from the measurement object
    // NOTE: EROT chassis will be having only one certificate at any momment of
    // time.
    crow::connections::systemBus->async_method_call(
        [req, asyncResp, objectPath,
         certificateID](const boost::system::error_code ec,
                        const dbus::utility::ManagedObjectType& objects) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            for (const auto& object : objects)
            {
                crow::connections::systemBus->async_method_call(
                    [req, asyncResp, object, objectPath, certificateID](
                        const boost::system::error_code ec,
                        std::variant<std::vector<std::string>>& resp) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR(
                                "Didn't find the inventory object");
                            return; // should have associoated inventory object.
                        }
                        std::vector<std::string>* data =
                            std::get_if<std::vector<std::string>>(&resp);
                        if (data == nullptr)
                        {
                            // Object must have associated inventory object.
                            return;
                        }

                        const std::string& associatedInventoryPath =
                            data->front();
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
                                    // rest of the properties are string.
                                    for (const auto& property :
                                         interface.second)
                                    {
                                        if (property.first == "Certificate")
                                        {
                                            certs = std::get_if<
                                                erot::SPDMCertificates>(
                                                &property.second);
                                        }
                                        if (property.first == "Slot")
                                        {
                                            slot = std::get_if<uint8_t>(
                                                &property.second);
                                            BMCWEB_LOG_DEBUG("Slot ID:{}",
                                                             *slot);
                                        }
                                    }
                                }
                            }

                            // Get the desired certificated and convert it into
                            // PEM.
                            auto chassisID =
                                std::filesystem::path(objectPath).filename().string();
                            if (slot)
                            {
                                    asyncResp->res.jsonValue = {
                                        {"@odata.id", req.url()},
                                        {"@odata.type",
                                        "#Certificate.v1_5_0.Certificate"},
                                        {"Id", certificateID},
                                        {"Name", chassisID + " Certificate Chain"},
                                        {"CertificateType", "PEMchain"},
                                        {"CertificateUsageTypes",
                                        nlohmann::json::array({"Device"})},
                                        {"SPDM", {{"SlotId", *slot}}},
                                    };
                            }

                    if (certs && slot && certs->size() > 0)
                    {
                        auto it = std::find_if(
                            (*certs).begin(), (*certs).end(),
                            [slot](
                                const std::tuple<uint8_t, std::string>& cert) {
                            return std::get<0>(cert) == (*slot);
                        });
                        if (it != (*certs).end())
                        {
                            std::cout << "Found" << std::endl;
                        }
                        std::string certStr = std::get<1>(*it);
                        asyncResp->res.jsonValue["CertificateString"] = certStr;
                    }
                }
            },
                "xyz.openbmc_project.ObjectMapper",
                std::string(object.first) + "/inventory_object",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Association", "endpoints");
            }
    },
        erot::spdmServiceName, erot::spdmObjectPath,
        "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
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
                                          asyncResp](const bool& status,
                                                     const std::string& url) {
            std::string redfishURL = url;
            if (!status)
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
        [req, asyncResp, chassisId(std::string(chassisId)),
         isCpuEROT](const boost::system::error_code ec,
                    const crow::openbmc_mapper::GetSubTreeType& subtree) {
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

                if (connectionNames.size() < 1)
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
                        [health](const boost::system::error_code ec2,
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

                firmware_info::updateProtectedComponentLink(asyncResp,
                                                            chassisId);
                firmware_info::updateIrreversibleConfigEnabled(asyncResp,
                                                               chassisId);

                // Might have 2+ services to support different properties
                for (size_t serviceIdx = 0; serviceIdx < connectionNames.size();
                     serviceIdx++)
                {
                    // Check if the interface exists, then go ahead getting the
                    // property value to prevent getting an internal error
                    for (const auto& interface :
                         connectionNames[serviceIdx].second)
                    {
                        if (interface == "xyz.openbmc_project.Common.UUID")
                        {
                            redfish::chassis_utils::getChassisUUID(
                                req, asyncResp,
                                connectionNames[serviceIdx].first, path, true);
                        }
                        else if (
                            interface ==
                            "xyz.openbmc_project.Inventory.Decorator.Location")
                        {
                            redfish::chassis_utils::getChassisLocationType(
                                asyncResp, connectionNames[serviceIdx].first,
                                path);
                        }
                        else if (
                            interface ==
                            "xyz.openbmc_project.Inventory.Decorator.LocationCode")
                        {
                            redfish::chassis_utils::getChassisLocationCode(
                                asyncResp, connectionNames[serviceIdx].first,
                                path);
                        }
                        else if (
                            interface ==
                            "xyz.openbmc_project.Inventory.Decorator.LocationContext")
                        {
                            redfish::chassis_utils::getChassisLocationContext(
                                asyncResp, connectionNames[serviceIdx].first,
                                path);
                        }
                        else if (interface ==
                                 "xyz.openbmc_project.Inventory.Item.Chassis")
                        {
                            redfish::chassis_utils::getChassisType(
                                asyncResp, connectionNames[serviceIdx].first,
                                path);
                        }
                        else if (
                            interface ==
                            "xyz.openbmc_project.Inventory.Decorator.Asset")
                        {
                            redfish::chassis_utils::getChassisManufacturer(
                                asyncResp, connectionNames[serviceIdx].first,
                                path);

                            redfish::chassis_utils::getChassisSerialNumber(
                                asyncResp, connectionNames[serviceIdx].first,
                                path);

                            redfish::chassis_utils::getChassisSKU(
                                asyncResp, connectionNames[serviceIdx].first,
                                path);
                        }
                        else if (
                            interface ==
                            "xyz.openbmc_project.Inventory.Decorator.Replaceable")
                        {
                            redfish::chassis_utils::getChassisReplaceable(
                                asyncResp, connectionNames[serviceIdx].first,
                                path);
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
                else
                {
                    (void)isCpuEROT;
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
                redfish::chassis_utils::isEROTChassis(
                    chassisID,
                    [req, asyncResp, chassisID, certificateID](
                        bool isEROT, [[maybe_unused]] bool isCpuEROT) {
                        if (!isEROT)
                        {
                            BMCWEB_LOG_DEBUG("Not a EROT chassis");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        if (certificateID != "CertChain")
                        {
                            BMCWEB_LOG_DEBUG("Not a valid Certificate ID");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        BMCWEB_LOG_DEBUG("URL={}", req.url());

                        // Get the correct objpath based on DBus interface
                        const std::array<const char*, 2> interfaces = {
                            "xyz.openbmc_project.Inventory.Item.Chassis",
                            "xyz.openbmc_project.Inventory.Item.SPDMResponder"};

                        crow::connections::systemBus->async_method_call(
                            [req, asyncResp, chassisID(std::string(chassisID)),
                             certificateID](
                                const boost::system::error_code ec,
                                const crow::openbmc_mapper::GetSubTreeType&
                                    subtree) {
                                if (ec)
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
                                         object : subtree)
                                {
                                    const std::string& path = object.first;
                                    const std::vector<std::pair<
                                        std::string, std::vector<std::string>>>&
                                        connectionNames = object.second;

                                    sdbusplus::message::object_path objectPath(
                                        path);
                                    if (objectPath.filename() != chassisID)
                                    {
                                        continue;
                                    }

                                    if (connectionNames.size() < 1)
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "Got 0 Connection names");
                                        continue;
                                    }

                                    getChassisCertificate(req, asyncResp,
                                                          objectPath,
                                                          certificateID);
                                    break;
                                }
                            },

                            "xyz.openbmc_project.ObjectMapper",
                            "/xyz/openbmc_project/object_mapper",
                            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                            "/xyz/openbmc_project/inventory", 0, interfaces);
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
                std::string url =
                    "/redfish/v1/Chassis/" + chassisID + "/Certificates";
                asyncResp->res.jsonValue = {
                    {"@odata.id", url},
                    {"@odata.type",
                     "#CertificateCollection.CertificateCollection"},
                    {"Name", "Certificates Collection"}};

                nlohmann::json& members = asyncResp->res.jsonValue["Members"];
                members = nlohmann::json::array();
                members.push_back({{"@odata.id", url + "/CertChain"}});

                asyncResp->res.jsonValue["Members@odata.count"] =
                    members.size();
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

    std::optional<nlohmann::json> oemNvidiaObject;

    if (!json_util::readJson(*oemObject, asyncResp->res, "Nvidia",
                             oemNvidiaObject))
    {
        return;
    }

    std::optional<bool> backgroundCopyEnabled;
    std::optional<bool> inBandEnabled;
    std::optional<bool> manualBootModeEnabled;

    if constexpr (BMCWEB_MANUAL_BOOT_MODE_SUPPORT)
    {
        if (isCpuEROT == false)
        {
            // Couldn't find an object with that name.  return an error
            messages::resourceNotFound(asyncResp->res,
                                       "#Chassis.v1_17_0.Chassis", chassisId);
            return;
        }
    }

    if (!json_util::readJson(
            *oemNvidiaObject, asyncResp->res, "ManualBootModeEnabled",
            manualBootModeEnabled, "AutomaticBackgroundCopyEnabled",
            backgroundCopyEnabled, "InbandUpdatePolicyEnabled", inBandEnabled))
    {
        return;
    }

    if (manualBootModeEnabled)
    {
        if (BMCWEB_MANUAL_BOOT_MODE_SUPPORT && isCpuEROT)
        {
            manual_boot::bootModeSet(req, asyncResp, chassisId,
                                     *manualBootModeEnabled);
        }
        else
        {
            messages::propertyNotWritable(asyncResp->res,
                                          "ManualBootModeEnabled");
            return;
        }
    }

    if (!backgroundCopyEnabled.has_value() && !inBandEnabled.has_value())
    {
        return;
    }
    const std::array<const char*, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.SPDMResponder"};
    crow::connections::systemBus->async_method_call(
        [req, asyncResp, chassisId(std::string(chassisId)),
         backgroundCopyEnabled,
         inBandEnabled](const boost::system::error_code ec,
                        const crow::openbmc_mapper::GetSubTreeType& subtree) {
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

                for (const auto& connection : connectionNames)
                {
                    sdbusplus::asio::getProperty<std::string>(
                        *crow::connections::systemBus, connection.first, path,
                        "xyz.openbmc_project.Common.UUID", "UUID",
                        [req, asyncResp, chassisId(std::string(chassisId)),
                         backgroundCopyEnabled,
                         inBandEnabled](const boost::system::error_code ec,
                                        const std::string& chassisUUID) {
                            if (ec)
                            {
                                return;
                            }

                            if (backgroundCopyEnabled.has_value())
                            {
                                redfish::chassis_utils::
                                    setBackgroundCopyEnabled(
                                        req, asyncResp, chassisId, chassisUUID,
                                        backgroundCopyEnabled.value());
                            }

                            if (inBandEnabled.has_value())
                            {
                                redfish::chassis_utils::setInBandEnabled(
                                    req, asyncResp, chassisId, chassisUUID,
                                    inBandEnabled.value());
                            }
                        });
                }
            }
        },

        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0, interfaces);
}

/**
 * DOT (device ownership transfer) support
 */
#define DOT_MCTP_VDM_UTIL_MCTP_STATUS_RESPONSE_SIZE 9
#define DOT_MCTP_VDM_UTIL_DOT_RESPONSE_SIZE 10
// defined in libmctp project in vdm/nvidia/libmctp-vdm-cmds.h
#define DOT_KEY_SIZE 96
// related to mctp_vendor_cmd_cak_install structure size in libmctp
#define DOT_CAK_INSTALL_DATA_SIZE (DOT_KEY_SIZE + 98)
#define DOT_TOKEN_SIZE 256

inline bool getBinaryKeyFromPem(const std::string& pem,
                                std::vector<uint8_t>& key)
{
    std::unique_ptr<BIO, decltype(&::BIO_free)> bio{BIO_new(BIO_s_mem()),
                                                    &::BIO_free};
    if (!bio)
    {
        BMCWEB_LOG_ERROR("openssl BIO allocation failed");
        return false;
    }

    size_t written = 0;
    int ret = BIO_write_ex(bio.get(), pem.data(), pem.size(), &written);
    if (ret != 1 || written != pem.size())
    {
        BMCWEB_LOG_ERROR("BIO_write_ex failed");
        return false;
    }

    std::unique_ptr<EVP_PKEY, decltype(&::EVP_PKEY_free)> pubKey{
        PEM_read_bio_PUBKEY(bio.get(), nullptr, lsp::emptyPasswordCallback,
                            nullptr),
        &::EVP_PKEY_free};
    if (!pubKey)
    {
        BMCWEB_LOG_ERROR("PEM_read_bio_PUBKEY failed");
        return false;
    }

    std::unique_ptr<EC_KEY, decltype(&::EC_KEY_free)> ecKey{
        EVP_PKEY_get1_EC_KEY(pubKey.get()), &::EC_KEY_free};
    if (!ecKey)
    {
        BMCWEB_LOG_ERROR("EVP_PKEY_get1_EC_KEY failed");
        return false;
    }

    const EC_GROUP* group = EC_KEY_get0_group(ecKey.get());
    if (!group)
    {
        BMCWEB_LOG_ERROR("EC_KEY_get0_group failed");
        return false;
    }
    const EC_POINT* point = EC_KEY_get0_public_key(ecKey.get());
    if (!point)
    {
        BMCWEB_LOG_ERROR("EC_KEY_get0_group failed");
        return false;
    }

    // the first byte contains information about whether the key
    // is compressed as per
    // https://www.rfc-editor.org/rfc/rfc5480#section-2.2
    key.resize(DOT_KEY_SIZE + 1);
    size_t resultSize = EC_POINT_point2oct(
        group, point, EC_GROUP_get_point_conversion_form(group), key.data(),
        key.size(), nullptr);
    if (resultSize == 0)
    {
        BMCWEB_LOG_ERROR("EC_POINT_point2oct failed");
        return false;
    }

    // remove the compression byte
    key.erase(key.begin());
    return true;
}

inline void createDotErrorResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& type, const std::string& hexErrorCode)
{
    int decErrorCode = 0;
    try
    {
        decErrorCode = std::stoi(hexErrorCode, nullptr, 16);
    }
    catch (const std::invalid_argument&)
    {
        BMCWEB_LOG_ERROR("Invalid error code hex octet: {}", hexErrorCode);
        decErrorCode = -1;
    }
    asyncResp->res.jsonValue["@odata.type"] = "#Message.v1_1_1.Message";
    asyncResp->res.jsonValue["MessageId"] = "Nvidia.ActionError";
    asyncResp->res.jsonValue["Message"] =
        "Action failed with " + type + ": " + std::to_string(decErrorCode);
    asyncResp->res.jsonValue["MessageArgs"] = std::to_string(decErrorCode);
    asyncResp->res.jsonValue["MessageSeverity"] = "Warning";
    asyncResp->res.jsonValue["Resolution"] = "None";
}

inline void executeDotCommand(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, dot::DotMctpVdmUtilCommand command,
    const std::vector<uint8_t>& data)
{
    static std::unique_ptr<dot::DotCommandHandler> dotOperation;
    auto resultHandler = [asyncResp](const std::string& output) {
        std::istringstream iss(output);
        std::vector<std::string> tokens{std::istream_iterator<std::string>{iss},
                                        std::istream_iterator<std::string>{}};
        if (tokens.size() != DOT_MCTP_VDM_UTIL_MCTP_STATUS_RESPONSE_SIZE &&
            tokens.size() != DOT_MCTP_VDM_UTIL_DOT_RESPONSE_SIZE)
        {
            BMCWEB_LOG_ERROR("mctp-vdm-util RX response has invalid length: {}",
                             output);
            messages::resourceErrorsDetectedFormatError(
                asyncResp->res, "mctp-vdm-util response", "invalid length");
        }
        else if (tokens.size() == DOT_MCTP_VDM_UTIL_MCTP_STATUS_RESPONSE_SIZE)
        {
            createDotErrorResponse(asyncResp, "MCTP status",
                                   tokens[tokens.size() - 1]);
        }
        else if (tokens[tokens.size() - 2] == "00" &&
                 tokens[tokens.size() - 1] == "00")
        {
            messages::success(asyncResp->res);
        }
        else if (tokens[tokens.size() - 2] == "01")
        {
            createDotErrorResponse(asyncResp, "DOT response",
                                   tokens[tokens.size() - 1]);
        }
        else
        {
            createDotErrorResponse(asyncResp, "MCTP status",
                                   tokens[tokens.size() - 2]);
        }
        boost::asio::post(crow::connections::systemBus->get_io_context(),
                          [] { dotOperation = nullptr; });
    };
    auto errorHandler =
        [asyncResp](const std::string& desc, const std::string& error) {
            BMCWEB_LOG_ERROR("{}: {}", desc, error);
            messages::resourceErrorsDetectedFormatError(asyncResp->res, desc,
                                                        error);
            boost::asio::post(crow::connections::systemBus->get_io_context(),
                              [] { dotOperation = nullptr; });
        };
    dotOperation = std::make_unique<dot::DotCommandHandler>(
        chassisID, command, data, resultHandler, errorHandler);
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
                if (!getBinaryKeyFromPem(cakKey, binaryKey))
                {
                    messages::actionParameterValueFormatError(
                        asyncResp->res, cakKey, "CAKKey", "CAKInstall");
                    return;
                }
                if (binaryKey.size() != DOT_KEY_SIZE)
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
                        (DOT_CAK_INSTALL_DATA_SIZE - DOT_KEY_SIZE - 1))
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
                executeDotCommand(asyncResp, chassisID,
                                  dot::DotMctpVdmUtilCommand::CAKInstall, data);
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
                if (!getBinaryKeyFromPem(key, binaryKey))
                {
                    messages::actionParameterValueFormatError(
                        asyncResp->res, key, "Key", "CAKLock");
                    return;
                }
                if (binaryKey.size() != DOT_KEY_SIZE)
                {
                    messages::propertyValueOutOfRange(
                        asyncResp->res, std::to_string(binaryKey.size()),
                        "Key size");
                    return;
                }
                executeDotCommand(asyncResp, chassisID,
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
                executeDotCommand(asyncResp, chassisID,
                                  dot::DotMctpVdmUtilCommand::CAKTest, data);
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
                if (!getBinaryKeyFromPem(key, binaryKey))
                {
                    messages::actionParameterValueFormatError(
                        asyncResp->res, key, "Key", "DOTDisable");
                    return;
                }
                if (binaryKey.size() != DOT_KEY_SIZE)
                {
                    messages::propertyValueOutOfRange(
                        asyncResp->res, std::to_string(binaryKey.size()),
                        "Key size");
                    return;
                }
                executeDotCommand(asyncResp, chassisID,
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
                if (req.body().size() != DOT_TOKEN_SIZE)
                {
                    BMCWEB_LOG_ERROR("Invalid DOT token size: {}",
                                     req.body().size());
                    messages::invalidUpload(asyncResp->res, "DOT token install",
                                            "filesize has to be equal to " +
                                                std::to_string(DOT_TOKEN_SIZE));
                    return;
                }
                std::vector<uint8_t> data(req.body().begin(), req.body().end());
                executeDotCommand(asyncResp, chassisID,
                                  dot::DotMctpVdmUtilCommand::DOTTokenInstall,
                                  data);
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
@brief - Performs ERoT chassis graceful reset using
/usr/bin/erot_reset_pre.sh and /usr/bin/erot_reset.sh scripts. The scripts
are platform-specific and need to be installed separately. Upon successful
reset, the ERoT reset will also reset the BMC by toggling the AP_reset pin.
There are three cases of failure:
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
    auto dataOut = std::make_shared<boost::process::ipstream>();
    auto dataErr = std::make_shared<boost::process::ipstream>();
    auto exitCallback = [asyncResp, dataOut, dataErr, erotResetPath,
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

        std::string command = erotResetPath + " " + std::to_string(endpointId);
        auto secondExitCallback =
            [](const boost::system::error_code& ec, int errorCode) mutable {
                BMCWEB_LOG_DEBUG("ec: {}  errorCode {}", ec, errorCode);
            };
        BMCWEB_LOG_DEBUG("Sending ERoT self-reset command");

        /* During the erotReset script, ERoT performs a self reset which
        leads to BMC external reset. Hence it is unnecessary to check its
        results */
        messages::success(asyncResp->res);

        bp::async_system(crow::connections::systemBus->get_io_context(),
                         std::move(secondExitCallback), command,
                         bp::std_in.close(), bp::std_out > *dataOut,
                         bp::std_err > *dataErr);
    };
    bp::async_system(crow::connections::systemBus->get_io_context(),
                     std::move(exitCallback), command, bp::std_in.close(),
                     bp::std_out > *dataOut, bp::std_err > *dataErr);
}

/**
@brief - Finds the endpoint ID associated with the given chassis UUID.
@param[in] - req: Request const variable
             asyncResp: Response const variable
             chassisUUID: ERoT chassis UUID
             isPCIe: Indicates if ERoT is connected via PCIe or SPI
*/
inline void
    findEIDforEROTReset(const crow::Request& req,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& chassisUUID, bool isPCIe = true)
{
    std::string serviceName = "xyz.openbmc_project.MCTP.Control.PCIe";
    if (!isPCIe)
    {
        serviceName = "xyz.openbmc_project.MCTP.Control.SPI";
    }

    crow::connections::systemBus->async_method_call(
        [req, asyncResp, chassisUUID,
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

            for (auto& objectPath : resp)
            {
                for (auto& interfaceMap : objectPath.second)
                {
                    if (interfaceMap.first == "xyz.openbmc_project.Common.UUID")
                    {
                        for (auto& propertyMap : interfaceMap.second)
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
                        for (auto& propertyMap : interfaceMap.second)
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
        [req, asyncResp, chassisId(std::string(chassisId))](
            const boost::system::error_code ec,
            const crow::openbmc_mapper::GetSubTreeType& subtree) {
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

                if (connectionNames.size() < 1)
                {
                    BMCWEB_LOG_ERROR("ERROR Got 0 Connection names");
                    continue;
                }

                chassisIdFound = true;

                sdbusplus::asio::getProperty<std::string>(
                    *crow::connections::systemBus, connectionNames[0].first,
                    path, "xyz.openbmc_project.Common.UUID", "UUID",
                    [req, asyncResp](const boost::system::error_code ec,
                                     const std::string& chassisUUID) {
                        if (ec)
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
            if (chassisIdFound == false)
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
