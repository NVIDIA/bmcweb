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

#include "error_messages.hpp"
#include "nvidia_dbus_utility.hpp"
#include "nvidia_error_messages.hpp"
#include "utils/nvidia_async_set_callbacks.hpp"

/**
 *@brief Updates InbandUpdatePolicyEnabled property
 *
 * @param asyncResp - Pointer to object holding response data
 * @param allowListMap - List containing allowable chassis Ids
 * @param chassisId - chassisId
 * @param[in] callback - A callback function to be called after update
 * InbandUpdatePolicyEnabled property
 *
 * @return None
 */
inline void updateInBandEnabled(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const auto& allowListMap, const std::string& chassisId,
    const std::function<void()>& callback = {})
{
    if (std::find(allowListMap.begin(), allowListMap.end(), chassisId) ==
        allowListMap.end())
    {
        if (callback)
        {
            callback();
        }
        return;
    }

    constexpr std::string_view chassisDbusPath =
        "/xyz/openbmc_project/inventory/system/chassis/";
    std::string objectPath = std::string(chassisDbusPath) + chassisId;

    dbus::utility::getProperty<std::string>(
        "xyz.openbmc_project.NSM", objectPath, "com.nvidia.InbandUpdatePolicy",
        "InbandUpdatePolicy",
        [asyncResp, callback](const boost::system::error_code& ec,
                              const std::string& propertyValue) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("error_code = {}", ec);
                redfish::messages::internalError(asyncResp->res);

                if (callback)
                {
                    callback();
                }
                return;
            }

            if (!asyncResp->res.jsonValue.contains("Oem"))
            {
                asyncResp->res.jsonValue["Oem"] = nlohmann::json::object();
            }
            if (!asyncResp->res.jsonValue["Oem"].contains("Nvidia"))
            {
                asyncResp->res.jsonValue["Oem"]["Nvidia"] =
                    nlohmann::json::object();
            }

            nlohmann::json& oem = asyncResp->res.jsonValue["Oem"]["Nvidia"];

            if (propertyValue ==
                "com.nvidia.InbandUpdatePolicy.InbandPolicyState.Enabled")
            {
                oem["InbandUpdatePolicyEnabled"] = true;
            }

            if (propertyValue ==
                "com.nvidia.InbandUpdatePolicy.InbandPolicyState.Disabled")
            {
                oem["InbandUpdatePolicyEnabled"] = false;
            }

            if (callback)
            {
                callback();
            }
        });
}

/**
 *@brief Enable or Disable in-band update policy
 *
 * @param asyncResp - Pointer to object holding response data
 * @param enabled Enable or disable the in-band
 * @param chassisId - chassis Id
 *
 * @return None
 */
inline void enableInBand(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         bool enabled, const std::string& chassisId)
{
    constexpr std::string_view chassisDbusPath =
        "/xyz/openbmc_project/inventory/system/chassis/";
    std::string objectPath = std::string(chassisDbusPath) + chassisId;

    dbus::utility::getDbusObject(
        objectPath,
        std::array<std::string_view, 1>{"com.nvidia.InbandUpdatePolicy"},
        [asyncResp, chassisId, objectPath,
         enabled](const boost::system::error_code& ec,
                  const dbus::utility::MapperGetObject& object) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "The D-Bus object that implements the InbandUpdatePolicy interface at object path '{}' does not exist",
                    objectPath);
                redfish::messages::resourceErrorsDetectedFormatError(
                    asyncResp->res, "/redfish/v1/Chassis/" + chassisId,
                    ec.message());
                return;
            }

            const std::string& service = object.front().first;

            std::string policyValue =
                enabled
                    ? "com.nvidia.InbandUpdatePolicy.InbandPolicyState.Enabled"
                    : "com.nvidia.InbandUpdatePolicy.InbandPolicyState.Disabled";

            BMCWEB_LOG_DEBUG(
                "Updating InbandUpdatePolicy for object path '{}' to '{}'",
                objectPath, policyValue);

            redfish::nvidia_async_operation_utils::
                doGenericSetAsyncAndGatherResult(
                    asyncResp, std::chrono::seconds(10), service, objectPath,
                    "com.nvidia.InbandUpdatePolicy", "InbandUpdatePolicy",
                    std::variant<std::string>(policyValue),
                    redfish::nvidia_async_operation_utils::PatchGenericCallback{
                        asyncResp});
        });
}
