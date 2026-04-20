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
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "error_messages.hpp"
#include "http_request.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/json_utils.hpp"
#include "utils/sw_utils.hpp"

#include <boost/beast/http/verb.hpp>

#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <string>

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
    dbus::utility::getDbusObject(
        biosConfigObj, std::array<std::string_view, 1>{biosConfigIface},
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

            dbus::utility::async_method_call(
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
        });
}

/**
 * Set ClearNonVolatileVariables.Clear to requested value
 */
inline void setClearVariables(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& path, const bool requestToClear)
{
    dbus::utility::setProperty(
        service, path,
        "xyz.openbmc_project.Control.Boot.ClearNonVolatileVariables", "Clear",
        requestToClear,
        [aResp, path, service](const boost::system::error_code& ec) {
            if (!ec)
            {
                BMCWEB_LOG_DEBUG("Set ClearUefiVariable successed");
                return;
            }

            BMCWEB_LOG_DEBUG("Set ClearUefiVariable failed: {}", ec.what());
            messages::internalError(aResp->res);
        });
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

            dbus::utility::getProperty<bool>(
                secureService, closestSecurePath,
                "xyz.openbmc_project.State.Decorator.SecureState", "secure",
                [aResp, secure, requestToClear, clearService,
                 clearPath](const boost::system::error_code& ec,
                            const bool& secureState) {
                    if (ec)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }

                    if ((secureState && secure == SecureSelector::secure) ||
                        (!secureState && secure == SecureSelector::nonSecure))
                    {
                        setClearVariables(aResp, clearService, clearPath,
                                          requestToClear);
                    }
                });
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

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/control", 0,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.State.Decorator.SecureState"},
        [aResp, secure, requestToClear,
         clearSubtree](const boost::system::error_code& ec,
                       const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                // No state sensors attached.
                messages::internalError(aResp->res);
                return;
            }

            handleClearSecureStateSubtree(aResp, secure, requestToClear,
                                          clearSubtree, subtree);
        });
}

inline void clearVariables(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const SecureSelector secure,
                           const bool requestToClear)
{
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/control", 0,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Control.Boot.ClearNonVolatileVariables"},
        [aResp, secure, requestToClear](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                // No state sensors attached.
                messages::internalError(aResp->res);
                return;
            }

            handleClearNonVolatileVariablesSubtree(aResp, secure,
                                                   requestToClear, subtree);
        });

    dbus::utility::setProperty(
        "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/control/host0/boot",
        "xyz.openbmc_project.Control.Boot.Flags", "CMOSClear", true,
        [aResp](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                messages::internalError(aResp->res);
                return;
            }
            BMCWEB_LOG_DEBUG("Boot override CMOSClear update done.");
        });

    dbus::utility::setProperty(
        "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/control/host0/boot",
        "xyz.openbmc_project.Object.Enable", "Enabled", true,
        [aResp](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                messages::internalError(aResp->res);
                return;
            }
            BMCWEB_LOG_DEBUG("Boot override enable update done.");
        });
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

/**
 * BiosAttributeRegistry DB for DPU bios managment
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static nlohmann::json biosRegistryJson;

const std::string biosRegistryJsonFileName =
    "/var/lib/bmcweb/BiosRegistryJson.json";

/**
 * BiosService DBus types
 */
using BaseBIOSTable = boost::container::flat_map<
    std::string,
    std::tuple<
        std::string, bool, std::string, std::string, std::string,
        std::variant<int64_t, std::string, bool>,
        std::variant<int64_t, std::string, bool>,
        std::vector<std::tuple<std::string, std::variant<int64_t, std::string>,
                               std::string>>>>;

using BaseBIOSTableItem = std::pair<
    std::string,
    std::tuple<
        std::string, bool, std::string, std::string, std::string,
        std::variant<int64_t, std::string, bool>,
        std::variant<int64_t, std::string, bool>,
        std::vector<std::tuple<std::string, std::variant<int64_t, std::string>,
                               std::string>>>>;

using PendingAttrType = boost::container::flat_map<
    std::string,
    std::tuple<std::string, std::variant<int64_t, std::string, bool>>>;

using PendingAttrItemType = std::pair<
    std::string,
    std::tuple<std::string, std::variant<int64_t, std::string, bool>>>;

using AttrBoundType =
    std::tuple<std::string, std::variant<int64_t, std::string>, std::string>;

enum BaseBiosTableIndex
{
    baseBiosAttrType = 0,
    baseBiosReadonlyStatus,
    baseBiosDisplayName,
    baseBiosDescription,
    baseBiosMenuPath,
    baseBiosCurrValue,
    baseBiosDefaultValue,
    baseBiosBoundValues
};

enum BaseBiosBoundIndex
{
    baseBiosBoundType = 0,
    baseBiosBoundValue
};

enum BiosPendingAttributesIndex
{
    biosPendingAttrType = 0,
    biosPendingAttrValue
};

/**
 *@brief Translates Base BIOS Table attribute type from DBUS property value to
 *Redfish string type.
 *
 *@param[in] attrType The DBUS BIOS attribute type value
 *
 *@return Returns as a string, the attribute type required for Redfish.
 *If attribute type didn't match, then returns 'UNKNOWN' string.
 */
static std::string getBiosAttrType(const std::string& attrType)
{
    std::string type;
    if (attrType ==
        "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Enumeration")
    {
        type = "Enumeration";
    }
    else if (attrType ==
             "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.String")
    {
        type = "String";
    }
    else if (attrType ==
             "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Password")
    {
        type = "Password";
    }
    else if (attrType ==
             "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Integer")
    {
        type = "Integer";
    }
    else if (attrType ==
             "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Boolean")
    {
        type = "Boolean";
    }
    else
    {
        type = "UNKNOWN";
    }
    return type;
}

/**
 *@brief Translates Base BIOS Table attribute type from Redfish string type to
 *DBUS property value.
 *
 *@param[in] attrType The Redfish BIOS attribute string type value
 *
 *@return Returns as a string, the attribute type required for DBUS.
 *If attribute type didn't match, then returns 'UNKNOWN' string.
 */
static std::string getDbusBiosAttrType(const std::string& attrType)
{
    std::string type;
    if (attrType == "Enumeration")
    {
        type =
            "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Enumeration";
    }
    else if (attrType == "String")
    {
        type = "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.String";
    }
    else if (attrType == "Password")
    {
        type = "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Password";
    }
    else if (attrType == "Integer")
    {
        type = "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Integer";
    }
    else if (attrType == "Boolean")
    {
        type = "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Boolean";
    }
    else
    {
        type = "UNKNOWN";
    }
    return type;
}

/**
 *@brief Translates Base BIOS Table attribute bound value type from DBUS
 *property value to Redfish string type.
 *
 *@param[in] attrType The DBUS BIOS Bound value attribute type value
 *
 *@return Returns as a string, the attribute bound value type required for
 *Redfish. If attribute type didn't match, then returns 'UNKNOWN' string.
 */
static std::string getBiosBoundValType(const std::string& boundValType)
{
    std::string type;
    if (boundValType ==
        "xyz.openbmc_project.BIOSConfig.Manager.BoundType.ScalarIncrement")
    {
        type = "ScalarIncrement";
    }
    else if (boundValType ==
             "xyz.openbmc_project.BIOSConfig.Manager.BoundType.LowerBound")
    {
        type = "LowerBound";
    }
    else if (boundValType ==
             "xyz.openbmc_project.BIOSConfig.Manager.BoundType.UpperBound")
    {
        type = "UpperBound";
    }
    else if (boundValType ==
             "xyz.openbmc_project.BIOSConfig.Manager.BoundType.OneOf")
    {
        type = "OneOf";
    }
    else if (boundValType ==
             "xyz.openbmc_project.BIOSConfig.Manager.BoundType.MinStringLength")
    {
        type = "MinStringLength";
    }
    else if (boundValType ==
             "xyz.openbmc_project.BIOSConfig.Manager.BoundType.MaxStringLength")
    {
        type = "MaxStringLength";
    }
    else
    {
        type = "UNKNOWN";
    }
    return type;
}

/**
 *@brief Translates Reset BIOS to Default Settings status type from DBUS
 *property value to Redfish string type.
 *
 *@param[in] biosMode The DBUS BIOS Reset BIOS to Default Setting status value
 *
 *@return Returns as a string, the Reset BIOS Settings to default type required
 *for Redfish. If attribute type didn't match, then returns 'UNKNOWN' string.
 */
static std::string getBiosDefaultSettingsMode(const std::string& biosMode)
{
    std::string mode;
    if (biosMode == "xyz.openbmc_project.BIOSConfig.Manager.ResetFlag.NoAction")
    {
        mode = "NoAction";
    }
    else if (biosMode ==
             "xyz.openbmc_project.BIOSConfig.Manager.ResetFlag.FactoryDefaults")
    {
        mode = "FactoryDefaults";
    }
    else if (
        biosMode ==
        "xyz.openbmc_project.BIOSConfig.Manager.ResetFlag.FailSafeDefaults")
    {
        mode = "FailSafeDefaults";
    }
    else
    {
        mode = "UNKNOWN";
    }
    return mode;
}

/**
 *@brief Reads the Reset BIOS Settings to default property.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 *
 * @return None.
 */
inline void getResetBiosSettings(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    BMCWEB_LOG_DEBUG("Get Reset Bios Settings to Defaults Pending Status");
    dbus::utility::getDbusObject(
        biosConfigObj, std::array<std::string_view, 1>{biosConfigIface},
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetObject& objType) {
            if (ec || objType.empty())
            {
                BMCWEB_LOG_DEBUG("GetObject for path {}", biosConfigObj);
                return;
            }

            const std::string& biosService = objType.begin()->first;

            dbus::utility::getProperty<std::string>(
                biosService, biosConfigObj, biosConfigIface,
                "ResetBIOSSettings",
                [asyncResp](const boost::system::error_code& ec2,
                            const std::string& resetBiosSettingsMode) {
                    if (ec2)
                    {
                        BMCWEB_LOG_DEBUG(
                            "DBUS response error for "
                            "Get Reset BIOS setting to default status.");
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    std::string biosMode =
                        getBiosDefaultSettingsMode(resetBiosSettingsMode);

                    if (biosMode == "NoAction")
                    {
                        asyncResp->res.jsonValue["ResetBiosToDefaultsPending"] =
                            false;
                    }
                    else if ((biosMode == "FactoryDefaults") ||
                             (biosMode == "FailSafeDefaults"))
                    {
                        asyncResp->res.jsonValue["ResetBiosToDefaultsPending"] =
                            true;
                    }
                    else
                    {
                        BMCWEB_LOG_DEBUG("Invalid Reset BIOS Settings Status");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                });
        });
}

/**
 *@brief Reads the BIOS Base Table DBUS property and update the Bios Attributes
 *response.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 *
 * @return None.
 */
inline void getBiosAttributes(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::getDbusObject(
        biosConfigObj, std::array<std::string_view, 1>{biosConfigIface},
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetObject& objType) {
            if (ec || objType.empty())
            {
                BMCWEB_LOG_DEBUG("GetObject for path {}", biosConfigObj);
                return;
            }

            const std::string& biosService = objType.begin()->first;
            dbus::utility::getProperty<BaseBIOSTable>(
                biosService, biosConfigObj, biosConfigIface, "BaseBIOSTable",
                [asyncResp](const boost::system::error_code& ec2,
                            const BaseBIOSTable& baseBiosTable) {
                    if (ec2)
                    {
                        BMCWEB_LOG_ERROR(
                            "Get BaseBIOSTable DBus response error{}", ec2);
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    nlohmann::json& attributesJson =
                        asyncResp->res.jsonValue["Attributes"];
                    for (const BaseBIOSTableItem& attrIt : baseBiosTable)
                    {
                        const std::string& attr = attrIt.first;

                        // read the attribute type at 0th field and convert from
                        // dbus to string format
                        std::string attrType = getBiosAttrType(std::string(
                            std::get<BaseBiosTableIndex::baseBiosAttrType>(
                                attrIt.second)));
                        if ((attrType == "String") ||
                            (attrType == "Enumeration"))
                        {
                            // read the current value of attribute at 5th field
                            const std::string* attrCurrValue =
                                std::get_if<std::string>(
                                    &std::get<
                                        BaseBiosTableIndex::baseBiosCurrValue>(
                                        attrIt.second));
                            if (attrCurrValue != nullptr)
                            {
                                attributesJson.emplace(attr, *attrCurrValue);
                            }
                            else
                            {
                                attributesJson.emplace(attr, std::string(""));
                            }
                        }
                        else if ((attrType == "Integer") ||
                                 (attrType == "Boolean"))
                        {
                            // read the current value of attribute at 5th field
                            const int64_t* attrCurrValue = std::get_if<int64_t>(
                                &std::get<
                                    BaseBiosTableIndex::baseBiosCurrValue>(
                                    attrIt.second));
                            if (attrCurrValue != nullptr)
                            {
                                if (attrType == "Boolean")
                                {
                                    if (*attrCurrValue != 0)
                                    {
                                        attributesJson.emplace(attr, true);
                                    }
                                    else
                                    {
                                        attributesJson.emplace(attr, false);
                                    }
                                }
                                else
                                {
                                    attributesJson.emplace(attr,
                                                           *attrCurrValue);
                                }
                            }
                            else
                            {
                                if (attrType == "Boolean")
                                {
                                    attributesJson.emplace(attr, false);
                                }
                                else
                                {
                                    attributesJson.emplace(attr, 0);
                                }
                            }
                        }
                        else
                        {
                            BMCWEB_LOG_ERROR("Attribute type not supported");
                        }
                    }
                });
        });
}

/**
 *@brief Validates the requested BIOS Base Table JSON with the required
 *attribute format.
 *
 * @param[in]	    attrJson	BIOS Attribute JSON
 * @param[in,out]   asyncResp   Async HTTP response.
 *
 * @return Returns as a bool flag, true if attribute json is in valid format,
 * or else returns false.
 */
static bool isValidAttrJson(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const nlohmann::json& attrJson)
{
    const std::vector<std::string> stringRequired{
        "AttributeName", "DisplayName", "Description", "MenuPath", "Type"};
    const std::vector<std::string> booleanRequired{"ReadOnly"};
    const std::vector<std::string> valueTypeRequired{"CurrentValue",
                                                     "DefaultValue"};
    const std::vector<std::string> integerAddition{"LowerBound", "UpperBound",
                                                   "ScalarIncrement"};
    const std::vector<std::string> stringAddition{"MinLength", "MaxLength"};
    const std::string enumerationAddition{"Values"};

    // checking existence of required keys
    for (const auto& key : stringRequired)
    {
        if (!attrJson.contains(key))
        {
            messages::propertyMissing(asyncResp->res, key);
            BMCWEB_LOG_ERROR("Required propery missing in req!");
            return false;
        }
        if (!attrJson[key].is_string())
        {
            messages::propertyValueTypeError(asyncResp->res,
                                             attrJson[key].dump(), key);
            BMCWEB_LOG_ERROR("Attribute type is not valid in req!");
            return false;
        }
    }

    for (const auto& key : booleanRequired)
    {
        if (!attrJson.contains(key))
        {
            messages::propertyMissing(asyncResp->res, key);
            BMCWEB_LOG_ERROR("Required propery missing in req!");
            return false;
        }
        if (!attrJson[key].is_boolean())
        {
            messages::propertyValueTypeError(asyncResp->res,
                                             attrJson[key].dump(), key);
            BMCWEB_LOG_ERROR("Attribute type is not valid in req!");
            return false;
        }
    }

    for (const auto& key : valueTypeRequired)
    {
        if (!attrJson.contains(key))
        {
            messages::propertyMissing(asyncResp->res, key);
            BMCWEB_LOG_ERROR("Required propery missing in req!");
            return false;
        }

        bool propertyValueTypeValid = false;
        if ((attrJson["Type"] == "Enumeration" && attrJson[key].is_string()) ||
            (attrJson["Type"] == "String" && attrJson[key].is_string()) ||
            (attrJson["Type"] == "Integer" && attrJson[key].is_number()) ||
            (attrJson["Type"] == "Boolean" && attrJson[key].is_boolean()) ||
            (key == "DefaultValue" && attrJson[key].is_null()))
        {
            propertyValueTypeValid = true;
        }

        if (!propertyValueTypeValid)
        {
            messages::propertyValueTypeError(asyncResp->res,
                                             attrJson[key].dump(), key);
            BMCWEB_LOG_ERROR("Attribute type is not valid in req!");
            return false;
        }
    }

    if (attrJson["Type"] == "Integer")
    {
        for (const auto& key : integerAddition)
        {
            if (!attrJson.contains(key))
            {
                messages::propertyMissing(asyncResp->res, key);
                BMCWEB_LOG_ERROR("Required propery missing in req!");
                return false;
            }
            if (!attrJson[key].is_number())
            {
                messages::propertyValueTypeError(asyncResp->res,
                                                 attrJson[key].dump(), key);
                BMCWEB_LOG_ERROR("Attribute type is not valid in req!");
                return false;
            }
        }
    }

    if (attrJson["Type"] == "String")
    {
        for (const auto& key : stringAddition)
        {
            if (!attrJson.contains(key))
            {
                messages::propertyMissing(asyncResp->res, key);
                BMCWEB_LOG_ERROR("Required propery missing in req!");
                return false;
            }
            if (!attrJson[key].is_number())
            {
                messages::propertyValueTypeError(asyncResp->res,
                                                 attrJson[key].dump(), key);
                BMCWEB_LOG_ERROR("Attribute type is not valid in req!");
                return false;
            }
        }
    }

    if (attrJson["Type"] == "Enumeration")
    {
        const auto& key = enumerationAddition;
        if (!attrJson.contains(key))
        {
            messages::propertyMissing(asyncResp->res, key);
            BMCWEB_LOG_ERROR("Required propery missing in req!");
            return false;
        }
        if (!attrJson[key].is_array())
        {
            messages::propertyValueTypeError(asyncResp->res,
                                             attrJson[key].dump(), key);
            BMCWEB_LOG_ERROR("Attribute type is not valid in req!");
            return false;
        }
        if (attrJson[key].empty())
        {
            messages::propertyValueIncorrect(asyncResp->res, key,
                                             attrJson[key].dump());
            BMCWEB_LOG_ERROR("Attribute type is not valid in req!");
            return false;
        }
        if (!attrJson[key][0].is_string())
        {
            messages::propertyValueIncorrect(asyncResp->res, key,
                                             attrJson[key].dump());
            BMCWEB_LOG_ERROR("Attribute type is not valid in req!");
            return false;
        }
    }

    if (attrJson["AttributeName"].empty())
    {
        messages::propertyValueIncorrect(asyncResp->res, "AttributeName",
                                         "empty");
        BMCWEB_LOG_ERROR("AttributeName is not valid in req!");
        return false;
    }
    return true;
}

/**
 *@brief Sets the BIOS Base Table DBUS property with requested BIOS default
 *attributes.
 *
 * @param[in]	    baseBiosTableJson BIOS Base Table default Attribute details
 * @param[in,out]   asyncResp   Async HTTP response.
 *
 * @return Returns None.
 */
inline void fillBiosTable(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::vector<nlohmann::json>& baseBiosTableJson)
{
    BaseBIOSTable baseBiosTable;
    for (const nlohmann::json& attrJson : baseBiosTableJson)
    {
        // Check all the fields are present
        if (!isValidAttrJson(asyncResp, attrJson))
        {
            BMCWEB_LOG_ERROR("Req attributes are missing!");
            return;
        }

        std::string attr;
        std::string attrDispName;
        std::string attrDescr;
        std::string attrMenuPath;
        std::string attrType;
        bool attrReadOnly = false;
        std::vector<std::tuple<std::string, std::variant<int64_t, std::string>,
                               std::string>>
            attrValues;

        attr = attrJson["AttributeName"].get<std::string>();
        attrDispName = attrJson["DisplayName"].get<std::string>();
        attrDescr = attrJson["Description"].get<std::string>();
        attrMenuPath = attrJson["MenuPath"].get<std::string>();
        attrType = attrJson["Type"].get<std::string>();
        attrReadOnly = attrJson["ReadOnly"].get<bool>();

        if ((attrType == "String") || (attrType == "Enumeration"))
        {
            std::string currVal = attrJson["CurrentValue"].get<std::string>();

            // read and update the bound values
            if (attrType == "Enumeration")
            {
                for (const auto& value :
                     attrJson["Values"].get<std::vector<std::string>>())
                {
                    attrValues.emplace_back(
                        "xyz.openbmc_project.BIOSConfig.Manager.BoundType.OneOf",
                        value, "");
                }
            }
            else if (attrType == "String")
            {
                const auto& minLength = attrJson["MinLength"].get<int64_t>();
                attrValues.emplace_back(
                    "xyz.openbmc_project.BIOSConfig.Manager.BoundType.MinStringLength",
                    minLength, "");
                const auto& maxLength = attrJson["MaxLength"].get<int64_t>();
                attrValues.emplace_back(
                    "xyz.openbmc_project.BIOSConfig.Manager.BoundType.MaxStringLength",
                    maxLength, "");
            }
            attrType = getDbusBiosAttrType(attrType);
            if (attrJson["DefaultValue"].is_null())
            {
                // put a incorrect type to indicate null
                int64_t defaultVal = 0;
                baseBiosTable.insert(std::make_pair(
                    attr, std::make_tuple(attrType, attrReadOnly, attrDispName,
                                          attrDescr, attrMenuPath, currVal,
                                          defaultVal, attrValues)));
            }
            else
            {
                std::string defaultVal =
                    attrJson["DefaultValue"].get<std::string>();
                baseBiosTable.insert(std::make_pair(
                    attr, std::make_tuple(attrType, attrReadOnly, attrDispName,
                                          attrDescr, attrMenuPath, currVal,
                                          defaultVal, attrValues)));
            }
        }
        else if (attrType == "Integer")
        {
            int64_t currVal = attrJson["CurrentValue"].get<int64_t>();

            // read and update the bound values
            attrValues.emplace_back(
                "xyz.openbmc_project.BIOSConfig.Manager.BoundType.LowerBound",
                attrJson["LowerBound"].get<int64_t>(), "");
            attrValues.emplace_back(
                "xyz.openbmc_project.BIOSConfig.Manager.BoundType.UpperBound",
                attrJson["UpperBound"].get<int64_t>(), "");
            attrValues.emplace_back(
                "xyz.openbmc_project.BIOSConfig.Manager.BoundType.ScalarIncrement",
                attrJson["ScalarIncrement"].get<int64_t>(), "");

            attrType = getDbusBiosAttrType(attrType);
            if (attrJson["DefaultValue"].is_null())
            {
                // put a incorrect type to indicate null
                std::string defaultVal;
                baseBiosTable.insert(std::make_pair(
                    attr, std::make_tuple(attrType, attrReadOnly, attrDispName,
                                          attrDescr, attrMenuPath, currVal,
                                          defaultVal, attrValues)));
            }
            else
            {
                int64_t defaultVal = attrJson["DefaultValue"].get<int64_t>();
                baseBiosTable.insert(std::make_pair(
                    attr, std::make_tuple(attrType, attrReadOnly, attrDispName,
                                          attrDescr, attrMenuPath, currVal,
                                          defaultVal, attrValues)));
            }
        }
        else if (attrType == "Boolean")
        {
            // for Boolean type, BaseBIOSTable DBus method will expect the data
            // in the int64_t type
            int64_t currVal =
                static_cast<int64_t>(attrJson["CurrentValue"].get<bool>());
            attrType = getDbusBiosAttrType(attrType);
            if (attrJson["DefaultValue"].is_null())
            {
                // put a incorrect type to indicate null
                std::string defaultVal;
                baseBiosTable.insert(std::make_pair(
                    attr, std::make_tuple(attrType, attrReadOnly, attrDispName,
                                          attrDescr, attrMenuPath, currVal,
                                          defaultVal, attrValues)));
            }
            else
            {
                int64_t defaultVal =
                    static_cast<int64_t>(attrJson["DefaultValue"].get<bool>());
                baseBiosTable.insert(std::make_pair(
                    attr, std::make_tuple(attrType, attrReadOnly, attrDispName,
                                          attrDescr, attrMenuPath, currVal,
                                          defaultVal, attrValues)));
            }
        }
        else
        {
            messages::propertyValueIncorrect(asyncResp->res, "Type", "UNKNOWN");
            BMCWEB_LOG_ERROR("Attribute Type is not valid in req!");
            return;
        }
    }

    dbus::utility::setProperty(
        "xyz.openbmc_project.BIOSConfigManager",
        "/xyz/openbmc_project/bios_config/manager",
        "xyz.openbmc_project.BIOSConfig.Manager", "BaseBIOSTable",
        baseBiosTable,
        [asyncResp, baseBiosTable](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Error occurred in setting BaseBIOSTable");
                messages::internalError(asyncResp->res);
                return;
            }

            messages::success(asyncResp->res);
        });
}

/**
 *@brief Reads the BIOS Pending Attributes, which are updated by oob the user
 * and update the Bios Settings Attributes response.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 *
 * @return None.
 */
inline void getBiosSettingsAttr(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::getDbusObject(
        biosConfigObj, std::array<std::string_view, 1>{biosConfigIface},
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetObject& objType) {
            if (ec || objType.empty())
            {
                BMCWEB_LOG_DEBUG("GetObject for path {}", biosConfigObj);
                return;
            }

            const std::string& biosService = objType.begin()->first;
            dbus::utility::getProperty<PendingAttrType>(
                biosService, biosConfigObj, biosConfigIface,
                "PendingAttributes",
                [asyncResp](const boost::system::error_code& ec2,
                            const PendingAttrType& pendingAttrs) {
                    if (ec2)
                    {
                        BMCWEB_LOG_ERROR(
                            "Get PendingAttributes DBus response error{}", ec2);
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    nlohmann::json& attributesJson =
                        asyncResp->res.jsonValue["Attributes"];

                    for (const PendingAttrItemType& attrIt : pendingAttrs)
                    {
                        const std::string& attr = attrIt.first;

                        // read the attribute type at 0th field and convert from
                        // dbus to string format
                        std::string attrType = getBiosAttrType(std::string(
                            std::get<BiosPendingAttributesIndex::
                                         biosPendingAttrType>(attrIt.second)));
                        if ((attrType == "String") ||
                            (attrType == "Enumeration"))
                        {
                            // read the current value of attribute at 1st field
                            const std::string* attrCurrValue =
                                std::get_if<std::string>(
                                    &std::get<BiosPendingAttributesIndex::
                                                  biosPendingAttrValue>(
                                        attrIt.second));
                            if (attrCurrValue != nullptr)
                            {
                                attributesJson.emplace(attr, *attrCurrValue);
                            }
                            else
                            {
                                attributesJson.emplace(attr, std::string(""));
                            }
                        }
                        else if ((attrType == "Integer") ||
                                 (attrType == "Boolean"))
                        {
                            // read the current value of attribute at 1st field
                            const int64_t* attrCurrValue = std::get_if<int64_t>(
                                &std::get<BiosPendingAttributesIndex::
                                              biosPendingAttrValue>(
                                    attrIt.second));
                            if (attrCurrValue != nullptr)
                            {
                                if (attrType == "Boolean")
                                {
                                    if (*attrCurrValue != 0)
                                    {
                                        attributesJson.emplace(attr, true);
                                    }
                                    else
                                    {
                                        attributesJson.emplace(attr, false);
                                    }
                                }
                                else
                                {
                                    attributesJson.emplace(attr,
                                                           *attrCurrValue);
                                }
                            }
                            else
                            {
                                if (attrType == "Boolean")
                                {
                                    attributesJson.emplace(attr, false);
                                }
                                else
                                {
                                    attributesJson.emplace(attr, 0);
                                }
                            }
                        }
                        else
                        {
                            BMCWEB_LOG_ERROR("Attribute type not supported");
                        }
                    }
                });
        });
}

/**
 *@brief
 *  1- Updates the BIOS Pending Attributes DBUS property, which are requested
 *     by the oob user.
 *  2- Updates the BIOS Attributes table DBUS property and clean BIOS Pending
 *      Attributes DBUS property , which are requested
 *     by the UEFI user.
 *
 * @param[in]	    pendingAttrJson BIOS Base Table pending Attribute details
 * @param[in]       biosFlag True  - updates BIOS Attributes table (2)
 *                            False - updates BIOS Pending Attributes (1)
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 *
 * @return None.
 */
inline void setBiosCurrentOrPendingAttr(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const nlohmann::json& pendingAttrJson, bool biosFlag)
{
    dbus::utility::getDbusObject(
        biosConfigObj, std::array<std::string_view, 1>{biosConfigIface},
        [asyncResp, pendingAttrJson,
         biosFlag](const boost::system::error_code& ec,
                   const dbus::utility::MapperGetObject& objType) {
            if (ec || objType.empty())
            {
                BMCWEB_LOG_DEBUG("GetObject for path {}", biosConfigObj);
                return;
            }

            const std::string& biosService = objType.begin()->first;
            dbus::utility::getProperty<BaseBIOSTable>(
                biosService, biosConfigObj, biosConfigIface, "BaseBIOSTable",
                [asyncResp, pendingAttrJson, biosService,
                 biosFlag](const boost::system::error_code& ec2,
                           const BaseBIOSTable& baseBiosTableResp) {
                    if (ec2)
                    {
                        BMCWEB_LOG_ERROR(
                            "Get BaseBIOSTable DBus response error{}", ec2);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    auto baseBiosTable =
                        std::make_shared<BaseBIOSTable>(baseBiosTableResp);
                    PendingAttrType pendingAttrs{};
                    for (const auto& pendingAttrIt : pendingAttrJson.items())
                    {
                        // Check whether the requested attribute is available
                        // inside BaseBIOSTable or not
                        auto attrIt = baseBiosTable->find(pendingAttrIt.key());
                        if (attrIt == baseBiosTable->end())
                        {
                            BMCWEB_LOG_ERROR("Not Found Attribute {}",
                                             pendingAttrIt.key());
                            messages::propertyValueNotInList(
                                asyncResp->res, pendingAttrIt.key(),
                                "Attributes");
                            return;
                        }

                        // read the attribute type at 0th field and convert from
                        // dbus to string format
                        std::string attrItType =
                            std::get<BaseBiosTableIndex::baseBiosAttrType>(
                                attrIt->second);
                        std::string attrType = getBiosAttrType(attrItType);
                        if ((attrType == "String") ||
                            (attrType == "Enumeration"))
                        {
                            if (!pendingAttrIt.value().is_string())
                            {
                                BMCWEB_LOG_ERROR(
                                    "Requested Attribute Value invalid");
                                messages::propertyValueTypeError(
                                    asyncResp->res,
                                    std::string(pendingAttrIt.value()),
                                    pendingAttrIt.key());
                                return;
                            }
                            std::string attrReqVal =
                                pendingAttrIt.value().get<std::string>();

                            if (attrType == "Enumeration")
                            {
                                // read the bound values for the attribute
                                const std::vector<AttrBoundType> boundValues =
                                    std::get<BaseBiosTableIndex::
                                                 baseBiosBoundValues>(
                                        attrIt->second);
                                bool found = false;

                                for (const AttrBoundType& boundValueIt :
                                     boundValues)
                                {
                                    // read the bound value type at 0th field
                                    // and convert from dbus to string format
                                    std::string boundValType =
                                        getBiosBoundValType(std::string(
                                            std::get<BaseBiosBoundIndex::
                                                         baseBiosBoundType>(
                                                boundValueIt)));

                                    if (boundValType == "OneOf")
                                    {
                                        // read the bound value  at 1st field
                                        // for each entry
                                        const std::string* currBoundVal =
                                            std::get_if<std::string>(
                                                &std::get<
                                                    BaseBiosBoundIndex::
                                                        baseBiosBoundValue>(
                                                    boundValueIt));
                                        if (currBoundVal == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Bound Value not found");
                                            continue;
                                        }
                                        if (attrReqVal == *currBoundVal)
                                        {
                                            found = true;
                                        }
                                    }
                                    else
                                    {
                                        continue;
                                    }
                                }

                                if (!found)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "Requested Attribute Value invalid");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                            }
                            else if (attrType == "String")
                            {
                                const std::vector<AttrBoundType> boundValues =
                                    std::get<BaseBiosTableIndex::
                                                 baseBiosBoundValues>(
                                        attrIt->second);
                                bool valid = true;

                                for (const AttrBoundType& boundValueIt :
                                     boundValues)
                                {
                                    // read the bound value type at 0th field
                                    // and convert from dbus to string format
                                    std::string boundValType =
                                        getBiosBoundValType(std::string(
                                            std::get<BaseBiosBoundIndex::
                                                         baseBiosBoundType>(
                                                boundValueIt)));

                                    if (boundValType == "MinStringLength")
                                    {
                                        const int64_t* currBoundVal =
                                            std::get_if<int64_t>(
                                                &std::get<
                                                    BaseBiosBoundIndex::
                                                        baseBiosBoundValue>(
                                                    boundValueIt));
                                        if (currBoundVal == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Bound Value not found");
                                            continue;
                                        }
                                        if (static_cast<int64_t>(
                                                attrReqVal.size()) <
                                            *currBoundVal)
                                        {
                                            valid = false;
                                        }
                                    }
                                    else if (boundValType == "MaxStringLength")
                                    {
                                        const int64_t* currBoundVal =
                                            std::get_if<int64_t>(
                                                &std::get<
                                                    BaseBiosBoundIndex::
                                                        baseBiosBoundValue>(
                                                    boundValueIt));
                                        if (currBoundVal == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Bound Value not found");
                                            continue;
                                        }
                                        if (static_cast<int64_t>(
                                                attrReqVal.size()) >
                                            *currBoundVal)
                                        {
                                            valid = false;
                                        }
                                    }
                                    else
                                    {
                                        continue;
                                    }
                                }

                                if (!valid)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "Requested Attribute Value invalid");
                                    messages::propertyValueOutOfRange(
                                        asyncResp->res, attrReqVal,
                                        pendingAttrIt.key());
                                    return;
                                }
                            }

                            if (biosFlag)
                            {
                                std::get<BaseBiosTableIndex::baseBiosCurrValue>(
                                    attrIt->second) = attrReqVal;
                            }
                            else
                            {
                                pendingAttrs.insert(std::make_pair(
                                    pendingAttrIt.key(),
                                    std::make_tuple(attrItType, attrReqVal)));
                            }
                        }
                        else if (attrType == "Boolean")
                        {
                            if (!pendingAttrIt.value().is_boolean())
                            {
                                BMCWEB_LOG_ERROR(
                                    "Requested Attribute Value invalid");
                                messages::propertyValueTypeError(
                                    asyncResp->res,
                                    std::string(pendingAttrIt.value()),
                                    pendingAttrIt.key());
                                return;
                            }
                            int64_t attrReqVal = static_cast<int64_t>(
                                pendingAttrIt.value().get<bool>());
                            if (biosFlag)
                            {
                                std::get<BaseBiosTableIndex::baseBiosCurrValue>(
                                    attrIt->second) = attrReqVal;
                            }
                            else
                            {
                                pendingAttrs.insert(std::make_pair(
                                    pendingAttrIt.key(),
                                    std::make_tuple(attrItType, attrReqVal)));
                            }
                        }
                        else if (attrType == "Integer")
                        {
                            if (!pendingAttrIt.value().is_number())
                            {
                                BMCWEB_LOG_ERROR(
                                    "Requested Attribute Value invalid");
                                messages::propertyValueTypeError(
                                    asyncResp->res,
                                    std::string(pendingAttrIt.value()),
                                    pendingAttrIt.key());
                                return;
                            }
                            int64_t attrReqVal =
                                pendingAttrIt.value().get<int64_t>();
                            if (biosFlag)
                            {
                                std::get<BaseBiosTableIndex::baseBiosCurrValue>(
                                    attrIt->second) = attrReqVal;
                            }
                            else
                            {
                                pendingAttrs.emplace(
                                    pendingAttrIt.key(),
                                    std::make_tuple(attrItType, attrReqVal));
                            }
                        }
                        else
                        {
                            BMCWEB_LOG_ERROR("Unknown Attribute Type{}",
                                             attrType);
                            messages::internalError(asyncResp->res);
                            return;
                        }
                    }
                    if (biosFlag)
                    {
                        dbus::utility::setProperty(
                            "xyz.openbmc_project.BIOSConfigManager",
                            "/xyz/openbmc_project/bios_config/manager",
                            "xyz.openbmc_project.BIOSConfig.Manager",
                            "BaseBIOSTable", *baseBiosTable,
                            [asyncResp, baseBiosTable](
                                const boost::system::error_code& ec1) {
                                if (ec1)
                                {
                                    BMCWEB_LOG_DEBUG(
                                        "Error occurred in setting BaseBIOSTable");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }

                                messages::success(asyncResp->res);
                            });
                    }
                    dbus::utility::setProperty(
                        biosService, biosConfigObj, biosConfigIface,
                        "PendingAttributes", pendingAttrs,
                        [asyncResp](const boost::system::error_code& ec3) {
                            if (ec3)
                            {
                                BMCWEB_LOG_ERROR(
                                    "Set PendingAttributes failed {}", ec3);
                                messages::internalError(asyncResp->res);
                                return;
                            }

                            messages::success(asyncResp->res);
                        });
                });
        });
}

/**
 *@brief Updates the BIOS Pending Attributes DBUS property, which are requested
 *by the oob user.
 *
 * @param[in]	    pendingAttrJson BIOS Base Table pending Attribute details
 * @param[in,out]   asyncResp   Async HTTP response.
 *
 * @return None.
 */
inline void setBiosPendingAttr(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const nlohmann::json& pendingAttrJson)
{
    setBiosCurrentOrPendingAttr(asyncResp, pendingAttrJson, false);
}

/**
 *@brief Updates the BIOS Attributes table DBUS property, which are requested
 *       by the UEFI user.
 *
 * @param[in]	    pendingAttrJson BIOS Base Table pending Attribute details
 * @param[in,out]   asyncResp   Async HTTP response.
 *
 * @return None.
 */
inline void setBiosServicCurrentAttr(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const nlohmann::json& pendingAttrJson)
{
    setBiosCurrentOrPendingAttr(asyncResp, pendingAttrJson, true);
}

/**
 *@brief Reads the BIOS Base Table DBUS property and update the Bios Attribute
 *Registry response.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 *
 * @return None.
 */
[[maybe_unused]] static void getBiosAttributeRegistry(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::getDbusObject(
        biosConfigObj, std::array<std::string_view, 1>{biosConfigIface},
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetObject& objType) {
            if (ec || objType.empty())
            {
                BMCWEB_LOG_DEBUG("GetObject for path {}", biosConfigObj);
                return;
            }

            const std::string& biosService = objType.begin()->first;
            dbus::utility::getProperty<BaseBIOSTable>(
                biosService, biosConfigObj, biosConfigIface, "BaseBIOSTable",
                [asyncResp](const boost::system::error_code& ec2,
                            const BaseBIOSTable& baseBiosTableResp) {
                    if (ec2)
                    {
                        BMCWEB_LOG_ERROR(
                            "Get BaseBIOSTable DBus response error{}", ec2);
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    const BaseBIOSTable* baseBiosTable = &baseBiosTableResp;

                    nlohmann::json& attributeArray =
                        asyncResp->res
                            .jsonValue["RegistryEntries"]["Attributes"];

                    if (baseBiosTable == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Empty BaseBIOSTable");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    for (const BaseBIOSTableItem& attrIt : *baseBiosTable)
                    {
                        // read the attribute type at 0th field and convert from
                        // dbus to string format
                        std::string attrType = getBiosAttrType(std::string(
                            std::get<BaseBiosTableIndex::baseBiosAttrType>(
                                attrIt.second)));

                        if (attrType == "UNKNOWN")
                        {
                            BMCWEB_LOG_ERROR("Attribute type not supported");
                            continue;
                        }

                        nlohmann::json attributeIt;
                        attributeIt["AttributeName"] = attrIt.first;
                        attributeIt["Type"] = attrType;
                        attributeIt["ReadOnly"] = std::get<
                            BaseBiosTableIndex::baseBiosReadonlyStatus>(
                            attrIt.second);
                        attributeIt["DisplayName"] =
                            std::get<BaseBiosTableIndex::baseBiosDisplayName>(
                                attrIt.second);
                        const std::string& helpText =
                            std::get<BaseBiosTableIndex::baseBiosDescription>(
                                attrIt.second);
                        if (!helpText.empty())
                        {
                            attributeIt["HelpText"] = helpText;
                        }
                        attributeIt["MenuPath"] =
                            std::get<BaseBiosTableIndex::baseBiosMenuPath>(
                                attrIt.second);

                        if ((attrType == "String") ||
                            (attrType == "Enumeration"))
                        {
                            // read the current value of attribute at 5th field
                            const std::string* attrCurrValue =
                                std::get_if<std::string>(
                                    &std::get<
                                        BaseBiosTableIndex::baseBiosCurrValue>(
                                        attrIt.second));
                            if (attrCurrValue != nullptr)
                            {
                                attributeIt["CurrentValue"] = *attrCurrValue;
                            }
                            else
                            {
                                attributeIt["CurrentValue"] = nullptr;
                            }

                            // read the default value of attribute at 6th field
                            const std::string* attrDefaultValue = std::get_if<
                                std::string>(
                                &std::get<
                                    BaseBiosTableIndex::baseBiosDefaultValue>(
                                    attrIt.second));
                            if (attrDefaultValue != nullptr)
                            {
                                attributeIt["DefaultValue"] = *attrDefaultValue;
                            }
                            else
                            {
                                attributeIt["DefaultValue"] = nullptr;
                            }
                        }
                        else if ((attrType == "Integer") ||
                                 (attrType == "Boolean"))
                        {
                            // read the current value of attribute at 5th field
                            const int64_t* attrCurrValue = std::get_if<int64_t>(
                                &std::get<
                                    BaseBiosTableIndex::baseBiosCurrValue>(
                                    attrIt.second));
                            if (attrCurrValue != nullptr)
                            {
                                if (attrType == "Boolean")
                                {
                                    if (*attrCurrValue != 0)
                                    {
                                        attributeIt["CurrentValue"] = true;
                                    }
                                    else
                                    {
                                        attributeIt["CurrentValue"] = false;
                                    }
                                }
                                else
                                {
                                    attributeIt["CurrentValue"] =
                                        *attrCurrValue;
                                }
                            }
                            else
                            {
                                attributeIt["CurrentValue"] = nullptr;
                            }

                            // read the current value of attribute at 6th field
                            const int64_t* attrDefaultValue = std::get_if<
                                int64_t>(
                                &std::get<
                                    BaseBiosTableIndex::baseBiosDefaultValue>(
                                    attrIt.second));
                            if (attrDefaultValue != nullptr)
                            {
                                if (attrType == "Boolean")
                                {
                                    if (*attrDefaultValue != 0)
                                    {
                                        attributeIt["DefaultValue"] = true;
                                    }
                                    else
                                    {
                                        attributeIt["DefaultValue"] = false;
                                    }
                                }
                                else
                                {
                                    attributeIt["DefaultValue"] =
                                        *attrDefaultValue;
                                }
                            }
                            else
                            {
                                attributeIt["DefaultValue"] = nullptr;
                            }
                        }

                        nlohmann::json boundValArray = nlohmann::json::array();

                        // read the bound values for the attribute
                        const std::vector<AttrBoundType> boundValues =
                            std::get<BaseBiosTableIndex::baseBiosBoundValues>(
                                attrIt.second);

                        for (const AttrBoundType& boundValueIt : boundValues)
                        {
                            nlohmann::json boundValJson;

                            // read the bound value type at 0th field
                            // and convert from dbus to string format
                            std::string boundValType =
                                getBiosBoundValType(std::string(
                                    std::get<
                                        BaseBiosBoundIndex::baseBiosBoundType>(
                                        boundValueIt)));

                            if (boundValType == "UNKNOWN")
                            {
                                BMCWEB_LOG_ERROR(
                                    "Attribute type not supported");
                                continue;
                            }

                            if (boundValType == "OneOf")
                            {
                                if ((attrType == "String") ||
                                    (attrType == "Enumeration"))
                                {
                                    // read the bound value  at 1st field
                                    // for each entry
                                    const std::string* currBoundVal =
                                        std::get_if<std::string>(
                                            &std::get<BaseBiosBoundIndex::
                                                          baseBiosBoundValue>(
                                                boundValueIt));
                                    if (currBoundVal != nullptr)
                                    {
                                        boundValJson["ValueName"] =
                                            *currBoundVal;
                                    }
                                    else
                                    {
                                        boundValJson["ValueName"] = "";
                                    }
                                }
                                else if (attrType == "Boolean")
                                {
                                    // read the bound value  at 1st field
                                    // for each entry
                                    const int64_t* currBoundVal =
                                        std::get_if<int64_t>(
                                            &std::get<BaseBiosBoundIndex::
                                                          baseBiosBoundValue>(
                                                boundValueIt));
                                    if (currBoundVal != nullptr)
                                    {
                                        if (*currBoundVal != 0)
                                        {
                                            boundValJson["ValueName"] = true;
                                        }
                                        else
                                        {
                                            boundValJson["ValueName"] = false;
                                        }
                                    }
                                    else
                                    {
                                        boundValJson["ValueName"] = false;
                                    }
                                }
                                else
                                {
                                    continue;
                                }
                            }
                            else if (boundValType == "LowerBound")
                            {
                                const int64_t* currBoundVal = std::get_if<
                                    int64_t>(
                                    &std::get<
                                        BaseBiosBoundIndex::baseBiosBoundValue>(
                                        boundValueIt));
                                if (currBoundVal != nullptr)
                                {
                                    attributeIt["LowerBound"] = *currBoundVal;
                                }
                                else
                                {
                                    attributeIt["LowerBound"] = 0;
                                }
                            }
                            else if (boundValType == "UpperBound")
                            {
                                const int64_t* currBoundVal = std::get_if<
                                    int64_t>(
                                    &std::get<
                                        BaseBiosBoundIndex::baseBiosBoundValue>(
                                        boundValueIt));
                                if (currBoundVal != nullptr)
                                {
                                    attributeIt["UpperBound"] = *currBoundVal;
                                }
                                else
                                {
                                    attributeIt["UpperBound"] = 0;
                                }
                            }
                            else if (boundValType == "ScalarIncrement")
                            {
                                const int64_t* currBoundVal = std::get_if<
                                    int64_t>(
                                    &std::get<
                                        BaseBiosBoundIndex::baseBiosBoundValue>(
                                        boundValueIt));
                                if (currBoundVal != nullptr)
                                {
                                    attributeIt["ScalarIncrement"] =
                                        *currBoundVal;
                                }
                                else
                                {
                                    attributeIt["ScalarIncrement"] = 0;
                                }
                            }
                            else if (boundValType == "MinStringLength")
                            {
                                const int64_t* currBoundVal = std::get_if<
                                    int64_t>(
                                    &std::get<
                                        BaseBiosBoundIndex::baseBiosBoundValue>(
                                        boundValueIt));
                                if (currBoundVal != nullptr)
                                {
                                    attributeIt["MinLength"] = *currBoundVal;
                                }
                                else
                                {
                                    attributeIt["MinLength"] = 0;
                                }
                            }
                            else if (boundValType == "MaxStringLength")
                            {
                                const int64_t* currBoundVal = std::get_if<
                                    int64_t>(
                                    &std::get<
                                        BaseBiosBoundIndex::baseBiosBoundValue>(
                                        boundValueIt));
                                if (currBoundVal != nullptr)
                                {
                                    attributeIt["MaxLength"] = *currBoundVal;
                                }
                                else
                                {
                                    attributeIt["MaxLength"] = 0;
                                }
                            }
                            else
                            {
                                // read the bound value  at 1st field
                                // for each entry
                                const int64_t* currBoundVal = std::get_if<
                                    int64_t>(
                                    &std::get<
                                        BaseBiosBoundIndex::baseBiosBoundValue>(
                                        boundValueIt));
                                if (currBoundVal != nullptr)
                                {
                                    boundValJson["ValueName"] = *currBoundVal;
                                }
                                else
                                {
                                    boundValJson["ValueName"] = 0;
                                }
                            }
                            boundValArray.push_back(boundValJson);
                        }

                        if (attrType == "Enumeration" && boundValArray.empty())
                        {
                            BMCWEB_LOG_ERROR("Bound Values Array is empty");
                            continue;
                        }
                        if (attrType == "Enumeration")
                        {
                            attributeIt["Value"] = boundValArray;
                        }
                        attributeArray.push_back(attributeIt);
                    }
                });
        });
}

/**
 *@brief Reads the BIOS Base Table DBUS property and update the Bios Attribute
 *Registry response.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 *
 * @return None.
 */
static void updateBiosAttrRegistry(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::getDbusObject(
        biosConfigObj, std::array<std::string_view, 1>{biosConfigIface},
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetObject& objType) {
            if (ec || objType.empty())
            {
                BMCWEB_LOG_DEBUG("GetObject for path {}", biosConfigObj);
                return;
            }

            const std::string& biosService = objType.begin()->first;
            dbus::utility::getProperty<BaseBIOSTable>(
                biosService, biosConfigObj, biosConfigIface, "BaseBIOSTable",
                [asyncResp](const boost::system::error_code& ec2,
                            const BaseBIOSTable& baseBiosTableResp) {
                    if (ec2)
                    {
                        BMCWEB_LOG_ERROR(
                            "Get BaseBIOSTable DBus response error{}", ec2);
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    const BaseBIOSTable* baseBiosTable = &baseBiosTableResp;

                    if (baseBiosTable == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Empty BaseBIOSTable");
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    auto& attributes =
                        biosRegistryJson["RegistryEntries"]["Attributes"];

                    bool biosTableLoopEntered = false;

                    for (const BaseBIOSTableItem& attrIt : *baseBiosTable)
                    {
                        biosTableLoopEntered = true;
                        std::string attrType = getBiosAttrType(std::string(
                            std::get<BaseBiosTableIndex::baseBiosAttrType>(
                                attrIt.second)));

                        auto it = std::ranges::find_if(
                            attributes, [&](const nlohmann::json& attr) {
                                return attr["AttributeName"] == attrIt.first;
                            });

                        if ((attrType == "String") ||
                            (attrType == "Enumeration"))
                        {
                            const std::string* attrCurrValue =
                                std::get_if<std::string>(
                                    &std::get<
                                        BaseBiosTableIndex::baseBiosCurrValue>(
                                        attrIt.second));

                            if (it != attributes.end())
                            {
                                (*it)["CurrentValue"] =
                                    nlohmann::json(*attrCurrValue);
                            }
                        }
                        else if ((attrType == "Integer") ||
                                 (attrType == "Boolean"))
                        {
                            const int64_t* attrCurrValue = std::get_if<int64_t>(
                                &std::get<
                                    BaseBiosTableIndex::baseBiosCurrValue>(
                                    attrIt.second));
                            if (it != attributes.end())
                            {
                                if (attrType == "Boolean")
                                {
                                    if (*attrCurrValue != 0)
                                    {
                                        (*it)["CurrentValue"] =
                                            nlohmann::json(true);
                                    }
                                    else
                                    {
                                        (*it)["CurrentValue"] =
                                            nlohmann::json(false);
                                    }
                                }
                                else
                                {
                                    (*it)["CurrentValue"] =
                                        nlohmann::json(*attrCurrValue);
                                }
                            }
                        }
                        else
                        {
                            BMCWEB_LOG_ERROR("Attribute type not supported");
                        }
                    }
                    if (!biosTableLoopEntered)
                    {
                        biosRegistryJson = nlohmann::json();
                    }
                    asyncResp->res.jsonValue = biosRegistryJson;
                });
        });
}

/**
 * BiosService class supports handle put method for bios.
 */
inline void handleBiosServicePut(
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
    if (req.session == nullptr)
    {
        BMCWEB_LOG_ERROR("Session is null");
        messages::insufficientPrivilege(asyncResp->res);
        return;
    }
    dbus::utility::async_method_call(
        [&req,
         asyncResp](const boost::system::error_code& ec,
                    const std::map<std::string, dbus::utility::DbusVariantType>&
                        userInfo) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("GetUserInfo failed");
                messages::internalError(asyncResp->res);
                return;
            }

            const std::vector<std::string>* userGroupPtr = nullptr;
            auto userInfoIter = userInfo.find("UserGroups");
            if (userInfoIter != userInfo.end())
            {
                userGroupPtr = std::get_if<std::vector<std::string>>(

                    &userInfoIter->second);
            }

            if (userGroupPtr == nullptr)
            {
                BMCWEB_LOG_ERROR("User Group not found");
                messages::internalError(asyncResp->res);
                return;
            }

            auto found =
                std::ranges::find_if(*userGroupPtr, [](const auto& group) {
                    return static_cast<bool>(group == "redfish-hostiface");
                });

            // Only Host Iface (redfish-hostiface) group user should
            // perform PUT operations
            if (found == userGroupPtr->end())
            {
                BMCWEB_LOG_ERROR("Not Sufficient Privilage");
                messages::insufficientPrivilege(asyncResp->res);
                return;
            }
            std::vector<nlohmann::json> baseBiosTableJson;
            if (!redfish::json_util::readJsonAction(
                    req, asyncResp->res, "Attributes", baseBiosTableJson))
            {
                BMCWEB_LOG_ERROR("No 'Attributes' found");
                messages::unrecognizedRequestBody(asyncResp->res);
                return;
            }

            // Set the BaseBIOSTable
            fillBiosTable(asyncResp, baseBiosTableJson);
        },
        "xyz.openbmc_project.User.Manager", "/xyz/openbmc_project/user",
        "xyz.openbmc_project.User.Manager", "GetUserInfo",
        req.session->username);
}

/**
 * BiosSetting class supports handle patch method for Bios.
 */
inline void handleBiosServicePatch(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (req.session == nullptr)
    {
        BMCWEB_LOG_ERROR("Session is null");
        messages::insufficientPrivilege(asyncResp->res);
        return;
    }
    dbus::utility::async_method_call(
        [&req,
         asyncResp](const boost::system::error_code& ec,
                    const std::map<std::string, dbus::utility::DbusVariantType>&
                        userInfo) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("GetUserInfo failed");
                messages::internalError(asyncResp->res);
                return;
            }

            const std::vector<std::string>* userGroupPtr = nullptr;
            auto userInfoIter = userInfo.find("UserGroups");
            if (userInfoIter != userInfo.end())
            {
                userGroupPtr = std::get_if<std::vector<std::string>>(

                    &userInfoIter->second);
            }

            if (userGroupPtr == nullptr)
            {
                BMCWEB_LOG_ERROR("User Group not found");
                messages::internalError(asyncResp->res);
                return;
            }

            auto found =
                std::ranges::find_if(*userGroupPtr, [](const auto& group) {
                    return static_cast<bool>(group == "redfish-hostiface");
                });

            // Only Host Iface (redfish-hostiface) group user should
            // perform PUT operations
            if (found == userGroupPtr->end())
            {
                BMCWEB_LOG_ERROR("Not Sufficient Privilage");
                messages::insufficientPrivilege(asyncResp->res);
                return;
            }

            nlohmann::json pendingAttrJson;
            if (!redfish::json_util::readJsonAction(
                    req, asyncResp->res, "Attributes", pendingAttrJson))
            {
                BMCWEB_LOG_ERROR("No 'Attributes' found");
                messages::unrecognizedRequestBody(asyncResp->res);
                return;
            }
            // Update the BaseBIOSTable attributes
            setBiosServicCurrentAttr(asyncResp, pendingAttrJson);
        },
        "xyz.openbmc_project.User.Manager", "/xyz/openbmc_project/user",
        "xyz.openbmc_project.User.Manager", "GetUserInfo",
        req.session->username);
}

inline void requestRoutesOemBios(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Bios/")
        .privileges(redfish::privileges::putBios)
        .methods(boost::beast::http::verb::put)(
            std::bind_front(handleBiosServicePut, std::ref(app)));

    if constexpr (BMCWEB_DPU_BIOS)
    {
        BMCWEB_ROUTE(app,
                     "/redfish/v1/Systems/" +
                         std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/Bios/")
            .privileges(redfish::privileges::patchBios)
            .methods(boost::beast::http::verb::patch)(
                std::bind_front(handleBiosServicePatch, std::ref(app)));
    }
}

/**
 * BiosSetting class supports handle patch method for Bios Settings.
 */
inline void handleBiosSettingsPatch(
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
    nlohmann::json pendingAttrJson;
    if (!redfish::json_util::readJsonAction(req, asyncResp->res, "Attributes",
                                            pendingAttrJson))
    {
        BMCWEB_LOG_ERROR("No 'Attributes' found");
        messages::unrecognizedRequestBody(asyncResp->res);
        return;
    }
    // Update the Pending Atttributes
    setBiosPendingAttr(asyncResp, pendingAttrJson);
}

/**
 * BiosSetting class supports handle get method for Bios Settings.
 */
inline void handleBiosSettingsGet(
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
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Bios/Settings";
    asyncResp->res.jsonValue["@odata.type"] = "#Bios.v1_2_0.Bios";
    asyncResp->res.jsonValue["Name"] = "BIOS Configuration";
    asyncResp->res.jsonValue["Description"] = "BIOS Settings";
    asyncResp->res.jsonValue["Id"] = "BIOS_Settings";

    asyncResp->res.jsonValue["Attributes"] = nlohmann::json({});
    // get the BIOS Attributes
    getBiosSettingsAttr(asyncResp);
}

inline void requestRoutesBiosSettings(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Bios/Settings")
        .privileges(redfish::privileges::getBios)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleBiosSettingsGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Bios/Settings")
        .privileges(redfish::privileges::patchBios)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleBiosSettingsPatch, std::ref(app)));
}

/**
 * ChangePassword class supports handle POST method for bios.
 * The class retrieves and sends data directly to D-Bus.
 *
 * Function handles POST method request.
 * Analyzes POST body message before sends ChangePassword request data to D-Bus.
 */
inline void handleBiosChangePasswordPost(
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
    std::string passwordName;
    std::string oldPassword;
    std::string newPassword;
    if (!redfish::json_util::readJsonAction(
            req, asyncResp->res, "PasswordName", passwordName, "OldPassword",
            oldPassword, "NewPassword", newPassword))
    {
        return;
    }

    dbus::utility::getSubTree(
        "/xyz/openbmc_project", 0,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.BIOSConfig.Password"},
        [asyncResp, passwordName, oldPassword,
         newPassword](const boost::system::error_code& ec,
                      const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec || subtree.size() != 1)
            {
                BMCWEB_LOG_ERROR("Failed to find BIOS Password object: {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            const auto& [path, services] = subtree[0];

            if (services.size() != 1)
            {
                BMCWEB_LOG_ERROR("Failed to find BIOS Password object: {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            const auto& [service, interfaces] = services[0];

            dbus::utility::async_method_call(
                [asyncResp](const boost::system::error_code& ec1,
                            const sdbusplus::message_t& msg) {
                    if (ec1)
                    {
                        const auto* const error = msg.get_error();
                        if (sd_bus_error_has_name(
                                error,
                                "xyz.openbmc_project.BIOSConfig.Common.Error.InvalidCurrentPassword") !=
                            0)
                        {
                            BMCWEB_LOG_ERROR(
                                "Failed to change password message: {}",
                                error->name);
                            messages::actionParameterValueError(
                                asyncResp->res, "OldPassword",
                                "ChangePassword");
                            return;
                        }

                        messages::internalError(asyncResp->res);
                        return;
                    }
                    messages::success(asyncResp->res);
                    return;
                },
                service, path, "xyz.openbmc_project.BIOSConfig.Password",
                "ChangePassword", passwordName, oldPassword, newPassword);
        });
}

inline void requestRoutesBiosChangePassword(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/Bios/Actions/Bios.ChangePassword/")
        .privileges(redfish::privileges::postBios)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleBiosChangePasswordPost, std::ref(app)));
}

/**
 * BiosAttributeRegistry class supports handle get method for Bios Attribute
 * Registry.
 */
inline void handleBiosAttrRegistryGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if constexpr (BMCWEB_DPU_BIOS)
    {
        std::ifstream inputFile(biosRegistryJsonFileName);
        if (!inputFile.is_open())
        {
            BMCWEB_LOG_DEBUG("Can't opening file for reading: {}",
                             biosRegistryJsonFileName);

            // Return empty json object if file not found
            biosRegistryJson = nlohmann::json();
        }
        else
        {
            std::string contents{std::istreambuf_iterator<char>{inputFile},
                                 std::istreambuf_iterator<char>{}};
            inputFile.close();
            biosRegistryJson = nlohmann::json::parse(contents);
            updateBiosAttrRegistry(asyncResp);
        }
    }
    else
    {
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Registries/BiosAttributeRegistry/"
            "BiosAttributeRegistry";
        asyncResp->res.jsonValue["@odata.type"] =
            "#AttributeRegistry.v1_3_2.AttributeRegistry";
        asyncResp->res.jsonValue["Name"] = "Bios Attribute Registry";
        asyncResp->res.jsonValue["Id"] = "BiosAttributeRegistry";
        asyncResp->res.jsonValue["RegistryVersion"] = "1.0.0";
        asyncResp->res.jsonValue["Language"] = "en";
        asyncResp->res.jsonValue["OwningEntity"] = "NVIDIA";

        asyncResp->res.jsonValue["RegistryEntries"]["Attributes"] =
            nlohmann::json::array();

        // Get the BIOS Attributes Registry
        getBiosAttributeRegistry(asyncResp);
    }
}

/**
 * BiosAttributeRegistry class supports handle put method for bios.
 */
inline void handleBiosAttrRegistryPut(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (req.session == nullptr)
    {
        BMCWEB_LOG_ERROR("Session is null");
        messages::insufficientPrivilege(asyncResp->res);
        return;
    }
    dbus::utility::async_method_call(
        [&req,
         asyncResp](const boost::system::error_code& ec,
                    const std::map<std::string, dbus::utility::DbusVariantType>&
                        userInfo) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("GetUserInfo failed");
                messages::internalError(asyncResp->res);
                return;
            }

            const std::vector<std::string>* userGroupPtr = nullptr;
            auto userInfoIter = userInfo.find("UserGroups");
            if (userInfoIter != userInfo.end())
            {
                userGroupPtr = std::get_if<std::vector<std::string>>(
                    &userInfoIter->second);
            }

            if (userGroupPtr == nullptr)
            {
                BMCWEB_LOG_ERROR("User Group not found");
                messages::internalError(asyncResp->res);
                return;
            }

            auto found =
                std::ranges::find_if(*userGroupPtr, [](const auto& group) {
                    return static_cast<bool>(group == "redfish-hostiface");
                });

            // Only Host Iface (redfish-hostiface) group user should
            // perform PUT operations
            if (found == userGroupPtr->end())
            {
                BMCWEB_LOG_ERROR("Not Sufficient Privilage");
                messages::insufficientPrivilege(asyncResp->res);
                return;
            }

            if (!json_util::processJsonFromRequest(asyncResp->res, req,
                                                   biosRegistryJson))
            {
                BMCWEB_LOG_ERROR("Json value not readable");
                return;
            }

            // Save BiosRegistryJson into file
            std::ofstream outputFile(biosRegistryJsonFileName, std::ios::trunc);
            if (!outputFile.is_open())
            {
                BMCWEB_LOG_ERROR("Error opening file for writing: {}",
                                 biosRegistryJsonFileName);
                return;
            }
            biosRegistryJson["Id"] = "BiosAttributeRegistry";
            outputFile << biosRegistryJson.dump();
            outputFile.close();

            auto attributes = biosRegistryJson["RegistryEntries"]["Attributes"];

            // Loop over the "Attributes" array
            for (auto& attr : attributes)
            {
                // replace "HelpText" with "description"
                if (attr.find("HelpText") != attr.end())
                {
                    attr["Description"] = attr["HelpText"];
                    attr.erase("HelpText");
                }
                // Add default value
                if (attr.find("DefaultValue") == attr.end())
                {
                    attr["DefaultValue"] = nullptr;
                }
            }
            std::vector<nlohmann::json> baseBiosTableJson;
            // Iterate over the 'Attributes' array and add each object to the
            // vector
            for (const auto& attribute : attributes)
            {
                baseBiosTableJson.push_back(attribute);
            }

            // Set the BaseBIOSTable
            fillBiosTable(asyncResp, baseBiosTableJson);
        },
        "xyz.openbmc_project.User.Manager", "/xyz/openbmc_project/user",
        "xyz.openbmc_project.User.Manager", "GetUserInfo",
        req.session->username);
}

inline void requestRoutesBiosAttrRegistryService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Registries/"
                      "BiosAttributeRegistry/BiosAttributeRegistry/")
        .privileges(redfish::privileges::putBios)
        .methods(boost::beast::http::verb::put)(
            std::bind_front(handleBiosAttrRegistryPut, std::ref(app)));
}

inline void handleBiosServiceGetExtended(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    asyncResp->res.jsonValue["Actions"]["#Bios.ChangePassword"]["target"] =
        std::format("/redfish/v1/Systems/{}/Bios/Actions/Bios.ChangePassword",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME);
    nlohmann::json biosSettings;
    biosSettings["@odata.type"] = "#Settings.v1_3_5.Settings";
    biosSettings["SettingsObject"] = {
        {"@odata.id", std::format("/redfish/v1/Systems/{}/Bios/Settings",
                                  BMCWEB_REDFISH_SYSTEM_URI_NAME)}};
    asyncResp->res.jsonValue["@Redfish.Settings"] = biosSettings;
    asyncResp->res.jsonValue["AttributeRegistry"] = boost::urls::format(
        "/redfish/v1/Registries/BiosAttributeRegistry/BiosAttributeRegistry");
    asyncResp->res.jsonValue["Attributes"] = nlohmann::json({});
    // Get the BIOS Attributes
    getBiosAttributes(asyncResp);

    // Get the ResetBiosToDefaultsPending
    getResetBiosSettings(asyncResp);
}
} // namespace redfish
