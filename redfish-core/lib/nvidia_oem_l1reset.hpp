/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION &
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

#include "app.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/json_utils.hpp"

#include <sdbusplus/message.hpp>

#include <string_view>

namespace redfish
{
namespace nvidia_oem_l1reset
{

inline void handleL1ResetError(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::string_view dbusErrorName)
{
    if (dbusErrorName == "xyz.openbmc_project.Common.Error.Unavailable")
    {
        messages::serviceTemporarilyUnavailable(asyncResp->res, "0");
        return;
    }
    if (dbusErrorName == "org.freedesktop.DBus.Error.UnknownObject")
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   BMCWEB_REDFISH_SYSTEM_URI_NAME);
        return;
    }
    if (dbusErrorName == "xyz.openbmc_project.Common.Error.Timeout")
    {
        messages::serviceTemporarilyUnavailable(asyncResp->res, "60");
        return;
    }
    messages::internalError(asyncResp->res);
}

inline void handleL1ResetResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, const sdbusplus::message_t& msg)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("L1Reset D-Bus call failed: {}", ec.message());
        const sd_bus_error* dbusError = msg.get_error();
        if (dbusError != nullptr)
        {
            BMCWEB_LOG_ERROR("L1Reset D-Bus error: {} - {}", dbusError->name,
                             dbusError->message);
            handleL1ResetError(asyncResp, dbusError->name);
            return;
        }
        messages::internalError(asyncResp->res);
        return;
    }
    messages::success(asyncResp->res);
}

inline void handleSystemsOemNvidiaL1Reset(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    if (!json_util::requireEmptyOrEmptyJsonObject(
            asyncResp->res, req.body(), "NvidiaComputerSystem.L1Reset"))
    {
        return;
    }

    dbus::utility::async_method_call(
        asyncResp,
        [asyncResp](const boost::system::error_code& ec,
                    const sdbusplus::message_t& msg) {
            handleL1ResetResponse(asyncResp, ec, msg);
        },
        "xyz.openbmc_project.State.Boot.Raw",
        "/xyz/openbmc_project/state/host0", "com.nvidia.L1Reset", "Reset");
}

} // namespace nvidia_oem_l1reset

inline void requestRoutesSystemsOemNvidiaL1Reset(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Actions/Oem/NvidiaComputerSystem.L1Reset/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            nvidia_oem_l1reset::handleSystemsOemNvidiaL1Reset, std::ref(app)));
}

} // namespace redfish
