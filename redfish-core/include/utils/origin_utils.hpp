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
/*!
 * @file    origin_utils.cpp
 * @brief   Source code for utility functions of handling origin of condition.
 */

#pragma once

#include "dbus_utility.hpp"
#include "registries.hpp"
#include "str_utility.hpp"

#include <algorithm>
#include <chrono>

namespace redfish
{
namespace origin_utils
{

const std::string redfishPrefix = "/redfish/v1";

const std::string inventorySubTree = "/xyz/openbmc_project/inventory";
const std::string sensorSubTree = "/xyz/openbmc_project/sensors";

// All the Chassis Devices follows pattern:
// "/xyz/openbmc_project/inventory/system/chassis/HGX_GPU_SXM_1/PCIeDevices/GPU_SXM_1"
// or "/xyz/openbmc_project/inventory/system/chassis/HGX_GPU_SXM_1"

// We find "/xyz/openbmc_project/inventory/system/chassis/" and remove it to use
// the device path: "HGX_GPU_SXM_1/PCIeDevices/GPU_SXM_1" and create redfish
// URI:
// "/redfish/v1/Chassis/HGX_GPU_SXM_1/PCIeDevices/GPU_SXM_1"
const std::string chassisPrefixDbus =
    "/xyz/openbmc_project/inventory/system/chassis/";
const std::string chassisPrefix = "/redfish/v1/Chassis/";

// All the Systems Devices:
const std::string systemsPrefixDbus =
    "/xyz/openbmc_project/inventory/system/systems/";
const std::string systemsPrefixRedfish = "/redfish/v1/Systems/";

// All the Fabric Devices follows pattern:

// "/xyz/openbmc_project/inventory/system/fabrics/HGX_NVLinkFabric_0/Switches/NVSwitch_0/Ports"

// We find "/xyz/openbmc_project/inventory/system/fabrics/" and remove it to use
// the device path: "HGX_NVLinkFabric_0/Switches/NVSwitch_0/Ports" and create
// redfish URI:
// "/redfish/v1/Fabrics/HGX_NVLinkFabric_0/Switches/NVSwitch_0/Ports"
const std::string fabricsPrefixDbus =
    "/xyz/openbmc_project/inventory/system/fabrics/";
const std::string fabricsPrefix = "/redfish/v1/Fabrics/";

// All the Memory Devices follows pattern:

// "/xyz/openbmc_project/inventory/system/memory/GPU_SXM_1_DRAM_0"

// We find "/xyz/openbmc_project/inventory/system/memory/" and remove it to use
// the device path: "GPU_SXM_1_DRAM_0" and create redfish URI:
// "/redfish/v1/Systems/HGX_Baseboard_0/Memory/GPU_SXM_1_DRAM_0"
const std::string memoryPrefixDbus =
    "/xyz/openbmc_project/inventory/system/memory/";
const std::string memoryPrefix = std::format("/redfish/v1/Systems/{}/Memory/",
                                             BMCWEB_REDFISH_SYSTEM_URI_NAME);

// All the Processor Devices follows pattern:

// "/xyz/openbmc_project/inventory/system/processors/GPU_SXM_1/Ports/NVLink_0"

// We find "/xyz/openbmc_project/inventory/system/processors/" and remove it to
// use the device path: "GPU_SXM_1/Ports/NVLink_0" and create redfish URI:
// "/redfish/v1/Systems/HGX_Baseboard_0/Processors/GPU_SXM_1/Ports/NVLink_0"
const std::string processorPrefixDbus =
    "/xyz/openbmc_project/inventory/system/processors/";
const std::string processorPrefix = std::format(
    "/redfish/v1/Systems/{}/Processors/", BMCWEB_REDFISH_SYSTEM_URI_NAME);

// All the Processor Devices follows pattern:

// "/xyz/openbmc_project/software/HGX_FW_FPGA_0"

// We find "/xyz/openbmc_project/software/" and remove it to use the device
// path: "HGX_FW_FPGA_0" and create redfish URI:
// "/redfish/v1/UpdateService/FirmwareInventory/HGX_FW_FPGA_0"
const std::string softwarePrefixDbus = "/xyz/openbmc_project/software/";
const std::string firmwarePrefix =
    "/redfish/v1/UpdateService/FirmwareInventory/";

static std::map<std::string, std::string> dBusToRedfishURI = {
    {chassisPrefixDbus, chassisPrefix},
    {fabricsPrefixDbus, fabricsPrefix},
    {processorPrefixDbus, processorPrefix},
    {memoryPrefixDbus, memoryPrefix},
    {softwarePrefixDbus, firmwarePrefix},
    {sensorSubTree, chassisPrefix},
    {systemsPrefixDbus, systemsPrefixRedfish}};

/**
 * Utility function for populating async response with
 * service conditions json containing origin of condition
 * device
 */

inline void oocUtilServiceConditions(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const std::string& ooc,
    const std::string& messageArgs, const std::string& timestamp,
    const std::string& severity, const std::string& id,
    const std::string& messageId)
{
    nlohmann::json j;
    BMCWEB_LOG_DEBUG("Generating MessageRegistry for [{}]", messageId);
    const registries::Message* msg = registries::getMessage(messageId);

    if (msg == nullptr)
    {
        BMCWEB_LOG_ERROR("Failed to lookup the message for MessageId[{}]",
                         messageId);
        return;
    }

    std::vector<std::string> fields;
    fields.reserve(msg->numberOfArgs);
    bmcweb::split(fields, messageArgs, ',');

    std::span<std::string> msgArgs;
    msgArgs = {&fields[0], fields.size()};

    std::string message = msg->message;
    int i = 0;
    for (auto& arg : msgArgs)
    {
        std::string argStr = "%" + std::to_string(++i);
        size_t argPos = message.find(argStr);
        if (argPos != std::string::npos)
        {
            message.replace(argPos, argStr.length(), arg);
        }
    }
    j = {{"Severity", severity},
         {"Timestamp", timestamp},
         {"Message", message},
         {"MessageId", messageId},
         {"MessageArgs", msgArgs}};
    j["LogEntry"]["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/"
        "LogServices/EventLog/Entries/" +
        id;
    j["@odata.type"] = "#LogEntry.v1_13_0.LogEntry";
    if (ooc.size() > 0)
    {
        BMCWEB_LOG_DEBUG("Populating service conditions with ooc {}", ooc);
        j["OriginOfCondition"]["@odata.id"] = ooc;
    }
    if (asyncResp->res.jsonValue.contains("Conditions"))
    {
        asyncResp->res.jsonValue["Conditions"].push_back(j);
    }
    else
    {
        asyncResp->res.jsonValue["Status"]["Conditions"].push_back(j);
    }
}

/**
 * Utility function for populating async response with
 * origin of condition device for system events
 */

inline void oocUtil(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    nlohmann::json& logEntry, const std::string& id, const std::string& ooc,
    const std::string& severity = "", const std::string& messageArgs = "",
    const std::string& timestamp = "", const std::string& messageId = "")
{
    if (!severity.empty())
    {
        oocUtilServiceConditions(asyncResp, ooc, messageArgs, timestamp,
                                 severity, id, messageId);
        return;
    }
    if (ooc.size() > 0)
    {
        if (!asyncResp->res.jsonValue.contains("Members"))
        {
            asyncResp->res
                .jsonValue["Links"]["OriginOfCondition"]["@odata.id"] = ooc;
            return;
        }
        logEntry["Links"]["OriginOfCondition"]["@odata.id"] = ooc;
    }
    return;
}

/**
 * Wrapper function for setting origin of condition
 * based on DBus path that will walk through different
 * device methods as necessary to set OOC properly
 */
inline void convertDbusObjectToOriginOfCondition(
    const std::string& path, const std::string& id,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    nlohmann::json& logEntry, const std::string& deviceName,
    const std::string& severity = "", const std::string& messageArgs = "",
    const std::string& timestamp = "", const std::string& messageId = "")
{
    if (path.empty())
    {
        BMCWEB_LOG_WARNING("Empty path/OriginOfCondition");
        return;
    }

    // if redfish URI is already provided in path, no need to compute, just use
    // it
    if (path.starts_with(redfishPrefix))
    {
        oocUtil(asyncResp, logEntry, id, path, severity, messageArgs, timestamp,
                messageId);
        return;
    }
    for (auto& it : dBusToRedfishURI)
    {
        if (path.find(it.first) != std::string::npos)
        {
            std::string newPath;
            if (it.first == sensorSubTree)
            {
                std::string chassisName = std::format(
                    "{}{}", BMCWEB_PLATFORM_DEVICE_PREFIX, deviceName);
                std::string sensorName;
                dbus::utility::getNthStringFromPath(path, 4, sensorName);
                newPath = chassisName + "/Sensors/";
                newPath += sensorName;
            }
            else
            {
                newPath = path.substr(it.first.length(), path.length());
            }

            oocUtil(asyncResp, logEntry, id, it.second + newPath, severity,
                    messageArgs, timestamp, messageId);
            return;
        }
    }
    oocUtil(asyncResp, logEntry, id, std::string(""), severity, messageArgs,
            timestamp, messageId);
    BMCWEB_LOG_ERROR(
        "No Matching prefix found for OriginOfCondition DBus object Path: {}",
        path);
    return;
}

inline std::string getDeviceRedfishURI(const std::string& device)
{
    // if 'device' is already a redfish URI, return it directly.
    if (std::string_view(device).starts_with(redfishPrefix))
    {
        return device;
    }

    if (BMCWEB_REDFISH_SYSTEM_URI_NAME.ends_with(device))
    {
        return systemsPrefixRedfish +
               std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME);
    }
    else
    {
        return std::format("{}{}{}", chassisPrefix,
                           BMCWEB_PLATFORM_DEVICE_PREFIX, device);
    }
}

} // namespace origin_utils
} // namespace redfish
