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

#include <atomic>
#include <cstdint>
namespace redfish
{

namespace nvidia_fabric_utils
{
// Map of service name to list of interfaces
using MapperServiceMap =
    std::vector<std::pair<std::string, std::vector<std::string>>>;

// Map of object paths to MapperServiceMaps
using MapperGetSubTreeResponse =
    std::vector<std::pair<std::string, MapperServiceMap>>;

/**
 * Handle the PATCH operation of the L1 Power Mode Boolean Property. Do basic
 * validation of the input data, and then update using async way.
 *
 * @param[in,out]   resp            Async HTTP response.
 * @param[in]       fabricId        Fabric's Id.
 * @param[in]       switchId        Switch's Id.
 * @param[in]       propertyValue   New property value to apply.
 * @param[in]       propertyName    Property name for new value
 * @param[in]       ObjectPath      Path of object to modify.
 * @param[in]       serviceMap      Service map for CPU object.
 */
inline void patchL1PowerModeBool(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                                 const std::string& fabricId,
                                 const std::string& switchId,
                                 const bool propertyValue,
                                 const std::string& propertyName,
                                 const std::string& objectPath,
                                 const MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::find(interfaceList.begin(), interfaceList.end(),
                      "com.nvidia.PowerMode") != interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        BMCWEB_LOG_ERROR(" L1PowerMode interface not found while {} patch",
                         propertyName);
        messages::internalError(resp->res);
        return;
    }

    dbus::utility::getDbusObject(
        objectPath,
        std::array<std::string_view, 1>{
            nvidia_async_operation_utils::setAsyncInterfaceName},
        [resp, propertyValue, propertyName, fabricId, switchId, objectPath,
         service = *inventoryService](
            const boost::system::error_code& ec,
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
                    "Performing Patch using Set Async Method Call for {}",
                    propertyName);

                nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
                    resp, std::chrono::seconds(60), service, objectPath,
                    "com.nvidia.PowerMode", propertyName,
                    std::variant<bool>(propertyValue),
                    nvidia_async_operation_utils::PatchPowerModeCallback{resp});

                return;
            }
        }
    });
}

/**
 * Handle the PATCH operation of the L1 Power Mode int Property. Do basic
 * validation of the input data, and then update using async way.
 *
 * @param[in,out]   resp            Async HTTP response.
 * @param[in]       fabricId        Fabric's Id.
 * @param[in]       switchId        Switch's Id.
 * @param[in]       propertyValue   New property value to apply.
 * @param[in]       propertyName    Property name for new value
 * @param[in]       ObjectPath      Path of object to modify.
 * @param[in]       serviceMap      Service map for CPU object.
 */
inline void patchL1PowerModeInt(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                                const std::string& fabricId,
                                const std::string& switchId,
                                const uint32_t propertyValue,
                                const std::string& propertyName,
                                const std::string& objectPath,
                                const MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::find(interfaceList.begin(), interfaceList.end(),
                      "com.nvidia.PowerMode") != interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        BMCWEB_LOG_ERROR(" L1PowerMode interface not found while {} patch",
                         propertyName);
        messages::internalError(resp->res);
        return;
    }

    dbus::utility::getDbusObject(
        objectPath,
        std::array<std::string_view, 1>{
            nvidia_async_operation_utils::setAsyncInterfaceName},
        [resp, propertyValue, propertyName, fabricId, switchId, objectPath,
         service = *inventoryService](
            const boost::system::error_code& ec,
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
                    "Performing Patch using Set Async Method Call for {}",
                    propertyName);

                nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
                    resp, std::chrono::seconds(60), service, objectPath,
                    "com.nvidia.PowerMode", propertyName,
                    std::variant<uint32_t>(propertyValue),
                    nvidia_async_operation_utils::PatchPowerModeCallback{resp});

                return;
            }
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

    crow::connections::systemBus->async_method_call(
        [resp, fabricId, switchId, handler = std::forward<Handler>(handler)](
            boost::system::error_code ec,
            const MapperGetSubTreeResponse& subtree) mutable {
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
            if (!boost::ends_with(objectPath, fabricId))
            {
                continue;
            }

            crow::connections::systemBus->async_method_call(
                [resp, fabricId, switchId,
                 handler](const boost::system::error_code ec,
                          std::variant<std::vector<std::string>>& response) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "DBUS response error: {} while getting switch", ec);
                    messages::internalError(resp->res);
                    return;
                }
                std::vector<std::string>* data =
                    std::get_if<std::vector<std::string>>(&response);
                if (data == nullptr)
                {
                    BMCWEB_LOG_ERROR(
                        "Response error data null while getting switch");
                    messages::internalError(resp->res);
                    return;
                }

                bool isFoundSwitchObject = false;
                // Iterate over all retrieved ObjectPaths.
                for (const std::string& path : *data)
                {
                    sdbusplus::message::object_path objPath(path);
                    if (objPath.filename() != switchId)
                    {
                        continue;
                    }

                    crow::connections::systemBus->async_method_call(
                        [resp, fabricId, switchId, path,
                         handler](const boost::system::error_code ec,
                                  const std::vector<std::pair<
                                      std::string, std::vector<std::string>>>&
                                      object) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR(
                                "Dbus response error while getting service name for switch");
                            messages::internalError(resp->res);
                            return;
                        }
                        handler(resp, fabricId, switchId, path, object);
                    },
                        "xyz.openbmc_project.ObjectMapper",
                        "/xyz/openbmc_project/object_mapper",
                        "xyz.openbmc_project.ObjectMapper", "GetObject", path,
                        std::array<const char*, 0>());
                    isFoundSwitchObject = true;
                }
                if (isFoundSwitchObject == false)
                {
                    messages::resourceNotFound(resp->res, "Switch", switchId);
                }
            },
                "xyz.openbmc_project.ObjectMapper",
                objectPath + "/all_switches", "org.freedesktop.DBus.Properties",
                "Get", "xyz.openbmc_project.Association", "endpoints");

            isFoundFabricObject = true;
        }
        if (isFoundFabricObject == false)
        {
            messages::resourceNotFound(resp->res, "Fabric", fabricId);
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Item.Fabric"});
}

/**
 * Populate the ErrorInjection path if interface exists. Do basic
 * validation of the input data, and then update using async way.
 *
 * @param[in,out]   resp            Async HTTP response.
 * @param[in]       fabricId        Fabric's Id.
 * @param[in]       switchId        Switch's Id.
 */
inline void
    populateErrorInjectionData(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                               const std::string& fabricId,
                               const std::string& switchId)
{
    getSwitchObject(resp, fabricId, switchId,
                    [](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                       const std::string& fabricId, const std::string& switchId,
                       const std::string& path,
                       [[maybe_unused]] const MapperServiceMap& serviceMap) {
        crow::connections::systemBus->async_method_call(
            [aResp, fabricId, switchId,
             path](const boost::system::error_code ec,
                   const MapperServiceMap& serviceMap) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("ErrorInjection object not found in {}", path);
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
                    "#NvidiaSwitch.v1_2_0.NvidiaSwitch";
                aResp->res.jsonValue["Oem"]["Nvidia"]["ErrorInjection"] = {
                    {"@odata.id", "/redfish/v1/Fabrics/" + fabricId +
                                      "/Switches/" + switchId +
                                      "/Oem/Nvidia/ErrorInjection"}};
                return;
            }
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetObject",
            path + "/ErrorInjection", std::array<const char*, 0>());
    });
}

inline void updateSwitchPowerModeData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Switch Power mode Data");
    // using PropertyType = std::variant<bool, uint8_t, uint16_t, uint32_t,
    // size_t>;
    using PropertiesMap =
        boost::container::flat_map<std::string, dbus::utility::DbusVariantType>;
    // Get interface properties
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath](const boost::system::error_code ec,
                             const PropertiesMap& properties) {
        if (ec)
        {
            messages::internalError(asyncResp->res);
            return;
        }

        for (const auto& property : properties)
        {
            const std::string& propertyName = property.first;
            if (propertyName == "HWModeControl")
            {
                const bool* value = std::get_if<bool>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for L1 HW Mode Control");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["L1HWModeEnabled"] = *value;
            }
            if (propertyName == "FWThrottlingMode")
            {
                const bool* value = std::get_if<bool>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for L1 FW Throttling Mode");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["L1FWThermalThrottlingModeEnabled"] =
                    *value;
            }
            if (propertyName == "PredictionMode")
            {
                const bool* value = std::get_if<bool>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for L1 Prediction Mode");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["L1PredictionModeEnabled"] = *value;
            }
            else if (propertyName == "HWThreshold")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for L1 HW Threshold Bytes");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["L1HWThresholdBytes"] = *value;
            }
            else if (propertyName == "HWActiveTime")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for L1 HW Active Time");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["L1HWActiveTimeMicroseconds"] = *value;
            }
            else if (propertyName == "HWInactiveTime")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for L1 HW Inctive Time");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["L1HWInactiveTimeMicroseconds"] =
                    *value;
            }
            else if (propertyName == "HWPredictionInactiveTime")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for L1 HW Prediction Inactive Time");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res
                    .jsonValue["L1PredictionInactiveTimeMicroseconds"] = *value;
            }
        }
    },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll", "");
}

inline void
    getSwitchPowerModeLink(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::vector<std::string>& interfaces,
                           const std::string& switchURI)
{
    const std::string powerModeInterface = "com.nvidia.PowerMode";
    if (std::find(interfaces.begin(), interfaces.end(), powerModeInterface) !=
        interfaces.end())
    {
        std::string switchPowerModeURI = switchURI;
        switchPowerModeURI += "/Oem/Nvidia/PowerMode";
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
            "#NvidiaSwitch.v1_2_0.NvidiaSwitch";
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["PowerMode"]["@odata.id"] =
            switchPowerModeURI;
        return;
    }
}

} // namespace nvidia_fabric_utils
} // namespace redfish
