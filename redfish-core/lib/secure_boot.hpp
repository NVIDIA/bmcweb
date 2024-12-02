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

#include "nlohmann/json.hpp"

#include <app.hpp>
#include <dbus_utility.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <utils/privilege_utils.hpp>

#include <fstream>
#include <iostream>

namespace redfish
{
namespace secure_boot
{

inline void handleSecureBootGet(crow::App& app, const crow::Request& req,
                                const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                                [[maybe_unused]] const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    aResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/SecureBoot";
    aResp->res.jsonValue["@odata.type"] = "#SecureBoot.v1_1_0.SecureBoot";
    aResp->res.jsonValue["Name"] = "UEFI Secure Boot";
    aResp->res.jsonValue["Description"] =
        "The UEFI Secure Boot associated with this system.";
    aResp->res.jsonValue["Id"] = "SecureBoot";
    aResp->res.jsonValue["SecureBootDatabases"]["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/SecureBoot/SecureBootDatabases";

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, "xyz.openbmc_project.BIOSConfigManager",
        "/xyz/openbmc_project/bios_config/manager",
        "xyz.openbmc_project.BIOSConfig.SecureBoot",
        [aResp](const boost::system::error_code ec,
                const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error on SecureBoot GetAll: {}",
                                 ec);
                messages::internalError(aResp->res);
                return;
            }

            std::string secureBootCurrentBoot;
            bool secureBootEnable = false;
            std::string secureBootMode;
            for (auto& [propertyName, propertyVariant] : properties)
            {
                if (propertyName == "CurrentBoot" &&
                    std::holds_alternative<std::string>(propertyVariant))
                {
                    secureBootCurrentBoot =
                        std::get<std::string>(propertyVariant);
                }
                else if (propertyName == "Enable" &&
                         std::holds_alternative<bool>(propertyVariant))
                {
                    secureBootEnable = std::get<bool>(propertyVariant);
                }
                else if (propertyName == "Mode" &&
                         std::holds_alternative<std::string>(propertyVariant))
                {
                    secureBootMode = std::get<std::string>(propertyVariant);
                }
            }
            if (secureBootCurrentBoot ==
                    "xyz.openbmc_project.BIOSConfig.SecureBoot.CurrentBootType.Unknown" ||
                secureBootMode ==
                    "xyz.openbmc_project.BIOSConfig.SecureBoot.ModeType.Unknown")
            {
                // BMC has not yet recevied data.
                return;
            }

            if (secureBootCurrentBoot ==
                "xyz.openbmc_project.BIOSConfig.SecureBoot.CurrentBootType.Enabled")
            {
                aResp->res.jsonValue["SecureBootCurrentBoot"] = "Enabled";
            }
            else if (
                secureBootCurrentBoot ==
                "xyz.openbmc_project.BIOSConfig.SecureBoot.CurrentBootType.Disabled")
            {
                aResp->res.jsonValue["SecureBootCurrentBoot"] = "Disabled";
            }

            aResp->res.jsonValue["SecureBootEnable"] = secureBootEnable;

            if (secureBootMode ==
                "xyz.openbmc_project.BIOSConfig.SecureBoot.ModeType.SetupMode")
            {
                aResp->res.jsonValue["SecureBootMode"] = "SetupMode";
            }
            else if (
                secureBootMode ==
                "xyz.openbmc_project.BIOSConfig.SecureBoot.ModeType.UserMode")
            {
                aResp->res.jsonValue["SecureBootMode"] = "UserMode";
            }
            else if (
                secureBootMode ==
                "xyz.openbmc_project.BIOSConfig.SecureBoot.ModeType.AuditMode")
            {
                aResp->res.jsonValue["SecureBootMode"] = "AuditMode";
            }
            else if (
                secureBootMode ==
                "xyz.openbmc_project.BIOSConfig.SecureBoot.ModeType.DeployedMode")
            {
                aResp->res.jsonValue["SecureBootMode"] = "DeployedMode";
            }
        });
}

inline void
    handleSecureBootPatch(crow::App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                          [[maybe_unused]] const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }

    privilege_utils::isBiosPrivilege(req, [req, aResp](
                                              const boost::system::error_code
                                                  ec,
                                              const bool isBios) {
        std::optional<std::string> secureBootCurrentBoot;
        std::optional<bool> secureBootEnable;
        std::optional<std::string> secureBootMode;
        if (ec || isBios == false)
        {
            // Request is not from BIOS
            if (!json_util::readJsonPatch(req, aResp->res, "SecureBootEnable",
                                          secureBootEnable))
            {
                return;
            }
        }
        else
        {
            // Request from BIOS
            if (!json_util::readJsonPatch(
                    req, aResp->res, "SecureBootCurrentBoot",
                    secureBootCurrentBoot, "SecureBootEnable", secureBootEnable,
                    "SecureBootMode", secureBootMode))
            {
                return;
            }
        }

        if (secureBootCurrentBoot)
        {
            if (*secureBootCurrentBoot == "Enabled")
            {
                *secureBootCurrentBoot =
                    "xyz.openbmc_project.BIOSConfig.SecureBoot.CurrentBootType.Enabled";
            }
            else if (*secureBootCurrentBoot == "Disabled")
            {
                *secureBootCurrentBoot =
                    "xyz.openbmc_project.BIOSConfig.SecureBoot.CurrentBootType.Disabled";
            }
            else
            {
                messages::propertyValueNotInList(aResp->res,
                                                 *secureBootCurrentBoot,
                                                 "SecureBootCurrentBoot");
                return;
            }
        }

        if (secureBootMode)
        {
            if (*secureBootMode == "SetupMode")
            {
                *secureBootMode =
                    "xyz.openbmc_project.BIOSConfig.SecureBoot.ModeType.SetupMode";
            }
            else if (*secureBootMode == "UserMode")
            {
                *secureBootMode =
                    "xyz.openbmc_project.BIOSConfig.SecureBoot.ModeType.UserMode";
            }
            else if (*secureBootMode == "AuditMode")
            {
                *secureBootMode =
                    "xyz.openbmc_project.BIOSConfig.SecureBoot.ModeType.AuditMode";
            }
            else if (*secureBootMode == "DeployedMode")
            {
                *secureBootMode =
                    "xyz.openbmc_project.BIOSConfig.SecureBoot.ModeType.DeployedMode";
            }
            else
            {
                messages::propertyValueNotInList(aResp->res, *secureBootMode,
                                                 "SecureBootMode");
                return;
            }
        }

        aResp->res.result(boost::beast::http::status::no_content);

        if (secureBootCurrentBoot)
        {
            crow::connections::systemBus->async_method_call(
                [aResp](const boost::system::error_code ec) {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                        messages::internalError(aResp->res);
                        return;
                    }
                },
                "xyz.openbmc_project.BIOSConfigManager",
                "/xyz/openbmc_project/bios_config/manager",
                "org.freedesktop.DBus.Properties", "Set",
                "xyz.openbmc_project.BIOSConfig.SecureBoot", "CurrentBoot",
                dbus::utility::DbusVariantType(*secureBootCurrentBoot));
        }

        if (secureBootEnable)
        {
            crow::connections::systemBus->async_method_call(
                [aResp](const boost::system::error_code ec) {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                        messages::internalError(aResp->res);
                        return;
                    }
                },
                "xyz.openbmc_project.BIOSConfigManager",
                "/xyz/openbmc_project/bios_config/manager",
                "org.freedesktop.DBus.Properties", "Set",
                "xyz.openbmc_project.BIOSConfig.SecureBoot", "Enable",
                dbus::utility::DbusVariantType(*secureBootEnable));
        }

        if (secureBootMode)
        {
            crow::connections::systemBus->async_method_call(
                [aResp](const boost::system::error_code ec) {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                        messages::internalError(aResp->res);
                        return;
                    }
                },
                "xyz.openbmc_project.BIOSConfigManager",
                "/xyz/openbmc_project/bios_config/manager",
                "org.freedesktop.DBus.Properties", "Set",
                "xyz.openbmc_project.BIOSConfig.SecureBoot", "Mode",
                dbus::utility::DbusVariantType(*secureBootMode));
        }
    });
}

} // namespace secure_boot

inline void requestRoutesSecureBoot(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/SecureBoot/")
        .privileges(redfish::privileges::getSecureBoot)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(secure_boot::handleSecureBootGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/SecureBoot/")
        .privileges(redfish::privileges::patchSecureBoot)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(secure_boot::handleSecureBootPatch, std::ref(app)));
}

} // namespace redfish
