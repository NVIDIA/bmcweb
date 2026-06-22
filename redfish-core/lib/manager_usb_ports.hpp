// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION &
// AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once
#include "bmcweb_config.h"

#include "app.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/port.hpp"
#include "generated/enums/protocol.hpp"
#include "generated/enums/resource.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"

#include <sdbusplus/message/native_types.hpp>

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace redfish
{
namespace manager_usb_ports
{

inline void afterGetUSBPortEnabled(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, bool enabled)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Failed to get USB port Enabled: {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }
    asyncResp->res.jsonValue["InterfaceEnabled"] = enabled;
}

inline void handleUSBPortCollectionGet(
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
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] = "#PortCollection.PortCollection";
    asyncResp->res.jsonValue["Name"] = "USB Port Collection";
    asyncResp->res.jsonValue["@odata.id"] =
        boost::urls::format("/redfish/v1/Managers/{}/USBPorts", managerId);

    constexpr std::array<std::string_view, 1> interfaces{
        "xyz.openbmc_project.Object.Enable"};
    collection_util::getCollectionMembers(
        asyncResp,
        boost::urls::format("/redfish/v1/Managers/{}/USBPorts", managerId),
        interfaces, "/xyz/openbmc_project/control/port");
}

inline void afterGetUSBPortService(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& portId,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec || object.empty())
    {
        messages::resourceNotFound(asyncResp->res, "Port", portId);
        return;
    }

    sdbusplus::object_path objPath("/xyz/openbmc_project/control/port");
    objPath /= portId;
    const std::string& service = object.begin()->first;

    asyncResp->res.jsonValue["@odata.type"] = "#Port.v1_4_0.Port";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/USBPorts/{}", managerId, portId);
    asyncResp->res.jsonValue["Id"] = portId;
    asyncResp->res.jsonValue["Name"] = portId;
    asyncResp->res.jsonValue["PortProtocol"] = protocol::Protocol::USB;
    asyncResp->res.jsonValue["PortType"] = port::PortType::DownstreamPort;
    asyncResp->res.jsonValue["Status"]["Health"] = resource::Health::OK;
    asyncResp->res.jsonValue["Status"]["State"] = resource::State::Enabled;

    dbus::utility::getProperty<bool>(
        service, objPath.str, "xyz.openbmc_project.Object.Enable", "Enabled",
        std::bind_front(afterGetUSBPortEnabled, asyncResp));
}

inline void handleUSBPortGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& portId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }

    sdbusplus::object_path objPath("/xyz/openbmc_project/control/port");
    objPath /= portId;

    constexpr std::array<std::string_view, 1> interfaces{
        "xyz.openbmc_project.Object.Enable"};
    dbus::utility::getDbusObject(
        objPath.str, interfaces,
        std::bind_front(afterGetUSBPortService, asyncResp, managerId, portId));
}

inline void afterSetUSBPortService(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& portId, bool newEnabled,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec || object.empty())
    {
        messages::resourceNotFound(asyncResp->res, "Port", portId);
        return;
    }

    sdbusplus::object_path objPath("/xyz/openbmc_project/control/port");
    objPath /= portId;
    const std::string& service = object.begin()->first;

    setDbusProperty(asyncResp, "InterfaceEnabled", service, objPath,
                    "xyz.openbmc_project.Object.Enable", "Enabled", newEnabled);
}

inline void handleUSBPortPatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& portId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }

    std::optional<bool> interfaceEnabled;
    if (!json_util::readJsonPatch(req, asyncResp->res, "InterfaceEnabled",
                                  interfaceEnabled))
    {
        return;
    }
    if (!interfaceEnabled)
    {
        return;
    }

    sdbusplus::object_path objPath("/xyz/openbmc_project/control/port");
    objPath /= portId;

    constexpr std::array<std::string_view, 1> interfaces{
        "xyz.openbmc_project.Object.Enable"};
    dbus::utility::getDbusObject(
        objPath.str, interfaces,
        std::bind_front(afterSetUSBPortService, asyncResp, portId,
                        *interfaceEnabled));
}

inline void requestRoutesManagerUSBPorts(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/USBPorts/")
        .privileges(redfish::privileges::getPortCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleUSBPortCollectionGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/USBPorts/<str>/")
        .privileges(redfish::privileges::getPort)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleUSBPortGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/USBPorts/<str>/")
        .privileges(redfish::privileges::patchPort)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleUSBPortPatch, std::ref(app)));
}

} // namespace manager_usb_ports
} // namespace redfish
