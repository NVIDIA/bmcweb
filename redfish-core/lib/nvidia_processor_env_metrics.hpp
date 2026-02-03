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
#include "error_messages.hpp"
#include "http_request.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "str_utility.hpp"
#include "utils/chassis_utils.hpp"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/url/format.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/environment_util.hpp>
#include <utils/json_utils.hpp>
#include <utils/processor_utils.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace redfish
{
// Map of service name to list of interfaces
using MapperServiceMap =
    std::vector<std::pair<std::string, std::vector<std::string>>>;
inline void requestRoutesProcessorEnvironmentMetrics(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/Processors/<str>/EnvironmentMetrics")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::string envMetricsURI =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/Processors/";
                envMetricsURI += processorId;
                envMetricsURI += "/EnvironmentMetrics";
                asyncResp->res.jsonValue["@odata.type"] =
                    "#EnvironmentMetrics.v1_3_0.EnvironmentMetrics";
                asyncResp->res.jsonValue["@odata.id"] = envMetricsURI;
                asyncResp->res.jsonValue["Id"] = "Environment Metrics";
                asyncResp->res.jsonValue["Name"] =
                    processorId + " Environment Metrics";

                redfish::nvidia_env_utils::getProcessorEnvironmentMetricsData(
                    asyncResp, processorId);
            });

    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/Processors/<str>/EnvironmentMetrics")
        .privileges(redfish::privileges::patchProcessor)
        .methods(
            boost::beast::http::verb::
                patch)([&app](
                           const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           [[maybe_unused]] const std::string& systemName,
                           const std::string& processorId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            std::optional<nlohmann::json> powerLimit;
            std::optional<nlohmann::json> oemObject;
            std::optional<bool> powerLimitPersistency;

            // Read json request
            if (!json_util::readJsonAction(req, asyncResp->res,
                                           "PowerLimitWatts", powerLimit, "Oem",
                                           oemObject))
            {
                return;
            }
            if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
            {
                // update Edpp Setpoint
                if (std::optional<nlohmann::json> oemNvidiaObject;
                    oemObject && json_util::readJson(*oemObject, asyncResp->res,
                                                     "Nvidia", oemNvidiaObject))
                {
                    if (std::optional<nlohmann::json> edppObject,
                        basePowerWatts;
                        oemNvidiaObject &&
                        json_util::readJson(
                            *oemNvidiaObject, asyncResp->res, "EDPpPercent",
                            edppObject, "PowerLimitPersistency",
                            powerLimitPersistency, "GPUBasePowerWatts",
                            basePowerWatts))
                    {
                        if (edppObject)
                        {
                            std::optional<size_t> setPoint;
                            std::optional<bool> persistency;

                            if (!json_util::readJson(
                                    *edppObject, asyncResp->res, "SetPoint",
                                    setPoint, "Persistency", persistency))
                            {
                                BMCWEB_LOG_ERROR(
                                    "Cannot read values from Edpp tag");
                                return;
                            }

                            if (setPoint && persistency)
                            {
                                redfish::processor_utils::getProcessorObject(
                                    asyncResp, processorId,
                                    [setPoint, persistency](
                                        const std::shared_ptr<
                                            bmcweb::AsyncResp>& lambdaAsyncResp,
                                        const std::string& lambdaProcessorId,
                                        const std::string& objectPath,
                                        const MapperServiceMap& serviceMap,
                                        [[maybe_unused]] const std::string&
                                            deviceType) {
                                        redfish::nvidia_env_utils::
                                            patchEdppSetPoint(
                                                lambdaAsyncResp,
                                                lambdaProcessorId, *setPoint,
                                                *persistency, objectPath,
                                                serviceMap);
                                    });
                            }
                            else if (setPoint)
                            {
                                redfish::processor_utils::getProcessorObject(
                                    asyncResp, processorId,
                                    [setPoint](
                                        const std::shared_ptr<
                                            bmcweb::AsyncResp>& lambdaAsyncResp,
                                        const std::string& lambdaProcessorId,
                                        const std::string& objectPath,
                                        const MapperServiceMap& serviceMap,
                                        [[maybe_unused]] const std::string&
                                            deviceType) {
                                        redfish::nvidia_env_utils::
                                            patchEdppSetPoint(
                                                lambdaAsyncResp,
                                                lambdaProcessorId, *setPoint,
                                                false, objectPath, serviceMap);
                                    });
                            }
                        }
                        if (basePowerWatts)
                        {
                            std::optional<uint32_t> basePowerWattsValue;
                            std::optional<bool> persistency;
                            if (json_util::readJson(*basePowerWatts,
                                                    asyncResp->res, "SetPoint",
                                                    basePowerWattsValue,
                                                    "Persistency", persistency))
                            {
                                redfish::nvidia_env_utils::patchBasePowerWatts(
                                    asyncResp, processorId,
                                    *basePowerWattsValue,
                                    persistency.value_or(false));
                            }
                            else
                            {
                                BMCWEB_LOG_ERROR(
                                    "Cannot read values from BasePowerWatts tag");
                                messages::internalError(asyncResp->res);
                                return;
                            }
                        }
                    }
                }
            }

            // Update power limit
            if (powerLimit)
            {
                std::optional<int> setPoint;
                if (json_util::readJson(*powerLimit, asyncResp->res, "SetPoint",
                                        setPoint))
                {
                    const std::array<const char*, 2> interfacesList = {
                        "xyz.openbmc_project.Inventory.Item.Cpu",
                        "xyz.openbmc_project.Inventory.Item.Accelerator"};

                    bool persistency = false;
                    if (powerLimitPersistency)
                    {
                        persistency = *powerLimitPersistency;
                    }

                    crow::connections::systemBus->async_method_call(
                        [asyncResp, processorId, setPoint, persistency](
                            const boost::system::error_code& ec,
                            const dbus::utility::GetSubTreeType& subtree) {
                            if (ec)
                            {
                                messages::internalError(asyncResp->res);
                                return;
                            }

                            // Iterate over all retrieved ObjectPaths.
                            for (const std::pair<
                                     std::string,
                                     std::vector<
                                         std::pair<std::string,
                                                   std::vector<std::string>>>>&
                                     object : subtree)
                            {
                                const std::string& path = object.first;
                                const std::vector<std::pair<
                                    std::string, std::vector<std::string>>>&
                                    connectionNames = object.second;

                                sdbusplus::message::object_path objPath(path);
                                if (objPath.filename() != processorId)
                                {
                                    continue;
                                }

                                if (connectionNames.empty())
                                {
                                    BMCWEB_LOG_ERROR("Got 0 Connection names");
                                    continue;
                                }
                                const std::vector<std::string>&
                                    lambdaInterfaces =
                                        connectionNames[0].second;

                                if (std::find(
                                        lambdaInterfaces.begin(),
                                        lambdaInterfaces.end(),
                                        "xyz.openbmc_project.Inventory.Item.Accelerator") !=
                                    lambdaInterfaces.end())
                                {
                                    std::string resourceType = "Processors";
                                    redfish::nvidia_env_utils::patchPowerLimit(
                                        asyncResp, processorId, *setPoint,
                                        objPath, resourceType, persistency);
                                }
                                else if (
                                    std::find(
                                        lambdaInterfaces.begin(),
                                        lambdaInterfaces.end(),
                                        "xyz.openbmc_project.Inventory.Item.Cpu") !=
                                    lambdaInterfaces.end())
                                {
                                    crow::connections::systemBus
                                        ->async_method_call(
                                            [asyncResp, processorId, setPoint](
                                                const boost::system::error_code&
                                                    ec1,
                                                std::variant<std::vector<
                                                    std::string>>& resp) {
                                                if (ec1)
                                                {
                                                    messages::internalError(
                                                        asyncResp->res);
                                                    return;
                                                }
                                                std::vector<std::string>* data =
                                                    std::get_if<std::vector<
                                                        std::string>>(&resp);
                                                if (data == nullptr)
                                                {
                                                    return;
                                                }
                                                for (const std::string&
                                                         ctrlPath : *data)
                                                {
                                                    std::string resourceType =
                                                        "Cpu";
                                                    redfish::nvidia_env_utils::
                                                        patchPowerLimit(
                                                            asyncResp,
                                                            processorId,
                                                            *setPoint, ctrlPath,
                                                            resourceType);
                                                }
                                            },
                                            "xyz.openbmc_project.ObjectMapper",
                                            path + "/power_controls",
                                            "org.freedesktop.DBus.Properties",
                                            "Get",
                                            "xyz.openbmc_project.Association",
                                            "endpoints");
                                    return;
                                }
                                return;
                            }

                            messages::resourceNotFound(
                                asyncResp->res, "#Processor.v1_20_0.Processor",
                                processorId);
                        },
                        "xyz.openbmc_project.ObjectMapper",
                        "/xyz/openbmc_project/object_mapper",
                        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                        "/xyz/openbmc_project/inventory", 0, interfacesList);
                }
            }
        });
}

inline void requestRoutesEdppReset(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/Processors/<str>/"
            "EnvironmentMetrics/Actions/Oem/NvidiaEnvironmentMetrics.ResetEDPp")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& processorId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                redfish::processor_utils::getProcessorObject(
                    asyncResp, processorId,
                    [](const std::shared_ptr<bmcweb::AsyncResp>&
                           lambdaAsyncResp,
                       const std::string& lambdaProcessorId,
                       const std::string& objectPath,
                       const MapperServiceMap& serviceMap,
                       [[maybe_unused]] const std::string& deviceType) {
                        redfish::nvidia_env_utils::postEdppReset(
                            lambdaAsyncResp, lambdaProcessorId, objectPath,
                            serviceMap);
                    });
            });
}

inline void requestRoutesProcessorEnvironmentMetricsClearOOBSetPoint(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Processors/<str>/"
        "EnvironmentMetrics/Actions/Oem/NvidiaEnvironmentMetrics.ClearOOBSetPoint")

        .privileges({{"ConfigureComponents"}})
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                const std::array<const char*, 1> interfaces = {
                    "com.nvidia.Common.ClearPowerCap"};

                crow::connections::systemBus->async_method_call(
                    [asyncResp, processorId](
                        const boost::system::error_code& ec,
                        const dbus::utility::GetSubTreeType& subtree) {
                        if (ec)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        // Iterate over all retrieved ObjectPaths.
                        for (const std::pair<
                                 std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                                 object : subtree)
                        {
                            const std::string& path = object.first;
                            const std::vector<std::pair<
                                std::string, std::vector<std::string>>>&
                                connectionNames = object.second;

                            sdbusplus::message::object_path objPath(path);
                            if (objPath.filename() != processorId)
                            {
                                continue;
                            }

                            if (connectionNames.empty())
                            {
                                BMCWEB_LOG_ERROR("Got 0 Connection names");
                                continue;
                            }

                            const std::string& connectionName =
                                connectionNames[0].first;
                            const std::vector<std::string>& lambdaInterfaces =
                                connectionNames[0].second;

                            if (std::find(lambdaInterfaces.begin(),
                                          lambdaInterfaces.end(),
                                          "com.nvidia.Common.ClearPowerCap") !=
                                lambdaInterfaces.end())
                            {
                                redfish::chassis_utils::resetPowerLimit(
                                    asyncResp, objPath, connectionName);
                            }

                            return;
                        }

                        messages::resourceNotFound(
                            asyncResp->res, "#Processor.v1_20_0.Processor",
                            processorId);
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                    "/xyz/openbmc_project/inventory", 0, interfaces);
            });
}

} // namespace redfish
