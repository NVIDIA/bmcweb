/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION &
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
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/nvidia_memory.hpp"
#include "utils/redfish_response_utils.hpp"

#include <boost/container/flat_map.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/message/native_types.hpp>
#include <sdbusplus/message/types.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>
namespace redfish
{
using DimmProperties =
    boost::container::flat_map<std::string, dbus::utility::DbusVariantType>;

namespace nvidia_memory
{

// Map the RowRemappingFailureState enum to bool; Unknown/unspecified ->
// nullopt so the caller omits the property.
inline std::optional<bool> translateRowRemappingFailure(
    const std::string& value)
{
    if (value == "com.nvidia.MemoryRowRemapping.RowRemappingFailureStates.True")
    {
        return true;
    }
    if (value ==
        "com.nvidia.MemoryRowRemapping.RowRemappingFailureStates.False")
    {
        return false;
    }
    return std::nullopt;
}

// Map the RowRemappingPendingState enum to bool; Unknown/unspecified ->
// nullopt so the caller omits the property.
inline std::optional<bool> translateRowRemappingPending(
    const std::string& value)
{
    if (value == "com.nvidia.MemoryRowRemapping.RowRemappingPendingStates.True")
    {
        return true;
    }
    if (value ==
        "com.nvidia.MemoryRowRemapping.RowRemappingPendingStates.False")
    {
        return false;
    }
    return std::nullopt;
}

// GPU HBM is published on D-Bus with the
// xyz.openbmc_project.Inventory.Item.Dimm interface, but nothing sets its
// FormFactor, MemoryDataWidth, MemoryTotalWidth, RevisionCode or
// MemoryAttributes. D-Bus keeps the defaults (the first FormFactor value,
// RDIMM, and 0), so the Memory resource shows BaseModuleType "RDIMM",
// BusWidthBits 0, DataWidthBits 0, FirmwareRevision "0" and RankCount 0.
// These values do not describe HBM. Remove them from a Memory resource whose
// MemoryDeviceType is HBM. Any other value is kept.
inline void removeHbmDimmPlaceholders(nlohmann::json& memory)
{
    if (!memory.is_object())
    {
        return;
    }
    const auto deviceType = memory.find("MemoryDeviceType");
    if (deviceType == memory.end())
    {
        return;
    }
    const std::string* type = deviceType->get_ptr<const std::string*>();
    if (type == nullptr || !type->starts_with("HBM"))
    {
        return;
    }

    const std::array<std::pair<const char*, nlohmann::json>, 5> placeholders{
        {{"BaseModuleType", "RDIMM"},
         {"BusWidthBits", 0},
         {"DataWidthBits", 0},
         {"FirmwareRevision", "0"},
         {"RankCount", 0}}};
    for (const auto& [key, placeholder] : placeholders)
    {
        const auto it = memory.find(key);
        if (it != memory.end() && *it == placeholder)
        {
            memory.erase(it);
        }
    }
}

// The memory-hbm-dimm-placeholders option is enabled by default and keeps
// the placeholder values. A platform that disables it drops them.
inline void applyHbmDimmPlaceholderOption(nlohmann::json& memory)
{
    if constexpr (!BMCWEB_MEMORY_HBM_DIMM_PLACEHOLDERS)
    {
        removeHbmDimmPlaceholders(memory);
    }
}

/**
 * @brief Fill out links association to parent processor by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getMemoryProcessorLink(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get parent processor link");
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_processor",
        "xyz.openbmc_project.Association", "endpoints",
        [aResp](const boost::system::error_code& ec2,
                const std::vector<std::string>& resp) {
            if (ec2)
            {
                return; // no processors = no failures
            }

            nlohmann::json& linksArray =
                aResp->res.jsonValue["Links"]["Processors"];
            linksArray = nlohmann::json::array();
            for (const std::string& processorPath : resp)
            {
                sdbusplus::message::object_path objectPath(processorPath);
                std::string processorName = objectPath.filename();
                if (processorName.empty())
                {
                    messages::internalError(aResp->res);
                    return;
                }
                linksArray.push_back(
                    {{"@odata.id",
                      "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/Processors/" + processorName}});
            }
        });
}

/**
 * @brief Fill out links association to parent chassis by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getMemoryChassisLink(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get parent chassis link");
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
        "xyz.openbmc_project.Association", "endpoints",
        [aResp](const boost::system::error_code& ec2,
                const std::vector<std::string>& resp) {
            if (ec2)
            {
                return; // no chassis = no failures
            }

            if (resp.size() > 1)
            {
                // Memory must have single parent chassis
                return;
            }
            const std::string& chassisPath = resp.front();
            sdbusplus::message::object_path objectPath(chassisPath);
            std::string chassisName = objectPath.filename();
            if (chassisName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            aResp->res.jsonValue["Links"]["Chassis"] = {
                {"@odata.id", "/redfish/v1/Chassis/" + chassisName}};
        });
}

inline void getMemoryDataByService(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                   const std::string& service,
                                   const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get memory metrics data.");
    dbus::utility::getAllProperties(
        service, objPath, "xyz.openbmc_project.Inventory.Item.Dimm",
        [aResp{std::move(aResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& property : properties)
            {
                if (property.first == "MemoryConfiguredSpeedInMhz")
                {
                    const uint16_t* value =
                        std::get_if<uint16_t>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    redfish::mapValidOrNull(aResp->res.jsonValue,
                                            "OperatingSpeedMHz", value);
                }
                else if (property.first == "Utilization")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["BandwidthPercent"] = *value;
                }
            }
        });
}

inline void getMemoryECCData(std::shared_ptr<bmcweb::AsyncResp> aResp,
                             const std::string& service,
                             const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get memory ecc data.");
    dbus::utility::getAllProperties(
        service, objPath, "xyz.openbmc_project.Memory.MemoryECC",
        [aResp{std::move(aResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& property : properties)
            {
                if (property.first == "ceCount")
                {
                    const int64_t* value =
                        std::get_if<int64_t>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    redfish::mapValidOrNull(aResp->res.jsonValue["LifeTime"],
                                            "CorrectableECCErrorCount", value);
                }
                else if (property.first == "ueCount")
                {
                    const int64_t* value =
                        std::get_if<int64_t>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    redfish::mapValidOrNull(aResp->res.jsonValue["LifeTime"],
                                            "UncorrectableECCErrorCount",
                                            value);
                }
            }
        });
}

inline void getMemoryMetrics(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& service,
    const std::string& objPath, const std::string& iface)
{
    BMCWEB_LOG_DEBUG("Get memory metrics data.");
    dbus::utility::getAllProperties(
        service, objPath, iface,
        [aResp{std::move(aResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for memory metrics");
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& property : properties)
            {
                if (property.first == "CapacityUtilizationPercent")
                {
                    const uint8_t* value =
                        std::get_if<uint8_t>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["CapacityUtilizationPercent"] = *value;
                }
            }
        });
}

inline void getMemoryRowRemappings(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                   const std::string& service,
                                   const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get memory row remapping counts.");
    dbus::utility::getAllProperties(
        service, objPath, "com.nvidia.MemoryRowRemapping",
        [aResp{std::move(aResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& property : properties)
            {
                if (property.first == "ceRowRemappingCount")
                {
                    const uint32_t* value =
                        std::get_if<uint32_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Invalid Data Type for property : {}",
                                         property.first);
                        messages::internalError(aResp->res);
                        return;
                    }
                    redfish::mapValidOrNull(
                        aResp->res.jsonValue["Oem"]["Nvidia"]["RowRemapping"],
                        "CorrectableRowRemappingCount", value);
                }
                else if (property.first == "ueRowRemappingCount")
                {
                    const uint32_t* value =
                        std::get_if<uint32_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Invalid Data Type for property : {}",
                                         property.first);
                        messages::internalError(aResp->res);
                        return;
                    }
                    redfish::mapValidOrNull(
                        aResp->res.jsonValue["Oem"]["Nvidia"]["RowRemapping"],
                        "UncorrectableRowRemappingCount", value);
                }
                else if (property.first == "HighRemappingAvailablityBankCount")
                {
                    const uint16_t* value =
                        std::get_if<uint16_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Invalid Data Type for property : {}",
                                         property.first);
                        messages::internalError(aResp->res);
                        return;
                    }
                    redfish::mapValidOrNull(
                        aResp->res.jsonValue["Oem"]["Nvidia"]["RowRemapping"],
                        "HighAvailabilityBankCount", value);
                }
                else if (property.first == "LowRemappingAvailablityBankCount")
                {
                    const uint16_t* value =
                        std::get_if<uint16_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Invalid Data Type for property : {}",
                                         property.first);
                        messages::internalError(aResp->res);
                        return;
                    }
                    redfish::mapValidOrNull(
                        aResp->res.jsonValue["Oem"]["Nvidia"]["RowRemapping"],
                        "LowAvailabilityBankCount", value);
                }
                else if (property.first == "MaxRemappingAvailablityBankCount")
                {
                    const uint16_t* value =
                        std::get_if<uint16_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Invalid Data Type for property : {}",
                                         property.first);
                        messages::internalError(aResp->res);
                        return;
                    }
                    redfish::mapValidOrNull(
                        aResp->res.jsonValue["Oem"]["Nvidia"]["RowRemapping"],
                        "MaxAvailabilityBankCount", value);
                }
                else if (property.first == "NoRemappingAvailablityBankCount")
                {
                    const uint16_t* value =
                        std::get_if<uint16_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Invalid Data Type for property : {}",
                                         property.first);
                        messages::internalError(aResp->res);
                        return;
                    }
                    redfish::mapValidOrNull(
                        aResp->res.jsonValue["Oem"]["Nvidia"]["RowRemapping"],
                        "NoAvailabilityBankCount", value);
                }
                else if (property.first ==
                         "PartialRemappingAvailablityBankCount")
                {
                    const uint16_t* value =
                        std::get_if<uint16_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Invalid Data Type for property : {}",
                                         property.first);
                        messages::internalError(aResp->res);
                        return;
                    }
                    redfish::mapValidOrNull(
                        aResp->res.jsonValue["Oem"]["Nvidia"]["RowRemapping"],
                        "PartialAvailabilityBankCount", value);
                }
            }
        });
}

inline void getMemoryMetricsData(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                 const std::string& dimmId)
{
    BMCWEB_LOG_DEBUG("Get available system memory resources.");
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Inventory.Item.Dimm"},
        [dimmId, aResp{std::move(aResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(" DBUS response error");
                messages::internalError(aResp->res);

                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!path.ends_with(dimmId))
                {
                    continue;
                }
                std::string memoryMetricsURI =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/Memory/";
                memoryMetricsURI += dimmId;
                memoryMetricsURI += "/MemoryMetrics";
                aResp->res.jsonValue["@odata.type"] =
                    "#MemoryMetrics.v1_7_0.MemoryMetrics";
                aResp->res.jsonValue["@odata.id"] = memoryMetricsURI;
                aResp->res.jsonValue["Id"] = "MemoryMetrics";
                aResp->res.jsonValue["Name"] = dimmId + " Memory Metrics";
                if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                {
                    aResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                        "#NvidiaMemoryMetrics.v1_0_0.NvidiaMemoryMetrics";
                }
                for (const auto& [service, interfaces] : object)
                {
                    if (std::find(interfaces.begin(), interfaces.end(),
                                  "xyz.openbmc_project.Inventory.Item.Dimm") !=
                        interfaces.end())
                    {
                        redfish::nvidia_memory::getMemoryDataByService(
                            aResp, service, path);
                    }
                    if (std::find(interfaces.begin(), interfaces.end(),
                                  "xyz.openbmc_project.Memory.MemoryECC") !=
                        interfaces.end())
                    {
                        redfish::nvidia_memory::getMemoryECCData(aResp, service,
                                                                 path);
                    }
                    if (std::find(
                            interfaces.begin(), interfaces.end(),
                            "xyz.openbmc_project.Inventory.Item.Dimm.MemoryMetrics") !=
                        interfaces.end())
                    {
                        getMemoryMetrics(
                            aResp, service, path,
                            "xyz.openbmc_project.Inventory.Item.Dimm.MemoryMetrics");
                    }

                    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                    {
                        if (std::find(interfaces.begin(), interfaces.end(),
                                      "com.nvidia.MemoryRowRemapping") !=
                            interfaces.end())
                        {
                            aResp->res
                                .jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                                "#NvidiaMemoryMetrics.v1_2_0.NvidiaGPUMemoryMetrics";
                            redfish::nvidia_memory::getMemoryRowRemappings(
                                aResp, service, path);
                        }
                        getStateSensors(aResp, path);
                    }
                }
                return;
            }
            // Object not found
            messages::resourceNotFound(aResp->res, "#Memory.v1_20_0.Memory",
                                       dimmId);
        });
}

/**
 * @brief Calibration status of one memory channel state object.
 *
 * OperationalStatus Enabled is Completed, Fault is Failed, Starting or
 * StandbyOffline is InProgress; any other state or an unavailable sample is
 * null.
 */
inline nlohmann::json toChannelCalibrationStatus(
    const dbus::utility::DBusPropertiesMap& properties)
{
    const bool* available = nullptr;
    const std::string* state = nullptr;
    if (!sdbusplus::unpackPropertiesNoThrow(dbus_utils::UnpackErrorPrinter(),
                                            properties, "Available", available,
                                            "State", state) ||
        state == nullptr || (available != nullptr && !*available))
    {
        return nullptr;
    }
    if (state->ends_with(".Enabled"))
    {
        return "Completed";
    }
    if (state->ends_with(".Fault"))
    {
        return "Failed";
    }
    if (state->ends_with(".Starting") || state->ends_with(".StandbyOffline"))
    {
        return "InProgress";
    }
    return nullptr;
}

struct MemoryChannelRead
{
    std::shared_ptr<bmcweb::AsyncResp> asyncResp;
    nlohmann::json status = nlohmann::json::array();
    size_t pending = 0;
};

inline void afterMemoryChannelState(
    const std::shared_ptr<MemoryChannelRead>& read, size_t channel,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (!ec)
    {
        read->status[channel] = toChannelCalibrationStatus(properties);
    }
    if (--read->pending != 0)
    {
        return;
    }
    nlohmann::json& oemNvidia = read->asyncResp->res.jsonValue["Oem"]["Nvidia"];
    oemNvidia["@odata.type"] = "#NvidiaMemory.v1_1_0.NvidiaMemory";
    oemNvidia["ChannelCalibrationStatus"] = std::move(read->status);
}

inline void afterMemoryChannelStates(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec || subtree.empty())
    {
        // No channel state objects: the property is not published.
        return;
    }
    std::vector<std::pair<std::string, std::string>> channels;
    for (const auto& [path, services] : subtree)
    {
        if (!services.empty())
        {
            channels.emplace_back(path, services.front().first);
        }
    }
    if (channels.empty())
    {
        return;
    }
    std::ranges::sort(channels);
    auto read = std::make_shared<MemoryChannelRead>();
    read->asyncResp = asyncResp;
    read->pending = channels.size();
    for (size_t channel = 0; channel < channels.size(); ++channel)
    {
        read->status.push_back(nullptr);
    }
    for (size_t channel = 0; channel < channels.size(); ++channel)
    {
        // Every decorator of the channel object comes from one service.
        dbus::utility::getAllProperties(
            channels[channel].second, channels[channel].first, "",
            std::bind_front(afterMemoryChannelState, read, channel));
    }
}

/**
 * @brief Oem.Nvidia.ChannelCalibrationStatus of a memory device.
 *
 * The backend publishes one state object per memory channel and associates
 * it with the memory object as one of its all_states. Array position is the
 * object path order of those objects.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       objPath     Memory object path.
 */
inline void getMemoryChannelCalibration(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::object_path& objPath)
{
    if constexpr (!BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        return;
    }
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.State.Decorator.OperationalStatus"};
    dbus::utility::getAssociatedSubTree(
        objPath / "all_states",
        sdbusplus::message::object_path("/xyz/openbmc_project/inventory"), 0,
        interfaces, std::bind_front(afterMemoryChannelStates, asyncResp));
}

inline void assignMemoryMetricsLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& dimmId)
{
    std::string baseUri =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Memory/" + dimmId;

    asyncResp->res.jsonValue["Metrics"]["@odata.id"] =
        baseUri + "/MemoryMetrics";
    asyncResp->res.jsonValue["EnvironmentMetrics"]["@odata.id"] =
        baseUri + "/EnvironmentMetrics";
}

} // namespace nvidia_memory
} // namespace redfish
