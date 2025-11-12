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
#include "trusted_components.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/conditions_utils.hpp"
#include "utils/health_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_async_set_callbacks.hpp"

#include <boost/container/flat_set.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/linux_error.hpp>
#include <boost/url/format.hpp>
#include <health.hpp>
#include <nlohmann/json.hpp>
#include <openbmc_dbus_rest.hpp>

#include <memory>
#include <string>
#include <variant>

namespace redfish
{

/**
 * @brief Get list of chassis that support in-band updates
 *
 * @param asyncResp - Pointer to object holding response data
 * @param callback Function to call with the list of chassis paths
 *                 The callback should accept std::vector<std::string> parameter
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

using AllowListMap = std::map<std::string, std::vector<std::string>>;

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
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code& ec2,
                std::variant<std::vector<std::string>>& resp) {
            if (ec2)
            {
                return; // no chassis = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            nlohmann::json& linksArray =
                aResp->res.jsonValue["Links"]["Contains"];
            linksArray = nlohmann::json::array();
            boost::container::flat_set<std::string> chassisNames;
            for (const std::string& chassisPath : *data)
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
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
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
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code& ec2,
                std::variant<std::vector<std::string>>& resp) {
            if (ec2)
            {
                return; // no chassis = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            aResp->res.jsonValue["Links"]["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaChassis.v1_7_0.NvidiaSMAChassis";
            nlohmann::json& protocalBridgeArray =
                aResp->res.jsonValue["Links"]["Oem"]["Nvidia"]
                                    ["ProtocolBridgeForDevices"];
            protocalBridgeArray = nlohmann::json::array();
            boost::container::flat_set<std::string> chassisNames;
            for (const std::string& chassisPath : *data)
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
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/bridging_processor",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
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
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec,
                std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no chassis = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            aResp->res.jsonValue["Links"]["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaChassis.v1_7_0.NvidiaSMAChassis";
            nlohmann::json& protocalBridgeArray =
                aResp->res.jsonValue["Links"]["Oem"]["Nvidia"]
                                    ["ProtocolBridgeForDevices"];
            protocalBridgeArray = nlohmann::json::array();
            for (const std::string& chassisPath : *data)
            {
                crow::connections::systemBus->async_method_call(
                    [aResp, &protocalBridgeArray, chassisPath](
                        const boost::system::error_code& ec2,
                        std::variant<std::vector<std::string>>& resp2) {
                        if (ec2)
                        {
                            return; // no chassis = no failures
                        }
                        std::vector<std::string>* data2 =
                            std::get_if<std::vector<std::string>>(&resp2);
                        if (data2 == nullptr)
                        {
                            return;
                        }
                        for (const std::string& networkAdapterPath : *data2)
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
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    chassisPath + "/network_adapters",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/bridging_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
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
    crow::connections::systemBus->async_method_call(
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
                    "#NvidiaChassis.v1_11_0.NvidiaSMAChassis";
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

inline void getBootReasonProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& resetObjPath)
{
    using PropertiesMap = boost::container::flat_map<
        std::string, std::variant<std::vector<std::string>, double>>;

    crow::connections::systemBus->async_method_call(
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
    crow::connections::systemBus->async_method_call(
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
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code& ec,
                std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no association = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            aResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaChassis.v1_10_0.NvidiaSMAChassis";

            for (const std::string& resetObjPath : *data)
            {
                getResetCounterMetricsObject(aResp, resetObjPath);
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/reset_statistics",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getHealthByAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath, const std::string& association,
    const std::string& objId)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, objId](const boost::system::error_code& ec,
                           std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                // no state sensors attached.
                return;
            }

            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            for (const std::string& sensorPath : *data)
            {
                if (!sensorPath.ends_with(objId))
                {
                    continue;
                }
                // Check Interface in Object or not
                crow::connections::systemBus->async_method_call(
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
                        crow::connections::systemBus->async_method_call(
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
                                        if constexpr (
                                            !BMCWEB_DISABLE_HEALTH_ROLLUP)
                                        {
                                            asyncResp->res
                                                .jsonValue["Status"]
                                                          ["HealthRollup"] =
                                                "OK";
                                        }
                                        // update health
                                        if constexpr (
                                            BMCWEB_HEALTH_ROLLUP_ALTERNATIVE)
                                        {
                                            std::shared_ptr<HealthRollup>
                                                health = std::make_shared<
                                                    HealthRollup>(
                                                    sensorPath,
                                                    [asyncResp](
                                                        const std::string&
                                                            rootHealth,
                                                        const std::string&
                                                            healthRollup) {
                                                        asyncResp->res.jsonValue
                                                            ["Status"]
                                                            ["Health"] =
                                                            rootHealth;
                                                        if constexpr (
                                                            !BMCWEB_DISABLE_HEALTH_ROLLUP)
                                                        {
                                                            asyncResp->res.jsonValue
                                                                ["Status"]
                                                                ["HealthRollup"] =
                                                                healthRollup;
                                                        }
                                                    });
                                            health->start();
                                        }
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
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/" + association,
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
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
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code& ec2,
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
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_processors",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
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
    crow::connections::systemBus->async_method_call(
        [aResp, objPath](const boost::system::error_code& ec2,
                         std::variant<std::vector<std::string>>& resp) {
            if (ec2)
            {
                return; // no fabric = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr || data->size() > 1)
            {
                // There must be single fabric
                return;
            }
            const std::string& fabricPath = data->front();
            sdbusplus::message::object_path objectPath(fabricPath);
            std::string fabricId = objectPath.filename();
            if (fabricId.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            // Get the switches
            crow::connections::systemBus->async_method_call(
                [aResp,
                 fabricId](const boost::system::error_code& ec1,
                           std::variant<std::vector<std::string>>& resp1) {
                    if (ec1)
                    {
                        return; // no switches = no failures
                    }
                    std::vector<std::string>* data1 =
                        std::get_if<std::vector<std::string>>(&resp1);
                    if (data1 == nullptr)
                    {
                        return;
                    }
                    // Sort the switches links
                    std::sort(data1->begin(), data1->end());
                    nlohmann::json& linksArray =
                        aResp->res.jsonValue["Links"]["Switches"];
                    linksArray = nlohmann::json::array();
                    for (const std::string& switchPath : *data1)
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
                },
                "xyz.openbmc_project.ObjectMapper", objPath + "/all_switches",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Association", "endpoints");
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/fabrics",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
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
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
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

            std::array<uint8_t, trayTopologyByteLength> byteArray{};
            for (size_t i = 0; i < trayTopologyByteLength; i++)
            {
                byteArray[i] = static_cast<uint8_t>(
                    std::stoi(property.substr((i * trayTopologyTokenLength),
                                              trayTopologyTokenLength),
                              nullptr, 16));
            }

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
                std::variant<std::vector<std::string>>& assoc) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Cannot get association");
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&assoc);
            if (data == nullptr)
            {
                return;
            }
            const std::string& fruPath = data->front();
            crow::connections::systemBus->async_method_call(
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
                    crow::connections::systemBus->async_method_call(
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
                             std::variant<std::vector<std::string>>& assoc) {
            if (ec)
            {
                messages::internalError(aResp->res);
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&assoc);
            if (data == nullptr)
            {
                return;
            }
            const std::string& fruPath = data->front();
            crow::connections::systemBus->async_method_call(
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
                        crow::connections::systemBus->async_method_call(
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
                        crow::connections::systemBus->async_method_call(
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
                            std::variant<std::vector<std::string>>& assoc) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Cannot get association");
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&assoc);
            if (data == nullptr)
            {
                return;
            }
            const std::string& fruPath = data->front();
            crow::connections::systemBus->async_method_call(
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
                    crow::connections::systemBus->async_method_call(
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
inline void getOemHdwWriteProtectInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Baseboard Hardware write protect info");
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code& ec,
                const std::vector<std::pair<
                    std::string, std::variant<std::string, bool, uint64_t>>>&
                    propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for "
                                 "Baseboard Hardware write protect info");
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& property : propertiesList)
            {
                if (property.first == "WriteProtected")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for hardware write protected");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res
                        .jsonValue["Oem"]["Nvidia"]["HardwareWriteProtected"] =
                        *value;
                }

                if (property.first == "WriteProtectedControl")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG(
                            "Null value returned "
                            "for hardware write protected control");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["Oem"]["Nvidia"]
                                        ["HardwareWriteProtectedControl"] =
                        *value;
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Software.Settings");
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
    crow::connections::systemBus->async_method_call(
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
    crow::connections::systemBus->async_method_call(
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
    double temperature)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath, cpuClockFrequency, workloadFactor, temperature](
            const boost::system::error_code& errorno,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
            if (errorno)
            {
                return;
            }

            for (const auto& [service, interfaces] : objInfo)
            {
                crow::connections::systemBus->async_method_call(
                    [asyncResp, objPath, service{service}, cpuClockFrequency,
                     workloadFactor, temperature](
                        const boost::system::error_code& errorno2,
                        const std::vector<
                            std::pair<std::string,
                                      std::variant<double, std::string, bool>>>&
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

                        crow::connections::systemBus->async_method_call(
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
                            temperature);
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
    double workloadFactor, double temperature)
{
    // get endpoints of chassisId/all_controls
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisObjPath, cpuClockFrequency, workloadFactor,
         temperature](const boost::system::error_code&,
                      std::variant<std::vector<std::string>>& resp) {
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            for (const auto& objPath : *data)
            {
                setStaticPowerHintByObjPath(asyncResp, objPath,
                                            cpuClockFrequency, workloadFactor,
                                            temperature);
            }
        },
        "xyz.openbmc_project.ObjectMapper", chassisObjPath + "/all_controls",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getStaticPowerHintByObjPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath)
{
    crow::connections::systemBus->async_method_call(
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
                crow::connections::systemBus->async_method_call(
                    [asyncResp, objPath](
                        const boost::system::error_code& errorno4,
                        const std::vector<
                            std::pair<std::string,
                                      std::variant<double, std::string, bool>>>&
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
    crow::connections::systemBus->async_method_call(
        [asyncResp,
         chassisObjPath](const boost::system::error_code&,
                         std::variant<std::vector<std::string>>& resp) {
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            for (const auto& objPath : *data)
            {
                getStaticPowerHintByObjPath(asyncResp, objPath);
            }
        },
        "xyz.openbmc_project.ObjectMapper", chassisObjPath + "/all_controls",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
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

    const std::array<const char*, 1> networkInterfaces = {
        "xyz.openbmc_project.Inventory.Item.NetworkInterface"};

    crow::connections::systemBus->async_method_call(
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
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", objPath, 0,
        networkInterfaces);
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
    crow::connections::systemBus->async_method_call(
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
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, object[0].first,
        sdbusplus::message::object_path("/xyz/openbmc_project/software") /=
        chassisId,
        "xyz.openbmc_project.Software.Settings", "WriteProtected",
        [asyncResp](const boost::system::error_code& ec2,
                    const bool& property) {
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
    sdbusplus::asio::setProperty(
        *crow::connections::systemBus, object[0].first,
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
            break;
        }
    }
    callback(chassisPath);
}

template <typename Callback>
void getValidLeakDetectionPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, Callback&& callback)
{
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Configuration.VoltageLeakDetector"};

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
    const std::string* prettyName = nullptr;
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

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), propertiesList, "PartNumber",
        partNumber, "SerialNumber", serialNumber, "Manufacturer", manufacturer,
        "Model", model, "SparePartNumber", sparePartNumber, "UUID", uuid,
        "LocationCode", locationCode, "LocationType", locationType,
        "PrettyName", prettyName, "Type", type, "Height", height, "Width",
        width, "Depth", depth, "MinPowerWatts", minPowerWatts, "MaxPowerWatts",
        maxPowerWatts, "AssetTag", assetTag, "WriteProtected", writeProtected,
        "WriteProtectedControl", writeProtectedControl,
        "PCIeReferenceClockCount", pCIeReferenceClockCount, "LocationContext",
        locationContext);

    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    if (partNumber != nullptr)
    {
        asyncResp->res.jsonValue["PartNumber"] = *partNumber;
    }

    if (serialNumber != nullptr)
    {
        asyncResp->res.jsonValue["SerialNumber"] = *serialNumber;
    }

    if (manufacturer != nullptr)
    {
        asyncResp->res.jsonValue["Manufacturer"] = *manufacturer;
    }

    if (model != nullptr)
    {
        asyncResp->res.jsonValue["Model"] = *model;
    }

    // SparePartNumber is optional on D-Bus
    // so skip if it is empty
    if (sparePartNumber != nullptr && !sparePartNumber->empty())
    {
        asyncResp->res.jsonValue["SparePartNumber"] = *sparePartNumber;
    }

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
    if (locationContext != nullptr)
    {
        asyncResp->res.jsonValue["Location"]["PartLocationContext"] =
            *locationContext;
    }
    if (prettyName != nullptr)
    {
        asyncResp->res.jsonValue["Name"] = *prettyName;
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
        oem["@odata.type"] = "#NvidiaChassis.v1_6_0.NvidiaChassis";

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

        if (pCIeReferenceClockCount != nullptr)
        {
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["PCIeReferenceClockCount"] =
                *pCIeReferenceClockCount;
        }

        if constexpr (BMCWEB_REDFISH_LEAK_DETECT)
        {
            // Policy Collection
            getValidLeakDetectionPath(asyncResp, chassisId,
                                      std::bind_front(doLeakDetectionPolicyGet,
                                                      asyncResp, chassisId));
        }
    }
    asyncResp->res.jsonValue["Name"] = chassisId;
    asyncResp->res.jsonValue["Id"] = chassisId;
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
    asyncResp->res.jsonValue["Status"]["State"] = "Enabled";

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

    // TrustedComponent collection
    getChassisAssociatedEndpoint(
        asyncResp, chassisId,
        [asyncResp,
         chassisId]([[maybe_unused]] const std::string& endpoint, bool exists) {
            if (exists)
            {
                // SPDM endpoint exists, add TrustedComponents link
                asyncResp->res.jsonValue["TrustedComponents"]["@odata.id"] =
                    boost::urls::format(
                        "/redfish/v1/Chassis/{}/TrustedComponents", chassisId);
            }
            else
            {
                redfish::chassis_utils::getValidChassisPath(
                    asyncResp, chassisId,
                    [asyncResp, chassisId](
                        const std::optional<std::string>& validChassisPath) {
                        checkTPMComponentsAndAddLink(asyncResp, chassisId,
                                                     validChassisPath);
                    });
            }
        });

    // Controls Collection
    asyncResp->res.jsonValue["Controls"] = {
        {"@odata.id", "/redfish/v1/Chassis/" + chassisId + "/Controls"}};

    nlohmann::json::array_t computerSystems;
    nlohmann::json::object_t system;
    system["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME);
    computerSystems.emplace_back(std::move(system));
    asyncResp->res.jsonValue["Links"]["ComputerSystems"] =
        std::move(computerSystems);

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

inline void getChassisAssetData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Chassis Asset Data");
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, objPath,
        "xyz.openbmc_project.Inventory.Decorator.Asset",
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

            if (serialNumber != nullptr && !serialNumber->empty())
            {
                asyncResp->res.jsonValue["SerialNumber"] = *serialNumber;
            }

            if ((model != nullptr) && !model->empty())
            {
                asyncResp->res.jsonValue["Model"] = *model;
            }

            if (partNumber != nullptr)
            {
                asyncResp->res.jsonValue["PartNumber"] = *partNumber;
            }

            if ((manufacturer != nullptr) && !manufacturer->empty())
            {
                asyncResp->res.jsonValue["Manufacturer"] = *manufacturer;
            }
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
                // The path should end with chassisId (representing resource)
                // and that path should implement Asset Interface
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

inline void getIntrusionByService(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get intrusion status by service ");

    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, objPath,
        "xyz.openbmc_project.Chassis.Intrusion", "Status",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& value) {
            if (ec)
            {
                // do not add err msg in redfish response, because this is not
                //     mandatory property
                BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                return;
            }

            asyncResp->res
                .jsonValue["PhysicalSecurity"]["IntrusionSensorNumber"] = 1;
            asyncResp->res.jsonValue["PhysicalSecurity"]["IntrusionSensor"] =
                value;
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
                // do not add err msg in redfish response, because this is not
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

enum class InBandOption
{
    BackgroundCopyStatus,
    setBackgroundCopyEnabled,
    setInBandEnabled,
};

inline AllowListMap getRoTChassisAllowListMap()
{
    using Json = nlohmann::json;
    namespace fs = std::filesystem;
    static std::map<std::string, std::vector<std::string>> allowListMap{};

    if (not allowListMap.empty())
    {
        return allowListMap;
    }

    std::string configPath(BMCWEB_FW_ROT_CHASSIS_ALLOWLIST_JSON);
    if (!fs::exists(configPath))
    {
        BMCWEB_LOG_ERROR("The file doesn't exist: {}", configPath);
        return allowListMap;
    }
    BMCWEB_LOG_DEBUG("Found config file path {}", configPath);

    std::ifstream jsonFile(configPath);
    auto data = Json::parse(jsonFile, nullptr, false);
    if (data.is_discarded())
    {
        BMCWEB_LOG_ERROR("Unable to parse json data {}", configPath);
        return allowListMap;
    }

    allowListMap = data.get<std::map<std::string, std::vector<std::string>>>();
    return allowListMap;
}

/**
 *@brief Function to check if the chassis id belongs in
 *       the allow list for the property
 *
 * @param chassisId   Chassis Id
 * @param property   Chassis property to check for
 *
 * @return bool True if the id is present in the allow list,
 *              False otherwise
 */
inline bool isChassisIdInAllowList(const std::string& chassisId,
                                   const std::string& property)
{
    const auto& allowListMap = getRoTChassisAllowListMap();
    if (!allowListMap.contains(property))
    {
        return false;
    }
    return (std::find(allowListMap.at(property).begin(),
                      allowListMap.at(property).end(), chassisId) !=
            allowListMap.at(property).end());
}

/**
 *@brief handles all calls to ERoT mctp UUID for
 *      setBackgroundCopyEnabled, setInBandEnabled,
 *      and getBackgroundCopyAndInBandInfo
 *
 * @param req   Pointer to object holding request data
 * @param asyncResp   Pointer to object holding response data
 * @param chassisUUID  Chassis UUID
 * @param option Determines which method will be called
 * @param enabled Enable or disable the background copy
 * @param chassisID  Chassis ID

 *
 * @return None.
 */

inline void handleMctpInBandActions(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisUUID, const InBandOption option,
    bool enabled = false, const std::string& chassisId = "")
{
    constexpr std::array<std::string_view, 1> interfaces = {
        "org.freedesktop.DBus.ObjectManager"};

    switch (option)
    {
        case InBandOption::BackgroundCopyStatus:
        {
            nlohmann::json& oem = asyncResp->res.jsonValue["Oem"]["Nvidia"];
            oem["@odata.type"] = "#NvidiaChassis.v1_3_0.NvidiaRoTChassis";

            redfish::getChassisListForInBand(
                asyncResp,
                [asyncResp, chassisId](const std::vector<std::string>&
                                           inbandUpdatePolicyAllowList) {
                    updateInBandEnabled(asyncResp, inbandUpdatePolicyAllowList,
                                        chassisId);
                });

            break;
        }
        case InBandOption::setInBandEnabled:
        {
            redfish::getChassisListForInBand(
                asyncResp, [asyncResp, enabled, chassisId](
                               const std::vector<std::string>&
                                   inbandUpdatePolicyAllowList) {
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
            return;
        }
        case InBandOption::setBackgroundCopyEnabled:
        {
            // This case is handled in the nested switch statement below
            break;
        }
        default:
        {
            // Handle unexpected enum values
            BMCWEB_LOG_DEBUG("Unexpected InBandOption enum value: {}",
                             static_cast<int>(option));
            messages::internalError(asyncResp->res);
            return;
        }
    }

    dbus::utility::getDbusObject(
        "/au/com/codeconstruct/mctp1", interfaces,
        [&req, asyncResp, chassisId, chassisUUID, option,
         enabled](const boost::system::error_code& ec,
                  const dbus::utility::MapperGetObject& resp) mutable {
            if (ec || resp.empty())
            {
                BMCWEB_LOG_WARNING(
                    "DBUS response error during getting of service name: {}",
                    ec);
                return;
            }
            auto chassisProcessed = std::make_shared<bool>(false);
            for (const auto& it : resp)
            {
                if (*chassisProcessed)
                {
                    return;
                }
                std::string serviceName = it.first;
                crow::connections::systemBus->async_method_call(
                    [&req, asyncResp, chassisUUID, serviceName, option, enabled,
                     chassisId,
                     chassisProcessed](const boost::system::error_code& ec2,
                                       const dbus::utility::ManagedObjectType&
                                           managedObjects) {
                        if (ec2)
                        {
                            BMCWEB_LOG_DEBUG(
                                "DBUS response error for MCTP.Control");
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        const uint8_t* eid = nullptr;
                        const std::string* uuid = nullptr;
                        const std::vector<uint8_t>* supportedMsgTypes = nullptr;
                        bool foundEID = false;

                        for (const auto& objectPath : managedObjects)
                        {
                            bool isMctpEp = false;
                            for (const auto& interfaceMap : objectPath.second)
                            {
                                if (interfaceMap.first ==
                                    "xyz.openbmc_project.Common.UUID")
                                {
                                    for (const auto& propertyMap :
                                         interfaceMap.second)
                                    {
                                        if (propertyMap.first == "UUID")
                                        {
                                            uuid = std::get_if<std::string>(
                                                &propertyMap.second);
                                        }
                                    }
                                }

                                if (interfaceMap.first ==
                                    "xyz.openbmc_project.MCTP.Endpoint")
                                {
                                    isMctpEp = true;
                                    for (const auto& propertyMap :
                                         interfaceMap.second)
                                    {
                                        if (propertyMap.first == "EID")
                                        {
                                            eid = std::get_if<uint8_t>(
                                                &propertyMap.second);
                                        }
                                        else if (propertyMap.first ==
                                                 "SupportedMessageTypes")
                                        {
                                            supportedMsgTypes = std::get_if<
                                                std::vector<uint8_t>>(
                                                &propertyMap.second);
                                        }
                                    }
                                }
                            }

                            // only check EID and UUID if it's a MCTP
                            // endpoint
                            if (!isMctpEp)
                            {
                                continue;
                            }

                            // only check EID if it's a MCTP endpoint
                            if (eid == nullptr)
                            {
                                BMCWEB_LOG_DEBUG(
                                    "handleMctpInBandActions: EID not found");
                                continue;
                            }
                            if (uuid && (*uuid) == chassisUUID &&
                                supportedMsgTypes)
                            {
                                if (std::find(supportedMsgTypes->begin(),
                                              supportedMsgTypes->end(),
                                              mctpTypeVDMIANA) !=
                                    supportedMsgTypes->end())
                                {
                                    foundEID = true;
                                    break;
                                }
                            }
                        }

                        if (foundEID and not *chassisProcessed)
                        {
                            *chassisProcessed = true;
                            switch (option)
                            {
                                case InBandOption::BackgroundCopyStatus:
                                {
                                    const auto& allowListMap =
                                        getRoTChassisAllowListMap();
                                    nlohmann::json& oem =
                                        asyncResp->res
                                            .jsonValue["Oem"]["Nvidia"];
                                    oem["@odata.type"] =
                                        "#NvidiaChassis.v1_3_0.NvidiaRoTChassis";

                                    // Calling the following methods,
                                    // updateInBandEnabled,
                                    // updateBackgroundCopyEnabled, and
                                    // updateBackgroundCopyStatus
                                    // asynchronously, may cause
                                    // unpredictable behavior. These methods
                                    // use 'mctp-vdm-util', which is not
                                    // designed to handle more than one
                                    // request at the same time. Running
                                    // more than one command simultaneously
                                    // may result in output from a previous
                                    // (or another) request. The fix
                                    // addresses this issue by changing the
                                    // way the functions are called,
                                    // simulating synchronous execution by
                                    // invoking each command sequentially
                                    // instead of simultaneously.

                                    uint32_t endpointId = *eid;
                                    if (allowListMap.empty())
                                    {
                                        break;
                                    }

                                    std::vector<std::string>
                                        automaticBackgroundCopyAllowList{};
                                    std::vector<std::string>
                                        backgroundCopyStatusAllowList{};

                                    if (allowListMap.contains(
                                            "AutomaticBackgroundCopyEnabled"))
                                    {
                                        automaticBackgroundCopyAllowList =
                                            allowListMap.at(
                                                "AutomaticBackgroundCopyEnabled");
                                    }
                                    if (allowListMap.contains(
                                            "BackgroundCopyStatus"))
                                    {
                                        backgroundCopyStatusAllowList =
                                            allowListMap.at(
                                                "BackgroundCopyStatus");
                                    }
                                    updateBackgroundCopyEnabled(
                                        req, asyncResp, endpointId,
                                        automaticBackgroundCopyAllowList,
                                        chassisId,
                                        [&req, asyncResp, endpointId,
                                         backgroundCopyStatusAllowList,
                                         chassisId]() {
                                            updateBackgroundCopyStatus(
                                                req, asyncResp, endpointId,
                                                backgroundCopyStatusAllowList,
                                                chassisId);
                                        });
                                    break;
                                }
                                case InBandOption::setBackgroundCopyEnabled:
                                    if (isChassisIdInAllowList(
                                            chassisId,
                                            "AutomaticBackgroundCopyEnabled"))
                                    {
                                        enableBackgroundCopy(req, asyncResp,
                                                             *eid, enabled,
                                                             chassisId);
                                        break;
                                    }
                                    messages::propertyUnknown(
                                        asyncResp->res,
                                        "AutomaticBackgroundCopyEnabled");
                                    return;
                                case InBandOption::setInBandEnabled:
                                    return;
                                default:
                                    BMCWEB_LOG_DEBUG(
                                        "Invalid enum provided for inNand mctp access");
                                    break;
                            }
                        }
                    },
                    serviceName, "/au/com/codeconstruct/mctp1",
                    "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
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
            crow::connections::systemBus->async_method_call(
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
 *@brief Sets the background copy for particular chassis
 *
 * @param req   Pointer to object holding request data
 * @param asyncResp   Pointer to object holding response data
 * @param chassisUUID  Chassis ID
 * @param enabled Enable or disable the background copy
 *
 * @return None.
 */
inline void setBackgroundCopyEnabled(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& chassisUUID, bool enabled)
{
    handleMctpInBandActions(req, asyncResp, chassisUUID,
                            InBandOption::setBackgroundCopyEnabled, enabled,
                            chassisId);
}

/**
 *@brief Sets in-band for particular chassis
 *
 * @param req   Pointer to object holding request data
 * @param asyncResp   Pointer to object holding response data
 * @param chassisUUID  Chassis ID
 *
 * @return None.
 */
inline void setInBandEnabled(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& chassisUUID, bool enabled)
{
    handleMctpInBandActions(req, asyncResp, chassisUUID,
                            InBandOption::setInBandEnabled, enabled, chassisId);
}

/**
 *@brief Gets background copy and in-band info for particular chassis
 *
 * @param req   Pointer to object holding request data
 * @param asyncResp   Pointer to object holding response data
 * @param chassisUUID  Chassis UUID
 * @param chassisID  Chassis ID
 *
 * @return None.
 */
inline void getBackgroundCopyAndInBandInfo(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisUUID, const std::string& chassisId)
{
    handleMctpInBandActions(req, asyncResp, chassisUUID,
                            InBandOption::BackgroundCopyStatus, false,
                            chassisId);
}

/**
 * @brief Get the Chassis UUID
 *
 * @param req - Pointer to object holding request data
 * @param asyncResp - Pointer to object holding response data
 * @param connectionName - connection name
 * @param path - D-Bus path
 * @param isERoT - true: ERoT resource. false: not a ERoT
 */
inline void getChassisUUID(const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& path, bool isERoT = false)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Common.UUID", "UUID",
        [&req, asyncResp, isERoT, path](const boost::system::error_code& ec,
                                        const std::string& chassisUUID) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for UUID");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["UUID"] = chassisUUID;

            if (isERoT)
            {
                auto chassisId =
                    sdbusplus::message::object_path(path).filename();
                getBackgroundCopyAndInBandInfo(req, asyncResp, chassisUUID,
                                               chassisId);
            }
        });
}

inline void getChassisName(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Item", "PrettyName",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& chassisName) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for chassis name");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["Name"] = chassisName;
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
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Item.Chassis", "Type",
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
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Item.Chassis", "Type",
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

inline void populateHealthRollupAndDeviceStatus(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath, const std::string& chassisId)
{
    if constexpr (BMCWEB_HEALTH_ROLLUP_ALTERNATIVE)
    {
        std::shared_ptr<HealthRollup> health = std::make_shared<HealthRollup>(
            objPath, [asyncResp](const std::string& rootHealth,
                                 const std::string& healthRollup) {
                asyncResp->res.jsonValue["Status"]["Health"] = rootHealth;
                if constexpr (!BMCWEB_DISABLE_HEALTH_ROLLUP)
                {
                    asyncResp->res.jsonValue["Status"]["HealthRollup"] =
                        healthRollup;
                }
            });
        health->start();
    }

    if constexpr (BMCWEB_NVIDIA_OEM_DEVICE_STATUS_FROM_FILE)
    {
        /** NOTES: This is a temporary solution to avoid performance
         * issues may impact other Redfish services. Please call for
         * architecture decisions from all NvBMC teams if want to use it
         * in other places.
         */

        if constexpr (BMCWEB_HEALTH_ROLLUP_ALTERNATIVE)
        {
            // #error "Conflicts! Please set
            // health-rollup-alternative=disabled."
        }

        if constexpr (BMCWEB_DISABLE_HEALTH_ROLLUP)
        {
            // #error "Conflicts! Please set
            // disable-health-rollup=disabled."
        }

        health_utils::getDeviceHealthInfo(asyncResp->res, chassisId);
    }
}

template <typename InterfacesContainer>
inline void populateChassisLinksOemAndStatus(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath, const std::string& path,
    const InterfacesContainer& interfaces2, const std::string& chassisId)
{
    if constexpr (!BMCWEB_DISABLE_CONDITIONS_ARRAY)
    {
        redfish::conditions_utils::populateServiceConditions(asyncResp,
                                                             chassisId);
    }
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
    redfish::nvidia_chassis_utils::getPhysicalSecurityData(asyncResp);
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
    std::optional<double>& cpuClockFrequency,
    std::optional<double>& workloadFactor, std::optional<double>& temperature,
    std::optional<std::string>& oemSKU)
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
                    hardwareWriteProtectEnable, "SKU", oemSKU);

                if (staticPowerHintJsonObj)
                {
                    std::optional<nlohmann::json> cpuClockFrequencyHzJsonObj;
                    std::optional<nlohmann::json> temperatureCelsiusJsonObj;
                    std::optional<nlohmann::json> workloadFactorJsonObj;
                    json_util::readJson(
                        *staticPowerHintJsonObj, asyncResp->res,
                        "CpuClockFrequencyHz", cpuClockFrequencyHzJsonObj,
                        "TemperatureCelsius", temperatureCelsiusJsonObj,
                        "WorkloadFactor", workloadFactorJsonObj);
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
                }
            }
        }
    }
}

inline void applyOemChassisPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& path,
    const std::optional<bool>& hardwareWriteProtectEnable,
    const std::optional<std::string>& partNumber,
    const std::optional<std::string>& serialNumber,
    const std::optional<double>& cpuClockFrequency,
    const std::optional<double>& workloadFactor,
    const std::optional<double>& temperature)
{
    if (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        if (hardwareWriteProtectEnable)
        {
            redfish::nvidia_chassis_utils::oemChassisHardwareWriteProtectEnable(
                asyncResp, chassisId, *hardwareWriteProtectEnable);
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
        if (cpuClockFrequency || workloadFactor || temperature)
        {
            if (cpuClockFrequency && workloadFactor && temperature)
            {
                redfish::nvidia_chassis_utils::setStaticPowerHintByChassis(
                    asyncResp, path, *cpuClockFrequency, *workloadFactor,
                    *temperature);
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
            }
        }
    }
}

inline void handleAuxPowerResetAction(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::string resetType;
    if (!json_util::readJsonAction(req, asyncResp->res, "ResetType", resetType))
    {
        return;
    }

    if (resetType != "AuxPowerCycle" && resetType != "AuxPowerCycleForce")
    {
        messages::actionParameterValueError(asyncResp->res, "ResetType",
                                            "NvidiaChassis.AuxPowerReset");
        return;
    }

    if (resetType == "AuxPowerCycle")
    {
        // check power status
        sdbusplus::asio::getProperty<std::string>(
            *crow::connections::systemBus, "xyz.openbmc_project.State.Host",
            "/xyz/openbmc_project/state/host0",
            "xyz.openbmc_project.State.Host", "CurrentHostState",
            [asyncResp](const boost::system::error_code& ec,
                        const std::string& hostState) {
                if (ec)
                {
                    if (ec == boost::system::errc::host_unreachable)
                    {
                        // Service not available, no error, just don't
                        // return host state info
                        BMCWEB_LOG_DEBUG("Service not available {}", ec);
                        return;
                    }
                    messages::internalError(asyncResp->res);
                    return;
                }
                if (hostState == "xyz.openbmc_project.State.Host.HostState.Off")
                {
                    crow::connections::systemBus->async_method_call(
                        [asyncResp](const boost::system::error_code& ec2) {
                            if (ec2)
                            {
                                BMCWEB_LOG_DEBUG("DBUS response error {}", ec2);
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            messages::success(asyncResp->res);
                        },
                        "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                        "org.freedesktop.systemd1.Manager", "StartUnit",
                        "nvidia-aux-power.service", "replace");
                }
                else
                {
                    messages::chassisPowerStateOffRequired(asyncResp->res, "0");
                }
            });
    }
    else
    {
        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code& ec) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                    messages::internalError(asyncResp->res);
                    return;
                }
            },
            "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
            "org.freedesktop.systemd1.Manager", "StartUnit",
            "nvidia-aux-power-force.service", "replace");
    }
}

template <typename CallbackFunc>
inline void isEROTChassis(const std::string& chassisID, CallbackFunc&& callback)
{
    const std::array<const char*, 1> interfaces = {
        "xyz.openbmc_project.Inventory.Item.SPDMResponder"};

    crow::connections::systemBus->async_method_call(
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
            sdbusplus::asio::getProperty<Associations>(
                *crow::connections::systemBus, serviceName, objIt->first,
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
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0, interfaces);
}

inline void getChassisManufacturer(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.Asset", "Manufacturer",
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
 * @brief Read SKU property from associated object and set in response
 */
inline void readSKUFromAssociatedObject(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& associatedPath)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, associatedPath,
        "xyz.openbmc_project.Inventory.Decorator.SKU", "SKU",
        [asyncResp, associatedPath](const boost::system::error_code& ec,
                                    const std::string& sku) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Failed to read SKU from {}", associatedPath);
                return;
            }
            if (!sku.empty())
            {
                BMCWEB_LOG_DEBUG("Setting SKU from associated object {}: {}",
                                 associatedPath, sku);
                asyncResp->res.jsonValue["SKU"] = sku;
            }
        });
}

/**
 * @brief Find service for associated object and read SKU
 */
inline void getAssociatedObjectService(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& associatedPath)
{
    dbus::utility::getDbusObject(
        associatedPath,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Inventory.Decorator.SKU"},
        [asyncResp,
         associatedPath](const boost::system::error_code& ec,
                         const dbus::utility::MapperGetObject& objMap) {
            if (ec || objMap.empty())
            {
                BMCWEB_LOG_DEBUG(
                    "SKU interface not found on associated object {}",
                    associatedPath);
                return;
            }

            const std::string& service = objMap.begin()->first;
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
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", path + "/associated_SKU",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, path](const boost::system::error_code& ec,
                          const std::vector<std::string>& endpoints) {
            if (ec || endpoints.empty())
            {
                BMCWEB_LOG_DEBUG("No associated_SKU for {}, SKU not available",
                                 path);
                return;
            }

            std::string associatedPath = endpoints[0];
            BMCWEB_LOG_DEBUG("Found associated_SKU for {}, reading SKU from {}",
                             path, associatedPath);
            getAssociatedObjectService(asyncResp, associatedPath);
        });
}

inline void getChassisSKU(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& connectionName,
                          const std::string& path)
{
    // Extract chassis name from path for checking if it's an IRoT chassis
    sdbusplus::message::object_path objPath(path);
    std::string chassisName = objPath.filename();

    // Don't show SKU for IRoT chassis - they provide SKU to other chassis
    if (chassisName.find("IRoT") != std::string::npos)
    {
        BMCWEB_LOG_DEBUG("Skipping SKU for IRoT chassis: {}", path);
        return;
    }

    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.SKU", "SKU",
        [asyncResp, path](const boost::system::error_code& ec,
                          const std::string& chassisSKU) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG(
                    "DBUS response error for SKU on {}: {} - checking backward association",
                    path, ec.message());
                checkAssociatedSKU(asyncResp, path);
                return;
            }
            if (!chassisSKU.empty())
            {
                BMCWEB_LOG_DEBUG("Setting SKU for {}: {}", path, chassisSKU);
                asyncResp->res.jsonValue["SKU"] = chassisSKU;
            }
            else
            {
                BMCWEB_LOG_DEBUG("SKU property is empty for {}", path);
            }
        });
}

inline void getChassisSerialNumber(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.Asset", "SerialNumber",
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
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.SKU", "SKU",
        [asyncResp,
         path](const boost::system::error_code& ec, const std::string& sku) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error for OEM Nvidia SKU");
                return;
            }
            if (!sku.empty())
            {
                asyncResp->res.jsonValue["Oem"]["Nvidia"]["SKU"] = sku;
            }
        });
}

/**
 * @brief Check if associated chassis has Async.Set interface and read OEM SKU
 * if available
 */
inline void checkAsyncSetAndReadSKU(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& associatedChassisPath)
{
    constexpr std::array<std::string_view, 1> interfaces = {
        "com.nvidia.Async.Set"};

    dbus::utility::getDbusObject(
        associatedChassisPath, interfaces,
        [asyncResp,
         associatedChassisPath](const boost::system::error_code& ec,
                                const dbus::utility::MapperGetObject& objMap) {
            if (ec || objMap.empty())
            {
                BMCWEB_LOG_DEBUG("Async.Set interface not found on {}",
                                 associatedChassisPath);
                return;
            }

            // Associated chassis has Async.Set, add OEM SKU to main chassis
            const std::string& serviceName = objMap.begin()->first;
            BMCWEB_LOG_DEBUG("Found Async.Set on {}, adding OEM SKU to chassis",
                             associatedChassisPath);
            // Read SKU from the associated chassis
            getChassisOemNvidiaSKU(asyncResp, serviceName,
                                   associatedChassisPath);
        });
}

/**
 * @brief Check backward association for associated chassis and read OEM SKU
 */
inline void checkBackwardAssociationForOemSKU(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisPath)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", chassisPath + "/associated_SKU",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, chassisPath](const boost::system::error_code& ec,
                                 const std::vector<std::string>& endpoints) {
            if (ec || endpoints.empty())
            {
                BMCWEB_LOG_ERROR(
                    "No associated_SKU backward association for {}, no OEM SKU",
                    chassisPath);
                return;
            }

            // Found backward association to associated chassis
            std::string associatedChassisPath = endpoints[0];
            BMCWEB_LOG_DEBUG(
                "Following associated_SKU backward association from {} to {}",
                chassisPath, associatedChassisPath);

            checkAsyncSetAndReadSKU(asyncResp, associatedChassisPath);
        });
}

/**
 * @brief Check for Async.Set interface via backward association and add OEM SKU
 *        Follows associated_SKU backward association to find chassis with
 * Async.Set
 *
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       chassisId    Chassis ID.
 * @param[in]       chassisPath  Chassis D-Bus path.
 *
 * @return None.
 */
inline void checkAndAddOemSKU(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& chassisId,
    const std::string& chassisPath)
{
    if constexpr (!BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        return;
    }

    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", chassisPath + "/inventory_SKU",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, chassisPath](const boost::system::error_code& ec,
                                 const std::vector<std::string>& endpoints) {
            if (!ec && !endpoints.empty())
            {
                BMCWEB_LOG_ERROR(
                    "Chassis {} has inventory_SKU forward association, skipping OEM SKU",
                    chassisPath);
                return;
            }
            // Check backward association for OEM SKU
            checkBackwardAssociationForOemSKU(asyncResp, chassisPath);
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
 * @brief Find Async.Set service and update SKU on associated chassis
 */
inline void findAsyncSetAndUpdateSKU(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& associatedChassisPath, const std::string& skuValue)
{
    dbus::utility::getDbusObject(
        associatedChassisPath,
        std::array<std::string_view, 1>{"com.nvidia.Async.Set"},
        [asyncResp, associatedChassisPath,
         skuValue](const boost::system::error_code& ec,
                   const dbus::utility::MapperGetObject& objMap) {
            if (ec || objMap.empty())
            {
                BMCWEB_LOG_ERROR("Failed to find Async.Set on {}",
                                 associatedChassisPath);
                messages::internalError(asyncResp->res);
                return;
            }

            const std::string& service = objMap.begin()->first;
            BMCWEB_LOG_DEBUG("Calling Async.Set on {} to update SKU",
                             associatedChassisPath);
            // Call Async.Set to update SKU on the associated chassis
            // (main chassis reads it via associated_SKU)
            updateChassisSKU(asyncResp, service, associatedChassisPath,
                             skuValue);
        });
}

/**
 * @brief Check backward association and update SKU on associated chassis
 */
inline void checkBackwardAssociationAndUpdateSKU(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisPath, const std::string& skuValue)
{
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", chassisPath + "/associated_SKU",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, chassisPath,
         skuValue](const boost::system::error_code& ec,
                   const std::vector<std::string>& endpoints) {
            if (ec || endpoints.empty())
            {
                BMCWEB_LOG_ERROR(
                    "PATCH: No associated_SKU for {}, cannot update SKU",
                    chassisPath);
                messages::propertyNotWritable(asyncResp->res, "Oem/Nvidia/SKU");
                return;
            }

            // Found associated chassis via backward association
            std::string associatedChassisPath = endpoints[0];
            BMCWEB_LOG_DEBUG("PATCH: Following associated_SKU from {} to {}",
                             chassisPath, associatedChassisPath);

            findAsyncSetAndUpdateSKU(asyncResp, associatedChassisPath,
                                     skuValue);
        });
}

/**
 * @brief Check if chassis has forward association and route SKU PATCH
 * appropriately
 */
inline void checkForwardAssociationAndUpdateSKU(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisPath, const std::string& skuValue)
{
    // Check if this chassis has forward inventory_SKU association
    // Such chassis should NOT accept SKU PATCH directly
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", chassisPath + "/inventory_SKU",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, chassisPath,
         skuValue](const boost::system::error_code& ec,
                   const std::vector<std::string>& endpoints) {
            if (!ec && !endpoints.empty())
            {
                // This chassis has forward association, reject SKU PATCH
                BMCWEB_LOG_ERROR(
                    "PATCH: Chassis {} has forward association, SKU not patchable here",
                    chassisPath);
                messages::propertyNotWritable(asyncResp->res, "Oem/Nvidia/SKU");
                return;
            }

            // No forward association, check for backward association
            checkBackwardAssociationAndUpdateSKU(asyncResp, chassisPath,
                                                 skuValue);
        });
}

} // namespace nvidia_chassis_utils
} // namespace redfish
