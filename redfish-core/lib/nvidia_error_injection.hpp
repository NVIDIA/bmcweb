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

#include "app.hpp"
#include "dbus_utility.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/collection.hpp"
#include "utils/conditions_utils.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/hex_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_async_call_utils.hpp"
#include "utils/nvidia_async_set_callbacks.hpp"
#include "utils/nvidia_async_set_utils.hpp"

#include <boost/container/flat_map.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <algorithm>
#include <array>
#include <string_view>
namespace redfish
{
using OperatingConfigProperties =
    std::vector<std::pair<std::string, dbus::utility::DbusVariantType>>;

using ErrorInjectionPatchMap =
    std::map<std::string, std::variant<bool, uint32_t, std::vector<uint8_t>>>;

inline ErrorInjectionPatchMap parseErrorInjectionJson(
    const crow::Request& req, const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    ErrorInjectionPatchMap properties;

    std::optional<bool> errorInjectionModeEnabled;
    std::optional<nlohmann::json> errorInjectionCapabilities;
    std::map<std::string, std::optional<nlohmann::json>> capabilities = {
        {"MemoryErrors", {}},
        {"PCIeErrors", {}},
        {"NVLinkErrors", {}},
        {"ThermalErrors", {}},
        {"FatalErrors", {}},
        {"PortRecoveryErrors", {}},
        {"USBBridgeEmulationErrors", {}},
        {"LeakDetectionErrors", {}},
        {"GPIOSpoofingErrors", {}}};
    if (!redfish::json_util::readJsonAction(
            req, aResp->res, "ErrorInjectionModeEnabled",
            errorInjectionModeEnabled, "ErrorInjectionCapabilities",
            errorInjectionCapabilities))
    {
        return properties;
    }
    if (errorInjectionModeEnabled)
    {
        properties["ErrorInjectionModeEnabled"] = *errorInjectionModeEnabled;
    }
    if (errorInjectionCapabilities &&
        redfish::json_util::readJson(
            *errorInjectionCapabilities, aResp->res, "MemoryErrors",
            capabilities["MemoryErrors"], "PCIeErrors",
            capabilities["PCIeErrors"], "NVLinkErrors",
            capabilities["NVLinkErrors"], "ThermalErrors",
            capabilities["ThermalErrors"], "FatalErrors",
            capabilities["FatalErrors"], "PortRecoveryErrors",
            capabilities["PortRecoveryErrors"], "USBBridgeEmulationErrors",
            capabilities["USBBridgeEmulationErrors"], "LeakDetectionErrors",
            capabilities["LeakDetectionErrors"], "GPIOSpoofingErrors",
            capabilities["GPIOSpoofingErrors"]))
    {
        for (auto& [name, json] : capabilities)
        {
            std::optional<bool> enabled;
            std::optional<std::string> faultBitmap;
            if (json && redfish::json_util::readJson(
                            *json, aResp->res, "Enabled", enabled,
                            "FaultBitmap", faultBitmap))
            {
                if (enabled)
                {
                    properties[name + "_Enabled"] = *enabled;
                }
                if (faultBitmap)
                {
                    BMCWEB_LOG_DEBUG("Parsing FaultBitmap: '{}'", *faultBitmap);
                    std::string hexStr = *faultBitmap;
                    std::vector<uint8_t> byteArray;

                    if (hexStr.size() >= 2 && hexStr[0] == '0' &&
                        (hexStr[1] == 'x' || hexStr[1] == 'X'))
                    {
                        hexStr = hexStr.substr(2);
                    }

                    if (hexStr.empty())
                    {
                        BMCWEB_LOG_ERROR(
                            "FaultBitmap hex string is empty after removing prefix");
                        messages::propertyValueIncorrect(
                            aResp->res, "FaultBitmap", *faultBitmap);
                        return properties;
                    }

                    if (hexStr.size() % 2 != 0)
                    {
                        BMCWEB_LOG_ERROR(
                            "FaultBitmap hex string has odd length: {}",
                            hexStr);
                        messages::propertyValueIncorrect(
                            aResp->res, "FaultBitmap", *faultBitmap);
                        return properties;
                    }

                    try
                    {
                        for (size_t i = 0; i < hexStr.size(); i += 2)
                        {
                            std::string byteStr = hexStr.substr(i, 2);
                            uint8_t byte = static_cast<uint8_t>(
                                std::stoul(byteStr, nullptr, 16));
                            byteArray.push_back(byte);
                        }
                    }
                    catch (const std::exception& e)
                    {
                        BMCWEB_LOG_ERROR("Failed to parse FaultBitmap '{}': {}",
                                         *faultBitmap, e.what());
                        messages::propertyValueIncorrect(
                            aResp->res, "FaultBitmap", *faultBitmap);
                        return properties;
                    }
                    properties[name + "_FaultBitmap"] = byteArray;
                }
            }
        }
    }

    return properties;
}

/**
 * @brief Fill out error injection processor nvidia specific info by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       capability  Capability name
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getErrorInjectionCapabilityData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& capability, const std::string& service,
    const std::string& objPath)

{
    dbus::utility::getAllProperties(
        service, objPath, "com.nvidia.ErrorInjection.ErrorInjectionCapability",
        [aResp,
         capability](const boost::system::error_code ec,
                     const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                return;
            }
            auto& json =
                aResp->res.jsonValue["ErrorInjectionCapabilities"][capability];
            for (const auto& property : properties)
            {
                if (property.first == "Supported")
                {
                    const bool* supported = std::get_if<bool>(&property.second);
                    if (supported == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Get Supported property failed");
                        messages::internalError(aResp->res);
                        return;
                    }
                    json["Supported"] = *supported;
                }
                else if (property.first == "Enabled")
                {
                    const bool* enabled = std::get_if<bool>(&property.second);
                    if (enabled == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Get Enabled property failed");
                        messages::internalError(aResp->res);
                        return;
                    }
                    json["Enabled"] = *enabled;
                }
            }
        });
}

/**
 * @brief Fill out error injection processor nvidia specific info by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       capability  Capability name
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 * @param[in]       chassisName Chassis resource name.
 */
inline void getErrorInjectionPayloadData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& capability, const std::string& service,
    const std::string& objPath, const std::string& chassisName)

{
    dbus::utility::getAllProperties(
        service, objPath, "com.nvidia.ErrorInjection.ErrorInjectionPayload",
        [aResp, capability,
         chassisName](const boost::system::error_code ec,
                      const OperatingConfigProperties& properties) {
            if (ec)
            {
                return;
            }
            auto& json =
                aResp->res.jsonValue["ErrorInjectionCapabilities"][capability];
            aResp->res.jsonValue["Actions"]["#NvidiaErrorInjection.Activate"]
                                ["target"] =
                "/redfish/v1/Chassis/" + chassisName +
                "/Oem/Nvidia/ErrorInjection/Actions/"
                "NvidiaErrorInjection.Activate";
            aResp->res.jsonValue["Actions"]["#NvidiaErrorInjection.Activate"]
                                ["@Redfish.ActionInfo"] =
                "/redfish/v1/Chassis/" + chassisName +
                "/Oem/Nvidia/ErrorInjection/ActivateActionInfo";
            for (const auto& property : properties)
            {
                if (property.first == "Payload")
                {
                    const std::vector<uint8_t>* payload =
                        std::get_if<std::vector<uint8_t>>(&property.second);
                    if (payload == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Get Payload property failed");
                        messages::internalError(aResp->res);
                        return;
                    }
                    std::ostringstream oss;
                    oss << "0x";
                    if (payload->empty())
                    {
                        oss << "00";
                    }
                    else
                    {
                        oss << std::hex << std::uppercase << std::setfill('0');
                        for (const uint8_t& byte : *payload)
                        {
                            oss << std::setw(2) << static_cast<int>(byte);
                        }
                    }
                    json["FaultBitmap"] = oss.str();
                }
            }
        });
}

/**
 * @brief Fill out error injection nvidia specific info by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       baseUri     Redfish base uri
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getErrorInjectionData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& baseUri,
    const std::string& service, const std::string& objPath)

{
    dbus::utility::getAllProperties(
        service, objPath, "com.nvidia.ErrorInjection.ErrorInjection",
        [aResp, baseUri, service,
         objPath](const boost::system::error_code ec,
                  const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error failed to get error injection data");
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json& json = aResp->res.jsonValue;
            json["@odata.type"] =
                "#NvidiaErrorInjection.v1_2_0.NvidiaErrorInjection";
            json["@odata.id"] = baseUri + "/Oem/Nvidia/ErrorInjection";
            json["Id"] = "ErrorInjection";
            json["Name"] = baseUri.substr(baseUri.find_last_of('/') + 1) +
                           " Oem Nvidia ErrorInjection";
            for (const auto& property : properties)
            {
                if (property.first == "ErrorInjectionModeEnabled")
                {
                    const bool* errorInjectionModeEnabled =
                        std::get_if<bool>(&property.second);
                    if (errorInjectionModeEnabled == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Get ErrorInjectionModeEnabled property failed");
                        messages::internalError(aResp->res);
                        return;
                    }
                    json["ErrorInjectionModeEnabled"] =
                        *errorInjectionModeEnabled;
                }
                else if (property.first == "PersistentDataModified")
                {
                    const bool* persistentDataModified =
                        std::get_if<bool>(&property.second);
                    if (persistentDataModified == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Get PersistentDataModified property failed");
                        messages::internalError(aResp->res);
                        return;
                    }
                    json["PersistentDataModified"] = *persistentDataModified;
                }
            }
            std::vector<std::string> capabilities = {
                "MemoryErrors",
                "PCIeErrors",
                "NVLinkErrors",
                "ThermalErrors",
                "FatalErrors",
                "PortRecoveryErrors",
                "USBBridgeEmulationErrors",
                "LeakDetectionErrors",
                "GPIOSpoofingErrors"};
            for (auto& cap : capabilities)
            {
                std::string capPath = objPath;
                capPath += "/";
                capPath += cap;
                getErrorInjectionCapabilityData(aResp, cap, service, capPath);
                if (cap == "FatalErrors" || cap == "PortRecoveryErrors" ||
                    cap == "USBBridgeEmulationErrors" ||
                    cap == "LeakDetectionErrors" || cap == "GPIOSpoofingErrors")
                {
                    std::string payloadPath = objPath;
                    payloadPath += "/";
                    payloadPath += cap;
                    std::string chassisName =
                        baseUri.substr(baseUri.find_last_of('/') + 1);
                    getErrorInjectionPayloadData(aResp, cap, service,
                                                 payloadPath, chassisName);
                }
            }
        });
}

inline void patchErrorInjectionData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& path, const ErrorInjectionPatchMap& properties)
{
    for (const auto& [name, value] : properties)
    {
        if (name == "ErrorInjectionModeEnabled")
        {
            const bool* errorInjectionModeEnabled = std::get_if<bool>(&value);
            if (errorInjectionModeEnabled == nullptr)
            {
                BMCWEB_LOG_ERROR(
                    "Get ErrorInjectionModeEnabled property failed");
                messages::internalError(aResp->res);
                return;
            }
            nvidia_async_operation_utils::patch(
                aResp, service, path,
                "com.nvidia.ErrorInjection.ErrorInjection", name,
                *errorInjectionModeEnabled);
        }
        else
        {
            std::string errorInjectionType =
                name.substr(0, name.find_last_of('_'));
            std::string property = name.substr(name.find_last_of('_') + 1);
            if (property == "FaultBitmap")
            {
                const std::vector<uint8_t>* faultBitmap =
                    std::get_if<std::vector<uint8_t>>(&value);
                if (faultBitmap == nullptr)
                {
                    BMCWEB_LOG_ERROR("Get FaultBitmap property failed");
                    messages::internalError(aResp->res);
                    return;
                }

                using PayloadVariant =
                    std::variant<bool, uint32_t, double, std::vector<uint8_t>>;
                std::vector<std::tuple<std::string, PayloadVariant>> payload;
                payload.emplace_back("FaultBitMap",
                                     PayloadVariant{*faultBitmap});

                std::string payloadPath = path;
                payloadPath += "/";
                payloadPath += errorInjectionType;
                redfish::nvidia_async_operation_utils::
                    doGenericSetAsyncAndGatherResult(
                        aResp, std::chrono::seconds(60), service, payloadPath,
                        "com.nvidia.ErrorInjection.ErrorInjectionPayload",
                        "Payload",
                        std::variant<std::vector<
                            std::tuple<std::string, PayloadVariant>>>{payload},
                        redfish::nvidia_async_operation_utils::
                            PatchErrorInjectionPayloadCallback{aResp});
            }
            else if (property == "Enabled")
            {
                const bool* enabled = std::get_if<bool>(&value);
                if (enabled == nullptr)
                {
                    BMCWEB_LOG_ERROR("Get Enabled property failed");
                    messages::internalError(aResp->res);
                    return;
                }
                std::string errorPath = path;
                errorPath += "/";
                errorPath += errorInjectionType;
                nvidia_async_operation_utils::patch(
                    aResp, service, errorPath,
                    "com.nvidia.ErrorInjection.ErrorInjectionCapability",
                    property, *enabled);
            }
        }
    }
}

template <typename Handler>
inline void getErrorInjectionService(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& path,
    Handler&& handler)
{
    std::string eiPath = path;
    eiPath += "/ErrorInjection";
    dbus::utility::getDbusObject(
        eiPath, std::array<std::string_view, 0>(),
        [aResp, eiPath, handler{std::forward<Handler>(handler)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperServiceMap& serviceMap) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Error while fetching service for {}", eiPath);
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& [service, interfaces] : serviceMap)
            {
                if (std::find(interfaces.begin(), interfaces.end(),
                              "com.nvidia.ErrorInjection.ErrorInjection") ==
                    interfaces.end())
                {
                    continue;
                }
                handler(service, eiPath);
                return;
            }
        });
}

template <typename Handler>
inline void getProcessor(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& processorId, Handler&& handler)
{
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory", 0,
        std::array<std::string_view, 2>{
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "xyz.openbmc_project.Inventory.Item.Cpu"},
        [processorId, aResp, handler{std::forward<Handler>(handler)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreePathsResponse& paths) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error failed to get processor paths");
                messages::internalError(aResp->res);

                return;
            }
            for (const auto& path : paths)
            {
                if (!path.ends_with(processorId))
                {
                    continue;
                }

                getErrorInjectionService(
                    aResp, path,
                    [processorId, aResp, handler](const std::string& service,
                                                  const std::string& objPath) {
                        std::string uri = "/redfish/v1/Systems/";
                        uri += BMCWEB_REDFISH_SYSTEM_URI_NAME;
                        uri += "/Processors/";
                        uri += processorId;
                        handler(uri, service, objPath);
                    });
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaErrorInjection.v1_0_0.NvidiaErrorInjection",
                processorId);
        });
}

template <typename Handler>
inline void getChassis(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                       const std::string& chassisId, Handler&& handler)
{
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory", 0,
        std::array<std::string_view, 3>{
            "xyz.openbmc_project.Inventory.Item.Board",
            "xyz.openbmc_project.Inventory.Item.Chassis",
            "xyz.openbmc_project.Inventory.Item.Component"},
        [chassisId, aResp, handler{std::forward<Handler>(handler)}](
            const boost::system::error_code ec,
            const dbus::utility::MapperGetSubTreePathsResponse& paths) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error failed to get chassis paths");
                messages::internalError(aResp->res);

                return;
            }
            for (const auto& path : paths)
            {
                if (!path.ends_with(chassisId))
                {
                    continue;
                }

                getErrorInjectionService(
                    aResp, path,
                    [chassisId, aResp, handler](const std::string& service,
                                                const std::string& objPath) {
                        std::string uri = "/redfish/v1/Chassis/";
                        uri += chassisId;
                        handler(uri, service, objPath);
                    });
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaErrorInjection.v1_0_0.NvidiaErrorInjection",
                chassisId);
        });
}

inline void getChassisErrorInjectionData(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    getChassis(aResp, chassisId,
               [aResp](const std::string& uri, const std::string& service,
                       const std::string& objPath) {
                   getErrorInjectionData(aResp, uri, service, objPath);
               });
}

inline void patchChassisErrorInjectionData(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    auto properties = parseErrorInjectionJson(req, aResp);
    getChassis(aResp, chassisId,
               [aResp, properties]([[maybe_unused]] const std::string& uri,
                                   const std::string& service,
                                   const std::string& objPath) {
                   patchErrorInjectionData(aResp, service, objPath, properties);
               });
}

inline void getProcessorErrorInjectionData(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& processorId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    getProcessor(aResp, processorId,
                 [aResp](const std::string& uri, const std::string& service,
                         const std::string& objPath) {
                     getErrorInjectionData(aResp, uri, service, objPath);
                 });
}

inline void patchProcessorErrorInjectionData(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& processorId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    auto properties = parseErrorInjectionJson(req, aResp);
    getProcessor(
        aResp, processorId,
        [aResp,
         properties]([[maybe_unused]] const std::string& uri,
                     const std::string& service, const std::string& objPath) {
            patchErrorInjectionData(aResp, service, objPath, properties);
        });
}

template <typename Handler>
inline void getNetworkAdapter(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                              const std::string& chassisId,
                              const std::string& networkAdapterId,
                              Handler&& handler)
{
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory/", 0,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Inventory.Item.NetworkInterface"},
        [chassisId, networkAdapterId, aResp,
         handler{std::forward<Handler>(handler)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreePathsResponse& paths) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error failed to get network adapter paths");
                messages::internalError(aResp->res);

                return;
            }
            for (const auto& path : paths)
            {
                if (!path.ends_with(networkAdapterId) ||
                    path.find(chassisId) == std::string::npos)
                {
                    continue;
                }

                getErrorInjectionService(
                    aResp, path,
                    [chassisId, networkAdapterId, aResp,
                     handler](const std::string& service,
                              const std::string& objPath) {
                        std::string uri = "/redfish/v1/Chassis/";
                        uri += chassisId;
                        uri += "/NetworkAdapters/";
                        uri += networkAdapterId;
                        handler(uri, service, objPath);
                    });
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaErrorInjection.v1_0_0.NvidiaErrorInjection",
                networkAdapterId);
        });
}

inline void getNetworkAdapterErrorInjectionData(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& chassisId, const std::string& networkAdapterId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    getNetworkAdapter(
        aResp, chassisId, networkAdapterId,
        [aResp](const std::string& uri, const std::string& service,
                const std::string& objPath) {
            getErrorInjectionData(aResp, uri, service, objPath);
        });
}
inline void patchNetworkAdapterErrorInjectionData(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& chassisId, const std::string& networkAdapterId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    auto properties = parseErrorInjectionJson(req, aResp);
    getNetworkAdapter(
        aResp, chassisId, networkAdapterId,
        [aResp,
         properties]([[maybe_unused]] const std::string& uri,
                     const std::string& service, const std::string& objPath) {
            patchErrorInjectionData(aResp, service, objPath, properties);
        });
}

template <typename Handler>
inline void getSwitch(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                      const std::string& fabricId, const std::string& switchId,
                      Handler&& handler)
{
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory/", 0,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Inventory.Item.NvSwitch"},
        [fabricId, switchId, aResp, handler{std::forward<Handler>(handler)}](
            const boost::system::error_code ec,
            const dbus::utility::MapperGetSubTreePathsResponse& paths) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error failed to get switch paths");
                messages::internalError(aResp->res);

                return;
            }
            for (const auto& path : paths)
            {
                if (!path.ends_with(switchId) ||
                    path.find(fabricId) == std::string::npos)
                {
                    continue;
                }

                getErrorInjectionService(
                    aResp, path,
                    [fabricId, switchId, aResp, handler](
                        const std::string& service, const std::string& path2) {
                        std::string uri = "/redfish/v1/Fabrics/";
                        uri += fabricId;
                        uri += "/Switches/";
                        uri += switchId;
                        handler(uri, service, path2);
                    });
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaErrorInjection.v1_0_0.NvidiaErrorInjection",
                switchId);
        });
}

inline void getSwitchErrorInjectionData(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& fabricId, const std::string& switchId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    getSwitch(aResp, fabricId, switchId,
              [aResp](const std::string& uri, const std::string& service,
                      const std::string& objPath) {
                  getErrorInjectionData(aResp, uri, service, objPath);
              });
}
inline void patchSwitchErrorInjectionData(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& fabricId, const std::string& switchId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    auto properties = parseErrorInjectionJson(req, aResp);
    getSwitch(aResp, fabricId, switchId,
              [aResp, properties]([[maybe_unused]] const std::string& uri,
                                  const std::string& service,
                                  const std::string& objPath) {
                  patchErrorInjectionData(aResp, service, objPath, properties);
              });
}

inline void activateErrorInjectionPayload(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path)
{
    static const std::string activateErrorInjectionPayloadAsyncIntf{
        "com.nvidia.ErrorInjection.ActivateErrorInjectionPayloadAsync"};

    dbus::utility::getDbusObject(
        path,
        std::array<std::string_view, 1>{activateErrorInjectionPayloadAsyncIntf},
        [asyncResp, path](const boost::system::error_code& ec,
                          const dbus::utility::MapperGetObject& object) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error failed to activate error injection payload");
                messages::internalError(asyncResp->res);

                return;
            }
            if (!object.empty())
            {
                const auto& [serv, _] = *object.begin();
                BMCWEB_LOG_DEBUG("Performing Post using Async Method Call");

                nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<
                    int>(asyncResp, std::chrono::seconds(60), serv, path,
                         activateErrorInjectionPayloadAsyncIntf, "Activate",
                         [asyncResp](const std::string& status,
                                     [[maybe_unused]] const int* retValue) {
                             if (status == nvidia_async_operation_utils::
                                               asyncStatusValueSuccess)
                             {
                                 BMCWEB_LOG_DEBUG(
                                     "Error Injection Payload Activated");
                                 messages::success(asyncResp->res);
                                 return;
                             }
                             BMCWEB_LOG_ERROR(
                                 "activateErrorInjectionPayload error {}",
                                 status);
                             messages::internalError(asyncResp->res);
                         });
            }
        });
}

template <typename Handler>
inline void getActivateCapableErrorTypes(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& eiObjPath, Handler&& handler)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory", 0,
        std::array<std::string_view, 3>{
            "xyz.openbmc_project.Inventory.Item.Board",
            "xyz.openbmc_project.Inventory.Item.Chassis",
            "xyz.openbmc_project.Inventory.Item.Component"},
        [chassisId,
         aResp](const boost::system::error_code& ec,
                const dbus::utility::MapperGetSubTreePathsResponse& paths) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "Failed to enumerate activate-capable error types");
                messages::internalError(aResp->res);
                return;
            }
            std::vector<std::string> errorTypes;
            for (const auto& path : paths)
            {
                errorTypes.emplace_back(
                    path.substr(path.find_last_of('/') + 1));
            }
<<<<<<< HEAD
            handler(errorTypes);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        std::array<const char*, 1>{
            "com.nvidia.ErrorInjection.ActivateErrorInjectionPayloadAsync"});
}

inline void getChassisActivateActionInfo(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    getChassis(
        asyncResp, chassisId,
        [asyncResp,
         chassisId](const std::string& /*uri*/, const std::string& /*service*/,
                    const std::string& objPath) {
            asyncResp->res.jsonValue["@odata.type"] =
                "#ActionInfo.v1_2_0.ActionInfo";
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/Chassis/" + chassisId +
                "/Oem/Nvidia/ErrorInjection/ActivateActionInfo";
            asyncResp->res.jsonValue["Id"] = "ActivateActionInfo";
            asyncResp->res.jsonValue["Name"] = "Activate Action Info";

            getActivateCapableErrorTypes(
                asyncResp, objPath,
                [asyncResp](const std::vector<std::string>& errorTypes) {
                    nlohmann::json::object_t errorTypeParam;
                    errorTypeParam["Name"] = "ErrorType";
                    errorTypeParam["Required"] = true;
                    errorTypeParam["DataType"] = "String";
                    nlohmann::json::array_t allowableValues;
                    for (const auto& et : errorTypes)
                    {
                        allowableValues.emplace_back(et);
                    }
                    errorTypeParam["AllowableValues"] =
                        std::move(allowableValues);
                    asyncResp->res.jsonValue["Parameters"] = {errorTypeParam};
                });
        });
}

inline void postChassisErrorInjectionData(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }

    std::string errorType;
    if (!json_util::readJsonAction(req, aResp->res, "ErrorType", errorType))
    {
        return;
    }

    getChassis(aResp, chassisId,
               [aResp, errorType](const std::string& /*uri*/,
                                  const std::string& /*service*/,
                                  const std::string& objPath) {
                   getActivateCapableErrorTypes(
                       aResp, objPath,
                       [aResp, errorType,
                        objPath](const std::vector<std::string>& validTypes) {
                           if (std::find(validTypes.begin(), validTypes.end(),
                                         errorType) == validTypes.end())
                           {
                               messages::actionParameterValueNotInList(
                                   aResp->res, errorType, "ErrorType",
                                   "NvidiaErrorInjection.Activate");
                               return;
                           }
                           std::string errorPath = objPath + "/" + errorType;
                           activateErrorInjectionPayload(aResp, errorPath);
                       });
               });
=======
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaErrorInjection.v1_2_0.NvidiaErrorInjection",
                chassisId);
        });
>>>>>>> GetSubtreePaths
}

inline void requestRoutesErrorInjection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/ErrorInjection/")
        .privileges(redfish::privileges::getChassisCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(getChassisErrorInjectionData, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/ErrorInjection/")
        .privileges(redfish::privileges::patchChassisCollection)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(patchChassisErrorInjectionData, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Oem/Nvidia/ErrorInjection/Actions/NvidiaErrorInjection.Activate")
        .privileges(redfish::privileges::postChassisCollection)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(postChassisErrorInjectionData, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Oem/Nvidia/ErrorInjection/ActivateActionInfo")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(getChassisActivateActionInfo, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/Processors/<str>/Oem/Nvidia/ErrorInjection/")
        .privileges(redfish::privileges::getProcessorCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(getProcessorErrorInjectionData, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/Processors/<str>/Oem/Nvidia/ErrorInjection/")
        .privileges(redfish::privileges::patchProcessorCollection)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(patchProcessorErrorInjectionData, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>"
                      "/Oem/Nvidia/ErrorInjection/")
        .privileges(redfish::privileges::getNetworkAdapterCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            getNetworkAdapterErrorInjectionData, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>"
                      "/Oem/Nvidia/ErrorInjection/")
        .privileges(redfish::privileges::patchNetworkAdapterCollection)
        .methods(boost::beast::http::verb::patch)(std::bind_front(
            patchNetworkAdapterErrorInjectionData, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Switches/<str>"
                      "/Oem/Nvidia/ErrorInjection/")
        .privileges(redfish::privileges::getSwitchCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(getSwitchErrorInjectionData, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Switches/<str>"
                      "/Oem/Nvidia/ErrorInjection/")
        .privileges(redfish::privileges::patchSwitchCollection)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(patchSwitchErrorInjectionData, std::ref(app)));
}

} // namespace redfish
