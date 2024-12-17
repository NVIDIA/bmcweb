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
#include <utils/json_utils.hpp>
#include <utils/nvidia_processor_utils.hpp>
#include <utils/port_utils.hpp>
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
inline void getFpgaTypeData(std::shared_ptr<bmcweb::AsyncResp> aResp,
                            const std::string& service,
                            const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Processor fpgatype");
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, objPath,
        "xyz.openbmc_project.Inventory.Decorator.FpgaType", "FpgaType",
        [objPath, aResp{std::move(aResp)}](const boost::system::error_code ec,
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
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath](
            const boost::system::error_code errorCode,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) mutable {
            if (errorCode)
            {
                BMCWEB_LOG_ERROR("error_code = {}", errorCode);
                BMCWEB_LOG_ERROR("error msg = {}", errorCode.message());
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
            sdbusplus::asio::getAllProperties(
                *crow::connections::systemBus, objInfo[0].first, objPath, "",
                [objPath, asyncResp](
                    const boost::system::error_code ec,
                    const dbus::utility::DBusPropertiesMap& properties) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("error_code = ", ec);
                        BMCWEB_LOG_ERROR("error msg = ", ec.message());
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    const double* currentSpeed = nullptr;
                    const size_t* activeWidth = nullptr;

                    const bool success = sdbusplus::unpackPropertiesNoThrow(
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
                        if (*activeWidth == INT_MAX)
                        {
                            asyncResp->res.jsonValue["SystemInterface"]["PCIe"]
                                                    ["LanesInUse"] = 0;
                        }
                        else
                        {
                            asyncResp->res.jsonValue["SystemInterface"]["PCIe"]
                                                    ["LanesInUse"] =
                                *activeWidth;
                        }
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
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec2,
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
    crow::connections::systemBus->async_method_call(
        [aResp, cpuId](const boost::system::error_code ec,
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

inline void postResetType(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& processorId, const std::string& cpuObjectPath,
    const std::string& resetType, const MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::find(interfaceList.begin(), interfaceList.end(),
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
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, conName, cpuObjectPath,
        "xyz.openbmc_project.Control.Processor.Reset", "ResetType",
        [resp, resetType, processorId, conName, cpuObjectPath](
            const boost::system::error_code ec, const std::string& property) {
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

            static const auto resetAsyncIntf =
                "xyz.openbmc_project.Control.Processor.ResetAsync";

            dbus::utility::getDbusObject(
                cpuObjectPath, std::array<std::string_view, 1>{resetAsyncIntf},
                [resp, cpuObjectPath, conName,
                 processorId](const boost::system::error_code& ec,
                              const dbus::utility::MapperGetObject& object) {
                    if (!ec)
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
                    crow::connections::systemBus->async_method_call(
                        [resp, processorId](boost::system::error_code ec1,
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
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath](
            const boost::system::error_code errorCode,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) mutable {
            if (errorCode)
            {
                BMCWEB_LOG_ERROR("error_code = ", errorCode);
                BMCWEB_LOG_ERROR("error msg = ", errorCode.message());
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
            sdbusplus::asio::getAllProperties(
                *crow::connections::systemBus, objInfo[0].first, objPath, "",
                [objPath, asyncResp](
                    const boost::system::error_code ec,
                    const dbus::utility::DBusPropertiesMap& properties) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("error_code = ", ec);
                        BMCWEB_LOG_ERROR("error msg = ", ec.message());
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
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec2,
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
    crow::connections::systemBus->async_method_call(
        [aResp, objPath,
         deviceType](const boost::system::error_code ec,
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
                        "#NvidiaProcessorMetrics.v1_4_0.NvidiaGPUProcessorMetrics";
                }
                else
                {
                    json["Oem"]["Nvidia"]["@odata.type"] =
                        "#NvidiaProcessorMetrics.v1_2_0.NvidiaProcessorMetrics";
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
                        redfish::dbus_utils::toPerformanceStateType(*state);
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.State.ProcessorPerformance");
}

inline void getPowerBreakThrottle(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string objPath, const std::string& deviceType)
{
    BMCWEB_LOG_DEBUG("Get processor module link");
    crow::connections::systemBus->async_method_call(
        [aResp, objPath, deviceType,
         service](const boost::system::error_code ec,
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

            BMCWEB_LOG_DEBUG("Get processor module state sensors");
            crow::connections::systemBus->async_method_call(
                [aResp, service, deviceType,
                 chassisPath](const boost::system::error_code& e,
                              std::variant<std::vector<std::string>>& resp) {
                    if (e)
                    {
                        // no state sensors attached.
                        return;
                    }
                    std::vector<std::string>* data =
                        std::get_if<std::vector<std::string>>(&resp);
                    if (data == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    for (const std::string& sensorpath : *data)
                    {
                        BMCWEB_LOG_DEBUG(
                            "proc module state sensor object path {}",
                            sensorpath);

                        const std::array<const char*, 1> sensorinterfaces = {
                            "xyz.openbmc_project.State.ProcessorPerformance"};
                        // process sensor reading
                        crow::connections::systemBus->async_method_call(
                            [aResp, sensorpath, deviceType](
                                const boost::system::error_code ec,
                                const std::vector<std::pair<
                                    std::string, std::vector<std::string>>>&
                                    object) {
                                if (ec)
                                {
                                    // the path does not implement any state
                                    // interfaces.
                                    return;
                                }

                                for (const auto& [service, interfaces] : object)
                                {
                                    if (std::find(
                                            interfaces.begin(),
                                            interfaces.end(),
                                            "xyz.openbmc_project.State.ProcessorPerformance") !=
                                        interfaces.end())
                                    {
                                        getPowerBreakThrottleData(
                                            aResp, service, sensorpath,
                                            deviceType);
                                    }
                                }
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            "/xyz/openbmc_project/object_mapper",
                            "xyz.openbmc_project.ObjectMapper", "GetObject",
                            sensorpath, sensorinterfaces);
                    }
                },
                "xyz.openbmc_project.ObjectMapper", chassisPath + "/all_states",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Association", "endpoints");
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
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
    crow::connections::systemBus->async_method_call(
        [aResp, pcieDeviceLink,
         objPath](const boost::system::error_code ec,
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
                if (property && !property->empty())
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
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec2,
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
                sdbusplus::message::object_path objectPath(memoryPath);
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
    crow::connections::systemBus->async_method_call(
        [aResp, chassisName](const boost::system::error_code ec,
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
            sdbusplus::message::object_path objectPath(parentChassisPath);
            std::string parentChassisName = objectPath.filename();
            if (parentChassisName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            crow::connections::systemBus->async_method_call(
                [aResp, chassisName,
                 parentChassisName](const boost::system::error_code ec1,
                                    const MapperGetSubTreeResponse& subtree) {
                    if (ec1)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    for (const auto& [objectPath1, serviceMap] : subtree)
                    {
                        // Process same device
                        if (!objectPath1.ends_with(chassisName))
                        {
                            continue;
                        }
                        std::string pcieDeviceLink = "/redfish/v1/Chassis/";
                        pcieDeviceLink += parentChassisName;
                        pcieDeviceLink += "/PCIeDevices/";
                        pcieDeviceLink += chassisName;
                        aResp->res.jsonValue["Links"]["PCIeDevice"] = {
                            {"@odata.id", pcieDeviceLink}};
                        if (serviceMap.size() < 1)
                        {
                            BMCWEB_LOG_ERROR("Got 0 service "
                                             "names");
                            messages::internalError(aResp->res);
                            return;
                        }
                        const std::string& serviceName = serviceMap[0].first;
                        // Get PCIeFunctions Link
                        redfish::nvidia_processor::
                            getProcessorPCIeFunctionsLinks(aResp, serviceName,
                                                           objectPath1,
                                                           pcieDeviceLink);
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
    const std::string& service)
{
    BMCWEB_LOG_DEBUG("Get parent chassis link");
    crow::connections::systemBus->async_method_call(
        [aResp, objPath,
         service](const boost::system::error_code ec,
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
            sdbusplus::message::object_path objectPath(chassisPath);
            std::string chassisName = objectPath.filename();
            if (chassisName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            aResp->res.jsonValue["Links"]["Chassis"] = {
                {"@odata.id", "/redfish/v1/Chassis/" + chassisName}};

            // Get PCIeDevice on this chassis
            crow::connections::systemBus->async_method_call(
                [aResp, chassisName, chassisPath,
                 service](const boost::system::error_code ec,
                          std::variant<std::vector<std::string>>& resp) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "Chassis has no connected PCIe devices");
                        return; // no pciedevices = no failures
                    }
                    std::vector<std::string>* data =
                        std::get_if<std::vector<std::string>>(&resp);
                    if (data == nullptr || data->size() > 1)
                    {
                        // Chassis must have single pciedevice
                        BMCWEB_LOG_ERROR("chassis must have single pciedevice");
                        return;
                    }
                    const std::string& pcieDevicePath = data->front();
                    sdbusplus::message::object_path objectPath(pcieDevicePath);
                    std::string pcieDeviceName = objectPath.filename();
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
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
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
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& service,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Processor firmware version");
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}](const boost::system::error_code ec,
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
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& service,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Processor LocationContext Data");
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, objPath,
        "xyz.openbmc_project.Inventory.Decorator.LocationContext",
        "LocationContext",
        [objPath, aResp{std::move(aResp)}](const boost::system::error_code ec,
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
inline void getCpuLocationType(std::shared_ptr<bmcweb::AsyncResp> aResp,
                               const std::string& service,
                               const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Cpu LocationType Data");
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, objPath,
        "xyz.openbmc_project.Inventory.Decorator.Location", "LocationType",
        [objPath, aResp{std::move(aResp)}](const boost::system::error_code ec,
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
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, connectionName, path,
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
    crow::connections::systemBus->async_method_call(
        [aResp, cpuId](boost::system::error_code ec,
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
    crow::connections::systemBus->async_method_call(
        [aResp, cpuId](const boost::system::error_code ec,
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
    crow::connections::systemBus->async_method_call(
        [aResp, cpuId](const boost::system::error_code ec,
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
    crow::connections::systemBus->async_method_call(
        [aResp, objPath,
         deviceType](const boost::system::error_code ec,
                     const OperatingConfigProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }

            nlohmann::json& json = aResp->res.jsonValue;
            if (deviceType == "xyz.openbmc_project.Inventory.Item.Accelerator")
            {
                json["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaProcessorMetrics.v1_4_0.NvidiaGPUProcessorMetrics";
            }
            else
            {
                json["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaProcessorMetrics.v1_2_0.NvidiaProcessorMetrics";
            }

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
                        BMCWEB_LOG_DEBUG(
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

                    for (auto val : *throttleReasons)
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
                        BMCWEB_LOG_DEBUG(
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
                        BMCWEB_LOG_DEBUG("Get  duraiton property failed");
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
                        BMCWEB_LOG_DEBUG("Get  acc duraiton property failed");
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
                    const uint32_t* val =
                        std::get_if<uint32_t>(&property.second);
                    if (val == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Get  pcie bytes property failed");
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
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, objPath, nvlinkMetricsIface,
        [aResp](const boost::system::error_code ec,
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
    crow::connections::systemBus->async_method_call(
        [aResp, objPath,
         deviceType](const boost::system::error_code ec,
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
                        "#NvidiaProcessorMetrics.v1_4_0.NvidiaGPUProcessorMetrics";
                }
                else
                {
                    json["Oem"]["Nvidia"]["@odata.type"] =
                        "#NvidiaProcessorMetrics.v1_2_0.NvidiaProcessorMetrics";
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
    crow::connections::systemBus->async_method_call(
        [aResp, objPath, deviceType](const boost::system::error_code ec,
                                     const std::variant<bool>& property) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json& json = aResp->res.jsonValue;

            const bool* memorySpareChannelPresence =
                std::get_if<bool>(&property);
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
                    "#NvidiaProcessorMetrics.v1_4_0.NvidiaGPUProcessorMetrics";
            }
            else
            {
                json["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaProcessorMetrics.v1_2_0.NvidiaProcessorMetrics";
            }
            json["Oem"]["Nvidia"]["MemorySpareChannelPresence"] =
                *memorySpareChannelPresence;
        },
        service, objPath, "org.freedesktop.DBus.Properties", "Get",
        "com.nvidia.MemorySpareChannel", "MemorySpareChannelPresence");
}

// TODO: move to oem
inline void getMemoryPageRetirementCountData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath, const std::string& deviceType)
{
    crow::connections::systemBus->async_method_call(
        [aResp, objPath, deviceType](const boost::system::error_code ec,
                                     const std::variant<uint32_t>& property) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json& json = aResp->res.jsonValue;

            const uint32_t* memoryPageRetirementCount =
                std::get_if<uint32_t>(&property);
            if (memoryPageRetirementCount == nullptr)
            {
                BMCWEB_LOG_ERROR(
                    "Null value returned for MemoryPageRetirementCount");
                messages::internalError(aResp->res);
                return;
            }
            if (deviceType == "xyz.openbmc_project.Inventory.Item.Accelerator")
            {
                json["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaProcessorMetrics.v1_4_0.NvidiaGPUProcessorMetrics";
            }
            else
            {
                json["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaProcessorMetrics.v1_2_0.NvidiaProcessorMetrics";
            }
            json["Oem"]["Nvidia"]["MemoryPageRetirementCount"] =
                *memoryPageRetirementCount;
        },
        service, objPath, "org.freedesktop.DBus.Properties", "Get",
        "com.nvidia.MemoryPageRetirementCount", "MemoryPageRetirementCount");
}
// TODO: move to oem
inline void getMigModeData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const std::string& cpuId, const std::string& service,
                           const std::string& objPath)
{
    crow::connections::systemBus->async_method_call(
        [aResp, cpuId](const boost::system::error_code ec,
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
    crow::connections::systemBus->async_method_call(
        [aResp, objPath](const boost::system::error_code ec,
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
    crow::connections::systemBus->async_method_call(
        [aResp, service,
         objPath](const boost::system::error_code& e,
                  std::variant<std::vector<std::string>>& resp) {
            if (e)
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
                crow::connections::systemBus->async_method_call(
                    [aResp, effecterPath](
                        const boost::system::error_code ec,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& object) {
                        if (ec)
                        {
                            // The path does not implement any state interfaces.
                            return;
                        }

                        for (const auto& [service, interfaces] : object)
                        {
                            if (std::find(
                                    interfaces.begin(), interfaces.end(),
                                    "xyz.openbmc_project.Control.Processor.RemoteDebug") !=
                                interfaces.end())
                            {
                                getProcessorRemoteDebugState(aResp, service,
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
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, objPath, gpmMetricsIface,
        [aResp](const boost::system::error_code ec,
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
                "NVJpgInstanceUtilizationPercent", nvjpgInstanceUtil);

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
    const std::string& setPropVal, boost::system::error_code ec,
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
                         const MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::find(interfaceList.begin(), interfaceList.end(),
                      "com.nvidia.MigMode") != interfaceList.end())
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
            crow::connections::systemBus->async_method_call(
                [resp, processorId](boost::system::error_code ec,
                                    sdbusplus::message::message& msg) {
                    if (!ec)
                    {
                        BMCWEB_LOG_DEBUG("Set MIG Mode property succeeded");
                        return;
                    }

                    BMCWEB_LOG_DEBUG("CPU:{} set MIG Mode  property failed: {}",
                                     processorId, ec);
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
                "Set", "com.nvidia.MigMode", "MIGModeEnabled",
                std::variant<bool>(migMode));
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
    crow::connections::systemBus->async_method_call(
        [aResp, objPath](const boost::system::error_code ec,
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
                     std::to_string(remoteDebugEnabled), processorId);

    // Find remote debug effecters from all effecters attached to "all_controls"
    crow::connections::systemBus->async_method_call(
        [aResp,
         remoteDebugEnabled](const boost::system::error_code& e,
                             std::variant<std::vector<std::string>>& resp) {
            if (e)
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
                crow::connections::systemBus->async_method_call(
                    [aResp, effecterPath, remoteDebugEnabled](
                        const boost::system::error_code ec,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& object) {
                        if (ec)
                        {
                            // The path does not implement any state interfaces.
                            BMCWEB_LOG_DEBUG(
                                " No any state effecter interface. ");
                            messages::internalError(aResp->res);
                            return;
                        }

                        for (const auto& [service, interfaces] : object)
                        {
                            if (std::find(
                                    interfaces.begin(), interfaces.end(),
                                    "xyz.openbmc_project.Control.Processor.RemoteDebug") !=
                                interfaces.end())
                            {
                                setProcessorRemoteDebugState(
                                    aResp, service, effecterPath,
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
inline void patchSpeedConfig(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                             const std::string& processorId,
                             const std::tuple<bool, uint32_t>& reqSpeedConfig,
                             const std::string& cpuObjectPath,
                             const MapperServiceMap& serviceMap)
{
    BMCWEB_LOG_DEBUG("Setting SpeedConfig");
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::find(
                interfaceList.begin(), interfaceList.end(),
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

            crow::connections::systemBus->async_method_call(
                [resp, processorId,
                 reqSpeedConfig](boost::system::error_code ec,
                                 sdbusplus::message::message& msg) {
                    if (!ec)
                    {
                        BMCWEB_LOG_DEBUG("Set speed config property succeeded");
                        return;
                    }

                    BMCWEB_LOG_DEBUG(
                        "CPU:{} set speed config property failed: {}",
                        processorId, ec);
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
    const std::string& cpuObjectPath, const MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::find(
                interfaceList.begin(), interfaceList.end(),
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
    sdbusplus::asio::getProperty<std::tuple<bool, uint32_t>>(
        *crow::connections::systemBus, conName, cpuObjectPath,
        "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig", "SpeedConfig",
        [resp, processorId, conName, cpuObjectPath, serviceMap,
         speedLocked](const boost::system::error_code ec,
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
inline void patchSpeedLimit(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& processorId, const int speedLimit,
    const std::string& cpuObjectPath, const MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::find(
                interfaceList.begin(), interfaceList.end(),
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
    sdbusplus::asio::getProperty<std::tuple<bool, uint32_t>>(
        *crow::connections::systemBus, conName, cpuObjectPath,
        "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig", "SpeedConfig",
        [resp, processorId, conName, cpuObjectPath, serviceMap,
         speedLimit](const boost::system::error_code ec,
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

inline void getProcessorDataByService(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                      const std::string& service,
                                      const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get processor metrics data.");
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}](const boost::system::error_code ec,
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

inline void getProcessorMemoryECCData(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                      const std::string& service,
                                      const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get processor memory ecc data.");
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}](const boost::system::error_code ec,
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
    crow::connections::systemBus->async_method_call(
        [aResp, chassisId, sensorPath](
            const boost::system::error_code ec,
            const std::vector<
                std::pair<std::string, std::variant<std::string, double>>>&
                propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("Can't get sensor reading");
                return;
            }
            sdbusplus::message::object_path objectPath(sensorPath);
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
    crow::connections::systemBus->async_method_call(
        [aResp, service,
         objPath](const boost::system::error_code ec,
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
            sdbusplus::message::object_path objectPath(chassisPath);
            std::string chassisName = objectPath.filename();
            if (chassisName.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            const std::string& chassisId = chassisName;
            crow::connections::systemBus->async_method_call(
                [aResp, service, objPath,
                 chassisId](const boost::system::error_code& e,
                            std::variant<std::vector<std::string>>& resp1) {
                    if (e)
                    {
                        messages::internalError(aResp->res);
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
                        boost::algorithm::split(split, sensorPath,
                                                boost::is_any_of("/"));
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

inline void getNumericSensorMetric(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath, const std::string& deviceType)
{
    crow::connections::systemBus->async_method_call(
        [aResp, service, deviceType,
         objPath](const boost::system::error_code& e,
                  std::variant<std::vector<std::string>>& resp) {
            if (e)
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
                BMCWEB_LOG_DEBUG("Numeric Sensor Object Path {}", sensorPath);

                const std::array<const char*, 1> sensorInterfaces = {
                    "com.nvidia.MemoryPageRetirementCount"};
                // Process sensor reading
                crow::connections::systemBus->async_method_call(
                    [aResp, sensorPath, deviceType](
                        const boost::system::error_code ec,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& object) {
                        if (ec)
                        {
                            // The path does not implement any numeric sensor
                            // interfaces.
                            return;
                        }

                        for (const auto& [service, interfaces] : object)
                        {
                            if (std::find(
                                    interfaces.begin(), interfaces.end(),
                                    "com.nvidia.MemoryPageRetirementCount") !=
                                interfaces.end())
                            {
                                getMemoryPageRetirementCountData(
                                    aResp, service, sensorPath, deviceType);
                            }
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetObject", sensorPath,
                    sensorInterfaces);
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_sensors",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getStateSensorMetric(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath, const std::string& deviceType)
{
    crow::connections::systemBus->async_method_call(
        [aResp, service, objPath,
         deviceType](const boost::system::error_code& e,
                     std::variant<std::vector<std::string>>& resp) {
            if (e)
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

                const std::array<const char*, 3> sensorInterfaces = {
                    "xyz.openbmc_project.State.Decorator.PowerSystemInputs",
                    "xyz.openbmc_project.State.ProcessorPerformance",
                    "com.nvidia.MemorySpareChannel"};
                // Process sensor reading
                crow::connections::systemBus->async_method_call(
                    [aResp, sensorPath, deviceType](
                        const boost::system::error_code ec,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& object) {
                        if (ec)
                        {
                            // The path does not implement any state
                            // interfaces.
                            return;
                        }

                        for (const auto& [service, interfaces] : object)
                        {
                            if (std::find(
                                    interfaces.begin(), interfaces.end(),
                                    "xyz.openbmc_project.State.ProcessorPerformance") !=
                                interfaces.end())
                            {
                                getProcessorPerformanceData(
                                    aResp, service, sensorPath, deviceType);
                            }
                            if (std::find(
                                    interfaces.begin(), interfaces.end(),
                                    "xyz.openbmc_project.State.Decorator.PowerSystemInputs") !=
                                interfaces.end())
                            {
                                getPowerSystemInputsData(
                                    aResp, service, sensorPath, deviceType);
                            }
                            if (std::find(interfaces.begin(), interfaces.end(),
                                          "com.nvidia.MemorySpareChannel") !=
                                interfaces.end())
                            {
                                getMemorySpareChannelPresenceData(
                                    aResp, service, sensorPath, deviceType);
                            }
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetObject", sensorPath,
                    sensorInterfaces);
            }

            getPowerBreakThrottle(aResp, service, objPath, deviceType);
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_states",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getProcessorMetricsData(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                    const std::string& processorId)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    crow::connections::systemBus->async_method_call(
        [processorId, aResp{std::move(aResp)}](
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
                    std::string deviceType = "";
                    if (std::find(
                            interfaces.begin(), interfaces.end(),
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

                    if (std::find(interfaces.begin(), interfaces.end(),
                                  "xyz.openbmc_project.Inventory.Item.Cpu."
                                  "OperatingConfig") != interfaces.end())
                    {
                        getProcessorDataByService(aResp, service, path);
                    }
                    if (std::find(interfaces.begin(), interfaces.end(),
                                  "xyz.openbmc_project.Memory.MemoryECC") !=
                        interfaces.end())
                    {
                        getProcessorMemoryECCData(aResp, service, path);
                    }
                    if (std::find(interfaces.begin(), interfaces.end(),
                                  "xyz.openbmc_project.PCIe.PCIeECC") !=
                        interfaces.end())
                    {
                        redfish::processor_utils::getPCIeErrorData(
                            aResp, service, path);
                    }
                    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                    {
                        if (std::find(interfaces.begin(), interfaces.end(),
                                      "com.nvidia.NVLink.NVLinkMetrics") !=
                            interfaces.end())
                        {
                            getGPUNvlinkMetricsData(
                                aResp, service, path,
                                "com.nvidia.NVLink.NVLinkMetrics");
                        }

                        if (std::find(interfaces.begin(), interfaces.end(),
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

                        if (std::find(interfaces.begin(), interfaces.end(),
                                      "com.nvidia.SMUtilization") !=
                            interfaces.end())
                        {
                            nvidia_processor_utils::getSMUtilizationData(
                                aResp, service, path);
                        }

                        // Move to the end because deviceType might be
                        // reassigned
                        if (std::find(
                                interfaces.begin(), interfaces.end(),
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
                        getNumericSensorMetric(aResp, service, path,
                                               deviceType);
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
    crow::connections::systemBus->async_method_call(
        [aResp, memoryPath, processorCECount, processorUECount](
            const boost::system::error_code ec, GetSubTreeType& subtree) {
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
                if (connectionNames.size() < 1)
                {
                    BMCWEB_LOG_ERROR("Got 0 Connection names");
                    continue;
                }

                for (size_t i = 0; i < connectionNames.size(); i++)
                {
                    const std::string& connectionName =
                        connectionNames[i].first;
                    crow::connections::systemBus->async_method_call(
                        [aResp{aResp}, processorCECount, processorUECount](
                            const boost::system::error_code ec1,
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
    crow::connections::systemBus->async_method_call(
        [aResp, processorCECount,
         processorUECount](const boost::system::error_code ec,
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
    crow::connections::systemBus->async_method_call(
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

                    if (std::find(interfaces.begin(), interfaces.end(),
                                  memoryECCInterface) != interfaces.end())
                    {
                        crow::connections::systemBus->async_method_call(
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
                                redfish::nvidia_processor::
                                    getProcessorMemorySummary(aResp, path,
                                                              processorCECount,
                                                              processorUECount);
                            },
                            service, path, "org.freedesktop.DBus.Properties",
                            "GetAll", memoryECCInterface);
                    }
                    if (std::find(interfaces.begin(), interfaces.end(),
                                  memoryMetricIface) != interfaces.end())
                    {
                        crow::connections::systemBus->async_method_call(
                            [aResp{aResp}](
                                const boost::system::error_code ec,
                                const OperatingConfigProperties& properties) {
                                if (ec)
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

inline void
    getProcessorSettingsData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                             const std::string& processorId)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    crow::connections::systemBus->async_method_call(
        [aResp, processorId](boost::system::error_code ec,
                             const MapperGetSubTreeResponse& subtree) mutable {
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
                    if (std::find(interfaces.begin(), interfaces.end(),
                                  "xyz.openbmc_project.Memory.MemoryECC") !=
                        interfaces.end())
                    {
                         redfish::nvidia_processor::getEccPendingData(aResp, processorId, service, path);
                    }
                    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                    {
                        if (std::find(interfaces.begin(), interfaces.end(),
                                      "com.nvidia.CCMode") != interfaces.end())
                        {
                            redfish::nvidia_processor_utils::
                                getCCModePendingData(aResp, processorId,
                                                     service, path);
                        }
                        if (std::find(interfaces.begin(), interfaces.end(),
                                      "com.nvidia.EgmMode") != interfaces.end())
                        {
                            redfish::nvidia_processor_utils::
                                getEgmModePendingData(aResp, processorId,
                                                      service, path);
                        }
                    }
                    if (std::find(interfaces.begin(), interfaces.end(),
                                  "xyz.openbmc_project.Software.ApplyTime") !=
                        interfaces.end())
                    {
                        crow::connections::systemBus->async_method_call(
                            [aResp](
                                const boost::system::error_code ec1,
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

inline void patchEccMode(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& processorId, const bool eccModeEnabled,
    const std::string& cpuObjectPath, const MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* inventoryService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::find(interfaceList.begin(), interfaceList.end(),
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
            crow::connections::systemBus->async_method_call(
                [resp, processorId](boost::system::error_code ec,
                                    sdbusplus::message::message& msg) {
                    if (!ec)
                    {
                        BMCWEB_LOG_DEBUG("Set eccModeEnabled succeeded");
                        messages::success(resp->res);
                        return;
                    }

                    BMCWEB_LOG_DEBUG(
                        "CPU:{} set eccModeEnabled property failed: {}",
                        processorId, ec);
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

} // namespace nvidia_processor
} // namespace redfish
