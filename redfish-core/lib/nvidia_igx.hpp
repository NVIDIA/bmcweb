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

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http_request.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/json_utils.hpp"

#include <systemd/sd-bus.h>

#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/message.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace redfish
{
namespace nvidia_igx
{

inline uint64_t timeoutUs(std::chrono::seconds timeout)
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(timeout).count());
}

inline std::string_view dbusErrorName(sdbusplus::message_t& msg)
{
    const sd_bus_error* dbusError = msg.get_error();
    if (dbusError == nullptr || dbusError->name == nullptr)
    {
        return "";
    }
    return dbusError->name;
}

// ---------------------------------------------------------------------------
// Serial console mux — thin relay to com.nvidia.ConsoleMux
// ---------------------------------------------------------------------------
//
// GET  Managers/<id>/Oem/Nvidia/SerialConsole -> { SerialConsoleSource }
// POST Managers/<id>/Actions/Oem/NvidiaManager.SwitchSerialConsole
//      { "Source": "HostProcessor" | "SafetyController" }

constexpr const char* consoleMuxService = "com.nvidia.ConsoleMux";
constexpr const char* consoleMuxObject = "/com/nvidia/ConsoleMux";
constexpr const char* consoleMuxIface = "com.nvidia.ConsoleMux";

// Redfish names the endpoints by role (NvidiaSerialConsole schema), the
// daemon by chip.
inline std::optional<std::string_view> serialConsoleSourceFromDbus(
    std::string_view source)
{
    if (source == "Tegra")
    {
        return "HostProcessor";
    }
    if (source == "SMCU")
    {
        return "SafetyController";
    }
    return std::nullopt;
}

inline std::optional<std::string_view> serialConsoleSourceToDbus(
    std::string_view source)
{
    if (source == "HostProcessor")
    {
        return "Tegra";
    }
    if (source == "SafetyController")
    {
        return "SMCU";
    }
    return std::nullopt;
}

inline void advertiseSerialConsole(nlohmann::json& oemNvidia,
                                   std::string_view managerId)
{
    oemNvidia["@odata.type"] = "#NvidiaManager.v1_12_0.NvidiaManager";
    oemNvidia["SerialConsole"]["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/SerialConsole", managerId);
}

inline void advertiseSwitchSerialConsole(nlohmann::json& oemActions,
                                         std::string_view managerId)
{
    nlohmann::json& action = oemActions["#NvidiaManager.SwitchSerialConsole"];
    action["target"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Actions/Oem/NvidiaManager.SwitchSerialConsole",
        managerId);
    action["Source@Redfish.AllowableValues"] =
        nlohmann::json::array_t{"HostProcessor", "SafetyController"};
}

inline void afterSerialConsoleGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, const std::string& source)
{
    nlohmann::json& j = asyncResp->res.jsonValue;
    if (ec)
    {
        // sd-bus maps ServiceUnknown to EHOSTUNREACH and NameHasNoOwner to
        // ENXIO. With the daemon not running the routing is simply unknown.
        if (ec.value() == boost::system::errc::host_unreachable ||
            ec.value() == boost::system::errc::no_such_device_or_address)
        {
            BMCWEB_LOG_DEBUG("ConsoleMux absent: {}", ec.message());
            j["SerialConsoleSource"] = nullptr;
            return;
        }
        BMCWEB_LOG_ERROR("ConsoleMux Source read failed: {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }
    // The daemon reports "" while the mux position is indeterminate (a
    // switch in progress, or the muxer not yet run this boot).
    std::optional<std::string_view> redfishSource =
        serialConsoleSourceFromDbus(source);
    if (!redfishSource)
    {
        if (!source.empty())
        {
            BMCWEB_LOG_WARNING("ConsoleMux reported unknown Source '{}'",
                               source);
        }
        j["SerialConsoleSource"] = nullptr;
        return;
    }
    j["SerialConsoleSource"] = *redfishSource;
}

/**
 * Lightweight GET for just the serial-console source. The WebUI polls this
 * instead of the full Manager resource, so a status refresh costs a single
 * D-Bus read rather than rebuilding the entire Manager object.
 */
inline void handleSerialConsoleGet(
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
    nlohmann::json& j = asyncResp->res.jsonValue;
    j["@odata.type"] = "#NvidiaSerialConsole.v1_0_0.NvidiaSerialConsole";
    j["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/SerialConsole", managerId);
    j["Id"] = "SerialConsole";
    j["Name"] = "Serial Console";
    dbus::utility::getProperty<std::string>(
        consoleMuxService, consoleMuxObject, consoleMuxIface, "Source",
        std::bind_front(afterSerialConsoleGet, asyncResp));
}

inline void serialConsoleSwitchError(crow::Response& res,
                                     std::string_view dbusError)
{
    if (dbusError == "com.nvidia.ConsoleMux.Error.Busy")
    {
        // UART busy (flash/switch in progress): retry later.
        messages::serviceTemporarilyUnavailable(res, "5");
    }
    else if (dbusError == "org.freedesktop.DBus.Error.ServiceUnknown" ||
             dbusError == "org.freedesktop.DBus.Error.NameHasNoOwner")
    {
        messages::actionNotSupported(res, "SwitchSerialConsole");
    }
    else if (dbusError == "org.freedesktop.DBus.Error.NoReply" ||
             dbusError == "org.freedesktop.DBus.Error.Timeout")
    {
        messages::serviceTemporarilyUnavailable(res, "10");
    }
    else
    {
        messages::internalError(res);
    }
}

inline void afterSwitchSerialConsole(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& source, const boost::system::error_code& ec,
    sdbusplus::message_t& msg)
{
    if (!ec)
    {
        messages::success(asyncResp->res);
        return;
    }
    std::string_view name = dbusErrorName(msg);
    BMCWEB_LOG_ERROR("ConsoleMux.Switch({}) failed: {} [{}]", source,
                     ec.message(), name);
    serialConsoleSwitchError(asyncResp->res, name);
}

/**
 * Routes the shared serial console to "HostProcessor" or "SafetyController".
 * Delegates to com.nvidia.ConsoleMux.Switch, which wraps the smcu-ctrl.sh
 * helper (GPIO + tcu-muxer).
 */
inline void handleSwitchSerialConsolePost(
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

    std::string requested;
    if (!json_util::readJsonAction(req, asyncResp->res, "Source", requested))
    {
        return;
    }
    std::optional<std::string_view> dbusSource =
        serialConsoleSourceToDbus(requested);
    if (!dbusSource)
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, requested, "Source", "SwitchSerialConsole");
        return;
    }
    std::string source(*dbusSource);

    BMCWEB_LOG_INFO("SwitchSerialConsole requested: {}", requested);
    using namespace std::chrono_literals;
    // Switching can be slow (tcu-muxer stop + settle). Use a timeout above
    // the daemon's worst-case operation time so a slow-but-successful switch
    // isn't cut off at the ~25s D-Bus default.
    dbus::utility::async_method_call_timed(
        [asyncResp, source](const boost::system::error_code& ec,
                            sdbusplus::message_t& msg) {
            afterSwitchSerialConsole(asyncResp, source, ec, msg);
        },
        consoleMuxService, consoleMuxObject, consoleMuxIface, "Switch",
        timeoutUs(75s), source);
}

} // namespace nvidia_igx

inline void requestRoutesNvidiaIgxSerialConsole(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/Oem/Nvidia/SerialConsole/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(nvidia_igx::handleSerialConsoleGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Actions/Oem/NvidiaManager.SwitchSerialConsole/")
        .privileges(redfish::privileges::postManager)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            nvidia_igx::handleSwitchSerialConsolePost, std::ref(app)));
}

} // namespace redfish
