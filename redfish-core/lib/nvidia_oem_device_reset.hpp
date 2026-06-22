/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
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

#include "app.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/action_info.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_async_call_utils.hpp"

#include <boost/container/flat_map.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <string>

namespace redfish
{

// D-Bus interface names
static constexpr std::string_view resetAsyncIface =
    "xyz.openbmc_project.Control.ResetAsync";

// Association endpoint suffix on a chassis that supports device reset.
// ObjectMapper exposes <chassisDbusPath>/reset_controls -> reset object paths.
static constexpr std::string_view resetControlsAssoc = "/reset_controls";

// Populate the ResetActionInfo AllowableValues from the chassis reset objects.
// nsmd names each reset object's D-Bus path leaf after its Redfish ResetType
// (FullReset, ForceDpuReset, DpuReset, ArmReset, ArmShutdown), so the path leaf
// of each reset_controls endpoint is surfaced verbatim as an allowable value.
// This keeps bmcweb device-agnostic: the supported set is whatever nsmd
// publishes, with no hard-coded reset-type table.
inline void populateChassisResetAllowableValues(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisDbusPath)
{
    dbus::utility::getAssociationEndPoints(
        chassisDbusPath + std::string(resetControlsAssoc),
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperEndPoints& resetPaths) {
            if (ec || resetPaths.empty())
            {
                BMCWEB_LOG_WARNING("reset_controls association not found: {}",
                                   ec.message());
                return;
            }
            // Static Redfish ResetType -> description text, mirroring the
            // NvidiaChassis OEM schema / mockup. The allowable *set* stays
            // device-agnostic (driven by the reset_controls leaves); only the
            // human description per known type is looked up here. An unknown
            // leaf yields an empty string so AllowableValueDescriptions stays
            // index-aligned with AllowableValues.
            static const boost::container::flat_map<std::string_view,
                                                    std::string_view>
                resetTypeDescriptions{
                    {"FullReset", "Card Level Orchestrated Reset.  This takes "
                                  "effect after the next PCIe reset."},
                    {"ForceDpuReset", "DPU Level Immediate Reset"},
                    {"DpuReset", "DPU Level Orchestrated Reset"},
                    {"ArmReset", "ARM Only Reset"},
                    {"ArmShutdown", "ARM Shutdown"}};

            nlohmann::json::array_t allowableValues;
            nlohmann::json::array_t allowableValueDescriptions;
            for (const std::string& resetPath : resetPaths)
            {
                std::string resetType =
                    sdbusplus::object_path(resetPath).filename();
                if (resetType.empty())
                {
                    continue;
                }
                auto descIt = resetTypeDescriptions.find(resetType);
                allowableValueDescriptions.emplace_back(
                    descIt != resetTypeDescriptions.end()
                        ? std::string(descIt->second)
                        : std::string());
                allowableValues.emplace_back(std::move(resetType));
            }
            asyncResp->res.jsonValue["Parameters"] = nlohmann::json::array(
                {{{"Name", "ResetType"},
                  {"Required", true},
                  {"DataType", "String"},
                  {"AllowableValues", std::move(allowableValues)},
                  {"AllowableValueDescriptions",
                   std::move(allowableValueDescriptions)}}});
        });
}

// Invoke Control.ResetAsync.Reset() on the matched chassis reset object.
inline void doChassisResetOnObject(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const std::string& svc,
    const std::string& resetPath)
{
    nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<int>(
        asyncResp, std::chrono::seconds(60), svc, resetPath,
        std::string(resetAsyncIface), "Reset",
        [asyncResp](const std::string& status, [[maybe_unused]] const int*) {
            if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
            {
                messages::success(asyncResp->res);
                return;
            }
            messages::internalError(asyncResp->res);
        });
}

// Resolved the matched reset object's service; invoke its Reset() method.
inline void afterGetObjForChassisReset(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& resetPath, const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& obj)
{
    if (ec || obj.empty())
    {
        messages::internalError(asyncResp->res);
        return;
    }
    doChassisResetOnObject(asyncResp, obj.begin()->first, resetPath);
}

// Match the requested ResetType against the reset_controls endpoints by path
// leaf — nsmd names each reset object after its Redfish ResetType — then drive
// the matched object. No matching leaf means the ResetType is unsupported.
inline void afterResetControlsForChassisReset(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& resetType, const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& resetPaths)
{
    if (ec || resetPaths.empty())
    {
        messages::resourceNotFound(asyncResp->res, "Action",
                                   "NvidiaChassis.Reset");
        return;
    }
    for (const std::string& resetPath : resetPaths)
    {
        if (sdbusplus::object_path(resetPath).filename() == resetType)
        {
            dbus::utility::getDbusObject(
                resetPath, std::array<std::string_view, 1>{resetAsyncIface},
                std::bind_front(afterGetObjForChassisReset, asyncResp,
                                resetPath));
            return;
        }
    }
    messages::actionParameterNotSupported(asyncResp->res, "ResetType",
                                          "NvidiaChassis.Reset");
}

inline void afterValidChassisForChassisReset(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& resetType, const std::optional<std::string>& path)
{
    if (!path)
    {
        messages::resourceNotFound(asyncResp->res, "Action",
                                   "NvidiaChassis.Reset");
        return;
    }
    dbus::utility::getAssociationEndPoints(
        *path + std::string(resetControlsAssoc),
        std::bind_front(afterResetControlsForChassisReset, asyncResp,
                        resetType));
}

inline void handleChassisResetPost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::string resetType;
    if (!json_util::readJsonAction(req, asyncResp->res, "ResetType", resetType))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(afterValidChassisForChassisReset, asyncResp,
                        resetType));
}

inline void handleNvidiaChassisResetActionInfoGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/Oem/Nvidia/ResetActionInfo", chassisId);
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_4_2.ActionInfo";
    asyncResp->res.jsonValue["Id"] = "ResetActionInfo";
    asyncResp->res.jsonValue["Name"] = "Reset Action Info";
    asyncResp->res.jsonValue["Parameters"] = nlohmann::json::array();

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        [asyncResp, chassisId](const std::optional<std::string>& path) {
            if (!path)
            {
                return;
            }
            populateChassisResetAllowableValues(asyncResp, *path);
        });
}

// Write the NvidiaChassis.Reset OEM action onto the chassis GET response once
// the chassis is confirmed to expose reset_controls endpoints.
inline void addChassisResetActionToResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& resetPaths)
{
    if (ec || resetPaths.empty())
    {
        return;
    }
    asyncResp->res.jsonValue["Actions"]["Oem"]["#NvidiaChassis.Reset"]
                            ["target"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/Actions/Oem/NvidiaChassis.Reset", chassisId);
    asyncResp->res.jsonValue["Actions"]["Oem"]["#NvidiaChassis.Reset"]
                            ["@Redfish.ActionInfo"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/Oem/Nvidia/ResetActionInfo", chassisId);
}

// Once the chassis path is resolved, look up its reset_controls association.
inline void addChassisResetOemActionForPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::optional<std::string>& chassisPath)
{
    if (!chassisPath)
    {
        return;
    }
    dbus::utility::getAssociationEndPoints(
        *chassisPath + std::string(resetControlsAssoc),
        std::bind_front(addChassisResetActionToResponse, asyncResp, chassisId));
}

// Add the NvidiaChassis.Reset action to the chassis GET response if the
// chassis exposes reset_controls association endpoints.
inline void addChassisResetOemAction(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(addChassisResetOemActionForPath, asyncResp, chassisId));
}

inline void requestRoutesDeviceReset(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Chassis/<str>/Actions/Oem/NvidiaChassis.Reset/")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleChassisResetPost, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/ResetActionInfo/")
        .privileges(redfish::privileges::getChassis)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleNvidiaChassisResetActionInfoGet, std::ref(app)));
}

// Add the NvidiaPort.ResetTransceiver OEM action onto a port GET response when
// the port actually supports it. Resolution mirrors the POST handler
// (handlePortResetTransceiverPost): walk the network adapter's /all_states
// association to the Inventory.Item.Port objects, match the requested port, and
// advertise only if that object carries Control.ResetAsync. Resolving the same
// way as the POST guarantees the action is advertised iff it is invokable —
// the port's own inventory object (as returned by getPortData) does not carry
// the reset interface, which is why advertising off that path silently
// dropped the action. Called from the Port resource handler.
inline void addPortResetTransceiverOemAction(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& networkAdapterPath, const std::string& chassisId,
    const std::string& networkAdapterId, const std::string& portId)
{
    dbus::utility::getAssociatedSubTreePaths(
        networkAdapterPath + "/all_states",
        sdbusplus::object_path("/xyz/openbmc_project/inventory"), 0,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Inventory.Item.Port"},
        [asyncResp, chassisId, networkAdapterId, portId](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreePathsResponse& portPaths) {
            if (ec)
            {
                return;
            }
            for (const std::string& portPath : portPaths)
            {
                if (sdbusplus::object_path(portPath).filename() != portId)
                {
                    continue;
                }
                dbus::utility::getDbusObject(
                    portPath, std::array<std::string_view, 1>{resetAsyncIface},
                    [asyncResp, chassisId, networkAdapterId,
                     portId](const boost::system::error_code& ec2,
                             const dbus::utility::MapperGetObject& obj) {
                        if (ec2 || obj.empty())
                        {
                            return;
                        }
                        asyncResp->res.jsonValue["Actions"]["Oem"]
                                                ["#NvidiaPort.ResetTransceiver"]
                                                ["target"] =
                            boost::urls::format(
                                "/redfish/v1/Chassis/{}/NetworkAdapters/{}"
                                "/Ports/{}/Actions/Oem/"
                                "NvidiaPort.ResetTransceiver",
                                chassisId, networkAdapterId, portId);
                    });
                return;
            }
        });
}

// Final step: the port object was resolved via the association flow and is
// known to exist. Invoke its Control.ResetAsync.Reset(). A missing reset
// interface means the action is not supported on this port (the port exists,
// so this is not a "port not found").
inline void doTransceiverResetOnPort(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& portObjPath, const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& obj)
{
    if (ec || obj.empty())
    {
        messages::resourceNotFound(asyncResp->res, "Action",
                                   "NvidiaPort.ResetTransceiver");
        return;
    }
    const std::string& svc = obj.begin()->first;
    nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<int>(
        asyncResp, std::chrono::seconds(60), svc, portObjPath,
        std::string(resetAsyncIface), "Reset",
        [asyncResp](const std::string& status, [[maybe_unused]] const int*) {
            if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
            {
                messages::success(asyncResp->res);
                return;
            }
            messages::internalError(asyncResp->res);
        });
}

// Match the requested port among the network adapter's ports; 404 if the port
// does not exist under the adapter.
inline void resolvePortForTransceiverReset(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& portId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& portPaths)
{
    if (!ec)
    {
        for (const std::string& portPath : portPaths)
        {
            if (sdbusplus::object_path(portPath).filename() == portId)
            {
                dbus::utility::getDbusObject(
                    portPath, std::array<std::string_view, 1>{resetAsyncIface},
                    std::bind_front(doTransceiverResetOnPort, asyncResp,
                                    portPath));
                return;
            }
        }
    }
    messages::resourceNotFound(asyncResp->res, "Port", portId);
}

// Match the requested network adapter under the chassis, then enumerate its
// ports; 404 if the adapter does not exist under the chassis.
inline void resolveAdapterForTransceiverReset(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& networkAdapterId, const std::string& portId,
    const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& adapterPaths)
{
    if (!ec)
    {
        for (const std::string& adapterPath : adapterPaths)
        {
            if (sdbusplus::object_path(adapterPath).filename() ==
                networkAdapterId)
            {
                dbus::utility::getAssociatedSubTreePaths(
                    adapterPath + "/all_states",
                    sdbusplus::object_path("/xyz/openbmc_project/inventory"), 0,
                    std::array<std::string_view, 1>{
                        "xyz.openbmc_project.Inventory.Item.Port"},
                    std::bind_front(resolvePortForTransceiverReset, asyncResp,
                                    portId));
                return;
            }
        }
    }
    messages::resourceNotFound(asyncResp->res, "NetworkAdapter",
                               networkAdapterId);
}

// Resolve the chassis, then walk chassis -> network adapter -> port through
// D-Bus associations (rather than assuming the inventory object path) before
// issuing the transceiver reset.
inline void afterValidChassisForTransceiverReset(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
    const std::string& portId, const std::optional<std::string>& chassisPath)
{
    if (!chassisPath)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }
    dbus::utility::getAssociationEndPoints(
        *chassisPath + "/network_adapters",
        std::bind_front(resolveAdapterForTransceiverReset, asyncResp,
                        networkAdapterId, portId));
}

inline void handlePortResetTransceiverPost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& networkAdapterId,
    const std::string& portId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(afterValidChassisForTransceiverReset, asyncResp,
                        chassisId, networkAdapterId, portId));
}

inline void requestRoutesPortResetTransceiver(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>/Ports/<str>"
                 "/Actions/Oem/NvidiaPort.ResetTransceiver/")
        .privileges(redfish::privileges::postNetworkAdapter)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handlePortResetTransceiverPost, std::ref(app)));
}

} // namespace redfish
