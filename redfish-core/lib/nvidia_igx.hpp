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
#include <exception>
#include <format>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

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

// ---------------------------------------------------------------------------
// Host recovery (Tegra RCM) — thin relay to com.nvidia.HostRecovery
// ---------------------------------------------------------------------------
//
// GET  Systems/<id>/Oem/Nvidia/HostRecovery
// POST Systems/<id>/Actions/Oem/NvidiaComputerSystem.EnterHostRecovery
// POST Systems/<id>/Actions/Oem/NvidiaComputerSystem.CheckHostRecoveryStatus
// POST Systems/<id>/Actions/Oem/NvidiaComputerSystem.ExitHostRecovery
// POST Systems/<id>/Actions/Oem/NvidiaComputerSystem.
//          AbandonUnverifiedRecoverySession
//
// The daemon owns all correlated state; bmcweb stays a thin relay. The
// destructive actions require ConfigureComponents (postComputerSystem).

constexpr const char* hostRecoveryService = "com.nvidia.HostRecovery";
constexpr const char* hostRecoveryObject = "/com/nvidia/HostRecovery";
constexpr const char* hostRecoveryIface = "com.nvidia.HostRecovery";

inline void advertiseHostRecovery(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::string_view systemId)
{
    nlohmann::json& oemNvidia = asyncResp->res.jsonValue["Oem"]["Nvidia"];
    oemNvidia["@odata.type"] =
        "#NvidiaComputerSystem.v1_11_0.NvidiaComputerSystem";
    oemNvidia["HostRecovery"]["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/HostRecovery", systemId);

    nlohmann::json& oemActions = asyncResp->res.jsonValue["Actions"]["Oem"];
    for (std::string_view action :
         {"EnterHostRecovery", "CheckHostRecoveryStatus", "ExitHostRecovery",
          "AbandonUnverifiedRecoverySession"})
    {
        oemActions[std::format("#NvidiaComputerSystem.{}", action)]["target"] =
            boost::urls::format(
                "/redfish/v1/Systems/{}/Actions/Oem/NvidiaComputerSystem.{}",
                systemId, action);
    }
}

inline void hostRecoveryError(crow::Response& res, std::string_view name,
                              std::string_view action)
{
    if (name == "com.nvidia.HostRecovery.Error.Busy")
    {
        messages::serviceTemporarilyUnavailable(res, "5");
    }
    else if (name == "com.nvidia.HostRecovery.Error.AttemptLimit")
    {
        messages::serviceTemporarilyUnavailable(res, "900");
    }
    else if (name == "com.nvidia.HostRecovery.Error.NotAcknowledged")
    {
        messages::actionParameterValueNotInList(
            res, "false", "AcknowledgeUsbDisruption", action);
    }
    else if (name == "com.nvidia.HostRecovery.Error.PoweredOff" ||
             name == "com.nvidia.HostRecovery.Error.NotSupported" ||
             name == "org.freedesktop.DBus.Error.ServiceUnknown" ||
             name == "org.freedesktop.DBus.Error.NameHasNoOwner")
    {
        messages::actionNotSupported(res, action);
    }
    else if (name == "org.freedesktop.DBus.Error.NoReply" ||
             name == "org.freedesktop.DBus.Error.Timeout")
    {
        messages::serviceTemporarilyUnavailable(res, "10");
    }
    else
    {
        messages::internalError(res);
    }
}

inline void hostRecoveryClassifyError(
    crow::Response& res, const boost::system::error_code& ec,
    sdbusplus::message_t& msg, std::string_view action)
{
    std::string_view name = dbusErrorName(msg);
    BMCWEB_LOG_ERROR("HostRecovery.{} failed: {} [{}]", action, ec.message(),
                     name);
    hostRecoveryError(res, name, action);
}

// The daemon answers a successful call with a JSON snapshot of what actually
// happened (Outcome / Message / RecoveryState / LastCheckStatus / ...).
// Relaying a bare success would make an EnterFailed (strap pulse issued, host
// never came back as APX) render exactly like a confirmed entry, so surface
// the snapshot and let the outcome decide whether the action succeeded.
inline void hostRecoveryRelayOutcome(
    crow::Response& res, const std::string& payload, std::string_view action)
{
    nlohmann::json parsed = nlohmann::json::parse(payload, nullptr, false);
    if (!parsed.is_object())
    {
        BMCWEB_LOG_ERROR("HostRecovery.{}: malformed reply: {}", action,
                         payload);
        messages::internalError(res);
        return;
    }

    // Every field below comes from the daemon's single result builder, so a
    // missing or wrong-typed one is a schema mismatch, not an absent option.
    // Defaulting it would answer "Idle, not in recovery" with HTTP 200 to an
    // operator whose reply could not be read. Collect first, publish only once
    // the whole reply is sound.
    bool valid = true;
    auto stringField = [&parsed, &payload, &valid, action](const char* key) {
        auto it = parsed.find(key);
        if (it == parsed.end() || !it->is_string())
        {
            BMCWEB_LOG_ERROR(
                "HostRecovery.{}: reply field '{}' missing or not a string: {}",
                action, key, payload);
            valid = false;
            return std::string();
        }
        return it->get<std::string>();
    };
    auto boolField = [&parsed, &payload, &valid, action](const char* key) {
        auto it = parsed.find(key);
        if (it == parsed.end() || !it->is_boolean())
        {
            BMCWEB_LOG_ERROR(
                "HostRecovery.{}: reply field '{}' missing or not a boolean: {}",
                action, key, payload);
            valid = false;
            return false;
        }
        return it->get<bool>();
    };

    std::string outcome = stringField("Outcome");
    std::string message = stringField("Message");
    std::string recoveryState = stringField("RecoveryState");
    bool recoveryActive = boolField("RecoveryActive");
    std::string lastCheckStatus = stringField("LastCheckStatus");
    std::string operationState = stringField("OperationState");
    if (!valid)
    {
        messages::internalError(res);
        return;
    }

    // NvidiaHostRecovery.v1_0_0.HostRecoveryActionResponse
    res.jsonValue["Outcome"] = outcome;
    res.jsonValue["Message"] = message;
    res.jsonValue["RecoveryState"] = recoveryState;
    res.jsonValue["RecoveryActive"] = recoveryActive;
    res.jsonValue["LastCheckStatus"] = lastCheckStatus;
    res.jsonValue["OperationState"] = operationState;

    // Only Enter can fail this way. For the other actions the outcome is the
    // resulting RecoveryState: a Check that runs fine on a host left in
    // EnterFailed reports that state back, and is not itself a failure.
    if (action == "EnterHostRecovery" && outcome == "EnterFailed")
    {
        messages::operationFailed(res);
        return;
    }
    messages::success(res);
}

inline void hostRecoveryReportOutcome(
    crow::Response& res, sdbusplus::message_t& msg, std::string_view action)
{
    std::string payload;
    try
    {
        msg.read(payload);
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("HostRecovery.{}: unreadable reply: {}", action,
                         e.what());
        messages::internalError(res);
        return;
    }
    hostRecoveryRelayOutcome(res, payload, action);
}

// Only the NvidiaHostRecovery.v1_0_0 properties. The daemon's correlation
// state (generation, revisions, evidence, timestamps) stays on D-Bus.
inline void hostRecoverySnapshotToJson(
    const dbus::utility::DBusPropertiesMap& props, nlohmann::json& j)
{
    for (const auto& [key, val] : props)
    {
        if (const std::string* s = std::get_if<std::string>(&val))
        {
            if (key == "RecoveryState" || key == "LastCheckStatus" ||
                key == "OperationState")
            {
                j[key] = *s;
            }
        }
        else if (const bool* b = std::get_if<bool>(&val))
        {
            if (key == "RecoveryActive")
            {
                j[key] = *b;
            }
        }
        else if (const uint32_t* u = std::get_if<uint32_t>(&val))
        {
            if (key == "RecoveryAttemptCount")
            {
                j[key] = *u;
            }
        }
    }
}

inline bool checkHostRecoverySystem(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return false;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return false;
    }
    return true;
}

inline void afterHostRecoveryGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& props)
{
    nlohmann::json& j = asyncResp->res.jsonValue;
    if (ec)
    {
        // Only a genuinely absent daemon may be published as "feature not
        // present": sd-bus maps ServiceUnknown to EHOSTUNREACH and
        // NameHasNoOwner to ENXIO. Any other failure is not a confirmed
        // negative; reporting it as one would make the WebUI hide an active
        // recovery session on a transient blip.
        if (ec.value() == boost::system::errc::host_unreachable ||
            ec.value() == boost::system::errc::no_such_device_or_address)
        {
            BMCWEB_LOG_DEBUG("HostRecovery absent: {}", ec.message());
            j["HostRecoveryAvailable"] = false;
            j["RecoveryState"] = "Unknown";
            j["LastCheckStatus"] = "Unknown";
            j["OperationState"] = "Unknown";
            return;
        }
        BMCWEB_LOG_ERROR("HostRecovery snapshot failed: {}", ec.message());
        messages::internalError(asyncResp->res);
        return;
    }
    j["HostRecoveryAvailable"] = true;
    hostRecoverySnapshotToJson(props, j);
}

inline void handleHostRecoveryGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!checkHostRecoverySystem(app, req, asyncResp, systemName))
    {
        return;
    }
    nlohmann::json& j = asyncResp->res.jsonValue;
    j["@odata.type"] = "#NvidiaHostRecovery.v1_0_0.NvidiaHostRecovery";
    j["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/HostRecovery", systemName);
    j["Id"] = "HostRecovery";
    j["Name"] = "Host Recovery";
    dbus::utility::getAllProperties(
        hostRecoveryService, hostRecoveryObject, hostRecoveryIface,
        std::bind_front(afterHostRecoveryGet, asyncResp));
}

inline void handleEnterHostRecoveryPost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!checkHostRecoverySystem(app, req, asyncResp, systemName))
    {
        return;
    }
    bool verifyAfterEnter = true;
    bool acknowledgeUsbDisruption = false;
    if (!json_util::readJsonAction(req, asyncResp->res, "VerifyAfterEnter",
                                   verifyAfterEnter, "AcknowledgeUsbDisruption",
                                   acknowledgeUsbDisruption))
    {
        return;
    }
    if (!acknowledgeUsbDisruption)
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "false", "AcknowledgeUsbDisruption",
            "EnterHostRecovery");
        return;
    }
    using namespace std::chrono_literals;
    // Must exceed the daemon's worst-case Enter time (105s: powerctrl lock
    // 10s + route 5s + strap pulse 75s + APX window 10s + restore 5s), or the
    // operator sees a failure for a recovery the daemon goes on to complete.
    dbus::utility::async_method_call_timed(
        [asyncResp](const boost::system::error_code& ec,
                    sdbusplus::message_t& msg) {
            if (ec)
            {
                hostRecoveryClassifyError(asyncResp->res, ec, msg,
                                          "EnterHostRecovery");
                return;
            }
            hostRecoveryReportOutcome(asyncResp->res, msg, "EnterHostRecovery");
        },
        hostRecoveryService, hostRecoveryObject, hostRecoveryIface, "Enter",
        timeoutUs(150s), verifyAfterEnter, acknowledgeUsbDisruption);
}

inline void handleCheckHostRecoveryStatusPost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!checkHostRecoverySystem(app, req, asyncResp, systemName))
    {
        return;
    }
    bool acknowledgeUsbDisruption = false;
    if (!json_util::readJsonAction(req, asyncResp->res,
                                   "AcknowledgeUsbDisruption",
                                   acknowledgeUsbDisruption))
    {
        return;
    }
    if (!acknowledgeUsbDisruption)
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "false", "AcknowledgeUsbDisruption",
            "CheckHostRecoveryStatus");
        return;
    }
    using namespace std::chrono_literals;
    dbus::utility::async_method_call_timed(
        [asyncResp](const boost::system::error_code& ec,
                    sdbusplus::message_t& msg) {
            if (ec)
            {
                hostRecoveryClassifyError(asyncResp->res, ec, msg,
                                          "CheckHostRecoveryStatus");
                return;
            }
            hostRecoveryReportOutcome(asyncResp->res, msg,
                                      "CheckHostRecoveryStatus");
        },
        hostRecoveryService, hostRecoveryObject, hostRecoveryIface,
        "CheckStatus", timeoutUs(90s), acknowledgeUsbDisruption);
}

inline void handleExitHostRecoveryPost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!checkHostRecoverySystem(app, req, asyncResp, systemName))
    {
        return;
    }
    // Exit restores the USB route and resets the host out of RCM, so it
    // carries the same explicit acknowledgment gate as Enter and Check.
    // com.nvidia.HostRecovery.ExitHostRecovery takes no acknowledgment
    // argument, so the gate is enforced here only.
    bool acknowledgeUsbDisruption = false;
    std::optional<std::string> reason;
    if (!json_util::readJsonAction(req, asyncResp->res,
                                   "AcknowledgeUsbDisruption",
                                   acknowledgeUsbDisruption, "Reason", reason))
    {
        return;
    }
    if (!acknowledgeUsbDisruption)
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, "false", "AcknowledgeUsbDisruption",
            "ExitHostRecovery");
        return;
    }
    using namespace std::chrono_literals;
    dbus::utility::async_method_call_timed(
        [asyncResp](const boost::system::error_code& ec,
                    sdbusplus::message_t& msg) {
            if (ec)
            {
                hostRecoveryClassifyError(asyncResp->res, ec, msg,
                                          "ExitHostRecovery");
                return;
            }
            hostRecoveryReportOutcome(asyncResp->res, msg, "ExitHostRecovery");
        },
        hostRecoveryService, hostRecoveryObject, hostRecoveryIface,
        "ExitHostRecovery", timeoutUs(120s),
        reason.value_or("Exited by operator over Redfish"));
}

inline void handleAbandonUnverifiedRecoverySessionPost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!checkHostRecoverySystem(app, req, asyncResp, systemName))
    {
        return;
    }
    std::string reason;
    if (!json_util::readJsonAction(req, asyncResp->res, "Reason", reason))
    {
        return;
    }
    using namespace std::chrono_literals;
    dbus::utility::async_method_call_timed(
        [asyncResp](const boost::system::error_code& ec,
                    sdbusplus::message_t& msg) {
            if (ec)
            {
                hostRecoveryClassifyError(asyncResp->res, ec, msg,
                                          "AbandonUnverifiedRecoverySession");
                return;
            }
            // The only action whose D-Bus method returns void: there is no
            // outcome snapshot to relay.
            messages::success(asyncResp->res);
        },
        hostRecoveryService, hostRecoveryObject, hostRecoveryIface,
        "AbandonUnverifiedRecoverySession", timeoutUs(30s), reason);
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

inline void requestRoutesNvidiaIgxHostRecovery(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Oem/Nvidia/HostRecovery/")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(nvidia_igx::handleHostRecoveryGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Actions/Oem/NvidiaComputerSystem.EnterHostRecovery/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            nvidia_igx::handleEnterHostRecoveryPost, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Actions/Oem/NvidiaComputerSystem.CheckHostRecoveryStatus/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            nvidia_igx::handleCheckHostRecoveryStatusPost, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Actions/Oem/NvidiaComputerSystem.ExitHostRecovery/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            nvidia_igx::handleExitHostRecoveryPost, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Actions/Oem/NvidiaComputerSystem.AbandonUnverifiedRecoverySession/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            nvidia_igx::handleAbandonUnverifiedRecoverySessionPost,
            std::ref(app)));
}

} // namespace redfish
