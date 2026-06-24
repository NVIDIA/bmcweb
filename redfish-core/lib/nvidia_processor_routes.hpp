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

#include "bmcweb_config.h" // For BMCWEB_DISABLE_CONDITIONS_ARRAY

#include "app.hpp"
#include "async_resp.hpp"
#include "nvidia_processor.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/hex_utils.hpp"
#include "utils/json_utils.hpp"
namespace redfish
{

inline void requestRoutesProcessorMetrics(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/Processors/<str>/ProcessorMetrics")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncRespOuter,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorIdOuter) {
                if (!redfish::setUpRedfishRoute(app, req, asyncRespOuter))
                {
                    return;
                }
                redfish::nvidia_processor::getProcessorMetricsData(
                    asyncRespOuter, processorIdOuter);
            });
}

inline void requestRoutesProcessorMemoryMetrics(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Processors/<str>/MemorySummary/MemoryMetrics")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncRespOuter,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorIdOuter) {
                if (!redfish::setUpRedfishRoute(app, req, asyncRespOuter))
                {
                    return;
                }
                redfish::nvidia_processor::getProcessorMemoryMetricsData(
                    asyncRespOuter, processorIdOuter);
            });
}

inline void requestRoutesProcessorSettings(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Settings")
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
                redfish::nvidia_processor::getProcessorSettingsData(
                    asyncResp, processorId);
            });

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Settings")
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
            std::optional<nlohmann::json> memSummary;
            std::optional<nlohmann::json> oemObject;
            if (!redfish::json_util::readJsonAction(
                    req, asyncResp->res, "MemorySummary", memSummary, "Oem",
                    oemObject))
            {
                return;
            }
            std::optional<bool> eccModeEnabled;
            if (memSummary)
            {
                if (redfish::json_util::readJson(*memSummary, asyncResp->res,
                                                 "ECCModeEnabled",
                                                 eccModeEnabled))
                {
                    redfish::processor_utils::getProcessorObject(
                        asyncResp, processorId,
                        [eccModeEnabled](
                            const std::shared_ptr<bmcweb::AsyncResp>&
                                asyncResp1,
                            const std::string& processorId1,
                            const std::string& objectPath,
                            const MapperServiceMap& serviceMap,
                            [[maybe_unused]] const std::string& deviceType) {
                            redfish::nvidia_processor::patchEccMode(
                                asyncResp1, processorId1, *eccModeEnabled,
                                objectPath, serviceMap);
                        });
                }
            }
            if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
            {
                // Update ccMode
                std::optional<nlohmann::json> oemNvidiaObject;

                if (oemObject &&
                    redfish::json_util::readJson(*oemObject, asyncResp->res,
                                                 "Nvidia", oemNvidiaObject))
                {
                    std::optional<bool> ccMode;
                    std::optional<bool> ccDevMode;
                    std::optional<bool> egmMode;
                    if (oemNvidiaObject &&
                        redfish::json_util::readJson(
                            *oemNvidiaObject, asyncResp->res, "CCModeEnabled",
                            ccMode, "CCDevModeEnabled", ccDevMode,
                            "EGMModeEnabled", egmMode))
                    {
                        if (ccMode && ccDevMode)
                        {
                            messages::queryCombinationInvalid(asyncResp->res);
                            return;
                        }

                        if (ccMode)
                        {
                            redfish::processor_utils::getProcessorObject(
                                asyncResp, processorId,
                                [ccMode](
                                    const std::shared_ptr<bmcweb::AsyncResp>&
                                        asyncResp1,
                                    const std::string& processorId1,
                                    const std::string& objectPath,
                                    const MapperServiceMap& serviceMap,
                                    [[maybe_unused]] const std::string&
                                        deviceType) {
                                    redfish::nvidia_processor_utils::
                                        patchCCMode(asyncResp1, processorId1,
                                                    *ccMode, objectPath,
                                                    serviceMap);
                                });
                        }
                        if (ccDevMode)
                        {
                            redfish::processor_utils::getProcessorObject(
                                asyncResp, processorId,
                                [ccDevMode](
                                    const std::shared_ptr<bmcweb::AsyncResp>&
                                        asyncResp1,
                                    const std::string& processorId1,
                                    const std::string& objectPath,
                                    const MapperServiceMap& serviceMap,
                                    [[maybe_unused]] const std::string&
                                        deviceType) {
                                    redfish::nvidia_processor_utils::
                                        patchCCDevMode(asyncResp1, processorId1,
                                                       *ccDevMode, objectPath,
                                                       serviceMap);
                                });
                        }
                        if (egmMode)
                        {
                            redfish::processor_utils::getProcessorObject(
                                asyncResp, processorId,
                                [egmMode](
                                    const std::shared_ptr<bmcweb::AsyncResp>&
                                        asyncResp1,
                                    const std::string& processorId1,
                                    const std::string& objectPath,
                                    const MapperServiceMap& serviceMap,
                                    [[maybe_unused]] const std::string&
                                        deviceType) {
                                    redfish::nvidia_processor_utils::
                                        patchEgmMode(asyncResp1, processorId1,
                                                     *egmMode, objectPath,
                                                     serviceMap);
                                });
                        }
                    }
                }
            }
        });
}

inline void requestRoutesProcessorReset(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Actions/Processor.Reset")
        .privileges(redfish::privileges::postProcessor)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::optional<std::string> resetType;
                if (!json_util::readJsonAction(req, asyncResp->res, "ResetType",
                                               resetType))
                {
                    return;
                }
                if (resetType)
                {
                    redfish::processor_utils::getProcessorObject(
                        asyncResp, processorId,
                        [resetType](
                            const std::shared_ptr<bmcweb::AsyncResp>&
                                asyncResp1,
                            const std::string& processorId1,
                            const std::string& objectPath,
                            const MapperServiceMap& serviceMap,
                            [[maybe_unused]] const std::string& deviceType) {
                            redfish::nvidia_processor::postResetType(
                                asyncResp1, processorId1, objectPath,
                                *resetType, serviceMap);
                        });
                }
            });
}
} // namespace redfish
