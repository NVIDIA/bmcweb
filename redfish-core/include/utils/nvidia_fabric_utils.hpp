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

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http_body.hpp"
#include "logging.hpp"
#include "nvidia_dbus_utility.hpp"
#include "utils/nvidia_async_call_utils.hpp"
#include "utils/nvidia_async_set_callbacks.hpp"
#include "utils/nvidia_async_set_utils.hpp"
#include "utils/nvidia_manager_utils.hpp"

#include <boost/container/flat_map.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
namespace redfish
{

namespace nvidia_fabric_utils
{

/**
 * Handle the PATCH operation of the L1 Power Mode Boolean Property. Do basic
 * validation of the input data, and then update using async way.
 *
 * @param[in,out]   resp            Async HTTP response.
 * @param[in]       l1PredictionModeEnabled   New property value to apply.
 * @param[in]       objectPath      Path of object to modify.
 * @param[in]       serviceMap      Service map for CPU object.
 */
inline void patchL1PowerMode(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const bool& l1PredictionModeEnabled, const std::string& objectPath,
    [[maybe_unused]] const dbus::utility::MapperServiceMap& serviceMap)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objectPath + "/l1_prediction_mode",
        "xyz.openbmc_project.Association", "endpoints",
        [resp, l1PredictionModeEnabled,
         objectPath](const boost::system::error_code& ec,
                     const std::vector<std::string>& respData) {
            if (ec)
            {
                // no associated histograms = no failure
                BMCWEB_LOG_DEBUG("No associated L1 Prediction Mode on {}",
                                 objectPath);
                return;
            }

            for (const auto& path : respData)
            {
                dbus::utility::getDbusObject(
                    path,
                    std::array<std::string_view, 1>{
                        nvidia_async_operation_utils::setAsyncInterfaceName},
                    [resp, l1PredictionModeEnabled,
                     path](const boost::system::error_code& ec1,
                           const dbus::utility::MapperGetObject& object) {
                        if (!ec1)
                        {
                            std::string serviceName;
                            for (const auto& [serv, interfaceList] : object)
                            {
                                if (std::ranges::find(
                                        interfaceList,

                                        "xyz.openbmc_project.Object.Enable") !=
                                    interfaceList.end())
                                {
                                    serviceName = serv;
                                    break;
                                }
                            }

                            if (serviceName.empty())
                            {
                                BMCWEB_LOG_ERROR(
                                    "L1 Prediction Mode interface not found on {}",
                                    path);
                                messages::internalError(resp->res);
                                return;
                            }

                            BMCWEB_LOG_DEBUG(
                                "Performing Patch using Set Async Method Call for {}",
                                path);

                            nvidia_async_operation_utils::
                                doGenericSetAsyncAndGatherResult(
                                    resp, std::chrono::seconds(60), serviceName,
                                    path, "xyz.openbmc_project.Object.Enable",
                                    "Enabled",
                                    std::variant<bool>(l1PredictionModeEnabled),
                                    nvidia_async_operation_utils::
                                        PatchL1PredictionModeCallback{resp});

                            return;
                        }
                    });
            }
        });
}

/**
 * Find the D-Bus object representing the requested switch, and call the
 * handler with the results. If matching object is not found, add 404 error to
 * response and don't call the handler.
 *
 * @param[in,out]   resp            Async HTTP response.
 * @param[in]       fabricId        Redfish Fabric Id.
 * @param[in]       switchId        Redfish Switch Id.
 * @param[in]       handler         Callback to continue processing request upon
 *                                  successfully finding object.
 */
template <typename Handler>
inline void getSwitchObject(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                            const std::string& fabricId,
                            const std::string& switchId, Handler&& handler)
{
    BMCWEB_LOG_DEBUG("Get available switch on fabric resources.");

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Inventory.Item.Fabric"},
        [resp, fabricId, switchId, handler = std::forward<Handler>(handler)](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) mutable {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error: {} while getting fabric",
                                 ec);
                messages::internalError(resp->res);
                return;
            }

            bool isFoundFabricObject = false;
            for (const auto& [objectPath, serviceMap] : subtree)
            {
                // Ignore any objects which don't end with our desired fabric
                if (!objectPath.ends_with(fabricId))
                {
                    continue;
                }

                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper",
                    objectPath + "/all_switches",
                    "xyz.openbmc_project.Association", "endpoints",
                    [resp, fabricId, switchId,
                     handler](const boost::system::error_code& ec2,
                              const std::vector<std::string>& response) {
                        if (ec2)
                        {
                            BMCWEB_LOG_ERROR(
                                "DBUS response error: {} while getting switch",
                                ec2);
                            messages::internalError(resp->res);
                            return;
                        }

                        bool isFoundSwitchObject = false;
                        // Iterate over all retrieved ObjectPaths.
                        for (const std::string& path : response)
                        {
                            sdbusplus::object_path objPath(path);
                            if (objPath.filename() != switchId)
                            {
                                continue;
                            }

                            dbus::utility::getDbusObject(
                                path, std::array<std::string_view, 0>(),
                                [resp, fabricId, switchId, path,
                                 handler](const boost::system::error_code& ec3,
                                          const dbus::utility::MapperGetObject&
                                              object) {
                                    if (ec3)
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "Dbus response error while getting service name for switch");
                                        messages::internalError(resp->res);
                                        return;
                                    }
                                    handler(resp, fabricId, switchId, path,
                                            object);
                                });
                            isFoundSwitchObject = true;
                        }
                        if (!isFoundSwitchObject)
                        {
                            messages::resourceNotFound(resp->res, "Switch",
                                                       switchId);
                        }
                    });

                isFoundFabricObject = true;
            }
            if (!isFoundFabricObject)
            {
                messages::resourceNotFound(resp->res, "Fabric", fabricId);
            }
        });
}

/**
 * Populate the ErrorInjection path if interface exists. Do basic
 * validation of the input data, and then update using async way.
 *
 * @param[in,out]   resp            Async HTTP response.
 * @param[in]       fabricId        Fabric's Id.
 * @param[in]       switchId        Switch's Id.
 */
inline void populateErrorInjectionData(
    const std::shared_ptr<bmcweb::AsyncResp>& resp, const std::string& fabricId,
    const std::string& switchId)
{
    getSwitchObject(
        resp, fabricId, switchId,
        [](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
           const std::string& fabricId2, const std::string& switchId2,
           const std::string& path,
           [[maybe_unused]] const dbus::utility::MapperServiceMap& serviceMap) {
            std::string errorInjectionObjPath = path;
            errorInjectionObjPath += "/ErrorInjection";
            dbus::utility::getDbusObject(
                errorInjectionObjPath, std::array<std::string_view, 0>(),
                [aResp, fabricId2, switchId2,
                 path](const boost::system::error_code& ec4,
                       const dbus::utility::MapperGetObject& serviceMap2) {
                    if (ec4)
                    {
                        BMCWEB_LOG_DEBUG(
                            "ErrorInjection object not found in {}", path);
                        return;
                    }

                    for (const auto& [_, interfaces] : serviceMap2)
                    {
                        if (std::ranges::find(
                                interfaces,
                                "com.nvidia.ErrorInjection.ErrorInjection") ==
                            interfaces.end())
                        {
                            continue;
                        }
                        aResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                            "#NvidiaSwitch.v1_5_0.NvidiaSwitch";
                        std::string errorInjectionPath = "/redfish/v1/Fabrics/";
                        errorInjectionPath += fabricId2;
                        errorInjectionPath += "/Switches/";
                        errorInjectionPath += switchId2;
                        errorInjectionPath += "/Oem/Nvidia/ErrorInjection";
                        aResp->res
                            .jsonValue["Oem"]["Nvidia"]["ErrorInjection"] = {
                            {"@odata.id", errorInjectionPath}};
                        return;
                    }
                });
        });
}

inline void updateSwitchPowerModeData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Switch Power mode Data");
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/l1_prediction_mode",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, service, objPath](const boost::system::error_code& ec,
                                      const std::vector<std::string>& resp) {
            if (ec)
            {
                // no associated histograms = no failure
                BMCWEB_LOG_DEBUG("No associated L1 Prediction Mode on {}",
                                 objPath);
                return;
            }
            for (const auto& path : resp)
            {
                dbus::utility::getAllProperties(
                    service, path, "xyz.openbmc_project.Object.Enable",
                    [path, asyncResp](
                        const boost::system::error_code& ec1,
                        const dbus::utility::DBusPropertiesMap& properties) {
                        if (ec1)
                        {
                            BMCWEB_LOG_ERROR(
                                "DBUS response error for updateSwitchPowerModeData()");
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        for (const auto& [propertyName, propertyValue] :
                             properties)
                        {
                            if (propertyName == "Enabled")
                            {
                                const bool* value =
                                    std::get_if<bool>(&propertyValue);
                                if (value == nullptr)
                                {
                                    BMCWEB_LOG_ERROR("Null value returned "
                                                     "for L1 Prediction Mode");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                asyncResp->res
                                    .jsonValue["L1PredictionModeEnabled"] =
                                    *value;
                            }
                        }
                    });
            }
        });
}

inline void getSwitchPowerModeLink(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath, const std::string& switchURI)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objectPath + "/l1_prediction_mode",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, objectPath,
         switchURI](const boost::system::error_code& ec,
                    const std::vector<std::string>& /*resp*/) {
            if (ec)
            {
                // no associated histograms = no failure
                BMCWEB_LOG_DEBUG("No associated L1 Prediction Mode on {}",
                                 switchURI);
                return;
            }

            std::string switchPowerModeURI = switchURI;
            switchPowerModeURI += "/Oem/Nvidia/PowerMode";
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaSwitch.v1_6_0.NvidiaSwitch";
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["PowerMode"]["@odata.id"] =
                switchPowerModeURI;
        });
}

constexpr std::string_view powerCappingModeIntf =
    "com.nvidia.DeviceMode.PowerCappingMode";

inline std::string translatePowerCapModeDbusToRedfish(
    const std::string& dbusValue)
{
    if (dbusValue ==
        "com.nvidia.DeviceMode.PowerCappingMode.PowerCapMode.Default")
    {
        return "Default";
    }
    if (dbusValue ==
        "com.nvidia.DeviceMode.PowerCappingMode.PowerCapMode.Enabled")
    {
        return "Enabled";
    }
    if (dbusValue ==
        "com.nvidia.DeviceMode.PowerCappingMode.PowerCapMode.Disabled")
    {
        return "Disabled";
    }
    return "";
}

inline std::string translatePowerCapModeRedfishToDbus(
    const std::string& redfishValue)
{
    if (redfishValue == "Default")
    {
        return "com.nvidia.DeviceMode.PowerCappingMode.PowerCapMode.Default";
    }
    if (redfishValue == "Enabled")
    {
        return "com.nvidia.DeviceMode.PowerCappingMode.PowerCapMode.Enabled";
    }
    if (redfishValue == "Disabled")
    {
        return "com.nvidia.DeviceMode.PowerCappingMode.PowerCapMode.Disabled";
    }
    return "";
}

inline void afterUpdateSwitchPowerCappingModeData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& switchURI, const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "DBUS response error for updateSwitchPowerCappingModeData(): {}",
            ec);
        messages::internalError(asyncResp->res);
        return;
    }
    for (const auto& [propertyName, propertyValue] : properties)
    {
        if (propertyName != "CurrentMode")
        {
            continue;
        }
        const std::string* value = std::get_if<std::string>(&propertyValue);
        if (value == nullptr)
        {
            BMCWEB_LOG_ERROR(
                "PowerCappingMode CurrentMode property is not a string");
            messages::internalError(asyncResp->res);
            return;
        }
        std::string redfishValue = translatePowerCapModeDbusToRedfish(*value);
        // Active resource reports Enabled/Disabled only.
        if (redfishValue.empty() || redfishValue == "Default")
        {
            BMCWEB_LOG_ERROR(
                "Unexpected CurrentMode on active PowerCappingMode: {}",
                *value);
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["PowerCapMode"] = redfishValue;
    }
    std::string settingsURI =
        switchURI + "/Oem/Nvidia/PowerCappingMode/Settings";
    asyncResp->res.jsonValue["@Redfish.Settings"]["@odata.type"] =
        "#Settings.v1_3_3.Settings";
    asyncResp->res
        .jsonValue["@Redfish.Settings"]["SettingsObject"]["@odata.id"] =
        settingsURI;
    std::string resetTarget =
        switchURI + "/Oem/Nvidia/PowerCappingMode/Actions/"
                    "NvidiaSwitchPowerCapMode.ResetToDefaults";
    asyncResp->res
        .jsonValue["Actions"]["#NvidiaSwitchPowerCapMode.ResetToDefaults"]
                  ["target"] = resetTarget;
}

inline void updateSwitchPowerCappingModeData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& powerCapObjPath,
    const std::string& switchURI)
{
    dbus::utility::getAllProperties(
        service, powerCapObjPath, std::string(powerCappingModeIntf),
        std::bind_front(afterUpdateSwitchPowerCappingModeData, asyncResp,
                        switchURI));
}

inline void afterUpdateSwitchPowerCappingModeSettingsData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "DBUS response error for PowerCappingMode Settings: {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }
    for (const auto& [propertyName, propertyValue] : properties)
    {
        if (propertyName != "PendingMode")
        {
            continue;
        }
        const std::string* value = std::get_if<std::string>(&propertyValue);
        if (value == nullptr)
        {
            BMCWEB_LOG_ERROR(
                "PowerCappingMode Settings PendingMode property is not a string");
            messages::internalError(asyncResp->res);
            return;
        }
        // Settings publishes Enabled/Disabled only; omit internal Default.
        std::string redfishValue = translatePowerCapModeDbusToRedfish(*value);
        if (redfishValue.empty())
        {
            BMCWEB_LOG_ERROR(
                "Unexpected PendingMode on PowerCappingMode Settings: {}",
                *value);
            messages::internalError(asyncResp->res);
            return;
        }
        if (redfishValue == "Default")
        {
            return;
        }
        asyncResp->res.jsonValue["PowerCapMode"] = redfishValue;
    }
}

inline void updateSwitchPowerCappingModeSettingsData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& powerCapObjPath)
{
    asyncResp->res.jsonValue["@Redfish.SettingsApplyTime"]["@odata.type"] =
        "#Settings.v1_3_3.PreferredApplyTime";
    asyncResp->res.jsonValue["@Redfish.SettingsApplyTime"]["ApplyTime"] =
        "OnReset";
    dbus::utility::getAllProperties(
        service, powerCapObjPath, std::string(powerCappingModeIntf),
        std::bind_front(afterUpdateSwitchPowerCappingModeSettingsData,
                        asyncResp));
}

inline void afterGetSwitchPowerCappingModeLink(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& switchURI, const boost::system::error_code& ec,
    const std::vector<std::string>& endpoints)
{
    if (ec || endpoints.empty())
    {
        return;
    }
    std::string uri = switchURI + "/Oem/Nvidia/PowerCappingMode";
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
        "#NvidiaSwitch.v1_5_0.NvidiaSwitch";
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["PowerCappingMode"]["@odata.id"] =
        uri;
}

inline void getSwitchPowerCappingModeLink(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath, const std::string& switchURI)
{
    dbus::utility::findAssociations(
        objectPath + "/power_capping_mode",
        std::bind_front(afterGetSwitchPowerCappingModeLink, asyncResp,
                        switchURI));
}

using PowerCappingModeObjectHandler =
    std::function<void(const std::string&, const std::string&,
                       const dbus::utility::MapperGetObject&)>;

inline void afterGetSwitchPowerCappingModeDbusObject(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& powerCapObjPath,
    const PowerCappingModeObjectHandler& handler,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec || object.empty())
    {
        messages::internalError(resp->res);
        return;
    }
    handler(object.front().first, powerCapObjPath, object);
}

inline void afterGetSwitchPowerCappingModeAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& resp, const std::string& switchId,
    const PowerCappingModeObjectHandler& handler,
    const boost::system::error_code& ec,
    const std::vector<std::string>& endpoints)
{
    if (ec || endpoints.empty())
    {
        messages::resourceNotFound(resp->res, "PowerCappingMode", switchId);
        return;
    }
    const std::string& powerCapObjPath = endpoints.front();
    dbus::utility::getDbusObject(
        powerCapObjPath, std::array<std::string_view, 1>{powerCappingModeIntf},
        std::bind_front(afterGetSwitchPowerCappingModeDbusObject, resp,
                        powerCapObjPath, handler));
}

inline void getSwitchPowerCappingModeObject(
    const std::shared_ptr<bmcweb::AsyncResp>& resp, const std::string& switchId,
    const std::string& switchObjPath,
    const PowerCappingModeObjectHandler& handler)
{
    dbus::utility::findAssociations(
        switchObjPath + "/power_capping_mode",
        std::bind_front(afterGetSwitchPowerCappingModeAssociation, resp,
                        switchId, handler));
}

inline void afterPatchSwitchPowerCappingModeGetDbusObject(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& dbusValue, const std::string& objectPath,
    const std::string& service, const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec || object.empty())
    {
        messages::internalError(resp->res);
        return;
    }
    for (const auto& [serv, _] : object)
    {
        if (serv != service)
        {
            continue;
        }
        nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
            resp, std::chrono::seconds(60), service, objectPath,
            std::string(powerCappingModeIntf), "PendingMode",
            std::variant<std::string>(dbusValue),
            nvidia_async_operation_utils::PatchGenericCallback{resp});
        return;
    }
    messages::internalError(resp->res);
}

inline void afterPatchSwitchPowerCappingModeGetConfigurable(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& dbusValue, const std::string& objectPath,
    const std::string& service, const boost::system::error_code& ec,
    bool isModeConfigurable)
{
    if (ec)
    {
        messages::internalError(resp->res);
        return;
    }
    if (!isModeConfigurable)
    {
        messages::propertyNotWritable(resp->res, "PowerCapMode");
        return;
    }
    dbus::utility::getDbusObject(
        objectPath,
        std::array<std::string_view, 1>{
            nvidia_async_operation_utils::setAsyncInterfaceName},
        std::bind_front(afterPatchSwitchPowerCappingModeGetDbusObject, resp,
                        dbusValue, objectPath, service));
}

inline void patchSwitchPowerCappingMode(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& powerCapMode, const std::string& objectPath,
    const dbus::utility::MapperServiceMap& serviceMap)
{
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::ranges::find(interfaceList,
                              std::string(powerCappingModeIntf)) !=
            interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        messages::internalError(resp->res);
        return;
    }

    std::string dbusValue = translatePowerCapModeRedfishToDbus(powerCapMode);
    if (dbusValue.empty())
    {
        messages::propertyValueNotInList(resp->res, powerCapMode,
                                         "PowerCapMode");
        return;
    }

    dbus::utility::getProperty<bool>(
        *inventoryService, objectPath, std::string(powerCappingModeIntf),
        "IsModeConfigurable",
        std::bind_front(afterPatchSwitchPowerCappingModeGetConfigurable, resp,
                        dbusValue, objectPath, *inventoryService));
}

/**
 * Handle the PATCH operation of the L1 Power Mode Boolean Property. Do basic
 * validation of the input data, and then update using async way.
 *
 * @param[in,out]   resp            Async HTTP response.
 * @param[in]       fabricId        Fabric's Id.
 * @param[in]       switchId        Switch's Id.
 * @param[in]       propertyValue   New property value to apply.
 * @param[in]       ObjectPath      Path of object to modify.
 * @param[in]       serviceMap      Service map for CPU object.
 */
inline void patchSwitchIsolationMode(
    const std::shared_ptr<bmcweb::AsyncResp>& resp, const std::string& fabricId,
    const std::string& switchId, const std::string& propertyValue,
    const std::string& objectPath,
    const dbus::utility::MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::ranges::find(interfaceList, "com.nvidia.SwitchIsolation") !=
            interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        BMCWEB_LOG_ERROR(
            "Switch Isolation Mode interface not found while patch");
        messages::internalError(resp->res);
        return;
    }

    dbus::utility::getDbusObject(
        objectPath,
        std::array<std::string_view, 1>{
            nvidia_async_operation_utils::setAsyncInterfaceName},
        [resp, propertyValue, fabricId, switchId, objectPath,
         service =
             *inventoryService](const boost::system::error_code& ec,
                                const dbus::utility::MapperGetObject& object) {
            if (!ec)
            {
                for (const auto& [serv, _] : object)
                {
                    if (serv != service)
                    {
                        continue;
                    }

                    BMCWEB_LOG_DEBUG(
                        "Performing Patch using Set Async Method Call for Switch Isolation Mode");

                    nvidia_async_operation_utils::
                        doGenericSetAsyncAndGatherResult(
                            resp, std::chrono::seconds(60), service, objectPath,
                            "com.nvidia.SwitchIsolation", "IsolationMode",
                            std::variant<std::string>(propertyValue),
                            nvidia_async_operation_utils::
                                PatchIsolationModeCallback{resp});

                    return;
                }
            }
            else
            {
                messages::internalError(resp->res);
                return;
            }
        });
}

inline void getSwitchIsolationMode(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& serv, const std::string& objPath,
    const std::string& interface)
{
    dbus::utility::getAllProperties(
        serv, objPath, interface,
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(asyncResp->res);
                return;
            }
            nlohmann::json& json = asyncResp->res.jsonValue;
            for (const auto& property : properties)
            {
                if (property.first == "IsolationMode")
                {
                    const std::string* isolationMode =
                        std::get_if<std::string>(&property.second);
                    if (isolationMode == nullptr)
                    {
                        messages::internalError(asyncResp->res);
                        BMCWEB_LOG_ERROR("Invalid Data Type");
                        return;
                    }
                    auto itr = (*isolationMode).find_last_of('.');
                    json["Oem"]["Nvidia"]["SwitchIsolationMode"] =
                        (itr != std::string::npos)
                            ? (*isolationMode).substr(itr + 1)
                            : "Unknown";
                }
            }
        });
}

inline void getFabricManagerState(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& serv, const std::string& objPath,
    const std::string& interface)
{
    dbus::utility::getAllProperties(
        serv, objPath, interface,
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(asyncResp->res);
                return;
            }
            for (const auto& property : properties)
            {
                if (property.first == "FMState")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for FM state");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res
                        .jsonValue["Oem"]["Nvidia"]["FabricManager"]["State"] =
                        redfish::nvidia_manager_util::getFMState(*value);
                }
                else if (property.first == "ReportStatus")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for Report Status");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["FabricManager"]
                                            ["ReportStatus"] =
                        redfish::nvidia_manager_util::getFMReportStatus(*value);
                }
                else if (property.first == "LastRestartDuration")
                {
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for Duration Since LastRestart");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res
                        .jsonValue["Oem"]["Nvidia"]["FabricManager"]
                                  ["DurationSinceLastRestartSeconds"] = *value;
                }
                else if (property.first == "LastRestartTime")
                {
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for Time Since LastRestart");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["FabricManager"]
                                            ["LastResetTime"] =
                        redfish::time_utils::getDateTimeUint(*value);
                }
            }
        });
}

inline void getSwitchHistogramLink(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& switchURI, const std::string& objectPath)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objectPath + "/histograms",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, switchURI](const boost::system::error_code& ec,
                               const std::vector<std::string>& /*resp*/) {
            if (ec)
            {
                // no associated histograms = no failure
                BMCWEB_LOG_DEBUG("No associated histograms on {}", switchURI);
                return;
            }

            std::string switchHistogramURI = switchURI;
            switchHistogramURI += "/Oem/Nvidia/Histograms";
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaSwitch.v1_6_0.NvidiaSwitch";
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["Histograms"]["@odata.id"] =
                switchHistogramURI;
        });
}

// LTX mode D-Bus interface and enum prefix constants.
constexpr std::string_view ltxModeIntf = "com.nvidia.DeviceMode.LTXMode";
constexpr std::string_view ltxModeEnumPrefix =
    "com.nvidia.DeviceMode.LTXMode.LinkTrainingExtendedMode.";

// Convert a D-Bus LTX mode enum string to a Redfish string.
// Returns nullopt on any unrecognised value.
inline std::optional<std::string> ltxModeDbusToRedfish(
    std::string_view dbusValue)
{
    if (!dbusValue.starts_with(ltxModeEnumPrefix))
    {
        return std::nullopt;
    }
    dbusValue.remove_prefix(ltxModeEnumPrefix.size());
    if (dbusValue != "Default" && dbusValue != "Enabled" &&
        dbusValue != "Disabled")
    {
        return std::nullopt;
    }
    return std::string(dbusValue);
}

// Convert a D-Bus LTX mode enum string for the active resource.
// Returns nullopt for Default (firmware contract violation on active resource)
// or any unrecognised value.
inline std::optional<std::string> ltxModeActiveDbusToRedfish(
    std::string_view dbusValue)
{
    std::optional<std::string> redfishValue = ltxModeDbusToRedfish(dbusValue);
    if (!redfishValue || *redfishValue == "Default")
    {
        return std::nullopt;
    }
    return redfishValue;
}

// Convert a Redfish LTX mode string to a D-Bus enum string.
// Returns nullopt on any unrecognised Redfish value.
inline std::optional<std::string> ltxModeRedfishToDbus(
    std::string_view redfishValue)
{
    if (redfishValue != "Default" && redfishValue != "Enabled" &&
        redfishValue != "Disabled")
    {
        return std::nullopt;
    }
    return std::string(ltxModeEnumPrefix).append(redfishValue);
}

// Callback: populate the active LTXMode resource from a GetAll result.
inline void afterUpdateSwitchLTXModeData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& switchURI, const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error for updateSwitchLTXModeData()");
        messages::internalError(asyncResp->res);
        return;
    }
    for (const auto& [propertyName, propertyValue] : properties)
    {
        if (propertyName != "CurrentMode")
        {
            continue;
        }
        const std::string* value = std::get_if<std::string>(&propertyValue);
        if (value == nullptr)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        std::optional<std::string> redfishValue =
            ltxModeActiveDbusToRedfish(*value);
        if (!redfishValue)
        {
            BMCWEB_LOG_ERROR("Unexpected CurrentMode on active LTXMode");
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["LTXMode"] = *redfishValue;
    }
    std::string settingsURI = switchURI + "/Oem/Nvidia/LTXMode/Settings";
    asyncResp->res.jsonValue["@Redfish.Settings"]["@odata.type"] =
        "#Settings.v1_3_3.Settings";
    asyncResp->res
        .jsonValue["@Redfish.Settings"]["SettingsObject"]["@odata.id"] =
        settingsURI;
    std::string resetTarget =
        switchURI +
        "/Oem/Nvidia/LTXMode/Actions/NvidiaSwitchLTXMode.ResetToDefaults";
    asyncResp->res.jsonValue["Actions"]["#NvidiaSwitchLTXMode.ResetToDefaults"]
                            ["target"] = resetTarget;
}

// Fetch all properties from the LTX mode object and populate the active
// resource.
inline void updateSwitchLTXModeData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& ltxObjPath,
    const std::string& switchURI)
{
    dbus::utility::getAllProperties(
        service, ltxObjPath, std::string(ltxModeIntf),
        std::bind_front(afterUpdateSwitchLTXModeData, asyncResp, switchURI));
}

// Callback: populate the Settings resource from a GetAll result.
// NOTE: No @Redfish.SettingsApplyTime — LTX mode applies on link toggle,
// not device reset.
inline void afterUpdateSwitchLTXModeSettingsData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error for LTXMode Settings");
        messages::internalError(asyncResp->res);
        return;
    }
    for (const auto& [propertyName, propertyValue] : properties)
    {
        if (propertyName != "PendingMode")
        {
            continue;
        }
        const std::string* value = std::get_if<std::string>(&propertyValue);
        if (value == nullptr)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        // If PendingMode is still Default (firmware resolving), skip the
        // property — it will be populated on next poll after the device
        // reports a concrete value.
        if (*value == std::string(ltxModeEnumPrefix) + "Default")
        {
            return;
        }
        std::optional<std::string> redfishValue =
            ltxModeActiveDbusToRedfish(*value);
        if (!redfishValue)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["LTXMode"] = *redfishValue;
    }
}

// Fetch all properties from the LTX mode object and populate the Settings
// resource. No @Redfish.SettingsApplyTime is added.
inline void updateSwitchLTXModeSettingsData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& ltxObjPath)
{
    dbus::utility::getAllProperties(
        service, ltxObjPath, std::string(ltxModeIntf),
        std::bind_front(afterUpdateSwitchLTXModeSettingsData, asyncResp));
}

// Callback: after checking the /ltx_mode association, populate the Switch
// OEM LTXMode navigation link if the association exists.
inline void afterGetSwitchLTXModeLink(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& switchURI, const boost::system::error_code& ec,
    const std::vector<std::string>& endpoints)
{
    if (ec || endpoints.empty())
    {
        return;
    }
    std::string uri = switchURI + "/Oem/Nvidia/LTXMode";
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
        "#NvidiaSwitch.v1_6_0.NvidiaSwitch";
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["LTXMode"]["@odata.id"] = uri;
}

// Probe the /ltx_mode D-Bus association on the switch object. If present,
// populate the OEM LTXMode link in the Switch GET response.
inline void getSwitchLTXModeLink(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath, const std::string& switchURI)
{
    dbus::utility::findAssociations(
        objectPath + "/ltx_mode",
        std::bind_front(afterGetSwitchLTXModeLink, asyncResp, switchURI));
}

// Function type used by the object-finder helpers below.
using LTXModeObjectHandler =
    std::function<void(const std::string&, const std::string&,
                       const dbus::utility::MapperGetObject&)>;

// Callback: after GetObject for the LTX mode D-Bus object.
inline void afterGetSwitchLTXModeDbusObject(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& ltxObjPath, const LTXModeObjectHandler& handler,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec || object.empty())
    {
        messages::internalError(resp->res);
        return;
    }
    handler(object.front().first, ltxObjPath, object);
}

// Callback: after reading the /ltx_mode association endpoints.
inline void afterGetSwitchLTXModeAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& resp, const std::string& switchId,
    const LTXModeObjectHandler& handler, const boost::system::error_code& ec,
    const std::vector<std::string>& endpoints)
{
    if (ec || endpoints.empty())
    {
        messages::resourceNotFound(resp->res, "LTXMode", switchId);
        return;
    }
    const std::string& ltxObjPath = endpoints.front();
    dbus::utility::getDbusObject(
        ltxObjPath, std::array<std::string_view, 1>{ltxModeIntf},
        std::bind_front(afterGetSwitchLTXModeDbusObject, resp, ltxObjPath,
                        handler));
}

// Find the LTX mode D-Bus object for a switch via the /ltx_mode association,
// then call handler(service, objectPath, objectMap).
inline void getSwitchLTXModeObject(
    const std::shared_ptr<bmcweb::AsyncResp>& resp, const std::string& switchId,
    const std::string& switchObjPath, const LTXModeObjectHandler& handler)
{
    dbus::utility::findAssociations(
        switchObjPath + "/ltx_mode",
        std::bind_front(afterGetSwitchLTXModeAssociation, resp, switchId,
                        handler));
}

// Callback: after GetObject for the async-set interface — perform the D-Bus
// Set on PendingMode.
inline void afterPatchSwitchLTXModeGetDbusObject(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& dbusValue, const std::string& objectPath,
    const std::string& service, const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec || object.empty())
    {
        messages::internalError(resp->res);
        return;
    }
    for (const auto& [serv, _] : object)
    {
        if (serv != service)
        {
            continue;
        }
        nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
            resp, std::chrono::seconds(60), service, objectPath,
            std::string(ltxModeIntf), "PendingMode",
            std::variant<std::string>(dbusValue),
            nvidia_async_operation_utils::PatchGenericCallback{resp});
        return;
    }
    messages::internalError(resp->res);
}

// Callback: after reading IsModeConfigurable — gate the write.
inline void afterPatchSwitchLTXModeGetConfigurable(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& dbusValue, const std::string& objectPath,
    const std::string& service, const boost::system::error_code& ec,
    bool isModeConfigurable)
{
    if (ec)
    {
        messages::internalError(resp->res);
        return;
    }
    if (!isModeConfigurable)
    {
        messages::propertyNotWritable(resp->res, "LTXMode");
        return;
    }
    dbus::utility::getDbusObject(
        objectPath,
        std::array<std::string_view, 1>{
            nvidia_async_operation_utils::setAsyncInterfaceName},
        std::bind_front(afterPatchSwitchLTXModeGetDbusObject, resp, dbusValue,
                        objectPath, service));
}

// Validate the requested mode, check IsModeConfigurable, then issue an async
// D-Bus Set on PendingMode.
inline void patchSwitchLTXMode(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                               const std::string& ltxMode,
                               const std::string& objectPath,
                               const dbus::utility::MapperGetObject& serviceMap)
{
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::ranges::find(interfaceList, std::string(ltxModeIntf)) !=
            interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        messages::internalError(resp->res);
        return;
    }

    auto dbusValue = ltxModeRedfishToDbus(ltxMode);
    if (!dbusValue)
    {
        messages::propertyValueNotInList(resp->res, ltxMode, "LTXMode");
        return;
    }

    dbus::utility::getProperty<bool>(
        *inventoryService, objectPath, std::string(ltxModeIntf),
        "IsModeConfigurable",
        std::bind_front(afterPatchSwitchLTXModeGetConfigurable, resp,
                        *dbusValue, objectPath, *inventoryService));
}

// UPhy recovery mode D-Bus interface and enum prefix constants.
// NOTE: These map to the phosphor-dbus-interfaces com.nvidia.DeviceMode
// interface and enum names as merged (MR !1641) and are intentionally NOT
// renamed to match the Redfish-facing "UPhyRecoveryMode" naming below.
constexpr std::string_view uphyModeIntf =
    "com.nvidia.DeviceMode.UPhyRecoveryMode";
constexpr std::string_view uphyModeEnumPrefix =
    "com.nvidia.DeviceMode.UPhyRecoveryMode.UPhyMode.";

// Convert a D-Bus UPhy recovery mode enum string to a Redfish string.
// Returns nullopt on any unrecognised value.
inline std::optional<std::string> uphyRecoveryModeDbusToRedfish(
    std::string_view dbusValue)
{
    if (!dbusValue.starts_with(uphyModeEnumPrefix))
    {
        return std::nullopt;
    }
    dbusValue.remove_prefix(uphyModeEnumPrefix.size());
    if (dbusValue != "Default" && dbusValue != "Enabled" &&
        dbusValue != "Disabled")
    {
        return std::nullopt;
    }
    return std::string(dbusValue);
}

// Convert a D-Bus UPhy recovery mode enum string for the active resource.
// Returns nullopt for Default (firmware contract violation on active resource)
// or any unrecognised value.
inline std::optional<std::string> uphyRecoveryModeActiveDbusToRedfish(
    std::string_view dbusValue)
{
    std::optional<std::string> redfishValue =
        uphyRecoveryModeDbusToRedfish(dbusValue);
    if (!redfishValue || *redfishValue == "Default")
    {
        return std::nullopt;
    }
    return redfishValue;
}

// Convert a Redfish UPhy recovery mode string to a D-Bus enum string.
// Returns nullopt on any unrecognised Redfish value.
inline std::optional<std::string> uphyRecoveryModeRedfishToDbus(
    std::string_view redfishValue)
{
    if (redfishValue != "Default" && redfishValue != "Enabled" &&
        redfishValue != "Disabled")
    {
        return std::nullopt;
    }
    return std::string(uphyModeEnumPrefix).append(redfishValue);
}

// Callback: populate the active UPhyRecoveryMode resource from a GetAll
// result.
inline void afterUpdateSwitchUPhyRecoveryModeData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& switchURI, const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "DBUS response error for updateSwitchUPhyRecoveryModeData()");
        messages::internalError(asyncResp->res);
        return;
    }
    bool foundCurrentMode = false;
    for (const auto& [propertyName, propertyValue] : properties)
    {
        if (propertyName != "CurrentMode")
        {
            continue;
        }
        const std::string* value = std::get_if<std::string>(&propertyValue);
        if (value == nullptr)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        std::optional<std::string> redfishValue =
            uphyRecoveryModeActiveDbusToRedfish(*value);
        if (!redfishValue)
        {
            BMCWEB_LOG_ERROR(
                "Unexpected CurrentMode on active UPhyRecoveryMode");
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["UPhyRecoveryMode"] = *redfishValue;
        foundCurrentMode = true;
    }
    if (!foundCurrentMode)
    {
        BMCWEB_LOG_ERROR(
            "CurrentMode not found in UPhyRecoveryMode GetAll response");
        messages::internalError(asyncResp->res);
        return;
    }
    std::string settingsURI =
        switchURI + "/Oem/Nvidia/UPhyRecoveryMode/Settings";
    asyncResp->res.jsonValue["@Redfish.Settings"]["@odata.type"] =
        "#Settings.v1_3_3.Settings";
    asyncResp->res
        .jsonValue["@Redfish.Settings"]["SettingsObject"]["@odata.id"] =
        settingsURI;
    std::string resetTarget =
        switchURI + "/Oem/Nvidia/UPhyRecoveryMode/Actions/"
                    "NvidiaSwitchUPhyRecoveryMode.ResetToDefaults";
    asyncResp->res
        .jsonValue["Actions"]["#NvidiaSwitchUPhyRecoveryMode.ResetToDefaults"]
                  ["target"] = resetTarget;
}

// Fetch all properties from the UPhy recovery mode object and populate the
// active resource.
inline void updateSwitchUPhyRecoveryModeData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& uphyObjPath,
    const std::string& switchURI)
{
    dbus::utility::getAllProperties(
        service, uphyObjPath, std::string(uphyModeIntf),
        std::bind_front(afterUpdateSwitchUPhyRecoveryModeData, asyncResp,
                        switchURI));
}

// Callback: populate the Settings resource from a GetAll result.
// NOTE: No @Redfish.SettingsApplyTime — UPhy recovery mode applies on link
// toggle, not device reset.
inline void afterUpdateSwitchUPhyRecoveryModeSettingsData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error for UPhyRecoveryMode Settings");
        messages::internalError(asyncResp->res);
        return;
    }
    for (const auto& [propertyName, propertyValue] : properties)
    {
        if (propertyName != "PendingMode")
        {
            continue;
        }
        const std::string* value = std::get_if<std::string>(&propertyValue);
        if (value == nullptr)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        // If PendingMode is still Default (firmware resolving), skip the
        // property — it will be populated on next poll after the device
        // reports a concrete value.
        if (*value == std::string(uphyModeEnumPrefix) + "Default")
        {
            return;
        }
        std::optional<std::string> redfishValue =
            uphyRecoveryModeActiveDbusToRedfish(*value);
        if (!redfishValue)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["UPhyRecoveryMode"] = *redfishValue;
    }
}

// Fetch all properties from the UPhy recovery mode object and populate the
// Settings resource. No @Redfish.SettingsApplyTime is added.
inline void updateSwitchUPhyRecoveryModeSettingsData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& uphyObjPath)
{
    dbus::utility::getAllProperties(
        service, uphyObjPath, std::string(uphyModeIntf),
        std::bind_front(afterUpdateSwitchUPhyRecoveryModeSettingsData,
                        asyncResp));
}

// Callback: after checking the /uphy_mode association, populate the Switch
// OEM UPhyRecoveryMode navigation link if the association exists.
inline void afterGetSwitchUPhyRecoveryModeLink(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& switchURI, const boost::system::error_code& ec,
    const std::vector<std::string>& endpoints)
{
    if (ec || endpoints.empty())
    {
        return;
    }
    std::string uri = switchURI + "/Oem/Nvidia/UPhyRecoveryMode";
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
        "#NvidiaSwitch.v1_6_0.NvidiaSwitch";
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["UPhyRecoveryMode"]["@odata.id"] =
        uri;
}

// Probe the /uphy_mode D-Bus association on the switch object. If present,
// populate the OEM UPhyRecoveryMode link in the Switch GET response.
inline void getSwitchUPhyRecoveryModeLink(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath, const std::string& switchURI)
{
    dbus::utility::findAssociations(
        objectPath + "/uphy_mode",
        std::bind_front(afterGetSwitchUPhyRecoveryModeLink, asyncResp,
                        switchURI));
}

// Function type used by the object-finder helpers below.
using UPhyRecoveryModeObjectHandler =
    std::function<void(const std::string&, const std::string&,
                       const dbus::utility::MapperGetObject&)>;

// Callback: after GetObject for the UPhy recovery mode D-Bus object.
inline void afterGetSwitchUPhyRecoveryModeDbusObject(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& uphyObjPath,
    const UPhyRecoveryModeObjectHandler& handler,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec || object.empty())
    {
        messages::internalError(resp->res);
        return;
    }
    handler(object.front().first, uphyObjPath, object);
}

// Callback: after reading the /uphy_mode association endpoints.
inline void afterGetSwitchUPhyRecoveryModeAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& resp, const std::string& switchId,
    const UPhyRecoveryModeObjectHandler& handler,
    const boost::system::error_code& ec,
    const std::vector<std::string>& endpoints)
{
    if (ec || endpoints.empty())
    {
        messages::resourceNotFound(resp->res, "UPhyRecoveryMode", switchId);
        return;
    }
    const std::string& uphyObjPath = endpoints.front();
    dbus::utility::getDbusObject(
        uphyObjPath, std::array<std::string_view, 1>{uphyModeIntf},
        std::bind_front(afterGetSwitchUPhyRecoveryModeDbusObject, resp,
                        uphyObjPath, handler));
}

// Find the UPhy recovery mode D-Bus object for a switch via the /uphy_mode
// association, then call handler(service, objectPath, objectMap).
inline void getSwitchUPhyRecoveryModeObject(
    const std::shared_ptr<bmcweb::AsyncResp>& resp, const std::string& switchId,
    const std::string& switchObjPath,
    const UPhyRecoveryModeObjectHandler& handler)
{
    dbus::utility::findAssociations(
        switchObjPath + "/uphy_mode",
        std::bind_front(afterGetSwitchUPhyRecoveryModeAssociation, resp,
                        switchId, handler));
}

// Callback: after GetObject for the async-set interface — perform the D-Bus
// Set on PendingMode.
inline void afterPatchSwitchUPhyRecoveryModeGetDbusObject(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& dbusValue, const std::string& objectPath,
    const std::string& service, const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec || object.empty())
    {
        messages::internalError(resp->res);
        return;
    }
    for (const auto& [serv, _] : object)
    {
        if (serv != service)
        {
            continue;
        }
        nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
            resp, std::chrono::seconds(60), service, objectPath,
            std::string(uphyModeIntf), "PendingMode",
            std::variant<std::string>(dbusValue),
            nvidia_async_operation_utils::PatchGenericCallback{resp});
        return;
    }
    messages::internalError(resp->res);
}

// Callback: after reading IsModeConfigurable — gate the write.
inline void afterPatchSwitchUPhyRecoveryModeGetConfigurable(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& dbusValue, const std::string& objectPath,
    const std::string& service, const boost::system::error_code& ec,
    bool isModeConfigurable)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("DBUS response error for UPhyRecoveryMode Settings");
        messages::internalError(resp->res);
        return;
    }
    if (!isModeConfigurable)
    {
        BMCWEB_LOG_DEBUG("UPhyRecoveryMode is not configurable");
        messages::propertyNotWritable(resp->res, "UPhyRecoveryMode");
        return;
    }
    dbus::utility::getDbusObject(
        objectPath,
        std::array<std::string_view, 1>{
            nvidia_async_operation_utils::setAsyncInterfaceName},
        std::bind_front(afterPatchSwitchUPhyRecoveryModeGetDbusObject, resp,
                        dbusValue, objectPath, service));
}

// Validate the requested mode, check IsModeConfigurable, then issue an async
// D-Bus Set on PendingMode.
inline void patchSwitchUPhyRecoveryMode(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& uphyRecoveryMode, const std::string& objectPath,
    const dbus::utility::MapperGetObject& serviceMap)
{
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::ranges::find(interfaceList, std::string(uphyModeIntf)) !=
            interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        BMCWEB_LOG_DEBUG(
            "UPhyRecoveryMode D-Bus interface not found on any service");
        messages::internalError(resp->res);
        return;
    }

    auto dbusValue = uphyRecoveryModeRedfishToDbus(uphyRecoveryMode);
    if (!dbusValue)
    {
        BMCWEB_LOG_DEBUG("Invalid UPhyRecoveryMode value");
        messages::propertyValueNotInList(resp->res, uphyRecoveryMode,
                                         "UPhyRecoveryMode");
        return;
    }

    dbus::utility::getProperty<bool>(
        *inventoryService, objectPath, std::string(uphyModeIntf),
        "IsModeConfigurable",
        std::bind_front(afterPatchSwitchUPhyRecoveryModeGetConfigurable, resp,
                        *dbusValue, objectPath, *inventoryService));
}

} // namespace nvidia_fabric_utils
} // namespace redfish
