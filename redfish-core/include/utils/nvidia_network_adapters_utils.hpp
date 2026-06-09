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
#include "logging.hpp"
#include "utils/nvidia_async_set_callbacks.hpp"

#include <boost/system/error_code.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
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
                if (std::find(interfaces.begin(), interfaces.end(),
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
                if (std::find(interfaces.begin(), interfaces.end(),
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
        if (std::find(interfaces.begin(), interfaces.end(),
                      "com.nvidia.DeviceProtection") != interfaces.end())
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

} // namespace nvidia_network_adapters_utils
} // namespace redfish
