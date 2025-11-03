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
#include "registries/privilege_registry.hpp"

namespace redfish
{

namespace nvidia_oem_managed_entity
{

inline void afterGetManagedEntityProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& entityGroupId, const std::string& entityId,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec.value() == EBADR || ec == boost::system::errc::host_unreachable ||
        ec == boost::system::errc::io_error)
    {
        messages::resourceNotFound(asyncResp->res, "ManagedEntity", entityId);
        return;
    }

    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "D-Bus response error on GetManagedEntityProperties {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    std::string name;
    std::string protocol;
    std::string ipv4Address;
    std::string ipv6Address;
    uint64_t port = 0;

    // clang-format off
    bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties, "IPv4Address",
        ipv4Address, "IPv6Address", ipv6Address, "Name", name, "Port", port,
        "TransportProtocol", protocol);
    // clang-format on

    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/ManagedEntityGroups/{}/ManagedEntities/{}",
        BMCWEB_REDFISH_MANAGER_URI_NAME, entityGroupId, entityId);
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaManagedEntity.v1_0_0.NvidiaManagedEntity";
    asyncResp->res.jsonValue["Id"] = entityId;
    asyncResp->res.jsonValue["Name"] = name;
    if (!ipv4Address.empty())
    {
        nlohmann::json ipv4AddressJson = {{"Address", ipv4Address}};
        asyncResp->res.jsonValue["IPv4Address"] = ipv4AddressJson;
    }
    if (!ipv6Address.empty())
    {
        nlohmann::json ipv6AddressJson = {{"Address", ipv6Address}};
        asyncResp->res.jsonValue["IPv6Address"] = ipv6AddressJson;
    }
    asyncResp->res.jsonValue["Port"] = port;
    asyncResp->res.jsonValue["TransportProtocol"] = protocol;
}

inline void afterCreateMemberManagedEntityCollection(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::object_path& dbusPath,
    const std::optional<std::string>& newIPv4Address,
    const std::optional<std::string>& newIPv6Address,
    const std::optional<uint64_t>& newPort,
    const std::optional<std::string>& newProtocol,
    const boost::system::error_code ec)
{
    if (ec.value() == EBADR || ec == boost::system::errc::host_unreachable ||
        ec == boost::system::errc::io_error)
    {
        messages::resourceNotFound(asyncResp->res, "ManagedEntityCollection",
                                   dbusPath.filename());
        return;
    }

    if (ec)
    {
        BMCWEB_LOG_ERROR("Error occurred in creating Managed Entity: {}",
                         ec.message());
        messages::internalError(asyncResp->res);
        return;
    }

    if (newIPv4Address)
    {
        setDbusProperty(asyncResp, "IPv4Address",
                        "com.Nvidia.RackPowerCompliance", dbusPath,
                        "com.Nvidia.State.PowerCompliance.ManagedEntity",
                        "IPv4Address", *newIPv4Address);
    }

    if (newIPv6Address)
    {
        setDbusProperty(asyncResp, "IPv6Address",
                        "com.Nvidia.RackPowerCompliance", dbusPath,
                        "com.Nvidia.State.PowerCompliance.ManagedEntity",
                        "IPv6Address", *newIPv6Address);
    }

    if (newPort)
    {
        setDbusProperty(
            asyncResp, "Port", "com.Nvidia.RackPowerCompliance", dbusPath,
            "com.Nvidia.State.PowerCompliance.ManagedEntity", "Port", *newPort);
    }

    if (newProtocol)
    {
        setDbusProperty(asyncResp, "TransportProtocol",
                        "com.Nvidia.RackPowerCompliance", dbusPath,
                        "com.Nvidia.State.PowerCompliance.ManagedEntity",
                        "TransportProtocol", *newProtocol);
    }

    messages::success(asyncResp->res);
}

inline void handleManagedEntityGetRequest(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& groupId,
    const std::string& entityId)
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

    sdbusplus::message::object_path dbusPath(
        "/com/nvidia/state/power_compliance/managed_entity_group");
    dbusPath /= groupId;
    dbusPath /= "managed_entity";
    dbusPath /= entityId;

    // Get all properties from D-Bus
    std::function<void(const boost::system::error_code&,
                       const dbus::utility::DBusPropertiesMap&)>
        callback = std::bind_front(afterGetManagedEntityProperties, asyncResp,
                                   groupId, entityId);

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, "com.Nvidia.RackPowerCompliance",
        dbusPath, "com.Nvidia.State.PowerCompliance.ManagedEntity",
        std::move(callback));
}

inline void handleManagedEntityCollectionGetRequest(
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

    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaManagedEntityCollection.NvidiaManagedEntityCollection";
    asyncResp->res.jsonValue["Name"] = "Managed Entities Collection";

    // Dynamically construct the D-Bus paths based on the powerPolicyId
    sdbusplus::message::object_path dbusPath(
        "/com/nvidia/state/power_compliance/managed_entity_group");
    dbusPath /= groupId;
    dbusPath /= "managed_entity";

    nvidia_oem_power_profile::handlePowerProfileCollectionGetRequest(
        app, dbusPath, "com.Nvidia.State.PowerCompliance.ManagedEntity",
        boost::urls::format(
            "/redfish/v1/Managers/{}/Oem/Nvidia/PowerCompliance/ManagedEntityGroups/{}/ManagedEntities",
            BMCWEB_REDFISH_MANAGER_URI_NAME, groupId),
        req, asyncResp, managerId);
}

inline void handleManagedEntityPatchRequest(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& entityGroupId,
    const std::string& entityId)
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

    sdbusplus::message::object_path dbusPath(
        "/com/nvidia/state/power_compliance/managed_entity_group");
    dbusPath /= entityGroupId;
    dbusPath /= "managed_entity";
    dbusPath /= entityId;

    std::optional<std::string> newIPv4Address;
    std::optional<std::string> newIPv6Address;
    std::optional<std::string> newName;
    std::optional<uint64_t> newPort;
    std::optional<std::string> newProtocol;

    if (!json_util::readJsonPatch(
            req, asyncResp->res, "IPv4Address/Address", newIPv4Address,
            "IPv6Address/Address", newIPv6Address, "Name", newName, "Port",
            newPort, "TransportProtocol", newProtocol))
    {
        return;
    }

    if (newIPv4Address)
    {
        setDbusProperty(asyncResp, "IPv4Address",
                        "com.Nvidia.RackPowerCompliance", dbusPath,
                        "com.Nvidia.State.PowerCompliance.ManagedEntity",
                        "IPv4Address", *newIPv4Address);
    }

    if (newIPv6Address)
    {
        setDbusProperty(asyncResp, "IPv6Address",
                        "com.Nvidia.RackPowerCompliance", dbusPath,
                        "com.Nvidia.State.PowerCompliance.ManagedEntity",
                        "IPv6Address", *newIPv6Address);
    }

    if (newName)
    {
        setDbusProperty(
            asyncResp, "Name", "com.Nvidia.RackPowerCompliance", dbusPath,
            "com.Nvidia.State.PowerCompliance.ManagedEntity", "Name", *newName);
    }

    if (newPort)
    {
        setDbusProperty(
            asyncResp, "Port", "com.Nvidia.RackPowerCompliance", dbusPath,
            "com.Nvidia.State.PowerCompliance.ManagedEntity", "Port", *newPort);
    }

    if (newProtocol)
    {
        setDbusProperty(asyncResp, "TransportProtocol",
                        "com.Nvidia.RackPowerCompliance", dbusPath,
                        "com.Nvidia.State.PowerCompliance.ManagedEntity",
                        "TransportProtocol", *newProtocol);
    }
}

inline void handleManagedEntityCollectionPostRequest(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& entityGroupId)
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

    std::optional<std::string> newId;
    std::optional<std::string> newName;
    std::optional<std::string> newIPv4Address;
    std::optional<std::string> newIPv6Address;
    std::optional<uint64_t> newPort;
    std::optional<std::string> newProtocol;

    if (!json_util::readJsonAction(
            req, asyncResp->res, "Id", newId, "Name", newName,
            "IPv4Address/Address", newIPv4Address, "IPv6Address/Address",
            newIPv6Address, "Port", newPort, "TransportProtocol", newProtocol))
    {
        return;
    }

    if (!newName.has_value() || newName->empty())
    {
        return;
    }

    if (!newId.has_value() || newId->empty())
    {
        return;
    }

    sdbusplus::message::object_path dbusGroupPath(
        "/com/nvidia/state/power_compliance/managed_entity_group");
    dbusGroupPath /= entityGroupId;
    dbusGroupPath /= "managed_entity";

    std::string entityId = *newId;
    sdbusplus::message::object_path dbusPath(
        "/com/nvidia/state/power_compliance/managed_entity_group");
    dbusPath /= entityGroupId;
    dbusPath /= "managed_entity";
    dbusPath /= entityId;

    std::function<void(const boost::system::error_code&)> callback =
        std::bind_front(afterCreateMemberManagedEntityCollection, asyncResp,
                        dbusPath, newIPv4Address, newIPv6Address, newPort,
                        newProtocol);

    dbus::utility::async_method_call(
        std::move(callback), "com.Nvidia.RackPowerCompliance", dbusGroupPath,
        "com.Nvidia.State.PowerCompliance.PowerProfileCollection",
        "CreateMember", entityId, entityGroupId, *newName);
}

inline void handleManagedEntityDeleteRequest(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& entityGroupId,
    const std::string& entityId)
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

    sdbusplus::message::object_path dbusGroupPath(
        "/com/nvidia/state/power_compliance/managed_entity_group");
    dbusGroupPath /= entityGroupId;
    dbusGroupPath /= "managed_entity";

    dbus::utility::async_method_call(
        [asyncResp, entityGroupId,
         entityId](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Error occurred in deleting Managed Entity");
                messages::internalError(asyncResp->res);
                return;
            }

            messages::success(asyncResp->res);
        },
        "com.Nvidia.RackPowerCompliance", dbusGroupPath,
        "com.Nvidia.State.PowerCompliance.PowerProfileCollection",
        "DeleteMember", entityId);
}

inline void requestRoutesNvidiaManagedEntity(App& app)
{
    /**
     * Define the GET route for ManagedEntity
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/ManagedEntityGroups/<str>/ManagedEntities/<str>/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleManagedEntityGetRequest, std::ref(app)));

    /**
     * Define the PATCH route for Managed Entity
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/ManagedEntityGroups/<str>/ManagedEntities/<str>/")
        .privileges(redfish::privileges::patchManager)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleManagedEntityPatchRequest, std::ref(app)));

    /**
     * Define the DELETE route for Managed Entity
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/ManagedEntityGroups/<str>/ManagedEntities/<str>/")
        .privileges(redfish::privileges::deleteManager)
        .methods(boost::beast::http::verb::delete_)(
            std::bind_front(handleManagedEntityDeleteRequest, std::ref(app)));

    /**
     * Define the GET route for ManagedEntityCollection
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/ManagedEntityGroups/<str>/ManagedEntities/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleManagedEntityCollectionGetRequest, std::ref(app)));

    /**
     * Define the POST route for ManagedEntityCollection
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/PowerCompliance/ManagedEntityGroups/<str>/ManagedEntities/")
        .privileges(redfish::privileges::postManager)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleManagedEntityCollectionPostRequest, std::ref(app)));
}

} // namespace nvidia_oem_managed_entity

} // namespace redfish
