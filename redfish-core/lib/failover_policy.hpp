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
#include "nvidia_error_messages.hpp"
#include "utils/nvidia_async_set_callbacks.hpp"

#include <sdbusplus/message/native_types.hpp>

#include <functional>

/**
 * @brief Callback to handle FailoverPolicy property retrieval result
 *
 * @param asyncResp Pointer to object holding response data
 * @param objectPath D-Bus object path being queried
 * @param ec Error code from the D-Bus property get call
 * @param propertyValue The FailoverPolicy property value
 */
inline void getFailoverPolicyCallback(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath, const boost::system::error_code& ec,
    const std::string& propertyValue)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "Failed to get the FailoverPolicy property for object path '{}': {}",
            objectPath, ec);
        redfish::messages::resourceErrorsDetectedFormatError(
            asyncResp->res, objectPath, ec.message());
        return;
    }

    // Extract the last part after the last '.'
    // e.g., "com.nvidia.FailoverPolicy.FailoverPolicyState.AutomaticFailover"
    // should become "AutomaticFailover"
    std::string failoverPolicy = propertyValue;
    size_t lastDot = propertyValue.rfind('.');
    if (lastDot != std::string::npos)
    {
        failoverPolicy = propertyValue.substr(lastDot + 1);
    }

    if (!asyncResp->res.jsonValue.contains("Oem"))
    {
        asyncResp->res.jsonValue["Oem"] = nlohmann::json::object();
    }
    if (!asyncResp->res.jsonValue["Oem"].contains("Nvidia"))
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"] = nlohmann::json::object();
    }

    nlohmann::json& oem = asyncResp->res.jsonValue["Oem"]["Nvidia"];

    // When D-Bus value is Unknown, show null in Redfish (same as BuildType)
    if (failoverPolicy == "NoFailover" || failoverPolicy == "AutomaticFailover")
    {
        oem["FailoverPolicy"] = failoverPolicy;
    }
    else
    {
        oem["FailoverPolicy"] = nullptr;
    }
}

/**
 *@brief Updates the failover policy (NoFailover or AutomaticFailover)
 *
 * @param asyncResp Pointer to object holding response data
 * @param failoverPolicy Failover policy value (NoFailover or AutomaticFailover)
 * @param chassisId Chassis Id
 *
 * @return None.
 */
inline void updateFailoverPolicy(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& failoverPolicy, const std::string& chassisId)
{
    // Validate input against allowed values
    if (failoverPolicy != "NoFailover" && failoverPolicy != "AutomaticFailover")
    {
        redfish::messages::propertyValueNotInList(
            asyncResp->res, failoverPolicy, "FailoverPolicy");
        return;
    }

    constexpr std::string_view chassisDBusPath =
        "/xyz/openbmc_project/inventory/system/chassis/";
    sdbusplus::message::object_path objectPath{std::string(chassisDBusPath)};
    objectPath /= chassisId;

    dbus::utility::getDbusObject(
        objectPath,
        std::array<std::string_view, 1>{"com.nvidia.FailoverPolicy"},
        [asyncResp, chassisId, objectPath,
         failoverPolicy](const boost::system::error_code& ec,
                         const dbus::utility::MapperGetObject& object) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "The D-Bus object that implements the FailoverPolicy interface at object path '{}' does not exist",
                    objectPath.str);
                redfish::messages::resourceErrorsDetectedFormatError(
                    asyncResp->res, "/redfish/v1/Chassis/" + chassisId,
                    ec.message());
                return;
            }

            const std::string& service = object.front().first;

            // Convert short form to full D-Bus enum path
            // e.g., "NoFailover" ->
            // "com.nvidia.FailoverPolicy.FailoverPolicyState.NoFailover"
            std::string policyValue =
                std::format("com.nvidia.FailoverPolicy.FailoverPolicyState.{}",
                            failoverPolicy);

            BMCWEB_LOG_DEBUG(
                "Updating FailoverPolicy for object path '{}' to '{}'",
                objectPath.str, policyValue);

            redfish::nvidia_async_operation_utils::
                doGenericSetAsyncAndGatherResult(
                    asyncResp, std::chrono::seconds(60), service, objectPath,
                    "com.nvidia.FailoverPolicy", "FailoverPolicy",
                    std::variant<std::string>(policyValue),
                    redfish::nvidia_async_operation_utils::PatchGenericCallback{
                        asyncResp, "FailoverPolicy", failoverPolicy});
        });
}
