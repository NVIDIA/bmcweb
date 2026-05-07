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

#include "app.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "sub_request.hpp"

#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <memory>
#include <string>
#include <string_view>

namespace redfish
{

// Forward declaration for functions from managers.hpp
void doBMCGracefulRestart(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp);

inline void handleGetManagerEmmcFullSecureErase(
    const SubRequest& /*req*/,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& /*managerId*/)
{
    // Only advertise the action if a backend implementing the D-Bus
    // interface is currently registered (mirrors the runtime check the
    // POST handler does).
    constexpr std::array<std::string_view, 1> interfaces = {
        "com.nvidia.Common.EmmcFullSecureErase"};
    dbus::utility::getDbusObject(
        "/xyz/openbmc_project/software", interfaces,
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetObject& interfaceNames) {
            if (ec || interfaceNames.empty())
            {
                return;
            }
            nlohmann::json& oemActions = asyncResp->res.jsonValue;
            oemActions["target"] = boost::urls::format(
                "/redfish/v1/Managers/{}/Actions/Oem/eMMC.FullSecureErase",
                BMCWEB_REDFISH_MANAGER_URI_NAME);
            oemActions["@Redfish.ActionInfo"] = boost::urls::format(
                "/redfish/v1/Managers/{}/Oem/EmmcFullSecureEraseActionInfo",
                BMCWEB_REDFISH_MANAGER_URI_NAME);
        });
}

inline void handleEmmcFullSecureEraseResult(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("Failed to EmmcFullSecureErase: {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }
    doBMCGracefulRestart(asyncResp);
}

inline void handleGetObjectEmmcFullSecureErase(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& interfaceNames)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("D-Bus error: {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }
    if (interfaceNames.empty())
    {
        BMCWEB_LOG_ERROR("eMMC Full Secure Erase interface not found");
        messages::resourceNotFound(asyncResp->res, "#Manager.v1_15_0.Manager",
                                   "eMMC.FullSecureErase");
        return;
    }

    for (const auto& object : interfaceNames)
    {
        dbus::utility::async_method_call(
            [asyncResp](const boost::system::error_code& ec1) {
                handleEmmcFullSecureEraseResult(asyncResp, ec1);
            },
            object.first, "/xyz/openbmc_project/software",
            "com.nvidia.Common.EmmcFullSecureErase", "EmmcFullSecureErase");
    }
}

/**
 * eMMCFullSecureErase class supports POST method for eMMC Full Secure Erase
 */
inline void requestRoutesNvidiaManagerEmmcFullSecureErase(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Managers/<str>/Actions/Oem/eMMC.FullSecureErase")
        .privileges(redfish::privileges::postManager)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                BMCWEB_LOG_DEBUG("Post eMMC Full Secure Erase.");
                (void)req;
                constexpr std::array<std::string_view, 1>
                    emmcFullSecureEraseIntf = {
                        "com.nvidia.Common.EmmcFullSecureErase"};

                dbus::utility::getDbusObject(
                    "/xyz/openbmc_project/software", emmcFullSecureEraseIntf,
                    [asyncResp](
                        const boost::system::error_code& ec,
                        const dbus::utility::MapperGetObject& interfaceNames) {
                        handleGetObjectEmmcFullSecureErase(asyncResp, ec,
                                                           interfaceNames);
                    });
            });
}

/**
 * EmmcFullSecureEraseActionInfo — describes the FullSecureErase action.
 * Mirrors requestRoutesManagerEmmcSecureEraseActionInfo (nvidia_managers.hpp).
 */
inline void requestRoutesManagerEmmcFullSecureEraseActionInfo(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/Managers/<str>/Oem/EmmcFullSecureEraseActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            [[maybe_unused]] const std::string& systemName) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            // Discovery-gate the ActionInfo body the same way the
            // Manager-level action listing and the POST handler do:
            // if no daemon exports the interface (e.g. platforms that
            // ship only the legacy nvidia-emmc-partition SecureErase
            // backend), return 404 instead of a static ActionInfo for
            // an action the platform cannot actually invoke.
            constexpr std::array<std::string_view, 1> emmcFullSecureEraseIntf =
                {"com.nvidia.Common.EmmcFullSecureErase"};
            dbus::utility::getDbusObject(
                "/xyz/openbmc_project/software", emmcFullSecureEraseIntf,
                [asyncResp](
                    const boost::system::error_code& ec,
                    const dbus::utility::MapperGetObject& interfaceNames) {
                    if (ec || interfaceNames.empty())
                    {
                        messages::resourceNotFound(
                            asyncResp->res, "#ActionInfo.v1_1_2.ActionInfo",
                            "EmmcFullSecureEraseActionInfo");
                        return;
                    }
                    asyncResp->res.jsonValue["@odata.type"] =
                        "#ActionInfo.v1_1_2.ActionInfo";
                    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
                        "/redfish/v1/Managers/{}/Oem/EmmcFullSecureEraseActionInfo",
                        BMCWEB_REDFISH_MANAGER_URI_NAME);
                    asyncResp->res.jsonValue["Name"] =
                        "Emmc Full Secure Erase Action Info";
                    asyncResp->res.jsonValue["Id"] =
                        "EmmcFullSecureEraseActionInfo";
                });
        });
}

} // namespace redfish
