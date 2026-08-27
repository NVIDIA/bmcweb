/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
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
#include "error_messages.hpp"
#include "logging.hpp"

#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/message/types.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace redfish::nvidia_platform_power_cycle
{

inline constexpr std::string_view interface =
    "com.nvidia.Control.Platform.PowerCycle";
inline constexpr std::string_view property = "SupportedPowerCycleTypes";
inline constexpr std::string_view method = "RequestPowerCycle";

inline constexpr std::string_view auxPowerCycle =
    "com.nvidia.Control.Platform.PowerCycle.PowerCycleType.AuxPowerCycle";
inline constexpr std::string_view auxPowerCycleForce =
    "com.nvidia.Control.Platform.PowerCycle.PowerCycleType.AuxPowerCycleForce";
inline constexpr std::string_view fullPowerCycle =
    "com.nvidia.Control.Platform.PowerCycle.PowerCycleType.FullPowerCycle";

inline constexpr std::string_view invalidArgumentError =
    "xyz.openbmc_project.Common.Error.InvalidArgument";
inline constexpr std::string_view notAllowedError =
    "xyz.openbmc_project.Common.Error.NotAllowed";
inline constexpr std::string_view unavailableError =
    "xyz.openbmc_project.Common.Error.Unavailable";
inline constexpr std::string_view internalFailureError =
    "xyz.openbmc_project.Common.Error.InternalFailure";

struct Capabilities
{
    std::string service;
    std::vector<std::string> types;
};

using CapabilitiesCallback = std::function<void(
    const boost::system::error_code&, std::optional<Capabilities>)>;
using SupportCallback =
    std::function<void(const boost::system::error_code&, bool)>;

inline sdbusplus::message::object_path getObjectPath(
    uint64_t computerSystemIndex)
{
    return {"/xyz/openbmc_project/control/power_cycle/host" +
            std::to_string(computerSystemIndex)};
}

inline bool supports(const Capabilities& capabilities, std::string_view type)
{
    return std::ranges::find(capabilities.types, type) !=
           capabilities.types.end();
}

inline void afterGetSupportedTypes(
    const CapabilitiesCallback& callback, std::string service,
    const boost::system::error_code& ec,
    const std::vector<std::string>& supportedTypes)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Failed to read platform power-cycle capabilities: {}",
                         ec);
        callback(ec, std::nullopt);
        return;
    }

    callback({}, Capabilities{std::move(service), supportedTypes});
}

inline const dbus::utility::MapperServiceMap* findPowerCycleServices(
    const sdbusplus::message::object_path& objectPath,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    auto object =
        std::ranges::find_if(subtree, [&objectPath](const auto& entry) {
            return entry.first == objectPath.str;
        });
    if (object == subtree.end())
    {
        return nullptr;
    }
    return &object->second;
}

inline void afterGetPowerCycleSubTree(
    const sdbusplus::message::object_path& objectPath,
    CapabilitiesCallback callback, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        callback(ec, std::nullopt);
        return;
    }

    const dbus::utility::MapperServiceMap* services =
        findPowerCycleServices(objectPath, subtree);
    if (services == nullptr)
    {
        // The provider is optional. An empty subtree or no exact host object is
        // a successful capability result, distinct from a mapper failure.
        callback({}, std::nullopt);
        return;
    }
    if (services->size() != 1)
    {
        callback(boost::system::errc::make_error_code(
                     boost::system::errc::state_not_recoverable),
                 std::nullopt);
        return;
    }

    const std::string& service = services->front().first;
    dbus::utility::getProperty<std::vector<std::string>>(
        service, objectPath.str, std::string(interface), std::string(property),
        std::bind_front(afterGetSupportedTypes, std::move(callback), service));
}

inline void getSupportedPowerCycleTypes(uint64_t computerSystemIndex,
                                        CapabilitiesCallback callback)
{
    sdbusplus::message::object_path objectPath =
        getObjectPath(computerSystemIndex);
    dbus::utility::getSubTree(
        "/", 0, std::array<std::string_view, 1>{interface},
        std::bind_front(afterGetPowerCycleSubTree, objectPath,
                        std::move(callback)));
}

inline void afterGetPowerCycleSupport(
    const SupportCallback& callback, const std::string& requestedType,
    const boost::system::error_code& ec,
    const std::optional<Capabilities>& capabilities)
{
    if (ec)
    {
        callback(ec, false);
        return;
    }
    callback({}, capabilities && supports(*capabilities, requestedType));
}

inline void getFullPowerCycleSupport(uint64_t computerSystemIndex,
                                     SupportCallback callback)
{
    getSupportedPowerCycleTypes(
        computerSystemIndex,
        std::bind_front(afterGetPowerCycleSupport, std::move(callback),
                        std::string(fullPowerCycle)));
}

inline void afterRequestPowerCycle(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& redfishType, const std::string& actionName,
    const boost::system::error_code& ec, const sdbusplus::message_t& response)
{
    if (!ec)
    {
        messages::success(asyncResp->res);
        return;
    }

    const sd_bus_error* dbusError = response.get_error();
    std::string_view errorName{};
    if (dbusError != nullptr && dbusError->name != nullptr)
    {
        errorName = dbusError->name;
    }

    if (errorName == invalidArgumentError)
    {
        BMCWEB_LOG_WARNING(
            "Platform power-cycle request rejected as invalid: {}", ec);
        messages::actionParameterValueNotInList(asyncResp->res, redfishType,
                                                "ResetType", actionName);
        return;
    }
    if (errorName == notAllowedError)
    {
        BMCWEB_LOG_WARNING("Platform power-cycle request is not allowed: {}",
                           ec);
        messages::actionParameterValueConflict(asyncResp->res, "ResetType",
                                               redfishType);
        return;
    }
    if (errorName == unavailableError)
    {
        BMCWEB_LOG_ERROR("Platform power-cycle service is unavailable: {}", ec);
        messages::serviceTemporarilyUnavailable(asyncResp->res, "10");
        return;
    }
    if (errorName == internalFailureError)
    {
        BMCWEB_LOG_ERROR("Platform power-cycle request failed internally: {}",
                         ec);
        messages::internalError(asyncResp->res);
        return;
    }
    BMCWEB_LOG_ERROR(
        "Platform power-cycle request failed with unexpected error {}: {}",
        errorName, ec);
    messages::internalError(asyncResp->res);
}

inline void afterGetCapabilitiesForRequest(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::object_path& objectPath,
    const std::string& requestedType, const std::string& redfishType,
    const std::string& actionName, const boost::system::error_code& ec,
    const std::optional<Capabilities>& capabilities)
{
    if (ec)
    {
        messages::internalError(asyncResp->res);
        return;
    }
    if (!capabilities || !supports(*capabilities, requestedType))
    {
        messages::actionParameterValueNotInList(asyncResp->res, redfishType,
                                                "ResetType", actionName);
        return;
    }

    std::function<void(const boost::system::error_code&,
                       const sdbusplus::message_t&)>
        callback = std::bind_front(afterRequestPowerCycle, asyncResp,
                                   redfishType, actionName);
    dbus::utility::async_method_call(std::move(callback), capabilities->service,
                                     objectPath.str, std::string(interface),
                                     std::string(method), requestedType);
}

inline void requestPowerCycleWithCapabilities(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    uint64_t computerSystemIndex, const Capabilities& capabilities,
    std::string_view requestedType, std::string_view redfishType,
    std::string_view actionName)
{
    if (!supports(capabilities, requestedType))
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, std::string(redfishType), "ResetType",
            std::string(actionName));
        return;
    }

    sdbusplus::message::object_path objectPath =
        getObjectPath(computerSystemIndex);
    std::function<void(const boost::system::error_code&,
                       const sdbusplus::message_t&)>
        callback =
            std::bind_front(afterRequestPowerCycle, asyncResp,
                            std::string(redfishType), std::string(actionName));
    dbus::utility::async_method_call(
        std::move(callback), capabilities.service, objectPath.str,
        std::string(interface), std::string(method),
        std::string(requestedType));
}

inline void requestPowerCycle(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    uint64_t computerSystemIndex, std::string_view requestedType,
    std::string_view redfishType, std::string_view actionName)
{
    sdbusplus::message::object_path objectPath =
        getObjectPath(computerSystemIndex);
    getSupportedPowerCycleTypes(
        computerSystemIndex,
        std::bind_front(afterGetCapabilitiesForRequest, asyncResp,
                        std::move(objectPath), std::string(requestedType),
                        std::string(redfishType), std::string(actionName)));
}

inline void requestFullPowerCycle(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    uint64_t computerSystemIndex, std::string_view actionName)
{
    requestPowerCycle(asyncResp, computerSystemIndex, fullPowerCycle,
                      "FullPowerCycle", actionName);
}

} // namespace redfish::nvidia_platform_power_cycle
