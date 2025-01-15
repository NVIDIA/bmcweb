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
            aResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaNetworkAdapter.v1_0_0.NvidiaNetworkAdapter";
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

} // namespace nvidia_network_adapters_utils
} // namespace redfish
