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

#include <atomic>
#include <cstdint>
namespace redfish
{

namespace nvidia_network_adapters_utils
{
// Map of service name to list of interfaces
using MapperServiceMap =
    std::vector<std::pair<std::string, std::vector<std::string>>>;

// Map of object paths to MapperServiceMaps
using MapperGetSubTreeResponse =
    std::vector<std::pair<std::string, MapperServiceMap>>;

/**
 * Populate the ErrorInjection path if interface exists. Do basic
 * validation of the input data, and then update using async way.
 *
 * @param[in,out]   aResp            Async HTTP response.
 * @param[in]       chassisId        Chassis's Id.
 * @param[in]       networkAdapterId        NetworkAdapter's Id.
 * @param[in]       networkAdapterPath        NetworkAdapter's dbus object path.
 */
inline void
    populateErrorInjectionLink(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const std::string& chassisId,
                               const std::string& networkAdapterId,
                               const std::string& networkAdapterPath)
{
    crow::connections::systemBus->async_method_call(
        [aResp, chassisId, networkAdapterId,
         networkAdapterPath](const boost::system::error_code ec,
                             const MapperServiceMap& serviceMap) {
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
            if (!aResp->res.jsonValue["Oem"]["Nvidia"].contains("@odata.type"))
            {
                aResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaNetworkAdapter.v1_1_0.NvidiaNetworkAdapter";
            }
            aResp->res.jsonValue["Oem"]["Nvidia"]["ErrorInjection"] = {
                {"@odata.id", "/redfish/v1/Chassis/" + chassisId +
                                  "/NetworkAdapters/" + networkAdapterId +
                                  "/Oem/Nvidia/ErrorInjection"}};
            return;
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject",
        networkAdapterPath + "/ErrorInjection", std::array<const char*, 0>());
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
 * Handle the response from getAllProperties for ProtectionLevel property.
 *
 * @param[in]       e                    Error code from D-Bus call.
 * @param[in]       properties           D-Bus properties map.
 * @param[in,out]   aResp                Async HTTP response.
 */
inline void handleProtectionPropertyResponse(
    const boost::system::error_code& e,
    const dbus::utility::DBusPropertiesMap& properties,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
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
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, networkAdapterPath,
        "com.nvidia.DeviceProtection",
        [aResp](const boost::system::error_code& e,
                const dbus::utility::DBusPropertiesMap& properties) {
            handleProtectionPropertyResponse(e, properties, aResp);
        });
}

/**
 * Handle the response from getDbusObject for ProtectionOptions interface.
 *
 * @param[in]       ec                  Error code from D-Bus call.
 * @param[in]       serviceMap          D-Bus service map.
 * @param[in]       aResp               Async HTTP response.
 * @param[in]       networkAdapterPath  NetworkAdapter's dbus object path.
 */
inline void handleProtectionOptionsResponse(
    const boost::system::error_code ec,
    const dbus::utility::MapperServiceMap& serviceMap,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& networkAdapterPath)
{
    if (ec || serviceMap.empty())
    {
        BMCWEB_LOG_DEBUG("ProtectionOptions object not found in {}",
                         networkAdapterPath);
        return;
    }

    const auto& [service, _interfaces] = *serviceMap.begin();
    if (!aResp->res.jsonValue["Oem"]["Nvidia"].contains("@odata.type"))
    {
        aResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
            "#NvidiaNetworkAdapter.v1_1_0.NvidiaNetworkAdapter";
    }

    readProtectionPropertyFromDbus(aResp, service, networkAdapterPath);
}

/**
 * Populate the ProtectionOptions data if interface exists. Do basic
 * validation of the input data, and then update using async way.
 *
 * @param[in,out]   aResp            Async HTTP response.
 * @param[in]       networkAdapterPath        NetworkAdapter's dbus object path.
 */
inline void populateProtectionOptions(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& networkAdapterPath)
{
    dbus::utility::getDbusObject(
        networkAdapterPath,
        std::array<std::string_view, 1>{"com.nvidia.DeviceProtection"},
        [aResp, networkAdapterPath](
            const boost::system::error_code ec,
            const dbus::utility::MapperServiceMap& serviceMap) {
            handleProtectionOptionsResponse(ec, serviceMap, aResp,
                                            networkAdapterPath);
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

    if (serviceMap.empty())
    {
        BMCWEB_LOG_ERROR("Service map is empty for object path {}",
                         objectPath);
        messages::resourceNotFound(resp->res, "ProtectionOption",
                                   protectionOption);
        return;
    }

    const auto& [service, _interfaces] = *serviceMap.begin();

    if (service.empty())
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
        resp, std::chrono::seconds(60), service, objectPath,
        "com.nvidia.DeviceProtection", "ProtectionLevel",
        std::variant<std::string>(dbusValue),
        nvidia_async_operation_utils::PatchGenericCallback{resp});
}

} // namespace nvidia_network_adapters_utils
} // namespace redfish
