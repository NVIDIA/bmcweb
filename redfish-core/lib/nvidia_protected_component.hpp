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

#include "utils/dbus_utils.hpp"

namespace redfish::firmware_info
{

static constexpr auto slotInterface = "xyz.openbmc_project.Software.Slot";
static constexpr auto secVersionInterface =
    "xyz.openbmc_project.Software.SecurityVersion";
static constexpr auto securityCfgIntf =
    "xyz.openbmc_project.Software.SecurityConfig";
static constexpr auto minSecVersionInterface =
    "xyz.openbmc_project.Software.MinSecVersionConfig";

static constexpr std::array<std::string_view, 4> interfaces = {
    "xyz.openbmc_project.Software.Slot",
    "xyz.openbmc_project.Software.BuildType",
    "xyz.openbmc_project.Software.State", secVersionInterface};

static const std::string chassisDbusPath =
    "/xyz/openbmc_project/inventory/system/chassis/";
static std::unique_ptr<sdbusplus::bus::match_t> updateIrreversibleConfigMatch;
static std::unique_ptr<boost::asio::steady_timer> irreversibleConfigTimer;
static std::unique_ptr<boost::asio::steady_timer> updateMinSecVersionTimer;
static std::unique_ptr<sdbusplus::bus::match_t> updateMinSecVersionMatch;
static constexpr auto timeoutTimeSeconds = 10;

static inline void clearSecVersion()
{
    updateMinSecVersionMatch = nullptr;
    updateMinSecVersionTimer.reset();
    updateMinSecVersionTimer = nullptr;
}

static inline std::string getStrBeforeLastDot(const std::string& text)
{
    size_t lastDot = text.find_last_of('.');
    if (lastDot != std::string::npos)
    {
        return text.substr(lastDot + 1);
    }
    else
    {
        return text;
    }
}

static inline bool stringToInt(const std::string& str, int& number)
{
    try
    {
        size_t pos;
        number = std::stoi(str, &pos);
        if (pos != str.size())
        {
            return false;
        }
    }
    catch (const std::invalid_argument&)
    {
        return false;
    }
    catch (const std::out_of_range&)
    {
        return false;
    }
    return true;
}

static inline std::string removeERoTFromStr(const std::string& input)
{
    size_t first_underscore = input.find('_');
    size_t second_underscore = input.find('_', first_underscore + 1);
    if (second_underscore != std::string::npos &&
        first_underscore != std::string::npos)
    {
        return input.substr(0, first_underscore + 1) +
               input.substr(second_underscore + 1);
    }
    return input;
}

inline void
    updateSlotProperties(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& service,
                         const std::string& objectPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](
            const boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, dbus::utility::DbusVariantType>& properties) {
        if (ec)
        {
            if (ec == boost::system::errc::host_unreachable)
            {
                // Service not available, no error, just don't
                // return chassis state info
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
                    BMCWEB_LOG_ERROR("Null value returned for SlotId");
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
                    BMCWEB_LOG_ERROR(
                        "Null value returned for FirmwareComparisonNumber");
                }
            }
            else if (key == "ExtendedVersion")
            {
                if (const std::string* value = std::get_if<std::string>(&val))
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
                if (const std::string* value = std::get_if<std::string>(&val))
                {
                    asyncResp->res.jsonValue["BuildType"] =
                        getStrBeforeLastDot(*value);
                }
                else
                {
                    BMCWEB_LOG_ERROR("Null value returned for {}", key);
                }
            }
            else if (key == "State")
            {
                if (const std::string* value = std::get_if<std::string>(&val))
                {
                    asyncResp->res.jsonValue["FirmwareState"] =
                        getStrBeforeLastDot(*value);
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
        }
    },
        service, objectPath, "org.freedesktop.DBus.Properties", "GetAll", "");
}

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

    int slotNum;
    if (!stringToInt(slotNumStr, slotNum) || (slotNum > 1))
    {
        messages::resourceNotFound(asyncResp->res, "SlotNumber", slotNumStr);
        return;
    }

    dbus::utility::getSubTree(
        chassisDbusPath + chassisId, 0, interfaces,
        [chassisId, slotNum, fwTypeStr, slotNumStr,
         asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("D-Bus error: {}, {}", ec, ec.message());
            messages::internalError(asyncResp->res);
            return;
        }
        auto componentId = fwTypeStr != "Self" ? removeERoTFromStr(chassisId)
                                               : "Self";
        if (componentId.find(fwTypeStr) == std::string::npos)
        {
            messages::resourceNotFound(asyncResp->res, "NvidiaRoTImageSlot",
                                       fwTypeStr);
            return;
        }
        for (const auto& [objectPath, serviceMap] : subtree)
        {
            for (const auto& [service, interfaces] : serviceMap)
            {
                auto it = std::find_if(std::begin(interfaces),
                                       std::end(interfaces),
                                       [](const auto& element) {
                    return element == slotInterface;
                });
                if (it == std::end(interfaces))
                {
                    continue;
                }
                sdbusplus::asio::getAllProperties(
                    *crow::connections::systemBus, service, objectPath,
                    "xyz.openbmc_project.Software.Slot",
                    [asyncResp, service, objectPath, chassisId, slotNum,
                     slotNumStr,
                     fwTypeStr](const boost::system::error_code& ec,
                                const dbus::utility::DBusPropertiesMap&
                                    propertiesList) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    const auto slotType =
                        (fwTypeStr == "Self")
                            ? "xyz.openbmc_project.Software.Slot.FirmwareType.EC"
                            : "xyz.openbmc_project.Software.Slot.FirmwareType.AP";
                    std::optional<uint8_t> slotId;
                    std::optional<bool> isActive;
                    std::optional<std::string> fwType;
                    const bool success = sdbusplus::unpackPropertiesNoThrow(
                        dbus_utils::UnpackErrorPrinter(), propertiesList,
                        "SlotId", slotId, "IsActive", isActive, "Type", fwType);
                    if (!success)
                    {
                        BMCWEB_LOG_ERROR("Unpack Slot properites error");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if ((fwType && *fwType == slotType) &&
                        (slotId && *slotId == slotNum))
                    {
                        asyncResp->res.jsonValue["Name"] =
                            chassisId + " RoTProtectedComponent " + fwTypeStr +
                            " ImageSlot " + slotNumStr;
                        asyncResp->res.jsonValue["@odata.type"] =
                            "#NvidiaRoTImageSlot.v1_0_0.NvidiaRoTImageSlot";
                        asyncResp->res.jsonValue["@odata.id"] =
                            "/redfish/v1/Chassis/" + chassisId +
                            "/NvidiaRoT/RoTProtectedComponents/" + fwTypeStr +
                            "/ImageSlots/" + slotNumStr;
                        updateSlotProperties(asyncResp, service, objectPath);
                    }
                });
            }
        }
    });
}

inline void updateProtectedComponentLink(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    dbus::utility::getSubTreePaths(
        chassisDbusPath + chassisId, 0, interfaces,
        [chassisId, asyncResp](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreePathsResponse& subtreePaths) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Service not available {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }
        if (subtreePaths.size() > 0)
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
        chassisDbusPath + chassisId, 0, interfaces,
        [chassisId,
         asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            if (ec == boost::system::errc::host_unreachable)
            {
                // Service not available, no error, just don't
                // return chassis state info
                BMCWEB_LOG_ERROR("Service not available {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            BMCWEB_LOG_ERROR("D-Bus error: {}, {}", ec, ec.message());
            messages::resourceNotFound(asyncResp->res,
                                       "NvidiaRoTProtectedComponentCollection",
                                       chassisId);
            return;
        }
        asyncResp->res.jsonValue["@odata.type"] =
            "#NvidiaRoTProtectedComponentCollection.NvidiaRoTProtectedComponentCollection";
        asyncResp->res.jsonValue["Name"] = chassisId +
                                           " RoTProtectedComponent Collection";
        asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
        for (const auto& [objectPath, serviceMap] : subtree)
        {
            for (const auto& [service, interfaces] : serviceMap)
            {
                auto it = std::find_if(std::begin(interfaces),
                                       std::end(interfaces),
                                       [](const auto& element) {
                    return element == slotInterface;
                });
                if (it == std::end(interfaces))
                {
                    continue;
                }
                sdbusplus::asio::getAllProperties(
                    *crow::connections::systemBus, service, objectPath,
                    "xyz.openbmc_project.Software.Slot",
                    [asyncResp, objectPath,
                     chassisId](const boost::system::error_code& ec,
                                const dbus::utility::DBusPropertiesMap&
                                    propertiesList) {
                    if (ec)
                    {
                        if (ec == boost::system::errc::host_unreachable)
                        {
                            // Service not available, no error, just don't
                            // return chassis state info
                            BMCWEB_LOG_ERROR("Service not available {}", ec);
                            return;
                        }
                        BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    std::optional<uint8_t> slotID;
                    std::optional<bool> isActive;
                    std::optional<std::string> fwType;
                    const bool success = sdbusplus::unpackPropertiesNoThrow(
                        dbus_utils::UnpackErrorPrinter(), propertiesList,
                        "SlotId", slotID, "IsActive", isActive, "Type", fwType);
                    if (!success)
                    {
                        BMCWEB_LOG_ERROR("Unpack Slot properites error");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if (slotID && fwType)
                    {
                        if (*slotID == 0 &&
                            *fwType ==
                                "xyz.openbmc_project.Software.Slot.FirmwareType.EC")
                        {
                            asyncResp->res.jsonValue["Members"].push_back(
                                {{"@odata.id",
                                  "/redfish/v1/Chassis/" + chassisId +
                                      "/Oem/NvidiaRoT/RoTProtectedComponents/Self"}});
                            asyncResp->res.jsonValue["Members@odata.count"] =
                                asyncResp->res.jsonValue["Members"].size();
                        }
                        else if (
                            *slotID == 0 &&
                            fwType ==
                                "xyz.openbmc_project.Software.Slot.FirmwareType.AP")
                        {
                            asyncResp->res.jsonValue["Members"].push_back(
                                {{"@odata.id",
                                  "/redfish/v1/Chassis/" + chassisId +
                                      "/Oem/NvidiaRoT/RoTProtectedComponents/" +
                                      removeERoTFromStr(chassisId)}});
                            asyncResp->res.jsonValue["Members@odata.count"] =
                                asyncResp->res.jsonValue["Members"].size();
                        }
                    }
                });
                break;
            }
        }
    });
}

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
        chassisDbusPath + chassisId, 0, interfaces,
        [chassisId, fwTypeStr,
         asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("D-Bus error: {}, {}", ec, ec.message());
            messages::internalError(asyncResp->res);
            return;
        }
        auto componentId = fwTypeStr != "Self" ? removeERoTFromStr(chassisId)
                                               : "Self";
        if (componentId.find(fwTypeStr) == std::string::npos)
        {
            messages::resourceNotFound(
                asyncResp->res, "NvidiaRoTImageSlotCollection", fwTypeStr);
            return;
        }
        asyncResp->res.jsonValue["@odata.type"] =
            "#NvidiaRoTImageSlotCollection.NvidiaRoTImageSlotCollection";
        asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Chassis/" + chassisId +
            "/NvidiaRoT/RoTProtectedComponents/" + fwTypeStr + "/ImageSlots";
        asyncResp->res.jsonValue["Name"] =
            chassisId + " RoTProtectedComponent " + fwTypeStr + " ImageSlot";
        for (const auto& [objectPath, serviceMap] : subtree)
        {
            for (const auto& [service, interfaces] : serviceMap)
            {
                auto it = std::find_if(std::begin(interfaces),
                                       std::end(interfaces),
                                       [](const auto& element) {
                    return element == slotInterface;
                });
                if (it == std::end(interfaces))
                {
                    continue;
                }
                sdbusplus::asio::getAllProperties(
                    *crow::connections::systemBus, service, objectPath,
                    "xyz.openbmc_project.Software.Slot",
                    [asyncResp, objectPath, chassisId,
                     fwTypeStr](const boost::system::error_code& ec,
                                const dbus::utility::DBusPropertiesMap&
                                    propertiesList) {
                    if (ec)
                    {
                        if (ec == boost::system::errc::host_unreachable)
                        {
                            // Service not available, no error, just don't
                            // return chassis state info
                            BMCWEB_LOG_ERROR("Service not available {}", ec);
                            return;
                        }
                        BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    const auto slotType =
                        (fwTypeStr == "Self")
                            ? "xyz.openbmc_project.Software.Slot.FirmwareType.EC"
                            : "xyz.openbmc_project.Software.Slot.FirmwareType.AP";
                    std::optional<uint8_t> slotID;
                    std::optional<bool> isActive;
                    std::optional<std::string> fwType;
                    const bool success = sdbusplus::unpackPropertiesNoThrow(
                        dbus_utils::UnpackErrorPrinter(), propertiesList,
                        "SlotId", slotID, "IsActive", isActive, "Type", fwType);
                    if (!success)
                    {
                        BMCWEB_LOG_ERROR("Unpack Slot properites error");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if (fwType && *fwType == slotType)
                    {
                        auto memberId = boost::urls::format(
                            "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/RoTProtectedComponents/{}/ImageSlots/{}",
                            chassisId, fwTypeStr, std::to_string(*slotID));
                        asyncResp->res.jsonValue["Members"].push_back(
                            {{"@odata.id", memberId}});
                        asyncResp->res.jsonValue["Members@odata.count"] =
                            asyncResp->res.jsonValue["Members"].size();
                    }
                });
                break;
            }
        }
    });
}

inline void updateSecurityVersionProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId)
{
    static constexpr std::array<std::string_view, 2> securityInterfaces = {
        secVersionInterface, minSecVersionInterface};
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
            BMCWEB_LOG_ERROR("SecurityConfig interface not present : {}, {}",
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
        sdbusplus::asio::getProperty<uint16_t>(
            *crow::connections::systemBus, service, securityPath,
            secVersionInterface, "Version",
            [asyncResp, chassisId, componentId](
                const boost::system::error_code& ec, const uint16_t property) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("MinSecurityVersion DBUS response error");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["MinimumSecurityVersion"] = property;
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

inline void
    updatePendingProperties(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisId,
                            const std::string& componentId)
{
    static constexpr std::array<std::string_view, 1> securityInterfaces = {
        secVersionInterface};
    std::string securityPath;
    if (componentId == "Self")
    {
        securityPath = std::format("{}{}/Settings", chassisDbusPath, chassisId);
    }
    else
    {
        securityPath = std::format("{}{}/Settings", chassisDbusPath,
                                   componentId);
    }
    dbus::utility::getDbusObject(
        securityPath, securityInterfaces,
        [asyncResp, chassisId, securityPath,
         componentId](const boost::system::error_code& ec,
                      const ::dbus::utility::MapperGetObject& mapperResponse) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("SecurityConfig interface not present : {}, {}",
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
        sdbusplus::asio::getProperty<uint16_t>(
            *crow::connections::systemBus, service, securityPath,
            secVersionInterface, "Version",
            [asyncResp, chassisId, componentId](
                const boost::system::error_code& ec, const uint16_t property) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("MinSecurityVersion DBUS response error");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["MinimumSecurityVersion"] = property;
        });
    });
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
    dbus::utility::getSubTree(
        chassisDbusPath + chassisId, 0, interfaces,
        [chassisId, fwTypeStr, asyncResp](
            const boost::system::error_code& ec,
            [[maybe_unused]] const dbus::utility::MapperGetSubTreeResponse&
                subtree) {
        if (ec)
        {
            if (ec == boost::system::errc::host_unreachable)
            {
                // Service not available, no error, just don't
                // return chassis state info
                BMCWEB_LOG_ERROR("Service not available {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            BMCWEB_LOG_ERROR("D-Bus error: {}, {}", ec, ec.message());
            messages::resourceNotFound(
                asyncResp->res, "NvidiaRoTProtectedComponent", fwTypeStr);
            return;
        }
        auto componentId = fwTypeStr != "Self" ? removeERoTFromStr(chassisId)
                                               : "Self";
        if (componentId.find(fwTypeStr) == std::string::npos)
        {
            messages::resourceNotFound(
                asyncResp->res, "NvidiaRoTProtectedComponent", fwTypeStr);
            return;
        }
        asyncResp->res.jsonValue["@odata.id"] =
            boost::urls::format("/redfish/v1/Chassis/{}/Oem/NvidiaRoT/"
                                "RoTProtectedComponents/{}/Settings",
                                chassisId, componentId);
        asyncResp->res.jsonValue["@odata.type"] =
            "#NvidiaRoTProtectedComponent.v1_0_0.NvidiaRoTProtectedComponent";
        asyncResp->res.jsonValue["Name"] =
            std::format("{} RoTProtectedComponent {} Pending Settings",
                        chassisId, fwTypeStr);
        asyncResp->res.jsonValue["Id"] = "Settings";
        updatePendingProperties(asyncResp, chassisId, componentId);
    });
}

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
        chassisDbusPath + chassisId, 0, interfaces,
        [chassisId, fwTypeStr, asyncResp](
            const boost::system::error_code& ec,
            [[maybe_unused]] const dbus::utility::MapperGetSubTreeResponse&
                subtree) {
        if (ec)
        {
            if (ec == boost::system::errc::host_unreachable)
            {
                // Service not available, no error, just don't
                // return chassis state info
                BMCWEB_LOG_ERROR("Service not available {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            BMCWEB_LOG_ERROR("D-Bus error: {}, {}", ec, ec.message());
            messages::resourceNotFound(
                asyncResp->res, "NvidiaRoTProtectedComponent", fwTypeStr);
            return;
        }
        auto componentId = fwTypeStr != "Self" ? removeERoTFromStr(chassisId)
                                               : "Self";
        if (componentId.find(fwTypeStr) == std::string::npos)
        {
            messages::resourceNotFound(
                asyncResp->res, "NvidiaRoTProtectedComponent", fwTypeStr);
            return;
        }
        asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
            "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/RoTProtectedComponents/{}",
            chassisId, componentId);
        asyncResp->res.jsonValue["@odata.type"] =
            "#NvidiaRoTProtectedComponent.v1_0_0.NvidiaRoTProtectedComponent";
        asyncResp->res.jsonValue["Name"] =
            chassisId + " RoTProtectedComponent " + fwTypeStr;
        asyncResp->res.jsonValue["Id"] = fwTypeStr;
        asyncResp->res.jsonValue["RoTProtectedComponentType"] =
            fwTypeStr == "Self" ? "Self" : "AP";
        auto slotUrl = boost::urls::format(
            "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/RoTProtectedComponents/{}/ImageSlots",
            chassisId, componentId);
        asyncResp->res.jsonValue["ImageSlots"] = {{"@odata.id", slotUrl}};
        auto settingsUrl = boost::urls::format(
            "/redfish/v1/Chassis/{}/Oem/NvidiaRoT/RoTProtectedComponents/{}/Settings",
            chassisId, componentId);
        asyncResp->res.jsonValue["@Redfish.Settings"] = {
            {"@odata.type", "#Settings.v1_3_3.Settings"},
            {"SettingsObject", {{"@odata.id", settingsUrl}}}};
        updateSecurityVersionProperties(asyncResp, chassisId, componentId);
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
        securityCfgIntf};
    auto chassisCfgPath = std::format("{}{}", chassisDbusPath, chassisId);
    dbus::utility::getDbusObject(
        chassisCfgPath, cfgIntf,
        [asyncResp, chassisId, chassisCfgPath](
            const boost::system::error_code& ec,
            const ::dbus::utility::MapperGetObject& mapperResponse) {
        if (ec)
        {
            BMCWEB_LOG_INFO("SecurityConfig interface not present : {}, {}", ec,
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
        sdbusplus::asio::getProperty<bool>(
            *crow::connections::systemBus, service, chassisCfgPath,
            securityCfgIntf, "IrreversibleConfigState",
            [asyncResp, chassisId](const boost::system::error_code& ec,
                                   const bool property) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "updateIrreversibleConfigEnabled DBUS response error");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                    ["IrreversibleConfigEnabled"] = property;
            auto cfgTarget =
                boost::urls::format("/redfish/v1/Chassis/{}/Actions/Oem/"
                                    "NvidiaRoTChassis.SetIrreversibleConfig",
                                    chassisId);
            auto cfgTargetActionInfo =
                boost::urls::format("/redfish/v1/Chassis/{}/Oem/NvidiaRoT/"
                                    "SetIrreversibleConfigActionInfo",
                                    chassisId);
            asyncResp->res.jsonValue["Actions"]["Oem"]
                                    ["#NvidiaRoTChassis.SetIrreversibleConfig"]
                                    ["target"] = cfgTarget;
            asyncResp->res.jsonValue["Actions"]["Oem"]
                                    ["#NvidiaRoTChassis.SetIrreversibleConfig"]
                                    ["@Redfish.ActionInfo"] =
                cfgTargetActionInfo;
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
            auto value = std::get_if<std::string>(&(progress->second));
            if (!value)
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
                else // Enable, return Nonce
                {
                    sdbusplus::asio::getProperty<uint64_t>(
                        *crow::connections::systemBus, service, chassisCfgPath,
                        securityCfgIntf, "Nonce",
                        [asyncResp](const boost::system::error_code& ec,
                                    const uint64_t property) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR(
                                "updateIrreversibleConfigEnabled DBUS error");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res.jsonValue["Nonce"] = property;
                        updateIrreversibleConfigMatch = nullptr;
                        irreversibleConfigTimer = nullptr;
                    });
                    return;
                }
            }
            else
            {
                BMCWEB_LOG_ERROR(
                    "updateIrreversibleConfigEnabled Method failed");
                messages::internalError(asyncResp->res);
                updateIrreversibleConfigMatch = nullptr;
                irreversibleConfigTimer = nullptr;
            }
            return;
        }
    }
}

inline void
    setIrreversibleConfig(const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& chassisId, bool state)
{
    static constexpr std::array<std::string_view, 1> cfgIntf = {
        securityCfgIntf};
    auto chassisCfgPath = std::format("{}{}", chassisDbusPath, chassisId);
    dbus::utility::getDbusObject(
        chassisCfgPath, cfgIntf,
        [req, asyncResp, chassisId, chassisCfgPath,
         state](const boost::system::error_code& ec,
                const ::dbus::utility::MapperGetObject& mapperResponse) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("SecurityConfig interface not present : {}, {}",
                             ec, ec.message());
            messages::resourceNotFound(asyncResp->res, "SetIrreversibleConfig",
                                       chassisId);
            return;
        }
        if (mapperResponse.size() != 1)
        {
            BMCWEB_LOG_ERROR("Invalid response for GetObject: {}, {}", ec,
                             ec.message());
            messages::resourceNotFound(asyncResp->res, "SetIrreversibleConfig",
                                       chassisId);
            return;
        }
        const auto& valueIface = *mapperResponse.begin();
        const std::string& service = valueIface.first;
        irreversibleConfigTimer =
            std::make_unique<boost::asio::steady_timer>(*req.ioService);
        irreversibleConfigTimer->expires_after(
            std::chrono::seconds(timeoutTimeSeconds));
        irreversibleConfigTimer->async_wait(
            [asyncResp](const boost::system::error_code& ec) {
            if (ec == boost::asio::error::operation_aborted)
            {
                // expected, we were canceled before the timer
                // completed.
                return;
            }
            BMCWEB_LOG_ERROR(
                "Timed out waiting for IrreversibleConfig response");
            updateIrreversibleConfigMatch = nullptr;
            if (ec)
            {
                BMCWEB_LOG_ERROR("Async_wait failed {}", ec);
                return;
            }
            if (asyncResp)
            {
                redfish::messages::internalError(asyncResp->res);
            }
        });

        auto callback = [asyncResp, service, chassisCfgPath,
                         state](sdbusplus::message_t& msg) mutable {
            handleIrreversibleConfigResponse(asyncResp, service, chassisCfgPath,
                                             msg, state);
        };
        updateIrreversibleConfigMatch =
            std::make_unique<sdbusplus::bus::match::match>(
                *crow::connections::systemBus,
                "interface='org.freedesktop.DBus.Properties',type='signal',"
                "member='PropertiesChanged',path='" +
                    chassisCfgPath + "'",
                callback);
        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_INFO("DBUS response error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
        },
            service, chassisCfgPath, securityCfgIntf,
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
    bool state;
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
    setIrreversibleConfig(req, asyncResp, chassisId, state);
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
            auto value = std::get_if<std::string>(&(progress->second));
            if (!value)
            {
                return;
            }
            if (*value ==
                "xyz.openbmc_project.Common.Progress.OperationStatus.Completed")
            {
                sdbusplus::asio::getProperty<std::vector<std::string>>(
                    *crow::connections::systemBus, service, securityPath,
                    minSecVersionInterface, "UpdateMethod",
                    [asyncResp](const boost::system::error_code& ec,
                                const std::vector<std::string> property) {
                    if (ec)
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
                            getStrBeforeLastDot(prop));
                    }
                    clearSecVersion();
                });
            }
            else
            {
                sdbusplus::asio::getProperty<std::tuple<uint16_t, std::string>>(
                    *crow::connections::systemBus, service, securityPath,
                    minSecVersionInterface, "ErrorCode",
                    [asyncResp](
                        const boost::system::error_code& ec,
                        const std::tuple<uint16_t, std::string> property) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("UpdateMinSecVersion DBUS error");
                        messages::internalError(asyncResp->res);
                        clearSecVersion();
                        return;
                    }
                    redfish::messages::resourceErrorsDetectedFormatError(
                        asyncResp->res, "UpdateMinimumSecurityVersion",
                        std::get<1>((property)));
                    BMCWEB_LOG_ERROR("Another staging is in progress.");
                    clearSecVersion();
                });
            }
        }
    }
}

inline void updateMinSecurityVersion(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& componentId,
    const std::string& requestType, const uint16_t reqMinSecVersion,
    uint64_t nonce)
{
    static constexpr std::array<std::string_view, 1> minSecIntf = {
        minSecVersionInterface};
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
        [req, asyncResp, chassisId, securityPath, componentId, requestType,
         nonce, reqMinSecVersion](
            const boost::system::error_code& ec,
            const ::dbus::utility::MapperGetObject& mapperResponse) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("MinSecVersionConfig interface not found : {}, {}",
                             ec, ec.message());
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
            std::make_unique<boost::asio::steady_timer>(*req.ioService);
        updateMinSecVersionTimer->expires_after(
            std::chrono::seconds(timeoutTimeSeconds));
        updateMinSecVersionTimer->async_wait(
            [asyncResp](const boost::system::error_code& ec) {
            if (ec == boost::asio::error::operation_aborted)
            {
                // expected, we were canceled before the timer
                // completed.
                return;
            }
            BMCWEB_LOG_ERROR(
                "Timed out waiting for updateMinSecVersion response");
            updateMinSecVersionMatch = nullptr;
            if (ec)
            {
                BMCWEB_LOG_ERROR("Async_wait failed {}", ec);
                return;
            }
            if (asyncResp)
            {
                redfish::messages::internalError(asyncResp->res);
            }
        });

        auto callback = [asyncResp, service,
                         securityPath](sdbusplus::message_t& msg) mutable {
            handleupdateMinSecVersionResponse(asyncResp, service, securityPath,
                                              msg);
        };
        updateMinSecVersionMatch =
            std::make_unique<sdbusplus::bus::match::match>(
                *crow::connections::systemBus,
                "interface='org.freedesktop.DBus.Properties',type='signal',"
                "member='PropertiesChanged',path='" +
                    securityPath + "'",
                callback);
        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_INFO("DBUS response error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
        },
            service, securityPath, minSecVersionInterface,
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
    uint64_t nonce;
    std::optional<uint16_t> minSecVersion;
    if (!json_util::readJsonAction(req, asyncResp->res, "Nonce", nonce,
                                   "MinimumSecurityVersion", minSecVersion))
    {
        return;
    }
    std::string requestType;
    uint16_t reqMinSecVersion;
    if (minSecVersion)
    {
        requestType = std::format("{}.RequestTypes.SpecifiedValue",
                                  minSecVersionInterface);
        reqMinSecVersion = *minSecVersion;
    }
    else
    {
        requestType = std::format("{}.RequestTypes.MostRestrictiveValue",
                                  minSecVersionInterface);
        reqMinSecVersion = 0;
    }
    updateMinSecurityVersion(req, asyncResp, chassisId, componentId,
                             requestType, reqMinSecVersion, nonce);
}

} // namespace redfish::firmware_info
