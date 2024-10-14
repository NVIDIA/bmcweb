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
#include "error_messages.hpp"
#include "http_response.hpp"
#include "logging.hpp"

#include <app.hpp>
#include <boost/algorithm/string/split.hpp>
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
#include <utils/json_utils.hpp>
#include <utils/sw_utils.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace redfish
{
constexpr auto diagServiceList = "cpu-diag-status.timer "
                                 "cpu-diag-status.service";

inline void
    handleDiagSysConfigGet(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code& ec,
                    const std::variant<std::string>& res) {
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

        nlohmann::json& json = asyncResp->res.jsonValue;
        const std::string& jsonString = std::get<std::string>(res);
        nlohmann::json data = nlohmann::json::parse(jsonString);
        json["Oem"]["Nvidia"]["ProcessorDiagSysConfig"] = data;
    },
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Control.Diag", "DiagSystemConfig");
}

inline void
    handleDiagTidConfigGet(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code& ec,
                    const std::variant<std::string>& res) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error {}", ec);
            if (ec.value() == boost::asio::error::host_unreachable)
            {
                messages::resourceNotFound(asyncResp->res, "Get", "DiagConfig");
                return;
            }
            messages::internalError(asyncResp->res);
            return;
        }
        BMCWEB_LOG_DEBUG("Get Diag Config update done.");

        nlohmann::json& json = asyncResp->res.jsonValue;
        const std::string& jsonString = std::get<std::string>(res);
        nlohmann::json data = nlohmann::json::parse(jsonString);
        json["Oem"]["Nvidia"]["ProcessorDiagTidConfig"] = data;
    },
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Control.Diag", "DiagConfig");
}
inline void
    handleDiagResultGet(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code& ec,
                    const std::variant<std::string>& res) {
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
        const std::string& jsonString = std::get<std::string>(res);
        nlohmann::json data = nlohmann::json::parse(jsonString);
        json["Oem"]["Nvidia"]["ProcessorDiagResult"] = nlohmann::json::array();

        for (const auto& item : data)
        {
            uint8_t tid = item["Tid"];
            uint16_t result = item["Result"];
            uint8_t resultMaskSize = item["ResultMaskSize"];
            const std::vector<uint8_t>& resultMask = item["ResultMask"];

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
            json["Oem"]["Nvidia"]["ProcessorDiagResult"].push_back(jsonObject);
        }
    },
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Control.Diag", "DiagResult");
}
inline void
    handleDiagStatusGet(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code& ec,
                    const std::variant<uint8_t>& res) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error {}", ec);
            if (ec.value() == boost::asio::error::host_unreachable)
            {
                messages::resourceNotFound(asyncResp->res, "Get", "DiagStatus");
                return;
            }
            messages::internalError(asyncResp->res);
            return;
        }
        BMCWEB_LOG_DEBUG("Get Diag Status update done.");

        nlohmann::json& json = asyncResp->res.jsonValue;
        uint8_t value = std::get<uint8_t>(res);
        if ((value == 0x1) || (value == 0x0))
        {
            json["Oem"]["Nvidia"]["ProcessorDiagCapabilities"]["DiagStatus"] =
                "Inprogress";
        }
        else if (value == 0x2)
        {
            json["Oem"]["Nvidia"]["ProcessorDiagCapabilities"]["DiagStatus"] =
                "Completed";
        }
        else if (value == 0x3)
        {
            json["Oem"]["Nvidia"]["ProcessorDiagCapabilities"]["DiagStatus"] =
                "Abort";
        }
        else if (value == 0x4)
        {
            json["Oem"]["Nvidia"]["ProcessorDiagCapabilities"]["DiagStatus"] =
                "Not Started";
        }
    },
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Control.Diag", "DiagStatus");
}
inline void
    handleDiagModeGet(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code& ec,
                    const std::variant<bool>& resp) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error {}", ec);
            if (ec.value() == boost::asio::error::host_unreachable)
            {
                messages::resourceNotFound(asyncResp->res, "Set", "DiagMode");
                return;
            }
            messages::internalError(asyncResp->res);
            return;
        }
        BMCWEB_LOG_DEBUG("Diag mode update done.");
        nlohmann::json& json = asyncResp->res.jsonValue;
        bool diagMode = std::get<bool>(resp);
        if (diagMode == 0)
        {
            json["Oem"]["Nvidia"]["ProcessorDiagCapabilities"]["DiagMode"] =
                false;
        }
        else
        {
            json["Oem"]["Nvidia"]["ProcessorDiagCapabilities"]["DiagMode"] =
                true;
            handleDiagStatusGet(asyncResp);
            handleDiagSysConfigGet(asyncResp);
            handleDiagTidConfigGet(asyncResp);
            handleDiagResultGet(asyncResp);
        }
    },
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Control.Diag", "DiagMode");
}

inline bool initDiagStatus(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    // Set diag Status as not started
    std::uint8_t diagStatus = 4;
    std::variant<std::uint8_t> variantData = diagStatus;

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error {}", ec);
            if (ec.value() == boost::asio::error::host_unreachable)
            {
                messages::resourceNotFound(asyncResp->res, "Set", "DiagStatus");
                return;
            }
            messages::internalError(asyncResp->res);
            return;
        }
        BMCWEB_LOG_DEBUG("DiagStatus done.");
    },
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Control.Diag", "DiagStatus", variantData);

    return true;
}

inline bool clearDiagResult(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::string jsonString = R"([])";
    std::variant<std::string> variantData = jsonString;

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error {}", ec);
            if (ec.value() == boost::asio::error::host_unreachable)
            {
                messages::resourceNotFound(asyncResp->res, "Set", "DiagConfig");
                return;
            }
            messages::internalError(asyncResp->res);
            return;
        }
        BMCWEB_LOG_DEBUG("DiagConfig done.");
    },
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Control.Diag", "DiagResult", variantData);

    return true;
}

inline bool setDiagMode(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                        nlohmann::json& json, std::string_view prop,
                        std::optional<bool> val)
{
    using namespace std::string_literals;
    std::string startupDiagTimerString = "systemctl start ";
    std::string stopDiagTimerString = "systemctl stop ";
    startupDiagTimerString += diagServiceList;
    stopDiagTimerString += diagServiceList;
    std::string propStr;

    if (!redfish::json_util::getValueFromJsonObject(json, std::string(prop),
                                                    propStr))
    {
        return true;
    }
    if (propStr == "Enable"s)
    {
        val = true;
        auto r = system(startupDiagTimerString.c_str());
        if (r != 0)
        {
            BMCWEB_LOG_ERROR("DiagFlowCtrl: service failed to start {}", r);
            return false;
        }
    }
    else if (propStr == "Disable"s)
    {
        val = false;
        clearDiagResult(aResp);
        initDiagStatus(aResp);
        auto r = system(stopDiagTimerString.c_str());
        if (r != 0)
        {
            BMCWEB_LOG_ERROR("DiagFlowCtrl: service failed to stop {}", r);
            return false;
        }
    }
    else
    {
        BMCWEB_LOG_ERROR("Invalid input it should be Enable/Disable");
        return false;
    }
    bool value = val.value();
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec) {
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
    },
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Control.Diag", "DiagMode",
        dbus::utility::DbusVariantType(value));

    return true;
}

inline void
    handleDiagPostReq(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      nlohmann::json& procCap)
{
    std::optional<bool> diagMode;

    if (!setDiagMode(asyncResp, procCap, "DiagMode", diagMode))
    {
        BMCWEB_LOG_ERROR("DiagMode property error");
        messages::propertyUnknown(asyncResp->res, "DiagMode");
        return;
    }
}

inline bool
    validateDiagSysConfig(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
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
    std::variant<std::string> variantData = jsonString;

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
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
    },
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Control.Diag", "DiagSystemConfig", variantData);

    return true;
}
inline bool
    validateDiagTidConfig(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
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
        if (item["DynamicDataSize"].get<unsigned>() > 244)
        {
            BMCWEB_LOG_ERROR(
                "DynamicDataSize value exceeds maximum allowed limit of 244");
            messages::propertyUnknown(asyncResp->res, "Invalid Configuration");
            return false;
        }
        uint8_t dynamicDataSize = item["DynamicDataSize"];
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
    std::variant<std::string> variantData = jsonString;

    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
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
    },
        "xyz.openbmc_project.Settings", "/xyz/openbmc_project/Control/Diag",
        "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Control.Diag", "DiagConfig", variantData);

    return true;
}

} // namespace redfish
