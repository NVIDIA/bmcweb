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

#include <app.hpp>
#include <dbus_utility.hpp>
#include <error_messages.hpp>
#include <registries/privilege_registry.hpp>
#include <utils/json_utils.hpp>

#include <optional>
#include <regex>

namespace redfish
{
using GetObjectType =
    std::vector<std::pair<std::string, std::vector<std::string>>>;

inline void
    getInterfaceStatus(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& ifaceId)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const std::variant<bool>& nicStatus) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error for "
                             "Get NICEnabled Status for the host interface.");
            messages::internalError(asyncResp->res);
            return;
        }
        const bool* nicEnabled = std::get_if<bool>(&nicStatus);
        if (nicEnabled == nullptr)
        {
            BMCWEB_LOG_DEBUG(
                "Error reading NICEnabled Status for the host interface.");
            messages::internalError(asyncResp->res);
            return;
        }

        constexpr const std::array<const char*, 1> inventoryForEthernet = {
            "xyz.openbmc_project.Inventory.Item.Ethernet"};

        nlohmann::json& jsonResponse = asyncResp->res.jsonValue;
        auto health = std::make_shared<HealthPopulate>(asyncResp);

        crow::connections::systemBus->async_method_call(
            [health](const boost::system::error_code ec,
                     const std::vector<std::string>& resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                return;
            }
            health->inventory = resp;
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths", "/",
            int32_t(0), inventoryForEthernet);
        health->populate();
        if (*nicEnabled)
        {
            jsonResponse["Status"]["State"] = "Enabled";
            asyncResp->res.jsonValue["InterfaceEnabled"] = true;
        }
        else
        {
            jsonResponse["Status"]["State"] = "Disabled";
            asyncResp->res.jsonValue["InterfaceEnabled"] = false;
        }
    },
        "xyz.openbmc_project.Network",
        "/xyz/openbmc_project/network/" + ifaceId,
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Network.EthernetInterface", "NICEnabled");
}

inline void
    getCredentialsBootStrap(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const GetObjectType& objType) {
        if (ec || objType.empty())
        {
            BMCWEB_LOG_ERROR("GetObject for path {}",
                             redfish::bios::biosConfigObj);
            messages::internalError(asyncResp->res);
            return;
        }

        const std::string& biosService = objType.begin()->first;
        crow::connections::systemBus->async_method_call(
            [asyncResp](
                const boost::system::error_code ec,
                const std::vector<std::pair<
                    std::string,
                    std::variant<std::string, redfish::bios::BaseBIOSTable,
                                 bool>>>& propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Can't get BIOSConfig Manager!");
                messages::internalError(asyncResp->res);
                return;
            }

            nlohmann::json& credentialBootstrap =
                asyncResp->res.jsonValue["CredentialBootstrapping"];
            credentialBootstrap["RoleId"] = "Administrator";

            for (const std::pair<
                     std::string,
                     std::variant<std::string, redfish::bios::BaseBIOSTable,
                                  bool>>& property : propertiesList)
            {
                const std::string& propertyName = property.first;

                if (propertyName == "CredentialBootstrap")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Can't get 'CredentialBootstrap'!");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    credentialBootstrap["Enabled"] = *value;
                }

                if (propertyName == "EnableAfterReset")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Can't get 'EnableAfterReset'!");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    credentialBootstrap["EnableAfterReset"] = *value;
                }
            }
        },
            biosService, redfish::bios::biosConfigObj,
            "org.freedesktop.DBus.Properties", "GetAll",
            redfish::bios::biosConfigIface);
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject",
        redfish::bios::biosConfigObj,
        std::array<const char*, 1>{redfish::bios::biosConfigIface});
}

inline void
    setCredentialBootstrap(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& property, const bool& flag)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, property, flag](const boost::system::error_code ec,
                                    const GetObjectType& objType) {
        if (ec || objType.empty())
        {
            BMCWEB_LOG_ERROR("GetObject for path {}",
                             redfish::bios::biosConfigObj);
            messages::internalError(asyncResp->res);
            return;
        }

        const std::string& biosService = objType.begin()->first;

        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
        },
            biosService, redfish::bios::biosConfigObj,
            "org.freedesktop.DBus.Properties", "Set",
            redfish::bios::biosConfigIface, property, std::variant<bool>(flag));
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject",
        redfish::bios::biosConfigObj,
        std::array<const char*, 1>{redfish::bios::biosConfigIface});
}

inline void
    setInterfaceEnabled(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& ifaceId,
                        const bool& interfaceEnabled)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }
    },
        "xyz.openbmc_project.Network",
        "/xyz/openbmc_project/network/" + ifaceId,
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Network.EthernetInterface", "NICEnabled",
        std::variant<bool>(interfaceEnabled));
}

inline void requestHostInterfacesRoutes(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/HostInterfaces/")
        .privileges(redfish::privileges::getHostInterfaceCollection)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               [[maybe_unused]] const std::string& managerName) {
        asyncResp->res.jsonValue["@odata.type"] =
            "#HostInterfaceCollection.HostInterfaceCollection";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Managers/" +
            std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) + "/HostInterfaces";
        asyncResp->res.jsonValue["Name"] = "Host Interface Collection";
        asyncResp->res.jsonValue["Description"] =
            "Collection of HostInterfaces for this Manager";

        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code errorCode,
                        dbus::utility::ManagedObjectType& resp) {
            boost::container::flat_set<std::string> ifaceList;
            ifaceList.reserve(resp.size());

            if (errorCode)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            nlohmann::json& ifaceArray = asyncResp->res.jsonValue["Members"];
            ifaceArray = nlohmann::json::array();

            // Iterate over all retrieved ObjectPaths.
            for (const auto& objpath : resp)
            {
                // And all interfaces available for certain ObjectPath.
                for (const auto& interface : objpath.second)
                {
                    if (interface.first ==
                        "xyz.openbmc_project.Network.EthernetInterface")
                    {
                        std::string ifaceId = objpath.first.filename();
                        // The Host Interface is configured with
                        // 'BMCWEB_HOST_IFACE_CHANNEL' interface. Check for
                        // 'BMCWEB_HOST_IFACE_CHANNEL' interface.
                        if (ifaceId == BMCWEB_HOST_IFACE_CHANNEL)
                        {
                            ifaceArray.push_back(
                                {{"@odata.id",
                                  "/redfish/v1/Managers/" +
                                      std::string(
                                          BMCWEB_REDFISH_MANAGER_URI_NAME) +
                                      "/HostInterfaces/" + ifaceId}});
                        }
                        asyncResp->res.jsonValue["Members@odata.count"] =
                            ifaceArray.size();
                    }
                }
            }
        },
            "xyz.openbmc_project.Network", "/xyz/openbmc_project/network",
            "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    });

    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/HostInterfaces/<str>/")
        .privileges(redfish::privileges::getHostInterface)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               [[maybe_unused]] const std::string& managerName,
               const std::string& ifaceId) {
        // Check for the match of requested HostInterface Id with
        // the configured HostInterface channel
        if (ifaceId != BMCWEB_HOST_IFACE_CHANNEL)
        {
            asyncResp->res.result(boost::beast::http::status::not_found);
            return;
        }

        getInterfaceStatus(asyncResp, ifaceId);
        asyncResp->res.jsonValue["@odata.type"] =
            "#HostInterface.v1_3_0.HostInterface";
        asyncResp->res.jsonValue["Name"] = "Host Interface";
        asyncResp->res.jsonValue["Description"] = "Management Host Interface";
        asyncResp->res.jsonValue["Id"] = ifaceId;
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Managers/" +
            std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) + "/HostInterfaces/" +
            ifaceId;

        asyncResp->res.jsonValue["AuthenticationModes"] = {"BasicAuth"};
        asyncResp->res.jsonValue["ExternallyAccessible"] = false;
        asyncResp->res.jsonValue["HostInterfaceType"] = "NetworkHostInterface";

        asyncResp->res.jsonValue["Links"]["ComputerSystems@odata.count"] = 1;
        asyncResp->res.jsonValue["Links"]["ComputerSystems"] = {
            {{"@odata.id", "/redfish/v1/Systems/" +
                               std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME)}}};

        asyncResp->res.jsonValue["ManagerEthernetInterface"] = {
            {"@odata.id", "/redfish/v1/Managers/" +
                              std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                              "/EthernetInterfaces/" + ifaceId}};
        asyncResp->res.jsonValue["NetworkProtocol"] = {
            {"@odata.id", "/redfish/v1/Managers/" +
                              std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                              "/NetworkProtocol/"}};

        getCredentialsBootStrap(asyncResp);
    });

    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/HostInterfaces/<str>/")
        .privileges(redfish::privileges::patchHostInterface)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& managerName,
                   const std::string& ifaceId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        // Check for the match of requested HostInterface Id with
        // the configured HostInterface channel
        if (ifaceId != BMCWEB_HOST_IFACE_CHANNEL)
        {
            asyncResp->res.result(boost::beast::http::status::not_found);
            return;
        }

        std::optional<bool> interfaceEnabled;
        std::optional<nlohmann::json> credentialBootstrap;
        if (!json_util::readJsonAction(
                req, asyncResp->res, "InterfaceEnabled", interfaceEnabled,
                "CredentialBootstrapping", credentialBootstrap))
        {
            BMCWEB_LOG_ERROR("Bad request for PATCH HostInterface");
            asyncResp->res.result(boost::beast::http::status::bad_request);
            return;
        }

        if (interfaceEnabled)
        {
            setInterfaceEnabled(asyncResp, ifaceId, *interfaceEnabled);
        }

        if (credentialBootstrap)
        {
            std::optional<bool> enableAfterReset;
            std::optional<bool> enabled;

            if (!json_util::readJson(*credentialBootstrap, asyncResp->res,
                                     "EnableAfterReset", enableAfterReset,
                                     "Enabled", enabled))
            {
                BMCWEB_LOG_ERROR("Invalid CredentialBootstrapping object");
                asyncResp->res.result(boost::beast::http::status::bad_request);
                return;
            }

            if (enableAfterReset)
            {
                setCredentialBootstrap(asyncResp, "EnableAfterReset",
                                       *enableAfterReset);
            }

            if (enabled)
            {
                setCredentialBootstrap(asyncResp, "CredentialBootstrap",
                                       *enabled);
            }
        }
    });
}
} // namespace redfish
