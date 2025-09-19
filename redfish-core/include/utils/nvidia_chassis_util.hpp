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
#include <openbmc_dbus_rest.hpp>
#include "trusted_components.hpp"
#include "utils/chassis_utils.hpp"

#include <boost/container/flat_set.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/linux_error.hpp>
#include <boost/url/format.hpp>
#include <health.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <variant>

namespace redfish
{
namespace nvidia_chassis_utils
{

constexpr const size_t trayTopologyStringLength = 16;
constexpr const size_t trayTopologyByteLength = 8;
constexpr const size_t trayTopologyTokenLength = 2;
constexpr const uint8_t trayTopologyMinRevision = 2;
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
        object[0].second[0], "WriteProtected",
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
        object[0].second[0], "WriteProtected", value,
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

    const std::string* sku = nullptr;
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
        "Model", model, "SparePartNumber", sparePartNumber, "SKU", sku, "UUID",
        uuid, "LocationCode", locationCode, "LocationType", locationType,
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

    if (sku != nullptr && !sku->empty())
    {
        asyncResp->res.jsonValue["SKU"] = *sku;
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
                    redfish::chassis_utils::getChassisType(chassisType);
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

} // namespace nvidia_chassis_utils
} // namespace redfish
