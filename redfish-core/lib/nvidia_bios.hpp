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
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/sw_utils.hpp"

#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>

namespace redfish
{
constexpr const char* biosConfigObj =
    "/xyz/openbmc_project/bios_config/manager";
constexpr const char* biosConfigIface =
    "xyz.openbmc_project.BIOSConfig.Manager";

enum class SecureSelector
{
    nonSecure = 0,
    secure = 1,
    both = 2
};

/**
 *@brief sets the Reset BIOS Settings to default property.
 *
 * @param[in]       ResetBiosToDefaultsPending    Reset BIOS Settings to Default
 *status
 * @param[in,out]   asyncResp   Async HTTP response.
 *
 * @return None.
 */
[[maybe_unused]] static void setResetBiosSettings(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const bool& resetBiosToDefaultsPending)
{
    BMCWEB_LOG_DEBUG("Set Reset Bios Settings to Defaults Pending Status");
    crow::connections::systemBus->async_method_call(
        [asyncResp, resetBiosToDefaultsPending](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetObject& objType) {
            if (ec || objType.empty())
            {
                BMCWEB_LOG_DEBUG("GetObject for path {}", biosConfigObj);
                return;
            }

            const std::string& biosService = objType.begin()->first;

            std::string biosMode;
            if (resetBiosToDefaultsPending)
            {
                biosMode =
                    "xyz.openbmc_project.BIOSConfig.Manager.ResetFlag.FactoryDefaults";
            }
            else
            {
                biosMode =
                    "xyz.openbmc_project.BIOSConfig.Manager.ResetFlag.NoAction";
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code& ec2) {
                    if (ec2)
                    {
                        BMCWEB_LOG_DEBUG(
                            "DBUS response error for "
                            "Set Reset BIOS setting to default status.");
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    messages::success(asyncResp->res);
                },
                biosService, biosConfigObj, "org.freedesktop.DBus.Properties",
                "Set", biosConfigIface, "ResetBIOSSettings",
                std::variant<std::string>(biosMode));
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", biosConfigObj,
        std::array<const char*, 1>{biosConfigIface});
}

/**
 * Set ClearNonVolatileVariables.Clear to requested value
 */
inline void setClearVariables(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& path, const bool requestToClear)
{
    crow::connections::systemBus->async_method_call(
        [aResp, path, service](const boost::system::error_code& ec,
                               sdbusplus::message::message& msg) {
            if (!ec)
            {
                BMCWEB_LOG_DEBUG("Set ClearUefiVariable successed");
                return;
            }

            BMCWEB_LOG_DEBUG("Set ClearUefiVariable failed: {}", ec.what());

            // Read and convert dbus error message to redfish error
            const sd_bus_error* dbusError = msg.get_error();
            if (dbusError == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }

            if (strcmp(dbusError->name, "xyz.openbmc_project.Common."
                                        "Device.Error.WriteFailure") == 0)
            {
                // Service failed to change the config
                messages::operationFailed(aResp->res);
            }
            else
            {
                messages::internalError(aResp->res);
            }
        },
        service, path, "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Control.Boot.ClearNonVolatileVariables", "Clear",
        std::variant<bool>(requestToClear));
}

inline void handleClearSecureStateSubtree(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const SecureSelector secure, const bool requestToClear,
    const dbus::utility::MapperGetSubTreeResponse& clearSubtree,
    const dbus::utility::MapperGetSubTreeResponse& secureSubtree)
{
    for (const auto& [clearPath, clearServices] : clearSubtree)
    {
        if (clearServices.size() != 1)
        {
            BMCWEB_LOG_ERROR(
                "Number of ClearNonVolatileVariables provider is not 1. size={}",
                clearServices.size());
            messages::internalError(aResp->res);
            return;
        }
        const auto& clearService = clearServices[0].first;

        if (secure == SecureSelector::both)
        {
            setClearVariables(aResp, clearService, clearPath, requestToClear);
        }
        else
        {
            std::string closestSecurePath;
            std::string secureService;
            for (const auto& [securePath, secureServices] : secureSubtree)
            {
                if (!clearPath.starts_with(securePath))
                {
                    // not a parent path of the ClearNonVolatileVariables
                    continue;
                }
                if (securePath.length() > closestSecurePath.length())
                {
                    closestSecurePath = securePath;
                    secureService = secureServices[0].first;
                }
            }

            if (closestSecurePath.empty())
            {
                // skip 2082_17/SystemReset_0_0 effector
                continue;
            }

            crow::connections::systemBus->async_method_call(
                [aResp, secure, requestToClear, clearService,
                 clearPath](const boost::system::error_code& ec,
                            const std::variant<bool>& resp) {
                    if (ec)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }

                    const bool* secureState = std::get_if<bool>(&resp);
                    if (secureState == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }

                    if ((*secureState && secure == SecureSelector::secure) ||
                        (!*secureState && secure == SecureSelector::nonSecure))
                    {
                        setClearVariables(aResp, clearService, clearPath,
                                          requestToClear);
                    }
                },
                secureService, closestSecurePath,
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.State.Decorator.SecureState", "secure");
        }
    }
}

inline void handleClearNonVolatileVariablesSubtree(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const SecureSelector secure, const bool requestToClear,
    const dbus::utility::MapperGetSubTreeResponse& clearSubtree)
{
    if (secure == SecureSelector::both)
    {
        handleClearSecureStateSubtree(
            aResp, secure, requestToClear, clearSubtree,
            dbus::utility::MapperGetSubTreeResponse());
        return;
    }

    crow::connections::systemBus->async_method_call(
        [aResp, secure, requestToClear,
         clearSubtree](boost::system::error_code& ec,
                       const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                // No state sensors attached.
                messages::internalError(aResp->res);
                return;
            }

            handleClearSecureStateSubtree(aResp, secure, requestToClear,
                                          clearSubtree, subtree);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/control", 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.State.Decorator.SecureState"});
}

inline void clearVariables(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const SecureSelector secure,
                           const bool requestToClear)
{
    crow::connections::systemBus->async_method_call(
        [aResp, secure, requestToClear](
            boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                // No state sensors attached.
                messages::internalError(aResp->res);
                return;
            }

            handleClearNonVolatileVariablesSubtree(aResp, secure,
                                                   requestToClear, subtree);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/control", 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Control.Boot.ClearNonVolatileVariables"});

    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                messages::internalError(aResp->res);
                return;
            }
            BMCWEB_LOG_DEBUG("Boot override CMOSClear update done.");
        },
        "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/control/host0/boot",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Control.Boot.Flags", "CMOSClear",
        dbus::utility::DbusVariantType(true));

    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                messages::internalError(aResp->res);
                return;
            }
            BMCWEB_LOG_DEBUG("Boot override enable update done.");
        },
        "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/control/host0/boot",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Object.Enable", "Enabled",
        dbus::utility::DbusVariantType(true));
}

inline void afterOemResetBiosGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec || subtree.empty())
    {
        return;
    }
    asyncResp->res.jsonValue["Actions"]["Oem"]
                            ["#NvidiaComputerSystem.ResetBios"]["target"] =
        boost::urls::format(
            "/redfish/v1/Systems/{}/Actions/Oem/NvidiaComputerSystem.ResetBios",
            BMCWEB_REDFISH_SYSTEM_URI_NAME);
    asyncResp->res
        .jsonValue["Actions"]["Oem"]["#NvidiaComputerSystem.ResetBios"]
                  ["@Redfish.ActionInfo"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/ResetBiosActionInfo",
        BMCWEB_REDFISH_SYSTEM_URI_NAME);
}

inline void handleOemResetBiosGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Control.Boot.ClearNonVolatileVariables"};

    dbus::utility::getSubTree("/xyz/openbmc_project/control", 0, interfaces,
                              std::bind_front(afterOemResetBiosGet, asyncResp));
}

inline void handleOemResetBiosActionInfoGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_2_0.ActionInfo";
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + systemName + "/Oem/Nvidia/ResetBiosActionInfo";
    asyncResp->res.jsonValue["Name"] = "BIOS Reset Action Info";
    asyncResp->res.jsonValue["Id"] = "ResetBiosActionInfo";
    nlohmann::json::array_t parameters;
    nlohmann::json::object_t parameter;

    parameter["Name"] = "ResetBiosType";
    parameter["Required"] = true;
    parameter["DataType"] = "String";
    nlohmann::json::array_t allowableValues;
    allowableValues.emplace_back("SecureReset");
    allowableValues.emplace_back("NonSecureReset");
    parameter["AllowableValues"] = std::move(allowableValues);
    parameters.emplace_back(std::move(parameter));

    asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
}

inline void afterGetClearNonVolatileVariablesSubtree(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const SecureSelector& secure, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec || subtree.empty())
    {
        messages::internalError(asyncResp->res);
        return;
    }

    handleClearNonVolatileVariablesSubtree(asyncResp, secure, true, subtree);
}

/**
 * Nvidia BiosReset class supports handle POST method for Reset bios.
 */
inline void handleNvidiaBiosResetPost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& systemName)
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

    // set the ResetBiosToDefaultsPending
    setResetBiosSettings(asyncResp, true);

    clearVariables(asyncResp, SecureSelector::nonSecure, true);
}

inline void handleOemBiosResetAction(
    App& app, const crow::Request& req,
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
    std::string resetType;
    if (!json_util::readJsonAction(req, asyncResp->res, "ResetBiosType",
                                   resetType))
    {
        BMCWEB_LOG_ERROR("No 'ResetBiosType' found");
        messages::unrecognizedRequestBody(asyncResp->res);
        return;
    }

    SecureSelector secure = SecureSelector::secure;
    if (resetType == "NonSecureReset")
    {
        secure = SecureSelector::nonSecure;
    }
    else if (resetType != "SecureReset")
    {
        messages::actionParameterValueError(asyncResp->res, "ResetBiosType",
                                            "NvidiaComputerSystem.ResetBios");
        return;
    }
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Control.Boot.ClearNonVolatileVariables"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/control", 0, interfaces,
        std::bind_front(afterGetClearNonVolatileVariablesSubtree, asyncResp,
                        secure));
}

inline void requestRoutesOemBiosResetService(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/Oem/Nvidia/ResetBiosActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleOemResetBiosActionInfoGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Actions/Oem/NvidiaComputerSystem.ResetBios/")
        .privileges(redfish::privileges::postBios)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleOemBiosResetAction, std::ref(app)));
}
} // namespace redfish
