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

#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/hex_utils.hpp"
#include "utils/nvidia_chassis_util.hpp"

#include <charconv>
#include <string>

namespace redfish
{

namespace firmware_info
{

static constexpr auto securitySigningInterface =
    "xyz.openbmc_project.Security.Signing";
static constexpr auto securitySigningConfigInterface =
    "xyz.openbmc_project.Security.SigningConfig";
static constexpr auto softwareBuildTypeInterface =
    "xyz.openbmc_project.Software.BuildType";
static constexpr auto softwareSecurityCommonInterface =
    "xyz.openbmc_project.Software.SecurityCommon";
static constexpr auto softwareSigningInterface =
    "xyz.openbmc_project.Software.Signing";
static constexpr auto softwareStateInterface =
    "xyz.openbmc_project.Software.State";
static constexpr auto softwareSlotInterface =
    "xyz.openbmc_project.Software.Slot";
static constexpr auto securityVersionInterface =
    "xyz.openbmc_project.Software.SecurityVersion";
static constexpr auto securityConfigInterface =
    "xyz.openbmc_project.Software.SecurityConfig";
static constexpr auto minSecVersionConfigInterface =
    "xyz.openbmc_project.Software.MinSecVersionConfig";

static constexpr std::array<std::string_view, 6> propertyInterfaces = {
    securitySigningInterface, softwareBuildTypeInterface,
    softwareSigningInterface, softwareStateInterface,
    softwareSlotInterface,    securityVersionInterface};

static const std::string chassisDbusPath =
    "/xyz/openbmc_project/inventory/system/chassis/";

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
static std::unique_ptr<sdbusplus::bus::match_t> updateIrreversibleConfigMatch;
static std::unique_ptr<boost::asio::steady_timer> irreversibleConfigTimer;
static std::unique_ptr<sdbusplus::bus::match_t> updateMinSecVersionMatch;
static std::unique_ptr<boost::asio::steady_timer> updateMinSecVersionTimer;
static std::unique_ptr<sdbusplus::bus::match_t> revokeKeysMatch;
static std::unique_ptr<boost::asio::steady_timer> revokeKeysTimer;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
static constexpr auto timeoutTimeSeconds = 10;

static inline void clearSecVersion()
{
    updateMinSecVersionMatch = nullptr;
    updateMinSecVersionTimer.reset();
    updateMinSecVersionTimer = nullptr;
}

static inline void clearRevokeKeys()
{
    revokeKeysMatch = nullptr;
    revokeKeysTimer.reset();
    revokeKeysTimer = nullptr;
}

static inline std::string getStrAfterLastDot(const std::string& text)
{
    size_t lastDot = text.find_last_of('.');
    if (lastDot != std::string::npos)
    {
        return text.substr(lastDot + 1);
    }

    return text;
}

/**
 * @brief Update slot properties
 * @param asyncResp Async response object
 * @param service Service name
 * @param objectPath Object path
 * @return void
 */
inline void updateSlotProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objectPath)
{
    dbus::utility::getAllProperties(
        service, objectPath, "",
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                if (ec == boost::system::errc::host_unreachable)
                {
                    BMCWEB_LOG_ERROR("Service not available {}", ec);
                    return;
                }
                BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            for (const auto& [key, val] : properties)
            {
                if (key == "SlotId")
                {
                    if (const uint8_t* value = std::get_if<uint8_t>(&val))
                    {
                        asyncResp->res.jsonValue["SlotId"] = *value;
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("Null value returned for {}", key);
                    }
                }
                else if (key == "FirmwareComparisonNumber")
                {
                    if (const uint32_t* value = std::get_if<uint32_t>(&val))
                    {
                        asyncResp->res.jsonValue["FirmwareComparisonNumber"] =
                            *value;
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("Null value returned for {}", key);
                    }
                }
                else if (key == "ExtendedVersion")
                {
                    if (const std::string* value =
                            std::get_if<std::string>(&val))
                    {
                        asyncResp->res.jsonValue["Version"] = *value;
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("Null value returned for {}", key);
                    }
                }
                else if (key == "BuildType")
                {
                    if (const std::string* value =
                            std::get_if<std::string>(&val))
                    {
                        std::string buildType = getStrAfterLastDot(*value);
                        if (buildType == "Development")
                        {
                            asyncResp->res.jsonValue["BuildType"] =
                                "Development";
                        }
                        else if (buildType == "Release")
                        {
                            asyncResp->res.jsonValue["BuildType"] = "Release";
                        }
                        else
                        {
                            asyncResp->res.jsonValue["BuildType"] = nullptr;
                        }
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("Null value returned for {}", key);
                    }
                }
                else if (key == "State")
                {
                    if (const std::string* value =
                            std::get_if<std::string>(&val))
                    {
                        asyncResp->res.jsonValue["FirmwareState"] =
                            getStrAfterLastDot(*value);
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("Null value returned for {}", key);
                    }
                }
                else if (key == "WriteProtected")
                {
                    if (const bool* value = std::get_if<bool>(&val))
                    {
                        asyncResp->res.jsonValue["WriteProtected"] = *value;
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("Null value returned for {}", key);
                    }
                }
                else if (key == "Version")
                {
                    if (const uint16_t* value = std::get_if<uint16_t>(&val))
                    {
                        asyncResp->res.jsonValue["SecurityVersion"] = *value;
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("Null value returned for {}", key);
                    }
                }
                else if (key == "SigningType")
                {
                    if (const std::string* value =
                            std::get_if<std::string>(&val))
                    {
                        asyncResp->res.jsonValue["SigningType"] =
                            getStrAfterLastDot(*value);
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("Null value returned for {}", key);
                    }
                }
                else if (key == "SigningKeyIndex")
                {
                    if (const uint8_t* value = std::get_if<uint8_t>(&val))
                    {
                        asyncResp->res.jsonValue["SigningKeyIndex"] = *value;
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("Null value returned for {}", key);
                    }
                }
                else if (key == "TrustedKeys")
                {
                    if (const std::vector<uint8_t>* value =
                            std::get_if<std::vector<uint8_t>>(&val))
                    {
                        asyncResp->res.jsonValue["AllowedKeyIndices"] = *value;
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("Null value returned for {}", key);
                    }
                }
                else if (key == "RevokedKeys")
                {
                    if (const std::vector<uint8_t>* value =
                            std::get_if<std::vector<uint8_t>>(&val))
                    {
                        asyncResp->res.jsonValue["RevokedKeyIndices"] = *value;
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("Null value returned for {}", key);
                    }
                }
            }
        });
}

/**
 * @brief Process image slot properties
 * @param asyncResp Async response object
 * @param chassisId Chassis ID
 * @param fwTypeStr Firmware type string
 * @param slotNumStr Slot number string
 * @param slotNum Slot number
 * @param service Service name
 * @param objectPath Object path
 * @param propertiesList Properties list
 * @return void
 */
inline void processImageSlotProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& fwTypeStr,
    const std::string& slotNumStr, const std::string& service,
    const std::string& objectPath,
    const dbus::utility::DBusPropertiesMap& propertiesList)
{
    std::optional<uint8_t> slotId;
    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), propertiesList, "SlotId", slotId);

    if (!success)
    {
        BMCWEB_LOG_ERROR("Unpack Slot properties error");
        messages::internalError(asyncResp->res);
        return;
    }

    asyncResp->res.jsonValue["Name"] =
        std::format("{} RoTProtectedComponent {} ImageSlot {}", chassisId,
                    fwTypeStr, slotNumStr);
    asyncResp->res.jsonValue["Id"] = slotNumStr;
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaRoTImageSlot.v1_0_0.NvidiaRoTImageSlot";
    asyncResp->res.jsonValue["@odata.id"] = std::format(
        "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/RoTProtectedComponents/{}/ImageSlots/{}",
        chassisId, fwTypeStr, slotNumStr);
    updateSlotProperties(asyncResp, service, objectPath);
}

/**
 * @brief Process Nvidia RoT image slot subtree
 * @param asyncResp Async response object
 * @param chassisId Chassis ID
 * @param fwTypeStr Firmware type string
 * @param slotNumStr Slot number string
 * @param subtree Subtree
 */
inline void processNvidiaRoTImageSlotSubtree(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& fwTypeStr,
    const std::string& slotNumStr,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    auto dbusComponentId = fwTypeStr == "Self" ? chassisId : fwTypeStr;

    std::vector<std::pair<std::string, std::string>> cachedPathServices;
    for (const auto& [objectPath, serviceMap] : subtree)
    {
        sdbusplus::message::object_path path(objectPath);
        if (path.filename() != dbusComponentId)
        {
            continue;
        }

        for (const auto& [service, interfaces] : serviceMap)
        {
            auto it = std::find_if(std::begin(interfaces), std::end(interfaces),
                                   [](const auto& element) {
                                       return element == softwareSlotInterface;
                                   });
            if (it == std::end(interfaces))
            {
                continue;
            }
            cachedPathServices.emplace_back(objectPath, service);
            break;
        }
    }

    if (cachedPathServices.empty())
    {
        BMCWEB_LOG_ERROR("Slot entry not found for {}.{}", chassisId,
                         slotNumStr);
        messages::resourceNotFound(asyncResp->res, "NvidiaRoTImageSlot",
                                   slotNumStr);
        return;
    }

    unsigned int slotNum = 0;
    std::string_view slotNumView(slotNumStr);
    auto [ptr, parseEc] =
        std::from_chars(slotNumView.begin(), slotNumView.end(), slotNum);
    if (parseEc != std::errc{} || ptr != slotNumView.end())
    {
        messages::resourceNotFound(asyncResp->res, "NvidiaRoTImageSlot",
                                   slotNumStr);
        return;
    }

    if (slotNum >= cachedPathServices.size())
    {
        messages::resourceNotFound(asyncResp->res, "NvidiaRoTImageSlot",
                                   slotNumStr);
        return;
    }

    auto [objectPath, service] = cachedPathServices[slotNum];
    dbus::utility::getAllProperties(
        service, objectPath, softwareSlotInterface,
        [asyncResp, chassisId, fwTypeStr, slotNumStr, service,
         objectPath](const boost::system::error_code& ec,
                     const dbus::utility::DBusPropertiesMap& propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            processImageSlotProperties(asyncResp, chassisId, fwTypeStr,
                                       slotNumStr, service, objectPath,
                                       propertiesList);
        });
}

/**
 * @brief Handle Nvidia RoT image slot
 * @param app App
 * @param req Request
 * @param asyncResp Async response object
 * @param chassisId Chassis ID
 * @param fwTypeStr Firmware type string
 * @param slotNumStr Slot number string
 */
inline void handleNvidiaRoTImageSlot(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& fwTypeStr,
    const std::string& slotNumStr)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    dbus::utility::getSubTree(
        chassisDbusPath + chassisId, 0, propertyInterfaces,
        [chassisId, fwTypeStr, slotNumStr,
         asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("D-Bus error: {}, {}", ec, ec.message());
                messages::internalError(asyncResp->res);
                return;
            }

            processNvidiaRoTImageSlotSubtree(asyncResp, chassisId, fwTypeStr,
                                             slotNumStr, subtree);
        });
}

/**
 * @brief Update protected component link
 * @param asyncResp Async response object
 * @param chassisId Chassis ID
 * @return void
 */
inline void updateProtectedComponentLink(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    dbus::utility::getSubTreePaths(
        chassisDbusPath + chassisId, 0, propertyInterfaces,
        [chassisId, asyncResp](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreePathsResponse& subtreePaths) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Service not available {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            if (!subtreePaths.empty())
            {
                asyncResp->res
                    .jsonValue["Oem"]["Nvidia"]["RoTProtectedComponents"] = {
                    {"@odata.id",
                     boost::urls::format("/redfish/v1/Chassis/{}/Oem/NvidiaRoT/"
                                         "RoTProtectedComponents",
                                         chassisId)}};
            }
        });
}

/**
 * @brief Process component collection properties
 * @param asyncResp Async response object
 * @param chassisId Chassis ID
 * @param objectPath Object path
 * @param propertiesList Properties list
 */
inline void processComponentCollectionProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& objectPath,
    const dbus::utility::DBusPropertiesMap& propertiesList)
{
    std::optional<uint8_t> slotId;
    std::optional<std::string> fwType;
    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), propertiesList, "SlotId", slotId,
        "Type", fwType);

    if (!success)
    {
        BMCWEB_LOG_ERROR("Unpack Slot properties error");
        messages::internalError(asyncResp->res);
        return;
    }

    if (slotId && fwType && *slotId == 0)
    {
        if (*fwType == "xyz.openbmc_project.Software.Slot.FirmwareType.EC")
        {
            asyncResp->res.jsonValue["Members"].push_back(
                {{"@odata.id",
                  std::format(
                      "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/RoTProtectedComponents/Self",
                      chassisId)}});
            asyncResp->res.jsonValue["Members@odata.count"] =
                asyncResp->res.jsonValue["Members"].size();
        }
        else if (*fwType == "xyz.openbmc_project.Software.Slot.FirmwareType.AP")
        {
            sdbusplus::message::object_path path(objectPath);
            auto componentId = path.filename();
            asyncResp->res.jsonValue["Members"].push_back(
                {{"@odata.id",
                  std::format(
                      "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/RoTProtectedComponents/{}",
                      chassisId, componentId)}});
            asyncResp->res.jsonValue["Members@odata.count"] =
                asyncResp->res.jsonValue["Members"].size();
        }
    }
}

/**
 * @brief Process Nvidia RoT protected component collection subtree
 * @param asyncResp Async response object
 * @param chassisId Chassis ID
 * @param subtree Subtree
 */
inline void processNvidiaRoTProtectedComponentCollectionSubtree(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    asyncResp->res.jsonValue["@odata.id"] = std::format(
        "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/RoTProtectedComponents",
        chassisId);
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaRoTProtectedComponentCollection.NvidiaRoTProtectedComponentCollection";
    asyncResp->res.jsonValue["Name"] =
        std::format("{} RoTProtectedComponent Collection", chassisId);
    asyncResp->res.jsonValue["Members"] = nlohmann::json::array();

    std::vector<std::pair<std::string, std::string>> cachedPathServices;
    for (const auto& [objectPath, serviceMap] : subtree)
    {
        for (const auto& [service, interfaces] : serviceMap)
        {
            cachedPathServices.emplace_back(objectPath, service);
            break;
        }
    }
    if (cachedPathServices.empty())
    {
        BMCWEB_LOG_ERROR(
            "NvidiaRoTProtectedComponentCollection entry not found for {}",
            chassisId);
        messages::resourceNotFound(
            asyncResp->res, "NvidiaRoTProtectedComponentCollection", chassisId);
        return;
    }

    for (const auto& [objectPath, service] : cachedPathServices)
    {
        dbus::utility::getAllProperties(
            service, objectPath, softwareSlotInterface,
            [asyncResp, chassisId, objectPath](
                const boost::system::error_code& ec,
                const dbus::utility::DBusPropertiesMap& propertiesList) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                    messages::internalError(asyncResp->res);
                    return;
                }
                processComponentCollectionProperties(
                    asyncResp, chassisId, objectPath, propertiesList);
            });
    }
}

/**
 * @brief Handle Nvidia RoT protected component collection
 * @param app App
 * @param req Request
 * @param asyncResp Async response object
 * @param chassisId Chassis ID
 */
inline void handleNvidiaRoTProtectedComponentCollection(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    dbus::utility::getSubTree(
        chassisDbusPath + chassisId, 0,
        std::array<std::string_view, 1>{softwareSlotInterface},
        [chassisId,
         asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                if (ec == boost::system::errc::host_unreachable)
                {
                    BMCWEB_LOG_ERROR("Service not available {}", ec);
                    messages::internalError(asyncResp->res);
                    return;
                }
                BMCWEB_LOG_ERROR("D-Bus error: {}, {}", ec, ec.message());
                messages::resourceNotFound(
                    asyncResp->res, "NvidiaRoTProtectedComponentCollection",
                    chassisId);
                return;
            }

            processNvidiaRoTProtectedComponentCollectionSubtree(
                asyncResp, chassisId, subtree);
        });
}

/**
 * @brief Process Nvidia RoT image slot subtree
 * @param asyncResp Async response object
 * @param chassisId Chassis ID
 * @param fwTypeStr Firmware type string
 * @param subtree Subtree
 */
inline void processNvidiaRoTImageSlotSubtree(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& fwTypeStr,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    auto dbusComponentId = fwTypeStr == "Self" ? chassisId : fwTypeStr;

    std::vector<std::pair<std::string, std::string>> cachedPathServices;
    for (const auto& [objectPath, serviceMap] : subtree)
    {
        sdbusplus::message::object_path path(objectPath);
        if (path.filename() != dbusComponentId)
        {
            continue;
        }

        for (const auto& [service, interfaces] : serviceMap)
        {
            auto it = std::find_if(std::begin(interfaces), std::end(interfaces),
                                   [](const auto& element) {
                                       return element == softwareSlotInterface;
                                   });
            if (it == std::end(interfaces))
            {
                continue;
            }
            cachedPathServices.emplace_back(objectPath, service);
            break;
        }
    }

    if (cachedPathServices.empty())
    {
        BMCWEB_LOG_ERROR(
            "NvidiaRoTImageSlotCollection entry not found for {}.{}", chassisId,
            fwTypeStr);
        messages::resourceNotFound(asyncResp->res,
                                   "NvidiaRoTImageSlotCollection", fwTypeStr);
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaRoTImageSlotCollection.NvidiaRoTImageSlotCollection";
    asyncResp->res.jsonValue["@odata.id"] = std::format(
        "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/RoTProtectedComponents/{}/ImageSlots",
        chassisId, fwTypeStr);
    asyncResp->res.jsonValue["Name"] = std::format(
        "{} RoTProtectedComponent {} ImageSlot", chassisId, fwTypeStr);

    for (size_t slotIndex = 0; slotIndex < cachedPathServices.size();
         ++slotIndex)
    {
        auto memberId = boost::urls::format(
            "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/RoTProtectedComponents/{}/ImageSlots/{}",
            chassisId, fwTypeStr, slotIndex);
        asyncResp->res.jsonValue["Members"].push_back(
            {{"@odata.id", memberId}});
    }
    asyncResp->res.jsonValue["Members@odata.count"] = cachedPathServices.size();
}

/**
 * @brief Handle Nvidia RoT image slot collection
 * @param app App
 * @param req Request
 * @param asyncResp Async response object
 * @param chassisId Chassis ID
 * @param fwTypeStr Firmware type string
 */
inline void handleNvidiaRoTImageSlotCollection(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& fwTypeStr)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    dbus::utility::getSubTree(
        chassisDbusPath + chassisId, 0,
        std::array<std::string_view, 1>{softwareSlotInterface},
        [chassisId, fwTypeStr,
         asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("D-Bus error: {}, {}", ec, ec.message());
                messages::internalError(asyncResp->res);
                return;
            }
            processNvidiaRoTImageSlotSubtree(asyncResp, chassisId, fwTypeStr,
                                             subtree);
        });
}

inline void updateSigningKeyProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    static constexpr std::array<std::string_view, 2> signingInterfaces = {
        securitySigningInterface, securitySigningConfigInterface};
    std::string securityPath;
    if (componentId == "Self")
    {
        securityPath = std::format("{}{}", chassisDbusPath, chassisId);
    }
    else
    {
        securityPath = std::format("{}{}", chassisDbusPath, componentId);
    }
    dbus::utility::getDbusObject(
        securityPath, signingInterfaces,
        [asyncResp, chassisId, securityPath,
         componentId](const boost::system::error_code& ec,
                      const ::dbus::utility::MapperGetObject& mapperResponse) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Signing interfaces not present : {}, {}", ec,
                                 ec.message());
                return;
            }
            if (mapperResponse.size() != 1)
            {
                BMCWEB_LOG_ERROR("Invalid response for GetObject: {}, {}", ec,
                                 ec.message());
                messages::internalError(asyncResp->res);
                return;
            }
            const auto& valueIface = *mapperResponse.begin();
            const std::string& service = valueIface.first;
            dbus::utility::getAllProperties(
                service, securityPath, securitySigningInterface,
                [asyncResp, chassisId, componentId](
                    const boost::system::error_code ec1,
                    const dbus::utility::DBusPropertiesMap& properties) {
                    if (ec1)
                    {
                        if (ec1 == boost::system::errc::host_unreachable)
                        {
                            // Service not available, no error, just don't
                            // return chassis state info
                            BMCWEB_LOG_ERROR("Service not available {}", ec1);
                            return;
                        }
                        BMCWEB_LOG_ERROR("DBUS response error {}", ec1);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if (!properties.empty())
                    {
                        auto revokeKeysTarget = boost::urls::format(
                            "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/RoTProtectedComponents"
                            "/{}/Actions/NvidiaRoTProtectedComponent"
                            ".RevokeKeys",
                            chassisId, componentId);
                        auto revokeKeysInfo = boost::urls::format(
                            "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/RoTProtectedComponents"
                            "/{}/RevokeKeysActionInfo",
                            chassisId, componentId);
                        asyncResp->res.jsonValue
                            ["Actions"]
                            ["#NvidiaRoTProtectedComponent.RevokeKeys"]
                            ["target"] = revokeKeysTarget;
                        asyncResp->res.jsonValue
                            ["Actions"]
                            ["#NvidiaRoTProtectedComponent.RevokeKeys"]
                            ["@Redfish.ActionInfo"] = revokeKeysInfo;
                    }
                    for (const auto& [key, val] : properties)
                    {
                        if (key == "SigningKeyIndex")
                        {
                            if (const uint8_t* value =
                                    std::get_if<uint8_t>(&val))
                            {
                                asyncResp->res
                                    .jsonValue["ActiveKeySetIdentifier"] =
                                    *value;
                            }
                            else
                            {
                                BMCWEB_LOG_ERROR("Null value returned for {}",
                                                 key);
                            }
                        }
                        else if (key == "TrustedKeys")
                        {
                            if (const std::vector<uint8_t>* value =
                                    std::get_if<std::vector<uint8_t>>(&val))
                            {
                                asyncResp->res.jsonValue["AllowedKeyIndices"] =
                                    *value;
                            }
                            else
                            {
                                BMCWEB_LOG_ERROR("Null value returned for {}",
                                                 key);
                            }
                        }
                        else if (key == "RevokedKeys")
                        {
                            if (const std::vector<uint8_t>* value =
                                    std::get_if<std::vector<uint8_t>>(&val))
                            {
                                asyncResp->res.jsonValue["RevokedKeyIndices"] =
                                    *value;
                            }
                            else
                            {
                                BMCWEB_LOG_ERROR("Null value returned for {}",
                                                 key);
                            }
                        }
                    }
                });
        });
}

inline void updateSecurityVersionProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    static constexpr std::array<std::string_view, 2> securityInterfaces = {
        securityVersionInterface, minSecVersionConfigInterface};
    std::string securityPath;
    if (componentId == "Self")
    {
        securityPath = std::format("{}{}", chassisDbusPath, chassisId);
    }
    else
    {
        securityPath = std::format("{}{}", chassisDbusPath, componentId);
    }
    dbus::utility::getDbusObject(
        securityPath, securityInterfaces,
        [asyncResp, chassisId, securityPath,
         componentId](const boost::system::error_code& ec,
                      const ::dbus::utility::MapperGetObject& mapperResponse) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "SecurityConfig interface not present : {}, {}", ec,
                    ec.message());
                return;
            }
            if (mapperResponse.size() != 1)
            {
                BMCWEB_LOG_ERROR("Invalid response for GetObject: {}, {}", ec,
                                 ec.message());
                messages::internalError(asyncResp->res);
                return;
            }
            const auto& valueIface = *mapperResponse.begin();
            const std::string& service = valueIface.first;
            dbus::utility::getProperty<uint16_t>(
                service, securityPath, securityVersionInterface, "Version",
                [asyncResp, chassisId,
                 componentId](const boost::system::error_code& ec1,
                              const uint16_t property) {
                    if (ec1)
                    {
                        BMCWEB_LOG_ERROR(
                            "MinSecurityVersion DBUS response error");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["MinimumSecurityVersion"] =
                        property;
                    auto updateMinSecVersionTarget = boost::urls::format(
                        "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/RoTProtectedComponents"
                        "/{}/Actions/NvidiaRoTProtectedComponent"
                        ".UpdateMinimumSecurityVersion",
                        chassisId, componentId);
                    auto updateMinSecVersionInfo = boost::urls::format(
                        "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/RoTProtectedComponents"
                        "/{}/UpdateMinimumSecurityVersionActionInfo",
                        chassisId, componentId);
                    asyncResp->res.jsonValue
                        ["Actions"]
                        ["#NvidiaRoTProtectedComponent.UpdateMinimumSecurityVersion"]
                        ["target"] = updateMinSecVersionTarget;
                    asyncResp->res.jsonValue
                        ["Actions"]
                        ["#NvidiaRoTProtectedComponent.UpdateMinimumSecurityVersion"]
                        ["@Redfish.ActionInfo"] = updateMinSecVersionInfo;
                });
        });
}

inline void updatePendingProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    static constexpr std::array<std::string_view, 2> securityInterfaces = {
        securitySigningInterface, securityVersionInterface};
    std::string securityPath;
    if (componentId == "Self")
    {
        securityPath = std::format("{}{}/Settings", chassisDbusPath, chassisId);
    }
    else
    {
        securityPath =
            std::format("{}{}/Settings", chassisDbusPath, componentId);
    }
    dbus::utility::getDbusObject(
        securityPath, securityInterfaces,
        [asyncResp, chassisId, securityPath,
         componentId](const boost::system::error_code& ec,
                      const ::dbus::utility::MapperGetObject& mapperResponse) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "SecurityConfig interface not present : {}, {}", ec,
                    ec.message());
                return;
            }
            if (mapperResponse.size() != 1)
            {
                BMCWEB_LOG_ERROR("Invalid response for GetObject: {}, {}", ec,
                                 ec.message());
                messages::internalError(asyncResp->res);
                return;
            }
            const auto& valueIface = *mapperResponse.begin();
            const std::string& service = valueIface.first;
            dbus::utility::getAllProperties(
                service, securityPath, "",
                [asyncResp](
                    const boost::system::error_code ec1,
                    const dbus::utility::DBusPropertiesMap& properties) {
                    if (ec1)
                    {
                        if (ec1 == boost::system::errc::host_unreachable)
                        {
                            BMCWEB_LOG_ERROR("Service not available {}", ec1);
                            return;
                        }
                        BMCWEB_LOG_ERROR("DBUS response error {}", ec1);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    for (const auto& [key, val] : properties)
                    {
                        if (key == "Version")
                        {
                            if (const uint16_t* value =
                                    std::get_if<uint16_t>(&val))
                            {
                                asyncResp->res
                                    .jsonValue["MinimumSecurityVersion"] =
                                    *value;
                            }
                            else
                            {
                                BMCWEB_LOG_ERROR("Null value returned for {}",
                                                 key);
                            }
                        }
                        else if (key == "TrustedKeys")
                        {
                            if (const std::vector<uint8_t>* value =
                                    std::get_if<std::vector<uint8_t>>(&val))
                            {
                                asyncResp->res.jsonValue["AllowedKeyIndices"] =
                                    *value;
                            }
                            else
                            {
                                BMCWEB_LOG_ERROR("Null value returned for {}",
                                                 key);
                            }
                        }
                        else if (key == "RevokedKeys")
                        {
                            if (const std::vector<uint8_t>* value =
                                    std::get_if<std::vector<uint8_t>>(&val))
                            {
                                asyncResp->res.jsonValue["RevokedKeyIndices"] =
                                    *value;
                            }
                            else
                            {
                                BMCWEB_LOG_ERROR("Null value returned for {}",
                                                 key);
                            }
                        }
                    }
                });
        });
}

/**
 * @brief Process protected component settings
 * @param asyncResp Async response object
 * @param chassisId Chassis ID
 * @param fwTypeStr Firmware type string
 */
inline void processNvidiaRoTProtectedComponentSettings(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& fwTypeStr)
{
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/RoTProtectedComponents/{}/Settings",
        chassisId, fwTypeStr);
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaRoTProtectedComponent.v1_0_0.NvidiaRoTProtectedComponent";
    asyncResp->res.jsonValue["Name"] = std::format(
        "{} RoTProtectedComponent {} Pending Settings", chassisId, fwTypeStr);
    asyncResp->res.jsonValue["Id"] = "Settings";
    updatePendingProperties(asyncResp, chassisId, fwTypeStr);
}

inline void handleNvidiaRoTProtectedComponentSettings(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& fwTypeStr)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    processNvidiaRoTProtectedComponentSettings(asyncResp, chassisId, fwTypeStr);
}

/**
 * @brief Process protected component properties
 * @param asyncResp Async response object
 * @param chassisId Chassis ID
 * @param fwTypeStr Firmware type string
 * @param ec Error code
 * @param propertiesList Properties list
 */
inline void processProtectedComponentProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& fwTypeStr,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& propertiesList)
{
    if (ec)
    {
        if (ec == boost::system::errc::host_unreachable)
        {
            BMCWEB_LOG_ERROR("Service not available {}", ec);
            return;
        }
        BMCWEB_LOG_ERROR("DBUS response error {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }
    std::optional<uint8_t> slotId;
    std::optional<bool> isActive;
    std::optional<std::string> fwType;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), propertiesList, "SlotId", slotId,
        "IsActive", isActive, "Type", fwType);

    if (!success)
    {
        BMCWEB_LOG_ERROR("Unpack Slot properties error");
        messages::internalError(asyncResp->res);
        return;
    }
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/RoTProtectedComponents/{}",
        chassisId, fwTypeStr);
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaRoTProtectedComponent.v1_0_0.NvidiaRoTProtectedComponent";
    asyncResp->res.jsonValue["Name"] =
        std::format("{} RoTProtectedComponent {}", chassisId, fwTypeStr);
    asyncResp->res.jsonValue["Id"] = fwTypeStr;
    if (fwType &&
        *fwType == "xyz.openbmc_project.Software.Slot.FirmwareType.AP")
    {
        asyncResp->res.jsonValue["RoTProtectedComponentType"] = "AP";
    }
    else
    {
        asyncResp->res.jsonValue["RoTProtectedComponentType"] = "Self";
    }
    auto slotUrl = boost::urls::format(
        "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/RoTProtectedComponents/{}/ImageSlots",
        chassisId, fwTypeStr);
    asyncResp->res.jsonValue["ImageSlots"] = {{"@odata.id", slotUrl}};

    auto settingsUrl = boost::urls::format(
        "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/RoTProtectedComponents/{}/Settings",
        chassisId, fwTypeStr);
    asyncResp->res.jsonValue["@Redfish.Settings"] = {
        {"@odata.type", "#Settings.v1_3_3.Settings"},
        {"SettingsObject", {{"@odata.id", settingsUrl}}}};

    if (slotId && isActive && *isActive)
    {
        asyncResp->res.jsonValue["ActiveSlotId"] = *slotId;
    }

    redfish::nvidia_chassis_utils::getOemBootStatus(asyncResp, chassisId);
    updateSigningKeyProperties(asyncResp, chassisId, fwTypeStr);
    updateSecurityVersionProperties(asyncResp, chassisId, fwTypeStr);
}

/**
 * @brief Process Nvidia RoT protected component subtree
 * @param asyncResp Async response object
 * @param chassisId Chassis ID
 * @param fwTypeStr Firmware type string
 * @param subtree Subtree
 */
inline void processNvidiaRoTProtectedComponentSubtree(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& fwTypeStr,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    auto dbusComponentId = fwTypeStr == "Self" ? chassisId : fwTypeStr;

    std::vector<std::pair<std::string, std::string>> cachedPathServices;
    for (const auto& [objectPath, serviceMap] : subtree)
    {
        sdbusplus::message::object_path path(objectPath);
        if (path.filename() != dbusComponentId)
        {
            continue;
        }

        for (const auto& [service, interfaces] : serviceMap)
        {
            cachedPathServices.emplace_back(objectPath, service);
            break;
        }
    }

    if (cachedPathServices.empty())
    {
        BMCWEB_LOG_ERROR("Slot entry not found for {}.{}", chassisId,
                         dbusComponentId);
        messages::resourceNotFound(asyncResp->res,
                                   "NvidiaRoTProtectedComponent", fwTypeStr);
        return;
    }

    for (const auto& [objectPath, service] : cachedPathServices)
    {
        dbus::utility::getAllProperties(
            service, objectPath, softwareSlotInterface,
            [asyncResp, chassisId, fwTypeStr](
                const boost::system::error_code& ec,
                const dbus::utility::DBusPropertiesMap& propertiesList) {
                processProtectedComponentProperties(
                    asyncResp, chassisId, fwTypeStr, ec, propertiesList);
            });
    }
}

/**
 * @brief Handle Nvidia RoT protected component
 * @param app App
 * @param req Request
 * @param asyncResp Async response object
 * @param chassisId Chassis ID
 * @param fwTypeStr Firmware type string
 */
inline void handleNvidiaRoTProtectedComponent(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& fwTypeStr)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    dbus::utility::getSubTree(
        chassisDbusPath + chassisId, 0,
        std::array<std::string_view, 1>{softwareSlotInterface},
        [chassisId, fwTypeStr,
         asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("D-Bus error: {}, {}", ec, ec.message());
                messages::resourceNotFound(
                    asyncResp->res, "NvidiaRoTProtectedComponent", fwTypeStr);
                return;
            }

            processNvidiaRoTProtectedComponentSubtree(asyncResp, chassisId,
                                                      fwTypeStr, subtree);
        });
}

inline void handleSetIrreversibleConfigActionInfo(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/SetIrreversibleConfigActionInfo",
        chassisId);
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_2_0.ActionInfo";
    asyncResp->res.jsonValue["Id"] = "SetIrreversibleConfigActionInfo";
    asyncResp->res.jsonValue["Name"] = "Set Irreversible Config ActionInfo";
    nlohmann::json parameterTargets;
    parameterTargets["Name"] = "RequestType";
    parameterTargets["Required"] = true;
    parameterTargets["DataType"] = "String";
    parameterTargets["AllowableValues"] = {"Enable", "Disable"};
    asyncResp->res.jsonValue["Parameters"] = {parameterTargets};
}

inline void updateIrreversibleConfigEnabled(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    static constexpr std::array<std::string_view, 1> cfgIntf = {
        securityConfigInterface};
    auto chassisCfgPath = std::format("{}{}", chassisDbusPath, chassisId);
    dbus::utility::getDbusObject(
        chassisCfgPath, cfgIntf,
        [asyncResp, chassisId, chassisCfgPath](
            const boost::system::error_code& ec,
            const ::dbus::utility::MapperGetObject& mapperResponse) {
            if (ec)
            {
                BMCWEB_LOG_INFO("SecurityConfig interface not present : {}, {}",
                                ec, ec.message());
                return;
            }
            if (mapperResponse.size() != 1)
            {
                BMCWEB_LOG_ERROR("Invalid response for GetObject: {}, {}", ec,
                                 ec.message());
                messages::internalError(asyncResp->res);
                return;
            }
            const auto& valueIface = *mapperResponse.begin();
            const std::string& service = valueIface.first;
            dbus::utility::getProperty<bool>(
                service, chassisCfgPath, securityConfigInterface,
                "IrreversibleConfigState",
                [asyncResp, chassisId](const boost::system::error_code& ec1,
                                       const bool property) {
                    if (ec1)
                    {
                        BMCWEB_LOG_ERROR(
                            "updateIrreversibleConfigEnabled DBUS response error");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                        "#NvidiaChassis.v1_3_0.NvidiaRoTChassis";
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                            ["IrreversibleConfigEnabled"] =
                        property;
                    auto cfgTarget = boost::urls::format(
                        "/redfish/v1/Chassis/{}/Actions/Oem/"
                        "NvidiaRoTChassis.SetIrreversibleConfig",
                        chassisId);
                    auto cfgTargetActionInfo = boost::urls::format(
                        "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/"
                        "SetIrreversibleConfigActionInfo",
                        chassisId);
                    asyncResp->res
                        .jsonValue["Actions"]["Oem"]
                                  ["#NvidiaRoTChassis.SetIrreversibleConfig"]
                                  ["target"] = cfgTarget;
                    asyncResp->res
                        .jsonValue["Actions"]["Oem"]
                                  ["#NvidiaRoTChassis.SetIrreversibleConfig"]
                                  ["@Redfish.ActionInfo"] = cfgTargetActionInfo;
                });
        });
}

inline void handleIrreversibleConfigResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& chassisCfgPath,
    sdbusplus::message_t& msg, bool state)
{
    std::string interface;
    boost::container::flat_map<std::string, dbus::utility::DbusVariantType>
        values;
    msg.read(interface, values);
    if (interface == "xyz.openbmc_project.Common.Progress")
    {
        auto progress = values.find("Status");
        if (progress != values.end())
        {
            auto* value = std::get_if<std::string>(&(progress->second));
            if (value == nullptr)
            {
                return;
            }
            if (*value ==
                "xyz.openbmc_project.Common.Progress.OperationStatus.Completed")
            {
                if (!state) // Disable, Success
                {
                    messages::success(asyncResp->res);
                    updateIrreversibleConfigMatch = nullptr;
                    irreversibleConfigTimer = nullptr;
                    return;
                }
                // Enable, return Nonce
                dbus::utility::getProperty<uint64_t>(
                    service, chassisCfgPath, securityConfigInterface, "Nonce",
                    [asyncResp](const boost::system::error_code& ec1,
                                const uint64_t property) {
                        if (ec1)
                        {
                            BMCWEB_LOG_ERROR(
                                "updateIrreversibleConfigEnabled DBUS error");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res.jsonValue["Nonce"] =
                            intToHexString(property, 16);
                        updateIrreversibleConfigMatch = nullptr;
                        irreversibleConfigTimer = nullptr;
                    });
                return;
            }
            BMCWEB_LOG_ERROR("updateIrreversibleConfigEnabled Method failed");
            messages::internalError(asyncResp->res);
            updateIrreversibleConfigMatch = nullptr;
            irreversibleConfigTimer = nullptr;
            return;
        }
    }
}

inline void setIrreversibleConfig(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, bool state)
{
    static constexpr std::array<std::string_view, 1> cfgIntf = {
        securityConfigInterface};
    auto chassisCfgPath = std::format("{}{}", chassisDbusPath, chassisId);
    dbus::utility::getDbusObject(
        chassisCfgPath, cfgIntf,
        [asyncResp, chassisId, chassisCfgPath,
         state](const boost::system::error_code& ec,
                const ::dbus::utility::MapperGetObject& mapperResponse) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "SecurityConfig interface not present : {}, {}", ec,
                    ec.message());
                messages::resourceNotFound(asyncResp->res,
                                           "SetIrreversibleConfig", chassisId);
                return;
            }
            if (mapperResponse.size() != 1)
            {
                BMCWEB_LOG_ERROR("Invalid response for GetObject: {}, {}", ec,
                                 ec.message());
                messages::resourceNotFound(asyncResp->res,
                                           "SetIrreversibleConfig", chassisId);
                return;
            }
            const auto& valueIface = *mapperResponse.begin();
            const std::string& service = valueIface.first;
            irreversibleConfigTimer =
                std::make_unique<boost::asio::steady_timer>(getIoContext());
            irreversibleConfigTimer->expires_after(
                std::chrono::seconds(timeoutTimeSeconds));
            irreversibleConfigTimer->async_wait(
                [asyncResp](const boost::system::error_code& ec1) {
                    if (ec1 == boost::asio::error::operation_aborted)
                    {
                        // expected, we were canceled before the timer
                        // completed.
                        return;
                    }
                    BMCWEB_LOG_ERROR(
                        "Timed out waiting for IrreversibleConfig response");
                    updateIrreversibleConfigMatch = nullptr;
                    if (ec1)
                    {
                        BMCWEB_LOG_ERROR("Async_wait failed {}", ec1);
                        return;
                    }
                    if (asyncResp)
                    {
                        redfish::messages::internalError(asyncResp->res);
                    }
                });

            auto callback = [asyncResp, service, chassisCfgPath,
                             state](sdbusplus::message_t& msg) mutable {
                handleIrreversibleConfigResponse(asyncResp, service,
                                                 chassisCfgPath, msg, state);
            };
            updateIrreversibleConfigMatch =
                std::make_unique<sdbusplus::bus::match::match>(
                    *crow::connections::systemBus,
                    "interface='org.freedesktop.DBus.Properties',type='signal',"
                    "member='PropertiesChanged',path='" +
                        chassisCfgPath + "'",
                    callback);
            dbus::utility::async_method_call(
                [asyncResp](const boost::system::error_code& ec1) {
                    if (ec1)
                    {
                        BMCWEB_LOG_INFO("DBUS response error {}", ec1);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                },
                service, chassisCfgPath, securityConfigInterface,
                "UpdateIrreversibleConfig", state);
            return;
        });
}

inline void handleSetIrreversibleConfigAction(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    std::string requestType;
    if (!json_util::readJsonAction(req, asyncResp->res, "RequestType",
                                   requestType))
    {
        return;
    }
    bool state = false;
    if (requestType == "Enable")
    {
        state = true;
    }
    else if (requestType == "Disable")
    {
        state = false;
    }
    else
    {
        BMCWEB_LOG_ERROR("Invalid property value for RequestType: {}",
                         requestType);
        messages::actionParameterNotSupported(asyncResp->res, requestType,
                                              "requestType");
        return;
    }
    setIrreversibleConfig(asyncResp, chassisId, state);
}

inline void handleUpdateMinSecVersionActionInfo(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/RoTProtectedComponents/{}"
        "/UpdateMinimumSecurityVersionActionInfo",
        chassisId, componentId);
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_2_0.ActionInfo";
    asyncResp->res.jsonValue["Id"] = "UpdateMinimumSecurityVersionActionInfo";
    asyncResp->res.jsonValue["Name"] =
        "Update MinimumSecurityVersion ActionInfo";
    nlohmann::json parameter1;
    parameter1["Name"] = "Nonce";
    parameter1["Required"] = true;
    parameter1["DataType"] = "String";
    nlohmann::json parameter2;
    parameter2["Name"] = "MinimumSecurityVersion";
    parameter2["Required"] = false;
    parameter2["DataType"] = "Number";
    asyncResp->res.jsonValue["Parameters"] = {parameter1, parameter2};
}

inline void handleupdateMinSecVersionResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& securityPath,
    sdbusplus::message_t& msg)
{
    std::string interface;
    boost::container::flat_map<std::string, dbus::utility::DbusVariantType>
        values;
    msg.read(interface, values);
    if (interface == "xyz.openbmc_project.Common.Progress")
    {
        auto progress = values.find("Status");
        if (progress != values.end())
        {
            auto* value = std::get_if<std::string>(&(progress->second));
            if (value == nullptr)
            {
                return;
            }
            if (*value ==
                "xyz.openbmc_project.Common.Progress.OperationStatus.Completed")
            {
                dbus::utility::getProperty<std::vector<std::string>>(
                    service, securityPath, minSecVersionConfigInterface,
                    "UpdateMethod",
                    [asyncResp](const boost::system::error_code& ec1,
                                const std::vector<std::string>& property) {
                        if (ec1)
                        {
                            BMCWEB_LOG_ERROR("UpdateMinSecVersion DBUS error");
                            messages::internalError(asyncResp->res);
                            clearSecVersion();
                            return;
                        }
                        asyncResp->res.jsonValue["UpdateMethods"] =
                            nlohmann::json::array();
                        for (const auto& prop : property)
                        {
                            asyncResp->res.jsonValue["UpdateMethods"].push_back(
                                getStrAfterLastDot(prop));
                        }
                        clearSecVersion();
                    });
            }
            else
            {
                dbus::utility::getProperty<std::tuple<uint16_t, std::string>>(
                    service, securityPath, minSecVersionConfigInterface,
                    "ErrorCode",
                    [asyncResp](
                        const boost::system::error_code& ec1,
                        const std::tuple<uint16_t, std::string>& property) {
                        if (ec1)
                        {
                            BMCWEB_LOG_ERROR("UpdateMinSecVersion DBUS error");
                            messages::internalError(asyncResp->res);
                            clearSecVersion();
                            return;
                        }
                        redfish::messages::resourceErrorsDetectedFormatError(
                            asyncResp->res, "UpdateMinimumSecurityVersion",
                            std::get<1>((property)));
                        clearSecVersion();
                    });
            }
        }
    }
}

inline void updateMinSecurityVersion(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId,
    const std::string& requestType, const uint16_t reqMinSecVersion,
    uint64_t nonce)
{
    static constexpr std::array<std::string_view, 1> minSecIntf = {
        minSecVersionConfigInterface};
    std::string securityPath;
    if (componentId == "Self")
    {
        securityPath = std::format("{}{}", chassisDbusPath, chassisId);
    }
    else
    {
        securityPath = std::format("{}{}", chassisDbusPath, componentId);
    }
    dbus::utility::getDbusObject(
        securityPath, minSecIntf,
        [asyncResp, chassisId, securityPath, requestType, nonce,
         reqMinSecVersion](
            const boost::system::error_code& ec,
            const ::dbus::utility::MapperGetObject& mapperResponse) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "MinSecVersionConfig interface not found : {}, {}", ec,
                    ec.message());
                messages::resourceNotFound(
                    asyncResp->res, "UpdateMinimumSecurityVersion", chassisId);
                return;
            }
            if (mapperResponse.size() != 1)
            {
                BMCWEB_LOG_ERROR("Invalid response for GetObject: {}, {}", ec,
                                 ec.message());
                messages::resourceNotFound(
                    asyncResp->res, "UpdateMinimumSecurityVersion", chassisId);
                return;
            }
            const auto& valueIface = *mapperResponse.begin();
            const std::string& service = valueIface.first;
            updateMinSecVersionTimer =
                std::make_unique<boost::asio::steady_timer>(getIoContext());
            updateMinSecVersionTimer->expires_after(
                std::chrono::seconds(timeoutTimeSeconds));
            updateMinSecVersionTimer->async_wait(
                [asyncResp](const boost::system::error_code& ec1) {
                    if (ec1 == boost::asio::error::operation_aborted)
                    {
                        // expected, we were canceled before the timer
                        // completed.
                        return;
                    }
                    BMCWEB_LOG_ERROR(
                        "Timed out waiting for updateMinSecVersion response");
                    updateMinSecVersionMatch = nullptr;
                    if (ec1)
                    {
                        BMCWEB_LOG_ERROR("Async_wait failed {}", ec1);
                        return;
                    }
                    if (asyncResp)
                    {
                        redfish::messages::internalError(asyncResp->res);
                    }
                });

            auto callback = [asyncResp, service,
                             securityPath](sdbusplus::message_t& msg) mutable {
                handleupdateMinSecVersionResponse(asyncResp, service,
                                                  securityPath, msg);
            };
            updateMinSecVersionMatch =
                std::make_unique<sdbusplus::bus::match::match>(
                    *crow::connections::systemBus,
                    "interface='org.freedesktop.DBus.Properties',type='signal',"
                    "member='PropertiesChanged',path='" +
                        securityPath + "'",
                    callback);
            dbus::utility::async_method_call(
                [asyncResp](const boost::system::error_code& ec1) {
                    if (ec1)
                    {
                        BMCWEB_LOG_INFO("DBUS response error {}", ec1);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                },
                service, securityPath, minSecVersionConfigInterface,
                "UpdateMinSecVersion", requestType, nonce, reqMinSecVersion);
            return;
        });
}

inline void handleUpdateMinSecVersionAction(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    std::string nonceStr;
    std::optional<uint16_t> minSecVersion;
    if (!json_util::readJsonAction(req, asyncResp->res, "Nonce", nonceStr,
                                   "MinimumSecurityVersion", minSecVersion))
    {
        return;
    }
    uint64_t nonce = 0;
    try
    {
        nonce = std::stoull(nonceStr, nullptr, 16);
    }
    catch (std::exception& e)
    {
        BMCWEB_LOG_ERROR("stoull failed: {}", e.what());
        messages::actionParameterValueError(asyncResp->res, "Nonce",
                                            "UpdateMinimumSecurityVersion");
        return;
    }
    std::string requestType;
    uint16_t reqMinSecVersion = 0;
    if (minSecVersion)
    {
        requestType = std::format("{}.RequestTypes.SpecifiedValue",
                                  softwareSecurityCommonInterface);
        reqMinSecVersion = *minSecVersion;
    }
    else
    {
        requestType = std::format("{}.RequestTypes.MostRestrictiveValue",
                                  softwareSecurityCommonInterface);
        reqMinSecVersion = 0;
    }
    updateMinSecurityVersion(asyncResp, chassisId, componentId, requestType,
                             reqMinSecVersion, nonce);
}

inline void handleRevokeKeysActionInfo(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& /*unused*/, const std::string& /*unused*/)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.id"] = req.url();
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_2_0.ActionInfo";
    asyncResp->res.jsonValue["Id"] = "RevokeKeysActionInfo";
    asyncResp->res.jsonValue["Name"] = "Revoke Keys ActionInfo";

    nlohmann::json::array_t parameters;
    nlohmann::json::object_t nonce;
    nonce["Name"] = "Nonce";
    nonce["Required"] = true;
    nonce["DataType"] = "String";
    parameters.emplace_back(std::move(nonce));
    nlohmann::json::object_t keyIndexes;
    keyIndexes["Name"] = "KeyIndexes";
    keyIndexes["Required"] = false;
    keyIndexes["DataType"] = "NumberArray";
    parameters.emplace_back(std::move(keyIndexes));

    asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
}

inline void handleRevokeKeysResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& securityPath,
    sdbusplus::message_t& msg)
{
    std::string interface;
    boost::container::flat_map<std::string, dbus::utility::DbusVariantType>
        values;
    msg.read(interface, values);
    if (interface == "xyz.openbmc_project.Common.Progress")
    {
        auto progress = values.find("Status");
        if (progress != values.end())
        {
            auto* value = std::get_if<std::string>(&(progress->second));
            if (value == nullptr)
            {
                return;
            }
            if (*value ==
                "xyz.openbmc_project.Common.Progress.OperationStatus.Completed")
            {
                dbus::utility::getProperty<std::vector<std::string>>(
                    service, securityPath, securitySigningConfigInterface,
                    "UpdateMethod",
                    [asyncResp](const boost::system::error_code& ec1,
                                const std::vector<std::string>& property) {
                        if (ec1)
                        {
                            BMCWEB_LOG_ERROR("RevokeKeys DBUS error");
                            messages::internalError(asyncResp->res);
                            clearRevokeKeys();
                            return;
                        }
                        asyncResp->res.jsonValue["UpdateMethods"] =
                            nlohmann::json::array();
                        for (const auto& prop : property)
                        {
                            asyncResp->res.jsonValue["UpdateMethods"].push_back(
                                getStrAfterLastDot(prop));
                        }
                        clearRevokeKeys();
                    });
            }
            else
            {
                dbus::utility::getProperty<std::tuple<uint16_t, std::string>>(
                    service, securityPath, securitySigningConfigInterface,
                    "ErrorCode",
                    [asyncResp](
                        const boost::system::error_code& ec1,
                        const std::tuple<uint16_t, std::string>& property) {
                        if (ec1)
                        {
                            BMCWEB_LOG_ERROR("RevokeKeys DBUS error");
                            messages::internalError(asyncResp->res);
                            clearRevokeKeys();
                            return;
                        }
                        redfish::messages::resourceErrorsDetectedFormatError(
                            asyncResp->res, "RevokeKeys",
                            std::get<1>((property)));
                        clearRevokeKeys();
                    });
            }
        }
    }
}

inline void revokeKeys(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& chassisId,
                       const std::string& componentId,
                       const std::string& requestType, uint64_t nonce,
                       const std::vector<uint8_t>& keys)
{
    static constexpr std::array<std::string_view, 1> signingConfigIntf = {
        securitySigningConfigInterface};
    std::string securityPath;
    if (componentId == "Self")
    {
        securityPath = std::format("{}{}", chassisDbusPath, chassisId);
    }
    else
    {
        securityPath = std::format("{}{}", chassisDbusPath, componentId);
    }
    dbus::utility::getDbusObject(
        securityPath, signingConfigIntf,
        [asyncResp, chassisId, securityPath, requestType, keys,
         nonce](const boost::system::error_code& ec,
                const ::dbus::utility::MapperGetObject& mapperResponse) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("SigningConfig interface not found : {}, {}",
                                 ec, ec.message());
                messages::resourceNotFound(asyncResp->res, "RevokeKeys",
                                           chassisId);
                return;
            }
            if (mapperResponse.size() != 1)
            {
                BMCWEB_LOG_ERROR("Invalid response for GetObject: {}, {}", ec,
                                 ec.message());
                messages::resourceNotFound(asyncResp->res, "RevokeKeys",
                                           chassisId);
                return;
            }
            const auto& valueIface = *mapperResponse.begin();
            const std::string& service = valueIface.first;
            revokeKeysTimer =
                std::make_unique<boost::asio::steady_timer>(getIoContext());
            revokeKeysTimer->expires_after(
                std::chrono::seconds(timeoutTimeSeconds));
            revokeKeysTimer->async_wait(
                [asyncResp](const boost::system::error_code& ec1) {
                    if (ec1 == boost::asio::error::operation_aborted)
                    {
                        // expected, we were canceled before the timer
                        // completed.
                        return;
                    }
                    BMCWEB_LOG_ERROR(
                        "Timed out waiting for revokeKeys response");
                    revokeKeysMatch = nullptr;
                    if (ec1)
                    {
                        BMCWEB_LOG_ERROR("Async_wait failed {}", ec1);
                        return;
                    }
                    if (asyncResp)
                    {
                        redfish::messages::internalError(asyncResp->res);
                    }
                });

            auto callback = [asyncResp, service,
                             securityPath](sdbusplus::message_t& msg) mutable {
                handleRevokeKeysResponse(asyncResp, service, securityPath, msg);
            };
            revokeKeysMatch = std::make_unique<sdbusplus::bus::match::match>(
                *crow::connections::systemBus,
                "interface='org.freedesktop.DBus.Properties',type='signal',"
                "member='PropertiesChanged',path='" +
                    securityPath + "'",
                callback);
            dbus::utility::async_method_call(
                [asyncResp](const boost::system::error_code& ec1) {
                    if (ec1)
                    {
                        BMCWEB_LOG_INFO("DBUS response error {}", ec1);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                },
                service, securityPath, securitySigningConfigInterface,
                "RevokeKeys", requestType, nonce, keys);
            return;
        });
}

inline void handleRevokeKeysAction(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    std::string nonceStr;
    std::optional<std::vector<uint8_t>> keys;
    if (!json_util::readJsonAction(req, asyncResp->res, "Nonce", nonceStr,
                                   "KeyIndexes", keys))
    {
        return;
    }
    uint64_t nonce = 0;
    try
    {
        nonce = std::stoull(nonceStr, nullptr, 16);
    }
    catch (std::exception& e)
    {
        BMCWEB_LOG_ERROR("stoull failed: {}", e.what());
        messages::actionParameterValueError(asyncResp->res, "Nonce",
                                            "RevokeKeys");
        return;
    }
    std::string requestType;
    if (keys)
    {
        requestType = std::format("{}.RequestTypes.SpecifiedValue",
                                  softwareSecurityCommonInterface);
    }
    else
    {
        requestType = std::format("{}.RequestTypes.MostRestrictiveValue",
                                  softwareSecurityCommonInterface);
        keys = std::vector<uint8_t>();
    }
    revokeKeys(asyncResp, chassisId, componentId, requestType, nonce, *keys);
}

} // namespace firmware_info

inline void requestRoutesChassisFirmwareInfo(App& app)
{
    using namespace firmware_info;

    BMCWEB_ROUTE(
        app, "/redfish/v1/Chassis/<str>/Oem/NvidiaRoT/RoTProtectedComponents/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleNvidiaRoTProtectedComponentCollection, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Oem/NvidiaRoT/RoTProtectedComponents/<str>/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleNvidiaRoTProtectedComponent, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Oem/NvidiaRoT/RoTProtectedComponents/<str>/"
        "ImageSlots/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleNvidiaRoTImageSlotCollection, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Oem/NvidiaRoT/RoTProtectedComponents/<str>/"
        "ImageSlots/<str>/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleNvidiaRoTImageSlot, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Oem/NvidiaRoT/RoTProtectedComponents/<str>/"
        "Settings/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleNvidiaRoTProtectedComponentSettings, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Oem/NvidiaRoT/SetIrreversibleConfigActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleSetIrreversibleConfigActionInfo, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Actions/Oem/NvidiaRoTChassis.SetIrreversibleConfig")
        .privileges(redfish::privileges::postActionInfo)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleSetIrreversibleConfigAction, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Oem/NvidiaRoT/RoTProtectedComponents/<str>"
        "/UpdateMinimumSecurityVersionActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleUpdateMinSecVersionActionInfo, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Oem/NvidiaRoT/RoTProtectedComponents/<str>"
        "/Actions/NvidiaRoTProtectedComponent.UpdateMinimumSecurityVersion")
        .privileges(redfish::privileges::postActionInfo)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleUpdateMinSecVersionAction, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Oem/NvidiaRoT/RoTProtectedComponents/<str>"
        "/RevokeKeysActionInfo")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleRevokeKeysActionInfo, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Oem/NvidiaRoT/RoTProtectedComponents/<str>"
        "/Actions/NvidiaRoTProtectedComponent.RevokeKeys")
        .privileges(redfish::privileges::postActionInfo)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleRevokeKeysAction, std::ref(app)));
}

} // namespace redfish
