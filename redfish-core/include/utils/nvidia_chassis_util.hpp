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

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "failover_policy.hpp"
#include "generated/enums/chassis.hpp"
#include "generated/enums/nvidia_chassis.hpp"
#include "generated/enums/resource.hpp"
#include "trusted_components.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/conditions_utils.hpp"
#include "utils/health_utils.hpp"
#include "utils/hex_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_async_set_callbacks.hpp"
#include "utils/nvidia_write_protect_domains_util.hpp"
#include "utils/redfish_response_utils.hpp"
#include "utils/time_utils.hpp"

#include <boost/container/flat_set.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/linux_error.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>
#include <openbmc_dbus_rest.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

namespace redfish
{
/**
 * @brief Get list of chassis that support in-band updates
 *
 * @param asyncResp - Pointer to object holding response data
 * @param callback Function to call with the list of chassis paths
 *                 The callback should accept std::vector<std::string>
 * parameter
 * @return None
 */
template <typename CallbackFunc>
void getChassisListForInBand(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    CallbackFunc&& callback)
{
    constexpr std::array<std::string_view, 1> interfaces = {
        "com.nvidia.InbandUpdatePolicy"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory/system/chassis", 0, interfaces,
        [asyncResp, callback = std::forward<CallbackFunc>(callback)](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("error_code = {}", ec);
                BMCWEB_LOG_ERROR("error msg = {}", ec.message());
                messages::internalError(asyncResp->res);
                callback(std::vector<std::string>());
                return;
            }

            std::vector<std::string> chassisList;
            for (const auto& object : subtree)
            {
                const std::string& path = object.first;
                sdbusplus::message::object_path objectPath(path);
                const std::string chassisName = objectPath.filename();
                chassisList.push_back(chassisName);
            }

            callback(chassisList);
        });
}

namespace nvidia_chassis_utils
{

using Associations =
    std::vector<std::tuple<std::string, std::string, std::string>>;
constexpr const char* bootStatusIntf = "com.nvidia.RoT.BootStatus";

constexpr const size_t trayTopologyStringLength = 16;
constexpr const size_t trayTopologyByteLength = 8;
constexpr const size_t trayTopologyTokenLength = 2;
constexpr const uint8_t trayTopologyMinRevision = 2;
static constexpr uint8_t mctpTypeVDMIANA = 0x7f;
#pragma pack(1)
struct TrayTopology
{
    uint8_t revision;
    uint8_t reserved1;
    uint8_t chassisSlotNumber;
    uint8_t trayIndex;
    uint8_t topologyId;
    uint8_t reserved2;
    uint8_t reserved3;
    uint8_t reserved4;
};
#pragma pack()

/**
 * @brief Conditionally populate Chassis Links/ComputerSystems.
 *
 * The DMTF Chassis schema defines Links/ComputerSystems as the computer systems
 * a chassis "directly and wholly contains", so the link must only appear on the
 * chassis that actually embodies the host (e.g. HGX_Chassis_0) rather than on
 * every chassis. Instead of hardcoding that platform assumption, the link is
 * gated on a "computer_system" D-Bus association authored from
 * entity-manager/nsmd config on the hosting chassis (mirroring how the adjacent
 * Contains/ContainedBy/Drives links are driven). A chassis without the
 * association omits the link entirely. The single-host system URI
 * (BMCWEB_REDFISH_SYSTEM_URI_NAME) is used as the target value.
 */
inline void getChassisComputerSystemsLink(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath)
{
    dbus::utility::getAssociationEndPoints(
        objPath + "/computer_system",
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperEndPoints& endpoints) {
            if (ec || endpoints.empty())
            {
                // No association: this chassis does not host the computer
                // system, so omit Links/ComputerSystems.
                return;
            }
            nlohmann::json::array_t computerSystems;
            nlohmann::json::object_t system;
            system["@odata.id"] = "/redfish/v1/Systems/" +
                                  std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME);
            computerSystems.emplace_back(std::move(system));
            asyncResp->res.jsonValue["Links"]["ComputerSystems"] =
                std::move(computerSystems);
        });
}

/**
 * @brief Decode a CBC tray topology hex string into raw bytes.
 *
 * @param[in] property   Hex string from the CustomField1 D-Bus property.
 * @return Decoded bytes, or std::nullopt if the input is not valid hex.
 */
inline std::optional<std::vector<uint8_t>> parseCBCTrayTopologyBytes(
    const std::string& property)
{
    std::vector<uint8_t> byteArray = hexStringToBytes(property);
    if (byteArray.size() != trayTopologyByteLength)
    {
        return std::nullopt;
    }
    return byteArray;
}

using AllowListMap = std::map<std::string, std::vector<std::string>>;

inline const std::unordered_map<std::string_view, std::string_view>
    dbusBootProgressToOSStateMap = {
        {"xyz.openbmc_project.State.Boot.Progress.ProgressStages.ResetBootROM",
         "ResetBootROM"},
        {"xyz.openbmc_project.State.Boot.Progress.ProgressStages.PrimaryProcInit",
         "FWBootStage1"},
        {"xyz.openbmc_project.State.Boot.Progress.ProgressStages.MotherboardInit",
         "FWBootStage2"},
        {"xyz.openbmc_project.State.Boot.Progress.ProgressStages.SystemInitComplete",
         "PreOS"},
        {"xyz.openbmc_project.State.Boot.Progress.ProgressStages.OSStart",
         "OSBooting"},
        {"xyz.openbmc_project.State.Boot.Progress.ProgressStages.OSRunning",
         "OSRunning"},
        {"xyz.openbmc_project.State.Boot.Progress.ProgressStages.OSQuiesced",
         "OSQuiesced"},
        {"xyz.openbmc_project.State.Boot.Progress.ProgressStages.FWUpdateInProgress",
         "FWUpdateInProgress"},
        {"xyz.openbmc_project.State.Boot.Progress.ProgressStages.OSCrashDumpInProgress",
         "OSCrashDumpInProgress"},
        {"xyz.openbmc_project.State.Boot.Progress.ProgressStages.OSCrashDumpCompleted",
         "OSCrashDumpCompleted"},
        {"xyz.openbmc_project.State.Boot.Progress.ProgressStages.FWFaultInProgress",
         "FWFaultInProgress"},
        {"xyz.openbmc_project.State.Boot.Progress.ProgressStages.FWFaultCompleted",
         "FWFaultCompleted"},
};

inline std::string getBootReasonTypes(const std::string& bootReasonType)
{
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.WakeUp")
    {
        return "WakeUp";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.PowerOn")
    {
        return "PowerOn";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.VoltageDetect")
    {
        return "VoltageDetect";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.WarmReset")
    {
        return "WarmReset";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.FatalError")
    {
        return "FatalError";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.Pin")
    {
        return "Pin";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.DebugAccessPort")
    {
        return "DebugAccessPort";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.ResetTimeout")
    {
        return "ResetTimeout";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.LowPowerAcknowledgeTimeout")
    {
        return "LowPowerAcknowledgeTimeout";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.SystemClockGenerator")
    {
        return "SystemClockGenerator";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.WindowedWatchdog0")
    {
        return "WindowedWatchdog0";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.WindowedWatchdog1")
    {
        return "WindowedWatchdog1";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.Software")
    {
        return "Software";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.LockupReset")
    {
        return "LockupReset";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.CPU1")
    {
        return "CPU1";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.VBAT")
    {
        return "VBAT";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.CodeWatchdog0")
    {
        return "CodeWatchdog0";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.CodeWatchdog1")
    {
        return "CodeWatchdog1";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.JTAG")
    {
        return "JTAG";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.SecurityViolation")
    {
        return "SecurityViolation";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.Tamper")
    {
        return "Tamper";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.IAccViol")
    {
        return "WDT_IACCVIOL";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.DAccViol")
    {
        return "WDT_DACCVIOL";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.Munstkerr")
    {
        return "WDT_MUNSTKERR";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.Mstkerr")
    {
        return "WDT_MSTKERR";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.MMFarValid")
    {
        return "WDT_MMARVALID";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.BFarValid")
    {
        return "WDT_BFARVALID";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.Stkerr")
    {
        return "WDT_STKERR";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.Unstkerr")
    {
        return "WDT_UNSTKERR";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.ImpreciseError")
    {
        return "WDT_IMPRECISEERR";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.PreciseError")
    {
        return "WDT_PRECISERR";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.IBusErr")
    {
        return "WDT_IBUSERR";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.UndefInstr")
    {
        return "WDT_UNDEFINSTR";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.InvState")
    {
        return "WDT_INVSTATE";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.InvPC")
    {
        return "WDT_INVPC";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.NoCP")
    {
        return "WDT_NOCP";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.Unaligned")
    {
        return "WDT_UNALIGNED";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.DevByZero")
    {
        return "WDT_DIVBYZERO";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.VectTbl")
    {
        return "WDT_VECTTBL";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.Forced")
    {
        return "WDT_FORCED";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.DebugEvt")
    {
        return "WDT_DEBUGEVT";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.MCTP")
    {
        return "WDT_MCTP";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.I2C")
    {
        return "WDT_I2C";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.I3C")
    {
        return "WDT_I3C";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.PLDM")
    {
        return "WDT_PLDM";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.USB")
    {
        return "WDT_USB";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.Flash")
    {
        return "WDT_Flash";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.Logger")
    {
        return "WDT_Logger";
    }
    if (bootReasonType ==
        "com.nvidia.ResetCounters.ResetCounterMetrics.BootReasonTypes.SPDM")
    {
        return "WDT_SPDM";
    }

    return "";
}

/* * @brief Fill out links association to underneath chassis by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getChassisLinksContains(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get underneath chassis links");
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_chassis",
        "xyz.openbmc_project.Association", "endpoints",
        [aResp](const boost::system::error_code& ec2,
                const std::vector<std::string>& resp) {
            if (ec2)
            {
                return; // no chassis = no failures
            }

            nlohmann::json& linksArray =
                aResp->res.jsonValue["Links"]["Contains"];
            linksArray = nlohmann::json::array();
            boost::container::flat_set<std::string> chassisNames;
            for (const std::string& chassisPath : resp)
            {
                sdbusplus::message::object_path objectPath(chassisPath);
                std::string chassisName = objectPath.filename();
                if (chassisName.empty())
                {
                    messages::internalError(aResp->res);
                    return;
                }
                chassisNames.emplace(std::move(chassisName));
            }
            for (const auto& chassisName : chassisNames)
            {
                linksArray.push_back(
                    {{"@odata.id", "/redfish/v1/Chassis/" + chassisName}});
            }
        });
}

/* * @brief Fill out links association to underneath chassis by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getChassisProcessorProtocolBridgeForDevices(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get underneath chassis links");
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/bridging_processor",
        "xyz.openbmc_project.Association", "endpoints",
        [aResp](const boost::system::error_code& ec2,
                const std::vector<std::string>& resp) {
            if (ec2)
            {
                return; // no chassis = no failures
            }

            aResp->res.jsonValue["Links"]["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaChassis.v1_7_0.NvidiaSMAChassis";
            nlohmann::json& protocalBridgeArray =
                aResp->res.jsonValue["Links"]["Oem"]["Nvidia"]
                                    ["ProtocolBridgeForDevices"];
            protocalBridgeArray = nlohmann::json::array();
            boost::container::flat_set<std::string> chassisNames;
            for (const std::string& chassisPath : resp)
            {
                sdbusplus::message::object_path objectPath(chassisPath);
                std::string chassisName = objectPath.filename();
                if (chassisName.empty())
                {
                    BMCWEB_LOG_ERROR(
                        "Empty string on chassisName for objPath:{}",
                        chassisPath);
                    messages::internalError(aResp->res);
                    return;
                }
                chassisNames.emplace(std::move(chassisName));
            }
            for (const auto& chassisName : chassisNames)
            {
                protocalBridgeArray.push_back(
                    {{"@odata.id",
                      "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/Processors/" + chassisName}});
            }
        });
}

/* * @brief Fill out links association to underneath chassis by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getChassisNetworkAdapterProtocolBridgeForDevices(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get underneath chassis links");
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/bridging_chassis",
        "xyz.openbmc_project.Association", "endpoints",
        [aResp](const boost::system::error_code& ec,
                const std::vector<std::string>& resp) {
            if (ec)
            {
                return; // no chassis = no failures
            }

            aResp->res.jsonValue["Links"]["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaChassis.v1_7_0.NvidiaSMAChassis";
            nlohmann::json& protocalBridgeArray =
                aResp->res.jsonValue["Links"]["Oem"]["Nvidia"]
                                    ["ProtocolBridgeForDevices"];
            protocalBridgeArray = nlohmann::json::array();
            for (const std::string& chassisPath : resp)
            {
                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper",
                    chassisPath + "/network_adapters",
                    "xyz.openbmc_project.Association", "endpoints",
                    [aResp, &protocalBridgeArray,
                     chassisPath](const boost::system::error_code& ec2,
                                  const std::vector<std::string>& resp2) {
                        if (ec2)
                        {
                            return; // no chassis = no failures
                        }

                        for (const std::string& networkAdapterPath : resp2)
                        {
                            sdbusplus::message::object_path objectPath(
                                networkAdapterPath);
                            std::string networkAdapterId =
                                objectPath.filename();
                            if (networkAdapterId.empty())
                            {
                                BMCWEB_LOG_ERROR(
                                    "Empty String networkAdapterId for objPath:{}",
                                    networkAdapterPath);
                                messages::internalError(aResp->res);
                                return;
                            }

                            sdbusplus::message::object_path objpath(
                                chassisPath);
                            std::string chassisId = objpath.filename();
                            if (chassisId.empty())
                            {
                                messages::internalError(aResp->res);
                                return;
                            }
                            std::string odataId = "/redfish/v1/Chassis/";
                            odataId += chassisId;
                            odataId += "/NetworkAdapters/";
                            odataId += networkAdapterId;
                            protocalBridgeArray.push_back(
                                {{"@odata.id", odataId}});
                        }
                    });
            }
        });
}

/* * @brief Fill out links association to underneath chassis by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getProtocolBridgeForDevices(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)
{
    // Links association to underneath chassis
    getChassisNetworkAdapterProtocolBridgeForDevices(aResp, objPath);
    // Links association to underneath processors
    getChassisProcessorProtocolBridgeForDevices(aResp, objPath);
}

/* * @brief Fill out links association to underneath chassis by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void populateErrorInjectionChassis(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& chassisId)
{
    dbus::utility::async_method_call(
        [aResp, chassisId, objPath](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                serviceMap) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("ErrorInjection object not found in {}",
                                 objPath);
                return;
            }

            for (const auto& [_, interfaces] : serviceMap)
            {
                if (std::find(interfaces.begin(), interfaces.end(),
                              "com.nvidia.ErrorInjection.ErrorInjection") ==
                    interfaces.end())
                {
                    continue;
                }
                aResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaChassis.v1_12_0.NvidiaSMAChassis";
                aResp->res
                    .jsonValue["Oem"]["Nvidia"]["ErrorInjection"]["@odata.id"] =
                    "/redfish/v1/Chassis/" + chassisId +
                    "/Oem/Nvidia/ErrorInjection";
                return;
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject",
        objPath + "/ErrorInjection", std::array<const char*, 0>());
}

/**
 * @brief If the chassis exposes PowerSmoothing (EnergyStorageFeatures via
 * D-Bus StateOfChargeFeatures), add the PowerSmoothing OEM link to the
 * response. Same object path as chassis.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       chassisId   Redfish chassis id.
 * @param[in]       interfaces  List of D-Bus interfaces on the chassis
 * object.
 */
inline void populatePowerSmoothingChassisIfPresent(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& chassisId, const std::vector<std::string>& interfaces)
{
    constexpr std::string_view stateOfChargeFeaturesIface =
        "com.nvidia.PowerSmoothing.StateOfChargeFeatures";
    if (std::find(interfaces.begin(), interfaces.end(),
                  stateOfChargeFeaturesIface) == interfaces.end())
    {
        return;
    }
    aResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
        "#NvidiaChassis.v1_12_0.NvidiaSMAChassis";
    aResp->res.jsonValue["Oem"]["Nvidia"]["PowerSmoothing"]["@odata.id"] =
        "/redfish/v1/Chassis/" + chassisId + "/Oem/Nvidia/PowerSmoothing";
}

inline void getBootReasonProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& resetObjPath)
{
    using PropertiesMap = boost::container::flat_map<
        std::string, std::variant<std::vector<std::string>, double>>;

    dbus::utility::async_method_call(
        [aResp](const boost::system::error_code& ec,
                const PropertiesMap& propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "Dbus error when getting boot reason properties {}",
                    ec.message());
                messages::internalError(aResp->res);
                return;
            }
            if (propertiesList.empty())
            {
                BMCWEB_LOG_ERROR("No property found.");
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json& lastResetReasonsArray =
                aResp->res.jsonValue["Oem"]["Nvidia"]["LastResetReasons"];
            lastResetReasonsArray = nlohmann::json::array();
            for (const auto& property : propertiesList)
            {
                if (property.first == "BootReason")
                {
                    const std::vector<std::string>* value =
                        std::get_if<std::vector<std::string>>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "Boot Reason");
                        messages::internalError(aResp->res);
                        return;
                    }
                    for (const std::string& bootReason : *value)
                    {
                        lastResetReasonsArray.emplace_back(
                            getBootReasonTypes(bootReason));
                    }
                }
            }
        },
        service, resetObjPath, "org.freedesktop.DBus.Properties", "GetAll", "");
}

inline void getResetCounterMetricsObject(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& resetObjPath)
{
    dbus::utility::async_method_call(
        [aResp, resetObjPath](
            const boost::system::error_code& ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objects) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "Dbus error when getting reset counter metrics object {}",
                    ec.message());
                messages::internalError(aResp->res);
                return;
            }
            if (objects.empty())
            {
                BMCWEB_LOG_ERROR(
                    "Dbus error when getting reset counter metrics object");
                messages::internalError(aResp->res);
                return;
            }

            getBootReasonProperties(aResp, objects[0].first, resetObjPath);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", resetObjPath,
        std::array<std::string, 1>(
            {"com.nvidia.ResetCounters.ResetCounterMetrics"}));
}

/* * @brief Fill out reset statistics on chassis by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getResetStatistics(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get reset statistics on chassis");
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/reset_statistics",
        "xyz.openbmc_project.Association", "endpoints",
        [aResp](const boost::system::error_code& ec,
                const std::vector<std::string>& resp) {
            if (ec)
            {
                return; // no association = no failures
            }

            aResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaChassis.v1_10_0.NvidiaSMAChassis";

            for (const std::string& resetObjPath : resp)
            {
                getResetCounterMetricsObject(aResp, resetObjPath);
            }
        });
}

inline void getHealthByAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath, const std::string& association,
    const std::string& objId)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/" + association,
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, objId](const boost::system::error_code& ec,
                           const std::vector<std::string>& resp) {
            if (ec)
            {
                // no state sensors attached.
                return;
            }

            for (const std::string& sensorPath : resp)
            {
                if (!sensorPath.ends_with(objId))
                {
                    continue;
                }
                // Check Interface in Object or not
                dbus::utility::async_method_call(
                    [asyncResp, sensorPath](
                        const boost::system::error_code& ec2,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& object) {
                        if (ec2)
                        {
                            // the path does not implement Decorator Health
                            // interfaces
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        using PropertiesMap = boost::container::flat_map<
                            std::string, std::variant<std::string, size_t>>;
                        // Get interface properties
                        dbus::utility::async_method_call(
                            [asyncResp,
                             sensorPath](const boost::system::error_code& ec3,
                                         const PropertiesMap& properties) {
                                if (ec3)
                                {
                                    messages::internalError(asyncResp->res);
                                    return;
                                }

                                for (const auto& property : properties)
                                {
                                    const std::string& propertyName =
                                        property.first;
                                    if (propertyName == "Health")
                                    {
                                        const std::string* value =
                                            std::get_if<std::string>(
                                                &property.second);
                                        if (value == nullptr)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Null value returned Health");
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }
                                        asyncResp->res
                                            .jsonValue["Status"]["State"] =
                                            "Enabled";

                                        if (*value ==
                                            "xyz.openbmc_project.State.Decorator.Health.HealthType.OK")
                                        {
                                            asyncResp->res
                                                .jsonValue["Status"]["Health"] =
                                                "OK";
                                        }
                                        else if (
                                            *value ==
                                            "xyz.openbmc_project.State.Decorator.Health.HealthType.Warning")
                                        {
                                            asyncResp->res
                                                .jsonValue["Status"]["Health"] =
                                                "Warning";
                                        }
                                        else if (
                                            *value ==
                                            "xyz.openbmc_project.State.Decorator.Health.HealthType.Critical")
                                        {
                                            asyncResp->res
                                                .jsonValue["Status"]["Health"] =
                                                "Critical";
                                        }
                                        else
                                        {
                                            asyncResp->res
                                                .jsonValue["Status"]["Health"] =
                                                "";
                                        }
                                    }
                                }
                            },
                            object.front().first, sensorPath,
                            "org.freedesktop.DBus.Properties", "GetAll",
                            "xyz.openbmc_project.State.Decorator.Health");
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetObject", sensorPath,
                    std::array<std::string, 1>(
                        {"xyz.openbmc_project.State.Decorator.Health"}));
            }
        });
}

/**
 * @brief Fill out processor links association by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getChassisProcessorLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get underneath processor links");
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_processors",
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
 * @brief Fill out fabric switches links association by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getChassisFabricSwitchesLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get fabric switches links");
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath + "/fabrics",
        "xyz.openbmc_project.Association", "endpoints",
        [aResp, objPath](const boost::system::error_code& ec2,
                         const std::vector<std::string>& resp) {
            if (ec2)
            {
                return; // no fabric = no failures
            }

            if (resp.size() > 1)
            {
                // There must be single fabric
                return;
            }
            const std::string& fabricPath = resp.front();
            sdbusplus::message::object_path objectPath(fabricPath);
            std::string fabricId = objectPath.filename();
            if (fabricId.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            // Get the switches
            dbus::utility::getProperty<std::vector<std::string>>(
                "xyz.openbmc_project.ObjectMapper", objPath + "/all_switches",
                "xyz.openbmc_project.Association", "endpoints",
                [aResp, fabricId](const boost::system::error_code& ec1,
                                  const std::vector<std::string>& resp1) {
                    if (ec1)
                    {
                        return; // no switches = no failures
                    }
                    // Sort the switches links
                    std::vector<std::string> sortedData(resp1);
                    std::sort(sortedData.begin(), sortedData.end());
                    nlohmann::json& linksArray =
                        aResp->res.jsonValue["Links"]["Switches"];
                    linksArray = nlohmann::json::array();
                    for (const std::string& switchPath : sortedData)
                    {
                        sdbusplus::message::object_path objectPath1(switchPath);
                        std::string switchId = objectPath1.filename();
                        if (switchId.empty())
                        {
                            messages::internalError(aResp->res);
                            return;
                        }
                        linksArray.push_back(
                            {{"@odata.id", std::string("/redfish/v1/Fabrics/")
                                               .append(fabricId)
                                               .append("/Switches/")
                                               .append(switchId)}});
                    }
                });
        });
}

/**
 * @brief Fill out chassis nvidia specific info by
 * requesting data from the associated D-Bus object.
 *
 * @param[in,out]   asyncResp      Async HTTP response.
 * @param[in]       connectionName D-Bus service to query.
 * @param[in]       path           D-Bus object path to query.
 */
inline void getOemCBCChassisAsset(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path)
{
    dbus::utility::getProperty<std::string>(
        connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.VendorInformation",
        "CustomField1",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& property) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error for CBC Tray IDs");
                messages::internalError(asyncResp->res);
                return;
            }

            // CBC FRU spec specifies that it is 8 bytes (string length 16)
            if (property.length() != trayTopologyStringLength)
            {
                BMCWEB_LOG_ERROR("CBC Tray ID string len is in invalid");
                messages::internalError(asyncResp->res);
                return;
            }

            std::optional<std::vector<uint8_t>> parsedBytes =
                parseCBCTrayTopologyBytes(property);
            if (!parsedBytes)
            {
                BMCWEB_LOG_ERROR("CBC Tray ID byte parse failed");
                messages::internalError(asyncResp->res);
                return;
            }
            const std::vector<uint8_t>& byteArray = *parsedBytes;

            // Safely copy into a TrayTopology struct
            TrayTopology trayTopology{};
            if (sizeof(trayTopology) > byteArray.size())
            {
                BMCWEB_LOG_ERROR(
                    "CBC Tray ID data is shorter than TrayTopology size");
                messages::internalError(asyncResp->res);
                return;
            }
            std::memcpy(&trayTopology, byteArray.data(), sizeof(trayTopology));

            // make sure it can support trayTopologyMinRevision at least
            if (trayTopology.revision < trayTopologyMinRevision)
            {
                BMCWEB_LOG_ERROR("CBC Tray ID revision must be >= {}",
                                 static_cast<int>(trayTopologyMinRevision));
                return;
            }

            auto& oem = asyncResp->res.jsonValue["Oem"]["Nvidia"];
            oem["@odata.type"] = "#NvidiaChassis.v1_4_0.NvidiaCBCChassis";
            oem["ChassisPhysicalSlotNumber"] = trayTopology.chassisSlotNumber;
            oem["ComputeTrayIndex"] = trayTopology.trayIndex;
            oem["RevisionId"] = trayTopology.revision;
            oem["TopologyId"] = trayTopology.topologyId;
        });
}

/**
 * @brief Fill out chassis nvidia specific info by
 * requesting data from the associated D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getOemBaseboardChassisAssert(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)

{
    BMCWEB_LOG_DEBUG("Get chassis OEM info");
    dbus::utility::findAssociations(
        objPath + "/associated_fru",
        [aResp](const boost::system::error_code& ec,
                const std::vector<std::string>& assoc) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Cannot get association");
                return;
            }
            const std::string& fruPath = assoc.front();
            dbus::utility::async_method_call(
                [aResp{aResp},
                 fruPath](const boost::system::error_code& ec2,
                          const std::vector<
                              std::pair<std::string, std::vector<std::string>>>&
                              objects) {
                    if (ec2 || objects.empty())
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for serial number");
                        messages::internalError(aResp->res);
                        return;
                    }
                    const std::string& fruObject = objects[0].first;
                    dbus::utility::async_method_call(
                        [aResp{aResp}](
                            const boost::system::error_code& ec3,
                            const std::vector<std::pair<
                                std::string,
                                std::variant<std::string, bool, uint64_t>>>&
                                propertiesList) {
                            if (ec3 || propertiesList.empty())
                            {
                                messages::internalError(aResp->res);
                                return;
                            }
                            for (const auto& property : propertiesList)
                            {
                                if (property.first == "CHASSIS_PART_NUMBER")
                                {
                                    const std::string* value =
                                        std::get_if<std::string>(
                                            &property.second);
                                    if (value == nullptr)
                                    {
                                        BMCWEB_LOG_DEBUG("Null value returned "
                                                         "Part number");
                                        messages::internalError(aResp->res);
                                        return;
                                    }
                                    aResp->res.jsonValue["Oem"]["Nvidia"]
                                                        ["PartNumber"] = *value;
                                }
                                else if (property.first ==
                                         "CHASSIS_SERIAL_NUMBER")
                                {
                                    const std::string* value =
                                        std::get_if<std::string>(
                                            &property.second);
                                    if (value == nullptr)
                                    {
                                        BMCWEB_LOG_DEBUG("Null value returned "
                                                         "for serial number");
                                        messages::internalError(aResp->res);
                                        return;
                                    }
                                    aResp->res.jsonValue["Oem"]["Nvidia"]
                                                        ["SerialNumber"] =
                                        *value;
                                }
                            }
                        },
                        fruObject, fruPath, "org.freedesktop.DBus.Properties",
                        "GetAll", "xyz.openbmc_project.FruDevice");
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetObject", fruPath,
                std::array<std::string, 1>({"xyz.openbmc_project.FruDevice"}));
        });
}

/**
 * @brief Write chassis nvidia specific info to eeprom by
 * setting data to the associated Fru D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void setOemBaseboardChassisAssert(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& prop, const std::string& value)
{
    BMCWEB_LOG_DEBUG("Set chassis OEM info: ");
    dbus::utility::findAssociations(
        objPath + "/associated_fru",
        [aResp, prop, value](const boost::system::error_code& ec,
                             const std::vector<std::string>& assoc) {
            if (ec)
            {
                messages::internalError(aResp->res);
                return;
            }
            const std::string& fruPath = assoc.front();
            dbus::utility::async_method_call(
                [aResp{aResp}, fruPath, prop,
                 value](const boost::system::error_code& ec4,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& objects) {
                    if (ec4 || objects.empty())
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    const std::string& fruObject = objects[0].first;
                    if (prop == "PartNumber")
                    {
                        dbus::utility::async_method_call(
                            [aResp](const boost::system::error_code& ec5) {
                                if (ec5)
                                {
                                    BMCWEB_LOG_DEBUG(
                                        "DBUS response error: Set CHASSIS_PART_NUMBER{}",
                                        ec5);
                                    messages::internalError(aResp->res);
                                    return;
                                }
                                messages::success(aResp->res);
                                BMCWEB_LOG_DEBUG(
                                    "Set CHASSIS_PART_NUMBER done.");
                            },
                            fruObject, fruPath,
                            "org.freedesktop.DBus.Properties", "Set",
                            "xyz.openbmc_project.FruDevice",
                            "CHASSIS_PART_NUMBER",
                            dbus::utility::DbusVariantType(value));
                    }
                    else if (prop == "SerialNumber")
                    {
                        dbus::utility::async_method_call(
                            [aResp](const boost::system::error_code& ec6) {
                                if (ec6)
                                {
                                    BMCWEB_LOG_DEBUG(
                                        "DBUS response error: Set CHASSIS_SERIAL_NUMBER{}",
                                        ec6);
                                    messages::internalError(aResp->res);
                                    return;
                                }
                                messages::success(aResp->res);
                                BMCWEB_LOG_DEBUG(
                                    "Set CHASSIS_SERIAL_NUMBER done.");
                            },
                            fruObject, fruPath,
                            "org.freedesktop.DBus.Properties", "Set",
                            "xyz.openbmc_project.FruDevice",
                            "CHASSIS_SERIAL_NUMBER",
                            dbus::utility::DbusVariantType(value));
                    }
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetObject", fruPath,
                std::array<std::string, 1>({"xyz.openbmc_project.FruDevice"}));
        });
}

/**
 * @brief Fill out nvidia assembly specific info by
 * requesting data from the associated D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       assemblyId  Assembly ID to query and update.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getOemAssemblyAssert(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& assemblyId, const std::string& objPath)

{
    BMCWEB_LOG_DEBUG("Get assembly OEM info");
    /*
     * FRU device objects in dbus are associated with assemblies
     * dbus object. Here is to find the associated FRU device object
     * and then get the OEM information from the FRU device.
     */
    dbus::utility::findAssociations(
        objPath + "/associated_fru",
        [aResp, assemblyId](const boost::system::error_code& ec,
                            const std::vector<std::string>& assoc) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Cannot get association");
                return;
            }
            const std::string& fruPath = assoc.front();
            dbus::utility::async_method_call(
                [aResp{aResp}, fruPath, assemblyId](
                    const boost::system::error_code& ec1,
                    const std::vector<std::pair<
                        std::string, std::vector<std::string>>>& objects) {
                    if (ec1 || objects.empty())
                    {
                        BMCWEB_LOG_DEBUG("Cannpt get object");
                        messages::internalError(aResp->res);
                        return;
                    }
                    const std::string& fruObject = objects[0].first;
                    dbus::utility::async_method_call(
                        [aResp{aResp}, assemblyId](
                            const boost::system::error_code& ec2,
                            const std::vector<std::pair<
                                std::string,
                                std::variant<std::string, bool, uint64_t>>>&
                                propertiesList) {
                            if (ec2 || propertiesList.empty())
                            {
                                messages::internalError(aResp->res);
                                return;
                            }
                            for (auto& assembly :
                                 aResp->res.jsonValue["Assemblies"])
                            {
                                if (assembly["MemberId"] == assemblyId)
                                {
                                    assembly["Oem"]["Nvidia"]["@odata.type"] =
                                        "#NvidiaAssembly.v1_0_0.NvidiaAssembly";
                                    nlohmann::json& vendorDataArray =
                                        assembly["Oem"]["Nvidia"]["VendorData"];
                                    vendorDataArray = nlohmann::json::array();
                                    for (const auto& property : propertiesList)
                                    {
                                        if (property.first.find(
                                                "BOARD_INFO_AM") !=
                                                std::string::npos &&
                                            assembly["PhysicalContext"] ==
                                                "Board")
                                        {
                                            const std::string* value =
                                                std::get_if<std::string>(
                                                    &property.second);
                                            if (value == nullptr)
                                            {
                                                BMCWEB_LOG_DEBUG(
                                                    "Null value returned "
                                                    "Board Extra");
                                                messages::internalError(
                                                    aResp->res);
                                                return;
                                            }
                                            vendorDataArray.emplace_back(
                                                *value);
                                        }
                                        else if (property.first.find(
                                                     "PRODUCT_INFO_AM") !=
                                                     std::string::npos &&
                                                 assembly["PhysicalContext"] ==
                                                     "SystemBoard")
                                        {
                                            const std::string* value =
                                                std::get_if<std::string>(
                                                    &property.second);
                                            if (value == nullptr)
                                            {
                                                BMCWEB_LOG_DEBUG(
                                                    "Null value returned "
                                                    "Product Extra");
                                                messages::internalError(
                                                    aResp->res);
                                                return;
                                            }
                                            vendorDataArray.emplace_back(
                                                *value);
                                        }
                                        else if (property.first.find(
                                                     "CHASSIS_INFO_AM") !=
                                                     std::string::npos &&
                                                 assembly["PhysicalContext"] ==
                                                     "Chassis")
                                        {
                                            const std::string* value =
                                                std::get_if<std::string>(
                                                    &property.second);
                                            if (value == nullptr)
                                            {
                                                BMCWEB_LOG_DEBUG(
                                                    "Null value returned "
                                                    "Product Extra");
                                                messages::internalError(
                                                    aResp->res);
                                                return;
                                            }
                                            vendorDataArray.emplace_back(
                                                *value);
                                        }
                                    }
                                }
                            }
                        },
                        fruObject, fruPath, "org.freedesktop.DBus.Properties",
                        "GetAll", "xyz.openbmc_project.FruDevice");
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetObject", fruPath,
                std::array<std::string, 1>({"xyz.openbmc_project.FruDevice"}));
        });
}

/**
 * @brief Fill out chassis nvidia specific info by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getOemPCIeDeviceClockReferenceInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Baseboard PCIeReference clock count");
    dbus::utility::async_method_call(
        [aResp](const boost::system::error_code& ec,
                const std::vector<std::pair<
                    std::string, std::variant<std::string, bool, uint64_t>>>&
                    propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for "
                                 "Baseboard PCIeReference clock count");
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& property : propertiesList)
            {
                if (property.first == "PCIeReferenceClockCount")
                {
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for pcie refernce clock count");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["Oem"]["Nvidia"][property.first] =
                        *value;
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Inventory.Decorator.PCIeRefClock");
}

/**
 * @brief Fill out chassis power limits info of a chassis by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getChassisPowerLimits(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get chassis power limits");
    dbus::utility::async_method_call(
        [aResp](const boost::system::error_code& ec,
                const std::vector<std::pair<std::string, std::variant<size_t>>>&
                    propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for "
                                 "Chassis power limits");
                messages::internalError(aResp->res);
                return;
            }
            for (const std::pair<std::string, std::variant<size_t>>& property :
                 propertiesList)
            {
                const std::string& propertyName = property.first;
                if ((propertyName == "MinPowerWatts") ||
                    (propertyName == "MaxPowerWatts"))
                {
                    const size_t* value = std::get_if<size_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for power limits");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue[propertyName] = *value;
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Inventory.Decorator.PowerLimit");
}

inline void setStaticPowerHintByObjPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath, double cpuClockFrequency, double workloadFactor,
    double temperature, uint32_t numberOfCores)
{
    dbus::utility::async_method_call(
        [asyncResp, objPath, cpuClockFrequency, workloadFactor, temperature,
         numberOfCores](
            const boost::system::error_code& errorno,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
            if (errorno)
            {
                return;
            }

            for (const auto& [service, interfaces] : objInfo)
            {
                dbus::utility::async_method_call(
                    [asyncResp, objPath, service{service}, cpuClockFrequency,
                     workloadFactor, temperature, numberOfCores](
                        const boost::system::error_code& errorno2,
                        const std::vector<std::pair<
                            std::string,
                            std::variant<double, uint32_t, std::string, bool>>>&
                            propertiesList) {
                        if (errorno2)
                        {
                            BMCWEB_LOG_ERROR(
                                "Properties::GetAll failed:{}objPath:{}",
                                errorno2, objPath);
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        double cpuClockFrequencyMax = 0;
                        double cpuClockFrequencyMin = 0;
                        double workloadFactorMax = 0;
                        double workloadFactorMin = 0;
                        double temperatureMax = 0;
                        double temperatureMin = 0;
                        uint32_t numberOfCoresMax = 0;
                        uint32_t numberOfCoresMin = 0;

                        for (const auto& [propertyName, value] : propertiesList)
                        {
                            if (propertyName == "MaxCpuClockFrequency" &&
                                std::holds_alternative<double>(value))
                            {
                                cpuClockFrequencyMax = std::get<double>(value);
                            }
                            else if (propertyName == "MinCpuClockFrequency" &&
                                     std::holds_alternative<double>(value))
                            {
                                cpuClockFrequencyMin = std::get<double>(value);
                            }
                            else if (propertyName == "MaxTemperature" &&
                                     std::holds_alternative<double>(value))
                            {
                                temperatureMax = std::get<double>(value);
                            }
                            else if (propertyName == "MinTemperature" &&
                                     std::holds_alternative<double>(value))
                            {
                                temperatureMin = std::get<double>(value);
                            }
                            else if (propertyName == "MaxWorkloadFactor" &&
                                     std::holds_alternative<double>(value))
                            {
                                workloadFactorMax = std::get<double>(value);
                            }
                            else if (propertyName == "MinWorkloadFactor" &&
                                     std::holds_alternative<double>(value))
                            {
                                workloadFactorMin = std::get<double>(value);
                            }
                            else if (propertyName == "MaxNumberOfCores" &&
                                     std::holds_alternative<uint32_t>(value))
                            {
                                numberOfCoresMax = std::get<uint32_t>(value);
                            }
                            else if (propertyName == "MinNumberOfCores" &&
                                     std::holds_alternative<uint32_t>(value))
                            {
                                numberOfCoresMin = std::get<uint32_t>(value);
                            }
                        }

                        if ((cpuClockFrequencyMax < cpuClockFrequency) ||
                            (cpuClockFrequencyMin > cpuClockFrequency))
                        {
                            messages::propertyValueOutOfRange(
                                asyncResp->res,
                                std::to_string(cpuClockFrequency),
                                "CpuClockFrequency");
                            return;
                        }

                        if ((temperatureMax < temperature) ||
                            (temperatureMin > temperature))
                        {
                            messages::propertyValueOutOfRange(
                                asyncResp->res, std::to_string(temperature),
                                "Temperature");
                            return;
                        }

                        if ((workloadFactorMax < workloadFactor) ||
                            (workloadFactorMin > workloadFactor))
                        {
                            messages::propertyValueOutOfRange(
                                asyncResp->res, std::to_string(workloadFactor),
                                "WorkloadFactor");
                            return;
                        }

                        // Only range-check NumberOfCores when the device
                        // actually exposes the effecter. When the effecter
                        // is absent the PLDM side reports max=min=0; ignore
                        // the argument entirely so legacy devices keep
                        // working with any value the client sends.
                        if ((numberOfCoresMax != 0 || numberOfCoresMin != 0) &&
                            ((numberOfCoresMax < numberOfCores) ||
                             (numberOfCoresMin > numberOfCores)))
                        {
                            messages::propertyValueOutOfRange(
                                asyncResp->res, std::to_string(numberOfCores),
                                "NumberOfCores");
                            return;
                        }

                        dbus::utility::async_method_call(
                            [asyncResp, objPath](
                                const boost::system::error_code& errorno3) {
                                if (errorno3)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "StaticPowerHint::Estimate failed:{}",
                                        errorno3);
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                            },
                            service, objPath, "com.nvidia.StaticPowerHint",
                            "EstimatePower", cpuClockFrequency, workloadFactor,
                            temperature, numberOfCores);
                    },
                    service, objPath, "org.freedesktop.DBus.Properties",
                    "GetAll", "com.nvidia.StaticPowerHint");
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
        std::array<const char*, 1>{"com.nvidia.StaticPowerHint"});
}

inline void setStaticPowerHintByChassis(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisObjPath, double cpuClockFrequency,
    double workloadFactor, double temperature, uint32_t numberOfCores)
{
    // get endpoints of chassisId/all_controls
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", chassisObjPath + "/all_controls",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, chassisObjPath, cpuClockFrequency, workloadFactor,
         temperature, numberOfCores](const boost::system::error_code&,
                                     const std::vector<std::string>& resp) {
            for (const auto& objPath : resp)
            {
                setStaticPowerHintByObjPath(asyncResp, objPath,
                                            cpuClockFrequency, workloadFactor,
                                            temperature, numberOfCores);
            }
        });
}

inline void getStaticPowerHintByObjPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath)
{
    dbus::utility::async_method_call(
        [asyncResp, objPath](
            const boost::system::error_code& errorno,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
            if (errorno)
            {
                return;
            }

            for (const auto& [service, interfaces] : objInfo)
            {
                dbus::utility::async_method_call(
                    [asyncResp, objPath](
                        const boost::system::error_code& errorno4,
                        const std::vector<std::pair<
                            std::string,
                            std::variant<double, uint32_t, std::string, bool>>>&
                            propertiesList) {
                        if (errorno4)
                        {
                            BMCWEB_LOG_ERROR(
                                "Properties::GetAll failed:{}objPath:{}",
                                errorno4, objPath);
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        nlohmann::json& staticPowerHint =
                            asyncResp->res
                                .jsonValue["Oem"]["Nvidia"]["StaticPowerHint"];
                        for (const auto& [propertyName, value] : propertiesList)
                        {
                            if (propertyName == "MaxCpuClockFrequency" &&
                                std::holds_alternative<double>(value))
                            {
                                staticPowerHint["CpuClockFrequencyHz"]
                                               ["AllowableMax"] =
                                                   std::get<double>(value);
                            }
                            else if (propertyName == "MinCpuClockFrequency" &&
                                     std::holds_alternative<double>(value))
                            {
                                staticPowerHint["CpuClockFrequencyHz"]
                                               ["AllowableMin"] =
                                                   std::get<double>(value);
                            }
                            else if (propertyName == "CpuClockFrequency" &&
                                     std::holds_alternative<double>(value))
                            {
                                staticPowerHint["CpuClockFrequencyHz"]
                                               ["SetPoint"] =
                                                   std::get<double>(value);
                            }
                            else if (propertyName == "MaxTemperature" &&
                                     std::holds_alternative<double>(value))
                            {
                                staticPowerHint["TemperatureCelsius"]
                                               ["AllowableMax"] =
                                                   std::get<double>(value);
                            }
                            else if (propertyName == "MinTemperature" &&
                                     std::holds_alternative<double>(value))
                            {
                                staticPowerHint["TemperatureCelsius"]
                                               ["AllowableMin"] =
                                                   std::get<double>(value);
                            }
                            else if (propertyName == "Temperature" &&
                                     std::holds_alternative<double>(value))
                            {
                                staticPowerHint["TemperatureCelsius"]
                                               ["SetPoint"] =
                                                   std::get<double>(value);
                            }
                            else if (propertyName == "MaxWorkloadFactor" &&
                                     std::holds_alternative<double>(value))
                            {
                                staticPowerHint["WorkloadFactor"]
                                               ["AllowableMax"] =
                                                   std::get<double>(value);
                            }
                            else if (propertyName == "MinWorkloadFactor" &&
                                     std::holds_alternative<double>(value))
                            {
                                staticPowerHint["WorkloadFactor"]
                                               ["AllowableMin"] =
                                                   std::get<double>(value);
                            }
                            else if (propertyName == "WorkloadFactor" &&
                                     std::holds_alternative<double>(value))
                            {
                                staticPowerHint["WorkloadFactor"]["SetPoint"] =
                                    std::get<double>(value);
                            }
                            else if (propertyName == "MaxNumberOfCores" &&
                                     std::holds_alternative<uint32_t>(value))
                            {
                                staticPowerHint["NumberOfCores"]
                                               ["AllowableMax"] =
                                                   std::get<uint32_t>(value);
                            }
                            else if (propertyName == "MinNumberOfCores" &&
                                     std::holds_alternative<uint32_t>(value))
                            {
                                staticPowerHint["NumberOfCores"]
                                               ["AllowableMin"] =
                                                   std::get<uint32_t>(value);
                            }
                            else if (propertyName == "NumberOfCores" &&
                                     std::holds_alternative<uint32_t>(value))
                            {
                                staticPowerHint["NumberOfCores"]["SetPoint"] =
                                    std::get<uint32_t>(value);
                            }
                            else if (propertyName == "PowerEstimate" &&
                                     std::holds_alternative<double>(value))
                            {
                                staticPowerHint["PowerEstimationWatts"]
                                               ["Reading"] =
                                                   std::get<double>(value);
                            }
                            else if (propertyName ==
                                         "StateOfLastEstimatePower" &&
                                     std::holds_alternative<std::string>(value))
                            {
                                staticPowerHint
                                    ["PowerEstimationWatts"]["State"] =
                                        ::redfish::chassis_utils::
                                            getStateOfEstimatePowerMethod(
                                                std::get<std::string>(value));
                            }
                        }
                    },
                    service, objPath, "org.freedesktop.DBus.Properties",
                    "GetAll", "com.nvidia.StaticPowerHint");
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
        std::array<const char*, 1>{"com.nvidia.StaticPowerHint"});
}

inline void getStaticPowerHintByChassis(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisObjPath)
{
    // get endpoints of chassisId/all_controls
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", chassisObjPath + "/all_controls",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, chassisObjPath](const boost::system::error_code&,
                                    const std::vector<std::string>& resp) {
            for (const auto& objPath : resp)
            {
                getStaticPowerHintByObjPath(asyncResp, objPath);
            }
        });
}

inline void maybePopulateStaticPowerHint(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path, const std::vector<std::string>& interfaces)
{
    const auto shouldFetch = std::any_of(
        interfaces.begin(), interfaces.end(), [](std::string_view iface) {
            return iface == "xyz.openbmc_project.Inventory.Item.System" ||
                   iface == "xyz.openbmc_project.Inventory.Item.Chassis";
        });

    if (shouldFetch)
    {
        getStaticPowerHintByChassis(asyncResp, path);
    }
}

inline void getNetworkAdapters(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath, const std::vector<std::string>& interfaces,
    const std::string& chassisId)
{
    // NetworkAdapters collection
    const std::string networkInterface =
        "xyz.openbmc_project.Inventory.Item.NetworkInterface";
    if (std::find(interfaces.begin(), interfaces.end(), networkInterface) !=
        interfaces.end())
    {
        // networkInterface at the same chassis objPath
        asyncResp->res.jsonValue["NetworkAdapters"] = {
            {"@odata.id",
             "/redfish/v1/Chassis/" + chassisId + "/NetworkAdapters"}};

        return;
    }

    const std::array<std::string_view, 1> networkInterfaces = {
        "xyz.openbmc_project.Inventory.Item.NetworkInterface"};

    dbus::utility::getSubTree(
        objPath, 0, networkInterfaces,
        [asyncResp, chassisId(std::string(chassisId))](
            const boost::system::error_code& ec,
            const dbus::utility::GetSubTreeType& subtree) {
            if (ec)
            {
                return;
            }

            if (subtree.empty())
            {
                return;
            }
            asyncResp->res.jsonValue["NetworkAdapters"] = {
                {"@odata.id",
                 "/redfish/v1/Chassis/" + chassisId + "/NetworkAdapters"}};
        });
}

/**
 * @brief Fill out chassis physical dimensions info by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getChassisDimensions(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get chassis dimensions");
    dbus::utility::async_method_call(
        [aResp](const boost::system::error_code& ec,
                const std::vector<
                    std::pair<std::string, dbus::utility::DbusVariantType>>&
                    propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for "
                                 "Chassis dimensions");
                messages::internalError(aResp->res);
                return;
            }
            for (const std::pair<std::string, dbus::utility::DbusVariantType>&
                     property : propertiesList)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "Height")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for Height");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["HeightMm"] = *value;
                }
                else if (propertyName == "Width")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for Width");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["WidthMm"] = *value;
                }
                else if (propertyName == "Depth")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for Depth");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["DepthMm"] = *value;
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Inventory.Decorator.Dimension");
}

inline void getChassisWriteProtectProtectEnable(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec1, const std::string& chassisId,
    const dbus::utility::MapperGetObject& object)
{
    if (ec1)
    {
        BMCWEB_LOG_INFO(
            "No ChassisWP Dbus Object, Skip 'HardwareWriteProtectEnable'");
        return;
    }
    dbus::utility::getProperty<bool>(
        object[0].first,
        sdbusplus::message::object_path("/xyz/openbmc_project/software") /=
        chassisId,
        "xyz.openbmc_project.Software.Settings", "WriteProtected",
        [asyncResp](const boost::system::error_code& ec2, bool property) {
            if (ec2.value() ==
                boost::system::linux_error::bad_request_descriptor)
            {
                BMCWEB_LOG_ERROR("WriteProtected property is not found");
                messages::resourceNotFound(
                    asyncResp->res, "WriteProtected property is not found", "");
                return;
            }
            if (ec2)
            {
                BMCWEB_LOG_ERROR("getProperty WriteProtected error");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["HardwareWriteProtectEnable"] =
                property;
        });
}

inline void afterGetHostNetworkEnabled(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, bool property)
{
    if (ec.value() == boost::system::linux_error::bad_request_descriptor)
    {
        BMCWEB_LOG_ERROR("Enabled property is not found");
        messages::resourceNotFound(asyncResp->res,
                                   "Enabled property is not found", "");
        return;
    }
    if (ec)
    {
        BMCWEB_LOG_ERROR("getProperty Enabled error");
        messages::internalError(asyncResp->res);
        return;
    }
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["HostManagementNetworkAccess"] =
        property;
}

inline void getChassisHostNetworkEnable(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath, const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec)
    {
        BMCWEB_LOG_INFO(
            "No Chassis Dbus Object, Skip 'HostManagementNetworkAccess'");
        return;
    }
    dbus::utility::getProperty<bool>(
        object[0].first, objPath, "xyz.openbmc_project.Object.Enable",
        "Enabled", std::bind_front(afterGetHostNetworkEnabled, asyncResp));
}

inline void afterGetHostNetworkAccessEndpoints(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& endpoints)
{
    if (ec || endpoints.empty())
    {
        // Chassis does not own the host-NIC control; omit the property.
        return;
    }
    dbus::utility::getDbusObject(
        endpoints[0],
        std::array<std::string_view, 1>{"xyz.openbmc_project.Object.Enable"},
        std::bind_front(getChassisHostNetworkEnable, asyncResp, endpoints[0]));
}

inline void setChassisWriteProtectProtectEnable(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec1, const std::string& chassisId,
    const dbus::utility::MapperGetObject& object, const bool value)
{
    if (ec1 == boost::system::errc::io_error)
    {
        BMCWEB_LOG_ERROR("ChassisWP: {}, Interface is not found", chassisId);
        messages::resourceNotFound(asyncResp->res, chassisId,
                                   "Interface is not found");
        return;
    }
    if (ec1)
    {
        BMCWEB_LOG_ERROR("getDbusObject: {}, error: {}", chassisId, ec1);
        messages::internalError(asyncResp->res);
        return;
    }
    dbus::utility::setProperty(
        object[0].first,
        sdbusplus::message::object_path("/xyz/openbmc_project/software") /=
        chassisId,
        "xyz.openbmc_project.Software.Settings", "WriteProtected", value,
        [asyncResp](const boost::system::error_code& ec2) {
            if (ec2.value() ==
                boost::system::linux_error::bad_request_descriptor)
            {
                BMCWEB_LOG_ERROR("WriteProtected property is not found");
                messages::resourceNotFound(
                    asyncResp->res, "WriteProtected property is not found", "");
                return;
            }
            if (ec2)
            {
                BMCWEB_LOG_ERROR("setProperty WriteProtected error");
                messages::internalError(asyncResp->res);
                return;
            }
        });
}

inline void afterSetHostNetworkEnabled(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec)
{
    if (ec.value() == boost::system::linux_error::bad_request_descriptor)
    {
        BMCWEB_LOG_ERROR("Enabled property is not found");
        messages::resourceNotFound(asyncResp->res,
                                   "Enabled property is not found", "");
        return;
    }
    if (ec)
    {
        BMCWEB_LOG_ERROR("setProperty Enabled error");
        messages::internalError(asyncResp->res);
        return;
    }
}

inline void setChassisHostNetworkEnable(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath, const bool value,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec == boost::system::errc::io_error)
    {
        BMCWEB_LOG_ERROR("Chassis Interface is not found");
        messages::resourceNotFound(asyncResp->res,
                                   "HostManagementNetworkAccess",
                                   "Interface is not found");
        return;
    }
    if (ec)
    {
        BMCWEB_LOG_ERROR("getDbusObject error: {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }
    std::function<void(const boost::system::error_code&)> handler =
        std::bind_front(afterSetHostNetworkEnabled, asyncResp);
    dbus::utility::setProperty(object[0].first, objPath,
                               "xyz.openbmc_project.Object.Enable", "Enabled",
                               value, handler);
}

template <typename Callback>
inline void checkAssociatedChassis(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, Callback&& callback,
    const dbus::utility::MapperGetSubTreePathsResponse& chassisPaths)
{
    auto foundFlag = std::make_shared<bool>(false);

    // Loop through all chassis paths to check their associations
    for (const std::string& chassisPath : chassisPaths)
    {
        dbus::utility::getAssociationEndPoints(
            chassisPath + "/chassis",
            [asyncResp, chassisId, callback, foundFlag,
             chassisPath](const boost::system::error_code& ec,
                          const dbus::utility::MapperEndPoints& endpoints) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG("No association endpoint found for {}: {}",
                                     chassisPath, ec);
                    return;
                }

                for (const std::string& endpoint : endpoints)
                {
                    if (endpoint.ends_with(chassisId))
                    {
                        if (!*foundFlag)
                        {
                            *foundFlag = true;
                            BMCWEB_LOG_DEBUG(
                                "Found associated chassis endpoint: {}",
                                endpoint);
                            callback(std::optional<std::string>(endpoint));
                        }
                        return;
                    }
                }
            });
    }
}

template <typename Callback>
inline void validLeakDetectionCallback(
    const boost::system::error_code& ec,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const dbus::utility::MapperGetSubTreePathsResponse& chassisPaths,
    const std::string& chassisId, Callback&& callback)
{
    BMCWEB_LOG_DEBUG("validLeakDetectionCallback respHandler enter");
    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "validLeakDetectionCallback respHandler DBUS error: {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    std::optional<std::string> chassisPath;
    for (const std::string& chassis : chassisPaths)
    {
        sdbusplus::message::object_path path(chassis);
        std::string chassisName = path.parent_path().filename();
        if (chassisName.empty())
        {
            BMCWEB_LOG_ERROR("Failed to find '/' in {}", chassis);
            continue;
        }
        if (chassisName == chassisId)
        {
            chassisPath = chassis;
            callback(chassisPath);
            return;
        }
    }

    // If not found in direct paths, check associated chassis
    checkAssociatedChassis(asyncResp, chassisId, callback, chassisPaths);
}

template <typename Callback>
void getValidLeakDetectionPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, Callback&& callback)
{
    constexpr std::array<std::string_view, 2> interfaces = {
        "xyz.openbmc_project.Configuration.VoltageLeakDetector",
        "xyz.openbmc_project.Inventory.Item.LeakDetector"};

    // Get the Chassis Collection
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [callback = std::forward<Callback>(callback), asyncResp,
         chassisId](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreePathsResponse&
                        chassisPaths) mutable {
            validLeakDetectionCallback(ec, asyncResp, chassisPaths, chassisId,
                                       callback);
        });
}

inline void doLeakDetectionUrlGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_DEBUG("{}: No leak detection", chassisId);
        return;
    }

    asyncResp->res.jsonValue["LeakDetection"]["@odata.id"] =
        boost::urls::format(
            "/redfish/v1/Chassis/{}/ThermalSubsystem/LeakDetection", chassisId);
}

inline void doLeakDetectionPolicyGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_DEBUG("{}: No Leak Detection policy", chassisId);
        return;
    }
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["Policies"]["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}/Oem/Nvidia/Policies",
                            chassisId);
}

inline void populateLastIntrusionDetected(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::getProperty<uint64_t>(
        "xyz.openbmc_project.IntrusionSensor",
        "/xyz/openbmc_project/Chassis/Intrusion",
        "xyz.openbmc_project.Time.EpochTime", "Elapsed",
        [asyncResp](const boost::system::error_code& ec,
                    const uint64_t& epochSeconds) {
            if (ec)
            {
                BMCWEB_LOG_WARNING("getLastIntrusionDetected DBUS error: {}",
                                   ec);
                return;
            }

            asyncResp->res.jsonValue["Oem"]["Nvidia"]["LastIntrusionDetected"] =
                redfish::time_utils::getDateTimeUint(epochSeconds);
        });
}

inline void afterGetAssociatedDomains(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subTree)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            messages::internalError(asyncResp->res);
        }
        return;
    }

    if (subTree.empty())
    {
        return;
    }

    const auto domainUri = boost::urls::format(
        "/redfish/v1/Chassis/{}/Oem/Nvidia/WriteProtectDomains", chassisId);
    auto& oem = asyncResp->res.jsonValue["Oem"]["Nvidia"];
    oem["WriteProtectDomains"]["@odata.id"] = domainUri;
}

inline std::string dbusToEmbeddedProcessorOSState(
    const std::string& dbusBootProgress)
{
    auto it = dbusBootProgressToOSStateMap.find(dbusBootProgress);
    if (it != dbusBootProgressToOSStateMap.end())
    {
        return std::string(it->second);
    }
    return "";
}

inline void getEmbeddedProcessorOSState(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisObjPath)
{
    dbus::utility::findAssociations(
        chassisObjPath + "/os_states",
        [asyncResp,
         chassisObjPath](const boost::system::error_code& ec,
                         const std::vector<std::string>& associations) {
            if (ec || associations.empty())
            {
                // no os_states association found, no error
                BMCWEB_LOG_DEBUG("No os_states association found for {}: {}",
                                 chassisObjPath, ec.message());
                return;
            }
            const std::string& osStatePath = associations.front();
            constexpr std::array<std::string_view, 1> bootProgressIface{
                "xyz.openbmc_project.State.Boot.Progress"};
            dbus::utility::getDbusObject(
                osStatePath, bootProgressIface,
                [asyncResp, osStatePath](
                    const boost::system::error_code& ecObj,
                    const dbus::utility::MapperGetObject& mapperResponse) {
                    if (ecObj || mapperResponse.empty())
                    {
                        BMCWEB_LOG_DEBUG(
                            "No BootProgress interface found for {}: {}",
                            osStatePath, ecObj.message());
                        return;
                    }
                    const std::string& service = mapperResponse.begin()->first;
                    sdbusplus::asio::getProperty<std::string>(
                        *crow::connections::systemBus, service, osStatePath,
                        "xyz.openbmc_project.State.Boot.Progress",
                        "BootProgress",
                        [asyncResp,
                         osStatePath](const boost::system::error_code& ecProp,
                                      const std::string& bootProgress) {
                            if (ecProp)
                            {
                                BMCWEB_LOG_ERROR(
                                    "BootProgress getProperty error for {}: {}",
                                    osStatePath, ecProp.message());
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            const std::string rfState =
                                dbusToEmbeddedProcessorOSState(bootProgress);
                            if (rfState.empty())
                            {
                                BMCWEB_LOG_ERROR(
                                    "Unknown BootProgress state for {}: {}",
                                    osStatePath, bootProgress);
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            asyncResp->res
                                .jsonValue["Oem"]["Nvidia"]
                                          ["EmbeddedProcessorOSState"] =
                                rfState;
                        });
                });
        });
}

inline void populateWriteProtectDomainLink(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    write_protect_domains::getAssociatedDomains(
        chassisId,
        std::bind_front(afterGetAssociatedDomains, asyncResp, chassisId));
}

inline void handleChassisGetAllProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, [[maybe_unused]] const std::string& path,
    const dbus::utility::DBusPropertiesMap& propertiesList,
    [[maybe_unused]] const std::string& connectionName,
    [[maybe_unused]] const std::vector<std::string>& interfaces)
{
    const std::string* partNumber = nullptr;
    const std::string* serialNumber = nullptr;
    const std::string* manufacturer = nullptr;
    const std::string* model = nullptr;
    const std::string* sparePartNumber = nullptr;

    const std::string* uuid = nullptr;
    const std::string* locationCode = nullptr;
    const std::string* locationType = nullptr;
    const std::string* locationContext = nullptr;
    const std::string* type = nullptr;
    const double* height = nullptr;
    const double* width = nullptr;
    const double* depth = nullptr;
    const size_t* minPowerWatts = nullptr;
    const size_t* maxPowerWatts = nullptr;
    const std::string* assetTag = nullptr;
    const bool* writeProtected = nullptr;
    const bool* writeProtectedControl = nullptr;
    const uint64_t* pCIeReferenceClockCount = nullptr;
    const std::string* reference = nullptr;
    const std::string* orientation = nullptr;
    const uint64_t* locationOrdinalValue = nullptr;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), propertiesList,   //
        "AssetTag", assetTag,                               //
        "Depth", depth,                                     //
        "Height", height,                                   //
        "LocationCode", locationCode,                       //
        "LocationContext", locationContext,                 //
        "LocationOrdinalValue", locationOrdinalValue,       //
        "LocationReference", reference,                     //
        "LocationType", locationType,                       //
        "Manufacturer", manufacturer,                       //
        "MaxPowerWatts", maxPowerWatts,                     //
        "MinPowerWatts", minPowerWatts,                     //
        "Model", model,                                     //
        "Orientation", orientation,                         //
        "PCIeReferenceClockCount", pCIeReferenceClockCount, //
        "PartNumber", partNumber,                           //
        "SerialNumber", serialNumber,                       //
        "SparePartNumber", sparePartNumber,                 //
        "Type", type,                                       //
        "UUID", uuid,                                       //
        "Width", width,                                     //
        "WriteProtected", writeProtected,                   //
        "WriteProtectedControl", writeProtectedControl);

    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    redfish::mapValidOrNull(asyncResp->res.jsonValue, "PartNumber", partNumber);
    redfish::mapValidOrNull(asyncResp->res.jsonValue, "SerialNumber",
                            serialNumber);
    redfish::mapValidOrNull(asyncResp->res.jsonValue, "Manufacturer",
                            manufacturer);
    redfish::mapValidOrNull(asyncResp->res.jsonValue, "Model", model);
    // SparePartNumber is optional on D-Bus
    // so skip if it is empty
    redfish::mapValidOrOmit(asyncResp->res.jsonValue, "SparePartNumber",
                            sparePartNumber);

    if (uuid != nullptr)
    {
        if (!(uuid->empty()))
        {
            asyncResp->res.jsonValue["UUID"] = *uuid;
        }
    }
    if (locationCode != nullptr)
    {
        asyncResp->res.jsonValue["Location"]["PartLocation"]["ServiceLabel"] =
            *locationCode;
    }
    if (locationType != nullptr)
    {
        asyncResp->res.jsonValue["Location"]["PartLocation"]["LocationType"] =
            redfish::dbus_utils::toLocationType(*locationType);
    }
    if (reference != nullptr)
    {
        asyncResp->res.jsonValue["Location"]["PartLocation"]["Reference"] =
            redfish::dbus_utils::toReference(*reference);
    }
    if (orientation != nullptr)
    {
        asyncResp->res.jsonValue["Location"]["PartLocation"]["Orientation"] =
            redfish::dbus_utils::toOrientation(*orientation);
    }
    if (locationOrdinalValue != nullptr)
    {
        asyncResp->res
            .jsonValue["Location"]["PartLocation"]["LocationOrdinalValue"] =
            *locationOrdinalValue;
    }
    if (locationContext != nullptr)
    {
        asyncResp->res.jsonValue["Location"]["PartLocationContext"] =
            *locationContext;
    }
    if (height != nullptr)
    {
        asyncResp->res.jsonValue["HeightMm"] = *height;
    }
    if (width != nullptr)
    {
        asyncResp->res.jsonValue["WidthMm"] = *width;
    }
    if (depth != nullptr)
    {
        asyncResp->res.jsonValue["DepthMm"] = *depth;
    }
    if (minPowerWatts != nullptr)
    {
        asyncResp->res.jsonValue["MinPowerWatts"] = *minPowerWatts;
    }
    if (maxPowerWatts != nullptr)
    {
        asyncResp->res.jsonValue["MaxPowerWatts"] = *maxPowerWatts;
    }
    if (assetTag != nullptr)
    {
        asyncResp->res.jsonValue["AssetTag"] = *assetTag;
    }
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        // default oem data
        nlohmann::json& oem = asyncResp->res.jsonValue["Oem"]["Nvidia"];
        oem["@odata.type"] = "#NvidiaChassis.v1_15_0.NvidiaChassis";
        populateWriteProtectDomainLink(asyncResp, chassisId);
        getEmbeddedProcessorOSState(asyncResp, path);

        if (writeProtected != nullptr)
        {
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["HardwareWriteProtected"] =
                *writeProtected;
        }

        if (writeProtectedControl != nullptr)
        {
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["HardwareWriteProtectedControl"] =
                *writeProtectedControl;
        }

        dbus::utility::getDbusObject(
            sdbusplus::message::object_path("/xyz/openbmc_project/software") /=
            chassisId,
            std::array<std::string_view, 1>{
                "xyz.openbmc_project.Software.Settings"},
            [asyncResp,
             chassisId](const boost::system::error_code& ec,
                        const dbus::utility::MapperGetObject& object) {
                getChassisWriteProtectProtectEnable(asyncResp, ec, chassisId,
                                                    object);
            });

        if constexpr (BMCWEB_NVIDIA_HOST_MANAGEMENT_NETWORK_ACCESS)
        {
            dbus::utility::getAssociationEndPoints(
                path + "/host_management_network_access",
                std::bind_front(afterGetHostNetworkAccessEndpoints, asyncResp));
        } // BMCWEB_NVIDIA_HOST_MANAGEMENT_NETWORK_ACCESS

        if (pCIeReferenceClockCount != nullptr)
        {
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["PCIeReferenceClockCount"] =
                *pCIeReferenceClockCount;
        }

        if constexpr (BMCWEB_NVIDIA_OEM_POLICIES)
        {
            if constexpr (BMCWEB_REDFISH_LEAK_DETECT)
            {
                // Policy Collection
                getValidLeakDetectionPath(
                    asyncResp, chassisId,
                    std::bind_front(doLeakDetectionPolicyGet, asyncResp,
                                    chassisId));
            }
        }
    }

    if constexpr (BMCWEB_REDFISH_ALLOW_DEPRECATED_POWER_THERMAL &&
                  BMCWEB_HOST_OS_FEATURES)
    {
        asyncResp->res.jsonValue["Thermal"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Chassis/{}/Thermal", chassisId);

        // Power object
        asyncResp->res.jsonValue["Power"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Chassis/{}/Power", chassisId);
    }

    if constexpr (BMCWEB_REDFISH_NEW_POWERSUBSYSTEM_THERMALSUBSYSTEM)
    {
        asyncResp->res.jsonValue["ThermalSubsystem"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Chassis/{}/ThermalSubsystem",
                                chassisId);
        asyncResp->res.jsonValue["PowerSubsystem"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Chassis/{}/PowerSubsystem",
                                chassisId);
        asyncResp->res.jsonValue["EnvironmentMetrics"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Chassis/{}/EnvironmentMetrics",
                                chassisId);
    }

    // SensorCollection
    asyncResp->res.jsonValue["Sensors"]["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}/Sensors", chassisId);

    // Assembly collection
    asyncResp->res.jsonValue["Assembly"]["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}/Assembly", chassisId);

    if constexpr (BMCWEB_NETWORK_ADAPTERS)
    {
        // NetworkAdapters collection
        asyncResp->res.jsonValue["NetworkAdapters"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Chassis/{}/NetworkAdapters",
                                chassisId);
    }
    // PCIeSlots collection
    asyncResp->res.jsonValue["PCIeSlots"]["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}/PCIeSlots", chassisId);

    // TrustedComponent collection (once per GET when already populated;
    // handleChassisGetAllProperties may run for multiple D-Bus interface
    // queries on the same chassis).
    if (!asyncResp->res.jsonValue.contains("TrustedComponents"))
    {
        getChassisAssociatedEndpoint(
            chassisId,
            [asyncResp, chassisId, chassisPath = path](
                [[maybe_unused]] const std::string& endpoint, bool exists,
                [[maybe_unused]] const std::optional<std::string>&
                    resolvedPath) {
                if (exists)
                {
                    asyncResp->res.jsonValue["TrustedComponents"]["@odata.id"] =
                        boost::urls::format(
                            "/redfish/v1/Chassis/{}/TrustedComponents",
                            chassisId);
                }
                else
                {
                    checkTPMComponentsAndAddLink(asyncResp, chassisId,
                                                 chassisPath);
                }
            },
            path);
    }

    // Controls Collection
    asyncResp->res.jsonValue["Controls"] = {
        {"@odata.id", "/redfish/v1/Chassis/" + chassisId + "/Controls"}};

    // Links/ComputerSystems is emitted only on the chassis that actually hosts
    // the computer system, gated on a "computer_system" association authored
    // from config, rather than uniformly on every chassis.
    getChassisComputerSystemsLink(asyncResp, path);

    nlohmann::json::array_t managedBy;
    nlohmann::json::object_t manager;
    manager["@odata.id"] =
        "/redfish/v1/Managers/" + std::string(BMCWEB_REDFISH_MANAGER_URI_NAME);
    managedBy.emplace_back(std::move(manager));
    asyncResp->res.jsonValue["Links"]["ManagedBy"] = std::move(managedBy);
}

inline void oemChassisHardwareWriteProtectEnable(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const bool value)
{
    dbus::utility::getDbusObject(
        sdbusplus::message::object_path("/xyz/openbmc_project/software") /=
        chassisId,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Software.Settings"},
        [asyncResp, chassisId,
         value](const boost::system::error_code& ec,
                const dbus::utility::MapperGetObject& object) {
            setChassisWriteProtectProtectEnable(asyncResp, ec, chassisId,
                                                object, value);
        });
}

inline void afterSetHostNetworkAccessEndpoints(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, const bool value,
    const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& endpoints)
{
    if (ec || endpoints.empty())
    {
        // Chassis does not own the host-NIC control; fail the write instead
        // of reporting success for a property the BMC never set.
        messages::propertyUnknown(asyncResp->res,
                                  "HostManagementNetworkAccess");
        return;
    }
    dbus::utility::getDbusObject(
        endpoints[0],
        std::array<std::string_view, 1>{"xyz.openbmc_project.Object.Enable"},
        std::bind_front(setChassisHostNetworkEnable, asyncResp, endpoints[0],
                        value));
}

inline void oemChassisHostNetworkEnable(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisPath, const bool value)
{
    dbus::utility::getAssociationEndPoints(
        chassisPath + "/host_management_network_access",
        std::bind_front(afterSetHostNetworkAccessEndpoints, asyncResp, value));
}

inline void getChassisAssetData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Chassis Asset Data");
    dbus::utility::getAllProperties(
        service, objPath, "xyz.openbmc_project.Inventory.Decorator.Asset",
        [objPath,
         asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error for getChassisAssetData()");
                messages::internalError(asyncResp->res);
                return;
            }

            const std::string* serialNumber = nullptr;
            const std::string* model = nullptr;
            const std::string* manufacturer = nullptr;
            const std::string* partNumber = nullptr;
            const std::string* sparePartNumber = nullptr;

            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), properties, "SerialNumber",
                serialNumber, "Model", model, "Manufacturer", manufacturer,
                "PartNumber", partNumber, "SparePartNumber", sparePartNumber);

            if (!success)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error while unpacking properties");
                messages::internalError(asyncResp->res);
                return;
            }

            redfish::mapValidOrOmit(asyncResp->res.jsonValue, "SerialNumber",
                                    serialNumber);
            redfish::mapValidOrOmit(asyncResp->res.jsonValue, "Model", model);
            redfish::mapValidOrNull(asyncResp->res.jsonValue, "PartNumber",
                                    partNumber);
            redfish::mapValidOrOmit(asyncResp->res.jsonValue, "Manufacturer",
                                    manufacturer);
        });
}

inline void handleFruAssetInformation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, std::string chassisPath)
{
    chassisPath.erase(chassisPath.size() - chassisId.size());
    asyncResp->res.jsonValue["Id"] = chassisId;
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Decorator.Asset"};
    dbus::utility::getSubTree(
        chassisPath, 0, interfaces,
        [asyncResp,
         chassisId](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                     object : subtree)
            {
                const std::string& path = object.first;
                const std::vector<
                    std::pair<std::string, std::vector<std::string>>>&
                    serviceMap = object.second;

                sdbusplus::message::object_path objPath(path);
                // The path should end with chassisId (representing
                // resource) and that path should implement Asset Interface
                if (objPath.filename() != chassisId)
                {
                    continue;
                }
                for (const auto& [serviceName, interfaceList] : serviceMap)
                {
                    for (const auto& interface : interfaceList)
                    {
                        if (interface ==
                            "xyz.openbmc_project.Inventory.Decorator.Asset")
                        {
                            getChassisAssetData(asyncResp, serviceName, path);
                        }
                    }
                }
            }
        });
}

template <typename Enum>
inline Enum parseDbusEnum(const std::string& stateType, Enum invalidValue)
{
    auto pos = stateType.rfind('.');
    if (pos == std::string::npos)
    {
        return invalidValue;
    }

    Enum value = nlohmann::json(stateType.substr(pos + 1)).get<Enum>();
    if (value == invalidValue)
    {
        BMCWEB_LOG_ERROR("Unknown DBus enum value {}", stateType);
        return invalidValue;
    }
    return value;
}
inline chassis::IntrusionSensor getIntrusionStateType(
    const std::string& stateType)
{
    return parseDbusEnum(stateType, chassis::IntrusionSensor::Invalid);
}

inline chassis::IntrusionSensorReArm getIntrusionRearmType(
    const std::string& stateType)
{
    return parseDbusEnum(stateType, chassis::IntrusionSensorReArm::Invalid);
}

inline void getIntrusionByService(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get intrusion status by service ");

    dbus::utility::getAllProperties(
        service, objPath, "xyz.openbmc_project.Chassis.Intrusion",
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                return;
            }
            const std::string* rearm = nullptr;
            const std::string* status = nullptr;

            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), properties, "Rearm", rearm,
                "Status", status);

            if (!success)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error while unpacking properties");
                messages::internalError(asyncResp->res);
                return;
            }

            auto& physicalSecurity =
                asyncResp->res.jsonValue["PhysicalSecurity"];
            if (rearm != nullptr)
            {
                physicalSecurity["IntrusionSensorReArm"] =
                    getIntrusionRearmType(*rearm);
            }

            if (status != nullptr)
            {
                physicalSecurity["IntrusionSensor"] =
                    getIntrusionStateType(*status);
            }

            physicalSecurity["IntrusionSensorNumber"] = 1;
        });
}

/**
 * Retrieves physical security properties over dbus
 */
inline void getPhysicalSecurityData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Chassis.Intrusion"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project", 0, interfaces,
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                // do not add err msg in redfish response, because this is
                // not
                //     mandatory property
                BMCWEB_LOG_INFO("DBUS error: no matched iface {}", ec);
                return;
            }
            // Iterate over all retrieved ObjectPaths.
            for (const auto& object : subtree)
            {
                if (!object.second.empty())
                {
                    const auto service = object.second.front();
                    getIntrusionByService(asyncResp, service.first,
                                          object.first);
                    return;
                }
            }
        });
}

// Function to insert an element in sorted order based on "Id" field
inline void insertSorted(nlohmann::json& arr, const nlohmann::json& element,
                         const std::string& sortField)
{
    auto it = std::lower_bound(
        arr.begin(), arr.end(), element,
        [sortField](const nlohmann::json& left, const nlohmann::json& right) {
            return left[sortField] < right[sortField];
        });
    arr.insert(it, element);
}

template <typename Handler>
inline void getChassisRelatedItem(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::object_path& objectPath,
    const std::string& chassisId, Handler&& handler)
{
    // Ensure to pick up the resource from Chassis interface
    size_t chassisNamePos = objectPath.str.rfind('/');
    if (chassisNamePos == std::string::npos ||
        chassisNamePos == (objectPath.str.size() - 1))
    {
        return;
    }

    constexpr std::array<std::string_view, 1> chassisInterface = {
        "xyz.openbmc_project.Inventory.Item.Chassis"};

    dbus::utility::getSubTreePaths(
        objectPath.str.substr(0, chassisNamePos), 0, chassisInterface,
        [asyncResp, objectPath, chassisId,
         handler = std::forward<Handler>(handler)](
            const boost::system::error_code ec,
            const dbus::utility::MapperGetSubTreePathsResponse& subtreePaths) {
            if ((!ec) && (!subtreePaths.empty()))
            {
                for (const auto& path : subtreePaths)
                {
                    sdbusplus::message::object_path chassisPath(path);
                    std::string chassisName = chassisPath.filename();
                    if (chassisId == chassisName)
                    {
                        handler(asyncResp, objectPath);
                    }
                }
            }
        });
}

/**
 * @brief Retrieves Oem BootStatus code for the chassis endpoint
 * @param asyncResp   Pointer to object holding response data
 * @param chassisObjPath   Path of the chassis endpoint
 */
inline void getOemBootStatus(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisObjPath)
{
    static constexpr std::array<std::string_view, 1> interfaces = {
        bootStatusIntf};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory/system/chassis", 0, interfaces,
        [asyncResp, chassisObjPath](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
            std::string statusService{};

            if (ec)
            {
                BMCWEB_LOG_DEBUG("No D-Bus object found implementing"
                                 "com.nvidia.RoT.BootStatus for {}",
                                 chassisObjPath);
                return;
            }

            for (const auto& obj : subtree)
            {
                sdbusplus::message::object_path objPath(obj.first);
                if (objPath.filename() != chassisObjPath)
                {
                    continue;
                }

                if (!obj.second.empty())
                {
                    statusService = obj.second.begin()->first;
                }
            }
            dbus::utility::async_method_call(
                [asyncResp, chassisObjPath](
                    const boost::system::error_code& ec1,
                    const boost::container::flat_map<
                        std::string, dbus::utility::DbusVariantType>&
                        propertiesList) {
                    if (ec1)
                    {
                        // OK since not all fwtypes support bootstatus
                        return;
                    }

                    const auto& it = propertiesList.find("BootStatus");
                    if (it == propertiesList.end())
                    {
                        BMCWEB_LOG_ERROR(
                            "Can't find D-Bus property \"com.nvidia.RoT.BootStatus.BootStatus\"!");
                        messages::propertyMissing(asyncResp->res, "BootStatus");
                        return;
                    }

                    const auto* bootStatus =
                        std::get_if<std::vector<uint8_t>>(&it->second);
                    if (bootStatus == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "wrong types for D-Bus property \"com.nvidia.RoT.BootStatus.BootStatus\"!");
                        messages::propertyValueTypeError(asyncResp->res, "",
                                                         "BootStatus");
                        return;
                    }
                    std::string out{};

                    std::ostringstream oss;
                    for (auto byte : *bootStatus)
                    {
                        // Convert each byte to a two-character hexadecimal
                        // string
                        oss << std::hex << std::setw(2) << std::setfill('0')
                            << static_cast<int>(byte);
                    }
                    out = "0x" + oss.str();
                    asyncResp->res.jsonValue["BootStatusCode"] = out;
                },
                statusService,
                "/xyz/openbmc_project/inventory/system/chassis/" +
                    chassisObjPath,
                "org.freedesktop.DBus.Properties", "GetAll",
                "com.nvidia.RoT.BootStatus");
        });
}

/**
 *@brief Sets in-band for particular chassis
 *
 * @param asyncResp   Pointer to object holding response data
 * @param chassisId  Chassis ID
 * @param enabled Enable or disable the in-band
 *
 * @return None.
 */
inline void setInBandEnabled(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, bool enabled)
{
    redfish::getChassisListForInBand(
        asyncResp,
        [asyncResp, enabled, chassisId](
            const std::vector<std::string>& inbandUpdatePolicyAllowList) {
            auto itAllowList =
                std::find(inbandUpdatePolicyAllowList.begin(),
                          inbandUpdatePolicyAllowList.end(), chassisId);
            if (itAllowList != inbandUpdatePolicyAllowList.end())
            {
                enableInBand(asyncResp, enabled, chassisId);
            }
            else
            {
                messages::propertyUnknown(asyncResp->res,
                                          "InbandUpdatePolicyEnabled");
            }
        });
}

/**
 * @brief Handle combined getDbusObject response: dispatch getProperty for
 * FailoverPolicy, InbandUpdatePolicy, and ImageCopyPolicy per interface
 * found.
 *
 * @param policyInterfaces List of interface names requested (order used for
 *                         dispatch).
 */
inline void getChassisPolicyPropertiesFromMapper(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisCfgPath,
    std::span<const std::string_view> policyInterfaces,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& mapperResponse)
{
    if (ec || mapperResponse.empty())
    {
        BMCWEB_LOG_DEBUG("Chassis policy interfaces not present at {}: {}",
                         chassisCfgPath, ec);
        return;
    }
    boost::container::flat_map<std::string_view, std::string> ifaceToService;
    for (const std::string& iface : mapperResponse.front().second)
    {
        ifaceToService[iface] = mapperResponse.front().first;
    }
    if (!asyncResp->res.jsonValue.contains("Oem"))
    {
        asyncResp->res.jsonValue["Oem"] = nlohmann::json::object();
    }
    if (!asyncResp->res.jsonValue["Oem"].contains("Nvidia"))
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"] = nlohmann::json::object();
    }
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
        "#NvidiaChassis.v1_12_0.NvidiaRoTChassis";

    for (std::string_view iface : policyInterfaces)
    {
        auto it = ifaceToService.find(iface);
        if (it == ifaceToService.end())
        {
            continue;
        }
        if (iface == "com.nvidia.FailoverPolicy")
        {
            sdbusplus::asio::getProperty<std::string>(
                *crow::connections::systemBus, it->second, chassisCfgPath,
                "com.nvidia.FailoverPolicy", "FailoverPolicy",
                [asyncResp,
                 chassisCfgPath](const boost::system::error_code& ecProp,
                                 const std::string& propertyValue) {
                    getFailoverPolicyCallback(asyncResp, chassisCfgPath, ecProp,
                                              propertyValue);
                });
        }
        else if (iface == "com.nvidia.InbandUpdatePolicy")
        {
            sdbusplus::asio::getProperty<std::string>(
                *crow::connections::systemBus, it->second, chassisCfgPath,
                "com.nvidia.InbandUpdatePolicy", "InbandUpdatePolicy",
                [asyncResp](const boost::system::error_code& ecProp,
                            const std::string& propertyValue) {
                    if (ecProp)
                    {
                        BMCWEB_LOG_DEBUG(
                            "InbandUpdatePolicy getProperty error: {}",
                            ecProp.message());
                        return;
                    }
                    nlohmann::json& oem =
                        asyncResp->res.jsonValue["Oem"]["Nvidia"];
                    if (propertyValue ==
                        "com.nvidia.InbandUpdatePolicy.InbandPolicyState.Enabled")
                    {
                        oem["InbandUpdatePolicyEnabled"] = true;
                    }
                    else if (propertyValue ==
                             "com.nvidia.InbandUpdatePolicy.InbandPolicyState."
                             "Disabled")
                    {
                        oem["InbandUpdatePolicyEnabled"] = false;
                    }
                });
        }
        else if (iface == "com.nvidia.ImageCopyPolicy")
        {
            sdbusplus::asio::getProperty<std::string>(
                *crow::connections::systemBus, it->second, chassisCfgPath,
                "com.nvidia.ImageCopyPolicy", "ImageCopyPolicy",
                [asyncResp,
                 chassisCfgPath](const boost::system::error_code& ecProp,
                                 const std::string& propertyValue) {
                    getImageCopyPolicyCallback(asyncResp, chassisCfgPath,
                                               ecProp, propertyValue);
                });
        }
    }
}

inline nvidia_chassis::BackgroundCopyStatus getBackgroundCopyStatusType(
    const std::string& status)
{
    if (status == "com.nvidia.ImageCopyState.Status.ImageCopyNotTriggered")
    {
        return nvidia_chassis::BackgroundCopyStatus::Pending;
    }
    if (status == "com.nvidia.ImageCopyState.Status.InProgress")
    {
        return nvidia_chassis::BackgroundCopyStatus::InProgress;
    }
    if (status == "com.nvidia.ImageCopyState.Status.Complete")
    {
        return nvidia_chassis::BackgroundCopyStatus::Completed;
    }
    if (status == "com.nvidia.ImageCopyState.Status.UndefinedFailure" ||
        status == "com.nvidia.ImageCopyState.Status.NoValidImage" ||
        status ==
            "com.nvidia.ImageCopyState.Status.DestinationWriteProtected" ||
        status == "com.nvidia.ImageCopyState.Status.FailFlashAccess" ||
        status == "com.nvidia.ImageCopyState.Status.FailedVerify")
    {
        return nvidia_chassis::BackgroundCopyStatus::Failed;
    }
    return nvidia_chassis::BackgroundCopyStatus::Invalid;
}

inline void getBackgroundCopyStatusCallback(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, const std::string& status)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("BackgroundCopyStatus getProperty error: {}",
                         ec.value());
        return;
    }

    nvidia_chassis::BackgroundCopyStatus backgroundCopyStatus =
        getBackgroundCopyStatusType(status);
    if (backgroundCopyStatus == nvidia_chassis::BackgroundCopyStatus::Invalid)
    {
        BMCWEB_LOG_ERROR("Unknown com.nvidia.ImageCopyState.Status value: {}",
                         status);
        return;
    }

    asyncResp->res.jsonValue["Oem"]["Nvidia"]["BackgroundCopyStatus"] =
        backgroundCopyStatus;
}

inline void getBackgroundCopyStatus(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const sdbusplus::message::object_path& objPath)
{
    dbus::utility::getProperty<std::string>(
        service, objPath.str, "com.nvidia.ImageCopyState", "Status",
        std::bind_front(getBackgroundCopyStatusCallback, asyncResp));
}

/**
 * @brief Single getDbusObject for chassis path requesting FailoverPolicy,
 * InbandUpdatePolicy, and ImageCopyPolicy; dispatches getProperty for each
 * interface found.
 */
inline void getChassisPolicyProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    constexpr std::string_view chassisPolicyDbusPath =
        "/xyz/openbmc_project/inventory/system/chassis/";
    constexpr std::array<std::string_view, 3> policyInterfaces = {
        "com.nvidia.FailoverPolicy", "com.nvidia.InbandUpdatePolicy",
        "com.nvidia.ImageCopyPolicy"};

    std::string chassisCfgPath = std::string(chassisPolicyDbusPath) + chassisId;
    dbus::utility::getDbusObject(
        chassisCfgPath, policyInterfaces,
        [asyncResp, chassisCfgPath, policyInterfaces](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetObject& mapperResponse) {
            getChassisPolicyPropertiesFromMapper(
                asyncResp, chassisCfgPath, policyInterfaces, ec,
                mapperResponse);
        });
}

/**
 * @brief Get the Chassis UUID
 *
 * @param asyncResp - Pointer to object holding response data
 * @param connectionName - connection name
 * @param path - D-Bus path
 *
 * @return None.
 */
inline void getChassisUUID(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& path)
{
    dbus::utility::getProperty<std::string>(
        connectionName, path, "xyz.openbmc_project.Common.UUID", "UUID",
        [asyncResp, path](const boost::system::error_code& ec,
                          const std::string& chassisUUID) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for UUID");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["UUID"] = chassisUUID;
        });
}

inline std::string getChassisType(const std::string& chassisType)
{
    if (chassisType ==
        "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.Component")
    {
        return "Component";
    }
    if (chassisType ==
        "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.Enclosure")
    {
        return "Enclosure";
    }
    if (chassisType ==
        "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.Module")
    {
        return "Module";
    }
    if (chassisType ==
        "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.RackMount")
    {
        return "RackMount";
    }
    if (chassisType ==
        "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.Shelf")
    {
        return "Shelf";
    }
    if (chassisType ==
        "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.StandAlone")
    {
        return "StandAlone";
    }
    if (chassisType ==
        "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.Card")
    {
        return "Card";
    }
    if (chassisType ==
        "xyz.openbmc_project.Inventory.Item.Chassis.ChassisType.Zone")
    {
        return "Zone";
    }
    // Unknown or others
    return "";
}

inline void getChassisType(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& path)
{
    dbus::utility::getProperty<std::string>(
        connectionName, path, "xyz.openbmc_project.Inventory.Item.Chassis",
        "Type",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& chassisType) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for UUID");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["ChassisType"] =
                getChassisType(chassisType);
        });
}

template <typename CallbackFunc>
inline void checkIndicatorChassis(const std::string& connectionName,
                                  const std::string& path,
                                  CallbackFunc&& callback)
{
    dbus::utility::getProperty<std::string>(
        connectionName, path, "xyz.openbmc_project.Inventory.Item.Chassis",
        "Type",
        [callback](const boost::system::error_code& ec,
                   const std::string& chassisType) {
            // The default should be true because this object may have other
            // hasIndicatorLed interfaces to support it.
            bool indicatorChassis = true;

            if (!ec)
            {
                // If the object has Chassis interface, need to ensure the
                // ChassisType should be 'Blade', 'Enclosure', 'Shelf', or
                // 'StorageEnclosure' to support the enclosure LED.
                std::array<std::string, 4> supportedType = {
                    "Blade", "Enclosure", "Shelf", "StorageEnclosure"};
                std::string strChassisType =
                    redfish::nvidia_chassis_utils::getChassisType(chassisType);
                auto* it = std::find(supportedType.begin(), supportedType.end(),
                                     strChassisType);
                if (it == supportedType.end())
                {
                    // unsupported ChassisType
                    indicatorChassis = false;
                }
            }

            callback(indicatorChassis);
        });
}

inline void populateDeviceHealthFromFile(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if constexpr (BMCWEB_NVIDIA_OEM_DEVICE_STATUS_FROM_FILE)
    {
        /** NOTES: This is a temporary solution to avoid performance
         * issues may impact other Redfish services. Please call for
         * architecture decisions from all NvBMC teams if want to use it
         * in other places.
         */
        health_utils::getDeviceHealthInfo(asyncResp->res, chassisId);
    }
}

template <typename InterfacesContainer>
inline void populateChassisLinksOemAndStatus(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath, const std::string& path,
    const InterfacesContainer& interfaces2, const std::string& chassisId)
{
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        // Baseboard Chassis OEM properties if exist, search by
        // association
        redfish::nvidia_chassis_utils::getOemBaseboardChassisAssert(
            asyncResp, objPath);
    }
    // Links association to underneath chassis
    redfish::nvidia_chassis_utils::getChassisLinksContains(asyncResp, objPath);
    // Links association to underneath processors
    redfish::nvidia_chassis_utils::getChassisProcessorLinks(asyncResp, objPath);
    redfish::nvidia_chassis_utils::getProtocolBridgeForDevices(
        asyncResp, objPath);
    // get boot status
    redfish::nvidia_chassis_utils::getResetStatistics(asyncResp, objPath);
    // Links association to connected fabric switches
    redfish::nvidia_chassis_utils::getChassisFabricSwitchesLinks(
        asyncResp, objPath);
    // Link association to parent chassis
    redfish::chassis_utils::getChassisLinksContainedBy(asyncResp, objPath);
    // get physical security data by default
    if constexpr (!BMCWEB_PLATFORM_CHASSIS_INTRUSION_COMPONENT_ENABLED)
    {
        redfish::nvidia_chassis_utils::getPhysicalSecurityData(asyncResp);
    }
    else
    {
        // if platform intrusion component is specified, get the intrusion
        // data from the platform intrusion component
        // NOLINTNEXTLINE(readability-container-size-empty)
        if (chassisId == BMCWEB_PLATFORM_CHASSIS_INTRUSION_COMPONENT)
        {
            redfish::nvidia_chassis_utils::getPhysicalSecurityData(asyncResp);
            redfish::nvidia_chassis_utils::populateLastIntrusionDetected(
                asyncResp);
        }
    }
    // get network adapter
    redfish::nvidia_chassis_utils::getNetworkAdapters(asyncResp, path,
                                                      interfaces2, chassisId);
    if constexpr (BMCWEB_NVIDIA_DEVICE_STATUS_FROM_ASSOCIATION)
    {
        // get health for network adapter and nvswitches chassis by
        // association
        redfish::nvidia_chassis_utils::getHealthByAssociation(
            asyncResp, path, "all_states", chassisId);
    }
}

inline void parseOemNvidiaPatchPayload(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::optional<nlohmann::json>& oemJsonObj,
    std::optional<std::string>& partNumber,
    std::optional<std::string>& serialNumber,
    std::optional<bool>& hardwareWriteProtectEnable,
    std::optional<bool>& hostNetworkEnable,
    std::optional<double>& cpuClockFrequency,
    std::optional<double>& workloadFactor, std::optional<double>& temperature,
    std::optional<uint32_t>& numberOfCores, std::optional<std::string>& oemSKU)
{
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        if (oemJsonObj)
        {
            nlohmann::json mutableOemJson = *oemJsonObj;
            std::optional<nlohmann::json> nvidiaJsonObj;
            if (json_util::readJson(mutableOemJson, asyncResp->res, "Nvidia",
                                    nvidiaJsonObj))
            {
                std::optional<nlohmann::json> staticPowerHintJsonObj;
                json_util::readJson(
                    *nvidiaJsonObj, asyncResp->res, "PartNumber", partNumber,
                    "SerialNumber", serialNumber, "StaticPowerHint",
                    staticPowerHintJsonObj, "HardwareWriteProtectEnable",
                    hardwareWriteProtectEnable, "HostManagementNetworkAccess",
                    hostNetworkEnable, "SKU", oemSKU);

                if (staticPowerHintJsonObj)
                {
                    std::optional<nlohmann::json> cpuClockFrequencyHzJsonObj;
                    std::optional<nlohmann::json> temperatureCelsiusJsonObj;
                    std::optional<nlohmann::json> workloadFactorJsonObj;
                    std::optional<nlohmann::json> numberOfCoresJsonObj;
                    json_util::readJson(
                        *staticPowerHintJsonObj, asyncResp->res,
                        "CpuClockFrequencyHz", cpuClockFrequencyHzJsonObj,
                        "TemperatureCelsius", temperatureCelsiusJsonObj,
                        "WorkloadFactor", workloadFactorJsonObj,
                        "NumberOfCores", numberOfCoresJsonObj);
                    if (cpuClockFrequencyHzJsonObj)
                    {
                        json_util::readJson(*cpuClockFrequencyHzJsonObj,
                                            asyncResp->res, "SetPoint",
                                            cpuClockFrequency);
                    }
                    if (temperatureCelsiusJsonObj)
                    {
                        json_util::readJson(*temperatureCelsiusJsonObj,
                                            asyncResp->res, "SetPoint",
                                            temperature);
                    }
                    if (workloadFactorJsonObj)
                    {
                        json_util::readJson(*workloadFactorJsonObj,
                                            asyncResp->res, "SetPoint",
                                            workloadFactor);
                    }
                    if (numberOfCoresJsonObj)
                    {
                        json_util::readJson(*numberOfCoresJsonObj,
                                            asyncResp->res, "SetPoint",
                                            numberOfCores);
                    }
                }
            }
        }
    }
}

inline void applyOemChassisPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& path,
    const std::optional<bool>& hardwareWriteProtectEnable,
    const std::optional<bool>& hostNetworkEnable,
    const std::optional<std::string>& partNumber,
    const std::optional<std::string>& serialNumber,
    const std::optional<double>& cpuClockFrequency,
    const std::optional<double>& workloadFactor,
    const std::optional<double>& temperature,
    const std::optional<uint32_t>& numberOfCores)
{
    if (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        if (hardwareWriteProtectEnable)
        {
            redfish::nvidia_chassis_utils::oemChassisHardwareWriteProtectEnable(
                asyncResp, chassisId, *hardwareWriteProtectEnable);
        }
        if (hostNetworkEnable)
        {
            redfish::nvidia_chassis_utils::oemChassisHostNetworkEnable(
                asyncResp, path, *hostNetworkEnable);
        }
        if (partNumber)
        {
            redfish::nvidia_chassis_utils::setOemBaseboardChassisAssert(
                asyncResp, path, "PartNumber", *partNumber);
        }
        if (serialNumber)
        {
            redfish::nvidia_chassis_utils::setOemBaseboardChassisAssert(
                asyncResp, path, "SerialNumber", *serialNumber);
        }
        if (cpuClockFrequency || workloadFactor || temperature || numberOfCores)
        {
            // NumberOfCores is optional for legacy devices that don't
            // expose the underlying effecter; the PLDM side silently
            // ignores the value when no NumberOfCores effecter is present.
            // Default to 0 when the client omits it so the call still goes
            // through.
            if (cpuClockFrequency && workloadFactor && temperature)
            {
                redfish::nvidia_chassis_utils::setStaticPowerHintByChassis(
                    asyncResp, path, *cpuClockFrequency, *workloadFactor,
                    *temperature, numberOfCores.value_or(0));
            }
            else
            {
                if (!cpuClockFrequency)
                {
                    messages::propertyMissing(asyncResp->res,
                                              "CpuClockFrequencyHz");
                }
                if (!workloadFactor)
                {
                    messages::propertyMissing(asyncResp->res, "WorkloadFactor");
                }
                if (!temperature)
                {
                    messages::propertyMissing(asyncResp->res,
                                              "TemperatureCelsius");
                }
                if (!numberOfCores)
                {
                    messages::propertyMissing(asyncResp->res, "NumberOfCores");
                }
            }
        }
    }
}

template <typename CallbackFunc>
inline void isEROTChassis(const std::string& chassisID, CallbackFunc&& callback)
{
    const std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.SPDMResponder"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [chassisID, callback](const boost::system::error_code& ec,
                              const dbus::utility::GetSubTreeType& subtree) {
            if (ec)
            {
                callback(false, false);
                return;
            }
            const auto objIt = std::find_if(
                subtree.begin(), subtree.end(),
                [chassisID](
                    const std::pair<
                        std::string,
                        std::vector<std::pair<
                            std::string, std::vector<std::string>>>>& object) {
                    return chassisID ==
                           sdbusplus::message::object_path(object.first)
                               .filename();
                });
            if (objIt == subtree.end())
            {
                BMCWEB_LOG_DEBUG("Dbus Object not found:{}", chassisID);
                callback(false, false);
                return;
            }
            std::string serviceName;
            for (const auto& service : objIt->second)
            {
                if (!serviceName.empty())
                {
                    break;
                }
                for (const auto& interface : service.second)
                {
                    if (interface ==
                        "xyz.openbmc_project.Association.Definitions")
                    {
                        serviceName = service.first;
                        break;
                    }
                }
            }
            if (serviceName.empty())
            {
                callback(false, false);
                return;
            }
            dbus::utility::getProperty<Associations>(
                serviceName, objIt->first,
                "xyz.openbmc_project.Association.Definitions", "Associations",
                [chassisID, callback](const boost::system::error_code& ec1,
                                      const Associations& associations) {
                    if (ec1)
                    {
                        callback(false, false);
                        return;
                    }
                    for (const auto& assoc : associations)
                    {
                        if (std::get<1>(assoc) == "associated_ROT")
                        {
                            // check if it is CPU ERoT
                            std::string path = std::get<2>(assoc);
                            size_t rotNamePos = path.rfind('/');
                            if (rotNamePos == std::string::npos ||
                                rotNamePos == (path.size() - 1))
                            {
                                callback(true, false);
                                return;
                            }

                            constexpr std::array<std::string_view, 1>
                                cpuInterface = {
                                    "xyz.openbmc_project.Inventory.Item.Cpu"};

                            dbus::utility::getSubTreePaths(
                                path.substr(0, rotNamePos), 0, cpuInterface,
                                [callback](const boost::system::error_code& ec2,
                                           const dbus::utility::
                                               MapperGetSubTreePathsResponse&
                                                   subtreePaths) {
                                    if ((ec2) || (subtreePaths.empty()))
                                    {
                                        callback(true, false);
                                        return;
                                    }
                                    // It's CPU ERoT for DOT actions
                                    callback(true, true);
                                });
                            return;
                        }
                    }
                    callback(false, false);
                });
        });
}

inline void getChassisManufacturer(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path)
{
    dbus::utility::getProperty<std::string>(
        connectionName, path, "xyz.openbmc_project.Inventory.Decorator.Asset",
        "Manufacturer",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& manufacturer) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for Manufacturer");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["Manufacturer"] = manufacturer;
        });
}

/**
 * @brief Get chassis OEM Nvidia SKU property
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       connectionName    Connection name for D-Bus.
 * @param[in]       path        Chassis D-Bus path.
 *
 * @return None.
 */
inline void getChassisOemNvidiaSKU(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path)
{
    BMCWEB_LOG_DEBUG(
        "Reading OEM SKU from: {} (service: {}), D-Bus call: busctl call {} {} org.freedesktop.DBus.Properties Get ss xyz.openbmc_project.Inventory.Decorator.SKU SKU",
        path, connectionName, connectionName, path);

    dbus::utility::getProperty<std::string>(
        connectionName, path, "xyz.openbmc_project.Inventory.Decorator.SKU",
        "SKU",
        [asyncResp,
         path](const boost::system::error_code& ec, const std::string& sku) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error for OEM Nvidia SKU from {}: {}", path,
                    ec.message());
                return;
            }
            if (sku.empty())
            {
                BMCWEB_LOG_DEBUG("OEM Nvidia SKU from {} is empty", path);
                return;
            }
            redfish::mapValidOrOmit(asyncResp->res.jsonValue["Oem"]["Nvidia"],
                                    "SKU", &sku);
        });
}

/**
 * @brief Handle Async.Set check result and add OEM SKU if writable
 */
inline void handleAsyncSetCheckForOemSKU(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& skuPath,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& objMap)
{
    if (ec || objMap.empty())
    {
        BMCWEB_LOG_DEBUG(
            "Path {} does not have Async.Set, OEM SKU not writable", skuPath);
        return;
    }

    BMCWEB_LOG_INFO("Path {} has Async.Set, adding OEM SKU as writable",
                    skuPath);

    // Reuse existing function to read and set OEM SKU
    getChassisOemNvidiaSKU(asyncResp, service, skuPath);
}

/**
 * @brief Check if SKU path has com.nvidia.Async.Set and add OEM SKU if
 * writable
 *
 * This function is called after main SKU is found (either direct or via
 * associated_SKU). It checks if the same path that provided the main SKU
 * also has com.nvidia.Async.Set interface, indicating it's writable.
 *
 * @param asyncResp   Async HTTP response
 * @param service     D-Bus service name
 * @param skuPath     Path where SKU was found
 */
inline void checkAndAddOemSKUIfWritable(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& skuPath)
{
    if constexpr (!BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        return;
    }

    BMCWEB_LOG_DEBUG(
        "Checking if {} has com.nvidia.Async.Set for OEM SKU writability",
        skuPath);

    // Check if this path has Async.Set interface
    dbus::utility::getDbusObject(
        skuPath, std::array<std::string_view, 1>{"com.nvidia.UpdateSKU"},
        std::bind_front(handleAsyncSetCheckForOemSKU, asyncResp, service,
                        skuPath));
}

/**
 * @brief Handle SKU read from associated object
 */
inline void handleAssociatedSKURead(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& associatedPath,
    const boost::system::error_code& ec, const std::string& sku)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Failed to read SKU from {}: {}", associatedPath,
                         ec.message());
        return;
    }
    if (sku.empty())
    {
        BMCWEB_LOG_DEBUG("SKU from associated object {} is empty",
                         associatedPath);
        return;
    }
    redfish::mapValidOrOmit(asyncResp->res.jsonValue, "SKU", &sku);
    // Only a real SKU (not a tombstone) can be writable via Async.Set.
    if (sku != redfish::propertyNotSupported)
    {
        checkAndAddOemSKUIfWritable(asyncResp, service, associatedPath);
    }
}

/**
 * @brief Read SKU property from associated object and set in response
 */
inline void readSKUFromAssociatedObject(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& associatedPath)
{
    BMCWEB_LOG_DEBUG(
        "Reading SKU from associated object: {} (service: {}), D-Bus call: busctl call {} {} org.freedesktop.DBus.Properties Get ss xyz.openbmc_project.Inventory.Decorator.SKU SKU",
        associatedPath, service, service, associatedPath);

    dbus::utility::getProperty<std::string>(
        service, associatedPath, "xyz.openbmc_project.Inventory.Decorator.SKU",
        "SKU",
        std::bind_front(handleAssociatedSKURead, asyncResp, service,
                        associatedPath));
}

/**
 * @brief Find service for associated object and read SKU
 */
inline void getAssociatedObjectService(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& associatedPath)
{
    BMCWEB_LOG_DEBUG(
        "Finding service for associated object: {}, looking for SKU interface",
        associatedPath);

    dbus::utility::getDbusObject(
        associatedPath,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Inventory.Decorator.SKU"},
        [asyncResp,
         associatedPath](const boost::system::error_code& ec,
                         const dbus::utility::MapperGetObject& objMap) {
            if (ec || objMap.empty())
            {
                BMCWEB_LOG_ERROR(
                    "SKU interface not found on associated object {}: {}",
                    associatedPath, ec.message());
                return;
            }

            const std::string& service = objMap.begin()->first;
            BMCWEB_LOG_DEBUG("Found service {} for associated object {}",
                             service, associatedPath);
            readSKUFromAssociatedObject(asyncResp, service, associatedPath);
        });
}

/**
 * @brief Check for backward association and read SKU from associated object
 */
inline void checkAssociatedSKU(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path)
{
    BMCWEB_LOG_DEBUG(
        "Checking for associated_SKU backward association for: {}, D-Bus call: busctl call xyz.openbmc_project.ObjectMapper {}/associated_SKU org.freedesktop.DBus.Properties Get ss xyz.openbmc_project.Association endpoints",
        path, path);

    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", path + "/associated_SKU",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, path](const boost::system::error_code& ec,
                          const std::vector<std::string>& endpoints) {
            if (ec || endpoints.empty())
            {
                BMCWEB_LOG_DEBUG(
                    "No associated_SKU for {}, SKU not available: {}", path,
                    ec.message());
                return;
            }

            std::string associatedPath = endpoints[0];
            BMCWEB_LOG_INFO("Found associated_SKU for {}, reading SKU from {}",
                            path, associatedPath);
            getAssociatedObjectService(asyncResp, associatedPath);
        });
}

/**
 * @brief Handle direct SKU property read result
 *
 * First tries to read SKU directly from the chassis. If that fails or is
 * empty, falls back to checking backward association (associated_SKU).
 */
inline void handleDirectSKURead(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path, const std::string& connectionName,
    const boost::system::error_code& ec, const std::string& chassisSKU)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG(
            "DBUS response error for SKU on {}: {} - checking backward association",
            path, ec.message());
        checkAssociatedSKU(asyncResp, path);
        return;
    }
    if (chassisSKU.empty())
    {
        // An empty property is inconclusive, so check the association fallback.
        BMCWEB_LOG_DEBUG(
            "SKU property is empty for {}, checking backward association",
            path);
        checkAssociatedSKU(asyncResp, path);
        return;
    }
    // A NOT_SUPPORTED tombstone is provider-local: omit it without erasing a
    // SKU supplied by another callback.
    redfish::mapValidOrOmit(asyncResp->res.jsonValue, "SKU", &chassisSKU);
    // Only a real SKU (not a tombstone) can be writable via Async.Set.
    if (chassisSKU != redfish::propertyNotSupported)
    {
        checkAndAddOemSKUIfWritable(asyncResp, connectionName, path);
    }
}

/**
 * @brief Get chassis SKU property
 *
 * First tries to read SKU directly from the chassis path.
 * If not available or empty, falls back to backward association
 * (associated_SKU).
 *
 * @param[in,out]   asyncResp       Async HTTP response.
 * @param[in]       connectionName  D-Bus connection name.
 * @param[in]       path            Chassis D-Bus path.
 *
 * @return None.
 */
inline void getChassisSKU(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& connectionName,
                          const std::string& path)
{
    BMCWEB_LOG_DEBUG("getChassisSKU called for chassis: {} (service: {})", path,
                     connectionName);

    // First try to read SKU directly from the chassis
    dbus::utility::getProperty<std::string>(
        connectionName, path, "xyz.openbmc_project.Inventory.Decorator.SKU",
        "SKU",
        std::bind_front(handleDirectSKURead, asyncResp, path, connectionName));
}

inline void getChassisSerialNumber(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path)
{
    dbus::utility::getProperty<std::string>(
        connectionName, path, "xyz.openbmc_project.Inventory.Decorator.Asset",
        "SerialNumber",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& serialNumber) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for SerialNumber");
                messages::internalError(asyncResp->res);
                return;
            }
            if (!serialNumber.empty())
            {
                asyncResp->res.jsonValue["SerialNumber"] = serialNumber;
            }
        });
}

/**
 * @brief Update chassis SKU using Async.Set pattern
 *
 * @param[in,out]   asyncResp      Async HTTP response.
 * @param[in]       service        D-Bus service name.
 * @param[in]       chassisPath    Chassis D-Bus path.
 * @param[in]       newSKU         New SKU value to set.
 *
 * @return None.
 */
inline void updateChassisSKU(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& chassisPath,
    const std::string& newSKU)
{
    nvidia_async_operation_utils::patch<
        nvidia_async_operation_utils::PatchGenericCallback>(
        asyncResp, service, chassisPath,
        "xyz.openbmc_project.Inventory.Decorator.SKU", "SKU", newSKU, true);
}

/**
 * @brief Handle SKU service found for PATCH operation
 */
inline void handleSKUServiceFoundForPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& associatedChassisPath, const std::string& skuValue,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& objMap)
{
    if (ec || objMap.empty())
    {
        BMCWEB_LOG_ERROR("Failed to find SKU interface on {}",
                         associatedChassisPath);
        messages::internalError(asyncResp->res);
        return;
    }

    const std::string& service = objMap.begin()->first;
    BMCWEB_LOG_DEBUG("Updating SKU on {} via async operation",
                     associatedChassisPath);
    // Use async operation utility to update SKU on the associated chassis
    // (main chassis reads it via associated_SKU association)
    // The nvidia_async_operation_utils handles the Async.Set method
    // internally
    updateChassisSKU(asyncResp, service, associatedChassisPath, skuValue);
}

/**
 * @brief Find SKU interface service and update SKU on associated chassis
 *
 * Uses nvidia_async_operation_utils::patch internally which handles the
 * com.nvidia.Async.Set interface method calls. This avoids direct coupling
 * with the Async.Set interface details.
 */
inline void findSKUServiceAndUpdateSKU(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& associatedChassisPath, const std::string& skuValue)
{
    // Look for SKU interface, not Async.Set directly
    // The nvidia_async_operation_utils::patch will handle async operations
    dbus::utility::getDbusObject(
        associatedChassisPath,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Inventory.Decorator.SKU"},
        std::bind_front(handleSKUServiceFoundForPatch, asyncResp,
                        associatedChassisPath, skuValue));
}

/**
 * @brief Handle backward association endpoints for PATCH operation
 */
inline void handleBackwardAssociationForPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisPath, const std::string& skuValue,
    const boost::system::error_code& ec,
    const std::vector<std::string>& endpoints)
{
    if (ec || endpoints.empty())
    {
        BMCWEB_LOG_ERROR("PATCH: No associated_SKU for {}, cannot update SKU",
                         chassisPath);
        messages::propertyNotWritable(asyncResp->res, "Oem/Nvidia/SKU");
        return;
    }

    // Found associated chassis via backward association
    std::string associatedChassisPath = endpoints[0];
    BMCWEB_LOG_DEBUG("PATCH: Following associated_SKU from {} to {}",
                     chassisPath, associatedChassisPath);

    findSKUServiceAndUpdateSKU(asyncResp, associatedChassisPath, skuValue);
}

/**
 * @brief Update chassis SKU via backward association
 *
 * This function follows the backward association (associated_SKU) to find
 * the target object and updates the SKU there.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       chassisPath Chassis D-Bus path.
 * @param[in]       skuValue    New SKU value to set.
 *
 * @return None.
 */
inline void updateChassisSKUViaAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisPath, const std::string& skuValue)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", chassisPath + "/associated_SKU",
        "xyz.openbmc_project.Association", "endpoints",
        std::bind_front(handleBackwardAssociationForPatch, asyncResp,
                        chassisPath, skuValue));
}

/**
 * @brief Handle direct SKU service check for PATCH operation
 */
inline void handleDirectSKUCheckForPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisPath, const std::string& skuValue,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& objMap)
{
    if (ec || objMap.empty())
    {
        // Direct path doesn't have SKU interface, try backward association
        BMCWEB_LOG_DEBUG(
            "PATCH: Direct path {} doesn't have SKU interface, checking backward association",
            chassisPath);
        updateChassisSKUViaAssociation(asyncResp, chassisPath, skuValue);
        return;
    }

    // Direct path has SKU interface, update it directly
    const std::string& service = objMap.begin()->first;
    BMCWEB_LOG_DEBUG("PATCH: Updating SKU directly on {} (service: {})",
                     chassisPath, service);
    updateChassisSKU(asyncResp, service, chassisPath, skuValue);
}

/**
 * @brief Update chassis SKU - tries direct path first, then backward
 * association
 *
 * This function first checks if the chassis path has the SKU interface.
 * If yes, updates SKU directly. If not, falls back to backward association.
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       chassisPath Chassis D-Bus path.
 * @param[in]       skuValue    New SKU value to set.
 *
 * @return None.
 */
inline void patchChassisSKU(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisPath,
                            const std::string& skuValue)
{
    BMCWEB_LOG_DEBUG("PATCH: Attempting to update SKU for {}", chassisPath);

    // First check if direct path has SKU interface
    dbus::utility::getDbusObject(
        chassisPath,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Inventory.Decorator.SKU"},
        std::bind_front(handleDirectSKUCheckForPatch, asyncResp, chassisPath,
                        skuValue));
}

/* This function implements the OEM property under
 * chassis schema.
 * It first gets the associated ErotInventoryObject then
 * it gets the inventory backed by the Erot and finally converts
 * the Dbus inventory path to the Redfish URL.
 * path: Dbus object path
 * */
inline void getChassisOEMComponentProtected(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path)
{
    std::string objPath = path + "/inventory";
    chassis_utils::getAssociationEndpoints(
        objPath, [objPath, asyncResp](const bool& status,
                                      const std::vector<std::string>& eps) {
            if (!status)
            {
                BMCWEB_LOG_DEBUG(
                    "Unable to get the association endpoint for {}", objPath);
                // inventory association is not created for
                // HMC and PcieSwitch
                // if we don't get the association
                // assumption is, it is hmc.
                asyncResp->res
                    .jsonValue["Links"]["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaChassis.v1_3_0.NvidiaChassis";
                nlohmann::json& componentsProtectedArray =
                    asyncResp->res.jsonValue["Links"]["Oem"]["Nvidia"]
                                            ["ComponentsProtected"];
                componentsProtectedArray = nlohmann::json::array();
                componentsProtectedArray.push_back(
                    {{"@odata.id",
                      "/redfish/v1/Managers/" +
                          std::string(BMCWEB_REDFISH_MANAGER_URI_NAME)}});

                return;
            }
            asyncResp->res.jsonValue["Links"]["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaChassis.v1_3_0.NvidiaChassis";
            asyncResp->res
                .jsonValue["Links"]["Oem"]["Nvidia"]["ComponentsProtected"] =
                nlohmann::json::array();
            for (const auto& ep : eps)
            {
                chassis_utils::getRedfishURL(
                    ep, [ep, asyncResp](const bool& status1,
                                        const std::string& url) {
                        if (!status1)
                        {
                            BMCWEB_LOG_DEBUG(
                                "Unable to get the Redfish URL for object={}",
                                ep);
                            return;
                        }
                        asyncResp->res
                            .jsonValue["Links"]["Oem"]["Nvidia"]
                                      ["ComponentsProtected"]
                            .push_back({{"@odata.id", url}});
                    });
            }
        });
}

inline void afterGetHardwareWriteProtectedControl(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, bool writeProtectedControl)
{
    // The object is looked up per request, so it can go away before the
    // property read completes. The property is optional, so omit it rather
    // than failing the whole chassis response.
    if (ec == boost::system::linux_error::bad_request_descriptor ||
        ec == boost::system::errc::host_unreachable)
    {
        BMCWEB_LOG_WARNING("HardwareWriteProtectedControl went away error={}",
                           ec);
        return;
    }
    if (ec)
    {
        BMCWEB_LOG_ERROR("Failed reading WriteProtectedControl error={}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    asyncResp->res.jsonValue["Oem"]["Nvidia"]["HardwareWriteProtectedControl"] =
        writeProtectedControl;
}

inline void getHardwareWriteProtectedControl(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& chassisPath)
{
    dbus::utility::getProperty<bool>(
        service, chassisPath, "com.nvidia.State.HardwareWriteProtectedControl",
        "WriteProtectedControl",
        std::bind_front(afterGetHardwareWriteProtectedControl, asyncResp));
}

inline void afterGetHardwareWriteProtectedControlObject(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisPath, const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& objectMap)
{
    if (ec || objectMap.empty())
    {
        BMCWEB_LOG_DEBUG("Chassis {} does not implement "
                         "HardwareWriteProtectedControl error={}",
                         chassisPath, ec);
        return;
    }

    if (objectMap.size() > 1)
    {
        BMCWEB_LOG_WARNING("Multiple services implement "
                           "HardwareWriteProtectedControl for {}",
                           chassisPath);
    }

    getHardwareWriteProtectedControl(asyncResp, objectMap[0].first,
                                     chassisPath);
}

inline void populateHardwareWriteProtectedControl(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisPath)
{
    static constexpr std::array<std::string_view, 1> interfaces{
        "com.nvidia.State.HardwareWriteProtectedControl"};
    dbus::utility::getDbusObject(
        chassisPath, interfaces,
        std::bind_front(afterGetHardwareWriteProtectedControlObject, asyncResp,
                        chassisPath));
}

} // namespace nvidia_chassis_utils
} // namespace redfish
