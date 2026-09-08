/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http_request.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/nvidia_fabric_utils.hpp"
#include "utils/nvidia_port_health_utils.hpp"
#include "utils/port_utils.hpp"

#include <asm-generic/errno.h>

#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace redfish
{
namespace nvidia
{

/**
 * @brief Continue the clear once the ports of the processor or switch are
 *        known (endpoints of its all_states association).
 *
 * The association is optional: EBADR means it is absent, so the parent has
 * no ports and the requested port is reported as not found. Any other D-Bus
 * error is a backend failure.
 */
inline void afterGetPortsForClearEarlyHealthIndication(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& portId, const boost::system::error_code& ec,
    const std::vector<std::string>& portPaths)
{
    if (ec)
    {
        if (ec.value() == EBADR)
        {
            // No all_states association: the parent has no ports.
            BMCWEB_LOG_DEBUG("No all_states association for port {}", portId);
            messages::resourceNotFound(asyncResp->res, "Port", portId);
            return;
        }
        BMCWEB_LOG_ERROR("DBUS response error {} reading all_states for {}", ec,
                         portId);
        messages::internalError(asyncResp->res);
        return;
    }

    std::string portPath = port_utils::getPortPathByPortId(portPaths, portId);
    if (portPath.empty())
    {
        messages::resourceNotFound(asyncResp->res, "Port", portId);
        return;
    }

    nvidia_port_health_utils::clearEarlyHealthIndication(asyncResp, portPath);
}

// ---------------------------------------------------------------------------
// GPU ports: /redfish/v1/Systems/<system>/Processors/<gpu>/Ports/<port>
// ---------------------------------------------------------------------------

inline void afterGetProcessorSubtreeForClearEarlyHealthIndication(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& processorId, const std::string& portId,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error: {} while getting processors",
                         ec);
        messages::internalError(asyncResp->res);
        return;
    }

    for (const auto& [path, serviceMap] : subtree)
    {
        // Exact last-segment match: GPU_1 must not select .../HGX_GPU_1.
        if (sdbusplus::message::object_path(path).filename() != processorId)
        {
            continue;
        }
        dbus::utility::getProperty<std::vector<std::string>>(
            "xyz.openbmc_project.ObjectMapper", path + "/all_states",
            "xyz.openbmc_project.Association", "endpoints",
            std::bind_front(afterGetPortsForClearEarlyHealthIndication,
                            asyncResp, portId));
        return;
    }

    messages::resourceNotFound(asyncResp->res, "#Processor.v1_20_0.Processor",
                               processorId);
}

inline void handleProcessorPortClearEarlyHealthIndication(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemId, const std::string& processorId,
    const std::string& portId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (systemId != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem", systemId);
        return;
    }
    // Parameterless action: reject any member before touching D-Bus.
    if (!nvidia_port_health_utils::readClearEarlyHealthIndicationBody(
            req.body(), asyncResp->res))
    {
        return;
    }

    constexpr std::array<std::string_view, 2> processorIfaces = {
        "xyz.openbmc_project.Inventory.Item.Cpu",
        "xyz.openbmc_project.Inventory.Item.Accelerator"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, processorIfaces,
        std::bind_front(afterGetProcessorSubtreeForClearEarlyHealthIndication,
                        asyncResp, processorId, portId));
}

inline void requestRoutesProcessorPortClearEarlyHealthIndication(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Ports/<str>/Metrics/Actions/Oem/"
                      "NvidiaPortMetrics.ClearEarlyHealthIndication/")
        .privileges(redfish::privileges::postPortMetrics)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleProcessorPortClearEarlyHealthIndication, std::ref(app)));
}

// ---------------------------------------------------------------------------
// NVSwitch ports: /redfish/v1/Fabrics/<fabric>/Switches/<switch>/Ports/<port>
// ---------------------------------------------------------------------------

inline void afterGetSwitchObjectForClearEarlyHealthIndication(
    const std::string& portId,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& /*fabricId*/, const std::string& /*switchId*/,
    const std::string& switchPath,
    const dbus::utility::MapperGetObject& /*object*/)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", switchPath + "/all_states",
        "xyz.openbmc_project.Association", "endpoints",
        std::bind_front(afterGetPortsForClearEarlyHealthIndication, asyncResp,
                        portId));
}

inline void handleSwitchPortClearEarlyHealthIndication(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId,
    const std::string& portId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    // Parameterless action: reject any member before touching D-Bus.
    if (!nvidia_port_health_utils::readClearEarlyHealthIndicationBody(
            req.body(), asyncResp->res))
    {
        return;
    }

    // getSwitchObject answers 404 for an unknown fabric or switch.
    nvidia_fabric_utils::getSwitchObject(
        asyncResp, fabricId, switchId,
        std::bind_front(afterGetSwitchObjectForClearEarlyHealthIndication,
                        portId));
}

inline void requestRoutesSwitchPortClearEarlyHealthIndication(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Switches/<str>/Ports/<str>/"
                      "Metrics/Actions/Oem/"
                      "NvidiaPortMetrics.ClearEarlyHealthIndication/")
        .privileges(redfish::privileges::postPortMetrics)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleSwitchPortClearEarlyHealthIndication, std::ref(app)));
}

} // namespace nvidia
} // namespace redfish
