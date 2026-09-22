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
#include "generated/enums/nvidia_astra.hpp"
#include "logging.hpp"
#include "nvidia_dbus_utility.hpp"
#include "utils/chassis_utils.hpp"

#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace redfish::nvidia_oem_chassis_astra
{

constexpr auto astraInterface = "com.nvidia.Astra";

struct AstraResource;

/** @brief Receives the chassis's Astra resource, or empty when it has none. */
using AstraResourceCallback =
    std::function<void(const std::optional<AstraResource>&)>;
using AstraStateCallback = std::function<void(
    const boost::system::error_code&, const dbus::utility::DBusPropertiesMap&)>;
using AstraEndpointsCallback = std::function<void(
    const boost::system::error_code&, const dbus::utility::MapperEndPoints&)>;
using SetAstraModeCallback =
    std::function<void(const boost::system::error_code&, sdbusplus::message_t&,
                       const sdbusplus::object_path&)>;

/** @brief A chassis's Astra resource, and the D-Bus object behind it. */
struct AstraResource
{
    std::string chassisId;
    std::string path;
    std::string service;

    std::string uri() const
    {
        return boost::urls::format("/redfish/v1/Chassis/{}/Oem/Nvidia/Astra",
                                   chassisId)
            .buffer();
    }

    std::string actionUri() const
    {
        return boost::urls::format("/redfish/v1/Chassis/{}/Oem/Nvidia/Astra/"
                                   "Actions/NvidiaAstra.SetAstraMode",
                                   chassisId)
            .buffer();
    }

    std::string actionInfoUri() const
    {
        return boost::urls::format("/redfish/v1/Chassis/{}/Oem/Nvidia/Astra/"
                                   "SetAstraModeActionInfo",
                                   chassisId)
            .buffer();
    }

    /** @brief The chassis's Astra object in a subtree search, or empty when
     *         the chassis has none. */
    static std::optional<AstraResource> fromSubtree(
        const std::string& chassisId,
        const dbus::utility::MapperGetSubTreeResponse& subtree)
    {
        for (const auto& [path, serviceMap] : subtree)
        {
            if (!serviceMap.empty())
            {
                return AstraResource{chassisId, path, serviceMap.front().first};
            }
        }
        return std::nullopt;
    }

    /**
     * @brief Find the chassis's Astra resource.
     *
     * An unknown chassis answers 404 and a D-Bus failure 500, without calling
     * back; a chassis without Astra calls back empty, for the caller to report.
     */
    static void find(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& chassisId,
                     AstraResourceCallback callback);

    void getState(AstraStateCallback callback) const
    {
        dbus::utility::getAllProperties(service, path, astraInterface,
                                        std::move(callback));
    }

    void getNetworkAdapters(AstraEndpointsCallback callback) const
    {
        dbus::utility::findAssociations(associationPath("network_adapters"),
                                        std::move(callback));
    }

    void getPCIeTopologies(AstraEndpointsCallback callback) const
    {
        dbus::utility::findAssociations(associationPath("pcie_topologies"),
                                        std::move(callback));
    }

    /** @brief The reply carries the operation's com.nvidia.Async.Status path.
     */
    void setAstraMode(nvidia_astra::AstraMode mode,
                      SetAstraModeCallback callback) const
    {
        dbus::utility::async_method_call(
            std::move(callback), service, path, astraInterface, "SetAstraMode",
            std::string(mode == nvidia_astra::AstraMode::Enabled
                            ? "com.nvidia.Astra.AstraMode.Enabled"
                            : "com.nvidia.Astra.AstraMode.Disabled"));
    }

  private:
    std::string associationPath(std::string_view name) const
    {
        sdbusplus::object_path associationPath(path);
        associationPath /= name;
        return associationPath.str;
    }
};

inline void afterAstraSubtreeFound(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const AstraResourceCallback& callback,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec && ec != boost::system::errc::host_unreachable)
    {
        BMCWEB_LOG_ERROR("Unable to find com.nvidia.Astra under {}: {}",
                         chassisId, ec.message());
        messages::internalError(asyncResp->res);
        return;
    }
    callback(AstraResource::fromSubtree(chassisId, subtree));
}

inline void afterChassisValidatedForLookup(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const AstraResourceCallback& callback,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    dbus::utility::getSubTree(
        *validChassisPath, 0, std::array<std::string_view, 1>{astraInterface},
        std::bind_front(afterAstraSubtreeFound, asyncResp, chassisId,
                        callback));
}

inline void AstraResource::find(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, AstraResourceCallback callback)
{
    chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(afterChassisValidatedForLookup, asyncResp, chassisId,
                        std::move(callback)));
}

} // namespace redfish::nvidia_oem_chassis_astra
