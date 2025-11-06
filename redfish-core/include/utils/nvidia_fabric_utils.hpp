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
#include "utils/nvidia_async_call_utils.hpp"
#include "utils/nvidia_async_set_callbacks.hpp"
#include "utils/nvidia_async_set_utils.hpp"
#include "utils/nvidia_manager_utils.hpp"

#include <boost/container/flat_map.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
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
                                if (std::find(
                                        interfaceList.begin(),
                                        interfaceList.end(),
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
                            sdbusplus::message::object_path objPath(path);
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
                        if (std::find(
                                interfaces.begin(), interfaces.end(),
                                "com.nvidia.ErrorInjection.ErrorInjection") ==
                            interfaces.end())
                        {
                            continue;
                        }
                        aResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                            "#NvidiaSwitch.v1_4_0.NvidiaSwitch";
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
                sdbusplus::asio::getAllProperties(
                    *crow::connections::systemBus, service, path,
                    "xyz.openbmc_project.Object.Enable",
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
                "#NvidiaSwitch.v1_4_0.NvidiaSwitch";
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["PowerMode"]["@odata.id"] =
                switchPowerModeURI;
        });
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
        if (std::find(interfaceList.begin(), interfaceList.end(),
                      "com.nvidia.SwitchIsolation") != interfaceList.end())
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
                "#NvidiaSwitch.v1_4_0.NvidiaSwitch";
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["Histograms"]["@odata.id"] =
                switchHistogramURI;
        });
}

} // namespace nvidia_fabric_utils
} // namespace redfish
