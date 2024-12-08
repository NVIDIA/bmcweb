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

#include "nvidia_oem_power_profile.hpp"

namespace redfish
{

namespace nvidia_oem_managed_entity_group
{

inline void afterGetManagedEntityGroupProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& entityGroupId, const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec.value() == EBADR || ec == boost::system::errc::host_unreachable ||
        ec == boost::system::errc::io_error)
    {
        messages::resourceNotFound(asyncResp->res, "ManagedEntityGroup",
                                   entityGroupId);
        return;
    }

    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "D-Bus response error on GetManagedEntityGroupProperties {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    std::string name;
    std::string description;
    std::string currentManagedEntityId;
    sdbusplus::message::object_path managedEntitiesDbusPath;

    // clang-format off
    bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties, "Name", name,
        "Description", description, "CurrentManagedEntityId",
        currentManagedEntityId, "ManagedEntities", managedEntitiesDbusPath);
    // clang-format on

    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia_PowerCompliance/ManagedEntityGroups/{}",
        BMCWEB_REDFISH_MANAGER_URI_NAME, entityGroupId);
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaManagedEntityGroup.v1_0_0.NvidiaManagedEntityGroup";
    asyncResp->res.jsonValue["Id"] = entityGroupId;
    asyncResp->res.jsonValue["Name"] = name;
    asyncResp->res.jsonValue["Description"] = description;
    asyncResp->res.jsonValue["CurrentManagedEntityId"] = currentManagedEntityId;

    if (!managedEntitiesDbusPath.str.empty())
    {
        asyncResp->res
            .jsonValue["ManagedEntities"]["@odata.id"] = boost::urls::format(
            "/redfish/v1/Managers/{}/Oem/Nvidia_PowerCompliance/ManagedEntityGroups/{}/ManagedEntities",
            BMCWEB_REDFISH_MANAGER_URI_NAME, entityGroupId);
    }
}

inline void handleManagedEntityGroupGetRequest(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& groupId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "#Manager",
                                   BMCWEB_REDFISH_MANAGER_URI_NAME);
        return;
    }

    // Dynamically construct the D-Bus paths based on the groupId
    sdbusplus::message::object_path dbusPath(
        "/com/nvidia/state/power_compliance/managed_entity_group");
    dbusPath /= groupId;

    // Get all properties from D-Bus
    std::function<void(const boost::system::error_code&,
                       const dbus::utility::DBusPropertiesMap&)>
        callback = std::bind_front(afterGetManagedEntityGroupProperties,
                                   asyncResp, groupId);

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, "com.Nvidia.RackPowerCompliance",
        dbusPath, "com.Nvidia.State.PowerCompliance.ManagedEntityGroup",
        std::move(callback));
}

inline void handleManagedEntityGroupPatchRequest(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& groupId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "#Manager",
                                   BMCWEB_REDFISH_MANAGER_URI_NAME);
        return;
    }

    // Dynamically construct the D-Bus paths based on the groupId
    sdbusplus::message::object_path dbusPath(
        "/com/nvidia/state/power_compliance/managed_entity_group");
    dbusPath /= groupId;

    std::optional<std::string> newCurrentManagedEntityId;

    if (!json_util::readJsonPatch(req, asyncResp->res, "CurrentManagedEnttiyId",
                                  newCurrentManagedEntityId))
    {
        return;
    }

    if (newCurrentManagedEntityId)
    {
        setDbusProperty(asyncResp, "Value", "com.Nvidia.RackPowerCompliance",
                        dbusPath,
                        "com.Nvidia.State.PowerCompliance.ManagedEntityGroup",
                        "CurrentManagedEntityId", *newCurrentManagedEntityId);
    }
}

inline void handleManagedEntityGroupCollectionGetRequest(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "#Manager",
                                   BMCWEB_REDFISH_MANAGER_URI_NAME);
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaManagedEntityGroupCollection.NvidiaManagedEntityGroupCollection";
    asyncResp->res.jsonValue["Name"] = "Managed Entity Group Collection";

    sdbusplus::message::object_path dbusPath(
        "/com/nvidia/state/power_compliance/managed_entity_group");

    nvidia_oem_power_profile::handlePowerProfileCollectionGetRequest(
        app, dbusPath, "com.Nvidia.State.PowerCompliance.ManagedEntityGroup",
        boost::urls::format(
            "/redfish/v1/Managers/{}/Oem/Nvidia_PowerCompliance/ManagedEntityGroups",
            BMCWEB_REDFISH_MANAGER_URI_NAME),
        req, asyncResp, managerId);
}

inline void requestRoutesNvidiaManagedEntityGroup(App& app)
{
    /**
     * Define the GET route for ManagedEntityGroup
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia_PowerCompliance/ManagedEntityGroups/<str>/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleManagedEntityGroupGetRequest, std::ref(app)));

    /**
     * Define the GET route for ManagedEntityGroupCollection
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia_PowerCompliance/ManagedEntityGroups/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleManagedEntityGroupCollectionGetRequest, std::ref(app)));

    /**
     * Define the PATCH route for ManagedEntityGroup
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia_PowerCompliance/ManagedEntityGroups/<str>/")
        .privileges(redfish::privileges::patchManager)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleManagedEntityGroupPatchRequest, std::ref(app)));
}

} // namespace nvidia_oem_managed_entity_group

} // namespace redfish
