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
#include "mctp_vdm_util_wrapper.hpp"
#include "nvidia_dbus_utility.hpp"
#include "nvidia_error_messages.hpp"

static const std::string chassisDbusPath =
    "/xyz/openbmc_project/inventory/system/chassis/";

static const uint16_t errorCodeUnsupportedCommand = 5;

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

    std::string objectPath = chassisDbusPath + chassisId;

    dbus::utility::getProperty<std::string>(
        *crow::connections::systemBus, "xyz.openbmc_project.NSM", objectPath,
        "com.nvidia.InbandUpdatePolicy", "InbandUpdatePolicy",
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
 *@brief Check and handle in-band update policy error code
 *
 * @param asyncResp - Pointer to object holding response data
 * @param objectPath - D-Bus object path for the chassis
 *
 * @return None
 */
inline void checkInBandUpdateError(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath)
{
    dbus::utility::getProperty<std::tuple<uint16_t, std::string>>(
        *crow::connections::systemBus, "xyz.openbmc_project.NSM", objectPath,
        "com.nvidia.InbandUpdatePolicy", "ErrorCode",
        [asyncResp](const boost::system::error_code& ec,
                    const std::tuple<uint16_t, std::string>& property) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("error_code = {}", ec);
                redfish::messages::internalError(asyncResp->res);
                return;
            }

            uint16_t errorCode = std::get<0>(property);

            if (errorCode == 0)
            {
                redfish::messages::success(asyncResp->res);
                return;
            }

            if (errorCode == errorCodeUnsupportedCommand)
            {
                std::string errorMessage = std::get<1>(property);
                redfish::messages::operationNotAllowed(asyncResp->res,
                                                       errorMessage);
                return;
            }

            redfish::messages::internalError(asyncResp->res);
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
    std::string objectPath = chassisDbusPath + chassisId;

    dbus::utility::async_method_call(
        [asyncResp, chassisId,
         objectPath](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("error_code = {}", ec);
                redfish::messages::resourceErrorsDetectedFormatError(
                    asyncResp->res, "/redfish/v1/Chassis/" + chassisId,
                    ec.message());
                return;
            }

            if (asyncResp->res.jsonValue.empty())
            {
                checkInBandUpdateError(asyncResp, objectPath);
            }
        },
        "xyz.openbmc_project.NSM", objectPath, "com.nvidia.InbandUpdatePolicy",
        "UpdatePolicy", enabled);
}
