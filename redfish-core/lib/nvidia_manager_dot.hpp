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

#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <tuple>

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

/**
 * @brief Handle CAKInstall action
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
    int64_t vendorMinimumSecurityVersion = 0;
    int64_t ownerMinimumSecurityVersion = 0;
    bool lockDisable = false;
    std::optional<std::string> lakAuthScheme;
    std::optional<std::string> lakEcdsaKey;

    nlohmann::json jsonRequest;
    if (!redfish::json_util::processJsonFromRequest(asyncResp->res, req,
                                                    jsonRequest))
    {
        return;
    }

    if (!redfish::json_util::readJson(
            jsonRequest, asyncResp->res, "CAKKey/AuthenticationScheme",
            cakAuthScheme, "CAKKey/ECDSAKey", cakEcdsaKey,
            "VendorMinimumSecurityVersion", vendorMinimumSecurityVersion,
            "OwnerMinimumSecurityVersion", ownerMinimumSecurityVersion,
            "LockDisable", lockDisable, "LAKKey/AuthenticationScheme",
            lakAuthScheme, "LAKKey/ECDSAKey", lakEcdsaKey))
    {
        return;
    }

    if (cakAuthScheme.empty() || cakEcdsaKey.empty())
    {
        messages::actionParameterValueFormatError(
            asyncResp->res,
            nlohmann::json{
                {"AuthenticationScheme", cakAuthScheme},
                {"ECDSAKey", cakEcdsaKey},
            },
            "CAKKey", "CAKInstall");
        return;
    }

    if (lakAuthScheme.has_value() != lakEcdsaKey.has_value())
    {
        messages::actionParameterMissing(
            asyncResp->res, "CAKInstall",
            lakAuthScheme ? "LAKKey/ECDSAKey" : "LAKKey/AuthenticationScheme");
        return;
    }

    bool hasLakKey = false;
    std::tuple<std::string, std::string> lakKeyStruct{"", ""};

    if (lakAuthScheme && lakEcdsaKey)
    {
        if (lakAuthScheme->empty() || lakEcdsaKey->empty())
        {
            messages::actionParameterValueFormatError(
                asyncResp->res,
                nlohmann::json{
                    {"AuthenticationScheme", *lakAuthScheme},
                    {"ECDSAKey", *lakEcdsaKey},
                },
                "LAKKey", "CAKInstall");
            return;
        }
        hasLakKey = true;
        lakKeyStruct = std::make_tuple(*lakAuthScheme, *lakEcdsaKey);
    }

    // Call D-Bus method installCak2BmcFs
    dbus::utility::async_method_call(
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& result) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("CAKInstall D-Bus call failed: {}",
                                 ec.message());
                messages::internalError(asyncResp->res);
                return;
            }

            if (result != "Success")
            {
                BMCWEB_LOG_ERROR("CAKInstall failed: {}", result);
                messages::actionParameterValueError(
                    asyncResp->res, nlohmann::json(result), "CAKInstall");
                return;
            }

            messages::success(asyncResp->res);
        },
        dotKeydService, dotKeydPath, dotKeydInterface, "installCak2BmcFs",
        std::make_tuple(cakAuthScheme, cakEcdsaKey), lockDisable,
        vendorMinimumSecurityVersion, ownerMinimumSecurityVersion, hasLakKey,
        lakKeyStruct);
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
        BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/Oem/Nvidia/DOT")
            .privileges(redfish::privileges::getManager)
            .methods(boost::beast::http::verb::get)(
                std::bind_front(handleManagerDOTGetRequest, std::ref(app)));

        BMCWEB_ROUTE(
            app,
            "/redfish/v1/Managers/<str>/Oem/Nvidia/DOT/Actions/NvidiaDOTManagement.CAKInstall")
            .privileges(redfish::privileges::postManager)
            .methods(boost::beast::http::verb::post)(std::bind_front(
                handleManagerDOTCAKInstallRequest, std::ref(app)));

        BMCWEB_ROUTE(
            app,
            "/redfish/v1/Managers/<str>/Oem/Nvidia/DOT/Actions/NvidiaDOTManagement.CAKDelete")
            .privileges(redfish::privileges::postManager)
            .methods(boost::beast::http::verb::post)(std::bind_front(
                handleManagerDOTCAKDeleteRequest, std::ref(app)));

        BMCWEB_ROUTE(app,
                     "/redfish/v1/Managers/<str>/Oem/Nvidia/DOT/CAKDownload")
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
