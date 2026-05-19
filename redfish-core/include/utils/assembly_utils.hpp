// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "async_resp.hpp"
#include "boost_formatters.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "human_sort.hpp"
#include "logging.hpp"
#include "utils/chassis_utils.hpp"

#include <asm-generic/errno.h>

#include <boost/system/error_code.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace redfish
{

static constexpr std::array<std::string_view, 2> assemblyInterfaces = {
    "xyz.openbmc_project.Inventory.Item.Panel",
    "xyz.openbmc_project.Inventory.Item.Assembly"};

namespace assembly_utils
{

using AssemblyCallback =
    std::function<void(const boost::system::error_code&,
                       const std::vector<std::string>& sortedAssemblyList)>;

inline void afterGetChassisAssemblyFallback(
    AssemblyCallback& callback, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& subtreePaths)
{
    // Some inventory providers (e.g. NSM) publish Item.Assembly objects
    // beneath the chassis path without a "containing" association. Treat
    // mapper errors here as "no assemblies" since the caller already
    // confirmed the chassis exists.
    if (ec)
    {
        callback(boost::system::error_code{}, std::vector<std::string>());
        return;
    }

    std::vector<std::string> sortedAssemblyList = subtreePaths;
    std::ranges::sort(sortedAssemblyList, AlphanumLess<std::string>());
    callback(boost::system::error_code{}, sortedAssemblyList);
}

inline void getChassisAssemblyFallback(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, AssemblyCallback&& callback)
{
    chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        [callback = std::move(callback)](
            const std::optional<std::string>& chassisPath) mutable {
            if (!chassisPath)
            {
                callback(boost::system::error_code{},
                         std::vector<std::string>());
                return;
            }
            dbus::utility::getSubTreePaths(
                *chassisPath, 0, assemblyInterfaces,
                std::bind_front(afterGetChassisAssemblyFallback,
                                std::move(callback)));
        });
}

inline void afterGetChassisAssembly(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, AssemblyCallback& callback,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& subtreePaths)
{
    if (ec)
    {
        if (ec.value() == boost::system::errc::io_error || ec.value() == EBADR)
        {
            // Association not present; try subtree-walk fallback.
            getChassisAssemblyFallback(asyncResp, chassisId,
                                       std::move(callback));
            return;
        }

        BMCWEB_LOG_ERROR("DBUS response error {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    if (subtreePaths.empty())
    {
        getChassisAssemblyFallback(asyncResp, chassisId, std::move(callback));
        return;
    }

    std::vector<std::string> sortedAssemblyList = subtreePaths;
    std::ranges::sort(sortedAssemblyList, AlphanumLess<std::string>());

    callback(ec, sortedAssemblyList);
}

/**
 * @brief Get chassis path with given chassis ID
 * @param[in] asyncResp - Shared pointer for asynchronous calls.
 * @param[in] chassisId - Chassis to which the assemblies are
 * associated.
 * @param[in] callback
 *
 * @return None.
 */
inline void getChassisAssembly(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, AssemblyCallback&& callback)
{
    BMCWEB_LOG_DEBUG("Get ChassisAssembly");

    // Downstream NVIDIA inventory providers publish the chassis->assemblies
    // link as the "assembly" forward association; upstream defaults to
    // "containing" which is empty on this hardware.
    dbus::utility::getAssociatedSubTreePathsById(
        chassisId, "/xyz/openbmc_project/inventory", chassisInterfaces,
        "assembly", assemblyInterfaces,
        std::bind_front(afterGetChassisAssembly, asyncResp, chassisId,
                        std::move(callback)));
}

} // namespace assembly_utils
} // namespace redfish
