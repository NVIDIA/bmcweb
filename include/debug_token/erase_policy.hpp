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

#include "dbus_utility.hpp"

#include <sdbusplus/asio/property.hpp>

#include <string_view>

namespace redfish::debug_token
{

constexpr const std::string_view erasePolicyIntf{
    "com.nvidia.DebugToken.ErasePolicy"};
constexpr const std::string_view erasePolicyEnumPrefix{
    "com.nvidia.DebugToken.ErasePolicy.PolicyTypes."};

template <typename Callback>
static inline void getErasePolicyObjectPath(
    Callback&& callback, const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    constexpr std::array<std::string_view, 1> interfaces = {erasePolicyIntf};
    dbus::utility::getSubTree(
        "/com/nvidia/debug_token/", 0, interfaces,
        [callback{std::forward<Callback>(callback)},
         asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
        std::string path, service;
        if (ec)
        {
            BMCWEB_LOG_ERROR("getSubTree error: {}", ec.message());
        }
        else if (subtree.size() == 0)
        {
            BMCWEB_LOG_ERROR("No erase policy objects found");
        }
        else if (subtree.size() != 1)
        {
            BMCWEB_LOG_ERROR(
                "One erase policy object was expected, more were found");
        }
        else
        {
            path = subtree[0].first;
            service = subtree[0].second[0].first;
        }
        if (service.empty() || path.empty())
        {
            messages::internalError(asyncResp->res);
            return;
        }
        callback(service, path);
    });
}

inline void getErasePolicy(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    auto getCallback = [asyncResp](const boost::system::error_code ec,
                                   const std::string& policy) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Erase policy get error: {}", ec.message());
            messages::internalError(asyncResp->res);
            return;
        }
        std::string policyStr = policy.substr(policy.find_last_of('.') + 1);
        bool erasePolicy = policyStr == "Automatic" ? true : false;
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["AutomaticDebugTokenErased"] =
            erasePolicy;
    };
    auto pathCallback = [asyncResp, getCallback](std::string service,
                                                 std::string path) {
        sdbusplus::asio::getProperty<std::string>(
            *crow::connections::systemBus, service, path,
            std::string(erasePolicyIntf), "Policy", getCallback);
    };
    getErasePolicyObjectPath(pathCallback, asyncResp);
}

inline void setErasePolicy(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           bool value)
{
    auto setCallback = [asyncResp](const boost::system::error_code ec) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Erase policy set error: {}", ec.message());
            messages::internalError(asyncResp->res);
            return;
        }
        messages::success(asyncResp->res);
    };
    auto pathCallback = [asyncResp, value, setCallback](std::string service,
                                                        std::string path) {
        std::string erasePolicy = value == true ? "Automatic" : "Manual";
        std::string dbusValue = std::string(erasePolicyEnumPrefix) +
                                erasePolicy;
        sdbusplus::asio::setProperty(*crow::connections::systemBus, service,
                                     path, std::string(erasePolicyIntf),
                                     "Policy", dbusValue, setCallback);
    };
    getErasePolicyObjectPath(pathCallback, asyncResp);
}

} // namespace redfish::debug_token
