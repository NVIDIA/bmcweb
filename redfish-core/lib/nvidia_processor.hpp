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

#include "app.hpp"
#include "dbus_utility.hpp"
#include "nvidia_error_messages.hpp"
#include "nvidia_pcore_dump.hpp"
#include "query.hpp"
#include "redfish_util.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/hex_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_pcie_utils.hpp"
#include "utils/nvidia_processor_utils.hpp"
#include "utils/nvidia_time_utils.hpp"
#include "utils/port_utils.hpp"
#include "utils/processor_utils.hpp"
#include "utils/time_utils.hpp"

#include <boost/container/flat_map.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/conditions_utils.hpp>
#include <utils/nvidia_chassis_util.hpp>
#include <utils/nvidia_histogram_utils.hpp>
#include <utils/systemd_utils.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace redfish
{
namespace nvidia_processor
{
using OperatingConfigProperties =
    std::vector<std::pair<std::string, dbus::utility::DbusVariantType>>;
inline std::string getProcessorType(const std::string& processorType)
{
    if (processorType == "xyz.openbmc_project.Inventory.Item.Accelerator."
                         "AcceleratorType.Accelerator")
    {
        return "Accelerator";
    }
    if (processorType ==
        "xyz.openbmc_project.Inventory.Item.Accelerator.AcceleratorType.FPGA")
    {
        return "FPGA";
    }
    if (processorType ==
        "xyz.openbmc_project.Inventory.Item.Accelerator.AcceleratorType.GPU")
    {
        return "GPU";
    }
    // Unknown or others
    return "";
}

inline std::string getProcessorFpgaType(const std::string& processorFpgaType)
{
    if (processorFpgaType ==
        "xyz.openbmc_project.Inventory.Decorator.FpgaType.FPGAType.Discrete")
    {
        return "Discrete";
    }
    if (processorFpgaType ==
        "xyz.openbmc_project.Inventory.Decorator.FpgaType.FPGAType.Integrated")
    {
        return "Integrated";
    }
    // Unknown or others
    return "";
}

/**
 * @brief Fill out fpgsType info of a processor by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getFpgaTypeData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            const std::string& service,
                            const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Processor fpgatype");
    dbus::utility::getProperty<std::string>(
        service, objPath, "xyz.openbmc_project.Inventory.Decorator.FpgaType",
        "FpgaType",
        [objPath, aResp{aResp}](const boost::system::error_code& ec,
                                const std::string& property) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            std::string fpgaType = getProcessorFpgaType(property);
            aResp->res.jsonValue["FPGA"]["FpgaType"] = fpgaType;
        });
}

// TODO: getSystemPCIeInterfaceProperties to be moved to new
/**
 * @brief Fill out pcie interface properties by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out] asyncResp       Async HTTP response.
 * @param[in]     objPath         D-Bus object to query.
 */
inline void getSystemPCIeInterfaceProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get processor system pcie interface properties");
    dbus::utility::async_method_call(
        [asyncResp, objPath](
            const boost::system::error_code& ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) mutable {
            if (ec)
            {
                BMCWEB_LOG_ERROR("error_code = {}", ec);
                BMCWEB_LOG_ERROR("error msg = {}", ec.message());
                if (asyncResp)
                {
                    messages::internalError(asyncResp->res);
                }
                return;
            }
            if (objInfo.empty())
            {
                BMCWEB_LOG_ERROR("Empty Object Size");
                if (asyncResp)
                {
                    messages::internalError(asyncResp->res);
                }
                return;
            }
            // Get all properties
            dbus::utility::getAllProperties(
                objInfo[0].first, objPath, "",
                [objPath, asyncResp](
                    const boost::system::error_code& ecInner,
                    const dbus::utility::DBusPropertiesMap& properties) {
                    if (ecInner)
                    {
                        BMCWEB_LOG_ERROR("error_code = ", ecInner);
                        BMCWEB_LOG_ERROR("error msg = ", ecInner.message());
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    const double* currentSpeed = nullptr;
                    const size_t* activeWidth = nullptr;
                    bool success = false;
                    success = sdbusplus::unpackPropertiesNoThrow(
                        dbus_utils::UnpackErrorPrinter(), properties,
                        "CurrentSpeed", currentSpeed, "ActiveWidth",
                        activeWidth);

                    asyncResp->res
                        .jsonValue["SystemInterface"]["InterfaceType"] = "PCIe";

                    if (!success)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    if ((currentSpeed != nullptr) && (activeWidth != nullptr))
                    {
                        asyncResp->res
                            .jsonValue["SystemInterface"]["PCIe"]["PCIeType"] =
                            redfish::port_utils::getLinkSpeedGeneration(
                                *currentSpeed);
                    }
                    if (activeWidth != nullptr)
                    {
                        asyncResp->res.jsonValue["SystemInterface"]["PCIe"]
                                                ["LanesInUse"] =
                            (*activeWidth == INT_MAX) ? 0 : *activeWidth;
                    }
                });
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
        std::array<const char*, 0>());
}

/**
 * @brief Fill out system PCIe interface properties by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getProcessorSystemPCIeInterface(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get underneath system interface pcie link");
    dbus::utility::async_method_call(
        [aResp](const boost::system::error_code& ec2,
                std::variant<std::vector<std::string>>& resp) {
            if (ec2)
            {
                return; // no system interface = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            for (const std::string& linkPath : *data)
            {
                getSystemPCIeInterfaceProperties(aResp, linkPath);
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/system_interface",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline std::string getProcessorResetType(const std::string& processorType)
{
    if (processorType ==
        "xyz.openbmc_project.Control.Processor.Reset.ResetTypes.ForceOff")
    {
        return "ForceOff";
    }
    if (processorType ==
        "xyz.openbmc_project.Control.Processor.Reset.ResetTypes.ForceOn")
    {
        return "ForceOn";
    }
    if (processorType ==
        "xyz.openbmc_project.Control.Processor.Reset.ResetTypes.ForceRestart")
    {
        return "ForceRestart";
    }
    if (processorType ==
        "xyz.openbmc_project.Control.Processor.Reset.ResetTypes.GracefulRestart")
    {
        return "GracefulRestart";
    }
    if (processorType ==
        "xyz.openbmc_project.Control.Processor.Reset.ResetTypes.GracefulShutdown")
    {
        return "GracefulShutdown";
    }
    // Unknown or others
    return "";
}

inline void getProcessorResetTypeData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath)
{
    dbus::utility::async_method_call(
        [aResp, cpuId](const boost::system::error_code& ec,
                       const OperatingConfigProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error on reset interface");
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& property : properties)
            {
                if (property.first == "ResetType")
                {
                    const std::string* processorResetType =
                        std::get_if<std::string>(&property.second);
                    if (processorResetType == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Property processorResetType is null");
                        messages::internalError(aResp->res);
                        return;
                    }
                    const std::string processorResetTypeValue =
                        getProcessorResetType(*processorResetType);
                    aResp->res.jsonValue["Actions"]["#Processor.Reset"] = {
                        {"target",
                         "/redfish/v1/Systems/" +
                             std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                             "/Processors/" + cpuId +
                             "/Actions/Processor.Reset"},
                        {"ResetType@Redfish.AllowableValues",
                         {processorResetTypeValue}}};
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Control.Processor.Reset");
}

inline void postResetType(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                          const std::string& processorId,
                          const std::string& cpuObjectPath,
                          const std::string& resetType,
                          const processor_utils::MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::ranges::find(interfaceList,
                              "xyz.openbmc_project.Control.Processor.Reset") !=
            interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        messages::internalError(resp->res);
        return;
    }
    const std::string conName = *inventoryService;
    dbus::utility::getProperty<std::string>(
        conName, cpuObjectPath, "xyz.openbmc_project.Control.Processor.Reset",
        "ResetType",
        [resp, resetType, processorId, conName, cpuObjectPath](
            const boost::system::error_code& ec, const std::string& property) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBus response, error for ResetType ");
                BMCWEB_LOG_ERROR("{}", ec.message());
                messages::internalError(resp->res);
                return;
            }

            const std::string processorResetType =
                getProcessorResetType(property);
            if (processorResetType != resetType)
            {
                BMCWEB_LOG_DEBUG("Property Value Incorrect");
                messages::actionParameterNotSupported(resp->res, "ResetType",
                                                      resetType);
                return;
            }

            static const auto* const resetAsyncIntf =
                "xyz.openbmc_project.Control.Processor.ResetAsync";

            dbus::utility::getDbusObject(
                cpuObjectPath, std::array<std::string_view, 1>{resetAsyncIntf},
                [resp, cpuObjectPath, conName,
                 processorId](const boost::system::error_code& getObjectError,
                              const dbus::utility::MapperGetObject& object) {
                    if (getObjectError)
                    {
                        for (const auto& [serv, _] : object)
                        {
                            if (serv != conName)
                            {
                                continue;
                            }

                            BMCWEB_LOG_DEBUG(
                                "Performing Post using Async Method Call");

                            nvidia_async_operation_utils::
                                doGenericCallAsyncAndGatherResult<int>(
                                    resp, std::chrono::seconds(60), conName,
                                    cpuObjectPath, resetAsyncIntf, "Reset",
                                    [resp, processorId](
                                        const std::string& status,
                                        [[maybe_unused]] const int* retValue) {
                                        if (status ==
                                            nvidia_async_operation_utils::
                                                asyncStatusValueSuccess)
                                        {
                                            BMCWEB_LOG_DEBUG(
                                                "CPU:{} Reset Succeded",
                                                processorId);
                                            messages::success(resp->res);
                                            return;
                                        }
                                        BMCWEB_LOG_ERROR("CPU:{} Reset failed",
                                                         processorId, status);
                                        messages::internalError(resp->res);
                                    });

                            return;
                        }
                    }

                    BMCWEB_LOG_DEBUG("Performing Post using Sync Method Call");

                    // Set the property, with handler to check error responses
                    dbus::utility::async_method_call(
                        [resp, processorId](boost::system::error_code& ec1,
                                            const int retValue) {
                            if (!ec1)
                            {
                                if (retValue != 0)
                                {
                                    BMCWEB_LOG_ERROR("{}", retValue);
                                    messages::internalError(resp->res);
                                }
                                BMCWEB_LOG_DEBUG("CPU:{} Reset Succeded",
                                                 processorId);
                                messages::success(resp->res);
                                return;
                            }
                            BMCWEB_LOG_DEBUG("{}", ec1);
                            messages::internalError(resp->res);
                            return;
                        },
                        conName, cpuObjectPath,
                        "xyz.openbmc_project.Control.Processor.Reset", "Reset");
                });
        });
}

/**
 * @brief Fill out pcie interface properties by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out] asyncResp       Async HTTP response.
 * @param[in]     objPath         D-Bus object to query.
 */
inline void getFPGAPCIeInterfaceProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get processor fpga pcie interface properties");
    dbus::utility::async_method_call(
        [asyncResp, objPath](
            const boost::system::error_code& ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) mutable {
            if (ec)
            {
                BMCWEB_LOG_ERROR("error_code = ", ec);
                BMCWEB_LOG_ERROR("error msg = ", ec.message());
                if (asyncResp)
                {
                    messages::internalError(asyncResp->res);
                }
                return;
            }
            if (objInfo.empty())
            {
                BMCWEB_LOG_ERROR("Empty Object Size");
                if (asyncResp)
                {
                    messages::internalError(asyncResp->res);
                }
                return;
            }
            // Get all properties
            dbus::utility::getAllProperties(
                objInfo[0].first, objPath, "",
                [objPath, asyncResp](
                    const boost::system::error_code& ecInner,
                    const dbus::utility::DBusPropertiesMap& properties) {
                    if (ecInner)
                    {
                        BMCWEB_LOG_ERROR("error_code = ", ecInner);
                        BMCWEB_LOG_ERROR("error msg = ", ecInner.message());
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    std::string speed;
                    size_t width = 0;

                    const double* currentSpeed = nullptr;
                    const size_t* activeWidth = nullptr;

                    const bool success = sdbusplus::unpackPropertiesNoThrow(
                        dbus_utils::UnpackErrorPrinter(), properties,
                        "CurrentSpeed", currentSpeed, "ActiveWidth",
                        activeWidth);

                    if (!success)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    if ((currentSpeed != nullptr) && (activeWidth != nullptr))
                    {
                        speed = redfish::port_utils::getLinkSpeedGeneration(
                            *currentSpeed);
                    }
                    if ((activeWidth != nullptr) && (*activeWidth != INT_MAX))
                    {
                        width = *activeWidth;
                    }
                    nlohmann::json& fpgaIfaceArray =
                        asyncResp->res.jsonValue["FPGA"]["ExternalInterfaces"];
                    fpgaIfaceArray = nlohmann::json::array();
                    fpgaIfaceArray.push_back(
                        {{"InterfaceType", "PCIe"},
                         {"PCIe",
                          {{"PCIeType", speed}, {"LanesInUse", width}}}});
                    return;
                });
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
        std::array<const char*, 0>());
}

/**
 * @brief Fill out fpga PCIe interface properties by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getProcessorFPGAPCIeInterface(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get underneath fpga interface pcie link");
    dbus::utility::async_method_call(
        [aResp](const boost::system::error_code& ec2,
                std::variant<std::vector<std::string>>& resp) {
            if (ec2)
            {
                return; // no fpga interface = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            for (const std::string& linkPath : *data)
            {
                getFPGAPCIeInterfaceProperties(aResp, linkPath);
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/fpga_interface",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

// TODO: move to oem
inline void getPowerBreakThrottleData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath, const std::string& deviceType)
{
    dbus::utility::async_method_call(
        [aResp, objPath,
         deviceType](const boost::system::error_code& ec,
                     const OperatingConfigProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json& json = aResp->res.jsonValue;
            for (const auto& property : properties)
            {
                if (deviceType ==
                    "xyz.openbmc_project.Inventory.Item.Accelerator")
                {
                    json["Oem"]["Nvidia"]["@odata.type"] =
                        "#NvidiaProcessorMetrics.v1_5_0.NvidiaGPUProcessorMetrics";
                }
                else
                {
                    json["Oem"]["Nvidia"]["@odata.type"] =
                        "#NvidiaProcessorMetrics.v1_5_0.NvidiaProcessorMetrics";
                }
                if (property.first == "Value")
                {
                    const std::string* state =
                        std::get_if<std::string>(&property.second);
                    if (state == nullptr)
                    {
                        BMCWEB_LOG_DEBUG(
                            "Get Power Break Value property failed");
                        messages::internalError(aResp->res);
                        return;
                    }
                    json["Oem"]["Nvidia"]["PowerBreakPerformanceState"] =
                        redfish::dbus_utils::toPowerBreakPerformanceState(
                            *state);
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.ProcessorPowerBreak");
}

/**
 * @brief Fill out pcie functions links association by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out] aResp           Async HTTP response.
 * @param[in]     objPath         D-Bus object to query.
 * @param[in]     service         D-Bus service to query.
 * @param[in]     pcieDeviceLink  D-Bus service to query.
 */
inline void getProcessorPCIeFunctionsLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath, const std::string& pcieDeviceLink)
{
    BMCWEB_LOG_DEBUG("Get processor pcie functions links");
    dbus::utility::async_method_call(
        [aResp, pcieDeviceLink,
         objPath](const boost::system::error_code& ec,
                  boost::container::flat_map<std::string,
                                             std::variant<std::string, size_t>>&
                      pcieDevProperties) {
            if (ec)
            {
                // Not reporting Internal Failure because we might have another
                // service with the same objpath to set up config only. Eg:
                // PartLoaction
                BMCWEB_LOG_WARNING("Can't get PCIeDevice DBus properties {}",
                                   objPath);
                return;
            }
            aResp->res.jsonValue["SystemInterface"]["InterfaceType"] = "PCIe";
            // PCIe interface properties
            for (const std::pair<std::string,
                                 std::variant<std::string, size_t>>& property :
                 pcieDevProperties)
            {
                const std::string& propertyName = property.first;
                if ((propertyName == "LanesInUse") ||
                    (propertyName == "MaxLanes"))
                {
                    const size_t* value = std::get_if<size_t>(&property.second);
                    if (value != nullptr)
                    {
                        aResp->res.jsonValue["SystemInterface"]["PCIe"]
                                            [propertyName] = *value;
                    }
                }
                else if ((propertyName == "PCIeType") ||
                         (propertyName == "MaxPCIeType"))
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        aResp->res.jsonValue["SystemInterface"]["PCIe"]
                                            [propertyName] =
                            getPCIeType(*value);
                    }
                }
            }
            // PCIe functions properties
            nlohmann::json& pcieFunctionList =
                aResp->res.jsonValue["Links"]["PCIeFunctions"];
            pcieFunctionList = nlohmann::json::array();
            static constexpr const int maxPciFunctionNum = 8;
            for (int functionNum = 0; functionNum < maxPciFunctionNum;
                 functionNum++)
            {
                // Check if this function exists by looking for a device
                // ID
                std::string devIDProperty =
                    "Function" + std::to_string(functionNum) + "DeviceId";
                std::string* property =
                    std::get_if<std::string>(&pcieDevProperties[devIDProperty]);
                if ((property != nullptr) && !property->empty())
                {
                    pcieFunctionList.push_back(
                        {{"@odata.id", pcieDeviceLink + "/PCIeFunctions/" +
                                           std::to_string(functionNum)}});
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Inventory.Item.PCIeDevice");
}

/**
 * @brief Fill out memory links association by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getProcessorMemoryLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get underneath memory links");
    dbus::utility::async_method_call(
        [aResp](const boost::system::error_code& ec2,
                std::variant<std::vector<std::string>>& resp) {
            if (ec2)
            {
                return; // no memory = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            nlohmann::json& linksArray =
                aResp->res.jsonValue["Links"]["Memory"];
            linksArray = nlohmann::json::array();
            for (const std::string& memoryPath : *data)
            {
                sdbusplus::object_path objectPath(memoryPath);
                std::string memoryName = objectPath.filename();
                if (memoryName.empty())
                {
                    messages::internalError(aResp->res);
                    return;
                }
                linksArray.push_back(
                    {{"@odata.id",
                      "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/Memory/" + memoryName}});
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_memory",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Fill out links for parent chassis PCIeDevice by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 * @param[in]       chassisName D-Bus object chassisName.
 */
inline void getParentChassisPCIeDeviceLink(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& chassisName)
{
    dbus::utility::async_method_call(
        [aResp, chassisName](const boost::system::error_code& ec,
                             std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no chassis = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr || data->size() > 1)
            {
                // Chassis must have single parent chassis
                return;
            }
            const std::string& parentChassisPath = data->front();
            sdbusplus::object_path objectParentChassisPath(parentChassisPath);
            std::string parentChassisName = objectParentChassisPath.filename();
            if (parentChassisName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            dbus::utility::async_method_call(
                [aResp, chassisName, parentChassisName](
                    const boost::system::error_code& ec1,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
                    if (ec1)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    for (const auto& [objectPath, serviceMap] : subtree)
                    {
                        // Process same device
                        if (!objectPath.ends_with(chassisName))
                        {
                            continue;
                        }
                        std::string pcieDeviceLink = "/redfish/v1/Chassis/";
                        pcieDeviceLink += parentChassisName;
                        pcieDeviceLink += "/PCIeDevices/";
                        pcieDeviceLink += chassisName;
                        aResp->res.jsonValue["Links"]["PCIeDevice"] = {
                            {"@odata.id", pcieDeviceLink}};
                        if (serviceMap.empty())
                        {
                            BMCWEB_LOG_ERROR("Got 0 service "
                                             "names");
                            messages::internalError(aResp->res);
                            return;
                        }
                        const std::string& serviceName = serviceMap[0].first;
                        // Get PCIeFunctions Link
                        redfish::nvidia_processor::
                            getProcessorPCIeFunctionsLinks(
                                aResp, serviceName, objectPath, pcieDeviceLink);
                    }
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                parentChassisPath, 0,
                std::array<const char*, 1>{"xyz.openbmc_project.Inventory.Item."
                                           "PCIeDevice"});
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
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
inline void getProcessorChassisLink(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& service, const std::string& deviceType)
{
    sdbusplus::object_path associationPath;
    if (deviceType == "xyz.openbmc_project.Inventory.Item.Cpu")
    {
        associationPath = objPath + "/chassis";
    }
    else
    {
        associationPath = objPath + "/parent_chassis";
    }
    BMCWEB_LOG_DEBUG("Get parent chassis link");
    dbus::utility::async_method_call(
        [aResp, objPath,
         service](const boost::system::error_code& ec,
                  std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no chassis = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr || data->size() > 1)
            {
                // Processor must have single parent chassis
                return;
            }
            const std::string& chassisPath = data->front();
            sdbusplus::object_path objectPath(chassisPath);
            std::string chassisName = objectPath.filename();
            if (chassisName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            aResp->res.jsonValue["Links"]["Chassis"] = {
                {"@odata.id", "/redfish/v1/Chassis/" + chassisName}};

            // Get PCIeDevice on this chassis
            dbus::utility::async_method_call(
                [aResp, chassisName, chassisPath,
                 service](const boost::system::error_code& getEndpointsError,
                          std::variant<std::vector<std::string>>& resp1) {
                    if (getEndpointsError)
                    {
                        BMCWEB_LOG_ERROR(
                            "Chassis {} has no connected PCIe devices",
                            chassisName);
                        return; // no pciedevices = no failures
                    }
                    std::vector<std::string>* data1 =
                        std::get_if<std::vector<std::string>>(&resp1);
                    if (data1 == nullptr || data1->size() > 1)
                    {
                        // Chassis must have single pciedevice
                        BMCWEB_LOG_ERROR("chassis must have single pciedevice");
                        return;
                    }
                    const std::string& pcieDevicePath = data1->front();
                    sdbusplus::object_path objectPath1(pcieDevicePath);
                    std::string pcieDeviceName = objectPath1.filename();
                    if (pcieDeviceName.empty())
                    {
                        BMCWEB_LOG_ERROR("chassis pciedevice name empty");
                        messages::internalError(aResp->res);
                        return;
                    }
                    std::string pcieDeviceLink = "/redfish/v1/Chassis/";
                    pcieDeviceLink += chassisName;
                    pcieDeviceLink += "/PCIeDevices/";
                    pcieDeviceLink += pcieDeviceName;
                    aResp->res.jsonValue["Links"]["PCIeDevice"] = {
                        {"@odata.id", pcieDeviceLink}};

                    // Get PCIeFunctions Link
                    getProcessorPCIeFunctionsLinks(
                        aResp, service, pcieDevicePath, pcieDeviceLink);
                },
                "xyz.openbmc_project.ObjectMapper", chassisPath + "/pciedevice",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Association", "endpoints");
        },
        "xyz.openbmc_project.ObjectMapper", associationPath,
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Fill out firmware version info of a accelerator by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getProcessorFirmwareVersion(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Processor firmware version");
    dbus::utility::async_method_call(
        [aResp{aResp}](const boost::system::error_code& ec,
                       const std::variant<std::string>& property) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for "
                                 "Processor firmware version");
                messages::internalError(aResp->res);
                return;
            }
            const std::string* value = std::get_if<std::string>(&property);
            if (value == nullptr)
            {
                BMCWEB_LOG_DEBUG("Null value returned for Version");
                messages::internalError(aResp->res);
                return;
            }
            aResp->res.jsonValue["FirmwareVersion"] = *value;
        },
        service, objPath, "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Software.Version", "Version");
}

/**
 * @brief Fill out location context of a processor by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getProcessorLocationContext(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Processor LocationContext Data");
    dbus::utility::getProperty<std::string>(
        service, objPath,
        "xyz.openbmc_project.Inventory.Decorator.LocationContext",
        "LocationContext",
        [objPath, aResp{aResp}](const boost::system::error_code& ec,
                                const std::string& property) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                // not throw out error to prevent aborting the resource display
                return;
            }

            aResp->res.jsonValue["Location"]["PartLocationContext"] = property;
        });
}

/**
 * @brief Fill out location info of a processor by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getCpuLocationType(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const std::string& service,
                               const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Cpu LocationType Data");
    dbus::utility::getProperty<std::string>(
        service, objPath, "xyz.openbmc_project.Inventory.Decorator.Location",
        "LocationType",
        [objPath, aResp{aResp}](const boost::system::error_code& ec,
                                const std::string& property) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }

            aResp->res.jsonValue["Location"]["PartLocation"]["LocationType"] =
                redfish::dbus_utils::toLocationType(property);
        });
}

/**
 * @brief Fill out replaceable info of a processor by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getProcessorReplaceable(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path)
{
    BMCWEB_LOG_DEBUG("Get Processor Replaceable");
    dbus::utility::getProperty<bool>(
        connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.Replaceable",
        "FieldReplaceable",
        [asyncResp](const boost::system::error_code& ec, const bool property) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error for Replaceable");
                messages::internalError(asyncResp->res);
                return;
            }

            asyncResp->res.jsonValue["Replaceable"] = property;
        });
}

/**
 * Request all the properties for the given D-Bus object and fill out the
 * related entries in the Redfish processor response.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       cpuId       CPU D-Bus name.
 * @param[in]       service     D-Bus service name to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getProcessorMemoryData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath)
{
    dbus::utility::async_method_call(
        [aResp, cpuId](boost::system::error_code& ec,
                       const OperatingConfigProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_WARNING("D-Bus error: {}, {}", ec, ec.message());
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json& json = aResp->res.jsonValue;
            std::string metricsURI =
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/Processors/";
            metricsURI += cpuId;
            metricsURI += "/MemorySummary/MemoryMetrics";
            json["MemorySummary"]["Metrics"]["@odata.id"] = metricsURI;
            for (const auto& [key, variant] : properties)
            {
                if (key == "CacheSizeInKiB")
                {
                    const uint64_t* value = std::get_if<uint64_t>(&variant);
                    if (value != nullptr && *value != 0)
                    {
                        json["MemorySummary"]["TotalCacheSizeMiB"] =
                            (*value) >> 10;
                    }
                }
                else if (key == "VolatileSizeInKiB")
                {
                    const uint64_t* value = std::get_if<uint64_t>(&variant);
                    if (value != nullptr)
                    {
                        json["MemorySummary"]["TotalMemorySizeMiB"] =
                            (*value) >> 10;
                    }
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Inventory.Item.PersistentMemory");
}

inline void getEccModeData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const std::string& cpuId, const std::string& service,
                           const std::string& objPath)
{
    dbus::utility::async_method_call(
        [aResp, cpuId](const boost::system::error_code& ec,
                       const OperatingConfigProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json& json = aResp->res.jsonValue;
            for (const auto& property : properties)
            {
                if (property.first == "ECCModeEnabled")
                {
                    const bool* eccModeEnabled =
                        std::get_if<bool>(&property.second);
                    if (eccModeEnabled == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    json["MemorySummary"]["ECCModeEnabled"] = *eccModeEnabled;
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Memory.MemoryECC");
}

inline void getProcessorEccModeData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath)
{
    nlohmann::json& json = aResp->res.jsonValue;
    std::string metricsURI =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Processors/";
    metricsURI += cpuId;
    metricsURI += "/MemorySummary/MemoryMetrics";
    json["MemorySummary"]["Metrics"]["@odata.id"] = metricsURI;
    getEccModeData(aResp, cpuId, service, objPath);
}

inline void getEccPendingData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath)
{
    dbus::utility::async_method_call(
        [aResp, cpuId](const boost::system::error_code& ec,
                       const OperatingConfigProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json& json = aResp->res.jsonValue;
            for (const auto& property : properties)
            {
                if (property.first == "PendingECCState")
                {
                    const bool* pendingEccState =
                        std::get_if<bool>(&property.second);
                    if (pendingEccState == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    json["MemorySummary"]["ECCModeEnabled"] = *pendingEccState;
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Memory.MemoryECC");
}

// TODO: move to oem
inline void getProcessorPerformanceData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath, const std::string& deviceType)
{
    dbus::utility::async_method_call(
        [aResp, objPath,
         deviceType](const boost::system::error_code& ec,
                     const OperatingConfigProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }

            nlohmann::json& json = aResp->res.jsonValue;
            const std::string_view metricType =
                (deviceType == "xyz.openbmc_project.Inventory.Item.Accelerator")
                    ? "#NvidiaProcessorMetrics.v1_5_0.NvidiaGPUProcessorMetrics"
                    : "#NvidiaProcessorMetrics.v1_5_0.NvidiaProcessorMetrics";

            json["Oem"]["Nvidia"]["@odata.type"] = metricType;

            for (const auto& property : properties)
            {
                if (property.first == "Value" &&
                    deviceType !=
                        "xyz.openbmc_project.Inventory.Item.Accelerator")
                {
                    const std::string* state =
                        std::get_if<std::string>(&property.second);
                    if (state == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Get Performance Value property failed");
                        messages::internalError(aResp->res);
                        return;
                    }
                    json["Oem"]["Nvidia"]["PerformanceState"] =
                        redfish::dbus_utils::toPerformanceStateType(*state);
                }

                if (property.first == "ThrottleReason")
                {
                    std::string reason;
                    const std::vector<std::string>* throttleReasons =
                        std::get_if<std::vector<std::string>>(&property.second);
                    std::vector<std::string> formattedThrottleReasons{};

                    if (throttleReasons == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Get Throttle reasons property failed");
                        messages::internalError(aResp->res);
                        return;
                    }

                    for (const auto& val : *throttleReasons)
                    {
                        reason = redfish::dbus_utils::toReasonType(val);
                        if (!reason.empty())
                        {
                            formattedThrottleReasons.push_back(reason);
                        }
                    }

                    json["Oem"]["Nvidia"]["ThrottleReasons"] =
                        formattedThrottleReasons;
                }
                if ((property.first == "PowerLimitThrottleDuration") ||
                    (property.first == "ThermalLimitThrottleDuration"))
                {
                    auto propName = property.first;
                    const uint64_t* val =
                        std::get_if<uint64_t>(&property.second);
                    if (val == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Get  power/thermal duration property failed");
                        messages::internalError(aResp->res);
                        return;
                    }

                    std::optional<std::string> duration =
                        time_utils::toDurationStringFromNano(*val);

                    if (duration)
                    {
                        json[propName] = *duration;
                    }
                }
                if ((property.first == "HardwareViolationThrottleDuration") ||
                    (property.first ==
                     "GlobalSoftwareViolationThrottleDuration"))
                {
                    auto propName = property.first;
                    const uint64_t* val =
                        std::get_if<uint64_t>(&property.second);
                    if (val == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Get  duraiton property failed");
                        messages::internalError(aResp->res);
                        return;
                    }

                    std::optional<std::string> duration =
                        time_utils::toDurationStringFromNano(*val);

                    if (duration)
                    {
                        json["Oem"]["Nvidia"][propName] = *duration;
                    }
                }
                if ((property.first == "AccumulatedSMUtilizationDuration") ||
                    (property.first ==
                     "AccumulatedGPUContextUtilizationDuration"))
                {
                    auto propName = property.first;
                    const uint64_t* val =
                        std::get_if<uint64_t>(&property.second);
                    if (val == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Get  acc duraiton property failed");
                        messages::internalError(aResp->res);
                        return;
                    }

                    std::optional<std::string> duration =
                        time_utils::toDurationStringFromNano(*val);

                    if (duration)
                    {
                        json["Oem"]["Nvidia"][propName] = *duration;
                    }
                }
                if ((property.first == "PCIeTXBytes") ||
                    (property.first == "PCIeRXBytes"))
                {
                    auto propName = property.first;
                    const uint64_t* val =
                        std::get_if<uint64_t>(&property.second);
                    if (val == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Get  pcie bytes property failed");
                        messages::internalError(aResp->res);
                        return;
                    }
                    json["Oem"]["Nvidia"][propName] = *val;
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.State.ProcessorPerformance");
}
// TODO: move to oem
inline void getGPUNvlinkMetricsData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath, const std::string& nvlinkMetricsIface)
{
    dbus::utility::getAllProperties(
        service, objPath, nvlinkMetricsIface,
        [aResp](const boost::system::error_code& ec,
                const dbus::utility::DBusPropertiesMap& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "Can't get GPU Nvlink Metrics Iface properties ");
                return;
            }

            nlohmann::json& json = aResp->res.jsonValue;

            const double* nvlinkDataRxBandwidthGbps = nullptr;
            const double* nvlinkDataTxBandwidthGbps = nullptr;
            const double* nvlinkRawTxBandwidthGbps = nullptr;
            const double* nvlinkRawRxBandwidthGbps = nullptr;

            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), resp,
                "NVLinkDataRxBandwidthGbps", nvlinkDataRxBandwidthGbps,
                "NVLinkDataTxBandwidthGbps", nvlinkDataTxBandwidthGbps,
                "NVLinkRawRxBandwidthGbps", nvlinkRawRxBandwidthGbps,
                "NVLinkRawTxBandwidthGbps", nvlinkRawTxBandwidthGbps);

            if (!success)
            {
                messages::internalError(aResp->res);
                return;
            }

            if (nvlinkRawTxBandwidthGbps != nullptr)
            {
                json["Oem"]["Nvidia"]["NVLinkRawTxBandwidthGbps"] =
                    *nvlinkRawTxBandwidthGbps;
            }
            else
            {
                BMCWEB_LOG_ERROR("Null value returned "
                                 "for NVLinkRawTxBandwidthGbps");
            }

            if (nvlinkRawRxBandwidthGbps != nullptr)
            {
                json["Oem"]["Nvidia"]["NVLinkRawRxBandwidthGbps"] =
                    *nvlinkRawRxBandwidthGbps;
            }
            else
            {
                BMCWEB_LOG_ERROR("Null value returned "
                                 "for NVLinkRawRxBandwidthGbps");
            }

            if (nvlinkDataTxBandwidthGbps != nullptr)
            {
                json["Oem"]["Nvidia"]["NVLinkDataTxBandwidthGbps"] =
                    *nvlinkDataTxBandwidthGbps;
            }
            else
            {
                BMCWEB_LOG_ERROR("Null value returned "
                                 "for NVLinkDataTxBandwidthGbps");
            }

            if (nvlinkDataRxBandwidthGbps != nullptr)
            {
                json["Oem"]["Nvidia"]["NVLinkDataRxBandwidthGbps"] =
                    *nvlinkDataRxBandwidthGbps;
            }
            else
            {
                BMCWEB_LOG_ERROR("Null value returned "
                                 "for NVLinkDataRxBandwidthGbps");
            }
        });
}

// TODO: move to oem
inline void getPowerSystemInputsData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath, const std::string& deviceType)
{
    dbus::utility::async_method_call(
        [aResp, objPath,
         deviceType](const boost::system::error_code& ec,
                     const OperatingConfigProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json& json = aResp->res.jsonValue;
            for (const auto& property : properties)
            {
                if (deviceType ==
                    "xyz.openbmc_project.Inventory.Item.Accelerator")
                {
                    json["Oem"]["Nvidia"]["@odata.type"] =
                        "#NvidiaProcessorMetrics.v1_5_0.NvidiaGPUProcessorMetrics";
                }
                else
                {
                    json["Oem"]["Nvidia"]["@odata.type"] =
                        "#NvidiaProcessorMetrics.v1_5_0.NvidiaProcessorMetrics";
                }
                if (property.first == "Status")
                {
                    const std::string* state =
                        std::get_if<std::string>(&property.second);
                    if (state == nullptr)
                    {
                        BMCWEB_LOG_DEBUG(
                            "Get PowerSystemInputs Status property failed");
                        messages::internalError(aResp->res);
                        return;
                    }
                    json["Oem"]["Nvidia"]["EDPViolationState"] =
                        redfish::dbus_utils::toPowerSystemInputType(*state);
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.State.Decorator.PowerSystemInputs");
}
// TODO: move to oem
inline void getMemorySpareChannelPresenceData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath, const std::string& deviceType)
{
    dbus::utility::async_method_call(
        [aResp, objPath,
         deviceType](const boost::system::error_code& ec,
                     const std::variant<std::string>& property) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json& json = aResp->res.jsonValue;

            const std::string* memorySpareChannelPresence =
                std::get_if<std::string>(&property);
            if (memorySpareChannelPresence == nullptr)
            {
                BMCWEB_LOG_ERROR(
                    "Null value returned for memorySpareChannelPresence");
                messages::internalError(aResp->res);
                return;
            }
            if (deviceType == "xyz.openbmc_project.Inventory.Item.Accelerator")
            {
                json["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaProcessorMetrics.v1_5_0.NvidiaGPUProcessorMetrics";
            }
            else
            {
                json["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaProcessorMetrics.v1_5_0.NvidiaProcessorMetrics";
            }
            json["Oem"]["Nvidia"]["MemorySpareChannelPresence"] =
                redfish::dbus_utils::toChannelPresence(
                    *memorySpareChannelPresence);
        },
        service, objPath, "org.freedesktop.DBus.Properties", "Get",
        "com.nvidia.MemorySpareChannel", "MemorySpareChannelPresence");
}

// TODO: move to oem
inline void getMetricValueSensorData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& sensorPath, const std::string& deviceType)
{
    sdbusplus::object_path objPath(sensorPath);
    std::string sensorName = objPath.filename();

    // Maps D-Bus sensor names to Redfish OEM property names. Backend sensor
    // names are decoupled from the Redfish schema so that changes to sensor
    // naming in pldm or other providers do not affect the Redfish mockup or
    // schema compliance.
    struct MetricMapping
    {
        std::string_view sensorName;
        std::string_view redfishProperty;
    };
    static constexpr std::array<MetricMapping, 4> metricMappings = {{
        {"CpuUptime", "CPUUptime"},
        {"PowerBRKAssertionTime", "PowerBrakeAssertionDuration"},
        {"PageRetirementCount", "MemoryPageRetirementCount"},
        {"TjMaxDramIndex", "TjMaxDramIndex"},
    }};

    std::string_view redfishProperty;
    for (const auto& mapping : metricMappings)
    {
        if (sensorName.find(mapping.sensorName) != std::string::npos)
        {
            redfishProperty = mapping.redfishProperty;
            break;
        }
    }
    if (redfishProperty.empty())
    {
        return;
    }

    dbus::utility::getAllProperties(
        service, sensorPath, "xyz.openbmc_project.Metric.Value",
        [aResp, deviceType, redfishProp = std::string(redfishProperty)](
            const boost::system::error_code& ec,
            const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for Metric.Value: {}",
                                 ec.message());
                return;
            }

            const double* value = nullptr;
            const std::string* unit = nullptr;
            for (const auto& [key, variant] : properties)
            {
                if (key == "Value")
                {
                    value = std::get_if<double>(&variant);
                }
                else if (key == "Unit")
                {
                    unit = std::get_if<std::string>(&variant);
                }
            }
            if (value == nullptr)
            {
                BMCWEB_LOG_ERROR(
                    "Null value returned for Metric.Value property {}",
                    redfishProp);
                messages::internalError(aResp->res);
                return;
            }

            nlohmann::json& json = aResp->res.jsonValue;
            if (deviceType == "xyz.openbmc_project.Inventory.Item.Accelerator")
            {
                json["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaProcessorMetrics.v1_5_0.NvidiaGPUProcessorMetrics";
            }
            else
            {
                json["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaProcessorMetrics.v1_5_0.NvidiaProcessorMetrics";
            }

            if (unit != nullptr &&
                *unit == "xyz.openbmc_project.Metric.Value.Unit.Seconds")
            {
                uint64_t nanoseconds =
                    static_cast<uint64_t>(*value * 1000.0 * 1000.0 * 1000.0);
                std::optional<std::string> duration =
                    time_utils::toDurationStringFromNano(nanoseconds);
                if (duration)
                {
                    json["Oem"]["Nvidia"][redfishProp] = *duration;
                }
            }
            else
            {
                json["Oem"]["Nvidia"][redfishProp] =
                    static_cast<int64_t>(*value);
            }
        });
}

// TODO: move to oem
inline void getMigModeData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const std::string& cpuId, const std::string& service,
                           const std::string& objPath)
{
    dbus::utility::async_method_call(
        [aResp, cpuId](const boost::system::error_code& ec,
                       const OperatingConfigProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json& json = aResp->res.jsonValue;
            for (const auto& property : properties)
            {
                if (property.first == "MIGModeEnabled")
                {
                    const bool* migModeEnabled =
                        std::get_if<bool>(&property.second);
                    if (migModeEnabled == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    json["Oem"]["Nvidia"]["@odata.type"] =
                        "#NvidiaProcessor.v1_4_0.NvidiaGPU";
                    json["Oem"]["Nvidia"]["MIGModeEnabled"] = *migModeEnabled;
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.MigMode");
}

// TODO: move to oem
inline void getProcessorRemoteDebugState(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath)
{
    dbus::utility::async_method_call(
        [aResp, objPath](const boost::system::error_code& ec,
                         const OperatingConfigProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json& json = aResp->res.jsonValue;
            for (const auto& property : properties)
            {
                json["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaProcessor.v1_0_0.NvidiaProcessor";
                if (property.first == "Enabled")
                {
                    const bool* state = std::get_if<bool>(&property.second);
                    if (state == nullptr)
                    {
                        BMCWEB_LOG_DEBUG(
                            "Get Performance Value property failed");
                        messages::internalError(aResp->res);
                        return;
                    }
                    json["Oem"]["Nvidia"]["RemoteDebugEnabled"] = *state;
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Control.Processor.RemoteDebug");
}

inline void getRemoteDebugState(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                                const std::string& service,
                                const std::string& objPath)
{
    dbus::utility::async_method_call(
        [aResp, service,
         objPath](const boost::system::error_code& ec,
                  std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                // No state effecter attached.
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }
            for (const std::string& effecterPath : *data)
            {
                BMCWEB_LOG_DEBUG("State Effecter Object Path {}", effecterPath);

                const std::array<const char*, 1> effecterInterfaces = {
                    "xyz.openbmc_project.Control.Processor.RemoteDebug"};
                // Process sensor reading
                dbus::utility::async_method_call(
                    [aResp, effecterPath](
                        const boost::system::error_code& getObjectError,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& object) {
                        if (getObjectError)
                        {
                            // The path does not implement any state interfaces.
                            return;
                        }

                        for (const auto& [serviceName, interfaces] : object)
                        {
                            if (std::ranges::find(
                                    interfaces,
                                    "xyz.openbmc_project.Control.Processor.RemoteDebug") !=
                                interfaces.end())
                            {
                                getProcessorRemoteDebugState(aResp, serviceName,
                                                             effecterPath);
                            }
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetObject",
                    effecterPath, effecterInterfaces);
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_controls",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

// TODO: move to oem
inline void getGPMMetricsData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath, const std::string& gpmMetricsIface)
{
    dbus::utility::getAllProperties(
        service, objPath, gpmMetricsIface,
        [aResp](const boost::system::error_code& ec,
                const dbus::utility::DBusPropertiesMap& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "GetPIDValues: Can't get GPM Metrics Iface properties ");
                return;
            }

            nlohmann::json& json = aResp->res.jsonValue;

            const double* fp16ActivityPercent = nullptr;
            const double* fp32ActivityPercent = nullptr;
            const double* fp64ActivityPercent = nullptr;
            const double* graphicsEngActivityPercent = nullptr;
            const double* nvDecUtilPercent = nullptr;
            const double* nvJpgUtilPercent = nullptr;
            const double* nvOfaUtilPercent = nullptr;
            const double* smActivityPercent = nullptr;
            const double* smOccupancyPercent = nullptr;
            const double* tensorCoreActivityPercent = nullptr;
            const double* dmmaUtil = nullptr;
            const double* hmmaUtil = nullptr;
            const double* immaUtil = nullptr;
            const double* integerActivityUtil = nullptr;
            const double* pcieRxBandwidthGbps = nullptr;
            const double* pcieTxBandwidthGbps = nullptr;
            const std::vector<double>* nvdecInstanceUtil = nullptr;
            const std::vector<double>* nvjpgInstanceUtil = nullptr;
            const std::vector<double>* nvEncInstanceUtil = nullptr;
            const double* nvEncUtilizationPercent = nullptr;
            const double* hostMemoryCacheHitPercent = nullptr;
            const double* hostMemoryCacheMissPercent = nullptr;
            const double* peerMemoryCacheHitPercent = nullptr;
            const double* peerMemoryCacheMissPercent = nullptr;
            const double* dramMemoryCacheHitPercent = nullptr;
            const double* dramMemoryCacheMissPercent = nullptr;
            const double* c2CRawTxGbps = nullptr;
            const double* c2CRawRxGbps = nullptr;
            const double* c2CDataTxGbps = nullptr;
            const double* c2CDataRxGbps = nullptr;

            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), resp, "FP16ActivityPercent",
                fp16ActivityPercent, "FP32ActivityPercent", fp32ActivityPercent,
                "FP64ActivityPercent", fp64ActivityPercent,
                "GraphicsEngineActivityPercent", graphicsEngActivityPercent,
                "NVDecUtilizationPercent", nvDecUtilPercent,
                "NVJpgUtilizationPercent", nvJpgUtilPercent,
                "NVOfaUtilizationPercent", nvOfaUtilPercent,
                "PCIeRawRxBandwidthGbps", pcieRxBandwidthGbps,
                "PCIeRawTxBandwidthGbps", pcieTxBandwidthGbps,
                "SMActivityPercent", smActivityPercent, "SMOccupancyPercent",
                smOccupancyPercent, "TensorCoreActivityPercent",
                tensorCoreActivityPercent, "IntegerActivityUtilizationPercent",
                integerActivityUtil, "DMMAUtilizationPercent", dmmaUtil,
                "HMMAUtilizationPercent", hmmaUtil, "IMMAUtilizationPercent",
                immaUtil, "NVDecInstanceUtilizationPercent", nvdecInstanceUtil,
                "NVJpgInstanceUtilizationPercent", nvjpgInstanceUtil,
                "NVEncInstanceUtilizationPercent", nvEncInstanceUtil,
                "NVEncUtilizationPercent", nvEncUtilizationPercent,
                "HostMemoryCacheHitPercent", hostMemoryCacheHitPercent,
                "HostMemoryCacheMissPercent", hostMemoryCacheMissPercent,
                "PeerMemoryCacheHitPercent", peerMemoryCacheHitPercent,
                "PeerMemoryCacheMissPercent", peerMemoryCacheMissPercent,
                "DRAMMemoryCacheHitPercent", dramMemoryCacheHitPercent,
                "DRAMMemoryCacheMissPercent", dramMemoryCacheMissPercent,
                "C2CRawTxBandwidthGbps", c2CRawTxGbps, "C2CRawRxBandwidthGbps",
                c2CRawRxGbps, "C2CDataTxBandwidthGbps", c2CDataTxGbps,
                "C2CDataRxBandwidthGbps", c2CDataRxGbps);

            if (!success)
            {
                messages::internalError(aResp->res);
                return;
            }

            if (graphicsEngActivityPercent != nullptr)
            {
                json["Oem"]["Nvidia"]["GraphicsEngineActivityPercent"] =
                    *graphicsEngActivityPercent;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for GraphicsEngineActivityPercent");
                messages::internalError(aResp->res);
                return;
            }

            if (smActivityPercent != nullptr)
            {
                json["Oem"]["Nvidia"]["SMActivityPercent"] = *smActivityPercent;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for SMActivityPercent");
                messages::internalError(aResp->res);
                return;
            }

            if (smOccupancyPercent != nullptr)
            {
                json["Oem"]["Nvidia"]["SMOccupancyPercent"] =
                    *smOccupancyPercent;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for SMOccupancyPercent");
                messages::internalError(aResp->res);
                return;
            }

            if (tensorCoreActivityPercent != nullptr)
            {
                json["Oem"]["Nvidia"]["TensorCoreActivityPercent"] =
                    *tensorCoreActivityPercent;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for TensorCoreActivityPercent");
                messages::internalError(aResp->res);
                return;
            }

            if (fp64ActivityPercent != nullptr)
            {
                json["Oem"]["Nvidia"]["FP64ActivityPercent"] =
                    *fp64ActivityPercent;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for FP64ActivityPercent");
                messages::internalError(aResp->res);
                return;
            }

            if (fp32ActivityPercent != nullptr)
            {
                json["Oem"]["Nvidia"]["FP32ActivityPercent"] =
                    *fp32ActivityPercent;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for FP32ActivityPercent");
                messages::internalError(aResp->res);
                return;
            }

            if (fp16ActivityPercent != nullptr)
            {
                json["Oem"]["Nvidia"]["FP16ActivityPercent"] =
                    *fp16ActivityPercent;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for FP16ActivityPercent");
                messages::internalError(aResp->res);
                return;
            }

            if (pcieTxBandwidthGbps != nullptr)
            {
                json["Oem"]["Nvidia"]["PCIeRawTxBandwidthGbps"] =
                    *pcieTxBandwidthGbps;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for PCIeRawTxBandwidthGbps");
                messages::internalError(aResp->res);
                return;
            }

            if (pcieRxBandwidthGbps != nullptr)
            {
                json["Oem"]["Nvidia"]["PCIeRawRxBandwidthGbps"] =
                    *pcieRxBandwidthGbps;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for PCIeRawRxBandwidthGbps");
                messages::internalError(aResp->res);
                return;
            }

            if (nvDecUtilPercent != nullptr)
            {
                json["Oem"]["Nvidia"]["NVDecUtilizationPercent"] =
                    *nvDecUtilPercent;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for NVDecUtilizationPercent");
                messages::internalError(aResp->res);
                return;
            }

            if (nvJpgUtilPercent != nullptr)
            {
                json["Oem"]["Nvidia"]["NVJpgUtilizationPercent"] =
                    *nvJpgUtilPercent;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for NVJpgUtilizationPercent");
                messages::internalError(aResp->res);
                return;
            }

            if (nvOfaUtilPercent != nullptr)
            {
                json["Oem"]["Nvidia"]["NVOfaUtilizationPercent"] =
                    *nvOfaUtilPercent;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for NVOfaUtilizationPercent");
                messages::internalError(aResp->res);
                return;
            }
            if (integerActivityUtil != nullptr)
            {
                json["Oem"]["Nvidia"]["IntegerActivityUtilizationPercent"] =
                    *integerActivityUtil;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for IntegerActivityUtilizationPercent");
                messages::internalError(aResp->res);
                return;
            }
            if (dmmaUtil != nullptr)
            {
                json["Oem"]["Nvidia"]["DMMAUtilizationPercent"] = *dmmaUtil;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for DMMAUtilizationPercent");
                messages::internalError(aResp->res);
                return;
            }
            if (hmmaUtil != nullptr)
            {
                json["Oem"]["Nvidia"]["HMMAUtilizationPercent"] = *hmmaUtil;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for HMMAUtilizationPercent");
                messages::internalError(aResp->res);
                return;
            }
            if (immaUtil != nullptr)
            {
                json["Oem"]["Nvidia"]["IMMAUtilizationPercent"] = *immaUtil;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for IMMAUtilizationPercent");
                messages::internalError(aResp->res);
                return;
            }
            if (nvdecInstanceUtil != nullptr)
            {
                std::vector<double> nvdecInstanceUtilization{};
                for (auto val : *nvdecInstanceUtil)
                {
                    nvdecInstanceUtilization.push_back(val);
                }
                json["Oem"]["Nvidia"]["NVDecInstanceUtilizationPercent"] =
                    nvdecInstanceUtilization;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for NVDecInstanceUtilizationPercent");
                messages::internalError(aResp->res);
                return;
            }

            if (nvjpgInstanceUtil != nullptr)
            {
                std::vector<double> nvjpgInstanceUtilization{};
                for (auto val : *nvjpgInstanceUtil)
                {
                    nvjpgInstanceUtilization.push_back(val);
                }
                json["Oem"]["Nvidia"]["NVJpgInstanceUtilizationPercent"] =
                    nvjpgInstanceUtilization;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for NVJpgUtilizationPercent");
                messages::internalError(aResp->res);
                return;
            }

            if (nvEncInstanceUtil != nullptr)
            {
                std::vector<double> nvEncInstanceUtilization{};
                for (auto val : *nvEncInstanceUtil)
                {
                    nvEncInstanceUtilization.push_back(val);
                }
                json["Oem"]["Nvidia"]["NVEncInstanceUtilizationPercent"] =
                    nvEncInstanceUtilization;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for NVEncInstanceUtilizationPercent");
            }
            if (nvEncUtilizationPercent != nullptr)
            {
                json["Oem"]["Nvidia"]["NVEncUtilizationPercent"] =
                    *nvEncUtilizationPercent;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for NVEncUtilizationPercent");
            }
            if (hostMemoryCacheHitPercent != nullptr)
            {
                json["Oem"]["Nvidia"]["HostMemoryCacheHitPercent"] =
                    *hostMemoryCacheHitPercent;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for HostMemoryCacheHitPercent");
            }
            if (hostMemoryCacheMissPercent != nullptr)
            {
                json["Oem"]["Nvidia"]["HostMemoryCacheMissPercent"] =
                    *hostMemoryCacheMissPercent;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for HostMemoryCacheMissPercent");
            }
            if (peerMemoryCacheHitPercent != nullptr)
            {
                json["Oem"]["Nvidia"]["PeerMemoryCacheHitPercent"] =
                    *peerMemoryCacheHitPercent;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for PeerMemoryCacheHitPercent");
            }
            if (peerMemoryCacheMissPercent != nullptr)
            {
                json["Oem"]["Nvidia"]["PeerMemoryCacheMissPercent"] =
                    *peerMemoryCacheMissPercent;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for PeerMemoryCacheMissPercent");
            }
            if (dramMemoryCacheHitPercent != nullptr)
            {
                json["Oem"]["Nvidia"]["DRAMMemoryCacheHitPercent"] =
                    *dramMemoryCacheHitPercent;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for DRAMCacheHitPercent");
            }
            if (dramMemoryCacheMissPercent != nullptr)
            {
                json["Oem"]["Nvidia"]["DRAMMemoryCacheMissPercent"] =
                    *dramMemoryCacheMissPercent;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for DRAMCacheMissPercent");
            }
            if (c2CRawTxGbps != nullptr)
            {
                json["Oem"]["Nvidia"]["C2CRawTxBandwidthGbps"] = *c2CRawTxGbps;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for C2CRawTxBandwidthGbps");
            }
            if (c2CRawRxGbps != nullptr)
            {
                json["Oem"]["Nvidia"]["C2CRawRxBandwidthGbps"] = *c2CRawRxGbps;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for C2CRawRxBandwidthGbps");
            }
            if (c2CDataTxGbps != nullptr)
            {
                json["Oem"]["Nvidia"]["C2CDataTxBandwidthGbps"] =
                    *c2CDataTxGbps;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for C2CDataTxBandwidthGbps");
            }
            if (c2CDataRxGbps != nullptr)
            {
                json["Oem"]["Nvidia"]["C2CDataRxBandwidthGbps"] =
                    *c2CDataRxGbps;
            }
            else
            {
                BMCWEB_LOG_DEBUG("Null value returned "
                                 "for C2CDataRxBandwidthGbps");
            }
        });
}

// TODO: Remove ?
/**
 * Handle the D-Bus response from attempting to set the CPU's AppliedConfig
 * property. Main task is to translate error messages into Redfish errors.
 *
 * @param[in,out]   resp    HTTP response.
 * @param[in]       setPropVal  Value which we attempted to set.
 * @param[in]       ec      D-Bus response error code.
 * @param[in]       msg     D-Bus response message.
 */
inline void handleAppliedConfigResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& setPropVal, boost::system::error_code& ec,
    const sdbusplus::message_t& msg)
{
    if (!ec)
    {
        BMCWEB_LOG_DEBUG("Set Property succeeded");
        return;
    }

    BMCWEB_LOG_DEBUG("Set Property failed: {}", ec);

    const sd_bus_error* dbusError = msg.get_error();
    if (dbusError == nullptr)
    {
        messages::internalError(resp->res);
        return;
    }

    // The asio error code doesn't know about our custom errors, so we have to
    // parse the error string. Some of these D-Bus -> Redfish translations are a
    // stretch, but it's good to try to communicate something vaguely useful.
    if (strcmp(dbusError->name,
               "xyz.openbmc_project.Common.Error.InvalidArgument") == 0)
    {
        // Service did not like the object_path we tried to set.
        messages::propertyValueIncorrect(
            resp->res, "AppliedOperatingConfig/@odata.id", setPropVal);
    }
    else if (strcmp(dbusError->name,
                    "xyz.openbmc_project.Common.Error.NotAllowed") == 0)
    {
        // Service indicates we can never change the config for this processor.
        messages::propertyNotWritable(resp->res, "AppliedOperatingConfig");
    }
    else if (strcmp(dbusError->name,
                    "xyz.openbmc_project.Common.Error.Unavailable") == 0)
    {
        // Service indicates the config cannot be changed right now, but maybe
        // in a different system state.
        messages::resourceInStandby(resp->res);
    }
    else
    {
        messages::internalError(resp->res);
    }
}

/**
 * Handle the PATCH operation of the MIG Mode Property. Do basic
 * validation of the input data, and then set the D-Bus property.
 *
 * @param[in,out]   resp            Async HTTP response.
 * @param[in]       processorId     Processor's Id.
 * @param[in]       migMode         New property value to apply.
 * @param[in]       cpuObjectPath   Path of CPU object to modify.
 * @param[in]       serviceMap      Service map for CPU object.
 */
inline void patchMigMode(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                         const std::string& processorId, const bool migMode,
                         const std::string& cpuObjectPath,
                         const processor_utils::MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::ranges::find(interfaceList, "com.nvidia.MigMode") !=
            interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        BMCWEB_LOG_DEBUG(" GpuMIGMode interface not found ");
        messages::internalError(resp->res);
        return;
    }

    dbus::utility::getDbusObject(
        cpuObjectPath,
        std::array<std::string_view, 1>{
            nvidia_async_operation_utils::setAsyncInterfaceName},
        [resp, migMode, processorId, cpuObjectPath,
         service =
             *inventoryService](const boost::system::error_code& ec,
                                const dbus::utility::MapperGetObject& object) {
            if (!ec)
            {
                for (const auto& [serv, _] : object)
                {
                    if (serv != service)
                    {
                        continue;
                    }

                    BMCWEB_LOG_DEBUG(
                        "Performing Patch using Set Async Method Call");

                    nvidia_async_operation_utils::
                        doGenericSetAsyncAndGatherResult(
                            resp, std::chrono::seconds(60), service,
                            cpuObjectPath, "com.nvidia.MigMode",
                            "MIGModeEnabled", std::variant<bool>(migMode),
                            nvidia_async_operation_utils::PatchMigModeCallback{
                                resp});

                    return;
                }
            }

            BMCWEB_LOG_DEBUG("Performing Patch using set-property Call");

            // Set the property, with handler to check error responses
            dbus::utility::async_method_call(
                [resp, processorId](boost::system::error_code& ec1,
                                    const int retValue) {
                    if (!ec1)
                    {
                        if (retValue != 0)
                        {
                            BMCWEB_LOG_ERROR("{}", retValue);
                            messages::internalError(resp->res);
                        }
                        BMCWEB_LOG_DEBUG("CPU:{} Reset Succeded", processorId);
                        messages::success(resp->res);
                        return;
                    }
                    BMCWEB_LOG_DEBUG("{}", ec1);
                    messages::internalError(resp->res);
                    return;
                },
                service, cpuObjectPath,
                "xyz.openbmc_project.Control.Processor.Reset", "Reset");
        });
}

/**
 * Do basic validation of the input data, and then set the D-Bus property.
 *
 * @param[in,out]   resp                Async HTTP response.
 * @param[in]       service             Service for effecter object.
 * @param[in]       objPath             Path of effecter object to modify.
 * @param[in]       remoteDebugEnables  New property value to apply.
 */
inline void setProcessorRemoteDebugState(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath, const bool remoteDebugEnabled)
{
    // Set the property, with handler to check error responses
    dbus::utility::async_method_call(
        [aResp, objPath](const boost::system::error_code& ec,
                         sdbusplus::message::message& msg) {
            if (!ec)
            {
                BMCWEB_LOG_DEBUG("Set Processor Remote Debug successed");
                messages::success(aResp->res);
                return;
            }

            BMCWEB_LOG_DEBUG("Set Processor Remote Debug failed: {}", ec);

            // Read and convert dbus error message to redfish error
            const sd_bus_error* dbusError = msg.get_error();
            if (dbusError == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }

            if (strcmp(dbusError->name, "xyz.openbmc_project.Common."
                                        "Device.Error.WriteFailure") == 0)
            {
                // Service failed to change the config
                messages::operationFailed(aResp->res);
            }
            else
            {
                messages::internalError(aResp->res);
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.Control.Processor.RemoteDebug", "Enabled",
        std::variant<bool>(remoteDebugEnabled));
}

/**
 * Handle the PATCH operation of the RemoteDebugEnabled Property.
 *
 * @param[in,out]   resp                Async HTTP response.
 * @param[in]       processorId         Processor's Id.
 * @param[in]       remoteDebugEnables  New property value to apply.
 * @param[in]       cpuObjectPath       Path of CPU object to modify.
 */
inline void patchRemoteDebug(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                             const std::string& processorId,
                             const bool remoteDebugEnabled,
                             const std::string& cpuObjectPath)
{
    BMCWEB_LOG_DEBUG("Set Remote Debug {} on CPU: {}",
                     std::to_string(static_cast<int>(remoteDebugEnabled)),
                     processorId);

    // Find remote debug effecters from all effecters attached to "all_controls"
    dbus::utility::async_method_call(
        [aResp,
         remoteDebugEnabled](const boost::system::error_code& ec,
                             std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                // No state effecter attached.
                BMCWEB_LOG_DEBUG(" No state effecter attached. ");
                messages::internalError(aResp->res);
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }
            for (const std::string& effecterPath : *data)
            {
                BMCWEB_LOG_DEBUG("State Effecter Object Path {}", effecterPath);

                const std::array<const char*, 1> effecterInterfaces = {
                    "xyz.openbmc_project.Control.Processor.RemoteDebug"};
                // Process sensor reading
                dbus::utility::async_method_call(
                    [aResp, effecterPath, remoteDebugEnabled](
                        const boost::system::error_code& getObjectError,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& object) {
                        if (getObjectError)
                        {
                            // The path does not implement any state interfaces.
                            BMCWEB_LOG_DEBUG(
                                " No any state effecter interface. ");
                            messages::internalError(aResp->res);
                            return;
                        }

                        for (const auto& [serviceName, interfaces] : object)
                        {
                            if (std::ranges::find(
                                    interfaces,
                                    "xyz.openbmc_project.Control.Processor.RemoteDebug") !=
                                interfaces.end())
                            {
                                setProcessorRemoteDebugState(
                                    aResp, serviceName, effecterPath,
                                    remoteDebugEnabled);
                            }
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetObject",
                    effecterPath, effecterInterfaces);
            }
        },
        "xyz.openbmc_project.ObjectMapper", cpuObjectPath + "/all_controls",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * Handle the PATCH operation of the speed config property. Do basic
 * validation of the input data, and then set the D-Bus property.
 *
 * @param[in,out]   resp            Async HTTP response.
 * @param[in]       processorId     Processor's Id.
 * @param[in]       speedConfig     New property value to apply.
 * @param[in]       cpuObjectPath   Path of CPU object to modify.
 * @param[in]       serviceMap      Service map for CPU object.
 */
inline void patchSpeedConfig(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& processorId,
    const std::tuple<bool, uint32_t>& reqSpeedConfig,
    const std::string& cpuObjectPath,
    const processor_utils::MapperServiceMap& serviceMap)
{
    BMCWEB_LOG_DEBUG("Setting SpeedConfig");
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::ranges::find(
                interfaceList,
                "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig") !=
            interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        messages::internalError(resp->res);
        return;
    }
    BMCWEB_LOG_DEBUG("patchSpeedConfig");

    dbus::utility::getDbusObject(
        cpuObjectPath,
        std::array<std::string_view, 1>{
            nvidia_async_operation_utils::setAsyncInterfaceName},
        [resp, reqSpeedConfig, processorId, cpuObjectPath,
         service =
             *inventoryService](const boost::system::error_code& ec,
                                const dbus::utility::MapperGetObject& object) {
            if (!ec)
            {
                for (const auto& [serv, _] : object)
                {
                    if (serv != service)
                    {
                        continue;
                    }

                    BMCWEB_LOG_DEBUG(
                        "Performing Patch using Set Async Method Call");

                    nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
                        resp, std::chrono::seconds(60), service, cpuObjectPath,
                        "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig",
                        "SpeedConfig",
                        std::variant<std::tuple<bool, uint32_t>>(
                            reqSpeedConfig),
                        nvidia_async_operation_utils::PatchSpeedConfigCallback{
                            resp, std::get<1>(reqSpeedConfig)});

                    return;
                }
            }

            BMCWEB_LOG_DEBUG("Performing Patch using set-property Call");

            dbus::utility::async_method_call(
                [resp, processorId,
                 reqSpeedConfig](boost::system::error_code& ec1,
                                 sdbusplus::message::message& msg) {
                    if (!ec1)
                    {
                        BMCWEB_LOG_DEBUG("Set speed config property succeeded");
                        return;
                    }

                    BMCWEB_LOG_DEBUG(
                        "CPU:{} set speed config property failed: {}",
                        processorId, ec1);
                    // Read and convert dbus error message to redfish error
                    const sd_bus_error* dbusError = msg.get_error();
                    if (dbusError == nullptr)
                    {
                        messages::internalError(resp->res);
                        return;
                    }
                    if (strcmp(
                            dbusError->name,
                            "xyz.openbmc_project.Common.Error.InvalidArgument") ==
                        0)
                    {
                        // Invalid value
                        uint32_t speedLimit = std::get<1>(reqSpeedConfig);
                        messages::propertyValueIncorrect(
                            resp->res, "SpeedLimitMHz",
                            std::to_string(speedLimit));
                    }
                    else if (
                        strcmp(
                            dbusError->name,
                            "xyz.openbmc_project.Common.Error.Unavailable") ==
                        0)
                    {
                        std::string errBusy = "0x50A";
                        std::string errBusyResolution =
                            "SMBPBI Command failed with error busy, please try after 60 seconds";

                        // busy error
                        messages::asyncError(resp->res, errBusy,
                                             errBusyResolution);
                    }
                    else if (strcmp(
                                 dbusError->name,
                                 "xyz.openbmc_project.Common.Error.Timeout") ==
                             0)
                    {
                        std::string errTimeout = "0x600";
                        std::string errTimeoutResolution =
                            "Settings may/maynot have applied, please check get response before patching";

                        // timeout error
                        messages::asyncError(resp->res, errTimeout,
                                             errTimeoutResolution);
                    }
                    else if (strcmp(dbusError->name,
                                    "xyz.openbmc_project.Common."
                                    "Device.Error.WriteFailure") == 0)
                    {
                        // Service failed to change the config
                        messages::operationFailed(resp->res);
                    }
                    else
                    {
                        messages::internalError(resp->res);
                    }
                },
                service, cpuObjectPath, "org.freedesktop.DBus.Properties",
                "Set", "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig",
                "SpeedConfig",
                std::variant<std::tuple<bool, uint32_t>>(reqSpeedConfig));
        });
}

/**
 * Handle the PATCH operation of the speed locked property. Do basic
 * validation of the input data, and then set the D-Bus property.
 *
 * @param[in,out]   resp            Async HTTP response.
 * @param[in]       processorId     Processor's Id.
 * @param[in]       speedLocked     New property value to apply.
 * @param[in]       cpuObjectPath   Path of CPU object to modify.
 * @param[in]       serviceMap      Service map for CPU object.
 */
inline void patchSpeedLocked(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& processorId, const bool speedLocked,
    const std::string& cpuObjectPath,
    const processor_utils::MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::ranges::find(
                interfaceList,
                "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig") !=
            interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        messages::internalError(resp->res);
        return;
    }
    const std::string conName = *inventoryService;
    dbus::utility::getProperty<std::tuple<bool, uint32_t>>(
        conName, cpuObjectPath,
        "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig", "SpeedConfig",
        [resp, processorId, conName, cpuObjectPath, serviceMap,
         speedLocked](const boost::system::error_code& ec,
                      const std::tuple<bool, uint32_t>& speedConfig) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for SpeedConfig");
                messages::internalError(resp->res);
                return;
            }
            std::tuple<bool, uint32_t> reqSpeedConfig;
            uint32_t cachedSpeedLimit = std::get<1>(speedConfig);
            reqSpeedConfig = std::make_tuple(speedLocked, cachedSpeedLimit);
            patchSpeedConfig(resp, processorId, reqSpeedConfig, cpuObjectPath,
                             serviceMap);
        });
}

/**
 * Handle the PATCH operation of the speed limit property. Do basic
 * validation of the input data, and then set the D-Bus property.
 *
 * @param[in,out]   resp            Async HTTP response.
 * @param[in]       processorId     Processor's Id.
 * @param[in]       speedLimit      New property value to apply.
 * @param[in]       cpuObjectPath   Path of CPU object to modify.
 * @param[in]       serviceMap      Service map for CPU object.
 */
inline void patchSpeedLimit(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                            const std::string& processorId,
                            const int speedLimit,
                            const std::string& cpuObjectPath,
                            const processor_utils::MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::ranges::find(
                interfaceList,
                "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig") !=
            interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        messages::internalError(resp->res);
        return;
    }
    const std::string conName = *inventoryService;
    BMCWEB_LOG_DEBUG("patchSpeedLimit");
    // Set the property, with handler to check error responses
    dbus::utility::getProperty<std::tuple<bool, uint32_t>>(
        conName, cpuObjectPath,
        "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig", "SpeedConfig",
        [resp, processorId, conName, cpuObjectPath, serviceMap,
         speedLimit](const boost::system::error_code& ec,
                     const std::tuple<bool, uint32_t>& speedConfig) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error for SpeedConfig");
                messages::internalError(resp->res);
                return;
            }
            std::tuple<bool, uint32_t> reqSpeedConfig;
            bool cachedSpeedLocked = std::get<0>(speedConfig);
            reqSpeedConfig = std::make_tuple(cachedSpeedLocked,
                                             static_cast<uint32_t>(speedLimit));
            patchSpeedConfig(resp, processorId, reqSpeedConfig, cpuObjectPath,
                             serviceMap);
        });
}

inline void getProcessorDataByService(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get processor metrics data.");
    dbus::utility::async_method_call(
        [aResp{aResp}](const boost::system::error_code& ec,
                       const OperatingConfigProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& property : properties)
            {
                if (property.first == "OperatingSpeed")
                {
                    const uint32_t* value =
                        std::get_if<uint32_t>(&property.second);
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
        "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig");
}

inline void getProcessorMemoryECCData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get processor memory ecc data.");
    dbus::utility::async_method_call(
        [aResp{aResp}](const boost::system::error_code& ec,
                       const OperatingConfigProperties& properties) {
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
                    aResp->res.jsonValue["CacheMetricsTotal"]["LifeTime"]
                                        ["CorrectableECCErrorCount"] = *value;
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
                    aResp->res.jsonValue["CacheMetricsTotal"]["LifeTime"]
                                        ["UncorrectableECCErrorCount"] = *value;
                }
                if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                {
                    if (property.first == "isThresholdExceeded")
                    {
                        const bool* value = std::get_if<bool>(&property.second);
                        if (value == nullptr)
                        {
                            BMCWEB_LOG_ERROR(
                                "NULL Value returned for isThresholdExceeded Property");
                            messages::internalError(aResp->res);
                            return;
                        }
                        aResp->res.jsonValue["Oem"]["Nvidia"]
                                            ["SRAMECCErrorThresholdExceeded"] =
                            *value;
                    }
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Memory.MemoryECC");
}

inline void getVoltageData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const std::string& service,
                           const std::string& chassisId,
                           const std::string& sensorPath)
{
    dbus::utility::async_method_call(
        [aResp, chassisId, sensorPath](
            const boost::system::error_code& ec,
            const std::vector<
                std::pair<std::string, std::variant<std::string, double>>>&
                propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Can't get sensor reading");
                return;
            }
            sdbusplus::object_path objectPath(sensorPath);
            const std::string& sensorName = objectPath.filename();
            std::string sensorURI =
                boost::urls::format("/redfish/v1/Chassis/{}/Sensors/{}",
                                    chassisId, sensorName)
                    .buffer();
            aResp->res.jsonValue["CoreVoltage"]["DataSourceUri"] = sensorURI;
            const double* attributeValue = nullptr;
            for (const std::pair<std::string,
                                 std::variant<std::string, double>>& property :
                 propertiesList)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "Value")
                {
                    attributeValue = std::get_if<double>(&property.second);
                    if (attributeValue != nullptr)
                    {
                        aResp->res.jsonValue["CoreVoltage"]["Reading"] =
                            *attributeValue;
                    }
                }
            }
        },
        service, sensorPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Sensor.Value");
}

inline void getSensorMetric(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            const std::string& service,
                            const std::string& objPath)
{
    dbus::utility::async_method_call(
        [aResp, service,
         objPath](const boost::system::error_code& ec,
                  std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no chassis = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr || data->size() > 1)
            {
                // Object must have single parent chassis
                return;
            }
            const std::string& chassisPath = data->front();
            sdbusplus::object_path objectPath(chassisPath);
            std::string chassisName = objectPath.filename();
            if (chassisName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            const std::string& chassisId = chassisName;
            dbus::utility::async_method_call(
                [aResp, service, objPath,
                 chassisId](const boost::system::error_code& getEndpointsError,
                            std::variant<std::vector<std::string>>& resp1) {
                    if (getEndpointsError)
                    {
                        // No sensors are expected when Host is off
                        BMCWEB_LOG_DEBUG("No sensors attached for {}",
                                         chassisId);
                        return;
                    }
                    std::vector<std::string>* data1 =
                        std::get_if<std::vector<std::string>>(&resp1);
                    if (data1 == nullptr)
                    {
                        return;
                    }
                    for (const std::string& sensorPath : *data1)
                    {
                        std::vector<std::string> split;
                        // Reserve space for
                        // /xyz/openbmc_project/sensors/<name>/<subname>
                        split.reserve(6);
                        bmcweb::split(split, sensorPath, '/');
                        if (split.size() < 6)
                        {
                            BMCWEB_LOG_ERROR(
                                "Got path that isn't long enough {}", objPath);
                            continue;
                        }
                        const std::string& sensorType = split[4];
                        if (sensorType == "voltage")
                        {
                            getVoltageData(aResp, service, chassisId,
                                           sensorPath);
                        }
                    }
                },
                "xyz.openbmc_project.ObjectMapper",
                chassisPath + "/all_sensors", "org.freedesktop.DBus.Properties",
                "Get", "xyz.openbmc_project.Association", "endpoints");
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getMetricValueSensorMetric(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& deviceType)
{
    dbus::utility::getAssociationEndPoints(
        objPath + "/measured_by",
        [aResp, deviceType](const boost::system::error_code& ec,
                            const dbus::utility::MapperEndPoints& metricPaths) {
            if (ec)
            {
                return;
            }
            constexpr std::array<std::string_view, 1> metricInterfaces = {
                "xyz.openbmc_project.Metric.Value"};
            for (const std::string& metricPath : metricPaths)
            {
                dbus::utility::getDbusObject(
                    metricPath, metricInterfaces,
                    [aResp, metricPath,
                     deviceType](const boost::system::error_code& ec2,
                                 const dbus::utility::MapperGetObject& object) {
                        if (ec2 || object.empty())
                        {
                            return;
                        }
                        const auto& [serviceName, interfaces] = *object.begin();
                        getMetricValueSensorData(aResp, serviceName, metricPath,
                                                 deviceType);
                    });
            }
        });
}

inline void getStateSensorMetric(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath, const std::string& deviceType)
{
    dbus::utility::async_method_call(
        [aResp, service, objPath,
         deviceType](const boost::system::error_code& ec,
                     std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                // No state sensors attached.
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }

            for (const std::string& sensorPath : *data)
            {
                BMCWEB_LOG_DEBUG("State Sensor Object Path {}", sensorPath);

                const std::array<const char*, 4> sensorInterfaces = {
                    "xyz.openbmc_project.State.Decorator.PowerSystemInputs",
                    "xyz.openbmc_project.State.ProcessorPerformance",
                    "com.nvidia.MemorySpareChannel",
                    "com.nvidia.ProcessorPowerBreak"};
                // Process sensor reading
                dbus::utility::async_method_call(
                    [aResp, sensorPath, deviceType](
                        const boost::system::error_code& getObjectError,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& object) {
                        if (getObjectError)
                        {
                            // The path does not implement any state
                            // interfaces.
                            return;
                        }

                        for (const auto& [serviceName, interfaces] : object)
                        {
                            if (std::ranges::find(
                                    interfaces,
                                    "xyz.openbmc_project.State.ProcessorPerformance") !=
                                interfaces.end())
                            {
                                getProcessorPerformanceData(
                                    aResp, serviceName, sensorPath, deviceType);
                            }
                            if (std::ranges::find(
                                    interfaces,
                                    "xyz.openbmc_project.State.Decorator.PowerSystemInputs") !=
                                interfaces.end())
                            {
                                getPowerSystemInputsData(
                                    aResp, serviceName, sensorPath, deviceType);
                            }
                            if (std::ranges::find(
                                    interfaces,
                                    "com.nvidia.MemorySpareChannel") !=
                                interfaces.end())
                            {
                                getMemorySpareChannelPresenceData(
                                    aResp, serviceName, sensorPath, deviceType);
                            }
                            if (std::ranges::find(
                                    interfaces,
                                    "com.nvidia.ProcessorPowerBreak") !=
                                interfaces.end())
                            {
                                getPowerBreakThrottleData(
                                    aResp, serviceName, sensorPath, deviceType);
                            }
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetObject", sensorPath,
                    sensorInterfaces);
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_states",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getProcessorMetricsData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& processorId)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    dbus::utility::async_method_call(
        [processorId, aResp{aResp}](
            const boost::system::error_code& ec,
            const boost::container::flat_map<
                std::string, boost::container::flat_map<
                                 std::string, std::vector<std::string>>>&
                subtree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);

                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!path.ends_with(processorId))
                {
                    continue;
                }
                std::string processorMetricsURI =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/Processors/";
                processorMetricsURI += processorId;
                processorMetricsURI += "/ProcessorMetrics";
                aResp->res.jsonValue["@odata.type"] =
                    "#ProcessorMetrics.v1_6_1.ProcessorMetrics";
                aResp->res.jsonValue["@odata.id"] = processorMetricsURI;
                aResp->res.jsonValue["Id"] = "ProcessorMetrics";
                aResp->res.jsonValue["Name"] =
                    processorId + " Processor Metrics";
                for (const auto& [service, interfaces] : object)
                {
                    std::string deviceType;
                    if (std::ranges::find(
                            interfaces,
                            "xyz.openbmc_project.Inventory.Item.Accelerator") !=
                        interfaces.end())
                    {
                        deviceType =
                            "xyz.openbmc_project.Inventory.Item.Accelerator";
                    }
                    else
                    {
                        deviceType = "xyz.openbmc_project.Inventory.Item.Cpu";
                    }

                    if (std::ranges::find(
                            interfaces,
                            "xyz.openbmc_project.Inventory.Item.Cpu."
                            "OperatingConfig") != interfaces.end())
                    {
                        getProcessorDataByService(aResp, service, path);
                    }
                    if (std::ranges::find(
                            interfaces,
                            "xyz.openbmc_project.Memory.MemoryECC") !=
                        interfaces.end())
                    {
                        getProcessorMemoryECCData(aResp, service, path);
                    }
                    if (std::ranges::find(interfaces,
                                          "xyz.openbmc_project.PCIe.PCIeECC") !=
                        interfaces.end())
                    {
                        redfish::processor_utils::getPCIeErrorData(
                            aResp, service, path);
                    }
                    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                    {
                        if (std::ranges::find(
                                interfaces,
                                "com.nvidia.NVLink.NVLinkMetrics") !=
                            interfaces.end())
                        {
                            getGPUNvlinkMetricsData(
                                aResp, service, path,
                                "com.nvidia.NVLink.NVLinkMetrics");
                        }

                        if (std::ranges::find(interfaces,
                                              "com.nvidia.GPMMetrics") !=
                            interfaces.end())
                        {
                            // Assign the device type to Accelerator because we
                            // have found the GPMMetrics interface here
                            deviceType =
                                "xyz.openbmc_project.Inventory.Item.Accelerator";
                            getGPMMetricsData(aResp, service, path,
                                              "com.nvidia.GPMMetrics");
                        }

                        if (std::ranges::find(interfaces,
                                              "com.nvidia.SMUtilization") !=
                            interfaces.end())
                        {
                            nvidia_processor_utils::getSMUtilizationData(
                                aResp, service, path);
                        }

                        // Move to the end because deviceType might be
                        // reassigned
                        if (std::ranges::find(
                                interfaces,
                                "xyz.openbmc_project.State.ProcessorPerformance") !=
                            interfaces.end())
                        {
                            getProcessorPerformanceData(aResp, service, path,
                                                        deviceType);
                        }
                    }
                    getSensorMetric(aResp, service, path);

                    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                    {
                        getStateSensorMetric(aResp, service, path, deviceType);
                        getMetricValueSensorMetric(aResp, path, deviceType);
                    }
                }
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#Processor.v1_20_0.Processor", processorId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 3>{
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "xyz.openbmc_project.Inventory.Item.Cpu", "com.nvidia.GPMMetrics"});
}

inline void getProcessorMemoryDataByService(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& memoryPath, const int64_t& processorCECount,
    const int64_t& processorUECount)
{
    BMCWEB_LOG_DEBUG("Get processor memory data");
    dbus::utility::async_method_call(
        [aResp, memoryPath, processorCECount,
         processorUECount](const boost::system::error_code& ec,
                           dbus::utility::GetSubTreeType& subtree) {
            if (ec)
            {
                messages::internalError(aResp->res);
                return;
            }
            // Iterate over all retrieved ObjectPaths.
            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                     object : subtree)
            {
                // Get the processor memory
                if (object.first != memoryPath)
                {
                    continue;
                }
                const std::vector<
                    std::pair<std::string, std::vector<std::string>>>&
                    connectionNames = object.second;
                if (connectionNames.empty())
                {
                    BMCWEB_LOG_ERROR("Got 0 Connection names");
                    continue;
                }

                for (const auto& i : connectionNames)
                {
                    const std::string& connectionName = i.first;
                    dbus::utility::async_method_call(
                        [aResp{aResp}, processorCECount, processorUECount](
                            const boost::system::error_code& ec1,
                            const OperatingConfigProperties& properties) {
                            if (ec1)
                            {
                                BMCWEB_LOG_DEBUG("DBUS response error");
                                messages::internalError(aResp->res);
                                return;
                            }
                            for (const auto& property : properties)
                            {
                                if (property.first ==
                                    "MemoryConfiguredSpeedInMhz")
                                {
                                    const uint16_t* value =
                                        std::get_if<uint16_t>(&property.second);
                                    if (value == nullptr)
                                    {
                                        messages::internalError(aResp->res);
                                        return;
                                    }
                                    aResp->res.jsonValue["OperatingSpeedMHz"] =
                                        *value;
                                }
                                else if (property.first == "Utilization")
                                {
                                    const double* value =
                                        std::get_if<double>(&property.second);
                                    if (value == nullptr)
                                    {
                                        messages::internalError(aResp->res);
                                        return;
                                    }
                                    aResp->res.jsonValue["BandwidthPercent"] =
                                        *value;
                                }
                                else if (property.first == "ceCount")
                                {
                                    const int64_t* value =
                                        std::get_if<int64_t>(&property.second);
                                    if (value == nullptr)
                                    {
                                        messages::internalError(aResp->res);
                                        return;
                                    }
                                    aResp->res
                                        .jsonValue["LifeTime"]
                                                  ["CorrectableECCErrorCount"] =
                                        *value + processorCECount;
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
                                    aResp->res.jsonValue
                                        ["LifeTime"]
                                        ["UncorrectableECCErrorCount"] =
                                        *value + processorUECount;
                                }
                            }
                        },
                        connectionName, memoryPath,
                        "org.freedesktop.DBus.Properties", "GetAll", "");
                }
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", objPath, 0,
        std::array<const char*, 1>{"xyz.openbmc_project.Inventory.Item.Dimm"});
}

inline void getProcessorMemorySummary(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const int64_t& processorCECount, const int64_t& processorUECount)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    // Get processor memory
    dbus::utility::async_method_call(
        [aResp, processorCECount,
         processorUECount](const boost::system::error_code& ec,
                           std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                return; // no memory = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }
            for (const std::string& memoryPath : *data)
            {
                // Get subtree for memory parent path
                size_t separator = memoryPath.rfind('/');
                if (separator == std::string::npos)
                {
                    BMCWEB_LOG_ERROR("Invalid memory path");
                    continue;
                }
                std::string parentPath = memoryPath.substr(0, separator);
                // Get entity subtree
                getProcessorMemoryDataByService(aResp, parentPath, memoryPath,
                                                processorCECount,
                                                processorUECount);
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_memory",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getProcessorMemoryMetricsData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& processorId)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    dbus::utility::async_method_call(
        [processorId, aResp{aResp}](
            const boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, boost::container::flat_map<
                                 std::string, std::vector<std::string>>>&
                subtree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);

                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!path.ends_with(processorId))
                {
                    continue;
                }
                std::string memoryMetricsURI =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/Processors/";
                memoryMetricsURI += processorId;
                memoryMetricsURI += "/MemorySummary/MemoryMetrics";
                aResp->res.jsonValue["@odata.type"] =
                    "#MemoryMetrics.v1_7_0.MemoryMetrics";
                aResp->res.jsonValue["@odata.id"] = memoryMetricsURI;
                aResp->res.jsonValue["Id"] = "MemoryMetrics";
                aResp->res.jsonValue["Name"] =
                    processorId + " Memory Summary Metrics";
                // Get processor cache memory ECC counts
                for (const auto& [service, interfaces] : object)
                {
                    const std::string memoryECCInterface =
                        "xyz.openbmc_project.Memory.MemoryECC";
                    const std::string memoryMetricIface =
                        "xyz.openbmc_project.Inventory.Item.Dimm.MemoryMetrics";

                    if (std::ranges::find(interfaces, memoryECCInterface) !=
                        interfaces.end())
                    {
                        dbus::utility::async_method_call(
                            [path = path, aResp{aResp}](
                                const boost::system::error_code ec1,
                                const OperatingConfigProperties& properties) {
                                if (ec1)
                                {
                                    BMCWEB_LOG_DEBUG("DBUS response error");
                                    messages::internalError(aResp->res);
                                    return;
                                }
                                // Get processor memory error counts to combine
                                // to memory summary error counts
                                int64_t processorCECount = 0;
                                int64_t processorUECount = 0;
                                for (const auto& property : properties)
                                {
                                    if (property.first == "ceCount")
                                    {
                                        const int64_t* value =
                                            std::get_if<int64_t>(
                                                &property.second);
                                        if (value == nullptr)
                                        {
                                            messages::internalError(aResp->res);
                                            return;
                                        }
                                        processorCECount = *value;
                                    }
                                    else if (property.first == "ueCount")
                                    {
                                        const int64_t* value =
                                            std::get_if<int64_t>(
                                                &property.second);
                                        if (value == nullptr)
                                        {
                                            messages::internalError(aResp->res);
                                            return;
                                        }
                                        processorUECount = *value;
                                    }
                                }
                                // Get processor memory summary data
                                getProcessorMemorySummary(aResp, path,
                                                          processorCECount,
                                                          processorUECount);
                            },
                            service, path, "org.freedesktop.DBus.Properties",
                            "GetAll", memoryECCInterface);
                    }
                    if (std::ranges::find(interfaces, memoryMetricIface) !=
                        interfaces.end())
                    {
                        dbus::utility::async_method_call(
                            [aResp{aResp}](
                                const boost::system::error_code ec2,
                                const OperatingConfigProperties& properties) {
                                if (ec2)
                                {
                                    BMCWEB_LOG_DEBUG(
                                        "DBUS response error for processor memory metrics");
                                    messages::internalError(aResp->res);
                                    return;
                                }

                                for (const auto& property : properties)
                                {
                                    if (property.first ==
                                        "CapacityUtilizationPercent")
                                    {
                                        const uint8_t* value =
                                            std::get_if<uint8_t>(
                                                &property.second);
                                        if (value == nullptr)
                                        {
                                            messages::internalError(aResp->res);
                                            return;
                                        }
                                        aResp->res.jsonValue
                                            ["CapacityUtilizationPercent"] =
                                            *value;
                                    }
                                }
                            },
                            service, path, "org.freedesktop.DBus.Properties",
                            "GetAll", memoryMetricIface);
                    }
                }
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#Processor.v1_20_0.Processor", processorId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 2>{
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "com.nvidia.GPMMetrics"});
}

inline std::string toRequestedApplyTime(const std::string& applyTime)
{
    if (applyTime ==
        "xyz.openbmc_project.Software.ApplyTime.RequestedApplyTimes.Immediate")
    {
        return "Immediate";
    }
    if (applyTime ==
        "xyz.openbmc_project.Software.ApplyTime.RequestedApplyTimes.OnReset")
    {
        return "OnReset";
    }
    // Unknown or others
    return "";
}

inline void getProcessorSettingsData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& processorId)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    dbus::utility::async_method_call(
        [aResp, processorId](
            boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) mutable {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error: {}", ec);
                messages::internalError(aResp->res);
                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!path.ends_with(processorId))
                {
                    continue;
                }
                nlohmann::json& json = aResp->res.jsonValue;
                json["@odata.id"] =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/Processors/" + processorId + "/Settings";
                json["@odata.type"] = "#Processor.v1_20_0.Processor";
                json["Id"] = "Settings";
                json["Name"] = processorId + "PendingSettings";
                for (const auto& [service, interfaces] : object)
                {
                    if (std::ranges::find(
                            interfaces,
                            "xyz.openbmc_project.Memory.MemoryECC") !=
                        interfaces.end())
                    {
                        redfish::nvidia_processor::getEccPendingData(
                            aResp, processorId, service, path);
                    }
                    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                    {
                        if (std::ranges::find(interfaces,
                                              "com.nvidia.CCMode") !=
                            interfaces.end())
                        {
                            redfish::nvidia_processor_utils::
                                getCCModePendingData(aResp, processorId,
                                                     service, path);
                        }
                        if (std::ranges::find(interfaces,
                                              "com.nvidia.EgmMode") !=
                            interfaces.end())
                        {
                            redfish::nvidia_processor_utils::
                                getEgmModePendingData(aResp, processorId,
                                                      service, path);
                        }
                        if (std::ranges::find(interfaces,
                                              "com.nvidia.AdaptiveTGPMode") !=
                            interfaces.end())
                        {
                            redfish::nvidia_processor_utils::
                                getAdaptiveTGPModePendingData(
                                    aResp, processorId, service, path);
                        }
                    }
                    if (std::ranges::find(
                            interfaces,
                            "xyz.openbmc_project.Software.ApplyTime") !=
                        interfaces.end())
                    {
                        dbus::utility::async_method_call(
                            [aResp](
                                const boost::system::error_code& ec1,
                                const OperatingConfigProperties& properties) {
                                if (ec1)
                                {
                                    BMCWEB_LOG_DEBUG("DBUS response error");
                                    messages::internalError(aResp->res);
                                    return;
                                }
                                nlohmann::json& json1 = aResp->res.jsonValue;
                                for (const auto& property : properties)
                                {
                                    if (property.first == "RequestedApplyTime")
                                    {
                                        const std::string* applyTime =
                                            std::get_if<std::string>(
                                                &property.second);
                                        if (applyTime == nullptr)
                                        {
                                            messages::internalError(aResp->res);
                                            return;
                                        }
                                        json1
                                            ["@Redfish.SettingsApplyTime"]
                                            ["@odata.type"] =
                                                "#Settings.v1_3_3.PreferredApplyTime";
                                        json1["@Redfish.SettingsApplyTime"]
                                             ["ApplyTime"] =
                                                 toRequestedApplyTime(
                                                     *applyTime);
                                    }
                                }
                            },
                            service, path, "org.freedesktop.DBus.Properties",
                            "GetAll", "xyz.openbmc_project.Software.ApplyTime");
                    }
                }
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 3>{
            "xyz.openbmc_project.Inventory.Item.Cpu",
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "com.nvidia.GPMMetrics"});
}

inline void patchEccMode(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                         const std::string& processorId,
                         const bool eccModeEnabled,
                         const std::string& cpuObjectPath,
                         const processor_utils::MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::ranges::find(interfaceList,
                              "xyz.openbmc_project.Memory.MemoryECC") !=
            interfaceList.end())
        {
            inventoryService = &serviceName;
            break;
        }
    }
    if (inventoryService == nullptr)
    {
        messages::internalError(resp->res);
        return;
    }

    dbus::utility::getDbusObject(
        cpuObjectPath,
        std::array<std::string_view, 1>{
            nvidia_async_operation_utils::setAsyncInterfaceName},
        [resp, eccModeEnabled, processorId, cpuObjectPath,
         service =
             *inventoryService](const boost::system::error_code& ec,
                                const dbus::utility::MapperGetObject& object) {
            if (!ec)
            {
                for (const auto& [serv, _] : object)
                {
                    if (serv != service)
                    {
                        continue;
                    }

                    BMCWEB_LOG_DEBUG(
                        "Performing Patch using Set Async Method Call");

                    nvidia_async_operation_utils::
                        doGenericSetAsyncAndGatherResult(
                            resp, std::chrono::seconds(60), service,
                            cpuObjectPath,
                            "xyz.openbmc_project.Memory.MemoryECC",
                            "ECCModeEnabled",
                            std::variant<bool>(eccModeEnabled),
                            nvidia_async_operation_utils::PatchEccModeCallback{
                                resp});

                    return;
                }
            }

            BMCWEB_LOG_DEBUG("Performing Patch using set-property Call");
            // Set the property, with handler to check error responses
            dbus::utility::async_method_call(
                [resp, processorId](boost::system::error_code& ec1,
                                    sdbusplus::message::message& msg) {
                    if (!ec1)
                    {
                        BMCWEB_LOG_DEBUG("Set eccModeEnabled succeeded");
                        messages::success(resp->res);
                        return;
                    }
                    BMCWEB_LOG_DEBUG(
                        "CPU:{} set eccModeEnabled property failed: {}",
                        processorId, ec1);
                    // Read and convert dbus error message to redfish error
                    const sd_bus_error* dbusError = msg.get_error();
                    if (dbusError == nullptr)
                    {
                        messages::internalError(resp->res);
                        return;
                    }

                    if (strcmp(dbusError->name,
                               "xyz.openbmc_project.Common."
                               "Device.Error.WriteFailure") == 0)
                    {
                        // Service failed to change the config
                        messages::operationFailed(resp->res);
                    }
                    else if (
                        strcmp(
                            dbusError->name,
                            "xyz.openbmc_project.Common.Error.Unavailable") ==
                        0)
                    {
                        std::string errBusy = "0x50A";
                        std::string errBusyResolution =
                            "SMBPBI Command failed with error busy, please try after 60 seconds";

                        // busy error
                        messages::asyncError(resp->res, errBusy,
                                             errBusyResolution);
                    }
                    else if (strcmp(
                                 dbusError->name,
                                 "xyz.openbmc_project.Common.Error.Timeout") ==
                             0)
                    {
                        std::string errTimeout = "0x600";
                        std::string errTimeoutResolution =
                            "Settings may/maynot have applied, please check get response before patching";

                        // timeout error
                        messages::asyncError(resp->res, errTimeout,
                                             errTimeoutResolution);
                    }
                    else
                    {
                        messages::internalError(resp->res);
                    }
                },
                service, cpuObjectPath, "org.freedesktop.DBus.Properties",
                "Set", "xyz.openbmc_project.Memory.MemoryECC", "ECCModeEnabled",
                std::variant<bool>(eccModeEnabled));
        });
}

inline void patchSpeedConfigIfRequested(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath, const MapperServiceMap& serviceMap,
    const std::string& processorId, const std::optional<int>& speedLimit,
    const std::optional<bool>& speedLocked)
{
    // speedlimit is required property for patching speedlocked
    if (!speedLimit && speedLocked)
    {
        BMCWEB_LOG_ERROR("SpeedLimit value required ");
        messages::propertyMissing(asyncResp->res, "SpeedLimit");
    }

    // Update speed limit
    else if (speedLimit && speedLocked)
    {
        std::tuple<bool, uint32_t> reqSpeedConfig =
            std::make_tuple(*speedLocked, static_cast<uint32_t>(*speedLimit));
        redfish::nvidia_processor::patchSpeedConfig(
            asyncResp, processorId, reqSpeedConfig, objectPath, serviceMap);
    }
}

inline void patchOperatingSpeedRangeIfRequested(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath,
    [[maybe_unused]] const MapperServiceMap& serviceMap,
    const std::string& processorId,
    const std::optional<nlohmann::json>& operatingSpeedRangeMHzObject)
{
    if (operatingSpeedRangeMHzObject)
    {
        std::optional<uint32_t> settingMin;
        std::optional<uint32_t> settingMax;
        nlohmann::json operatingSpeedRange = *operatingSpeedRangeMHzObject;
        if (redfish::json_util::readJson(operatingSpeedRange, asyncResp->res,
                                         "SettingMax", settingMax, "SettingMin",
                                         settingMin))
        {
            if (settingMin && settingMax)
            {
                redfish::nvidia_processor_utils::patchOperatingSpeedRangeMHz(
                    asyncResp, processorId,
                    std::make_tuple(*settingMin, *settingMax), "SettingRange",
                    objectPath);
            }
            else if (settingMax)
            {
                redfish::nvidia_processor_utils::patchOperatingSpeedRangeMHz(
                    asyncResp, processorId, *settingMax, "SettingMax",
                    objectPath);
            }
            else if (settingMin)
            {
                redfish::nvidia_processor_utils::patchOperatingSpeedRangeMHz(
                    asyncResp, processorId, *settingMin, "SettingMin",
                    objectPath);
            }
        }
    }
}

inline void patchMigModeIfPresent(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& processorId, const std::string& objectPath,
    const MapperServiceMap& serviceMap, const std::optional<bool>& migMode)
{
    if (!migMode)
    {
        return;
    }

    redfish::nvidia_processor::patchMigMode(asyncResp, processorId, *migMode,
                                            objectPath, serviceMap);
}

inline void patchRemoteDebugIfPresent(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& processorId, const std::string& objectPath,
    [[maybe_unused]] const MapperServiceMap& serviceMap,
    const std::optional<bool>& remoteDebugEnabled)
{
    if (!remoteDebugEnabled)
    {
        return;
    }

    redfish::nvidia_processor::patchRemoteDebug(
        asyncResp, processorId, *remoteDebugEnabled, objectPath);
}

inline void patchReconfigPermissionsIfPresent(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& processorId,
    const std::optional<nlohmann::json>& inbandReconfigPermissions,
    const std::optional<nlohmann::json>& doeReconfigPermissions)
{
    if (inbandReconfigPermissions)
    {
        nlohmann::json inbandJson = *inbandReconfigPermissions; // mutable copy
        nvidia_processor_utils::patchInbandReconfigPermissions(
            asyncResp, processorId, inbandJson);
    }
    if (doeReconfigPermissions)
    {
        nlohmann::json doeJson = *doeReconfigPermissions; // mutable copy
        nvidia_processor_utils::patchDOEReconfigPermissions(
            asyncResp, processorId, doeJson);
    }
}

inline void patchPCIeLinkEnableMaskIfPresent(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& processorId, const std::string& objectPath,
    const std::optional<nlohmann::json>& pcieLinkEnableMask)
{
    if (!pcieLinkEnableMask)
    {
        return;
    }
    if (!pcieLinkEnableMask->is_object())
    {
        messages::propertyValueTypeError(asyncResp->res, *pcieLinkEnableMask,
                                         "PCIeLinkEnableMask");
        return;
    }
    // SupportedMask and Writable are device-reported and read-only. Naming
    // either is a distinct client error from naming something unknown, so
    // reject them before readJson folds them into PropertyUnknown.
    for (const char* readOnlyMember : {"SupportedMask", "Writable"})
    {
        if (pcieLinkEnableMask->contains(readOnlyMember))
        {
            messages::propertyNotWritable(
                asyncResp->res,
                std::string("PCIeLinkEnableMask/") + readOnlyMember);
            return;
        }
    }

    nlohmann::json maskRoot = *pcieLinkEnableMask; // mutable copy

    std::optional<std::string> mask;
    if (!redfish::json_util::readJson(maskRoot, asyncResp->res, "Mask", mask))
    {
        return;
    }
    if (!mask)
    {
        return;
    }

    const std::string& maskStr = *mask;
    // Schema pattern ^0[xX][0-9a-fA-F]{1,16}$: hexStringToUint64 treats the
    // 0x prefix as optional and accepts any number of leading zeros, so the
    // prefix and length are enforced here.
    std::optional<uint64_t> parsed;
    if (maskStr.size() >= 3 && maskStr.size() <= 18 && maskStr[0] == '0' &&
        (maskStr[1] == 'x' || maskStr[1] == 'X'))
    {
        parsed = hexStringToUint64(maskStr);
    }
    if (!parsed)
    {
        messages::propertyValueFormatError(asyncResp->res, maskStr,
                                           "PCIeLinkEnableMask/Mask");
        return;
    }
    nvidia_processor_utils::patchPCIeLinkEnableMask(
        asyncResp, processorId, objectPath, *parsed, maskStr);
}

inline void handleNvidiaOemIfRequested(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath, const MapperServiceMap& serviceMap,
    const std::string& processorId,
    const std::optional<nlohmann::json>& oemObject)
{
    if constexpr (!BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        return;
    }

    if (!oemObject)
    {
        return;
    }

    nlohmann::json oemRoot = *oemObject; // mutable copy

    std::optional<nlohmann::json> oemNvidiaObject;
    if (!redfish::json_util::readJson(oemRoot, asyncResp->res, "Nvidia",
                                      oemNvidiaObject))
    {
        return;
    }

    std::optional<bool> migMode;
    std::optional<bool> remoteDebugEnabled;
    std::optional<nlohmann::json> inbandReconfigPermissions;
    std::optional<nlohmann::json> doeReconfigPermissions;
    std::optional<nlohmann::json> pcieLinkEnableMask;

    if (oemNvidiaObject)
    {
        nlohmann::json nvidiaRoot = *oemNvidiaObject; // mutable copy

        if (!redfish::json_util::readJson(
                nvidiaRoot, asyncResp->res, "MIGModeEnabled", migMode,
                "RemoteDebugEnabled", remoteDebugEnabled,
                "InbandReconfigPermissions", inbandReconfigPermissions,
                "DOEReconfigPermissions", doeReconfigPermissions,
                "PCIeLinkEnableMask", pcieLinkEnableMask))
        {
            return;
        }
    }
    else
    {
        return;
    }

    patchMigModeIfPresent(asyncResp, processorId, objectPath, serviceMap,
                          migMode);
    patchRemoteDebugIfPresent(asyncResp, processorId, objectPath, serviceMap,
                              remoteDebugEnabled);
    patchReconfigPermissionsIfPresent(asyncResp, processorId,
                                      inbandReconfigPermissions,
                                      doeReconfigPermissions);
    patchPCIeLinkEnableMaskIfPresent(asyncResp, processorId, objectPath,
                                     pcieLinkEnableMask);
}

inline void handleNvidiaProcessorInterface(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& processorId, const std::string& serviceName,
    const std::string& objectPath, const std::string& interface,
    [[maybe_unused]] const std::string& deviceType)
{
    // Non-OEM NVIDIA interfaces
    if (interface == "xyz.openbmc_project.Inventory.Decorator.LocationContext")
    {
        getProcessorLocationContext(asyncResp, serviceName, objectPath);
    }
    else if (interface == "xyz.openbmc_project.Inventory.Decorator.Location")
    {
        getCpuLocationType(asyncResp, serviceName, objectPath);
    }
    else if (interface == "xyz.openbmc_project.Inventory.Item.PersistentMemory")
    {
        getProcessorMemoryData(asyncResp, processorId, serviceName, objectPath);
    }
    else if (interface == "xyz.openbmc_project.Memory.MemoryECC")
    {
        getProcessorEccModeData(asyncResp, processorId, serviceName,
                                objectPath);
    }
    else if (interface == "xyz.openbmc_project.Inventory.Decorator.FpgaType")
    {
        getFpgaTypeData(asyncResp, serviceName, objectPath);
    }
    else if (interface == "xyz.openbmc_project.Control.Processor.Reset")
    {
        getProcessorResetTypeData(asyncResp, processorId, serviceName,
                                  objectPath);
    }
    else if (interface == "xyz.openbmc_project.Inventory.Decorator.Replaceable")
    {
        getProcessorReplaceable(asyncResp, serviceName, objectPath);
    }

    // OEM-guarded NVIDIA interfaces
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        if (interface == "xyz.openbmc_project.Inventory.Item.Cpu")
        {
            getRemoteDebugState(asyncResp, serviceName, objectPath);
        }
        else if (interface == "com.nvidia.MigMode")
        {
            getMigModeData(asyncResp, processorId, serviceName, objectPath);
        }
        else if (interface == "com.nvidia.CCMode")
        {
            redfish::nvidia_processor_utils::getCCModeData(
                asyncResp, processorId, serviceName, objectPath);
        }
        else if (interface == "com.nvidia.PowerSmoothing.PowerSmoothing")
        {
            redfish::nvidia_processor_utils::getPowerSmoothingInfo(
                asyncResp, processorId, serviceName, objectPath);
        }
        else if (interface == "com.nvidia.NVLink.NvLinkTotalCount")
        {
            redfish::nvidia_processor_utils::getNvLinkTotalCount(
                asyncResp, processorId, serviceName, objectPath);
        }
        else if (interface == "com.nvidia.PowerProfile.ProfileInfo")
        {
            redfish::nvidia_processor_utils::getWorkLoadPowerInfo(
                asyncResp, processorId);
        }
        else if (interface == "com.nvidia.SysGUID.SysGUID")
        {
            redfish::nvidia_processor_utils::getSysGUID(asyncResp, serviceName,
                                                        objectPath);
        }
        else if (interface == "com.nvidia.EgmMode")
        {
            redfish::nvidia_processor_utils::getEgmModeData(
                asyncResp, processorId, serviceName, objectPath);
        }
        else if (interface == "com.nvidia.AdaptiveTGPMode")
        {
            redfish::nvidia_processor_utils::getAdaptiveTGPModeData(
                asyncResp, processorId, serviceName, objectPath);
        }
        else if (interface == "com.nvidia.NVLink.MNNVLinkTopology")
        {
            redfish::nvidia_processor_utils::getMNNVLinkTopologyInfo(
                asyncResp, processorId, serviceName, objectPath, interface);
        }
        else if (interface ==
                 "com.nvidia.ResetCounters.ResetCounterMetricsSupported")
        {
            redfish::nvidia_processor_utils::getResetMetricsInfo(
                asyncResp, processorId, serviceName, objectPath);
        }
    }
}

inline void populateNvidiaProcessorPostData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& processorId, const std::string& objectPath,
    const dbus::utility::MapperServiceMap& serviceMap,
    const std::string& deviceType)
{
    getComponentFirmwareVersion(asyncResp, objectPath);
    redfish::nvidia_processor_utils::getOperatingSpeedRange(
        asyncResp, objectPath);

    asyncResp->res.jsonValue["Metrics"] = {
        {"@odata.id",
         "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
             "/Processors/" + processorId + "/ProcessorMetrics"}};

    asyncResp->res.jsonValue["EnvironmentMetrics"] = {
        {"@odata.id",
         "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
             "/Processors/" + processorId + "/EnvironmentMetrics"}};

    asyncResp->res.jsonValue["@Redfish.Settings"]["@odata.type"] =
        "#Settings.v1_3_3.Settings";
    asyncResp->res.jsonValue["@Redfish.Settings"]["SettingsObject"] = {
        {"@odata.id",
         "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
             "/Processors/" + processorId + "/Settings"}};

    asyncResp->res.jsonValue["Ports"] = {
        {"@odata.id",
         "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
             "/Processors/" + processorId + "/Ports"}};

    getProcessorMemoryLinks(asyncResp, objectPath);

    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        getProcessorChassisLink(asyncResp, objectPath, serviceName, deviceType);
    }

    getProcessorSystemPCIeInterface(asyncResp, objectPath);
    getProcessorFPGAPCIeInterface(asyncResp, objectPath);

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        nvidia_processor_utils::getReconfigPermissionsData(
            asyncResp, processorId, objectPath);
        nvidia_processor_utils::populateErrorInjectionData(asyncResp,
                                                           processorId);
        if (deviceType == "xyz.openbmc_project.Inventory.Item.Cpu")
        {
            nvidia_processor_utils::getPCIeLinkEnableMask(asyncResp,
                                                          objectPath);
        }
    }
    if constexpr (BMCWEB_NVIDIA_PCORE_DUMP)
    {
        // Advertises Actions/Oem only on a CPU that resolves to a PCore dump
        // trigger, so the action never appears on an Accelerator or on a
        // platform whose firmware does not expose one.
        nvidia_pcore_dump::advertisePCoreDump(asyncResp, processorId,
                                              objectPath, deviceType);
    }
    if constexpr (!BMCWEB_DISABLE_CONDITIONS_ARRAY)
    {
        redfish::conditions_utils::populateServiceConditions(asyncResp,
                                                             processorId);
    }
}

inline void populatePowerState(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string* accType, const std::string* operationalState)
{
    if (accType != nullptr && !accType->empty())
    {
        asyncResp->res.jsonValue["ProcessorType"] =
            redfish::nvidia_processor::getProcessorType(*accType);
    }

    if (operationalState != nullptr && !operationalState->empty())
    {
        asyncResp->res.jsonValue["Status"]["State"] =
            redfish::chassis_utils::getPowerStateType(*operationalState);
    }
}
} // namespace nvidia_processor

inline void requestRoutesProcessorPortHistogramBuckets(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Ports/<str>/Oem/Nvidia/Histograms/<str>/Buckets")
        .privileges(redfish::privileges::getProcessor)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            const std::string& systemName,
                            const std::string& processorId,
                            const std::string& portId,
                            const std::string& histogramId) {
            if (!redfish::setUpRedfishRoute(app, req, aResp))
            {
                return;
            }
            if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
            {
                messages::resourceNotFound(aResp->res, "ComputerSystem",
                                           systemName);
                return;
            }

            BMCWEB_LOG_DEBUG("Get available system processor resource");
            dbus::utility::async_method_call(
                [processorId, portId, histogramId, aResp](
                    const boost::system::error_code ec,
                    const boost::container::flat_map<
                        std::string,
                        boost::container::flat_map<
                            std::string, std::vector<std::string>>>& subtree) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error while getting processor: {}",
                            ec.message());
                        messages::internalError(aResp->res);
                        return;
                    }
                    for (const auto& [objPath, object] : subtree)
                    {
                        if (!objPath.ends_with(processorId))
                        {
                            continue;
                        }

                        dbus::utility::async_method_call(
                            [aResp, objPath, processorId, portId, histogramId](
                                const boost::system::error_code ec2,
                                std::variant<std::vector<std::string>>& resp) {
                                if (ec2)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "DBUS response error while getting port on processor: {}",
                                        ec2.message());
                                    messages::internalError(aResp->res);
                                    return;
                                }

                                std::vector<std::string>* data =
                                    std::get_if<std::vector<std::string>>(
                                        &resp);
                                if (data == nullptr)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "Null data response while getting port on processor");
                                    messages::internalError(aResp->res);
                                    return;
                                }

                                for (const std::string& sensorpath : *data)
                                {
                                    // Check Interface in Object or not
                                    BMCWEB_LOG_DEBUG(
                                        "processor state sensor object path {}",
                                        sensorpath);
                                    sdbusplus::object_path path(sensorpath);
                                    if (path.filename() != portId)
                                    {
                                        continue;
                                    }

                                    dbus::utility::async_method_call(
                                        [aResp, processorId, portId,
                                         histogramId](
                                            const boost::system::error_code ec3,
                                            std::variant<std::vector<
                                                std::string>>& resp2) {
                                            if (ec3)
                                            {
                                                BMCWEB_LOG_ERROR(
                                                    "DBUS response error while getting switch on fabric: {}",
                                                    ec3.message());
                                                messages::internalError(
                                                    aResp->res);
                                                return;
                                            }
                                            std::vector<std::string>*
                                                bucketData = std::get_if<
                                                    std::vector<std::string>>(
                                                    &resp2);
                                            if (bucketData == nullptr)
                                            {
                                                BMCWEB_LOG_ERROR(
                                                    "Null data response while getting switch on fabric");
                                                messages::internalError(
                                                    aResp->res);
                                                return;
                                            }
                                            // Iterate over all retrieved
                                            // ObjectPaths.
                                            for (const std::string& histoPath :
                                                 *bucketData)
                                            {
                                                sdbusplus::object_path
                                                    histoObjPath(histoPath);
                                                if (histoObjPath.filename() !=
                                                    histogramId)
                                                {
                                                    continue;
                                                }

                                                std::string histoURI =
                                                    "/redfish/v1/Systems/";
                                                histoURI += std::string(
                                                    BMCWEB_REDFISH_SYSTEM_URI_NAME);
                                                histoURI += "/Processors/";
                                                histoURI += processorId;
                                                histoURI += "/Ports/";
                                                histoURI += portId;
                                                histoURI +=
                                                    "/Oem/Nvidia/Histograms/";
                                                histoURI += histogramId;
                                                histoURI += "/Buckets";
                                                aResp->res
                                                    .jsonValue["@odata.type"] =
                                                    "#NvidiaHistogramBuckets.v1_0_0.NvidiaHistogramBuckets";
                                                aResp->res
                                                    .jsonValue["@odata.id"] =
                                                    histoURI;
                                                std::string name = processorId;
                                                name += "_";
                                                name += portId;
                                                name += "_Histogram_";
                                                name += histogramId;
                                                name += "_Buckets";
                                                aResp->res.jsonValue["Name"] =
                                                    name;
                                                aResp->res.jsonValue["Id"] =
                                                    "Buckets";
                                                aResp->res
                                                    .jsonValue["Buckets"] =
                                                    nlohmann::json::array();
                                                redfish::nvidia_histogram_utils::
                                                    updateHistogramBucketData(
                                                        aResp, histoPath);
                                            }
                                        },
                                        "xyz.openbmc_project.ObjectMapper",
                                        sensorpath + "/histograms",
                                        "org.freedesktop.DBus.Properties",
                                        "Get",
                                        "xyz.openbmc_project.Association",
                                        "endpoints");

                                    return;
                                }
                                // Couldn't find an object with that name.
                                // Return an error
                                messages::resourceNotFound(
                                    aResp->res, "#Port.v1_0_0.Port", portId);
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            objPath + "/all_states",
                            "org.freedesktop.DBus.Properties", "Get",
                            "xyz.openbmc_project.Association", "endpoints");
                        return;
                    }
                    // Object not found
                    messages::resourceNotFound(aResp->res,
                                               "#Processor.v1_20_0.Processor",
                                               processorId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/inventory", 0,
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Inventory.Item.Accelerator"});
        });
}

inline void requestRoutesProcessorPortHistogram(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Ports/<str>/Oem/Nvidia/Histograms/<str>")
        .privileges(redfish::privileges::getProcessor)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            const std::string& systemName,
                            const std::string& processorId,
                            const std::string& portId,
                            const std::string& histogramId) {
            if (!redfish::setUpRedfishRoute(app, req, aResp))
            {
                return;
            }
            if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
            {
                messages::resourceNotFound(aResp->res, "ComputerSystem",
                                           systemName);
                return;
            }

            BMCWEB_LOG_DEBUG("Get available system processor resource");
            dbus::utility::async_method_call(
                [processorId, portId, histogramId, aResp](
                    const boost::system::error_code ec,
                    const boost::container::flat_map<
                        std::string,
                        boost::container::flat_map<
                            std::string, std::vector<std::string>>>& subtree) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error while getting processor: {}",
                            ec.message());
                        messages::internalError(aResp->res);
                        return;
                    }
                    for (const auto& [objPath, object] : subtree)
                    {
                        if (!objPath.ends_with(processorId))
                        {
                            continue;
                        }

                        dbus::utility::async_method_call(
                            [aResp, processorId, portId, histogramId](
                                const boost::system::error_code ec2,
                                std::variant<std::vector<std::string>>& resp) {
                                if (ec2)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "DBUS response error while getting port on processor: {}",
                                        ec2.message());
                                    messages::internalError(aResp->res);
                                    return;
                                }

                                std::vector<std::string>* data =
                                    std::get_if<std::vector<std::string>>(
                                        &resp);
                                if (data == nullptr)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "Null data response while getting port on processor");
                                    messages::internalError(aResp->res);
                                    return;
                                }

                                for (const std::string& sensorpath : *data)
                                {
                                    // Check Interface in Object or not
                                    BMCWEB_LOG_DEBUG(
                                        "processor state sensor object path {}",
                                        sensorpath);
                                    sdbusplus::object_path path(sensorpath);
                                    if (path.filename() != portId)
                                    {
                                        continue;
                                    }

                                    std::string histoURI =
                                        "/redfish/v1/Systems/";
                                    histoURI += std::string(
                                        BMCWEB_REDFISH_SYSTEM_URI_NAME);
                                    histoURI += "/Processors/";
                                    histoURI += processorId;
                                    histoURI += "/Ports/";
                                    histoURI += portId;
                                    histoURI += "/Oem/Nvidia/Histograms/";
                                    histoURI += histogramId;
                                    aResp->res.jsonValue["@odata.type"] =
                                        "#NvidiaHistogram.v1_1_0.NvidiaHistogram";
                                    aResp->res.jsonValue["@odata.id"] =
                                        histoURI;
                                    aResp->res.jsonValue["Id"] = histogramId;
                                    std::string name = processorId;
                                    name += "_";
                                    name += portId;
                                    name += "_Histogram_";
                                    name += histogramId;
                                    aResp->res.jsonValue["Name"] = name;

                                    std::string bucketURI =
                                        histoURI + "/Buckets";
                                    aResp->res.jsonValue["HistogramBuckets"]
                                                        ["@odata.id"] =
                                        bucketURI;
                                    redfish::nvidia_histogram_utils::
                                        getHistogramDataByAssociation(
                                            aResp, histogramId, sensorpath);

                                    return;
                                }
                                // Couldn't find an object with that name.
                                // Return an error
                                messages::resourceNotFound(
                                    aResp->res, "#Port.v1_0_0.Port", portId);
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            objPath + "/all_states",
                            "org.freedesktop.DBus.Properties", "Get",
                            "xyz.openbmc_project.Association", "endpoints");
                        return;
                    }
                    // Object not found
                    messages::resourceNotFound(aResp->res,
                                               "#Processor.v1_20_0.Processor",
                                               processorId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/inventory", 0,
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Inventory.Item.Accelerator"});
        });
}

inline void requestRoutesProcessorPortHistogramCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Ports/<str>/Oem/Nvidia/Histograms")
        .privileges(redfish::privileges::getProcessor)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            const std::string& systemName,
                            const std::string& processorId,
                            const std::string& portId) {
            if (!redfish::setUpRedfishRoute(app, req, aResp))
            {
                return;
            }
            if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
            {
                messages::resourceNotFound(aResp->res, "ComputerSystem",
                                           systemName);
                return;
            }

            BMCWEB_LOG_DEBUG("Get available system processor resource");
            dbus::utility::async_method_call(
                [processorId, portId, aResp](
                    const boost::system::error_code ec,
                    const boost::container::flat_map<
                        std::string,
                        boost::container::flat_map<
                            std::string, std::vector<std::string>>>& subtree) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error while getting processor: {}",
                            ec.message());
                        messages::internalError(aResp->res);
                        return;
                    }
                    for (const auto& [objPath, object] : subtree)
                    {
                        if (!objPath.ends_with(processorId))
                        {
                            continue;
                        }

                        dbus::utility::async_method_call(
                            [aResp, objPath, processorId, portId](
                                const boost::system::error_code ec2,
                                std::variant<std::vector<std::string>>& resp) {
                                if (ec2)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "DBUS response error while getting port on processor: {}",
                                        ec2.message());
                                    messages::internalError(aResp->res);
                                    return;
                                }

                                std::vector<std::string>* data =
                                    std::get_if<std::vector<std::string>>(
                                        &resp);
                                if (data == nullptr)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "Null data response while getting port on processor");
                                    messages::internalError(aResp->res);
                                    return;
                                }

                                for (const std::string& sensorpath : *data)
                                {
                                    // Check Interface in Object or not
                                    BMCWEB_LOG_DEBUG(
                                        "processor state sensor object path {}",
                                        sensorpath);
                                    sdbusplus::object_path path(sensorpath);
                                    if (path.filename() != portId)
                                    {
                                        continue;
                                    }

                                    std::string histoURI =
                                        "/redfish/v1/Systems/";
                                    histoURI += std::string(
                                        BMCWEB_REDFISH_SYSTEM_URI_NAME);
                                    histoURI += "/Processors/";
                                    histoURI += processorId;
                                    histoURI += "/Ports/";
                                    histoURI += portId;
                                    histoURI += "/Oem/Nvidia/Histograms";
                                    aResp->res.jsonValue["@odata.type"] =
                                        "#NvidiaHistogramCollection.NvidiaHistogramCollection";
                                    aResp->res.jsonValue["@odata.id"] =
                                        histoURI;
                                    std::string name = processorId;
                                    name += "_";
                                    name += portId;
                                    name += "_Histogram_Collection";
                                    aResp->res.jsonValue["Name"] = name;

                                    std::string collectionUri =
                                        "/redfish/v1/Systems/";
                                    collectionUri += std::string(
                                        BMCWEB_REDFISH_SYSTEM_URI_NAME);
                                    collectionUri += "/Processors/";
                                    collectionUri += processorId;
                                    collectionUri += "/Ports/";
                                    collectionUri += portId;
                                    collectionUri += "/Oem/Nvidia/Histograms";
                                    collection_util::
                                        getCollectionMembersByAssociation(
                                            aResp, collectionUri,
                                            sensorpath + "/histograms", {});
                                    return;
                                }
                                // Couldn't find an object with that name.
                                // Return an error
                                messages::resourceNotFound(
                                    aResp->res, "#Port.v1_0_0.Port", portId);
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            objPath + "/all_states",
                            "org.freedesktop.DBus.Properties", "Get",
                            "xyz.openbmc_project.Association", "endpoints");
                        return;
                    }
                    // Object not found
                    messages::resourceNotFound(aResp->res,
                                               "#Processor.v1_20_0.Processor",
                                               processorId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/inventory", 0,
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Inventory.Item.Accelerator"});
        });
}

} // namespace redfish
