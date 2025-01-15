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
#include "utils/collection.hpp"

#include <app.hpp>
#include <dbus_utility.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <utils/privilege_utils.hpp>

#include <fstream>
#include <iostream>

namespace redfish
{
namespace boot_options
{

/**
 * @brief Create a new BootOption
 * @param id ID of the new BootOption
 * @param callback A callback function, [](boost::system::error_code ec).
 */
template <typename Callback>
void createBootOption(const std::string& id, Callback&& callback)
{
    crow::connections::systemBus->async_method_call(
        callback, "xyz.openbmc_project.BIOSConfigManager",
        "/xyz/openbmc_project/bios_config/manager",
        "xyz.openbmc_project.BIOSConfig.BootOrder", "CreateBootOption", id);
}

/**
 * @brief Delete a BootOption
 * @param id ID of the BootOption
 * @param callback A callback function, [](boost::system::error_code ec).
 */
template <typename Callback>
void deleteBootOption(const std::string& id, Callback&& callback)
{
    std::string path = "/xyz/openbmc_project/bios_config/bootOptions/" + id;
    crow::connections::systemBus->async_method_call(
        callback, "xyz.openbmc_project.BIOSConfigManager", path,
        "xyz.openbmc_project.Object.Delete", "Delete");
}

/**
 * @brief Get all property of a BootOption
 * @param id ID of the BootOption
 * @param callback A callback function, [](boost::system::error_code,
 * dbus::utility::DBusPropertiesMap).
 */
template <typename Callback>
void getBootOption(const std::string& id, Callback&& callback)
{
    std::string path = "/xyz/openbmc_project/bios_config/bootOptions/" + id;
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, "xyz.openbmc_project.BIOSConfigManager",
        path, "xyz.openbmc_project.BIOSConfig.BootOption", callback);
}

/**
 * @brief Set properties to a BootOption
 * @param id ID of the BootOption
 * @param properties vector of pair of property name and property value
 * @param callback A callback function, [](boost::system::error_code ec).
 */
template <typename Callback>
void setBootOption(const std::string& id,
                   const dbus::utility::DBusPropertiesMap& properties,
                   Callback&& callback)
{
    if (properties.empty())
    {
        callback(boost::system::errc::make_error_code(
            boost::system::errc::invalid_argument));
        return;
    }

    std::string path = "/xyz/openbmc_project/bios_config/bootOptions/" + id;

    // holds the callback until all properties were set
    auto holdTask = dbus_utils::deferTask(std::forward<Callback>(callback));
    for (const auto& [propertyName, propertyVariant] : properties)
    {
        crow::connections::systemBus->async_method_call(
            [holdTask](const boost::system::error_code ec) {
                if (ec)
                {
                    holdTask->ec = ec;
                    BMCWEB_LOG_DEBUG(" setBootOption D-BUS error");
                }
            },
            "xyz.openbmc_project.BIOSConfigManager", path,
            "org.freedesktop.DBus.Properties", "Set",
            "xyz.openbmc_project.BIOSConfig.BootOption", propertyName,
            propertyVariant);
    }
}

inline void handleCollectionPendingBootOptionMembers(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::urls::url& collectionPath, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& objects)
{
    if (ec == boost::system::errc::io_error)
    {
        asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
        asyncResp->res.jsonValue["Members@odata.count"] = 0;
        return;
    }

    if (ec)
    {
        BMCWEB_LOG_DEBUG("DBUS response error {}", ec.value());
        messages::internalError(asyncResp->res);
        return;
    }

    std::vector<std::string> pathNames;

    for (const auto& object : objects)
    {
        sdbusplus::message::object_path path(object);

        std::string leaf = path.filename();
        if (leaf.empty())
        {
            continue;
        }

        pathNames.push_back(leaf);
    }
    std::ranges::sort(pathNames, AlphanumLess<std::string>());

    nlohmann::json& members = asyncResp->res.jsonValue["Members"];
    members = nlohmann::json::array();
    for (const std::string& bootOptionName : pathNames)
    {
        getBootOption(
            bootOptionName,
            [asyncResp, bootOptionName, collectionPath, &members](
                const boost::system::error_code ec,
                const dbus::utility::DBusPropertiesMap& bootOptionProperties) {
                if (ec)
                {
                    messages::resourceNotFound(asyncResp->res, "BootOption",
                                               bootOptionName);
                    return;
                }
                bool enabled = true;
                bool pendingEnabled = true;
                for (const auto& [propertyName, propertyVariant] :
                     bootOptionProperties)
                {
                    if (propertyName == "Enabled" &&
                        std::holds_alternative<bool>(propertyVariant))
                    {
                        enabled = std::get<bool>(propertyVariant);
                    }
                    else if (propertyName == "PendingEnabled" &&
                             std::holds_alternative<bool>(propertyVariant))
                    {
                        pendingEnabled = std::get<bool>(propertyVariant);
                    }
                }
                if (enabled != pendingEnabled)
                {
                    asyncResp->res.jsonValue["BootOptionEnabled"] =
                        pendingEnabled;
                    boost::urls::url url = collectionPath;
                    crow::utility::appendUrlPieces(url, bootOptionName);
                    nlohmann::json::object_t member;
                    member["@odata.id"] = std::move(url);
                    members.emplace_back(std::move(member));
                    asyncResp->res.jsonValue["Members@odata.count"] =
                        members.size();
                }
            });
        asyncResp->res.jsonValue["Members@odata.count"] = members.size();
    }
}

inline void handleBootOptionCollectionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    [[maybe_unused]] const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }

    aResp->res.jsonValue["@odata.type"] =
        "#BootOptionCollection.BootOptionCollection";
    aResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/BootOptions";
    aResp->res.jsonValue["Name"] = "Boot Option Collection";
    constexpr std::array<std::string_view, 1> interfaces{
        "xyz.openbmc_project.BIOSConfig.BootOption"};
    collection_util::getCollectionMembers(
        aResp,
        boost::urls::url(
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/BootOptions"),
        interfaces, "/xyz/openbmc_project/");
}

inline void handleBootOptionCollectionPost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    [[maybe_unused]] const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }

    privilege_utils::isBiosPrivilege(
        req,
        [req, aResp](const boost::system::error_code ec, const bool isBios) {
            if (ec || isBios == false)
            {
                messages::insufficientPrivilege(aResp->res);
                return;
            }
            std::string newBootOptionReference;
            bool newBootOptionEnabled = true;
            std::optional<std::string> optBootOptionDescription;
            std::optional<std::string> optBootOptionDisplayName;
            std::optional<std::string> optBootOptionUefiDevicePath;
            if (!json_util::readJsonPatch(
                    req, aResp->res, "BootOptionReference",
                    newBootOptionReference, "BootOptionEnabled",
                    newBootOptionEnabled, "Description",
                    optBootOptionDescription, "DisplayName",
                    optBootOptionDisplayName, "UefiDevicePath",
                    optBootOptionUefiDevicePath))
            {
                return;
            }

            if (newBootOptionReference.empty())
            {
                messages::propertyValueIncorrect(
                    aResp->res, "BootOptionReference", newBootOptionReference);
                return;
            }
            std::string id = newBootOptionReference;

            dbus::utility::escapePathForDbus(id);
            dbus::utility::DBusPropertiesMap properties;
            properties.push_back({"Enabled", newBootOptionEnabled});
            if (optBootOptionDescription)
            {
                properties.push_back(
                    {"Description", *optBootOptionDescription});
            }
            if (optBootOptionDisplayName)
            {
                properties.push_back(
                    {"DisplayName", *optBootOptionDisplayName});
            }
            if (optBootOptionUefiDevicePath)
            {
                properties.push_back(
                    {"UefiDevicePath", *optBootOptionUefiDevicePath});
            }

            createBootOption(id, [aResp, id, properties](
                                     const boost::system::error_code ec2) {
                if (ec2)
                {
                    messages::resourceAlreadyExists(aResp->res, "BootOption",
                                                    "BootOptionReference", id);
                    return;
                }

                setBootOption(id, properties,
                              [aResp](const boost::system::error_code ec3) {
                                  if (ec3)
                                  {
                                      messages::internalError(aResp->res);
                                      return;
                                  }
                              });

                messages::created(aResp->res);
                aResp->res.addHeader(
                    boost::beast::http::field::location,
                    "/redfish/v1/Systems/" +
                        std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                        "/BootOptions/" + id);
            });
        });
}

inline void handleBootOptionGet(crow::App& app, const crow::Request& req,
                                const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                                [[maybe_unused]] const std::string& systemName,
                                const std::string& bootOptionName)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    getBootOption(
        bootOptionName,
        [aResp, bootOptionName](
            const boost::system::error_code ec,
            const dbus::utility::DBusPropertiesMap& bootOptonProperties) {
            if (ec)
            {
                messages::resourceNotFound(aResp->res, "BootOption",
                                           bootOptionName);
                return;
            }
            aResp->res.jsonValue["@odata.type"] =
                "#BootOption.v1_0_4.BootOption";
            aResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/BootOptions/" +
                bootOptionName;
            aResp->res.jsonValue["Name"] = bootOptionName;
            aResp->res.jsonValue["Id"] = bootOptionName;
            aResp->res.jsonValue["BootOptionReference"] = bootOptionName;
            for (const auto& [propertyName, propertyVariant] :
                 bootOptonProperties)
            {
                if (propertyName == "Enabled" &&
                    std::holds_alternative<bool>(propertyVariant))
                {
                    aResp->res.jsonValue["BootOptionEnabled"] =
                        std::get<bool>(propertyVariant);
                }
                else if (propertyName == "Description" &&
                         std::holds_alternative<std::string>(propertyVariant))
                {
                    aResp->res.jsonValue["Description"] =
                        std::get<std::string>(propertyVariant);
                }
                else if (propertyName == "DisplayName" &&
                         std::holds_alternative<std::string>(propertyVariant))
                {
                    aResp->res.jsonValue["DisplayName"] =
                        std::get<std::string>(propertyVariant);
                }
                else if (propertyName == "UefiDevicePath" &&
                         std::holds_alternative<std::string>(propertyVariant))
                {
                    aResp->res.jsonValue["UefiDevicePath"] =
                        std::get<std::string>(propertyVariant);
                }
            }
        });
}

/**
 * @brief Retrieves host boot order properties over DBUS
 *
 * @param[in] aResp     Shared pointer for completing asynchronous calls.
 * @param[in] bootOptionName  Boot option name.
 *
 * @return None.
 */
inline void
    getBootPendingEnable(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& bootOptionName)
{
    getBootOption(
        bootOptionName,
        [asyncResp, bootOptionName](
            const boost::system::error_code ec,
            const dbus::utility::DBusPropertiesMap& bootOptionProperties) {
            if (ec)
            {
                messages::resourceNotFound(asyncResp->res, "BootOption",
                                           bootOptionName);
                return;
            }
            bool enabled = true;
            bool pendingEnabled = true;
            for (const auto& [propertyName, propertyVariant] :
                 bootOptionProperties)
            {
                if (propertyName == "Enabled" &&
                    std::holds_alternative<bool>(propertyVariant))
                {
                    enabled = std::get<bool>(propertyVariant);
                }
                else if (propertyName == "PendingEnabled" &&
                         std::holds_alternative<bool>(propertyVariant))
                {
                    pendingEnabled = std::get<bool>(propertyVariant);
                }
            }
            if (enabled != pendingEnabled)
            {
                asyncResp->res.jsonValue["BootOptionEnabled"] = pendingEnabled;
            }
        });
}

inline void handleBootOptionPatch(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    [[maybe_unused]] const std::string& systemName,
    const std::string& bootOptionName)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    privilege_utils::isBiosPrivilege(
        req, [req, aResp, bootOptionName](const boost::system::error_code ec,
                                          const bool isBios) {
            if (ec || isBios == false)
            {
                messages::insufficientPrivilege(aResp->res);
                return;
            }
            bool newBootOptionEnabled = true;
            if (!json_util::readJsonPatch(req, aResp->res, "BootOptionEnabled",
                                          newBootOptionEnabled))
            {
                return;
            }

            dbus::utility::DBusPropertiesMap properties;
            properties.push_back({"Enabled", newBootOptionEnabled});
            setBootOption(
                bootOptionName, properties,
                [aResp, bootOptionName](const boost::system::error_code ec) {
                    if (ec == boost::system::errc::no_such_device_or_address)
                    {
                        messages::resourceNotFound(aResp->res, "BootOption",
                                                   bootOptionName);
                        return;
                    }
                    if (ec)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.result(boost::beast::http::status::no_content);
                });
        });
}

inline void handleBootOptionDelete(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    [[maybe_unused]] const std::string& systemName,
    const std::string& bootOptionName)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }

    privilege_utils::isBiosPrivilege(
        req, [aResp, bootOptionName](const boost::system::error_code ec,
                                     const bool isBios) {
            if (ec || isBios == false)
            {
                messages::insufficientPrivilege(aResp->res);
                return;
            }
            deleteBootOption(
                bootOptionName,
                [aResp, bootOptionName](const boost::system::error_code ec2) {
                    if (ec2)
                    {
                        messages::resourceNotFound(aResp->res, "BootOption",
                                                   bootOptionName);
                        return;
                    }
                    aResp->res.result(boost::beast::http::status::no_content);
                });
        });
}

inline void handleComputerSystemSettingsBootOptionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& systemName,
    const std::string& bootOptionName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] = "#BootOption.v1_0_4.BootOption";
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Settings/BootOptions/" + bootOptionName;
    asyncResp->res.jsonValue["Name"] = bootOptionName;
    asyncResp->res.jsonValue["Id"] = bootOptionName;
    asyncResp->res.jsonValue["BootOptionReference"] = bootOptionName;
    boot_options::getBootPendingEnable(asyncResp, bootOptionName);
}

inline void handleComputerSystemSettingsBootOptionsCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    BMCWEB_LOG_DEBUG("Start getting system settings boot options");
    asyncResp->res.jsonValue["@odata.type"] =
        "#BootOptionCollection.BootOptionCollection";
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Settings/BootOptions";
    asyncResp->res.jsonValue["Name"] = "Settings Boot Option Collection";
    constexpr std::array<std::string_view, 1> interfaces{
        "xyz.openbmc_project.BIOSConfig.BootOption"};
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/", 0, interfaces,
        std::bind_front(
            handleCollectionPendingBootOptionMembers, asyncResp,
            boost::urls::url("/redfish/v1/Systems/" +
                             std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                             "/Settings/BootOptions")));
}

inline void handleComputerSystemSettingsBootOptionPatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& systemName,
    const std::string& bootOptionName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    bool bootOptionEnabled;

    if (!json_util::readJsonPatch(req, asyncResp->res, "BootOptionEnabled",
                                  bootOptionEnabled))
    {
        BMCWEB_LOG_ERROR("Error with patch string");

        return;
    }

    dbus::utility::DBusPropertiesMap properties;
    properties.push_back({"PendingEnabled", bootOptionEnabled});
    boot_options::setBootOption(
        bootOptionName, properties,
        [asyncResp](const boost::system::error_code ec) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                BMCWEB_LOG_ERROR("Error when setting the pending boot "
                                 "option enabled property: ",
                                 ec.message());
                return;
            }
        });
}
} // namespace boot_options

inline void requestRoutesBootOptions(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/BootOptions/")
        .privileges(redfish::privileges::getBootOptionCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            boot_options::handleBootOptionCollectionGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/BootOptions/")
        .privileges(redfish::privileges::postBootOptionCollection)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            boot_options::handleBootOptionCollectionPost, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/BootOptions/<str>/")
        .privileges(redfish::privileges::getBootOption)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(boot_options::handleBootOptionGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/BootOptions/<str>/")
        .privileges(redfish::privileges::patchBootOption)
        .methods(boost::beast::http::verb::patch)(std::bind_front(
            boot_options::handleBootOptionPatch, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/BootOptions/<str>/")
        .privileges(redfish::privileges::deleteBootOption)
        .methods(boost::beast::http::verb::delete_)(std::bind_front(
            boot_options::handleBootOptionDelete, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Settings/BootOptions")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            boot_options::handleComputerSystemSettingsBootOptionsCollectionGet,
            std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Settings/BootOptions/<str>/")
        .privileges(redfish::privileges::patchComputerSystem)
        .methods(boost::beast::http::verb::patch)(std::bind_front(
            boot_options::handleComputerSystemSettingsBootOptionPatch,
            std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Settings/BootOptions/<str>/")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            boot_options::handleComputerSystemSettingsBootOptionGet,
            std::ref(app)));
}

} // namespace redfish
