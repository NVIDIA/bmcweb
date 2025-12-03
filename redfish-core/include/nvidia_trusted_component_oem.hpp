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

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "debug_token/base.hpp"
#include "dot/base.hpp"
#include "dot/dot_utils.hpp"

#include <nlohmann/json.hpp>
#include <sdbusplus/message.hpp>

#include <format>
#include <memory>
#include <string>

namespace redfish
{

/**
 * @brief Adds OEM properties for Unified Debug Token to TrustedComponent
 *
 * This function adds Nvidia OEM extensions to a TrustedComponent response,
 * including action URIs and debug token capabilities.
 *
 * @param asyncResp Response object to populate
 * @param chassisID Chassis identifier
 * @param componentID Component identifier (not the full D-Bus path)
 */
inline void addTrustedComponentOemProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& componentID)
{
    constexpr std::array<std::string_view, 1> interfaces = {
        debug_token::debugTokenStatusIntf};
    dbus::utility::getSubTree(
        std::string(debug_token::debugTokenBasePath), 0, interfaces,
        [asyncResp, chassisID,
         componentID](const boost::system::error_code& ec,
                      const dbus::utility::MapperGetSubTreeResponse& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG(
                    "GetSubTree for debug token status interface failed: {}",
                    ec.message());
                return;
            }

            if (resp.empty())
            {
                BMCWEB_LOG_DEBUG(
                    "No objects with debug token status interface found");
                return;
            }

            std::string targetChassisId =
                std::format("{}{}", PLATFORMDEVICEPREFIX, componentID);

            bool componentFound = false;
            for (const auto& [path, _] : resp)
            {
                std::string pathComponentId =
                    std::filesystem::path(path).filename().string();
                if (targetChassisId == pathComponentId)
                {
                    componentFound = true;
                    break;
                }
            }

            if (!componentFound)
            {
                BMCWEB_LOG_DEBUG(
                    "Debug token status interface not found for component: {}",
                    componentID);
                return;
            }

            std::string basePath =
                std::format("/redfish/v1/Chassis/{}/TrustedComponents/{}",
                            chassisID, componentID);

            nlohmann::json& actionsOem =
                asyncResp->res.jsonValue["Actions"]["Oem"];
            actionsOem["#NvidiaTrustedComponent.GenerateToken"]["target"] =
                std::format(
                    "{}/Actions/Oem/NvidiaTrustedComponent.GenerateToken",
                    basePath);

            actionsOem["#NvidiaTrustedComponent.EraseToken"]["target"] =
                std::format("{}/Actions/Oem/NvidiaTrustedComponent.EraseToken",
                            basePath);
            actionsOem["#NvidiaTrustedComponent.EraseToken"]
                      ["@Redfish.ActionInfo"] = std::format(
                          "{}/Oem/Nvidia/EraseTokenActionInfo", basePath);

            nlohmann::json& oemNvidia =
                asyncResp->res.jsonValue["Oem"]["Nvidia"];

            oemNvidia["@odata.type"] =
                "#NvidiaTrustedComponent.v1_0_0.NvidiaTrustedComponent";

            oemNvidia["InstallTokenPushURI"] =
                std::format("{}/install-token", basePath);

            oemNvidia["DebugTokens"]["@odata.id"] =
                std::format("{}/Oem/Nvidia/DebugTokens", basePath);
        });
}

/**
 * @brief Checks for DOT component support and adds DOT URI if found
 *
 * Performs D-Bus discovery for DOT components and adds the DOT URI to the
 * OEM properties if the component supports DOT operations.
 *
 * @param asyncResp Response object to populate
 * @param chassisID Chassis identifier
 * @param componentID Component identifier to match against DOT components
 */
inline void addDOTURISupport(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& componentID)
{
    constexpr std::array<std::string_view, 1> dotInterfaces = {
        dot::dotActionIntf};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project", 0, dotInterfaces,
        [asyncResp, chassisID,
         componentID](const boost::system::error_code& dotEc,
                      const dbus::utility::MapperGetSubTreeResponse& dotResp) {
            if (dotEc)
            {
                BMCWEB_LOG_DEBUG("GetSubTree for DOT interface failed: {}",
                                 dotEc.message());
                return;
            }

            auto dotMatchResult =
                dot_utils::findMatchingDOTComponent(componentID, dotResp);
            if (dotMatchResult)
            {
                std::string basePath =
                    std::format("/redfish/v1/Chassis/{}/TrustedComponents/{}",
                                chassisID, componentID);
                nlohmann::json& oemNvidia =
                    asyncResp->res.jsonValue["Oem"]["Nvidia"];
                oemNvidia["DOT"]["@odata.id"] =
                    std::format("{}/Oem/Nvidia/DOT", basePath);
            }
        });
}

} // namespace redfish
