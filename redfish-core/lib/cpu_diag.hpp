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

#include <array>
#include <format>
#include <functional>
#include <memory>
#include <vector>

namespace redfish
{

// TODO(DGXOPENBMC-23861): The OEM field name "ProcessorDiagCapabilities"
// is misleading — it carries Enable/Disable + DiagMode + DiagStatus, which
// is boot status, not capabilities. Rename to "ProcessorDiagBootStatus"
// once external scripts are ready to adopt the new name (tracked via the
// weekly sync). Touches this file plus systems.hpp / nvidia_system.hpp
// route paths.

constexpr auto diagServiceList = "cpu-diag-status.timer "
                                 "cpu-diag-status.service";

enum class DiagStatus : uint8_t
{
    Inprogress = 0x0,
    RecoveryMode = 0x1,
    Completed = 0x2,
    Abort = 0x3,
    NotStarted = 0x4,
    TestRunning = 0x5
};

inline bool isDiagRunning(DiagStatus status)
{
    bool result = (status == DiagStatus::Inprogress) ||
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
        case DiagStatus::Inprogress:
            return "Inprogress";
        case DiagStatus::RecoveryMode:
            return "RecoveryMode";
        case DiagStatus::Completed:
            return "Completed";
        case DiagStatus::Abort:
            return "Abort";
        case DiagStatus::NotStarted:
            return "Not Started";
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

            nlohmann::json data =
                nlohmann::json::parse(jsonString, nullptr, false);
            if (data.is_discarded())
            {
                BMCWEB_LOG_ERROR(
                    "Failed to parse DiagSystemConfig JSON payload: {}",
                    jsonString);
                messages::internalError(asyncResp->res);
                return;
            }
            nlohmann::json& json = asyncResp->res.jsonValue;
            json["Oem"]["Nvidia"]["ProcessorDiagSysConfig"] = data;
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

            nlohmann::json data =
                nlohmann::json::parse(jsonString, nullptr, false);
            if (data.is_discarded())
            {
                BMCWEB_LOG_ERROR("Failed to parse DiagConfig JSON payload: {}",
                                 jsonString);
                messages::internalError(asyncResp->res);
                return;
            }
            nlohmann::json& json = asyncResp->res.jsonValue;
            json["Oem"]["Nvidia"]["ProcessorDiagTidConfig"] = data;
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

            nlohmann::json data =
                nlohmann::json::parse(jsonString, nullptr, false);
            if (data.is_discarded())
            {
                BMCWEB_LOG_ERROR("Failed to parse DiagResult JSON payload: {}",
                                 jsonString);
                messages::internalError(asyncResp->res);
                return;
            }
            nlohmann::json& json = asyncResp->res.jsonValue;
            json["Oem"]["Nvidia"]["ProcessorDiagResult"] =
                nlohmann::json::array();

            for (const auto& item : data)
            {
                uint8_t tid = item["Tid"].get<uint8_t>();
                uint16_t result = item["Result"].get<uint16_t>();
                uint8_t resultMaskSize = item["ResultMaskSize"].get<uint8_t>();
                std::vector<uint8_t> resultMask =
                    item["ResultMask"].get<std::vector<uint8_t>>();

                // Copy the required number of bytes
                std::vector<uint8_t> truncatedResultMask(
                    resultMask.begin(), resultMask.begin() + resultMaskSize);

                // Create an object with the required fields
                nlohmann::json jsonObject;
                jsonObject["Tid"] = tid;
                jsonObject["Result"] = result;
                jsonObject["ResultMaskSize"] = resultMaskSize;
                jsonObject["ResultMask"] = truncatedResultMask;

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
                json["Oem"]["Nvidia"]["ProcessorDiagCapabilities"]
                    ["DiagStatus"] =
                        diagStatusToString(static_cast<DiagStatus>(value));
            }
            else
            {
                if ((value == 0x1) || (value == 0x0))
                {
                    json["Oem"]["Nvidia"]["ProcessorDiagCapabilities"]
                        ["DiagStatus"] = "Inprogress";
                }
                else if (value == 0x2)
                {
                    json["Oem"]["Nvidia"]["ProcessorDiagCapabilities"]
                        ["DiagStatus"] = "Completed";
                }
                else if (value == 0x3)
                {
                    json["Oem"]["Nvidia"]["ProcessorDiagCapabilities"]
                        ["DiagStatus"] = "Abort";
                }
                else if (value == 0x4)
                {
                    json["Oem"]["Nvidia"]["ProcessorDiagCapabilities"]
                        ["DiagStatus"] = "Not Started";
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
            json["Oem"]["Nvidia"]["ProcessorDiagCapabilities"]["DiagMode"] =
                static_cast<int>(diagMode) != 0;
            // Always expose configs, status, and last-run result regardless
            // of DiagMode. The daemon owns DiagMode lifecycle and flips it
            // false at session end (clean or abort), but DiagStatus and the
            // previous run's DiagResult remain meaningful afterwards (e.g.
            // "NotStarted" or "Abort" with the last result still readable).
            handleDiagSysConfigGet(asyncResp);
            handleDiagTidConfigGet(asyncResp);
            handleDiagStatusGet(asyncResp);
            handleDiagResultGet(asyncResp);
        });
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
    using namespace std::string_literals;
    std::string propStr{};

    if (!redfish::json_util::getValueFromJsonObject(json, std::string(prop),
                                                    propStr))
    {
        BMCWEB_LOG_ERROR("Couldn't get {} from JSON {}", prop, json.dump());
        return false;
    }
    if constexpr (BMCWEB_PREBOOT_DIAG_SUPPORT)
    {
        // Vera path: D-Bus guards + prebootdiag property
        if (propStr == "Enable"s)
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
        else if (propStr == "Disable"s)
        {
            setPreBootDiagEnabled(aResp, false);
        }
        else
        {
            BMCWEB_LOG_ERROR("Invalid input it should be Enable/Disable");
            return false;
        }
    }
    else
    {
        // Grace path: systemctl timers + Settings DiagMode property
        if (propStr == "Enable"s)
        {
            std::string startupDiagTimerString = "systemctl start ";
            startupDiagTimerString += diagServiceList;
            // NOLINTNEXTLINE(cert-env33-c, concurrency-mt-unsafe)
            auto r = system(startupDiagTimerString.c_str());
            if (r != 0)
            {
                BMCWEB_LOG_ERROR("DiagFlowCtrl: service failed to start {}", r);
                return false;
            }
            setDiagModeProperty(aResp, true);
        }
        else if (propStr == "Disable"s)
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
                return false;
            }
            setDiagModeProperty(aResp, false);
        }
        else
        {
            BMCWEB_LOG_ERROR("Invalid input it should be Enable/Disable");
            return false;
        }
    }

    return true;
}

inline void handleDiagPostReq(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    nlohmann::json& procCap)
{
    if (!setDiagMode(asyncResp, procCap, "DiagMode"))
    {
        BMCWEB_LOG_ERROR("DiagMode property error");
        messages::propertyUnknown(asyncResp->res, "DiagMode");
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
        messages::propertyUnknown(asyncResp->res, "Invalid Configuration");
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
            messages::propertyUnknown(asyncResp->res, "Invalid Configuration");
            return false;
        }
        if (item["ConfigType"].get<unsigned>() > 1)
        {
            BMCWEB_LOG_ERROR(
                "Config Type value exceeds maximum allowed limit of 1");
            messages::propertyUnknown(asyncResp->res, "Invalid Configuration");
            return false;
        }
        if (item["TestDuration"].get<unsigned>() > 255)
        {
            BMCWEB_LOG_ERROR(
                "TestDuration value exceeds maximum allowed limit of 255");
            messages::propertyUnknown(asyncResp->res, "Invalid Configuration");
            return false;
        }
        // Validate DynamicData contains all unsigned numbers
        for (const auto& dynamicDataVal : item["DynamicData"])
        {
            if (!dynamicDataVal.is_number_unsigned())
            {
                BMCWEB_LOG_ERROR("Invalid type in 'DynamicData' array");
                messages::propertyUnknown(asyncResp->res,
                                          "Invalid Configuration");
                return false;
            }
            if (dynamicDataVal.get<unsigned>() > 255)
            {
                BMCWEB_LOG_ERROR(
                    "DynamicData value exceeds maximum allowed limit of 255");
                messages::propertyUnknown(asyncResp->res,
                                          "Invalid Configuration");
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
        messages::propertyUnknown(asyncResp->res, "Invalid Configuration");
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
            !item.contains("DynamicDataSize") ||
            !item["DynamicDataSize"].is_number_unsigned() ||
            !item.contains("DynamicData") || !item["DynamicData"].is_array())
        {
            BMCWEB_LOG_ERROR("Invalid item in DiagTidConfig");
            messages::propertyUnknown(asyncResp->res, "Invalid Configuration");
            return false;
        }

        if (item["Tid"].get<unsigned>() > 255)
        {
            BMCWEB_LOG_ERROR("Tid value exceeds maximum allowed limit of 255");
            messages::propertyUnknown(asyncResp->res, "Invalid Configuration");
            return false;
        }
        if (item["TestDuration"].get<unsigned>() > 255)
        {
            BMCWEB_LOG_ERROR(
                "TestDuration value exceeds maximum allowed limit of 255");
            messages::propertyUnknown(asyncResp->res, "Invalid Configuration");
            return false;
        }
        if (item["Loops"].get<unsigned>() > 65535)
        {
            BMCWEB_LOG_ERROR(
                "Loops value exceeds maximum allowed limit of 65535");
            messages::propertyUnknown(asyncResp->res, "Invalid Configuration");
            return false;
        }
        if (item["LogLevel"].get<unsigned>() > 255)
        {
            BMCWEB_LOG_ERROR(
                "LogLevel value exceeds maximum allowed limit of 255");
            messages::propertyUnknown(asyncResp->res, "Invalid Configuration");
            return false;
        }
        if (item["DynamicDataSize"].get<unsigned>() > 255)
        {
            BMCWEB_LOG_ERROR(
                "DynamicDataSize value exceeds maximum allowed limit of 255");
            messages::propertyUnknown(asyncResp->res, "Invalid Configuration");
            return false;
        }
        uint8_t dynamicDataSize = item["DynamicDataSize"].get<uint8_t>();
        std::vector<uint8_t> dynamicData =
            item["DynamicData"].get<std::vector<uint8_t>>();
        if (dynamicDataSize != dynamicData.size())
        {
            BMCWEB_LOG_ERROR("DynamicDataSize and DynamicData value mismatch");
            messages::propertyUnknown(asyncResp->res, "Invalid Configuration");
            return false;
        }
        unsigned tidValue = item["Tid"].get<unsigned>();
        if (!tidNumbers.insert(tidValue).second)
        {
            BMCWEB_LOG_ERROR("Duplicate TID");
            messages::propertyUnknown(asyncResp->res, "Invalid Configuration");
            return false;
        }
        // Validate DynamicData contains all unsigned numbers
        for (const auto& dynamicDataVal : item["DynamicData"])
        {
            if (!dynamicDataVal.is_number_unsigned())
            {
                BMCWEB_LOG_ERROR("Invalid type in 'DynamicData' array");
                messages::propertyUnknown(asyncResp->res,
                                          "Invalid Configuration");
                return false;
            }
            if (dynamicDataVal.get<unsigned>() > 255)
            {
                BMCWEB_LOG_ERROR(
                    "DynamicData value exceeds maximum allowed limit of 255");
                messages::propertyUnknown(asyncResp->res,
                                          "Invalid Configuration");
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
