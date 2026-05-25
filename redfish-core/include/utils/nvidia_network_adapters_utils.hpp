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
#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/nvidia_async_set_callbacks.hpp"

#include <boost/system/error_code.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace redfish
{

namespace nvidia_network_adapters_utils
{

/**
 * Populate the ErrorInjection path if interface exists. Do basic
 * validation of the input data, and then update using async way.
 *
 * @param[in,out]   aResp            Async HTTP response.
 * @param[in]       chassisId        Chassis's Id.
 * @param[in]       networkAdapterId        NetworkAdapter's Id.
 * @param[in]       networkAdapterPath        NetworkAdapter's dbus object path.
 */
inline void populateErrorInjectionLink(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& chassisId, const std::string& networkAdapterId,
    const std::string& networkAdapterPath)
{
    dbus::utility::getDbusObject(
        networkAdapterPath + "/ErrorInjection",
        std::array<std::string_view, 0>(),
        [aResp, chassisId, networkAdapterId,
         networkAdapterPath](const boost::system::error_code ec,
                             const dbus::utility::MapperGetObject& serviceMap) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("ErrorInjection object not found in {}",
                                 networkAdapterPath);
                return;
            }

            for (const auto& [_, interfaces] : serviceMap)
            {
                if (std::ranges::find(
                        interfaces,
                        "com.nvidia.ErrorInjection.ErrorInjection") ==
                    interfaces.end())
                {
                    continue;
                }
                aResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaNetworkAdapter.v1_0_0.NvidiaNetworkAdapter";
                std::string odataId = "/redfish/v1/Chassis/";
                odataId += chassisId;
                odataId += "/NetworkAdapters/";
                odataId += networkAdapterId;
                odataId += "/Oem/Nvidia/ErrorInjection";
                aResp->res.jsonValue["Oem"]["Nvidia"]["ErrorInjection"] = {
                    {"@odata.id", odataId}};
                return;
            }
        });
}

/**
 * Map D-Bus ProtectionLevel value to Redfish ProtectionOption enum.
 *
 * @param[in]       dbusValue        D-Bus ProtectionLevel value.
 * @return          Redfish ProtectionOption enum value as string.
 */
inline std::string getProtectionLevelDbusToRF(const std::string& dbusValue)
{
    if (dbusValue ==
        "com.nvidia.DeviceProtection.ProtectionOption.NoProtection")
    {
        return "NoProtection";
    }
    if (dbusValue == "com.nvidia.DeviceProtection.ProtectionOption.PreventAll")
    {
        return "PreventAll";
    }
    if (dbusValue ==
        "com.nvidia.DeviceProtection.ProtectionOption.PreventHostFirmwareUpdates")
    {
        return "PreventHostFirmwareUpdates";
    }
    if (dbusValue ==
        "com.nvidia.DeviceProtection.ProtectionOption.PreventHostConfigurations")
    {
        return "PreventHostConfigurations";
    }

    BMCWEB_LOG_WARNING("Unknown ProtectionLevel value: {}", dbusValue);
    return "";
}

/**
 * Map Redfish ProtectionOption enum to D-Bus ProtectionLevel value.
 *
 * @param[in]       redfishValue     Redfish ProtectionOption enum value.
 * @return          D-Bus ProtectionLevel value as string.
 */
inline std::string getProtectionLevelRFToDbus(const std::string& redfishValue)
{
    if (redfishValue == "NoProtection")
    {
        return "com.nvidia.DeviceProtection.ProtectionOption.NoProtection";
    }
    if (redfishValue == "PreventAll")
    {
        return "com.nvidia.DeviceProtection.ProtectionOption.PreventAll";
    }
    if (redfishValue == "PreventHostFirmwareUpdates")
    {
        return "com.nvidia.DeviceProtection.ProtectionOption.PreventHostFirmwareUpdates";
    }
    if (redfishValue == "PreventHostConfigurations")
    {
        return "com.nvidia.DeviceProtection.ProtectionOption.PreventHostConfigurations";
    }

    BMCWEB_LOG_WARNING("Unknown ProtectionOption value: {}", redfishValue);
    return "";
}

/**
 * Read the ProtectionLevel property from D-Bus and populate the response.
 *
 * @param[in,out]   aResp                 Async HTTP response.
 * @param[in]       service               D-Bus service name.
 * @param[in]       networkAdapterPath    NetworkAdapter's dbus object path.
 */
inline void readProtectionPropertyFromDbus(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& networkAdapterPath)
{
    dbus::utility::getAllProperties(
        service, networkAdapterPath, "com.nvidia.DeviceProtection",
        [aResp, networkAdapterPath](
            const boost::system::error_code& e,
            const dbus::utility::DBusPropertiesMap& properties) {
            if (e)
            {
                BMCWEB_LOG_ERROR(
                    "populateProtectionOptions: D-Bus error getting properties : {}",
                    e.message());
                messages::internalError(aResp->res);
                return;
            }

            const std::string* protectionLevel = nullptr;

            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), properties, "ProtectionLevel",
                protectionLevel);

            if (!success)
            {
                BMCWEB_LOG_ERROR("Failed to unpack properties");
                messages::internalError(aResp->res);
                return;
            }

            if (protectionLevel != nullptr)
            {
                aResp->res.jsonValue["Oem"]["Nvidia"]["ProtectionOption"] =
                    getProtectionLevelDbusToRF(*protectionLevel);
            }
        });
}

/**
 * Populate the ProtectionOptions data if interface exists. Do basic
 * validation of the input data, and then update using async way.
 *
 * @param[in,out]   aResp            Async HTTP response.
 * @param[in]       chassisId        Chassis's Id.
 * @param[in]       networkAdapterId        NetworkAdapter's Id.
 * @param[in]       networkAdapterPath        NetworkAdapter's dbus object path.
 */
inline void populateProtectionOptions(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& chassisId, const std::string& networkAdapterId,
    const std::string& networkAdapterPath)
{
    dbus::utility::getDbusObject(
        networkAdapterPath, std::array<std::string_view, 0>{},
        [aResp, chassisId, networkAdapterId, networkAdapterPath](
            const boost::system::error_code ec,
            const dbus::utility::MapperServiceMap& serviceMap) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("ProtectionOptions object not found in {}",
                                 networkAdapterPath);
                return;
            }

            for (const auto& [service, interfaces] : serviceMap)
            {
                if (std::ranges::find(interfaces,
                                      "com.nvidia.DeviceProtection") ==
                    interfaces.end())
                {
                    continue;
                }
                aResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaNetworkAdapter.v1_1_0.NvidiaNetworkAdapter";

                readProtectionPropertyFromDbus(aResp, service,
                                               networkAdapterPath);
                return;
            }
        });
}

/**
 * Patch the ProtectionOption property on the NetworkAdapter.
 *
 * @param[in,out]   resp             Async HTTP response.
 * @param[in]       protectionOption Redfish ProtectionOption value to set.
 * @param[in]       objectPath       D-Bus object path of the NetworkAdapter.
 * @param[in]       serviceMap       Service map from GetObject call.
 */
inline void patchProtectionOption(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& protectionOption, const std::string& objectPath,
    const dbus::utility::MapperServiceMap& serviceMap)
{
    std::string dbusValue = getProtectionLevelRFToDbus(protectionOption);
    if (dbusValue.empty())
    {
        messages::propertyValueIncorrect(resp->res, "ProtectionOption",
                                         protectionOption);
        return;
    }

    std::string serviceName;
    for (const auto& [service, interfaces] : serviceMap)
    {
        if (std::ranges::find(interfaces, "com.nvidia.DeviceProtection") !=
            interfaces.end())
        {
            serviceName = service;
            break;
        }
    }

    if (serviceName.empty())
    {
        BMCWEB_LOG_ERROR("DeviceProtection interface not found on {}",
                         objectPath);
        messages::resourceNotFound(resp->res, "ProtectionOption",
                                   protectionOption);
        return;
    }
    BMCWEB_LOG_DEBUG("Patching ProtectionOption to {} on {}", dbusValue,
                     objectPath);

    nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
        resp, std::chrono::seconds(60), serviceName, objectPath,
        "com.nvidia.DeviceProtection", "ProtectionLevel",
        std::variant<std::string>(dbusValue),
        nvidia_async_operation_utils::PatchGenericCallback{resp});
}

/*
 * Device mode settings — D-Bus interfaces and Redfish OEM mapping.
 *
 * D-Bus interfaces (PDI YAMLs in
 * phosphor-dbus-interfaces/yaml/com/nvidia/DeviceMode/):
 *
 *  Interface                                       Property           Type
 * Values / semantics
 *  ----------------------------------------------- ------------------
 * ------------ --------------------------------
 *  com.nvidia.DeviceMode.DPUOperationMode           CurrentMode       enum
 * string  OperationMode.DPU / .NIC PendingMode       enum string  (writable
 * when IsModeConfigurable) IsModeConfigurable bool        readonly
 *  com.nvidia.DeviceMode.PCIeMultiSocket            CurrentMode       byte 1 =
 * single, 2 = dual, … PendingMode       byte IsModeConfigurable bool
 *  com.nvidia.DeviceMode.PCIeControlledEWTraffic    CurrentMode       enum
 * string  EWTrafficMode.Disabled / .Enabled PendingMode       enum string
 *                                                   IsModeConfigurable bool
 *  com.nvidia.DeviceMode.PCIeBifurcation            CurrentMode       byte 1 =
 * no bif, 2 = 2×8, … PendingMode       byte IsModeConfigurable bool
 *
 * Object path: <NetworkAdapter
 * inventory>/Settings/Oem/Nvidia/DeviceMode/{DPUOperationMode,PCIeDeviceMode}
 * Association: ("network_adapter", "device_mode_settings", <NetworkAdapter
 * path>) Batch PATCH uses com.nvidia.DeviceMode.PCIeDeviceMode / PendingModes
 * on the PCIeDeviceMode object.
 */
constexpr std::string_view dpuOperationModeIntf =
    "com.nvidia.DeviceMode.DPUOperationMode";
constexpr std::string_view pcieMultiSocketDbusIntf =
    "com.nvidia.DeviceMode.PCIeMultiSocket";
constexpr std::string_view pcieControlledEWTrafficDbusIntf =
    "com.nvidia.DeviceMode.PCIeControlledEWTraffic";
constexpr std::string_view pcieBifurcationDbusIntf =
    "com.nvidia.DeviceMode.PCIeBifurcation";
constexpr std::string_view pcieDeviceModePatchIntf =
    "com.nvidia.DeviceMode.PCIeDeviceMode";
constexpr std::string_view dpuOperationModeEnumPrefix =
    "com.nvidia.DeviceMode.DPUOperationMode.OperationMode.";

inline bool dpuOperationModeDbusToRedfish(nlohmann::json& json,
                                          const std::string& dbusValue)
{
    if (dbusValue == "com.nvidia.DeviceMode.DPUOperationMode.OperationMode.DPU")
    {
        json["DPUOperationMode"] = "DPU";
        return true;
    }
    if (dbusValue == "com.nvidia.DeviceMode.DPUOperationMode.OperationMode.NIC")
    {
        json["DPUOperationMode"] = "NIC";
        return true;
    }
    return false;
}

inline bool ewTrafficModeDbusToRedfish(nlohmann::json& json,
                                       const std::string& dbusValue)
{
    if (dbusValue ==
        "com.nvidia.DeviceMode.PCIeControlledEWTraffic.EWTrafficMode.Disabled")
    {
        json["EastWestControlEnabled"] = false;
        return true;
    }
    if (dbusValue ==
        "com.nvidia.DeviceMode.PCIeControlledEWTraffic.EWTrafficMode.Enabled")
    {
        json["EastWestControlEnabled"] = true;
        return true;
    }
    return false;
}

inline bool socketModeDbusToRedfish(nlohmann::json& json, uint8_t dbusValue)
{
    json["NumberOfUpstreamSockets"] = static_cast<int64_t>(dbusValue);
    return true;
}

inline bool bifurcationModeDbusToRedfish(nlohmann::json& json,
                                         uint8_t dbusValue)
{
    json["PCIeBifurcationLinkCount"] = static_cast<int64_t>(dbusValue);
    return true;
}

inline std::optional<uint32_t> socketModeRedfishToRaw(int64_t redfishValue)
{
    if (redfishValue < 0)
    {
        return std::nullopt;
    }
    return static_cast<uint32_t>(redfishValue);
}

inline uint32_t ewTrafficModeRedfishToRaw(bool redfishValue)
{
    return redfishValue ? 1U : 0U;
}

inline std::optional<uint32_t> bifurcationModeRedfishToRaw(int64_t redfishValue)
{
    if (redfishValue < 0)
    {
        return std::nullopt;
    }
    return static_cast<uint32_t>(redfishValue);
}

using DbusToRedfishFn = bool (*)(nlohmann::json&, const std::string&);

struct DeviceModeDescriptor
{
    const char* dbusInterface;
    DbusToRedfishFn dbusToRedfish;
};

constexpr std::array<DeviceModeDescriptor, 2> enumDeviceModeDescriptors = {{
    {"com.nvidia.DeviceMode.DPUOperationMode", dpuOperationModeDbusToRedfish},
    {"com.nvidia.DeviceMode.PCIeControlledEWTraffic",
     ewTrafficModeDbusToRedfish},
}};

using ByteDbusToRedfishFn = bool (*)(nlohmann::json&, uint8_t);

struct ByteDeviceModeDescriptor
{
    const char* dbusInterface;
    ByteDbusToRedfishFn dbusToRedfish;
};

constexpr std::array<ByteDeviceModeDescriptor, 2> byteDeviceModeDescriptors = {
    {{"com.nvidia.DeviceMode.PCIeMultiSocket", socketModeDbusToRedfish},
     {"com.nvidia.DeviceMode.PCIeBifurcation", bifurcationModeDbusToRedfish}}};

inline void afterGetEnumDeviceModeProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    DeviceModeDescriptor desc, bool isReadOnly,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec)
    {
        return;
    }

    const std::string* currentMode = nullptr;
    const std::string* pendingMode = nullptr;
    const bool* isModeConfigurable = nullptr;

    if (!sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), properties, "CurrentMode",
            currentMode, "PendingMode", pendingMode, "IsModeConfigurable",
            isModeConfigurable))
    {
        return;
    }

    nlohmann::json& oemNvidiaJson = asyncResp->res.jsonValue["Oem"]["Nvidia"];
    oemNvidiaJson["@odata.type"] =
        "#NvidiaNetworkAdapter.v1_2_0.NvidiaNetworkAdapter";

    if (isReadOnly)
    {
        if (currentMode != nullptr)
        {
            desc.dbusToRedfish(oemNvidiaJson, *currentMode);
        }
    }
    else
    {
        if (isModeConfigurable != nullptr && *isModeConfigurable &&
            pendingMode != nullptr)
        {
            desc.dbusToRedfish(oemNvidiaJson, *pendingMode);
        }
    }
}

inline void afterGetByteDeviceModeProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    ByteDeviceModeDescriptor desc, bool isReadOnly,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec)
    {
        return;
    }

    const uint8_t* currentMode = nullptr;
    const uint8_t* pendingMode = nullptr;
    const bool* isModeConfigurable = nullptr;

    if (!sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), properties, "CurrentMode",
            currentMode, "PendingMode", pendingMode, "IsModeConfigurable",
            isModeConfigurable))
    {
        return;
    }

    nlohmann::json& oemNvidiaJson = asyncResp->res.jsonValue["Oem"]["Nvidia"];
    oemNvidiaJson["@odata.type"] =
        "#NvidiaNetworkAdapter.v1_2_0.NvidiaNetworkAdapter";

    if (isReadOnly)
    {
        if (currentMode != nullptr)
        {
            desc.dbusToRedfish(oemNvidiaJson, *currentMode);
        }
    }
    else
    {
        if (isModeConfigurable != nullptr && *isModeConfigurable &&
            pendingMode != nullptr)
        {
            desc.dbusToRedfish(oemNvidiaJson, *pendingMode);
        }
    }
}

inline void afterGetDeviceModeObject(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& endpoint, bool isReadOnly,
    const boost::system::error_code& ec,
    const dbus::utility::MapperServiceMap& serviceMap)
{
    if (ec || serviceMap.empty())
    {
        return;
    }

    const auto& [serviceName, interfaces] = *serviceMap.begin();

    for (const auto& desc : enumDeviceModeDescriptors)
    {
        if (std::ranges::find(interfaces, desc.dbusInterface) !=
            interfaces.end())
        {
            dbus::utility::getAllProperties(
                serviceName, endpoint, desc.dbusInterface,
                std::bind_front(afterGetEnumDeviceModeProperties, asyncResp,
                                desc, isReadOnly));
        }
    }
    for (const auto& desc : byteDeviceModeDescriptors)
    {
        if (std::ranges::find(interfaces, desc.dbusInterface) !=
            interfaces.end())
        {
            dbus::utility::getAllProperties(
                serviceName, endpoint, desc.dbusInterface,
                std::bind_front(afterGetByteDeviceModeProperties, asyncResp,
                                desc, isReadOnly));
        }
    }
}

inline void afterGetDeviceModeEndpoints(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, bool isReadOnly,
    const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& endpoints)
{
    if (ec || endpoints.empty())
    {
        BMCWEB_LOG_DEBUG("No device_mode_settings endpoints: {}", ec.message());
        return;
    }

    for (const std::string& endpoint : endpoints)
    {
        dbus::utility::getDbusObject(
            endpoint, std::array<std::string_view, 0>{},
            std::bind_front(afterGetDeviceModeObject, asyncResp, endpoint,
                            isReadOnly));
    }
}

inline void populateDeviceModeSettings(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
    const std::string& networkAdapterPath, bool isReadOnly)
{
    if (isReadOnly)
    {
        asyncResp->res.jsonValue["@Redfish.Settings"]["@odata.type"] =
            "#Settings.v1_3_3.Settings";
        asyncResp->res.jsonValue["@Redfish.Settings"]["SettingsObject"]
                                ["@odata.id"] = boost::urls::format(
            "/redfish/v1/Chassis/{}/NetworkAdapters/{}/Settings", chassisId,
            networkAdapterId);
    }

    dbus::utility::getAssociationEndPoints(
        networkAdapterPath + "/device_mode_settings",
        std::bind_front(afterGetDeviceModeEndpoints, asyncResp, isReadOnly));
}

inline void afterGetDpuModeObject(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& serviceName, const std::string& endpoint,
    const std::string& dbusEnumValue, const boost::system::error_code& ec,
    bool isModeConfigurable)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Failed to read IsModeConfigurable on {}: {}",
                         endpoint, ec.message());
        messages::internalError(asyncResp->res);
        return;
    }

    if (!isModeConfigurable)
    {
        BMCWEB_LOG_DEBUG("DPUOperationMode is not configurable on {}",
                         endpoint);
        messages::propertyUnknown(asyncResp->res, "DPUOperationMode");
        return;
    }

    nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
        asyncResp, std::chrono::seconds(60), serviceName, endpoint,
        std::string(dpuOperationModeIntf), "PendingMode",
        std::variant<std::string>(dbusEnumValue),
        nvidia_async_operation_utils::PatchGenericCallback{asyncResp});
}

inline void afterGetDpuModeEndpoints(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& networkAdapterId, const std::string& dbusEnumValue,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec || subtree.empty() || subtree.front().second.empty())
    {
        BMCWEB_LOG_DEBUG(
            "No DPUOperationMode DeviceMode object for NetworkAdapter {}: {}",
            networkAdapterId, ec.message());
        messages::resourceNotFound(asyncResp->res, "DPUOperationMode",
                                   networkAdapterId);
        return;
    }

    const auto& [endpoint, serviceMap] = subtree.front();
    const std::string& serviceName = serviceMap.front().first;
    dbus::utility::getProperty<bool>(
        serviceName, endpoint, std::string(dpuOperationModeIntf),
        "IsModeConfigurable",
        std::bind_front(afterGetDpuModeObject, asyncResp, serviceName, endpoint,
                        dbusEnumValue));
}

inline void patchDpuOperationMode(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& networkAdapterId, const std::string& networkAdapterPath,
    const std::string& redfishValue)
{
    std::string dbusEnumValue =
        std::string(dpuOperationModeEnumPrefix) + redfishValue;

    dbus::utility::getAssociatedSubTree(
        sdbusplus::message::object_path(
            networkAdapterPath + "/device_mode_settings"),
        sdbusplus::message::object_path("/xyz/openbmc_project/inventory"), 0,
        std::array<std::string_view, 1>{dpuOperationModeIntf},
        std::bind_front(afterGetDpuModeEndpoints, asyncResp, networkAdapterId,
                        dbusEnumValue));
}

inline void afterGetPcieModeObject(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& endpoint,
    const std::vector<std::tuple<std::string, uint32_t>>& modeEntries,
    const boost::system::error_code& ec,
    const dbus::utility::MapperServiceMap& serviceMap)
{
    if (ec || serviceMap.empty())
    {
        return;
    }

    const auto& serviceName = serviceMap.begin()->first;
    nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
        asyncResp, std::chrono::seconds(60), serviceName, endpoint,
        std::string(pcieDeviceModePatchIntf), "PendingModes",
        std::variant<std::vector<std::tuple<std::string, uint32_t>>>(
            modeEntries),
        nvidia_async_operation_utils::PatchGenericCallback{asyncResp});
}

inline void afterGetPcieModeEndpoints(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::vector<std::tuple<std::string, uint32_t>>& modeEntries,
    const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& endpoints)
{
    if (ec || endpoints.empty())
    {
        BMCWEB_LOG_DEBUG(
            "patchPCIeDeviceMode: no device_mode_settings endpoints: {}",
            ec.message());
        return;
    }

    for (const std::string& endpoint : endpoints)
    {
        dbus::utility::getDbusObject(
            endpoint, std::array<std::string_view, 1>{pcieMultiSocketDbusIntf},
            std::bind_front(afterGetPcieModeObject, asyncResp, endpoint,
                            modeEntries));
    }
}

inline void patchPCIeDeviceMode(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& networkAdapterPath,
    const std::vector<std::tuple<std::string, uint32_t>>& modeEntries)
{
    dbus::utility::getAssociationEndPoints(
        networkAdapterPath + "/device_mode_settings",
        std::bind_front(afterGetPcieModeEndpoints, asyncResp, modeEntries));
}

/*
 * OOB Miswiring Detection — LLDP mode interfaces.
 *
 * D-Bus interface (PDI YAML:
 * yaml/com/nvidia/Network/LLDP/Modes.interface.yaml):
 *   com.nvidia.Network.LLDP.Modes
 *     - TXMode    : enum string (LLDPModeType : Off | Mandatory | All)
 *     - RXMode    : enum string (LLDPModeType : Off | Mandatory | All)
 *     - DCBXMode  : enum string (DCBXModeType : Disabled | Enabled)
 *
 * Object path (published by nsmd):
 *   <NetworkAdapter>/Settings/Oem/Nvidia/LLDPModes
 * Association: ("network_adapter", "lldp_mode_settings", <NetworkAdapter path>)
 *
 * Redfish OEM mapping (NvidiaNetworkAdapter.v1_3_0):
 * Oem.Nvidia.LLDP.{TXMode, RXMode, DCBXModeEnabled (boolean)}
 */
constexpr std::string_view lldpRedfishTxMode = "TXMode";
constexpr std::string_view lldpRedfishRxMode = "RXMode";
constexpr std::string_view lldpRedfishDcbxMode = "DCBXModeEnabled";
constexpr std::string_view lldpRedfishTxModePath = "Oem/Nvidia/LLDP/TXMode";
constexpr std::string_view lldpRedfishRxModePath = "Oem/Nvidia/LLDP/RXMode";
constexpr std::string_view lldpRedfishDcbxModePath =
    "Oem/Nvidia/LLDP/DCBXModeEnabled";
constexpr std::string_view lldpModesDbusIntf = "com.nvidia.Network.LLDP.Modes";
constexpr std::string_view lldpModeTypeEnumPrefix =
    "com.nvidia.Network.LLDP.Modes.LLDPModeType.";
constexpr std::string_view dcbxModeTypeEnumPrefix =
    "com.nvidia.Network.LLDP.Modes.DCBXModeType.";
constexpr std::string_view nvidiaNetworkAdapterOdataType =
    "#NvidiaNetworkAdapter.v1_3_0.NvidiaNetworkAdapter";

inline std::string stripLldpEnumPrefix(const std::string& dbusValue)
{
    size_t lastDot = dbusValue.find_last_of('.');
    if (lastDot == std::string::npos)
    {
        return dbusValue;
    }
    return dbusValue.substr(lastDot + 1);
}

inline bool isValidLldpModeValue(const std::string& value)
{
    return value == "Off" || value == "Mandatory" || value == "All";
}

inline bool dcbxModeDbusToRedfish(const std::string& dbusValue)
{
    return stripLldpEnumPrefix(dbusValue) == "Enabled";
}

inline std::string lldpModeRedfishToDbus(const std::string& redfishValue)
{
    return std::string(lldpModeTypeEnumPrefix) + redfishValue;
}

inline std::string dcbxModeRedfishToDbus(bool enabled)
{
    return std::string(dcbxModeTypeEnumPrefix) +
           (enabled ? "Enabled" : "Disabled");
}

inline void afterGetLldpModeProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("LLDP Modes GetAll error: {}", ec.message());
        return;
    }

    const std::string* txMode = nullptr;
    const std::string* rxMode = nullptr;
    const std::string* dcbxMode = nullptr;

    if (!sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), properties, "TXMode", txMode,
            "RXMode", rxMode, "DCBXMode", dcbxMode))
    {
        return;
    }

    nlohmann::json& oemNvidiaJson = asyncResp->res.jsonValue["Oem"]["Nvidia"];
    oemNvidiaJson["@odata.type"] = nvidiaNetworkAdapterOdataType;
    nlohmann::json& lldpJson = oemNvidiaJson["LLDP"];
    if (txMode != nullptr)
    {
        lldpJson[std::string(lldpRedfishTxMode)] = stripLldpEnumPrefix(*txMode);
    }
    if (rxMode != nullptr)
    {
        lldpJson[std::string(lldpRedfishRxMode)] = stripLldpEnumPrefix(*rxMode);
    }
    if (dcbxMode != nullptr)
    {
        lldpJson[std::string(lldpRedfishDcbxMode)] =
            dcbxModeDbusToRedfish(*dcbxMode);
    }
}

inline void afterGetLldpModesObject(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath, const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& serviceMap)
{
    if (ec || serviceMap.empty())
    {
        BMCWEB_LOG_DEBUG("LLDP Modes object not found at {}", objectPath);
        return;
    }
    // Service that owns com.nvidia.Network.LLDP.Modes at this path.
    const std::string& serviceName = serviceMap.front().first;
    dbus::utility::getAllProperties(
        serviceName, objectPath, std::string(lldpModesDbusIntf),
        std::bind_front(afterGetLldpModeProperties, asyncResp));
}

inline void afterGetLldpModeEndpoints(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& endpoints)
{
    if (ec || endpoints.empty())
    {
        BMCWEB_LOG_DEBUG("No lldp_mode_settings endpoints: {}", ec.message());
        return;
    }

    for (const std::string& endpoint : endpoints)
    {
        dbus::utility::getDbusObject(
            endpoint, std::array<std::string_view, 1>{lldpModesDbusIntf},
            std::bind_front(afterGetLldpModesObject, asyncResp, endpoint));
    }
}

/**
 * @brief Populate Oem.Nvidia.LLDP block on a NetworkAdapter Settings resource.
 *
 * Resolves com.nvidia.Network.LLDP.Modes via the parent NetworkAdapter's
 * lldp_mode_settings association. On any D-Bus error the block is silently
 * omitted (the caller's other fields are preserved).
 */
inline void populateLldpModeSettings(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& networkAdapterPath)
{
    dbus::utility::getAssociationEndPoints(
        networkAdapterPath + "/lldp_mode_settings",
        std::bind_front(afterGetLldpModeEndpoints, asyncResp));
}

struct LldpModePatchState
{
    std::shared_ptr<bmcweb::AsyncResp> asyncResp;
    std::string networkAdapterId;
    std::string service;
    std::string endpoint;
    std::optional<std::string> txMode;
    std::optional<std::string> rxMode;
    std::optional<bool> dcbxMode;
};

inline void continueLldpModePatch(
    const std::shared_ptr<LldpModePatchState>& state);

class PatchLldpModeCallback
{
  public:
    PatchLldpModeCallback(std::shared_ptr<LldpModePatchState> stateIn,
                          std::string propertyNameIn,
                          std::string propertyValueIn) :
        state(std::move(stateIn)), propertyName(std::move(propertyNameIn)),
        propertyValue(std::move(propertyValueIn))
    {}

    void operator()(const std::string& status) const
    {
        if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
        {
            continueLldpModePatch(state);
            return;
        }

        nvidia_async_operation_utils::PatchGenericCallback{
            state->asyncResp, propertyName, propertyValue}(status);
    }

  private:
    std::shared_ptr<LldpModePatchState> state;
    std::string propertyName;
    std::string propertyValue;
};

inline void doLldpModeAsyncSet(
    const std::shared_ptr<LldpModePatchState>& state,
    const std::string& dbusProperty, const std::string& redfishProperty,
    const std::string& redfishValue, const std::string& dbusValue)
{
    nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
        state->asyncResp, std::chrono::seconds(60), state->service,
        state->endpoint, std::string(lldpModesDbusIntf), dbusProperty,
        std::variant<std::string>(dbusValue),
        PatchLldpModeCallback{state, redfishProperty, redfishValue});
}

inline void continueLldpModePatch(
    const std::shared_ptr<LldpModePatchState>& state)
{
    if (state->txMode)
    {
        std::string redfishValue = *state->txMode;
        state->txMode.reset();
        doLldpModeAsyncSet(state, "TXMode", std::string(lldpRedfishTxMode),
                           redfishValue, lldpModeRedfishToDbus(redfishValue));
        return;
    }
    if (state->rxMode)
    {
        std::string redfishValue = *state->rxMode;
        state->rxMode.reset();
        doLldpModeAsyncSet(state, "RXMode", std::string(lldpRedfishRxMode),
                           redfishValue, lldpModeRedfishToDbus(redfishValue));
        return;
    }
    if (state->dcbxMode)
    {
        bool redfishValue = *state->dcbxMode;
        state->dcbxMode.reset();
        doLldpModeAsyncSet(state, "DCBXMode", std::string(lldpRedfishDcbxMode),
                           redfishValue ? "true" : "false",
                           dcbxModeRedfishToDbus(redfishValue));
    }
}

inline void afterGetLldpModeObjectForPatch(
    const std::shared_ptr<LldpModePatchState>& state,
    const std::string& endpoint, const boost::system::error_code& ec,
    const dbus::utility::MapperServiceMap& serviceMap)
{
    if (ec || serviceMap.empty())
    {
        BMCWEB_LOG_WARNING("LLDP Modes object not present at {}", endpoint);
        messages::resourceNotFound(state->asyncResp->res, "NetworkAdapter",
                                   state->networkAdapterId);
        return;
    }

    state->service = serviceMap.begin()->first;
    state->endpoint = endpoint;
    continueLldpModePatch(state);
}

inline void afterGetLldpModeEndpointsForPatch(
    const std::shared_ptr<LldpModePatchState>& state,
    const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& endpoints)
{
    if (ec || endpoints.empty())
    {
        BMCWEB_LOG_WARNING(
            "No lldp_mode_settings endpoints for NetworkAdapter patch");
        messages::resourceNotFound(state->asyncResp->res, "NetworkAdapter",
                                   state->networkAdapterId);
        return;
    }

    if (endpoints.size() > 1)
    {
        BMCWEB_LOG_DEBUG(
            "Multiple lldp_mode_settings endpoints for NetworkAdapter patch; using first");
    }

    const std::string& endpoint = endpoints.front();
    dbus::utility::getDbusObject(
        endpoint, std::array<std::string_view, 1>{lldpModesDbusIntf},
        std::bind_front(afterGetLldpModeObjectForPatch, state, endpoint));
}

/**
 * @brief Dispatch LLDP mode PATCH writes to com.nvidia.Network.LLDP.Modes.
 *
 * Resolves the target object via the parent NetworkAdapter's
 * lldp_mode_settings association, then writes each provided property through
 * the SetAsync path (same pattern as DPU/PCIe device mode patches). Multiple
 * properties are applied sequentially because nsmd serializes LLDP writes.
 */
inline void patchLldpModes(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& networkAdapterId, const std::string& networkAdapterPath,
    const std::optional<std::string>& txMode,
    const std::optional<std::string>& rxMode,
    const std::optional<bool>& dcbxMode)
{
    auto state = std::make_shared<LldpModePatchState>(LldpModePatchState{
        asyncResp, networkAdapterId, {}, {}, txMode, rxMode, dcbxMode});

    dbus::utility::getAssociationEndPoints(
        networkAdapterPath + "/lldp_mode_settings",
        std::bind_front(afterGetLldpModeEndpointsForPatch, state));
}

} // namespace nvidia_network_adapters_utils
} // namespace redfish
