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
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/bios_utils.hpp"
#include "utils/sw_utils.hpp"

#include <sys/types.h>

#include <boost/beast/http/verb.hpp>

#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace redfish
{
static constexpr std::string_view biosConfigManagerPath =
    "/xyz/openbmc_project/bios_config/manager";
static constexpr std::string_view biosConfigManagerInterface =
    "xyz.openbmc_project.BIOSConfig.Manager";

using BaseTableOption =
    std::tuple<std::string, dbus::utility::DbusVariantType, std::string>;

using BaseTableAttribute =
    std::tuple<std::string, bool, std::string, std::string, std::string,
               dbus::utility::DbusVariantType, dbus::utility::DbusVariantType,
               std::vector<BaseTableOption>>;

enum class BaseTableAttributeIndex
{
    Type = 0,
    ReadOnly,
    Name,
    Description,
    Path,
    CurrentValue,
    DefaultValue,
    Options
};

using BaseTable = std::map<std::string, BaseTableAttribute>;

using PendingAttributeValue =
    std::tuple<std::string, dbus::utility::DbusVariantType>;

using PendingAttributes = std::map<std::string, PendingAttributeValue>;

enum class PendingAttributeValueIndex
{
    Type = 0,
    Value
};

inline void afterBiosPasswordChangeObjectResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& passwordName,
    const std::string& oldPassword, const std::string& newPassword)
{
    dbus::utility::async_method_call(
        asyncResp,
        [asyncResp](const boost::system::error_code& ec,
                    sdbusplus::message_t& msg) {
            if (ec)
            {
                const sd_bus_error* dbusError = msg.get_error();
                if (dbusError != nullptr)
                {
                    if (std::string_view(
                            "xyz.openbmc_project.BIOSConfig.Common.Error.InvalidCurrentPassword") ==
                        dbusError->name)
                    {
                        messages::actionParameterValueError(
                            asyncResp->res, "OldPassword", "ChangePassword");
                        return;
                    }
                }
                BMCWEB_LOG_ERROR("DBUS response error: {}", ec.message());
                messages::internalError(asyncResp->res);
                return;
            }
            messages::success(asyncResp->res);
            return;
        },
        service, "/xyz/openbmc_project/bios_config/password",
        "xyz.openbmc_project.BIOSConfig.Password", "ChangePassword",
        passwordName, oldPassword, newPassword);
}

inline void handleBiosChangePasswordPost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
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
    if (!json_util::readJsonAction(req, asyncResp->res, "PasswordName",
                                   passwordName, "OldPassword", oldPassword,
                                   "NewPassword", newPassword))
    {
        return;
    }

    constexpr std::array<std::string_view, 1> biosPasswordInterfaces = {
        "xyz.openbmc_project.BIOSConfig.Password"};
    dbus::utility::getDbusObject(
        "/xyz/openbmc_project/bios_config/password", biosPasswordInterfaces,
        [asyncResp, passwordName, oldPassword,
         newPassword](const boost::system::error_code& ec,
                      const dbus::utility::MapperGetObject& objType) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Failed to get BIOS Password object: {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            if (objType.empty())
            {
                BMCWEB_LOG_ERROR("BIOS Password object not found");
                messages::internalError(asyncResp->res);
                return;
            }
            const std::string& objectPath = objType.begin()->first;
            afterBiosPasswordChangeObjectResponse(
                asyncResp, objectPath, passwordName, oldPassword, newPassword);
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

template <typename Type>
inline void extractValue(nlohmann::json& attributes, const std::string& name,
                         const dbus::utility::DbusVariantType& value)
{
    const Type* tValue = std::get_if<Type>(&value);
    if (tValue != nullptr)
    {
        attributes[name] = *tValue;
        return;
    }
    attributes[name] = Type{};
}

template <>
inline void extractValue<bool>(nlohmann::json& attributes,
                               const std::string& name,
                               const dbus::utility::DbusVariantType& value)
{
    const int64_t* tValue = std::get_if<int64_t>(&value);
    if (tValue != nullptr)
    {
        attributes[name] = (*tValue != 0);
        return;
    }
    attributes[name] = false;
}

using HandlerType = std::function<void(nlohmann::json&, const std::string&,
                                       const dbus::utility::DbusVariantType&)>;

inline void addAttribute(nlohmann::json& attributes, const std::string& name,
                         const dbus::utility::DbusVariantType& type,
                         const dbus::utility::DbusVariantType& value)
{
    static const std::unordered_map<std::string, HandlerType> typeMap = {
        {"xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Enumeration",
         extractValue<std::string>},
        {"xyz.openbmc_project.BIOSConfig.Manager.AttributeType.String",
         extractValue<std::string>},
        {"xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Password",
         extractValue<std::string>},
        {"xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Integer",
         extractValue<int64_t>},
        {"xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Boolean",
         extractValue<bool>}};

    const std::string* typeStr = std::get_if<std::string>(&type);
    if (typeStr != nullptr)
    {
        auto it = typeMap.find(*typeStr);
        if (it != typeMap.end())
        {
            it->second(attributes, name, value);
        }
    }
}

template <typename T>
inline void getBIOSManagerProperty(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& property, const std::string& objectPath,
    std::function<void(const T&)> handler)
{
    sdbusplus::asio::getProperty<T>(
        *crow::connections::systemBus, objectPath,
        std::string(biosConfigManagerPath),
        std::string(biosConfigManagerInterface), property,
        [asyncResp, property, handler{std::move(handler)}](
            const boost::system::error_code& ec, const T& value) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBus response error for {}: {}", property,
                                 ec);
                messages::internalError(asyncResp->res);
                return;
            }
            handler(value);
        });
}

template <typename T>
inline void setBIOSManagerProperty(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& propertyName, const T& propertyValue,
    const std::string& objectPath)
{
    sdbusplus::asio::setProperty(
        *crow::connections::systemBus, objectPath,
        std::string(biosConfigManagerPath),
        std::string(biosConfigManagerInterface), propertyName, propertyValue,
        [asyncResp, propertyName](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBus response error for setting {}: {}",
                                 propertyName, ec);
                messages::internalError(asyncResp->res);
                return;
            }
        });
}

template <typename CallbackFunc>
inline void getBIOSManagerObject(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    CallbackFunc&& callback)
{
    dbus::utility::getDbusObject(
        std::string(biosConfigManagerPath),
        std::array<std::string_view, 1>{biosConfigManagerInterface},
        [asyncResp, callback = std::forward<CallbackFunc>(callback)](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetObject& object) {
            if (ec || object.empty())
            {
                BMCWEB_LOG_ERROR("Error finding BIOS Manager object {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            if (object.size() > 1)
            {
                BMCWEB_LOG_ERROR("More than one BIOS Manager object found");
                messages::internalError(asyncResp->res);
                return;
            }
            callback(object.begin()->first);
        });
}

inline void populateRedfishFromPendingAttributesTable(
    crow::Response& res, const PendingAttributes& pendingAttributes)
{
    nlohmann::json& attributes = res.jsonValue["Attributes"];
    for (const auto& [name, pendingAttribute] : pendingAttributes)
    {
        addAttribute(
            attributes, name,
            std::get<uint(PendingAttributeValueIndex::Type)>(pendingAttribute),
            std::get<uint(PendingAttributeValueIndex::Value)>(
                pendingAttribute));
    }
}

inline void handleBiosManagerObjectForGetBiosPendingAttributes(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath)
{
    getBIOSManagerProperty<PendingAttributes>(
        asyncResp, "PendingAttributes", objectPath,
        std::bind_front(populateRedfishFromPendingAttributesTable,
                        std::ref(asyncResp->res)));
}

inline void getBiosPendingAttributes(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    getBIOSManagerObject(
        asyncResp,
        std::bind_front(handleBiosManagerObjectForGetBiosPendingAttributes,
                        asyncResp));
}

inline void handleBiosSettingsGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    asyncResp->res.jsonValue["@odata.id"] = std::format(
        "/redfish/v1/Systems/{}/Bios/Settings", BMCWEB_REDFISH_SYSTEM_URI_NAME);
    asyncResp->res.jsonValue["@odata.type"] = "#Bios.v1_2_0.Bios";
    asyncResp->res.jsonValue["Name"] = "BIOS Configuration";
    asyncResp->res.jsonValue["Description"] = "BIOS Settings";
    asyncResp->res.jsonValue["Id"] = "BIOS_Settings";
    dbus::utility::checkDbusPathExists(
        "/xyz/openbmc_project/bios_config/manager", [asyncResp](int rc) {
            if (rc > 0)
            {
                getBiosPendingAttributes(asyncResp);
            }
        });
}

inline bool mergePendingAttributes(PendingAttributes& pending,
                                   const nlohmann::json& jsonAttributes,
                                   crow::Response& response)
{
    static const std::unordered_map<nlohmann::json::value_t, std::string>
        typeMap = {
            {nlohmann::json::value_t::string,
             "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.String"},
            {nlohmann::json::value_t::number_integer,
             "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Integer"},
            {nlohmann::json::value_t::boolean,
             "xyz.openbmc_project.BIOSConfig.Manager.AttributeType.Boolean"}};

    for (const auto& [name, value] : jsonAttributes.items())
    {
        auto it = typeMap.find(value.type());
        if (it == typeMap.end())
        {
            BMCWEB_LOG_ERROR("Unsupported type for attribute {}", name);
            messages::propertyValueTypeError(response, value, name);
            return false;
        }

        if (value.is_string())
        {
            pending[name] =
                std::make_tuple(it->second, value.get<std::string>());
        }
        else if (value.is_boolean())
        {
            pending[name] = std::make_tuple(
                it->second, static_cast<int64_t>(value.get<bool>()));
        }
        else if (value.is_number_integer())
        {
            pending[name] = std::make_tuple(it->second, value.get<int64_t>());
        }
    }
    return true;
}

inline void processPendingAttributes(
    const nlohmann::json& jsonAttributes,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const PendingAttributes& current)
{
    PendingAttributes pending = current;
    if (!mergePendingAttributes(pending, jsonAttributes, asyncResp->res))
    {
        return;
    }

    getBIOSManagerObject(
        asyncResp,
        std::bind_front(setBIOSManagerProperty<PendingAttributes>, asyncResp,
                        "PendingAttributes", std::move(pending)));
}

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
    dbus::utility::checkDbusPathExists(
        "/xyz/openbmc_project/bios_config/manager", [asyncResp](int rc) {
            if (rc > 0)
            {
                getBIOSManagerObject(
                    asyncResp, [asyncResp, pendingAttrJson](
                                   const std::string& objectPath) {
                        getBIOSManagerProperty<PendingAttributes>(
                            asyncResp, "PendingAttributes", objectPath,
                            [asyncResp, pendingAttrJson,
                             objectPath](const PendingAttributes& current) {
                                processPendingAttributes(pendingAttrJson,
                                                         asyncResp, current);
                            });
                    });
            }
        });
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

inline void populateRedfishFromBaseTable(crow::Response& response,
                                         const BaseTable& baseTable)
{
    nlohmann::json& attributes = response.jsonValue["Attributes"];
    for (const auto& [name, baseTableAttribute] : baseTable)
    {
        addAttribute(
            attributes, name,
            std::get<uint(BaseTableAttributeIndex::Type)>(baseTableAttribute),
            std::get<uint(BaseTableAttributeIndex::CurrentValue)>(
                baseTableAttribute));
    }
}

inline void handleBiosManagerObjectForGetBiosAttributes(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath)
{
    getBIOSManagerProperty<BaseTable>(
        asyncResp, "BaseBIOSTable", objectPath,
        std::bind_front(populateRedfishFromBaseTable,
                        std::ref(asyncResp->res)));
}

inline void getBiosAttributes(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::checkDbusPathExists(
        "/xyz/openbmc_project/bios_config/manager", [asyncResp](int rc) {
            if (rc > 0)
            {
                getBIOSManagerObject(
                    asyncResp,
                    std::bind_front(handleBiosManagerObjectForGetBiosAttributes,
                                    asyncResp));
            }
        });
}

inline void populateRedfishFromResetBiosSettings(
    crow::Response& response, const std::string& resetBiosSettingsMode)
{
    static const std::unordered_map<std::string, std::string>
        resetBiosSettingsModeMap = {
            {"xyz.openbmc_project.BIOSConfig.Manager.ResetFlag.NoAction",
             "NoAction"},
            {"xyz.openbmc_project.BIOSConfig.Manager.ResetFlag.FactoryDefaults",
             "FactoryDefaults"},
            {"xyz.openbmc_project.BIOSConfig.Manager.ResetFlag.FailSafeDefaults",
             "FailSafeDefaults"}};
    auto it = resetBiosSettingsModeMap.find(resetBiosSettingsMode);
    if (it != resetBiosSettingsModeMap.end())
    {
        response.jsonValue["ResetBiosToDefaultsPending"] =
            it->second == "FactoryDefaults" || it->second == "FailSafeDefaults";
    }
    else
    {
        BMCWEB_LOG_ERROR("Invalid Reset BIOS Settings Status");
        messages::internalError(response);
    }
}

inline void handleBiosManagerObjectForGetResetBiosSettings(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath)
{
    getBIOSManagerProperty<std::string>(
        asyncResp, "ResetBIOSSettings", objectPath,
        std::bind_front(populateRedfishFromResetBiosSettings,
                        std::ref(asyncResp->res)));
}

inline void getResetBiosSettings(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::checkDbusPathExists(
        "/xyz/openbmc_project/bios_config/manager", [asyncResp](int rc) {
            if (rc > 0)
            {
                getBIOSManagerObject(
                    asyncResp,
                    std::bind_front(
                        handleBiosManagerObjectForGetResetBiosSettings,
                        asyncResp));
            }
        });
}

} // namespace redfish
