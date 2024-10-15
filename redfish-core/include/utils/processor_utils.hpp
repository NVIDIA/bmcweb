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

namespace redfish
{

namespace processor_utils
{
using OperatingConfigProperties =
    std::vector<std::pair<std::string, dbus::utility::DbusVariantType>>;

// Map of service name to list of interfaces
using MapperServiceMap =
    std::vector<std::pair<std::string, std::vector<std::string>>>;

// Map of object paths to MapperServiceMaps
using MapperGetSubTreeResponse =
    std::vector<std::pair<std::string, MapperServiceMap>>;

// Interfaces which imply a D-Bus object represents a Processor
constexpr std::array<const char*, 2> processorInterfaces = {
    "xyz.openbmc_project.Inventory.Item.Cpu",
    "xyz.openbmc_project.Inventory.Item.Accelerator"};

/**
 * Find the D-Bus object representing the requested Processor, and call the
 * handler with the results. If matching object is not found, add 404 error to
 * response and don't call the handler.
 *
 * @param[in,out]   resp            Async HTTP response.
 * @param[in]       processorId     Redfish Processor Id.
 * @param[in]       handler         Callback to continue processing request upon
 *                                  successfully finding object.
 */
template <typename Handler>
inline void getProcessorObject(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                               const std::string& processorId,
                               Handler&& handler)
{
    BMCWEB_LOG_DEBUG("Get available system processor resources.");

    // GetSubTree on all interfaces which provide info about a Processor
    crow::connections::systemBus->async_method_call(
        [resp, processorId, handler = std::forward<Handler>(handler)](
            boost::system::error_code ec,
            const MapperGetSubTreeResponse& subtree) mutable {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error: {}", ec);
            messages::internalError(resp->res);
            return;
        }

        bool isFoundProcessorObject = false;
        for (const auto& [objectPath, serviceMap] : subtree)
        {
            // Ignore any objects which don't end with our desired cpu name
            if (!objectPath.ends_with(processorId))
            {
                continue;
            }

            bool found = false;
            std::string deviceType = "";
            // Filter out objects that don't have the CPU-specific
            // interfaces to make sure we can return 404 on non-CPUs
            // (e.g. /redfish/../Processors/dimm0)
            for (const auto& [serviceName, interfaceList] : serviceMap)
            {
                for (const auto& iface : processorInterfaces)
                {
                    auto it = std::find(interfaceList.begin(),
                                        interfaceList.end(), iface);
                    if (it != interfaceList.end())
                    {
                        deviceType = *it;
                        found = true;
                        break;
                    }
                }

                if (found)
                    break;
            }

            if (!found)
            {
                continue;
            }

            // Process the first object which does match our cpu name and
            // required interfaces, and potentially ignore any other
            // matching objects. Assume all interfaces we want to process
            // must be on the same object path.

            handler(resp, processorId, objectPath, serviceMap, deviceType);

            isFoundProcessorObject = true;
            // need to check all objpath because a few configs are set by
            // another service
        }
        if (isFoundProcessorObject == false)
        {
            messages::resourceNotFound(resp->res, "Processor", processorId);
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 9>{
            "xyz.openbmc_project.Common.UUID",
            "xyz.openbmc_project.Inventory.Decorator.Asset",
            "xyz.openbmc_project.Inventory.Decorator.Revision",
            "xyz.openbmc_project.Inventory.Item.Cpu",
            "xyz.openbmc_project.Inventory.Decorator.LocationCode",
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "xyz.openbmc_project.Software.Version",
            "xyz.openbmc_project.Control.Processor.CurrentOperatingConfig",
            "xyz.openbmc_project.Inventory.Decorator.UniqueIdentifier"});
}

inline void getPCIeErrorData(std::shared_ptr<bmcweb::AsyncResp> aResp,
                             const std::string& service,
                             const std::string& objPath)
{
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}](const boost::system::error_code ec,
                                  const OperatingConfigProperties& properties) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error");
            messages::internalError(aResp->res);
            return;
        }

        for (const auto& property : properties)
        {
            if (property.first == "ceCount")
            {
                const int64_t* value = std::get_if<int64_t>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["PCIeErrors"]["CorrectableErrorCount"] =
                    *value;
            }
            else if (property.first == "nonfeCount")
            {
                const int64_t* value = std::get_if<int64_t>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["PCIeErrors"]["NonFatalErrorCount"] =
                    *value;
            }
            else if (property.first == "feCount")
            {
                const int64_t* value = std::get_if<int64_t>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["PCIeErrors"]["FatalErrorCount"] = *value;
            }
            else if (property.first == "L0ToRecoveryCount")
            {
                const int64_t* value = std::get_if<int64_t>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["PCIeErrors"]["L0ToRecoveryCount"] =
                    *value;
            }
            else if (property.first == "ReplayCount")
            {
                const int64_t* value = std::get_if<int64_t>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["PCIeErrors"]["ReplayCount"] = *value;
            }
            else if (property.first == "ReplayRolloverCount")
            {
                const int64_t* value = std::get_if<int64_t>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["PCIeErrors"]["ReplayRolloverCount"] =
                    *value;
            }
            else if (property.first == "NAKSentCount")
            {
                const int64_t* value = std::get_if<int64_t>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["PCIeErrors"]["NAKSentCount"] = *value;
            }
            else if (property.first == "NAKReceivedCount")
            {
                const int64_t* value = std::get_if<int64_t>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["PCIeErrors"]["NAKReceivedCount"] = *value;
            }
            else if (property.first == "UnsupportedRequestCount")
            {
                const int64_t* value = std::get_if<int64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Invalid Data Type");
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["PCIeErrors"]["UnsupportedRequestCount"] =
                    *value;
            }
        }
    },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.PCIe.PCIeECC");
}

} // namespace processor_utils
} // namespace redfish
