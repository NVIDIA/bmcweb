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

#include <functional>
#include <optional>
#include <string_view>

namespace redfish::debug_token
{

constexpr const std::string_view erasePolicyIntf{
    "com.nvidia.DebugToken.ErasePolicy"};
constexpr const std::string_view erasePolicyEnumPrefix{
    "com.nvidia.DebugToken.ErasePolicy.PolicyTypes."};

/*
 * @brief Erase policy property DBus get handler
 *
 * @param[in] callback - The callback to call with the final result
 * @param[in] ec - DBus rror code
 * @param[in] policy - The erase policy property value
 */
static inline void
    dbusGetHandler(std::function<void(std::optional<bool>)> callback,
                   const boost::system::error_code& ec,
                   const std::string& policy)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Erase policy get error: {}", ec.message());
        callback(std::nullopt);
        return;
    }
    std::string policyStr = policy.substr(policy.find_last_of('.') + 1);
    bool automatic = policyStr == "Automatic" ? true : false;
    callback(automatic);
}

/*
 * @brief Erase policy object path DBus handler for the get operation
 *
 * @param[in] callback - The callback to call with the final result
 * @param[in] service - The name of the service hosting the erase policy object
 * @param[in] path - The path to the erase policy object
 */
static inline void
    getPathCallback(std::function<void(std::optional<bool>)> callback,
                    std::string service, std::string path)
{
    if (service.empty() || path.empty())
    {
        BMCWEB_LOG_ERROR("Invalid service or path");
        callback(std::nullopt);
        return;
    }
    std::function<void(const boost::system::error_code&, const std::string&)>
        handler = std::bind_front(dbusGetHandler, std::move(callback));
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, path,
        std::string(erasePolicyIntf), "Policy", handler);
}

/*
 * @brief Erase policy property DBus set handler
 *
 * @param[in] asyncResp - The async response pointer
 * @param[in] ec - DBus error code
 */
static inline void
    dbusSetHandler(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const boost::system::error_code& ec)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Erase policy set error: {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }
    asyncResp->res.result(boost::beast::http::status::no_content);
}

/*
 * @brief Erase policy object path DBus handler for the set operation
 *
 * @param[in] asyncResp - The async response pointer
 * @param[in] value - The erase policy setting
 * @param[in] service - The name of the service hosting the erase policy object
 * @param[in] path - The path to the erase policy object
 */
static inline void
    setPathCallback(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                    bool automatic, std::string service, std::string path)
{
    if (service.empty() || path.empty())
    {
        BMCWEB_LOG_ERROR("Invalid service or path");
        messages::internalError(asyncResp->res);
        return;
    }
    std::string erasePolicy = automatic ? "Automatic" : "Manual";
    std::string dbusValue = std::string(erasePolicyEnumPrefix) + erasePolicy;
    std::function<void(const boost::system::error_code&)> handler =
        std::bind_front(dbusSetHandler, std::move(asyncResp));
    sdbusplus::asio::setProperty(*crow::connections::systemBus, service, path,
                                 std::string(erasePolicyIntf), "Policy",
                                 dbusValue, handler);
}

/*
 * @brief DBus handler for the get subtree operation
 *
 * @param[in] callback - The callback to call with the final result
 * @param[in] ec - DBus error code
 * @param[in] subtree - The subtree response
 */
static inline void
    getSubTreeHandler(std::function<void(std::string, std::string)> callback,
                      const boost::system::error_code& ec,
                      const dbus::utility::MapperGetSubTreeResponse& subtree)
{
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
    callback(service, path);
}

/*
 * @brief Get the erase policy object path
 *
 * @param[in] callback - The callback to call with the service and path
 */
static inline void getErasePolicyObjectPath(
    std::function<void(std::string, std::string)> callback)
{
    constexpr std::array<std::string_view, 1> interfaces = {erasePolicyIntf};
    dbus::utility::getSubTree(
        "/com/nvidia/debug_token/", 0, interfaces,
        std::bind_front(getSubTreeHandler, std::move(callback)));
}

/*
 * @brief Get the erase policy
 *
 * @param[in] callback - The callback to call with the final result
 */
inline void getErasePolicy(std::function<void(std::optional<bool>)> callback)
{
    getErasePolicyObjectPath(
        std::bind_front(getPathCallback, std::move(callback)));
}

/*
 * @brief Set the erase policy
 *
 * @param[in] asyncResp - The async response pointer
 * @param[in] value - The erase policy setting
 */
inline void setErasePolicy(std::shared_ptr<bmcweb::AsyncResp> asyncResp,
                           bool automatic)
{
    getErasePolicyObjectPath(
        std::bind_front(setPathCallback, std::move(asyncResp), automatic));
}

} // namespace redfish::debug_token
