/*
 * SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION &
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

#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "error_message_utils.hpp"
#include "error_messages.hpp"
#include "http_response.hpp"
#include "logging.hpp"

#include <app.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/url/format.hpp>
#include <dbus_utility.hpp>
#include <nlohmann/json.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/conditions_utils.hpp>
#include <utils/dbus_log_utils.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/fw_utils.hpp>
#include <utils/nvidia_json_utils.hpp>
#include <utils/sw_utils.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

namespace redfish
{

constexpr auto diagServiceList = "cpu-diag-status.timer "
                                 "cpu-diag-status.service";

// True when the JSON value is an unsigned number that survives narrowing to T.
// nlohmann's get<T>() narrows with an unchecked static_cast, so an is_number_
// unsigned() check alone lets an oversized persisted value through wrapped.
template <typename T>
inline bool fitsInUnsigned(const nlohmann::json& value)
{
    return value.is_number_unsigned() &&
           value.get<uint64_t>() <= std::numeric_limits<T>::max();
}

// Bound OEM action names and their parameter names, as advertised in
// NvidiaComputerSystem_v1.xml. Rejecting a request body is an action-parameter
// failure, not a property failure, so the ActionParameter* messages below need
// both names.
constexpr std::string_view setProcessorDiagModeAction = "SetProcessorDiagMode";
constexpr std::string_view processorDiagStateParam = "ProcessorDiagState";
constexpr std::string_view configProcessorDiagAction = "ConfigProcessorDiag";
constexpr std::string_view processorDiagSysConfigParam =
    "ProcessorDiagSysConfig";
constexpr std::string_view configProcessorDiagTidAction =
    "ConfigProcessorDiagTid";
constexpr std::string_view processorDiagTidConfigParam =
    "ProcessorDiagTidConfig";

enum class DiagStatus : uint8_t
{
    InProgress = 0x0,
    RecoveryMode = 0x1,
    Completed = 0x2,
    Aborted = 0x3,
    NotStarted = 0x4,
    TestRunning = 0x5
};

inline bool isDiagRunning(DiagStatus status)
{
    bool result = (status == DiagStatus::InProgress) ||
                  (status == DiagStatus::RecoveryMode) ||
                  (status == DiagStatus::TestRunning);
    BMCWEB_LOG_DEBUG("isDiagRunning: {} for status {}", result,
                     static_cast<uint8_t>(status));
    return result;
}

inline std::string diagStatusToString(DiagStatus status)
{
    switch (status)
    {
        case DiagStatus::InProgress:
            return "InProgress";
        case DiagStatus::RecoveryMode:
            return "RecoveryMode";
        case DiagStatus::Completed:
            return "Completed";
        case DiagStatus::Aborted:
            return "Aborted";
        case DiagStatus::NotStarted:
            return "NotStarted";
        case DiagStatus::TestRunning:
            return "TestRunning";
        default:
            return std::format("Unknown (0x{:x})",
                               static_cast<uint8_t>(status));
    }
}

inline void handleDiagSysConfigGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::getProperty<std::string>(
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "xyz.openbmc_project.Control.Diag", "DiagSystemConfig",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& jsonString) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                if (ec.value() == boost::asio::error::host_unreachable)
                {
                    messages::resourceNotFound(asyncResp->res, "Get",
                                               "DiagSystemConfig");
                    return;
                }
                messages::internalError(asyncResp->res);
                return;
            }
            BMCWEB_LOG_DEBUG("Get Diag Config update done.");

            // Non-throwing parse: the daemon owns this property and a
            // malformed value must fail the request, not abort the process.
            nlohmann::json& json = asyncResp->res.jsonValue;
            nlohmann::json data =
                nlohmann::json::parse(jsonString, nullptr, false);
            if (data.is_discarded())
            {
                BMCWEB_LOG_ERROR("Malformed DiagSystemConfig: {}", jsonString);
                messages::internalError(asyncResp->res);
                return;
            }
            json["Oem"]["Nvidia"]["ProcessorDiagSysConfig"] = std::move(data);
        });
}

inline void handleDiagTidConfigGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::getProperty<std::string>(
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "xyz.openbmc_project.Control.Diag", "DiagConfig",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& jsonString) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                if (ec.value() == boost::asio::error::host_unreachable)
                {
                    messages::resourceNotFound(asyncResp->res, "Get",
                                               "DiagConfig");
                    return;
                }
                messages::internalError(asyncResp->res);
                return;
            }
            BMCWEB_LOG_DEBUG("Get Diag Config update done.");

            nlohmann::json& json = asyncResp->res.jsonValue;
            nlohmann::json data =
                nlohmann::json::parse(jsonString, nullptr, false);
            if (data.is_discarded())
            {
                BMCWEB_LOG_ERROR("Malformed DiagConfig: {}", jsonString);
                messages::internalError(asyncResp->res);
                return;
            }
            json["Oem"]["Nvidia"]["ProcessorDiagTidConfig"] = std::move(data);
        });
}
inline void handleDiagResultGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::getProperty<std::string>(
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "xyz.openbmc_project.Control.Diag", "DiagResult",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& jsonString) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                if (ec.value() == boost::asio::error::host_unreachable)
                {
                    messages::resourceNotFound(asyncResp->res, "Get",
                                               "Diag Result");
                    return;
                }
                messages::internalError(asyncResp->res);
                return;
            }
            BMCWEB_LOG_DEBUG("Get Diag result update done.");

            nlohmann::json& json = asyncResp->res.jsonValue;
            nlohmann::json data =
                nlohmann::json::parse(jsonString, nullptr, false);
            if (data.is_discarded() || !data.is_array())
            {
                BMCWEB_LOG_ERROR("Malformed DiagResult: {}", jsonString);
                messages::internalError(asyncResp->res);
                return;
            }
            json["Oem"]["Nvidia"]["ProcessorDiagResult"] =
                nlohmann::json::array();

            for (const auto& item : data)
            {
                // Validate the persisted entry shape before extracting: the
                // daemon owns this property, and get<>() on a missing or
                // ill-typed member throws out of this callback. Range-check
                // each field too: get<>() narrows with an unchecked cast, so
                // an out-of-range value would otherwise be served wrapped
                // rather than rejected.
                if (!item.is_object() || !item.contains("Tid") ||
                    !fitsInUnsigned<uint8_t>(item["Tid"]) ||
                    !item.contains("Result") ||
                    !fitsInUnsigned<uint16_t>(item["Result"]) ||
                    !item.contains("ResultMask") ||
                    !item["ResultMask"].is_array())
                {
                    BMCWEB_LOG_ERROR("Malformed DiagResult entry: {}",
                                     item.dump());
                    messages::internalError(asyncResp->res);
                    return;
                }
                for (const auto& maskByte : item["ResultMask"])
                {
                    if (!fitsInUnsigned<uint8_t>(maskByte))
                    {
                        BMCWEB_LOG_ERROR("Malformed DiagResult entry: {}",
                                         item.dump());
                        messages::internalError(asyncResp->res);
                        return;
                    }
                }

                uint8_t tid = item["Tid"].get<uint8_t>();
                uint16_t result = item["Result"].get<uint16_t>();
                std::vector<uint8_t> resultMask =
                    item["ResultMask"].get<std::vector<uint8_t>>();

                // Entries persisted by producers that pad the mask carry a
                // ResultMaskSize field counting the valid bytes; honor it
                // when present so padding is not exposed. Producers storing
                // exact-length masks omit the field. Validate the persisted
                // field before use: a malformed value (negative, string, or
                // larger than the mask) is ignored rather than trusted.
                size_t maskSize = resultMask.size();
                if (const auto maskSizeIt = item.find("ResultMaskSize");
                    maskSizeIt != item.end() &&
                    maskSizeIt->is_number_unsigned())
                {
                    const size_t candidate = maskSizeIt->get<size_t>();
                    maskSize = std::min(candidate, maskSize);
                }
                if (maskSize < resultMask.size())
                {
                    resultMask.resize(maskSize);
                }

                // Create an object with the required fields
                nlohmann::json jsonObject;
                jsonObject["Tid"] = tid;
                jsonObject["Result"] = result;
                jsonObject["ResultMask"] = resultMask;

                // Add the object to the response array
                json["Oem"]["Nvidia"]["ProcessorDiagResult"].push_back(
                    jsonObject);
            }
        });
}
inline void handleDiagStatusGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::getProperty<uint8_t>(
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "xyz.openbmc_project.Control.Diag", "DiagStatus",
        [asyncResp](const boost::system::error_code& ec, const uint8_t& value) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                if (ec.value() == boost::asio::error::host_unreachable)
                {
                    messages::resourceNotFound(asyncResp->res, "Get",
                                               "DiagStatus");
                    return;
                }
                messages::internalError(asyncResp->res);
                return;
            }
            BMCWEB_LOG_DEBUG("Get Diag Status update done.");

            nlohmann::json& json = asyncResp->res.jsonValue;
            if constexpr (BMCWEB_PREBOOT_DIAG_SUPPORT)
            {
                json["Oem"]["Nvidia"]["ProcessorDiagState"]["DiagStatus"] =
                    diagStatusToString(static_cast<DiagStatus>(value));
            }
            else
            {
                if ((value == 0x1) || (value == 0x0))
                {
                    json["Oem"]["Nvidia"]["ProcessorDiagState"]["DiagStatus"] =
                        "InProgress";
                }
                else if (value == 0x2)
                {
                    json["Oem"]["Nvidia"]["ProcessorDiagState"]["DiagStatus"] =
                        "Completed";
                }
                else if (value == 0x3)
                {
                    json["Oem"]["Nvidia"]["ProcessorDiagState"]["DiagStatus"] =
                        "Aborted";
                }
                else if (value == 0x4)
                {
                    json["Oem"]["Nvidia"]["ProcessorDiagState"]["DiagStatus"] =
                        "NotStarted";
                }
            }
        });
}
inline void handleDiagModeGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::getProperty<bool>(
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "xyz.openbmc_project.Control.Diag", "DiagMode",
        [asyncResp](const boost::system::error_code& ec, const bool& diagMode) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                if (ec.value() == boost::asio::error::host_unreachable)
                {
                    messages::resourceNotFound(asyncResp->res, "Set",
                                               "DiagMode");
                    return;
                }
                messages::internalError(asyncResp->res);
                return;
            }
            BMCWEB_LOG_DEBUG("Diag mode update done.");
            nlohmann::json& json = asyncResp->res.jsonValue;
            json["Oem"]["Nvidia"]["ProcessorDiagState"]["DiagModeEnabled"] =
                diagMode;
            // Always expose configs, status, and last-run result regardless
            // of DiagMode. The daemon owns DiagMode lifecycle and flips it
            // false at session end (clean or abort), but DiagStatus and the
            // previous run's DiagResult remain meaningful afterwards (e.g.
            // "NotStarted" or "Aborted" with the last result still readable).
            handleDiagSysConfigGet(asyncResp);
            handleDiagTidConfigGet(asyncResp);
            handleDiagStatusGet(asyncResp);
            handleDiagResultGet(asyncResp);
        });
}

// Advertises the pre-boot diagnostic OEM actions under Actions.Oem of the
// ComputerSystem resource with their @Redfish.ActionInfo pointers, then
// fetches the current diagnostic state. Extracted from systems.hpp so the
// NVIDIA-specific payload lives in the NVIDIA-specific file and the generic
// handler calls only this one helper (mirroring
// advertiseSetProcessorPowerLimits). The BMCWEB_CPU_DIAG_SUPPORT gating
// remains at the systems.hpp call site.
//
// systemId is this build's own BMCWEB_REDFISH_SYSTEM_URI_NAME, so the surface
// lands on whichever ComputerSystem the image serves. On a tray carrying more
// than one manager, build cpu-diag-support only into the one that owns the
// diagnostic path; a second image would advertise the same actions over a
// D-Bus backend that is not there.
inline void advertiseProcessorDiagActions(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::string_view systemId)
{
    nlohmann::json& oemActions = asyncResp->res.jsonValue["Actions"]["Oem"];

    oemActions["#NvidiaComputerSystem.SetProcessorDiagMode"]["target"] =
        boost::urls::format("/redfish/v1/Systems/{}/Actions/Oem/"
                            "NvidiaComputerSystem.SetProcessorDiagMode",
                            systemId);
    oemActions
        ["#NvidiaComputerSystem.SetProcessorDiagMode"]
        ["@Redfish.ActionInfo"] = boost::urls::format(
            "/redfish/v1/Systems/{}/Oem/Nvidia/SetProcessorDiagModeActionInfo",
            systemId);

    oemActions["#NvidiaComputerSystem.ConfigProcessorDiag"]["target"] =
        boost::urls::format("/redfish/v1/Systems/{}/Actions/Oem/"
                            "NvidiaComputerSystem.ConfigProcessorDiag",
                            systemId);
    oemActions
        ["#NvidiaComputerSystem.ConfigProcessorDiag"]
        ["@Redfish.ActionInfo"] = boost::urls::format(
            "/redfish/v1/Systems/{}/Oem/Nvidia/ConfigProcessorDiagActionInfo",
            systemId);

    oemActions["#NvidiaComputerSystem.ConfigProcessorDiagTid"]["target"] =
        boost::urls::format("/redfish/v1/Systems/{}/Actions/Oem/"
                            "NvidiaComputerSystem.ConfigProcessorDiagTid",
                            systemId);
    oemActions
        ["#NvidiaComputerSystem.ConfigProcessorDiagTid"]
        ["@Redfish.ActionInfo"] = boost::urls::format(
            "/redfish/v1/Systems/{}/Oem/Nvidia/ConfigProcessorDiagTidActionInfo",
            systemId);

    handleDiagModeGet(asyncResp);
}

inline bool initDiagStatus(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::uint8_t diagStatus = static_cast<uint8_t>(DiagStatus::NotStarted);

    dbus::utility::setProperty(
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "xyz.openbmc_project.Control.Diag", "DiagStatus", diagStatus,
        [asyncResp](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                if (ec.value() == boost::asio::error::host_unreachable)
                {
                    messages::resourceNotFound(asyncResp->res, "Set",
                                               "DiagStatus");
                    return;
                }
                messages::internalError(asyncResp->res);
                return;
            }
            BMCWEB_LOG_DEBUG("DiagStatus reset to NotStarted.");
        });

    return true;
}

inline bool clearDiagResult(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::string jsonString = R"([])";

    dbus::utility::setProperty(
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "xyz.openbmc_project.Control.Diag", "DiagResult", jsonString,
        [asyncResp](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                if (ec.value() == boost::asio::error::host_unreachable)
                {
                    messages::resourceNotFound(asyncResp->res, "Set",
                                               "DiagResult");
                    return;
                }
                messages::internalError(asyncResp->res);
                return;
            }
            BMCWEB_LOG_DEBUG("DiagResult cleared.");
        });

    return true;
}

inline void setPreBootDiagEnabled(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, bool value)
{
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Object.Enable"};
    dbus::utility::getDbusObject(
        "/com/nvidia/prebootdiag", interfaces,
        [aResp, value](const boost::system::error_code& ec,
                       const dbus::utility::MapperGetObject& objInfo) {
            if (ec || objInfo.empty())
            {
                BMCWEB_LOG_ERROR(
                    "Failed to find prebootdiag service for /com/nvidia/prebootdiag: {}",
                    ec);
                messages::internalError(aResp->res);
                return;
            }
            const std::string& service = objInfo.begin()->first;
            dbus::utility::setProperty(
                service, "/com/nvidia/prebootdiag",
                "xyz.openbmc_project.Object.Enable", "Enabled", value,
                [aResp, value](const boost::system::error_code& ec2) {
                    if (ec2)
                    {
                        BMCWEB_LOG_ERROR(
                            "Failed to set PreBootDiag Enabled={}: {}", value,
                            ec2);
                        messages::internalError(aResp->res);
                        return;
                    }
                    BMCWEB_LOG_DEBUG("PreBootDiag Enabled set to {}.", value);
                });
        });
}

inline void setDiagModeProperty(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                                bool value)
{
    dbus::utility::setProperty(
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "xyz.openbmc_project.Control.Diag", "DiagMode", value,
        [aResp](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                if (ec.value() == boost::asio::error::host_unreachable)
                {
                    messages::resourceNotFound(aResp->res, "Set", "DiagMode");
                    return;
                }
                messages::internalError(aResp->res);
                return;
            }
            BMCWEB_LOG_DEBUG("DiagMode update done.");
        });
}

inline bool setDiagMode(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                        nlohmann::json& json, std::string_view prop)
{
    if (!json.is_object())
    {
        BMCWEB_LOG_ERROR("{} is not an object: {}", processorDiagStateParam,
                         json.dump());
        messages::actionParameterValueTypeError(
            aResp->res, json, processorDiagStateParam,
            setProcessorDiagModeAction);
        return false;
    }

    const auto propIt = json.find(std::string(prop));
    if (propIt == json.end())
    {
        BMCWEB_LOG_ERROR("Couldn't get {} from JSON {}", prop, json.dump());
        messages::actionParameterMissing(aResp->res, setProcessorDiagModeAction,
                                         prop);
        return false;
    }
    if (!propIt->is_boolean())
    {
        BMCWEB_LOG_ERROR("{} is not a boolean in JSON {}", prop, json.dump());
        messages::actionParameterValueTypeError(aResp->res, *propIt, prop,
                                                setProcessorDiagModeAction);
        return false;
    }

    const bool enable = propIt->get<bool>();
    if constexpr (BMCWEB_PREBOOT_DIAG_SUPPORT)
    {
        // Vera path: D-Bus guards + prebootdiag property
        if (enable)
        {
            // Guard 1: verify DiagConfig is non-empty (412 if absent)
            dbus::utility::getProperty<std::string>(
                "xyz.openbmc_project.Settings",
                "/xyz/openbmc_project/Control/Diag",
                "xyz.openbmc_project.Control.Diag", "DiagConfig",
                [aResp](const boost::system::error_code& ec,
                        const std::string& configStr) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "setDiagMode Enable: failed to read DiagConfig: {}",
                            ec);
                        messages::internalError(aResp->res);
                        return;
                    }
                    if (configStr.empty() || configStr == "[]")
                    {
                        messages::preconditionFailed(aResp->res);
                        return;
                    }

                    // Guard 2: verify DiagStatus is not running (409)
                    dbus::utility::getProperty<uint8_t>(
                        "xyz.openbmc_project.Settings",
                        "/xyz/openbmc_project/Control/Diag",
                        "xyz.openbmc_project.Control.Diag", "DiagStatus",
                        [aResp](const boost::system::error_code& ec2,
                                const uint8_t& diagStatus) {
                            if (ec2)
                            {
                                BMCWEB_LOG_ERROR(
                                    "setDiagMode Enable: failed to read DiagStatus: {}",
                                    ec2);
                                messages::internalError(aResp->res);
                                return;
                            }
                            BMCWEB_LOG_DEBUG("DiagStatus: {}", diagStatus);
                            if (isDiagRunning(
                                    static_cast<DiagStatus>(diagStatus)))
                            {
                                aResp->res.result(
                                    boost::beast::http::status::conflict);
                                messages::addMessageToErrorJson(
                                    aResp->res.jsonValue,
                                    messages::resourceInUse());
                                return;
                            }

                            // Setting Enabled=true on the prebootdiag service
                            // triggers the diag boot. The daemon owns the
                            // Settings DiagMode lifecycle (writes true on
                            // session start, false on any session end);
                            // bmcweb does not write it on the Vera path.
                            setPreBootDiagEnabled(aResp, true);
                        });
                });
        }
        else
        {
            setPreBootDiagEnabled(aResp, false);
        }
    }
    else
    {
        // Grace path: systemctl timers + Settings DiagMode property
        if (enable)
        {
            std::string startupDiagTimerString = "systemctl start ";
            startupDiagTimerString += diagServiceList;
            // NOLINTNEXTLINE(cert-env33-c, concurrency-mt-unsafe)
            auto r = system(startupDiagTimerString.c_str());
            if (r != 0)
            {
                BMCWEB_LOG_ERROR("DiagFlowCtrl: service failed to start {}", r);
                messages::internalError(aResp->res);
                return false;
            }
            setDiagModeProperty(aResp, true);
        }
        else
        {
            clearDiagResult(aResp);
            initDiagStatus(aResp);
            std::string stopDiagTimerString = "systemctl stop ";
            stopDiagTimerString += diagServiceList;
            // NOLINTNEXTLINE(cert-env33-c, concurrency-mt-unsafe)
            auto r = system(stopDiagTimerString.c_str());
            if (r != 0)
            {
                BMCWEB_LOG_ERROR("DiagFlowCtrl: service failed to stop {}", r);
                messages::internalError(aResp->res);
                return false;
            }
            setDiagModeProperty(aResp, false);
        }
    }

    return true;
}

inline void handleDiagPostReq(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    nlohmann::json& procCap)
{
    // setDiagMode reports the specific failure into asyncResp: an
    // ActionParameter* message for a bad request body, internalError for a
    // failure to drive the diagnostic services. Don't overwrite it here.
    if (!setDiagMode(asyncResp, procCap, "DiagModeEnabled"))
    {
        BMCWEB_LOG_ERROR("DiagModeEnabled property error");
        return;
    }
}

inline bool validateDiagSysConfig(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    nlohmann::json& diagSysConfigJson)
{
    if (!diagSysConfigJson.is_array())
    {
        BMCWEB_LOG_ERROR("DiagSysConfig should be an array");
        messages::actionParameterValueTypeError(
            asyncResp->res, diagSysConfigJson, processorDiagSysConfigParam,
            configProcessorDiagAction);
        return false;
    }

    for (const auto& item : diagSysConfigJson)
    {
        if (!item.is_object() || !item.contains("ConfigType") ||
            !item["ConfigType"].is_number_unsigned() ||
            !item.contains("TestDuration") ||
            !item["TestDuration"].is_number_unsigned() ||
            !item.contains("DynamicData") || !item["DynamicData"].is_array())
        {
            BMCWEB_LOG_ERROR("Invalid item in DiagSysConfig");
            messages::actionParameterValueFormatError(
                asyncResp->res, item, processorDiagSysConfigParam,
                configProcessorDiagAction);
            return false;
        }
        if (item["ConfigType"].get<uint64_t>() > 1)
        {
            BMCWEB_LOG_ERROR(
                "Config Type value exceeds maximum allowed limit of 1");
            messages::actionParameterValueOutOfRange(
                asyncResp->res, item["ConfigType"].dump(), "ConfigType",
                configProcessorDiagAction);
            return false;
        }
        if (item["TestDuration"].get<uint64_t>() > 255)
        {
            BMCWEB_LOG_ERROR(
                "TestDuration value exceeds maximum allowed limit of 255");
            messages::actionParameterValueOutOfRange(
                asyncResp->res, item["TestDuration"].dump(), "TestDuration",
                configProcessorDiagAction);
            return false;
        }
        // NSM_DIAG_MAX_DYNAMIC_DATA_SIZE (libnsm/diagnostics.h): reject
        // here what the Cmd 0x80 encoder would reject as a length error.
        if (item["DynamicData"].size() > 251)
        {
            BMCWEB_LOG_ERROR(
                "DynamicData exceeds maximum allowed length of 251");
            messages::arraySizeTooLong(asyncResp->res, "DynamicData", 251);
            return false;
        }
        // Validate DynamicData contains all unsigned numbers
        for (const auto& dynamicDataVal : item["DynamicData"])
        {
            if (!dynamicDataVal.is_number_unsigned())
            {
                BMCWEB_LOG_ERROR("Invalid type in 'DynamicData' array");
                messages::actionParameterValueTypeError(
                    asyncResp->res, dynamicDataVal, "DynamicData",
                    configProcessorDiagAction);
                return false;
            }
            if (dynamicDataVal.get<uint64_t>() > 255)
            {
                BMCWEB_LOG_ERROR(
                    "DynamicData value exceeds maximum allowed limit of 255");
                messages::actionParameterValueOutOfRange(
                    asyncResp->res, dynamicDataVal.dump(), "DynamicData",
                    configProcessorDiagAction);
                return false;
            }
        }
    }
    return true;
}

inline bool handleDiagSysConfigPostReq(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    nlohmann::json& diagSysConfigCap)
{
    if (!validateDiagSysConfig(asyncResp, diagSysConfigCap))
    {
        BMCWEB_LOG_ERROR("DiagSystemConfig Json is not proper");
        return false;
    }

    std::string jsonString = diagSysConfigCap.dump();

    dbus::utility::setProperty(
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "xyz.openbmc_project.Control.Diag", "DiagSystemConfig", jsonString,
        [asyncResp](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                if (ec.value() == boost::asio::error::host_unreachable)
                {
                    messages::resourceNotFound(asyncResp->res, "Set",
                                               "DiagSystemConfig");
                    return;
                }
                messages::internalError(asyncResp->res);
                return;
            }
            BMCWEB_LOG_DEBUG("DiagSystemConfig done.");
        });

    return true;
}
inline bool validateDiagTidConfig(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    nlohmann::json& diagTidConfigJson)
{
    std::set<unsigned> tidNumbers;

    if (!diagTidConfigJson.is_array())
    {
        BMCWEB_LOG_ERROR("DiagTidConfig should be an array");
        messages::actionParameterValueTypeError(
            asyncResp->res, diagTidConfigJson, processorDiagTidConfigParam,
            configProcessorDiagTidAction);
        return false;
    }

    for (const auto& item : diagTidConfigJson)
    {
        if (!item.is_object() || !item.contains("Tid") ||
            !item["Tid"].is_number_unsigned() ||
            !item.contains("TestDuration") ||
            !item["TestDuration"].is_number_unsigned() ||
            !item.contains("Loops") || !item["Loops"].is_number_unsigned() ||
            !item.contains("LogLevel") ||
            !item["LogLevel"].is_number_unsigned() ||
            !item.contains("DynamicData") || !item["DynamicData"].is_array())
        {
            BMCWEB_LOG_ERROR("Invalid item in DiagTidConfig");
            messages::actionParameterValueFormatError(
                asyncResp->res, item, processorDiagTidConfigParam,
                configProcessorDiagTidAction);
            return false;
        }

        if (item["Tid"].get<uint64_t>() > 255)
        {
            BMCWEB_LOG_ERROR("Tid value exceeds maximum allowed limit of 255");
            messages::actionParameterValueOutOfRange(
                asyncResp->res, item["Tid"].dump(), "Tid",
                configProcessorDiagTidAction);
            return false;
        }
        if (item["TestDuration"].get<uint64_t>() > 255)
        {
            BMCWEB_LOG_ERROR(
                "TestDuration value exceeds maximum allowed limit of 255");
            messages::actionParameterValueOutOfRange(
                asyncResp->res, item["TestDuration"].dump(), "TestDuration",
                configProcessorDiagTidAction);
            return false;
        }
        if (item["Loops"].get<uint64_t>() > 65535)
        {
            BMCWEB_LOG_ERROR(
                "Loops value exceeds maximum allowed limit of 65535");
            messages::actionParameterValueOutOfRange(
                asyncResp->res, item["Loops"].dump(), "Loops",
                configProcessorDiagTidAction);
            return false;
        }
        if (item["LogLevel"].get<uint64_t>() > 255)
        {
            BMCWEB_LOG_ERROR(
                "LogLevel value exceeds maximum allowed limit of 255");
            messages::actionParameterValueOutOfRange(
                asyncResp->res, item["LogLevel"].dump(), "LogLevel",
                configProcessorDiagTidAction);
            return false;
        }
        unsigned tidValue = item["Tid"].get<unsigned>();
        if (!tidNumbers.insert(tidValue).second)
        {
            BMCWEB_LOG_ERROR("Duplicate TID");
            messages::actionParameterDuplicate(
                asyncResp->res, configProcessorDiagTidAction, "Tid");
            return false;
        }
        // NSM_DIAG_MAX_TID_DYNAMIC_DATA_SIZE (libnsm/diagnostics.h): reject
        // here what the Cmd 0x81 encoder would reject as a length error.
        if (item["DynamicData"].size() > 244)
        {
            BMCWEB_LOG_ERROR(
                "DynamicData exceeds maximum allowed length of 244");
            messages::arraySizeTooLong(asyncResp->res, "DynamicData", 244);
            return false;
        }
        // Validate DynamicData contains all unsigned numbers
        for (const auto& dynamicDataVal : item["DynamicData"])
        {
            if (!dynamicDataVal.is_number_unsigned())
            {
                BMCWEB_LOG_ERROR("Invalid type in 'DynamicData' array");
                messages::actionParameterValueTypeError(
                    asyncResp->res, dynamicDataVal, "DynamicData",
                    configProcessorDiagTidAction);
                return false;
            }
            if (dynamicDataVal.get<uint64_t>() > 255)
            {
                BMCWEB_LOG_ERROR(
                    "DynamicData value exceeds maximum allowed limit of 255");
                messages::actionParameterValueOutOfRange(
                    asyncResp->res, dynamicDataVal.dump(), "DynamicData",
                    configProcessorDiagTidAction);
                return false;
            }
        }
    }
    return true;
}
inline bool handleDiagTidConfigPostReq(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    nlohmann::json& diagTidConfigCap)
{
    if (!validateDiagTidConfig(asyncResp, diagTidConfigCap))
    {
        BMCWEB_LOG_ERROR("DiagTidConfig Json is not proper");
        return false;
    }
    std::string jsonString = diagTidConfigCap.dump();

    dbus::utility::setProperty(
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "xyz.openbmc_project.Control.Diag", "DiagConfig", jsonString,
        [asyncResp](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                if (ec.value() == boost::asio::error::host_unreachable)
                {
                    messages::resourceNotFound(asyncResp->res, "Set",
                                               "DiagTidConfig");
                    return;
                }
                messages::internalError(asyncResp->res);
                return;
            }
            BMCWEB_LOG_DEBUG("DiagTidConfig done.");
        });

    return true;
}

} // namespace redfish
