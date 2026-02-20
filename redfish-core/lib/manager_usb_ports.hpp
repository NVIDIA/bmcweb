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
#include "query.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"

namespace redfish
{
namespace manager_usb_ports
{

constexpr std::string_view usbPortIface = "xyz.openbmc_project.Object.Enable";
constexpr std::string_view usbPortSubtree = "/xyz/openbmc_project/control/port";

// ---------------------------------------------------------------------------
// Collection GET — /redfish/v1/Managers/{managerId}/USBPorts/
// ---------------------------------------------------------------------------
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

    dbus::utility::getSubTree(
        std::string(usbPortSubtree), 0,
        std::array<std::string_view, 1>{usbPortIface},
        [asyncResp,
         managerId](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec || subtree.empty())
            {
                asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
                asyncResp->res.jsonValue["Members@odata.count"] = 0;
                return;
            }
            nlohmann::json& members = asyncResp->res.jsonValue["Members"];
            members = nlohmann::json::array();
            for (const auto& [objPath, serviceMap] : subtree)
            {
                std::string portId = objPath.substr(objPath.rfind('/') + 1);
                members.push_back(
                    {{"@odata.id",
                      boost::urls::format("/redfish/v1/Managers/{}/USBPorts/{}",
                                          managerId, portId)}});
            }
            asyncResp->res.jsonValue["Members@odata.count"] = members.size();
        });
}

// ---------------------------------------------------------------------------
// Single Port GET callback — runs after ObjectMapper returns the service
// ---------------------------------------------------------------------------
inline void afterGetUSBPortService(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& portId,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec || object.empty())
    {
        BMCWEB_LOG_ERROR("USB port D-Bus object not found: {}", ec.message());
        messages::resourceNotFound(asyncResp->res, "Port", portId);
        return;
    }

    std::string objPath = std::string(usbPortSubtree) + "/" + portId;

    asyncResp->res.jsonValue["@odata.type"] = "#Port.v1_4_0.Port";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/USBPorts/{}", managerId, portId);
    asyncResp->res.jsonValue["Id"] = portId;
    asyncResp->res.jsonValue["Name"] = "SMM USB-C Host Port";
    asyncResp->res.jsonValue["PortProtocol"] = "USB";
    asyncResp->res.jsonValue["PortType"] = "UpstreamPort";

    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, object.begin()->first, objPath,
        std::string(usbPortIface), "Enabled",
        [asyncResp](const boost::system::error_code& ec2, const bool enabled) {
            if (ec2)
            {
                BMCWEB_LOG_ERROR("Failed to get USB port Enabled: {}",
                                 ec2.message());
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["InterfaceEnabled"] = enabled;
        });
}

// ---------------------------------------------------------------------------
// Single Port GET — /redfish/v1/Managers/{managerId}/USBPorts/{portId}
// ---------------------------------------------------------------------------
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

    std::string objPath = std::string(usbPortSubtree) + "/" + portId;

    dbus::utility::getDbusObject(
        objPath, std::array<std::string_view, 1>{usbPortIface},
        std::bind_front(afterGetUSBPortService, asyncResp, managerId, portId));
}

// ---------------------------------------------------------------------------
// Single Port PATCH callback — runs after ObjectMapper returns the service
// ---------------------------------------------------------------------------
inline void afterSetUSBPortService(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& portId, bool newEnabled,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec || object.empty())
    {
        BMCWEB_LOG_ERROR("USB port D-Bus object not found: {}", ec.message());
        messages::resourceNotFound(asyncResp->res, "Port", portId);
        return;
    }

    std::string objPath = std::string(usbPortSubtree) + "/" + portId;
    const std::string service = object.begin()->first;

    sdbusplus::asio::setProperty(
        *crow::connections::systemBus, service, objPath,
        std::string(usbPortIface), "Enabled", newEnabled,
        [asyncResp](const boost::system::error_code& ec2) {
            if (ec2)
            {
                BMCWEB_LOG_ERROR("Failed to set USB port Enabled: {}",
                                 ec2.message());
                messages::internalError(asyncResp->res);
                return;
            }
            messages::success(asyncResp->res);
        });
}

// ---------------------------------------------------------------------------
// Single Port PATCH — /redfish/v1/Managers/{managerId}/USBPorts/{portId}
// ---------------------------------------------------------------------------
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

    std::string objPath = std::string(usbPortSubtree) + "/" + portId;

    dbus::utility::getDbusObject(
        objPath, std::array<std::string_view, 1>{usbPortIface},
        std::bind_front(afterSetUSBPortService, asyncResp, portId,
                        *interfaceEnabled));
}

// ---------------------------------------------------------------------------
// Route Registration
// ---------------------------------------------------------------------------
inline void requestRoutesManagerUSBPorts(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/USBPorts/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleUSBPortCollectionGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/USBPorts/<str>/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleUSBPortGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/USBPorts/<str>/")
        .privileges(redfish::privileges::patchManager)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleUSBPortPatch, std::ref(app)));
}

} // namespace manager_usb_ports
} // namespace redfish
