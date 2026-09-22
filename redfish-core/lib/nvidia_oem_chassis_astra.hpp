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
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/action_info.hpp"
#include "generated/enums/nvidia_astra.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "logging.hpp"
#include "nvidia_messages.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "task.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_astra_utils.hpp"
#include "utils/nvidia_async_operation_task.hpp"
#include "utils/nvidia_log_entry_utils.hpp"

#include <asm-generic/errno.h>
#include <systemd/sd-bus.h>

#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/message/native_types.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace redfish
{
namespace nvidia_oem_chassis_astra
{

constexpr std::string_view stateEnabled = "com.nvidia.Astra.State.Enabled";
constexpr std::string_view stateDisabled = "com.nvidia.Astra.State.Disabled";
constexpr std::string_view stateError = "com.nvidia.Astra.State.Error";
constexpr std::string_view stateUnknown = "com.nvidia.Astra.State.Unknown";
constexpr auto taskTimeout = std::chrono::seconds(300);
constexpr auto astraLogNamespace = "Astra";

/** @brief What one of State or PendingState says on its own. */
enum class DeviceState
{
    Enabled,
    Disabled,
    Error,
};

inline std::optional<DeviceState> deviceStateFromDbus(
    std::string_view dbusState)
{
    if (dbusState == stateEnabled)
    {
        return DeviceState::Enabled;
    }
    if (dbusState == stateDisabled)
    {
        return DeviceState::Disabled;
    }
    // The schema has no unknown value; its Error covers an offline adapter.
    if (dbusState == stateError || dbusState == stateUnknown)
    {
        return DeviceState::Error;
    }
    return std::nullopt;
}

/**
 * @brief Fold the active and pending states into the single reported state.
 *
 * The schema reports one value where D-Bus carries two, so a request that has
 * been accepted but still needs a power cycle shows up as the two disagreeing.
 * Either one being Error makes the pair unusable.
 */
inline nvidia_astra::AstraState foldAstraState(DeviceState active,
                                               DeviceState pending)
{
    using nvidia_astra::AstraState;
    if (active == DeviceState::Error || pending == DeviceState::Error)
    {
        return AstraState::Error;
    }
    if (active == pending)
    {
        return active == DeviceState::Enabled ? AstraState::Enabled
                                              : AstraState::Disabled;
    }
    return pending == DeviceState::Enabled ? AstraState::PendingEnable
                                           : AstraState::PendingDisable;
}

inline std::optional<nlohmann::json> resolveAstraMessage(
    const std::string& msgId, const std::vector<std::string>& args)
{
    auto arg = [&args](size_t i) -> std::string_view {
        return i < args.size() ? std::string_view(args[i]) : std::string_view{};
    };

    if (msgId == "Base.1.19.PropertyValueModified")
    {
        // The value arrives as JSON text (nsmd logs true or false); parse it so
        // a boolean reaches MessageArgs as true, not the quoted string "true".
        nlohmann::json value = nlohmann::json::parse(arg(1), nullptr, false);
        if (value.is_discarded())
        {
            value = std::string(arg(1));
        }
        return messages::propertyValueModified(arg(0), value);
    }
    if (msgId == "NvidiaResourceEvent.1.0.DeviceDriverErrorsDetected")
    {
        return messages::deviceDriverErrorsDetected(arg(0), arg(1), arg(2));
    }
    return std::nullopt;
}

/** @brief The task message for one of an Astra operation's log entries. */
inline std::optional<nlohmann::json> formatTaskLogMessage(
    const nvidia_log_entry::LogEntryProperties& entry)
{
    auto message = resolveAstraMessage(entry.messageId, entry.messageArgs);
    if (!message)
    {
        return std::nullopt;
    }
    message->update(nlohmann::json(entry));
    return message;
}

inline void mapSetAstraModeError(crow::Response& res,
                                 const boost::system::error_code& ec,
                                 std::string_view errorName)
{
    if (ec.value() == EBADR || ec == boost::system::errc::host_unreachable)
    {
        // The object went away after the subtree lookup found it.
        messages::resourceNotFound(res, "Action", "NvidiaAstra.SetAstraMode");
        return;
    }
    if (errorName == "xyz.openbmc_project.Common.Error.Unavailable")
    {
        // A second operation while one runs conflicts with the resource's
        // current state (DSP0266 409); resourceInUse alone answers 503.
        messages::resourceInUse(res);
        res.result(boost::beast::http::status::conflict);
        return;
    }
    messages::internalError(res);
}

inline void handleSetAstraModeResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<task::Payload>& payload,
    const std::shared_ptr<nvidia_async_operation_utils::OperationTask>&
        operationTask,
    const std::string& service, const boost::system::error_code& ec,
    sdbusplus::message_t& msg,
    const sdbusplus::message::object_path& operationPath)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Astra SetAstraMode failed: {}", ec.message());
        const sd_bus_error* dbusError = msg.get_error();
        mapSetAstraModeError(asyncResp->res, ec,
                             dbusError != nullptr && dbusError->name != nullptr
                                 ? std::string_view(dbusError->name)
                                 : std::string_view{});
        return;
    }

    operationTask->start(asyncResp, std::move(*payload), service,
                         operationPath.str, formatTaskLogMessage, taskTimeout);
}

inline void afterAdapterUrlFound(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& adapterPath, bool found, const std::string& url)
{
    if (!found)
    {
        BMCWEB_LOG_WARNING("No Redfish URI for network adapter {}",
                           adapterPath);
        return;
    }

    nlohmann::json& links =
        asyncResp->res.jsonValue["Links"]["NetworkAdapters"];
    links.push_back({{"@odata.id", url}});
    asyncResp->res.jsonValue["Links"]["NetworkAdapters@odata.count"] =
        links.size();
}

inline void afterNetworkAdaptersFound(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& adapterPaths)
{
    if (ec)
    {
        // The mapper hosts no association until it has an endpoint.
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("Unable to read network_adapters association: {}",
                             ec.message());
            messages::internalError(asyncResp->res);
        }
        return;
    }

    for (const std::string& adapterPath : adapterPaths)
    {
        chassis_utils::getRedfishURL(
            adapterPath,
            std::bind_front(afterAdapterUrlFound, asyncResp, adapterPath));
    }
}

/**
 * @brief A fabric's Redfish id is its inventory path leaf, as the Fabric
 *        collection also derives it.
 */
inline std::string fabricUri(const std::string& fabricPath)
{
    return boost::urls::format(
               "/redfish/v1/Fabrics/{}",
               sdbusplus::message::object_path(fabricPath).filename())
        .buffer();
}

inline void afterPCIeTopologiesFound(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& fabricPaths)
{
    if (ec)
    {
        // The mapper hosts no association until it has an endpoint.
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("Unable to read pcie_topologies association: {}",
                             ec.message());
            messages::internalError(asyncResp->res);
        }
        return;
    }

    nlohmann::json& links = asyncResp->res.jsonValue["Links"]["PCIeTopologies"];
    for (const std::string& fabricPath : fabricPaths)
    {
        links.push_back({{"@odata.id", fabricUri(fabricPath)}});
    }
    asyncResp->res.jsonValue["Links"]["PCIeTopologies@odata.count"] =
        links.size();
}

inline void afterAstraStateFound(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec.value() == EBADR || ec == boost::system::errc::host_unreachable)
    {
        // The object went away after the subtree lookup found it.
        messages::resourceNotFound(asyncResp->res, "NvidiaAstra", "Astra");
        return;
    }
    if (ec)
    {
        BMCWEB_LOG_ERROR("Unable to read com.nvidia.Astra: {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }

    const std::string* state = nullptr;
    const std::string* pendingState = nullptr;
    if (!sdbusplus::unpackPropertiesNoThrow(dbus_utils::UnpackErrorPrinter(),
                                            properties, "State", state,
                                            "PendingState", pendingState) ||
        state == nullptr || pendingState == nullptr)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    std::optional<DeviceState> active = deviceStateFromDbus(*state);
    std::optional<DeviceState> pending = deviceStateFromDbus(*pendingState);
    if (!active || !pending)
    {
        BMCWEB_LOG_ERROR("Unknown Astra state {} or pending state {}", *state,
                         *pendingState);
        messages::internalError(asyncResp->res);
        return;
    }
    asyncResp->res.jsonValue["AstraState"] = foldAstraState(*active, *pending);
}

inline void populateResource(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const AstraResource& resource)
{
    nlohmann::json& body = asyncResp->res.jsonValue;
    body["@odata.type"] = "#NvidiaAstra.v1_0_0.NvidiaAstra";
    body["@odata.id"] = resource.uri();
    body["Id"] = "Astra";
    body["Name"] = resource.chassisId + " Oem Nvidia Astra";
    body["Links"]["NetworkAdapters"] = nlohmann::json::array();
    body["Links"]["NetworkAdapters@odata.count"] = 0;
    body["Links"]["PCIeTopologies"] = nlohmann::json::array();
    body["Links"]["PCIeTopologies@odata.count"] = 0;

    nlohmann::json& action = body["Actions"]["#NvidiaAstra.SetAstraMode"];
    action["target"] = resource.actionUri();
    action["@Redfish.ActionInfo"] = resource.actionInfoUri();

    resource.getState(std::bind_front(afterAstraStateFound, asyncResp));
    resource.getNetworkAdapters(
        std::bind_front(afterNetworkAdaptersFound, asyncResp));
    resource.getPCIeTopologies(
        std::bind_front(afterPCIeTopologiesFound, asyncResp));
}

inline void afterResourceFoundForGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::optional<AstraResource>& resource)
{
    if (!resource)
    {
        messages::resourceNotFound(asyncResp->res, "NvidiaAstra", "Astra");
        return;
    }
    populateResource(asyncResp, *resource);
}

inline void handleResourceGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    AstraResource::find(asyncResp, chassisId,
                        std::bind_front(afterResourceFoundForGet, asyncResp));
}

inline void populateActionInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const AstraResource& resource)
{
    nlohmann::json& body = asyncResp->res.jsonValue;
    body["@odata.type"] = "#ActionInfo.v1_2_0.ActionInfo";
    body["@odata.id"] = resource.actionInfoUri();
    body["Id"] = "SetAstraModeActionInfo";
    body["Name"] = "SetAstraMode Action Info";
    body["Description"] =
        "The action parameters for setting the Advanced Secure Trusted "
        "Resource Architecture (ASTRA) mode on all East/West ConnectX NICs in "
        "this chassis.";

    nlohmann::json::array_t allowable;
    allowable.emplace_back(nvidia_astra::AstraMode::Enabled);
    allowable.emplace_back(nvidia_astra::AstraMode::Disabled);

    nlohmann::json::object_t parameter;
    parameter["Name"] = "AstraMode";
    parameter["Required"] = true;
    parameter["DataType"] = action_info::ParameterTypes::String;
    parameter["AllowableValues"] = std::move(allowable);

    nlohmann::json::array_t parameters;
    parameters.emplace_back(std::move(parameter));
    body["Parameters"] = std::move(parameters);
}

inline void afterResourceFoundForActionInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::optional<AstraResource>& resource)
{
    if (!resource)
    {
        messages::resourceNotFound(asyncResp->res, "ActionInfo",
                                   "SetAstraModeActionInfo");
        return;
    }
    populateActionInfo(asyncResp, *resource);
}

inline void handleActionInfoGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    AstraResource::find(
        asyncResp, chassisId,
        std::bind_front(afterResourceFoundForActionInfo, asyncResp));
}

/**
 * @brief Reads the AstraMode parameter, rejecting anything the action's
 *        ActionInfo does not allow.
 */
inline bool readSetAstraModeAction(const crow::Request& req,
                                   crow::Response& response,
                                   nvidia_astra::AstraMode& mode)
{
    std::string requested;
    if (!redfish::json_util::readJsonAction(req, response, "AstraMode",
                                            requested))
    {
        return false;
    }

    mode = nlohmann::json(requested).get<nvidia_astra::AstraMode>();
    if (mode == nvidia_astra::AstraMode::Invalid)
    {
        messages::actionParameterValueNotInList(
            response, requested, "AstraMode", "NvidiaAstra.SetAstraMode");
        return false;
    }
    return true;
}

inline void afterResourceFoundForPost(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<task::Payload>& payload, nvidia_astra::AstraMode mode,
    const std::optional<AstraResource>& resource)
{
    if (!resource)
    {
        messages::resourceNotFound(asyncResp->res, "Action",
                                   "NvidiaAstra.SetAstraMode");
        return;
    }

    resource->setAstraMode(
        mode,
        std::bind_front(handleSetAstraModeResponse, asyncResp, payload,
                        nvidia_async_operation_utils::OperationTask::watch(
                            astraLogNamespace),
                        resource->service));
}

inline void handleSetAstraModePost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    nvidia_astra::AstraMode mode = nvidia_astra::AstraMode::Invalid;
    if (!readSetAstraModeAction(req, asyncResp->res, mode))
    {
        return;
    }
    AstraResource::find(
        asyncResp, chassisId,
        std::bind_front(afterResourceFoundForPost, asyncResp,
                        std::make_shared<task::Payload>(req), mode));
}

} // namespace nvidia_oem_chassis_astra

inline void requestRoutesNvidiaAstra(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/Astra/")
        .privileges(redfish::privileges::getChassis)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            nvidia_oem_chassis_astra::handleResourceGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Oem/Nvidia/Astra/SetAstraModeActionInfo/")
        .privileges(redfish::privileges::getChassis)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            nvidia_oem_chassis_astra::handleActionInfoGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/Astra/Actions/"
                      "NvidiaAstra.SetAstraMode/")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            nvidia_oem_chassis_astra::handleSetAstraModePost, std::ref(app)));
}

} // namespace redfish
