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
#include "logging.hpp"

#include <boost/container/flat_map.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace redfish
{

namespace nvidia_power_smoothing_utils
{
inline void getPowerSmoothingPresetProfileParameters(
    const std::string& processorId,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::async_method_call(
        [processorId, asyncResp](
            const boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, boost::container::flat_map<
                                 std::string, std::vector<std::string>>>&
                subtree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                return;
            }
            for (const auto& [path, _] : subtree)
            {
                if (!path.ends_with(processorId))
                {
                    continue;
                }

                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper", path + "/power_profile",
                    "xyz.openbmc_project.Association", "endpoints",
                    [asyncResp,
                     processorId](const boost::system::error_code ec2,
                                  const std::vector<std::string>& resp) {
                        if (ec2)
                        {
                            return; // no processors = no failures
                        }

                        nlohmann::json& parameters =
                            asyncResp->res.jsonValue["Parameters"];
                        nlohmann::json param = nlohmann::json::object();
                        param["Name"] = "ProfileId";
                        param["Required"] = true;
                        param["MinimumValue"] = 0;
                        param["MaximumValue"] = resp.size();
                        param["DataType"] = "Number";
                        parameters.push_back(param);
                    });
                return;
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 3>{
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "xyz.openbmc_project.Inventory.Item.Cpu",
            "com.nvidia.PowerSmoothing.PowerSmoothing"});
}
} // namespace nvidia_power_smoothing_utils
} // namespace redfish
