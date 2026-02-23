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
 * @brief Callback to handle ImageCopyPolicy property retrieval result
 *
 * @param asyncResp Pointer to object holding response data
 * @param objectPath D-Bus object path being queried
 * @param ec Error code from the D-Bus property get call
 * @param propertyValue The ImageCopyPolicy property value
 */
inline void getImageCopyPolicyCallback(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath, const boost::system::error_code& ec,
    const std::string& propertyValue)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "Failed to get the ImageCopyPolicy property for object path '{}': {}",
            objectPath, ec.message());
        redfish::messages::resourceErrorsDetectedFormatError(
            asyncResp->res, objectPath, ec.message());
        return;
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

    if (propertyValue ==
        "com.nvidia.ImageCopyPolicy.ImageCopyPolicyState.Automatic")
    {
        oem["AutomaticBackgroundCopyEnabled"] = true;
    }

    if (propertyValue ==
        "com.nvidia.ImageCopyPolicy.ImageCopyPolicyState.Manual")
    {
        oem["AutomaticBackgroundCopyEnabled"] = false;
    }
}

/**
 *@brief Populates the AutomaticBackgroundCopyEnabled property based on
 * ImageCopyPolicy
 *
 * @param asyncResp Pointer to object holding response data
 * @param chassisId chassisId
 *
 * @return None.
 */
inline void populateBackgroundCopyPolicy(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    constexpr std::string_view chassisDBusPath =
        "/xyz/openbmc_project/inventory/system/chassis/";
    sdbusplus::message::object_path objectPath{std::string(chassisDBusPath)};
    objectPath /= chassisId;

    dbus::utility::getDbusObject(
        objectPath,
        std::array<std::string_view, 1>{"com.nvidia.ImageCopyPolicy"},
        [asyncResp, objectPath](const boost::system::error_code& ec,
                                const dbus::utility::MapperGetObject& object) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG(
                    "The D-Bus object that implements the ImageCopyPolicy interface at object path '{}' does not exist",
                    objectPath.str);
                return;
            }

            const std::string& service = object.front().first;

            dbus::utility::getProperty<std::string>(
                service, objectPath, "com.nvidia.ImageCopyPolicy",
                "ImageCopyPolicy",
                std::bind_front(getImageCopyPolicyCallback, asyncResp,
                                objectPath.str));
        });
}

/**
 *@brief Updates the background copy policy (Automatic or Manual)
 *
 * @param asyncResp Pointer to object holding response data
 * @param enabled Enable (Automatic) or disable (Manual) the background copy
 * @param chassisID Chassis Id
 *
 * @return None.
 */
inline void updateBackgroundCopyPolicy(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, bool enabled,
    const std::string& chassisId)
{
    constexpr std::string_view chassisDBusPath =
        "/xyz/openbmc_project/inventory/system/chassis/";
    sdbusplus::message::object_path objectPath{std::string(chassisDBusPath)};
    objectPath /= chassisId;

    dbus::utility::getDbusObject(
        objectPath,
        std::array<std::string_view, 1>{"com.nvidia.ImageCopyPolicy"},
        [asyncResp, chassisId, objectPath,
         enabled](const boost::system::error_code& ec,
                  const dbus::utility::MapperGetObject& object) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "The D-Bus object that implements the ImageCopyPolicy interface at object path '{}' does not exist",
                    objectPath.str);
                redfish::messages::resourceErrorsDetectedFormatError(
                    asyncResp->res, "/redfish/v1/Chassis/" + chassisId,
                    ec.message());
                return;
            }

            const std::string& service = object.front().first;

            std::string policyValue =
                enabled
                    ? "com.nvidia.ImageCopyPolicy.ImageCopyPolicyState.Automatic"
                    : "com.nvidia.ImageCopyPolicy.ImageCopyPolicyState.Manual";

            BMCWEB_LOG_DEBUG(
                "Updating ImageCopyPolicy for object path '{}' to '{}'",
                objectPath.str, policyValue);

            redfish::nvidia_async_operation_utils::
                doGenericSetAsyncAndGatherResult(
                    asyncResp, std::chrono::seconds(60), service, objectPath,
                    "com.nvidia.ImageCopyPolicy", "ImageCopyPolicy",
                    std::variant<std::string>(policyValue),
                    redfish::nvidia_async_operation_utils::PatchGenericCallback{
                        asyncResp});
        });
}
