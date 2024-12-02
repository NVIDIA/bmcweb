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
#include <boost/algorithm/string/split.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/json_utils.hpp>

#include <iostream>
#include <regex>
#include <variant>
#include <vector>

namespace redfish
{

const std::vector<const char*> trustedComponentInterfaces = {
    "xyz.openbmc_project.Inventory.Item.Tpm"};

inline void trustedComponentGetAllProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path,
    const std::string& interface)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const std::vector<
                        std::pair<std::string, dbus::utility::DbusVariantType>>&
                        propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error for trustedComponent properties");
                messages::internalError(asyncResp->res);
                return;
            }
            for (const auto& [propertyName, propertyVariant] : propertiesList)
            {
                if (propertyName == "Manufacturer" &&
                    std::holds_alternative<std::string>(propertyVariant))
                {
                    asyncResp->res.jsonValue["Manufacturer"] =
                        std::get<std::string>(propertyVariant);
                }
                else if (propertyName == "PrettyName" &&
                         std::holds_alternative<std::string>(propertyVariant))
                {
                    asyncResp->res.jsonValue["Description"] =
                        std::get<std::string>(propertyVariant);
                }
                else if (propertyName == "Version" &&
                         std::holds_alternative<std::string>(propertyVariant))
                {
                    asyncResp->res.jsonValue["FirmwareVersion"] =
                        std::get<std::string>(propertyVariant);
                }
            }
        },
        service, path, "org.freedesktop.DBus.Properties", "GetAll", interface);
}

inline void handleTrustedComponentsCollectionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisID,
        [asyncResp,
         chassisID](const std::optional<std::string>& validChassisPath) {
            if (!validChassisPath)
            {
                BMCWEB_LOG_ERROR("Cannot get validChassisPath");
                messages::internalError(asyncResp->res);
                return;
            }
            const std::string collectionPath =
                "/redfish/v1/Chassis/" + chassisID + "/TrustedComponents";
            asyncResp->res.jsonValue["@odata.type"] =
                "#TrustedComponentCollection.TrustedComponentCollection";
            asyncResp->res.jsonValue["Name"] = "Trusted Component Collection";
            asyncResp->res.jsonValue["@odata.id"] = collectionPath;
            asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
            constexpr std::array<std::string_view, 1> interfaces{
                "xyz.openbmc_project.Inventory.Item.Tpm"};
            redfish::collection_util::getCollectionMembers(
                asyncResp, boost::urls::url(collectionPath), interfaces,
                validChassisPath->c_str());
        });
}

inline void handleTrustedComponentGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& componentID)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisID,
        [asyncResp, chassisID,
         componentID](const std::optional<std::string>& validChassisPath) {
            if (!validChassisPath)
            {
                BMCWEB_LOG_ERROR("Cannot get validChassisPath");
                messages::internalError(asyncResp->res);
                return;
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp, chassisID, componentID](
                    const boost::system::error_code ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("error_code = {}", ec);
                        BMCWEB_LOG_ERROR("error msg = {}", ec.message());
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (const auto& [path, services] : subtree)
                    {
                        sdbusplus::message::object_path opath(path);
                        std::string componentName = opath.filename();
                        if (componentName != componentID)
                        {
                            continue;
                        }

                        asyncResp->res.jsonValue["@odata.type"] =
                            "#TrustedComponent.v1_0_0.TrustedComponent";
                        asyncResp->res.jsonValue["@odata.id"] =
                            std::string("/redfish/v1/Chassis/")
                                .append(chassisID)
                                .append("/TrustedComponents/")
                                .append(componentID);
                        asyncResp->res.jsonValue["Id"] = componentID;
                        asyncResp->res.jsonValue["Name"] = componentID;
                        asyncResp->res.jsonValue["TrustedComponentType"] =
                            "Discrete";
                        for (const auto& [service, interfaces] : services)
                        {
                            for (const auto& interface : interfaces)
                            {
                                if (interface ==
                                        "xyz.openbmc_project.Inventory.Decorator.Asset" ||
                                    interface ==
                                        "xyz.openbmc_project.Inventory.Item" ||
                                    interface ==
                                        "xyz.openbmc_project.Software.Version")
                                {
                                    trustedComponentGetAllProperties(
                                        asyncResp, service, path, interface);
                                }
                            }
                        }

                        return; // component has been found
                    }

                    BMCWEB_LOG_ERROR("Cannot find trustedComponent");
                    messages::internalError(asyncResp->res);
                    return;
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                *validChassisPath, static_cast<int32_t>(0),
                trustedComponentInterfaces);
        });
}

inline void requestRoutesTrustedComponents(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/TrustedComponents/")
        .privileges(redfish::privileges::privilegeSetLogin)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleTrustedComponentsCollectionGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/TrustedComponents/<str>/")
        .privileges(redfish::privileges::privilegeSetLogin)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleTrustedComponentGet, std::ref(app)));
}

} // namespace redfish
