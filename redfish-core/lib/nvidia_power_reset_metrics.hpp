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
#include "utils/dbus_utils.hpp"
#include "utils/hex_utils.hpp"
#include "utils/json_utils.hpp"

#include <boost/container/flat_map.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/conditions_utils.hpp>
#include <utils/systemd_utils.hpp>

#include <array>
#include <string_view>

namespace redfish
{

inline std::string getLastResetType(const std::string& resetType)
{
    if (resetType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.ResetTypes.PFFLR")
    {
        return "PFFLR";
    }
    if (resetType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.ResetTypes.Conventional")
    {
        return "Conventional";
    }
    if (resetType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.ResetTypes.Fundamental")
    {
        return "Fundamental";
    }
    if (resetType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.ResetTypes.IRoTReset")
    {
        return "IRoTReset";
    }

    // Unknown or unsupported reset type
    return "Unknown";
}

/**
 * @brief Fill out pcie interface properties by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out] asyncResp       Async HTTP response.
 * @param[in]     objPath         D-Bus object to query.
 */
inline void getResetMetricsInterfaceProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get ResetMetrics interface properties for path: {}",
                     objPath);

    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath](
            const boost::system::error_code errorCode,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
        if (errorCode)
        {
            BMCWEB_LOG_ERROR("Failed to get object info for path {}: {}",
                             objPath, errorCode.message());
            messages::internalError(asyncResp->res);
            return;
        }

        // Find the service implementing ResetCounterMetrics
        std::string targetService;
        for (const auto& [service, interfaces] : objInfo)
        {
            if (std::find(interfaces.begin(), interfaces.end(),
                          "com.nvidia.ResetCounters.ResetCounterMetrics") !=
                interfaces.end())
            {
                targetService = service;
                break;
            }
        }

        if (targetService.empty())
        {
            BMCWEB_LOG_ERROR(
                "No service implements ResetCounterMetrics at path {}",
                objPath);
            messages::internalError(asyncResp->res);
            return;
        }

        // Fetch properties for the identified service
        sdbusplus::asio::getAllProperties(
            *crow::connections::systemBus, targetService, objPath,
            "com.nvidia.ResetCounters.ResetCounterMetrics",
            [asyncResp,
             objPath](const boost::system::error_code ec,
                      const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Failed to fetch properties for path {}: {}",
                                 objPath, ec.message());
                messages::internalError(asyncResp->res);
                return;
            }

            // Map of D-Bus property keys to Redfish JSON keys
            static const std::unordered_map<std::string, std::string>
                propertyKeyMap = {
                    {"PF_FLR_ResetEntryCount", "PF_FLR_ResetEntryCount"},
                    {"PF_FLR_ResetExitCount", "PF_FLR_ResetExitCount"},
                    {"ConventionalResetEntryCount",
                     "ConventionalResetEntryCount"},
                    {"ConventionalResetExitCount",
                     "ConventionalResetExitCount"},
                    {"FundamentalResetEntryCount",
                     "FundamentalResetEntryCount"},
                    {"FundamentalResetExitCount", "FundamentalResetExitCount"},
                    {"IRoTResetExitCount", "IRoTResetExitCount"},
                    {"LastResetType", "LastResetType"}};

            // Populate Redfish JSON response with properties
            for (const auto& [key, value] : properties)
            {
                try
                {
                    auto jsonKey = propertyKeyMap.find(key);
                    if (jsonKey == propertyKeyMap.end())
                    {
                        BMCWEB_LOG_WARNING("Unknown property key: {}", key);
                        continue;
                    }

                    if (const double* count = std::get_if<double>(&value))
                    {
                        asyncResp->res.jsonValue[jsonKey->second] = *count;
                    }
                    else if (const std::string* resetType =
                                 std::get_if<std::string>(&value))
                    {
                        asyncResp->res.jsonValue[jsonKey->second] =
                            getLastResetType(*resetType);
                    }
                    else
                    {
                        BMCWEB_LOG_WARNING(
                            "Unsupported property type for key: {}", key);
                    }
                }
                catch (const std::exception& e)
                {
                    BMCWEB_LOG_ERROR("Error parsing property {}: {}", key,
                                     e.what());
                    messages::internalError(asyncResp->res);
                }
            }
        });
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
        std::array<const char*, 1>{
            "com.nvidia.ResetCounters.ResetCounterMetrics"});
}

inline void
    getProcessorResetMetricsData(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                 const std::string& processorId)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    crow::connections::systemBus->async_method_call(
        [processorId, aResp{std::move(aResp)}](
            const boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, boost::container::flat_map<
                                 std::string, std::vector<std::string>>>&
                subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error");
            messages::internalError(aResp->res);

            return;
        }
        for (const auto& [path, object] : subtree)
        {
            if (!boost::ends_with(path, processorId))
            {
                continue;
            }

            crow::connections::systemBus->async_method_call(
                [aResp, processorId](
                    const boost::system::error_code& ec,
                    const std::variant<std::vector<std::string>>& resp) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "Failed to get ResetMetrics association endpoints: {}",
                        ec.message());
                    messages::internalError(aResp->res);
                    // No associated ResetMetrics found
                    return;
                }

                const std::vector<std::string>* data =
                    std::get_if<std::vector<std::string>>(&resp);
                if (data == nullptr || data->empty())
                {
                    BMCWEB_LOG_INFO(
                        "No associated ResetMetrics found for processor: {}",
                        processorId);
                    return;
                }

                std::string resetMetricsURI =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/Processors/";
                resetMetricsURI += processorId;
                resetMetricsURI += "/Oem/Nvidia/ProcessorResetMetrics";
                aResp->res.jsonValue["@odata.type"] =
                    "#NvidiaProcessorResetMetrics.v1_0_0."
                    "NvidiaProcessorResetMetrics";
                aResp->res.jsonValue["@odata.id"] = resetMetricsURI;
                aResp->res.jsonValue["Id"] = "ProcessorResetMetrics";
                aResp->res.jsonValue["Name"] = processorId +
                                               " Processor Reset Metrics";

                for (const std::string& linkPath : *data)
                {
                    getResetMetricsInterfaceProperties(aResp, linkPath);
                }
            },
                "xyz.openbmc_project.ObjectMapper", path + "/reset_statistics",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Association", "endpoints");
            return;
        }
        // Object not found
        messages::resourceNotFound(aResp->res,
                                   "#Processor not found: ", processorId);
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Item.Accelerator"});
}

inline void requestRoutesProcessorResetMetrics(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Processors/<str>/Oem/Nvidia/ProcessorResetMetrics/")
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
        getProcessorResetMetricsData(asyncResp, processorId);
    });
}

} // namespace redfish
