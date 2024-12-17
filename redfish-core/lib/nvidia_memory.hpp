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
#include "dbus_utility.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/pcie_util.hpp"
#include "utils/sw_utils.hpp"
#include "utils/time_utils.hpp"

namespace redfish
{
using DimmProperties =
    boost::container::flat_map<std::string, dbus::utility::DbusVariantType>;
namespace nvidia_memory
{

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
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec2,
                std::variant<std::vector<std::string>>& resp) {
            if (ec2)
            {
                return; // no processors = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            nlohmann::json& linksArray =
                aResp->res.jsonValue["Links"]["Processors"];
            linksArray = nlohmann::json::array();
            for (const std::string& processorPath : *data)
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
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_processor",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
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
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec2,
                std::variant<std::vector<std::string>>& resp) {
            if (ec2)
            {
                return; // no chassis = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr || data->size() > 1)
            {
                // Memory must have single parent chassis
                return;
            }
            const std::string& chassisPath = data->front();
            sdbusplus::message::object_path objectPath(chassisPath);
            std::string chassisName = objectPath.filename();
            if (chassisName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            aResp->res.jsonValue["Links"]["Chassis"] = {
                {"@odata.id", "/redfish/v1/Chassis/" + chassisName}};
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getMemoryDataByService(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                   const std::string& service,
                                   const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get memory metrics data.");
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}](const boost::system::error_code ec,
                                  const DimmProperties& properties) {
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
                    aResp->res.jsonValue["OperatingSpeedMHz"] = *value;
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
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Inventory.Item.Dimm");
}

inline void getMemoryECCData(std::shared_ptr<bmcweb::AsyncResp> aResp,
                             const std::string& service,
                             const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get memory ecc data.");
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}](const boost::system::error_code ec,
                                  const DimmProperties& properties) {
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
                    aResp->res
                        .jsonValue["LifeTime"]["CorrectableECCErrorCount"] =
                        *value;
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
                    aResp->res
                        .jsonValue["LifeTime"]["UncorrectableECCErrorCount"] =
                        *value;
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Memory.MemoryECC");
}

inline void getMemoryMetrics(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& service,
    const std::string& objPath, const std::string& iface)
{
    BMCWEB_LOG_DEBUG("Get memory metrics data.");
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}](const boost::system::error_code ec,
                                  const DimmProperties& properties) {
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
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll", iface);
}

inline void getMemoryRowRemappings(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                   const std::string& service,
                                   const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get memory row remapping counts.");
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}](const boost::system::error_code ec,
                                  const DimmProperties& properties) {
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
                    aResp->res.jsonValue["Oem"]["Nvidia"]["RowRemapping"]
                                        ["CorrectableRowRemappingCount"] =
                        *value;
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
                    aResp->res.jsonValue["Oem"]["Nvidia"]["RowRemapping"]
                                        ["UncorrectableRowRemappingCount"] =
                        *value;
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
                    aResp->res.jsonValue["Oem"]["Nvidia"]["RowRemapping"]
                                        ["HighAvailabilityBankCount"] = *value;
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
                    aResp->res.jsonValue["Oem"]["Nvidia"]["RowRemapping"]
                                        ["LowAvailabilityBankCount"] = *value;
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
                    aResp->res.jsonValue["Oem"]["Nvidia"]["RowRemapping"]
                                        ["MaxAvailabilityBankCount"] = *value;
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
                    aResp->res.jsonValue["Oem"]["Nvidia"]["RowRemapping"]
                                        ["NoAvailabilityBankCount"] = *value;
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
                    aResp->res.jsonValue["Oem"]["Nvidia"]["RowRemapping"]
                                        ["PartialAvailabilityBankCount"] =
                        *value;
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.MemoryRowRemapping");
}

inline void getMemoryMetricsData(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                 const std::string& dimmId)
{
    BMCWEB_LOG_DEBUG("Get available system memory resources.");
    crow::connections::systemBus->async_method_call(
        [dimmId, aResp{std::move(aResp)}](
            const boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, boost::container::flat_map<
                                 std::string, std::vector<std::string>>>&
                subtree) {
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
                    }
                }
                return;
            }
            // Object not found
            messages::resourceNotFound(aResp->res, "#Memory.v1_20_0.Memory",
                                       dimmId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 1>{"xyz.openbmc_project.Inventory.Item.Dimm"});
}

} // namespace nvidia_memory
} // namespace redfish
