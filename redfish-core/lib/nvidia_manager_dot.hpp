/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved. SPDX-License-Identifier: Apache-2.0
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

namespace redfish
{

#include "dbus_utility.hpp"
#include "http/utility.hpp"
#include "nvidia_messages.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/json_utils.hpp"

#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/message.hpp>

#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

constexpr const char* dotKeydService = "xyz.openbmc_project.Security.DOT";
constexpr const char* dotKeydPath = "/xyz/openbmc_project/security/dot";
constexpr const char* dotKeydInterface = "xyz.openbmc_project.Security.DOT";

/**
 * @brief Get CAKInstalled property and set response
 */
inline void getCAKInstalledProperty(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, int32_t timeoutSeconds)
{
    dbus::utility::getProperty<std::string>(
        dotKeydService, dotKeydPath, dotKeydInterface, "CAKInstalled",
        [asyncResp, timeoutSeconds](const boost::system::error_code& errorCode,
                                    const std::string& installed) {
            if (errorCode)
            {
                BMCWEB_LOG_DEBUG("Failed to get CAKInstalled property: {}",
                                 errorCode.message());
                // Optional property, set to NotInstalled if unavailable
                asyncResp->res.jsonValue["CAKInstalled"] = "NotInstalled";
            }
            else
            {
                asyncResp->res.jsonValue["CAKInstalled"] = installed;
                // If error state, add error message with timeout value
                if (installed == "Error")
                {
                    asyncResp->res.jsonValue["CAKInstalledError"] = std::format(
                        "System failed to boot within {} seconds after CAK installation. Make sure correct CAK is being used.",
                        timeoutSeconds);
                }
            }
        });
}

inline void processCAKVerificationTimeoutProperty(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, int32_t timeoutSeconds)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG(
            "Failed to get CAKVerificationTimeoutInSeconds property: {}",
            ec.message());
        // Default to 10 seconds if unavailable
        timeoutSeconds = static_cast<int32_t>(10);
    }

    asyncResp->res.jsonValue["CAKVerificationTimeoutInSeconds"] =
        timeoutSeconds;

    // Get CAKInstalled property (enum: "NotInstalled", "Installed", or
    // "Error")
    getCAKInstalledProperty(asyncResp, timeoutSeconds);
}

/**
 * @brief Get DOT base resource with properties
 */
inline void getManagerDOT(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& managerId)
{
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaDOTManagement.v1_0_0.NvidiaDOTManagement";
    asyncResp->res.jsonValue["@odata.id"] =
        boost::urls::format("/redfish/v1/Managers/{}/Oem/Nvidia/DOT",
                            BMCWEB_REDFISH_MANAGER_URI_NAME);
    asyncResp->res.jsonValue["Id"] = "DOT";
    asyncResp->res.jsonValue["Name"] = "DOT Key";

    asyncResp->res.jsonValue["CAKDownloadUri"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/DOT/CAKDownload",
        BMCWEB_REDFISH_MANAGER_URI_NAME);

    // Actions
    asyncResp->res.jsonValue["Actions"]["#NvidiaDOTManagement.CAKInstall"]
                            ["target"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/DOT/Actions/NvidiaDOTManagement.CAKInstall",
        BMCWEB_REDFISH_MANAGER_URI_NAME);
    asyncResp->res.jsonValue["Actions"]["#NvidiaDOTManagement.CAKDelete"]
                            ["target"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/DOT/Actions/NvidiaDOTManagement.CAKDelete",
        BMCWEB_REDFISH_MANAGER_URI_NAME);

    // Get Stored property (CAKPresent)
    dbus::utility::getProperty<bool>(
        dotKeydService, dotKeydPath, dotKeydInterface, "Stored",
        [asyncResp](const boost::system::error_code& ec, const bool& stored) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Failed to get Stored property: {}",
                                 ec.message());
                // Optional property, set to false if unavailable
                asyncResp->res.jsonValue["CAKPresent"] = false;
            }
            else
            {
                asyncResp->res.jsonValue["CAKPresent"] = stored;
            }
        });

    // Get CAKVerificationTimeoutInSeconds first, then CAKInstalled
    dbus::utility::getProperty<int32_t>(
        dotKeydService, dotKeydPath, dotKeydInterface,
        "CAKVerificationTimeoutInSeconds",
        [asyncResp](const boost::system::error_code& ec,
                    const int32_t& timeoutSeconds) {
            processCAKVerificationTimeoutProperty(asyncResp, ec,
                                                  timeoutSeconds);
        });
}

namespace dot_cak
{

/*
 * The CAKInstall arguments go to dot-keyd as a D-Bus a{sv} dictionary
 * (dbus::utility::DBusPropertiesMap): CAKKey and LAKKey as a{ss}
 * KeyStructures holding AuthenticationScheme, ECDSAKey and, for the Hybrid
 * scheme, LMSKey; LockDisable as bool; the two minimum security versions as
 * int64. Members are added in alphabetical order. The member values are
 * checked by dot-keyd, which rejects a bad one with
 * xyz.openbmc_project.Common.Error.InvalidArgument naming the member.
 */

/**
 * @brief Add one NvidiaDOT KeyStructure to the dot-keyd arguments
 */
inline void addKeyStructure(
    dbus::utility::DBusPropertiesMap& args, std::string_view member,
    std::string_view authScheme, std::string_view ecdsaKey,
    const std::optional<std::string>& lmsKey)
{
    std::vector<std::pair<std::string, std::string>> key;
    key.emplace_back("AuthenticationScheme", std::string(authScheme));
    key.emplace_back("ECDSAKey", std::string(ecdsaKey));
    if (lmsKey)
    {
        key.emplace_back("LMSKey", *lmsKey);
    }
    args.emplace_back(std::string(member),
                      dbus::utility::DbusVariantType(std::move(key)));
}

} // namespace dot_cak

/**
 * @brief Process the dot-keyd installCak2BmcFs reply
 *
 * A rejection comes back as xyz.openbmc_project.Common.Error.InvalidArgument
 * whose message names the offending member; anything else is internal.
 */
inline void processCAKInstallResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, sdbusplus::message_t& msg)
{
    if (!ec)
    {
        messages::success(asyncResp->res);
        return;
    }
    const sd_bus_error* dbusError = msg.get_error();
    if (dbusError != nullptr && dbusError->name != nullptr &&
        std::string_view(dbusError->name) ==
            "xyz.openbmc_project.Common.Error.InvalidArgument")
    {
        std::string reason =
            dbusError->message != nullptr ? dbusError->message : "";
        BMCWEB_LOG_WARNING("CAKInstall rejected by dot-keyd: {}", reason);
        messages::actionParameterValueError(
            asyncResp->res, nlohmann::json(reason), "CAKInstall");
        return;
    }
    BMCWEB_LOG_ERROR("CAKInstall D-Bus call failed: {}", ec.message());
    messages::internalError(asyncResp->res);
}

/**
 * @brief Handle CAKInstall action
 *
 * The body is parsed against the NvidiaDOTManagement.CAKInstall parameters:
 * CAKKey and the optional LAKKey as NvidiaDOT KeyStructure
 * (AuthenticationScheme, ECDSAKey and, for the Hybrid scheme, LMSKey),
 * LockDisable and the two minimum security versions. Unknown members, missing
 * members and wrong types are rejected here. The members are handed to
 * dot-keyd's installCak2BmcFs as a D-Bus dictionary; dot-keyd owns the key
 * store and validates the values. A rejection there comes back as the D-Bus
 * error xyz.openbmc_project.Common.Error.InvalidArgument whose message names
 * the offending member.
 */
inline void handleCAKInstall(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const crow::Request& req)
{
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }

    std::string cakAuthScheme;
    std::string cakEcdsaKey;
    std::optional<std::string> cakLmsKey;
    std::optional<std::string> lakAuthScheme;
    std::optional<std::string> lakEcdsaKey;
    std::optional<std::string> lakLmsKey;
    bool lockDisable = false;
    int64_t ownerMinimumSecurityVersion = 0;
    int64_t vendorMinimumSecurityVersion = 0;

    if (!redfish::json_util::readJsonAction(                             //
            req, asyncResp->res,                                         //
            "CAKKey/AuthenticationScheme", cakAuthScheme,                //
            "CAKKey/ECDSAKey", cakEcdsaKey,                              //
            "CAKKey/LMSKey", cakLmsKey,                                  //
            "LAKKey/AuthenticationScheme", lakAuthScheme,                //
            "LAKKey/ECDSAKey", lakEcdsaKey,                              //
            "LAKKey/LMSKey", lakLmsKey,                                  //
            "LockDisable", lockDisable,                                  //
            "OwnerMinimumSecurityVersion", ownerMinimumSecurityVersion,  //
            "VendorMinimumSecurityVersion", vendorMinimumSecurityVersion //
            ))
    {
        return;
    }

    dbus::utility::DBusPropertiesMap args;
    dot_cak::addKeyStructure(args, "CAKKey", cakAuthScheme, cakEcdsaKey,
                             cakLmsKey);

    // LAKKey is optional as a whole, but once any of its members is given
    // the KeyStructure needs AuthenticationScheme and ECDSAKey.
    if (lakAuthScheme || lakEcdsaKey || lakLmsKey)
    {
        if (!lakAuthScheme)
        {
            messages::actionParameterMissing(asyncResp->res, "CAKInstall",
                                             "LAKKey/AuthenticationScheme");
            return;
        }
        if (!lakEcdsaKey)
        {
            messages::actionParameterMissing(asyncResp->res, "CAKInstall",
                                             "LAKKey/ECDSAKey");
            return;
        }
        dot_cak::addKeyStructure(args, "LAKKey", *lakAuthScheme, *lakEcdsaKey,
                                 lakLmsKey);
    }
    args.emplace_back("LockDisable",
                      dbus::utility::DbusVariantType(lockDisable));
    args.emplace_back(
        "OwnerMinimumSecurityVersion",
        dbus::utility::DbusVariantType(ownerMinimumSecurityVersion));
    args.emplace_back(
        "VendorMinimumSecurityVersion",
        dbus::utility::DbusVariantType(vendorMinimumSecurityVersion));

    dbus::utility::async_method_call(
        [asyncResp](const boost::system::error_code& ec,
                    sdbusplus::message_t& msg) {
            processCAKInstallResponse(asyncResp, ec, msg);
        },
        dotKeydService, dotKeydPath, dotKeydInterface, "installCak2BmcFs",
        args);
}

inline void processCAKDeleteResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, const std::string& result)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("CAKDelete D-Bus call failed: {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }

    if (result != "Success")
    {
        BMCWEB_LOG_ERROR("CAKDelete failed: {}", result);
        messages::actionParameterValueError(
            asyncResp->res, nlohmann::json(result), "CAKDelete");
        return;
    }

    messages::success(asyncResp->res);
}

/**
 * @brief Handle CAKDelete action
 */
inline void handleCAKDelete(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& managerId)
{
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }

    dbus::utility::async_method_call(
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& result) {
            processCAKDeleteResponse(asyncResp, ec, result);
        },
        dotKeydService, dotKeydPath, dotKeydInterface, "DeleteCak");
}

/**
 * @brief Process CAK download response from DBus property
 */
inline void processCAKDownloadResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, const std::string& cakValue)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Failed to get CAKValue: {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }

    if (cakValue.empty())
    {
        messages::resourceNotFound(asyncResp->res, "CAK", "CAKDownload");
        return;
    }

    asyncResp->res.jsonValue["CAKValue"] = cakValue;
}

/**
 * @brief Handle CAKDownload GET
 */
inline void handleCAKDownload(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }

    // Get CAKValue property (contains full JSON payload)
    dbus::utility::getProperty<std::string>(
        dotKeydService, dotKeydPath, dotKeydInterface, "CAKValue",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& cakValue) {
            processCAKDownloadResponse(asyncResp, ec, cakValue);
        });
}

inline void handleManagerDOTGetRequest(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    getManagerDOT(asyncResp, managerId);
}

inline void handleManagerDOTCAKInstallRequest(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    handleCAKInstall(asyncResp, managerId, req);
}

inline void handleManagerDOTCAKDeleteRequest(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    handleCAKDelete(asyncResp, managerId);
}

inline void handleManagerDOTCAKDownloadRequest(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    handleCAKDownload(asyncResp, managerId);
}

/**
 * @brief Register routes for Manager DOT endpoints
 */
inline void requestRoutesManagerDOT(App& app)
{
    if constexpr (BMCWEB_NVIDIA_DOT_KEYD_SUPPORT)
    {
        BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/Oem/Nvidia/DOT/")
            .privileges(redfish::privileges::getManager)
            .methods(boost::beast::http::verb::get)(
                std::bind_front(handleManagerDOTGetRequest, std::ref(app)));

        BMCWEB_ROUTE(
            app,
            "/redfish/v1/Managers/<str>/Oem/Nvidia/DOT/Actions/NvidiaDOTManagement.CAKInstall/")
            .privileges(redfish::privileges::postManager)
            .methods(boost::beast::http::verb::post)(std::bind_front(
                handleManagerDOTCAKInstallRequest, std::ref(app)));

        BMCWEB_ROUTE(
            app,
            "/redfish/v1/Managers/<str>/Oem/Nvidia/DOT/Actions/NvidiaDOTManagement.CAKDelete/")
            .privileges(redfish::privileges::postManager)
            .methods(boost::beast::http::verb::post)(std::bind_front(
                handleManagerDOTCAKDeleteRequest, std::ref(app)));

        BMCWEB_ROUTE(app,
                     "/redfish/v1/Managers/<str>/Oem/Nvidia/DOT/CAKDownload/")
            .privileges(redfish::privileges::getManager)
            .methods(boost::beast::http::verb::get)(std::bind_front(
                handleManagerDOTCAKDownloadRequest, std::ref(app)));
    }
    else
    {
        (void)app; // Suppress unused parameter warning when feature is disabled
    }
}

} // namespace redfish
