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

#include <nlohmann/json.hpp>

#include <format>
#include <memory>
#include <string>

namespace redfish
{
namespace debug_token
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
    std::string basePath = std::format(
        "/redfish/v1/Chassis/{}/TrustedComponents/{}", chassisID, componentID);
    nlohmann::json& actionsOem = asyncResp->res.jsonValue["Actions"]["Oem"];
    actionsOem["#NvidiaTrustedComponent.GenerateToken"]["target"] = std::format(
        "{}/Actions/Oem/NvidiaTrustedComponent.GenerateToken", basePath);

    actionsOem["#NvidiaTrustedComponent.EraseToken"]["target"] = std::format(
        "{}/Actions/Oem/NvidiaTrustedComponent.EraseToken", basePath);
    actionsOem["#NvidiaTrustedComponent.EraseToken"]["@Redfish.ActionInfo"] =
        std::format("{}/Oem/Nvidia/EraseTokenActionInfo", basePath);

    nlohmann::json& oemNvidia = asyncResp->res.jsonValue["Oem"]["Nvidia"];

    oemNvidia["@odata.type"] =
        "#NvidiaTrustedComponent.v1_0_0.NvidiaTrustedComponent";

    oemNvidia["InstallTokenPushURI"] =
        std::format("{}/install-token", basePath);

    oemNvidia["DebugTokens"]["@odata.id"] =
        std::format("{}/Oem/Nvidia/DebugTokens", basePath);
}

} // namespace debug_token
} // namespace redfish
