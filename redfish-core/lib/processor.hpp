/*
// Copyright (c) 2018 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
#pragma once

#include "app.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/processor.hpp"
#include "health.hpp"
#include "pcie.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_async_set_callbacks.hpp"

#include <boost/container/flat_map.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/message/native_types.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <sdbusplus/utility/dedup_variant.hpp>
#include <utils/chassis_utils.hpp>
#include <utils/collection.hpp>
#include <utils/conditions_utils.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/json_utils.hpp>
#include <utils/nvidia_async_set_utils.hpp>
#include <utils/nvidia_processor_utils.hpp>
#include <utils/nvidia_utils.hpp>
#include <utils/port_utils.hpp>
#include <utils/processor_utils.hpp>
#include <utils/time_utils.hpp>

#include <array>
#include <filesystem>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>

namespace redfish
{

using GetSubTreeType = std::vector<
    std::pair<std::string,
              std::vector<std::pair<std::string, std::vector<std::string>>>>>;
using GetManagedPropertyType =
    boost::container::flat_map<std::string, dbus::utility::DbusVariantType>;
// Map of service name to list of interfaces
using MapperServiceMap =
    std::vector<std::pair<std::string, std::vector<std::string>>>;

// Map of object paths to MapperServiceMaps
using MapperGetSubTreeResponse =
    std::vector<std::pair<std::string, MapperServiceMap>>;

// Interfaces which imply a D-Bus object represents a Processor
constexpr std::array<std::string_view, 2> processorInterfaces = {
    "xyz.openbmc_project.Inventory.Item.Cpu",
    "xyz.openbmc_project.Inventory.Item.Accelerator"};

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
            [objPath,
             asyncResp](const boost::system::error_code ec,
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
                dbus_utils::UnpackErrorPrinter(), properties, "CurrentSpeed",
                currentSpeed, "ActiveWidth", activeWidth);

            asyncResp->res.jsonValue["SystemInterface"]["InterfaceType"] =
                "PCIe";

            if (!success)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            if ((currentSpeed != nullptr) && (activeWidth != nullptr))
            {
                asyncResp->res
                    .jsonValue["SystemInterface"]["PCIe"]["PCIeType"] =
                    redfish::port_utils::getLinkSpeedGeneration(*currentSpeed);
            }
            if (activeWidth != nullptr)
            {
                if (*activeWidth == INT_MAX)
                {
                    asyncResp->res
                        .jsonValue["SystemInterface"]["PCIe"]["LanesInUse"] = 0;
                }
                else
                {
                    asyncResp->res.jsonValue["SystemInterface"]["PCIe"]
                                            ["LanesInUse"] = *activeWidth;
                }
            }
        });
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
        std::array<const char*, 0>());
}

/*
 * @param[in,out]   asyncResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getProcessorUUID(std::shared_ptr<bmcweb::AsyncResp> asyncResp,
                             const std::string& service,
                             const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Processor UUID");
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, objPath,
        "xyz.openbmc_project.Common.UUID", "UUID",
        [objPath, asyncResp{std::move(asyncResp)}](
            const boost::system::error_code& ec, const std::string& property) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error");
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["UUID"] = property;
    });
}

inline void getCpuDataByInterface(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const dbus::utility::DBusInterfacesMap& cpuInterfacesProperties)
{
    BMCWEB_LOG_DEBUG("Get CPU resources by interface.");

    // Set the default value of state
    asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
    asyncResp->res.jsonValue["Status"]["Health"] = "OK";

    for (const auto& interface : cpuInterfacesProperties)
    {
        for (const auto& property : interface.second)
        {
            if (property.first == "Present")
            {
                const bool* cpuPresent = std::get_if<bool>(&property.second);
                if (cpuPresent == nullptr)
                {
                    // Important property not in desired type
                    messages::internalError(asyncResp->res);
                    return;
                }
                if (!*cpuPresent)
                {
                    // Slot is not populated
                    asyncResp->res.jsonValue["Status"]["State"] = "Absent";
                }
            }
            else if (property.first == "Functional")
            {
                const bool* cpuFunctional = std::get_if<bool>(&property.second);
                if (cpuFunctional == nullptr)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                if (!*cpuFunctional)
                {
                    asyncResp->res.jsonValue["Status"]["Health"] = "Critical";
                }
            }
            else if (property.first == "CoreCount")
            {
                const uint16_t* coresCount =
                    std::get_if<uint16_t>(&property.second);
                if (coresCount == nullptr)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["TotalCores"] = *coresCount;
            }
            else if (property.first == "MaxSpeedInMhz")
            {
                const uint32_t* value = std::get_if<uint32_t>(&property.second);
                if (value != nullptr)
                {
                    asyncResp->res.jsonValue["MaxSpeedMHz"] = *value;
                }
            }
            else if (property.first == "Socket")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value != nullptr)
                {
                    asyncResp->res.jsonValue["Socket"] = *value;
                }
            }
            else if (property.first == "ThreadCount")
            {
                const uint16_t* value = std::get_if<uint16_t>(&property.second);
                if (value != nullptr)
                {
                    asyncResp->res.jsonValue["TotalThreads"] = *value;
                }
            }
            else if (property.first == "EffectiveFamily")
            {
                const uint16_t* value = std::get_if<uint16_t>(&property.second);
                if (value != nullptr && *value != 2)
                {
                    asyncResp->res.jsonValue["ProcessorId"]["EffectiveFamily"] =
                        "0x" + intToHexString(*value, 4);
                }
            }
            else if (property.first == "EffectiveModel")
            {
                const uint16_t* value = std::get_if<uint16_t>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                if (*value != 0)
                {
                    asyncResp->res.jsonValue["ProcessorId"]["EffectiveModel"] =
                        "0x" + intToHexString(*value, 4);
                }
            }
            else if (property.first == "Id")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value != nullptr && *value != 0)
                {
                    asyncResp->res
                        .jsonValue["ProcessorId"]["IdentificationRegisters"] =
                        "0x" + intToHexString(*value, 16);
                }
            }
            else if (property.first == "Microcode")
            {
                const uint32_t* value = std::get_if<uint32_t>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                if (*value != 0)
                {
                    asyncResp->res.jsonValue["ProcessorId"]["MicrocodeInfo"] =
                        "0x" + intToHexString(*value, 8);
                }
            }
            else if (property.first == "Step")
            {
                const uint16_t* value = std::get_if<uint16_t>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                if (*value != std::numeric_limits<uint16_t>::max())
                {
                    asyncResp->res.jsonValue["ProcessorId"]["Step"] =
                        "0x" + intToHexString(*value, 4);
                }
            }
        }
    }
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
            [objPath,
             asyncResp](const boost::system::error_code ec,
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
                dbus_utils::UnpackErrorPrinter(), properties, "CurrentSpeed",
                currentSpeed, "ActiveWidth", activeWidth);

            if (!success)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            if ((currentSpeed != nullptr) && (activeWidth != nullptr))
            {
                speed =
                    redfish::port_utils::getLinkSpeedGeneration(*currentSpeed);
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
                 {"PCIe", {{"PCIeType", speed}, {"LanesInUse", width}}}});
            return;
        });
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
        std::array<const char*, 0>());
}

// TODO: getProcessorSystemPCIeInterface to new file
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

inline void getCpuDataByService(std::shared_ptr<bmcweb::AsyncResp> asyncResp,
                                const std::string& cpuId,
                                const std::string& service,
                                const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get available system cpu resources by service.");

    sdbusplus::message::object_path path("/xyz/openbmc_project/inventory");
    dbus::utility::getManagedObjects(
        service, path,
        [cpuId, service, objPath, asyncResp{std::move(asyncResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::ManagedObjectType& dbusData) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error");
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["Id"] = cpuId;
        asyncResp->res.jsonValue["Name"] = "Processor";
        asyncResp->res.jsonValue["ProcessorType"] = "CPU";

        bool slotPresent = false;
        std::string corePath = objPath + "/core";
        size_t totalCores = 0;
        for (const auto& object : dbusData)
        {
            if (object.first.str == objPath)
            {
                getCpuDataByInterface(asyncResp, object.second);
            }
            else if (object.first.str.starts_with(corePath))
            {
                for (const auto& interface : object.second)
                {
                    if (interface.first == "xyz.openbmc_project.Inventory.Item")
                    {
                        for (const auto& property : interface.second)
                        {
                            if (property.first == "Present")
                            {
                                const bool* present =
                                    std::get_if<bool>(&property.second);
                                if (present != nullptr)
                                {
                                    if (*present)
                                    {
                                        slotPresent = true;
                                        totalCores++;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        // In getCpuDataByInterface(), state and health are set
        // based on the present and functional status. If core
        // count is zero, then it has a higher precedence.
        if (slotPresent)
        {
            asyncResp->res.jsonValue["TotalCores"] = totalCores;
        }
        return;
    });
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

/**
 * @brief Fill out memory links association by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void
    getProcessorMemoryLinks(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            const std::string& objPath)
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
        nlohmann::json& linksArray = aResp->res.jsonValue["Links"]["Memory"];
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
                {{"@odata.id", "/redfish/v1/Systems/" +
                                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                   "/Memory/" + memoryName}});
        }
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_memory",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}
// TODO: move to a new file
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
        for (const std::pair<std::string, std::variant<std::string, size_t>>&
                 property : pcieDevProperties)
        {
            const std::string& propertyName = property.first;
            if ((propertyName == "LanesInUse") || (propertyName == "MaxLanes"))
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
                                        [propertyName] = getPCIeType(*value);
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
                if (!boost::ends_with(objectPath1, chassisName))
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
                getProcessorPCIeFunctionsLinks(aResp, serviceName, objectPath1,
                                               pcieDeviceLink);
            }
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree", parentChassisPath,
            0,
            std::array<const char*, 1>{"xyz.openbmc_project.Inventory.Item."
                                       "PCIeDevice"});
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

/**
 * @brief Translates throttle cause DBUS property to redfish.
 *
 * @param[in] dbusSource    The throttle cause from DBUS
 *
 * @return Returns as a string, the throttle cause in Redfish terms. If
 * translation cannot be done, returns "Unknown" throttle reason.
 */
inline processor::ThrottleCause
    dbusToRfThrottleCause(const std::string& dbusSource)
{
    if (dbusSource ==
        "xyz.openbmc_project.Control.Power.Throttle.ThrottleReasons.ClockLimit")
    {
        return processor::ThrottleCause::ClockLimit;
    }
    if (dbusSource ==
        "xyz.openbmc_project.Control.Power.Throttle.ThrottleReasons.ManagementDetectedFault")
    {
        return processor::ThrottleCause::ManagementDetectedFault;
    }
    if (dbusSource ==
        "xyz.openbmc_project.Control.Power.Throttle.ThrottleReasons.PowerLimit")
    {
        return processor::ThrottleCause::PowerLimit;
    }
    if (dbusSource ==
        "xyz.openbmc_project.Control.Power.Throttle.ThrottleReasons.ThermalLimit")
    {
        return processor::ThrottleCause::ThermalLimit;
    }
    if (dbusSource ==
        "xyz.openbmc_project.Control.Power.Throttle.ThrottleReasons.Unknown")
    {
        return processor::ThrottleCause::Unknown;
    }
    return processor::ThrottleCause::Invalid;
}

inline void
    readThrottleProperties(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const boost::system::error_code& ec,
                           const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Processor Throttle getAllProperties error {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    const bool* status = nullptr;
    const std::vector<std::string>* causes = nullptr;

    if (!sdbusplus::unpackPropertiesNoThrow(dbus_utils::UnpackErrorPrinter(),
                                            properties, "Throttled", status,
                                            "ThrottleCauses", causes))
    {
        messages::internalError(asyncResp->res);
        return;
    }

    asyncResp->res.jsonValue["Throttled"] = *status;
    nlohmann::json::array_t rCauses;
    for (const std::string& cause : *causes)
    {
        processor::ThrottleCause rfCause = dbusToRfThrottleCause(cause);
        if (rfCause == processor::ThrottleCause::Invalid)
        {
            messages::internalError(asyncResp->res);
            return;
        }

        rCauses.emplace_back(rfCause);
    }
    asyncResp->res.jsonValue["ThrottleCauses"] = std::move(rCauses);
}

inline void
    getThrottleProperties(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& service,
                          const std::string& objectPath)
{
    BMCWEB_LOG_DEBUG("Get processor throttle resources");

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, objectPath,
        "xyz.openbmc_project.Control.Power.Throttle",
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::DBusPropertiesMap& properties) {
        readThrottleProperties(asyncResp, ec, properties);
    });
}

inline void getCpuAssetData(std::shared_ptr<bmcweb::AsyncResp> asyncResp,
                            const std::string& service,
                            const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Cpu Asset Data");
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, objPath,
        "xyz.openbmc_project.Inventory.Decorator.Asset",
        [objPath, asyncResp{std::move(asyncResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::DBusPropertiesMap& properties) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error");
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

        if (manufacturer != nullptr)
        {
            asyncResp->res.jsonValue["Manufacturer"] = *manufacturer;

            // Otherwise would be unexpected.
            if (manufacturer->find("Intel") != std::string::npos)
            {
                asyncResp->res.jsonValue["ProcessorArchitecture"] = "x86";
                asyncResp->res.jsonValue["InstructionSet"] = "x86-64";
            }
            else if (manufacturer->find("IBM") != std::string::npos)
            {
                asyncResp->res.jsonValue["ProcessorArchitecture"] = "Power";
                asyncResp->res.jsonValue["InstructionSet"] = "PowerISA";
            }
        }

        if (partNumber != nullptr)
        {
            asyncResp->res.jsonValue["PartNumber"] = *partNumber;
        }

        if (sparePartNumber != nullptr && !sparePartNumber->empty())
        {
            asyncResp->res.jsonValue["SparePartNumber"] = *sparePartNumber;
        }
    });
}

// TODO: move to new file
/**
 * @brief Fill out links association to parent chassis by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void
    getProcessorChassisLink(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            const std::string& objPath,
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
                BMCWEB_LOG_ERROR("Chassis has no connected PCIe devices");
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
            getProcessorPCIeFunctionsLinks(aResp, service, pcieDevicePath,
                                           pcieDeviceLink);
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

inline void getCpuRevisionData(std::shared_ptr<bmcweb::AsyncResp> asyncResp,
                               const std::string& service,
                               const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Cpu Revision Data");
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, objPath,
        "xyz.openbmc_project.Inventory.Decorator.Revision",
        [objPath, asyncResp{std::move(asyncResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::DBusPropertiesMap& properties) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error");
            messages::internalError(asyncResp->res);
            return;
        }

        const std::string* version = nullptr;

        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), properties, "Version", version);

        if (!success)
        {
            messages::internalError(asyncResp->res);
            return;
        }

        if (version != nullptr)
        {
            asyncResp->res.jsonValue["Version"] = *version;
        }
    });
}

/**
 * @brief Fill out firmware version info of a accelerator by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void
    getProcessorFirmwareVersion(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                const std::string& service,
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

inline void getAcceleratorDataByService(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& acclrtrId,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get available system Accelerator resources by service.");

#ifdef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE
    std::shared_ptr<HealthRollup> health = std::make_shared<HealthRollup>(
        objPath, [aResp](const std::string& rootHealth,
                         const std::string& healthRollup) {
        aResp->res.jsonValue["Status"]["Health"] = rootHealth;
#ifndef BMCWEB_DISABLE_HEALTH_ROLLUP
        aResp->res.jsonValue["Status"]["HealthRollup"] = healthRollup;
#endif // BMCWEB_DISABLE_HEALTH_ROLLUP
    });
    health->start();
#endif // ifdef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, objPath, "",
        [acclrtrId, aResp{std::move(aResp)}](
            const boost::system::error_code ec,
            const dbus::utility::DBusPropertiesMap& properties) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error");
            messages::internalError(aResp->res);
            return;
        }

        const bool* functional = nullptr;
        const bool* present = nullptr;
        const std::string* accType = nullptr;
        const std::string* operationalState = nullptr;

        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), properties, "Functional",
            functional, "Present", present, "Type", accType, "State",
            operationalState);

        if (!success)
        {
            messages::internalError(aResp->res);
            return;
        }

        std::string state = "Enabled";
#ifndef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE
        std::string health = "OK";
#endif // ifndef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE

        if (present != nullptr && !*present)
        {
            state = "Absent";
        }
#ifndef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE
        if (functional != nullptr && !*functional)
        {
            if (state == "Enabled")
            {
                health = "Critical";
            }
        }
#else  // ifndef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE
        (void)functional;
#endif // ifndef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE

        aResp->res.jsonValue["Id"] = acclrtrId;
        aResp->res.jsonValue["Name"] = "Processor";
        aResp->res.jsonValue["Status"]["State"] = state;
#ifndef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE
        aResp->res.jsonValue["Status"]["Health"] = health;
#endif // ifndef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE

#ifdef BMCWEB_ENABLE_DEVICE_STATUS_FROM_FILE
        /** NOTES: This is a temporary solution to avoid performance issues may
         * impact other Redfish services. Please call for architecture decisions
         * from all NvBMC teams if want to use it in other places.
         */

#ifdef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE
#error "Conflicts! Please set health-rollup-alternative=disabled."
#endif

#ifdef BMCWEB_DISABLE_HEALTH_ROLLUP
#error "Conflicts! Please set disable-health-rollup=disabled."
#endif

        health_utils::getDeviceHealthInfo(aResp->res, acclrtrId);
#endif // BMCWEB_ENABLE_DEVICE_STATUS_FROM_FILE

        if (accType != nullptr && !accType->empty())
        {
            aResp->res.jsonValue["ProcessorType"] = getProcessorType(*accType);
        }

        if (operationalState != nullptr && !operationalState->empty())
        {
            aResp->res.jsonValue["Status"]["State"] =
                redfish::chassis_utils::getPowerStateType(*operationalState);
        }
    });
}

// OperatingConfig D-Bus Types
using TurboProfileProperty = std::vector<std::tuple<uint32_t, size_t>>;
using BaseSpeedPrioritySettingsProperty =
    std::vector<std::tuple<uint32_t, std::vector<uint32_t>>>;
using OperatingConfigProperties =
    std::vector<std::pair<std::string, dbus::utility::DbusVariantType>>;

// uint32_t and size_t may or may not be the same type, requiring a dedup'd
// variant

/**
 * Fill out the HighSpeedCoreIDs in a Processor resource from the given
 * OperatingConfig D-Bus property.
 *
 * @param[in,out]   aResp               Async HTTP response.
 * @param[in]       baseSpeedSettings   Full list of base speed priority groups,
 *                                      to use to determine the list of high
 *                                      speed cores.
 */
inline void highSpeedCoreIdsHandler(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const BaseSpeedPrioritySettingsProperty& baseSpeedSettings)
{
    // The D-Bus property does not indicate which bucket is the "high
    // priority" group, so let's discern that by looking for the one with
    // highest base frequency.
    auto highPriorityGroup = baseSpeedSettings.cend();
    uint32_t highestBaseSpeed = 0;
    for (auto it = baseSpeedSettings.cbegin(); it != baseSpeedSettings.cend();
         ++it)
    {
        const uint32_t baseFreq = std::get<uint32_t>(*it);
        if (baseFreq > highestBaseSpeed)
        {
            highestBaseSpeed = baseFreq;
            highPriorityGroup = it;
        }
    }

    nlohmann::json& jsonCoreIds = aResp->res.jsonValue["HighSpeedCoreIDs"];
    jsonCoreIds = nlohmann::json::array();

    // There may not be any entries in the D-Bus property, so only populate
    // if there was actually something there.
    if (highPriorityGroup != baseSpeedSettings.cend())
    {
        jsonCoreIds = std::get<std::vector<uint32_t>>(*highPriorityGroup);
    }
}

/**
 * Fill out OperatingConfig related items in a Processor resource by requesting
 * data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       cpuId       CPU D-Bus name.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getCpuConfigData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                             const std::string& cpuId,
                             const std::string& service,
                             const std::string& objPath)
{
    BMCWEB_LOG_INFO("Getting CPU operating configs for {}", cpuId);

    // First, GetAll CurrentOperatingConfig properties on the object
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, objPath,
        "xyz.openbmc_project.Control.Processor.CurrentOperatingConfig",
        [aResp, cpuId,
         service](const boost::system::error_code ec,
                  const dbus::utility::DBusPropertiesMap& properties) {
        if (ec)
        {
            BMCWEB_LOG_WARNING("D-Bus error: {}, {}", ec, ec.message());
            messages::internalError(aResp->res);
            return;
        }

        nlohmann::json& json = aResp->res.jsonValue;

        const sdbusplus::message::object_path* appliedConfig = nullptr;
        const bool* baseSpeedPriorityEnabled = nullptr;

        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), properties, "AppliedConfig",
            appliedConfig, "BaseSpeedPriorityEnabled",
            baseSpeedPriorityEnabled);

        if (!success)
        {
            messages::internalError(aResp->res);
            return;
        }

        if (appliedConfig != nullptr)
        {
            const std::string& dbusPath = appliedConfig->str;
            nlohmann::json::object_t operatingConfig;
            operatingConfig["@odata.id"] = boost::urls::format(
                "/redfish/v1/Systems/{}/Processors/{}/OperatingConfigs",
                BMCWEB_REDFISH_SYSTEM_URI_NAME, cpuId);
            json["OperatingConfigs"] = std::move(operatingConfig);

            // Reuse the D-Bus config object name for the Redfish
            // URI
            size_t baseNamePos = dbusPath.rfind('/');
            if (baseNamePos == std::string::npos ||
                baseNamePos == (dbusPath.size() - 1))
            {
                // If the AppliedConfig was somehow not a valid path,
                // skip adding any more properties, since everything
                // else is tied to this applied config.
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json::object_t appliedOperatingConfig;
            appliedOperatingConfig["@odata.id"] = boost::urls::format(
                "/redfish/v1/Systems/{}/Processors/{}/OperatingConfigs/{}",
                BMCWEB_REDFISH_SYSTEM_URI_NAME, cpuId,
                dbusPath.substr(baseNamePos + 1));
            json["AppliedOperatingConfig"] = std::move(appliedOperatingConfig);

            // Once we found the current applied config, queue another
            // request to read the base freq core ids out of that
            // config.
            sdbusplus::asio::getProperty<BaseSpeedPrioritySettingsProperty>(
                *crow::connections::systemBus, service, dbusPath,
                "xyz.openbmc_project.Inventory.Item.Cpu."
                "OperatingConfig",
                "BaseSpeedPrioritySettings",
                [aResp](
                    const boost::system::error_code ec2,
                    const BaseSpeedPrioritySettingsProperty& baseSpeedList) {
                if (ec2)
                {
                    BMCWEB_LOG_WARNING("D-Bus Property Get error: {}", ec2);
                    messages::internalError(aResp->res);
                    return;
                }

                highSpeedCoreIdsHandler(aResp, baseSpeedList);
            });
        }

        if (baseSpeedPriorityEnabled != nullptr)
        {
            json["BaseSpeedPriorityState"] =
                *baseSpeedPriorityEnabled ? "Enabled" : "Disabled";
        }
    });
}

/**
 * @brief Fill out location context of a processor by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void
    getProcessorLocationContext(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                const std::string& service,
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
inline void getCpuLocationCode(std::shared_ptr<bmcweb::AsyncResp> aResp,
                               const std::string& service,
                               const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Cpu Location Data");
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, objPath,
        "xyz.openbmc_project.Inventory.Decorator.LocationCode", "LocationCode",
        [objPath, aResp{std::move(aResp)}](const boost::system::error_code ec,
                                           const std::string& property) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error");
            messages::internalError(aResp->res);
            return;
        }

        aResp->res.jsonValue["Location"]["PartLocation"]["ServiceLabel"] =
            property;
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
inline void
    getProcessorReplaceable(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& connectionName,
                            const std::string& path)
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
 * Populate the unique identifier in a Processor resource by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getCpuUniqueId(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const std::string& service,
                           const std::string& objectPath)
{
    BMCWEB_LOG_DEBUG("Get CPU UniqueIdentifier");
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, objectPath,
        "xyz.openbmc_project.Inventory.Decorator.UniqueIdentifier",
        "UniqueIdentifier",
        [aResp](boost::system::error_code ec, const std::string& id) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to read cpu unique id: {}", ec);
            messages::internalError(aResp->res);
            return;
        }
        aResp->res.jsonValue["ProcessorId"]["ProtectedIdentificationNumber"] =
            id;
    });
}

/**
 * Request all the properties for the given D-Bus object and fill out the
 * related entries in the Redfish OperatingConfig response.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       service     D-Bus service name to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getOperatingConfigData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath, const std::string& deviceType)
{
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, objPath,
        "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig",
        [aResp,
         deviceType](const boost::system::error_code ec,
                     const dbus::utility::DBusPropertiesMap& properties) {
        if (ec)
        {
            BMCWEB_LOG_WARNING("D-Bus error: {}, {}", ec, ec.message());
            messages::internalError(aResp->res);
            return;
        }

        using SpeedConfigProperty = std::tuple<bool, uint32_t>;
        const SpeedConfigProperty* speedConfig = nullptr;
        const size_t* availableCoreCount = nullptr;
        const uint32_t* baseSpeed = nullptr;
        const uint32_t* defaultBoostClockSpeedMHz = nullptr;
        const uint32_t* maxJunctionTemperature = nullptr;
        const uint32_t* maxSpeed = nullptr;
        const uint32_t* minSpeed = nullptr;
        const uint32_t* operatingSpeed = nullptr;
        const uint32_t* powerLimit = nullptr;
        const TurboProfileProperty* turboProfile = nullptr;
        const BaseSpeedPrioritySettingsProperty* baseSpeedPrioritySettings =
            nullptr;

        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), properties, "AvailableCoreCount",
            availableCoreCount, "BaseSpeed", baseSpeed,
            "DefaultBoostClockSpeedMHz", defaultBoostClockSpeedMHz,
            "MaxJunctionTemperature", maxJunctionTemperature, "MaxSpeed",
            maxSpeed, "PowerLimit", powerLimit, "TurboProfile", turboProfile,
            "BaseSpeedPrioritySettings", baseSpeedPrioritySettings, "MinSpeed",
            minSpeed, "OperatingSpeed", operatingSpeed, "SpeedConfig",
            speedConfig);

        if (!success)
        {
            messages::internalError(aResp->res);
            return;
        }

        nlohmann::json& json = aResp->res.jsonValue;

        if (availableCoreCount != nullptr &&
            deviceType != "xyz.openbmc_project.Inventory.Item.Accelerator")
        {
            json["TotalAvailableCoreCount"] = *availableCoreCount;
        }

        if (baseSpeed != nullptr)
        {
            json["BaseSpeedMHz"] = *baseSpeed;
        }

        if (maxJunctionTemperature != nullptr &&
            deviceType != "xyz.openbmc_project.Inventory.Item.Accelerator")
        {
            json["MaxJunctionTemperatureCelsius"] = *maxJunctionTemperature;
        }

        if (maxSpeed != nullptr)
        {
            json["MaxSpeedMHz"] = *maxSpeed;
        }

        if (minSpeed != nullptr)
        {
            json["MinSpeedMHz"] = *minSpeed;
        }

        if (operatingSpeed != nullptr)
        {
            json["OperatingSpeedMHz"] = *operatingSpeed;
        }

        if (operatingSpeed != nullptr)
        {
            json["OperatingSpeedMHz"] = *operatingSpeed;
        }

        if (speedConfig != nullptr)
        {
            const auto& [speedLock, speed] = *speedConfig;
            json["SpeedLocked"] = speedLock;
            json["SpeedLimitMHz"] = speed;
        }

        if (turboProfile != nullptr && !turboProfile->empty())
        {
            nlohmann::json& turboArray = json["TurboProfile"];
            turboArray = nlohmann::json::array();
            for (const auto& [turboSpeed, coreCount] : *turboProfile)
            {
                nlohmann::json::object_t turbo;
                turbo["ActiveCoreCount"] = coreCount;
                turbo["MaxSpeedMHz"] = turboSpeed;
                turboArray.push_back(std::move(turbo));
            }
        }

        if (baseSpeedPrioritySettings != nullptr &&
            !baseSpeedPrioritySettings->empty())
        {
            nlohmann::json& baseSpeedArray = json["BaseSpeedPrioritySettings"];
            baseSpeedArray = nlohmann::json::array();
            for (const auto& [baseSpeedMhz, coreList] :
                 *baseSpeedPrioritySettings)
            {
                nlohmann::json::object_t speed;
                speed["CoreCount"] = coreList.size();
                speed["CoreIDs"] = coreList;
                speed["BaseSpeedMHz"] = baseSpeedMhz;
                baseSpeedArray.push_back(std::move(speed));
            }
        }
        if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
        {
            if (defaultBoostClockSpeedMHz != nullptr)
            {
                json["Oem"]["Nvidia"]["DefaultBoostClockSpeedMHz"] =
                    *defaultBoostClockSpeedMHz;
            }
        }
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
inline void
    getProcessorMemoryData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const std::string& cpuId, const std::string& service,
                           const std::string& objPath)
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
        std::string metricsURI = "/redfish/v1/Systems/" +
                                 std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                 "/Processors/";
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
                    json["MemorySummary"]["TotalCacheSizeMiB"] = (*value) >> 10;
                }
            }
            else if (key == "VolatileSizeInKiB")
            {
                const uint64_t* value = std::get_if<uint64_t>(&variant);
                if (value != nullptr)
                {
                    json["MemorySummary"]["TotalMemorySizeMiB"] = (*value) >>
                                                                  10;
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

inline void getEccPendingData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                              const std::string& cpuId,
                              const std::string& service,
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

inline void getProcessorEccModeData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath)
{
    nlohmann::json& json = aResp->res.jsonValue;
    std::string metricsURI = "/redfish/v1/Systems/" +
                             std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                             "/Processors/";
    metricsURI += cpuId;
    metricsURI += "/MemorySummary/MemoryMetrics";
    json["MemorySummary"]["Metrics"]["@odata.id"] = metricsURI;
    getEccModeData(aResp, cpuId, service, objPath);
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
                    {"target", "/redfish/v1/Systems/" +
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
            if (property.first == "Value")
            {
                const std::string* state =
                    std::get_if<std::string>(&property.second);
                if (state == nullptr)
                {
                    BMCWEB_LOG_DEBUG("Get Power Break Value property failed");
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
                deviceType != "xyz.openbmc_project.Inventory.Item.Accelerator")
            {
                const std::string* state =
                    std::get_if<std::string>(&property.second);
                if (state == nullptr)
                {
                    BMCWEB_LOG_DEBUG("Get Performance Value property failed");
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
                    BMCWEB_LOG_ERROR("Get Throttle reasons property failed");
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
                const uint64_t* val = std::get_if<uint64_t>(&property.second);
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
                (property.first == "GlobalSoftwareViolationThrottleDuration"))
            {
                auto propName = property.first;
                const uint64_t* val = std::get_if<uint64_t>(&property.second);
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
                (property.first == "AccumulatedGPUContextUtilizationDuration"))
            {
                auto propName = property.first;
                const uint64_t* val = std::get_if<uint64_t>(&property.second);
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
                const uint32_t* val = std::get_if<uint32_t>(&property.second);
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
            BMCWEB_LOG_ERROR("Can't get GPU Nvlink Metrics Iface properties ");
            return;
        }

        nlohmann::json& json = aResp->res.jsonValue;

        const double* nvlinkDataRxBandwidthGbps = nullptr;
        const double* nvlinkDataTxBandwidthGbps = nullptr;
        const double* nvlinkRawTxBandwidthGbps = nullptr;
        const double* nvlinkRawRxBandwidthGbps = nullptr;

        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), resp, "NVLinkDataRxBandwidthGbps",
            nvlinkDataRxBandwidthGbps, "NVLinkDataTxBandwidthGbps",
            nvlinkDataTxBandwidthGbps, "NVLinkRawRxBandwidthGbps",
            nvlinkRawRxBandwidthGbps, "NVLinkRawTxBandwidthGbps",
            nvlinkRawTxBandwidthGbps);

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

        const bool* memorySpareChannelPresence = std::get_if<bool>(&property);
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
                    "#NvidiaProcessor.v1_5_0.NvidiaGPU";
                json["Oem"]["Nvidia"]["MIGModeEnabled"] = *migModeEnabled;
            }
        }
    },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.MigMode");
}

inline void getProcessorMigModeData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG(" get GpuMIGMode data");
    getMigModeData(aResp, cpuId, service, objPath);
}

inline void getProcessorEgmModeData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG(" get EGMMode data");
    redfish::nvidia_processor_utils::getEgmModeData(aResp, cpuId, service,
                                                    objPath);
}

inline void getPortDisableFutureStatus(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& processorId, const std::string& objectPath,
    const dbus::utility::MapperServiceMap& serviceMap,
    const std::string& portId)
{
    BMCWEB_LOG_DEBUG("Get getPortDisableFutureStatus data");
    redfish::nvidia_processor_utils::getPortDisableFutureStatus(
        aResp, processorId, objectPath, serviceMap, portId);
}

inline void getProcessorSystemGUID(std::shared_ptr<bmcweb::AsyncResp> asyncResp,
                                   const std::string& service,
                                   const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get System-GUID");
    redfish::nvidia_processor_utils::getSysGUID(asyncResp, service, objPath);
}

inline void getMNNVLinkTopologyInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath,
    const std::string& interface)
{
    BMCWEB_LOG_DEBUG("Get MNNVLinkTopologyInfo");
    redfish::nvidia_processor_utils::getMNNVLinkTopologyInfo(
        aResp, cpuId, service, objPath, interface);
}

inline void
    getProcessorCCModeData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const std::string& cpuId, const std::string& service,
                           const std::string& objPath)
{
    BMCWEB_LOG_DEBUG(" get GpuCCMode data");
    redfish::nvidia_processor_utils::getCCModeData(aResp, cpuId, service,
                                                   objPath);
}

inline void
    getPowerSmoothingInfo(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                          std::string processorId, const std::string& service,
                          const std::string& objPath)
{
    BMCWEB_LOG_DEBUG(" get getPowerSmoothingInfo data");
    redfish::nvidia_processor_utils::getPowerSmoothingInfo(aResp, processorId,
                                                           service, objPath);
}

inline void getResetMetricsInfo(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                                std::string processorId,
                                const std::string& service,
                                const std::string& objPath)
{
    BMCWEB_LOG_DEBUG(" get getResetMetricsInfo data");
    redfish::nvidia_processor_utils::getResetMetricsInfo(aResp, processorId,
                                                         service, objPath);
}

inline void
    getWorkLoadPowerInfo(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         std::string processorId)
{
    BMCWEB_LOG_DEBUG(" get getWorkLoadPowerInfo data");
    redfish::nvidia_processor_utils::getWorkLoadPowerInfo(aResp, processorId);
}

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
                    BMCWEB_LOG_DEBUG("Get Performance Value property failed");
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
                "xyz.openbmc_project.ObjectMapper", "GetObject", effecterPath,
                effecterInterfaces);
        }
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_controls",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getGPMMetricsData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                              const std::string& service,
                              const std::string& objPath,
                              const std::string& gpmMetricsIface)
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
            "PCIeRawTxBandwidthGbps", pcieTxBandwidthGbps, "SMActivityPercent",
            smActivityPercent, "SMOccupancyPercent", smOccupancyPercent,
            "TensorCoreActivityPercent", tensorCoreActivityPercent,
            "IntegerActivityUtilizationPercent", integerActivityUtil,
            "DMMAUtilizationPercent", dmmaUtil, "HMMAUtilizationPercent",
            hmmaUtil, "IMMAUtilizationPercent", immaUtil,
            "NVDecInstanceUtilizationPercent", nvdecInstanceUtil,
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
            json["Oem"]["Nvidia"]["SMOccupancyPercent"] = *smOccupancyPercent;
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
            json["Oem"]["Nvidia"]["FP64ActivityPercent"] = *fp64ActivityPercent;
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
            json["Oem"]["Nvidia"]["FP32ActivityPercent"] = *fp32ActivityPercent;
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
            json["Oem"]["Nvidia"]["FP16ActivityPercent"] = *fp16ActivityPercent;
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

inline void getProcessorData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                             const std::string& processorId,
                             const std::string& objectPath,
                             const dbus::utility::MapperServiceMap& serviceMap,
                             const std::string& deviceType)
{
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        for (const auto& interface : interfaceList)
        {
            if (interface == "xyz.openbmc_project.Inventory.Decorator.Asset")
            {
                getCpuAssetData(aResp, serviceName, objectPath);
            }
            else if (interface ==
                     "xyz.openbmc_project.Inventory.Decorator.Revision")
            {
                getCpuRevisionData(aResp, serviceName, objectPath);
            }
            else if (interface == "xyz.openbmc_project.Inventory.Item.Cpu")
            {
                getCpuDataByService(aResp, processorId, serviceName,
                                    objectPath);
                if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                {
                    getRemoteDebugState(aResp, serviceName, objectPath);
                }
            }
            else if (interface ==
                     "xyz.openbmc_project.Inventory.Item.Accelerator")
            {
                getAcceleratorDataByService(aResp, processorId, serviceName,
                                            objectPath);
            }
            else if (
                interface ==
                "xyz.openbmc_project.Control.Processor.CurrentOperatingConfig")
            {
                getCpuConfigData(aResp, processorId, serviceName, objectPath);
            }
            else if (interface ==
                     "xyz.openbmc_project.Inventory.Decorator.LocationCode")
            {
                getCpuLocationCode(aResp, serviceName, objectPath);
            }
            else if (interface ==
                     "xyz.openbmc_project.Inventory.Decorator.LocationContext")
            {
                getProcessorLocationContext(aResp, serviceName, objectPath);
            }
            else if (interface == "xyz.openbmc_project.Inventory."
                                  "Decorator.Location")
            {
                getCpuLocationType(aResp, serviceName, objectPath);
            }
            else if (interface == "xyz.openbmc_project.Common.UUID")
            {
                getProcessorUUID(aResp, serviceName, objectPath);
            }
            else if (interface ==
                     "xyz.openbmc_project.Inventory.Decorator.UniqueIdentifier")
            {
                getCpuUniqueId(aResp, serviceName, objectPath);
            }
            else if (interface ==
                     "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig")
            {
                getOperatingConfigData(aResp, serviceName, objectPath,
                                       deviceType);
            }
            else if (interface ==
                     "xyz.openbmc_project.Inventory.Item.PersistentMemory")
            {
                getProcessorMemoryData(aResp, processorId, serviceName,
                                       objectPath);
            }
            else if (interface == "xyz.openbmc_project.Memory.MemoryECC")
            {
                getProcessorEccModeData(aResp, processorId, serviceName,
                                        objectPath);
            }
            else if (interface == "xyz.openbmc_project.Inventory."
                                  "Decorator.FpgaType")
            {
                getFpgaTypeData(aResp, serviceName, objectPath);
            }
            else if (interface == "xyz.openbmc_project.Control.Processor.Reset")
            {
                getProcessorResetTypeData(aResp, processorId, serviceName,
                                          objectPath);
            }
            else if (interface ==
                     "xyz.openbmc_project.Inventory.Decorator.Replaceable")
            {
                getProcessorReplaceable(aResp, serviceName, objectPath);
            }

            if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
            {
                if (interface == "com.nvidia.MigMode")
                {
                    getProcessorMigModeData(aResp, processorId, serviceName,
                                            objectPath);
                }
                else if (interface == "com.nvidia.CCMode")
                {
                    getProcessorCCModeData(aResp, processorId, serviceName,
                                           objectPath);
                }
                else if (interface ==
                         "com.nvidia.PowerSmoothing.PowerSmoothing")
                {
                    getPowerSmoothingInfo(aResp, processorId, serviceName,
                                          objectPath);
                }
                else if (interface == "com.nvidia.NVLink.NvLinkTotalCount")
                {
                    redfish::nvidia_processor_utils::getNvLinkTotalCount(
                        aResp, processorId, serviceName, objectPath);
                }
                else if (interface == "com.nvidia.PowerProfile.ProfileInfo")
                {
                    getWorkLoadPowerInfo(aResp, processorId);
                }
                else if (interface == "com.nvidia.SysGUID.SysGUID")
                {
                    getProcessorSystemGUID(aResp, serviceName, objectPath);
                }
                else if (interface == "com.nvidia.EgmMode")
                {
                    getProcessorEgmModeData(aResp, processorId, serviceName,
                                            objectPath);
                }
                else if (interface == "com.nvidia.NVLink.MNNVLinkTopology")
                {
                    getMNNVLinkTopologyInfo(aResp, processorId, serviceName,
                                            objectPath, interface);
                }
                else if (
                    interface ==
                    "com.nvidia.ResetCounters.ResetCounterMetricsSupported")
                {
                    getResetMetricsInfo(aResp, processorId, serviceName,
                                        objectPath);
                }
            }
        }
    }

    getComponentFirmwareVersion(aResp, objectPath);
    redfish::nvidia_processor_utils::getOperatingSpeedRange(aResp, objectPath);

    aResp->res.jsonValue["EnvironmentMetrics"] = {
        {"@odata.id",
         "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
             "/Processors/" + processorId + "/EnvironmentMetrics"}};
    aResp->res.jsonValue["@Redfish.Settings"]["@odata.type"] =
        "#Settings.v1_3_3.Settings";
    aResp->res.jsonValue["@Redfish.Settings"]["SettingsObject"] = {
        {"@odata.id", "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/Processors/" + processorId + "/Settings"}};

    aResp->res.jsonValue["Ports"] = {
        {"@odata.id", "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/Processors/" + processorId + "/Ports"}};
    // Links association to underneath memory
    getProcessorMemoryLinks(aResp, objectPath);
    // Link association to parent chassis
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        getProcessorChassisLink(aResp, objectPath, serviceName);
    }
    // Get system and fpga interfaces properties
    getProcessorSystemPCIeInterface(aResp, objectPath);
    getProcessorFPGAPCIeInterface(aResp, objectPath);

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        nvidia_processor_utils::getReconfigPermissionsData(aResp, processorId,
                                                           objectPath);
        nvidia_processor_utils::populateErrorInjectionData(aResp, processorId);
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
         service = *inventoryService](
            const boost::system::error_code& ec,
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
                    "com.nvidia.MigMode", "MIGModeEnabled",
                    std::variant<bool>(migMode),
                    nvidia_async_operation_utils::PatchMigModeCallback{resp});

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

            if (strcmp(dbusError->name, "xyz.openbmc_project.Common."
                                        "Device.Error.WriteFailure") == 0)
            {
                // Service failed to change the config
                messages::operationFailed(resp->res);
            }
            else if (strcmp(dbusError->name,
                            "xyz.openbmc_project.Common.Error.Unavailable") ==
                     0)
            {
                std::string errBusy = "0x50A";
                std::string errBusyResolution =
                    "SMBPBI Command failed with error busy, please try after 60 seconds";

                // busy error
                messages::asyncError(resp->res, errBusy, errBusyResolution);
            }
            else if (strcmp(dbusError->name,
                            "xyz.openbmc_project.Common.Error.Timeout") == 0)
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
            service, cpuObjectPath, "org.freedesktop.DBus.Properties", "Set",
            "com.nvidia.MigMode", "MIGModeEnabled",
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
                    BMCWEB_LOG_DEBUG(" No any state effecter interface. ");
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
                            aResp, service, effecterPath, remoteDebugEnabled);
                    }
                }
            },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetObject", effecterPath,
                effecterInterfaces);
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
         service = *inventoryService](
            const boost::system::error_code& ec,
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
                    std::variant<std::tuple<bool, uint32_t>>(reqSpeedConfig),
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

            BMCWEB_LOG_DEBUG("CPU:{} set speed config property failed: {}",
                             processorId, ec);
            // Read and convert dbus error message to redfish error
            const sd_bus_error* dbusError = msg.get_error();
            if (dbusError == nullptr)
            {
                messages::internalError(resp->res);
                return;
            }
            if (strcmp(dbusError->name,
                       "xyz.openbmc_project.Common.Error.InvalidArgument") == 0)
            {
                // Invalid value
                uint32_t speedLimit = std::get<1>(reqSpeedConfig);
                messages::propertyValueIncorrect(resp->res, "SpeedLimitMHz",
                                                 std::to_string(speedLimit));
            }
            else if (strcmp(dbusError->name,
                            "xyz.openbmc_project.Common.Error.Unavailable") ==
                     0)
            {
                std::string errBusy = "0x50A";
                std::string errBusyResolution =
                    "SMBPBI Command failed with error busy, please try after 60 seconds";

                // busy error
                messages::asyncError(resp->res, errBusy, errBusyResolution);
            }
            else if (strcmp(dbusError->name,
                            "xyz.openbmc_project.Common.Error.Timeout") == 0)
            {
                std::string errTimeout = "0x600";
                std::string errTimeoutResolution =
                    "Settings may/maynot have applied, please check get response before patching";

                // timeout error
                messages::asyncError(resp->res, errTimeout,
                                     errTimeoutResolution);
            }
            else if (strcmp(dbusError->name, "xyz.openbmc_project.Common."
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
            service, cpuObjectPath, "org.freedesktop.DBus.Properties", "Set",
            "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig",
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
inline void patchSpeedLocked(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                             const std::string& processorId,
                             const bool speedLocked,
                             const std::string& cpuObjectPath,
                             const MapperServiceMap& serviceMap)
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
inline void patchSpeedLimit(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                            const std::string& processorId,
                            const int speedLimit,
                            const std::string& cpuObjectPath,
                            const MapperServiceMap& serviceMap)
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

/**
 * Handle the D-Bus response from attempting to set the CPU's AppliedConfig
 * property. Main task is to translate error messages into Redfish errors.
 *
 * @param[in,out]   resp    HTTP response.
 * @param[in]       setPropVal  Value which we attempted to set.
 * @param[in]       ec      D-Bus response error code.
 * @param[in]       msg     D-Bus response message.
 */
inline void
    handleAppliedConfigResponse(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                                const std::string& setPropVal,
                                boost::system::error_code ec,
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
 * Handle the PATCH operation of the AppliedOperatingConfig property. Do basic
 * validation of the input data, and then set the D-Bus property.
 *
 * @param[in,out]   resp            Async HTTP response.
 * @param[in]       processorId     Processor's Id.
 * @param[in]       appliedConfigUri    New property value to apply.
 * @param[in]       cpuObjectPath   Path of CPU object to modify.
 * @param[in]       serviceMap      Service map for CPU object.
 */
inline void patchAppliedOperatingConfig(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& processorId, const std::string& appliedConfigUri,
    const std::string& cpuObjectPath,
    const dbus::utility::MapperServiceMap& serviceMap)
{
    // Check that the property even exists by checking for the interface
    const std::string* controlService = nullptr;
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        if (std::ranges::find(interfaceList,
                              "xyz.openbmc_project.Control.Processor."
                              "CurrentOperatingConfig") != interfaceList.end())
        {
            controlService = &serviceName;
            break;
        }
    }

    if (controlService == nullptr)
    {
        messages::internalError(resp->res);
        return;
    }

    // Check that the config URI is a child of the cpu URI being patched.
    std::string expectedPrefix(std::format("/redfish/v1/Systems/{}/Processors/",
                                           BMCWEB_REDFISH_SYSTEM_URI_NAME));
    expectedPrefix += processorId;
    expectedPrefix += "/OperatingConfigs/";
    if (!appliedConfigUri.starts_with(expectedPrefix) ||
        expectedPrefix.size() == appliedConfigUri.size())
    {
        messages::propertyValueIncorrect(resp->res, "AppliedOperatingConfig",
                                         appliedConfigUri);
        return;
    }

    // Generate the D-Bus path of the OperatingConfig object, by assuming it's a
    // direct child of the CPU object.
    // Strip the expectedPrefix from the config URI to get the "filename", and
    // append to the CPU's path.
    std::string configBaseName = appliedConfigUri.substr(expectedPrefix.size());
    sdbusplus::message::object_path configPath(cpuObjectPath);
    configPath /= configBaseName;

    BMCWEB_LOG_INFO("Setting config to {}", configPath.str);

    // Set the property, with handler to check error responses
    setDbusProperty(
        resp, "AppliedOperatingConfig", *controlService, cpuObjectPath,
        "xyz.openbmc_project.Control.Processor.CurrentOperatingConfig",
        "AppliedConfig", configPath);
}

inline void requestRoutesOperatingConfigCollection(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/Processors/<str>/OperatingConfigs/")
        .privileges(redfish::privileges::getOperatingConfigCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& cpuName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        asyncResp->res.jsonValue["@odata.type"] =
            "#OperatingConfigCollection.OperatingConfigCollection";
        asyncResp->res.jsonValue["@odata.id"] = req.url();
        asyncResp->res.jsonValue["Name"] = "Operating Config Collection";

        // First find the matching CPU object so we know how to
        // constrain our search for related Config objects.
        crow::connections::systemBus->async_method_call(
            [asyncResp, cpuName](
                const boost::system::error_code ec,
                const dbus::utility::MapperGetSubTreePathsResponse& objects) {
            if (ec)
            {
                BMCWEB_LOG_WARNING("D-Bus error: {}, {}", ec, ec.message());
                messages::internalError(asyncResp->res);
                return;
            }

            for (const std::string& object : objects)
            {
                if (!object.ends_with(cpuName))
                {
                    continue;
                }

                // Not expected that there will be multiple matching
                // CPU objects, but if there are just use the first
                // one.

                // Use the common search routine to construct the
                // Collection of all Config objects under this CPU.
                constexpr std::array<std::string_view, 1> interface{
                    "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig"};
                collection_util::getCollectionMembers(
                    asyncResp,
                    boost::urls::format(
                        "/redfish/v1/Systems/{}/Processors/{}/OperatingConfigs",
                        BMCWEB_REDFISH_SYSTEM_URI_NAME, cpuName),
                    interface, object);
                return;
            }
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
            "/xyz/openbmc_project/inventory", 0,
            std::array<const char*, 1>{
                "xyz.openbmc_project.Control.Processor.CurrentOperatingConfig"});
    });
}

inline void requestRoutesOperatingConfig(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Processors/<str>/OperatingConfigs/<str>/")
        .privileges(redfish::privileges::getOperatingConfig)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& cpuName, const std::string& configName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        // Ask for all objects implementing OperatingConfig so we can search
        // for one with a matching name
        crow::connections::systemBus->async_method_call(
            [asyncResp, cpuName, configName, reqUrl{req.url()}](
                boost::system::error_code ec,
                const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_WARNING("D-Bus error: {}, {}", ec, ec.message());
                messages::internalError(asyncResp->res);
                return;
            }
            const std::string expectedEnding = cpuName + '/' + configName;
            for (const auto& [objectPath, serviceMap] : subtree)
            {
                // Ignore any configs without matching cpuX/configY
                if (!objectPath.ends_with(expectedEnding) || serviceMap.empty())
                {
                    continue;
                }

                nlohmann::json& json = asyncResp->res.jsonValue;
                json["@odata.type"] = "#OperatingConfig.v1_0_0.OperatingConfig";
                json["@odata.id"] = reqUrl;
                json["Name"] = "Processor Profile";
                json["Id"] = configName;

                std::string deviceType =
                    "xyz.openbmc_project.Inventory.Item.Cpu";
                // Just use the first implementation of the object - not
                // expected that there would be multiple matching
                // services
                getOperatingConfigData(asyncResp, serviceMap.begin()->first,
                                       objectPath, deviceType);
                return;
            }
            messages::resourceNotFound(asyncResp->res, "OperatingConfig",
                                       configName);
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/inventory", 0,
            std::array<const char*, 1>{
                "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig"});
    });
}

inline void requestRoutesProcessorCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/")
        .privileges(redfish::privileges::getProcessorCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
        {
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       systemName);
            return;
        }
        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/ProcessorCollection/ProcessorCollection.json>; rel=describedby");

        asyncResp->res.jsonValue["@odata.type"] =
            "#ProcessorCollection.ProcessorCollection";
        asyncResp->res.jsonValue["Name"] = "Processor Collection";

        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/Processors";

        asyncResp->res.jsonValue["Members"] = nlohmann::json::array();

        collection_util::getCollectionMembers(
            asyncResp,
            boost::urls::url("/redfish/v1/Systems/" +
                             std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                             "/Processors"),
            processorInterfaces, "/xyz/openbmc_project/inventory");
    });
}

inline void requestRoutesProcessor(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& systemName,
                   const std::string& processorId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
        {
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       systemName);
            return;
        }

        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/Processor/Processor.json>; rel=describedby");
        asyncResp->res.jsonValue["@odata.type"] =
            "#Processor.v1_20_0.Processor";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/Processors/" +
            processorId;
        std::string processorMetricsURI =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/Processors/";
        processorMetricsURI += processorId;
        processorMetricsURI += "/ProcessorMetrics";
        asyncResp->res.jsonValue["Metrics"]["@odata.id"] = processorMetricsURI;

        redfish::processor_utils::getProcessorObject(asyncResp, processorId,
                                                     getProcessorData);
#ifndef BMCWEB_DISABLE_CONDITIONS_ARRAY
        redfish::conditions_utils::populateServiceConditions(asyncResp,
                                                             processorId);
#endif // BMCWEB_DISABLE_CONDITIONS_ARRAY
    });

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/")
        .privileges(redfish::privileges::patchProcessor)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& systemName,
                   const std::string& processorId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
        {
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       systemName);
            return;
        }

        std::optional<int> speedLimit;
        std::optional<bool> speedLocked;
        std::optional<nlohmann::json> oemObject;
        std::optional<nlohmann::json> operatingSpeedRangeMHzObject;
        std::optional<uint32_t> settingMin;
        std::optional<uint32_t> settingMax;
        std::optional<std::string> appliedConfigUri;
        if (!redfish::json_util::readJsonAction(
                req, asyncResp->res, "SpeedLimitMHz", speedLimit, "SpeedLocked",
                speedLocked, "AppliedOperatingConfig/@odata.id",
                appliedConfigUri, "Oem", oemObject, "OperatingSpeedRangeMHz",
                operatingSpeedRangeMHzObject))
        {
            return;
        }
        // speedlimit is required property for patching speedlocked
        if (!speedLimit && speedLocked)
        {
            BMCWEB_LOG_ERROR("SpeedLimit value required ");
            messages::propertyMissing(asyncResp->res, "SpeedLimit");
        }

        // Update speed limit
        else if (speedLimit && speedLocked)
        {
            std::tuple<bool, uint32_t> reqSpeedConfig;
            reqSpeedConfig = std::make_tuple(
                *speedLocked, static_cast<uint32_t>(*speedLimit));
            redfish::processor_utils::getProcessorObject(
                asyncResp, processorId,
                [reqSpeedConfig](
                    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp1,
                    const std::string& processorId1,
                    const std::string& objectPath,
                    const MapperServiceMap& serviceMap,
                    [[maybe_unused]] const std::string& deviceType) {
                patchSpeedConfig(asyncResp1, processorId1, reqSpeedConfig,
                                 objectPath, serviceMap);
            });
        }

        else if (operatingSpeedRangeMHzObject &&
                 redfish::json_util::readJson(
                     *operatingSpeedRangeMHzObject, asyncResp->res,
                     "SettingMax", settingMax, "SettingMin", settingMin))
        {
            redfish::processor_utils::getProcessorObject(
                asyncResp, processorId,
                [settingMin, settingMax](
                    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                    const std::string& processorId,
                    const std::string& objectPath,
                    [[maybe_unused]] const MapperServiceMap& serviceMap,
                    [[maybe_unused]] const std::string& deviceType) {
                if (settingMax)
                {
                    redfish::nvidia_processor_utils::
                        patchOperatingSpeedRangeMHz(asyncResp, processorId,
                                                    *settingMax, "SettingMax",
                                                    objectPath);
                }
                else if (settingMin)
                {
                    redfish::nvidia_processor_utils::
                        patchOperatingSpeedRangeMHz(asyncResp, processorId,
                                                    *settingMin, "SettingMin",
                                                    objectPath);
                }
            });
        }

        if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
        {
            // Update migMode
            if (std::optional<nlohmann::json> oemNvidiaObject;
                oemObject &&
                redfish::json_util::readJson(*oemObject, asyncResp->res,
                                             "Nvidia", oemNvidiaObject))
            {
                std::optional<bool> migMode;
                std::optional<bool> remoteDebugEnabled;
                std::optional<nlohmann::json> inbandReconfigPermissions;
                std::optional<nlohmann::json> doeReconfigPermissions;

                if (oemNvidiaObject &&
                    redfish::json_util::readJson(
                        *oemNvidiaObject, asyncResp->res, "MIGModeEnabled",
                        migMode, "RemoteDebugEnabled", remoteDebugEnabled,
                        "InbandReconfigPermissions", inbandReconfigPermissions,
                        "DOEReconfigPermissions", doeReconfigPermissions))
                {
                    if (migMode)
                    {
                        redfish::processor_utils::getProcessorObject(
                            asyncResp, processorId,
                            [migMode](const std::shared_ptr<bmcweb::AsyncResp>&
                                          asyncResp1,
                                      const std::string& processorId1,
                                      const std::string& objectPath,
                                      const MapperServiceMap& serviceMap,
                                      [[maybe_unused]] const std::string&
                                          deviceType) {
                            patchMigMode(asyncResp1, processorId1, *migMode,
                                         objectPath, serviceMap);
                        });
                    }

                    if (remoteDebugEnabled)
                    {
                        redfish::processor_utils::getProcessorObject(
                            asyncResp, processorId,
                            [remoteDebugEnabled](
                                const std::shared_ptr<bmcweb::AsyncResp>&
                                    asyncResp,
                                const std::string& processorId,
                                const std::string& objectPath,
                                [[maybe_unused]] const MapperServiceMap&
                                    serviceMap,
                                [[maybe_unused]] const std::string&
                                    deviceType) {
                            patchRemoteDebug(asyncResp, processorId,
                                             *remoteDebugEnabled, objectPath);
                        });
                    }

                    if (inbandReconfigPermissions)
                    {
                        nvidia_processor_utils::patchInbandReconfigPermissions(
                            asyncResp, processorId, *inbandReconfigPermissions);
                    }

                    if (doeReconfigPermissions)
                    {
                        nvidia_processor_utils::patchDOEReconfigPermissions(
                            asyncResp, processorId, *doeReconfigPermissions);
                    }
                }
            }
        }

        if (appliedConfigUri)
        {
            // Check for 404 and find matching D-Bus object, then run
            // property patch handlers if that all succeeds.
            redfish::processor_utils::getProcessorObject(
                asyncResp, processorId,
                [appliedConfigUri = std::move(appliedConfigUri)](
                    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp1,
                    const std::string& processorId1,
                    const std::string& objectPath,
                    const MapperServiceMap& serviceMap,
                    [[maybe_unused]] const std::string& deviceType) {
                patchAppliedOperatingConfig(asyncResp1, processorId1,
                                            *appliedConfigUri, objectPath,
                                            serviceMap);
            });
        }
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
                const uint32_t* value = std::get_if<uint32_t>(&property.second);
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
                const int64_t* value = std::get_if<int64_t>(&property.second);
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
                const int64_t* value = std::get_if<int64_t>(&property.second);
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
            (boost::format("/redfish/v1/Chassis/%s/Sensors/%s") % chassisId %
             sensorName)
                .str();
        aResp->res.jsonValue["CoreVoltage"]["DataSourceUri"] = sensorURI;
        const double* attributeValue = nullptr;
        for (const std::pair<std::string, std::variant<std::string, double>>&
                 property : propertiesList)
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
                    BMCWEB_LOG_ERROR("Got path that isn't long enough {}",
                                     objPath);
                    continue;
                }
                const std::string& sensorType = split[4];
                if (sensorType == "voltage")
                {
                    getVoltageData(aResp, service, chassisId, sensorPath);
                }
            }
        },
            "xyz.openbmc_project.ObjectMapper", chassisPath + "/all_sensors",
            "org.freedesktop.DBus.Properties", "Get",
            "xyz.openbmc_project.Association", "endpoints");
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/parent_chassis",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void
    getPowerBreakThrottle(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                          const std::string& service, const std::string objPath,
                          const std::string& deviceType)
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
                BMCWEB_LOG_DEBUG("proc module state sensor object path {}",
                                 sensorpath);

                const std::array<const char*, 1> sensorinterfaces = {
                    "xyz.openbmc_project.State.ProcessorPerformance"};
                // process sensor reading
                crow::connections::systemBus->async_method_call(
                    [aResp, sensorpath, deviceType](
                        const boost::system::error_code ec,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& object) {
                    if (ec)
                    {
                        // the path does not implement any state
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
                            getPowerBreakThrottleData(aResp, service,
                                                      sensorpath, deviceType);
                        }
                    }
                },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetObject", sensorpath,
                    sensorinterfaces);
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

inline void
    getStateSensorMetric(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& service, const std::string& objPath,
                         const std::string& deviceType)
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
                    // The path does not implement any state interfaces.
                    return;
                }

                for (const auto& [service, interfaces] : object)
                {
                    if (std::find(
                            interfaces.begin(), interfaces.end(),
                            "xyz.openbmc_project.State.ProcessorPerformance") !=
                        interfaces.end())
                    {
                        getProcessorPerformanceData(aResp, service, sensorPath,
                                                    deviceType);
                    }
                    if (std::find(
                            interfaces.begin(), interfaces.end(),
                            "xyz.openbmc_project.State.Decorator.PowerSystemInputs") !=
                        interfaces.end())
                    {
                        getPowerSystemInputsData(aResp, service, sensorPath,
                                                 deviceType);
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
                    if (std::find(interfaces.begin(), interfaces.end(),
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
            if (!boost::ends_with(path, processorId))
            {
                continue;
            }
            std::string processorMetricsURI =
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/Processors/";
            processorMetricsURI += processorId;
            processorMetricsURI += "/ProcessorMetrics";
            aResp->res.jsonValue["@odata.type"] =
                "#ProcessorMetrics.v1_6_1.ProcessorMetrics";
            aResp->res.jsonValue["@odata.id"] = processorMetricsURI;
            aResp->res.jsonValue["Id"] = "ProcessorMetrics";
            aResp->res.jsonValue["Name"] = processorId + " Processor Metrics";
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
                    redfish::processor_utils::getPCIeErrorData(aResp, service,
                                                               path);
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
                                  "com.nvidia.GPMMetrics") != interfaces.end())
                    {
                        // Assign the device type to Accelerator because we have
                        // found the GPMMetrics interface here
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

                    // Move to the end because deviceType might be reassigned
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
                    getNumericSensorMetric(aResp, service, path, deviceType);
                }
            }
            return;
        }
        // Object not found
        messages::resourceNotFound(aResp->res, "#Processor.v1_20_0.Processor",
                                   processorId);
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 3>{
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "xyz.openbmc_project.Inventory.Item.Cpu", "com.nvidia.GPMMetrics"});
}

inline void requestRoutesProcessorMetrics(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/Processors/<str>/ProcessorMetrics")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        getProcessorMetricsData(asyncResp, processorId);
    });
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
        for (const std::pair<
                 std::string,
                 std::vector<std::pair<std::string, std::vector<std::string>>>>&
                 object : subtree)
        {
            // Get the processor memory
            if (object.first != memoryPath)
            {
                continue;
            }
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                connectionNames = object.second;
            if (connectionNames.size() < 1)
            {
                BMCWEB_LOG_ERROR("Got 0 Connection names");
                continue;
            }

            for (size_t i = 0; i < connectionNames.size(); i++)
            {
                const std::string& connectionName = connectionNames[i].first;
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
                            const double* value =
                                std::get_if<double>(&property.second);
                            if (value == nullptr)
                            {
                                messages::internalError(aResp->res);
                                return;
                            }
                            aResp->res.jsonValue["BandwidthPercent"] = *value;
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
                            aResp->res.jsonValue["LifeTime"]
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
                            aResp->res.jsonValue["LifeTime"]
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
                                            processorCECount, processorUECount);
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
            if (!boost::ends_with(path, processorId))
            {
                continue;
            }
            std::string memoryMetricsURI =
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/Processors/";
            memoryMetricsURI += processorId;
            memoryMetricsURI += "/MemorySummary/MemoryMetrics";
            aResp->res.jsonValue["@odata.type"] =
                "#MemoryMetrics.v1_7_0.MemoryMetrics";
            aResp->res.jsonValue["@odata.id"] = memoryMetricsURI;
            aResp->res.jsonValue["Id"] = "MemoryMetrics";
            aResp->res.jsonValue["Name"] = processorId +
                                           " Memory Summary Metrics";
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
                                    std::get_if<int64_t>(&property.second);
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
                                    std::get_if<int64_t>(&property.second);
                                if (value == nullptr)
                                {
                                    messages::internalError(aResp->res);
                                    return;
                                }
                                processorUECount = *value;
                            }
                        }
                        // Get processor memory summary data
                        getProcessorMemorySummary(aResp, path, processorCECount,
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
                            if (property.first == "CapacityUtilizationPercent")
                            {
                                const uint8_t* value =
                                    std::get_if<uint8_t>(&property.second);
                                if (value == nullptr)
                                {
                                    messages::internalError(aResp->res);
                                    return;
                                }
                                aResp->res
                                    .jsonValue["CapacityUtilizationPercent"] =
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
        messages::resourceNotFound(aResp->res, "#Processor.v1_20_0.Processor",
                                   processorId);
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 2>{
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "com.nvidia.GPMMetrics"});
}

inline void requestRoutesProcessorMemoryMetrics(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "MemorySummary/MemoryMetrics")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        getProcessorMemoryMetricsData(asyncResp, processorId);
    });
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
            if (!boost::ends_with(path, processorId))
            {
                continue;
            }
            nlohmann::json& json = aResp->res.jsonValue;
            json["@odata.id"] = "/redfish/v1/Systems/" +
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
                    getEccPendingData(aResp, processorId, service, path);
                }
                if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                {
                    if (std::find(interfaces.begin(), interfaces.end(),
                                  "com.nvidia.CCMode") != interfaces.end())
                    {
                        redfish::nvidia_processor_utils::getCCModePendingData(
                            aResp, processorId, service, path);
                    }
                    if (std::find(interfaces.begin(), interfaces.end(),
                                  "com.nvidia.EgmMode") != interfaces.end())
                    {
                        redfish::nvidia_processor_utils::getEgmModePendingData(
                            aResp, processorId, service, path);
                    }
                }
                if (std::find(interfaces.begin(), interfaces.end(),
                              "xyz.openbmc_project.Software.ApplyTime") !=
                    interfaces.end())
                {
                    crow::connections::systemBus->async_method_call(
                        [aResp](const boost::system::error_code ec1,
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
                                    std::get_if<std::string>(&property.second);
                                if (applyTime == nullptr)
                                {
                                    messages::internalError(aResp->res);
                                    return;
                                }
                                json1["@Redfish.SettingsApplyTime"]
                                     ["@odata.type"] =
                                         "#Settings.v1_3_3.PreferredApplyTime";
                                json1["@Redfish.SettingsApplyTime"]
                                     ["ApplyTime"] =
                                         toRequestedApplyTime(*applyTime);
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
                         const MapperServiceMap& serviceMap)
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
         service = *inventoryService](
            const boost::system::error_code& ec,
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
                    "xyz.openbmc_project.Memory.MemoryECC", "ECCModeEnabled",
                    std::variant<bool>(eccModeEnabled),
                    nvidia_async_operation_utils::PatchEccModeCallback{resp});

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

            BMCWEB_LOG_DEBUG("CPU:{} set eccModeEnabled property failed: {}",
                             processorId, ec);
            // Read and convert dbus error message to redfish error
            const sd_bus_error* dbusError = msg.get_error();
            if (dbusError == nullptr)
            {
                messages::internalError(resp->res);
                return;
            }

            if (strcmp(dbusError->name, "xyz.openbmc_project.Common."
                                        "Device.Error.WriteFailure") == 0)
            {
                // Service failed to change the config
                messages::operationFailed(resp->res);
            }
            else if (strcmp(dbusError->name,
                            "xyz.openbmc_project.Common.Error.Unavailable") ==
                     0)
            {
                std::string errBusy = "0x50A";
                std::string errBusyResolution =
                    "SMBPBI Command failed with error busy, please try after 60 seconds";

                // busy error
                messages::asyncError(resp->res, errBusy, errBusyResolution);
            }
            else if (strcmp(dbusError->name,
                            "xyz.openbmc_project.Common.Error.Timeout") == 0)
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
            service, cpuObjectPath, "org.freedesktop.DBus.Properties", "Set",
            "xyz.openbmc_project.Memory.MemoryECC", "ECCModeEnabled",
            std::variant<bool>(eccModeEnabled));
    });
}

inline void requestRoutesProcessorSettings(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Settings")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        getProcessorSettingsData(asyncResp, processorId);
    });

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Settings")
        .privileges(redfish::privileges::patchProcessor)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::optional<nlohmann::json> memSummary;
        std::optional<nlohmann::json> oemObject;
        if (!redfish::json_util::readJsonAction(req, asyncResp->res,
                                                "MemorySummary", memSummary,
                                                "Oem", oemObject))
        {
            return;
        }
        std::optional<bool> eccModeEnabled;
        if (memSummary)
        {
            if (redfish::json_util::readJson(*memSummary, asyncResp->res,
                                             "ECCModeEnabled", eccModeEnabled))
            {
                redfish::processor_utils::getProcessorObject(
                    asyncResp, processorId,
                    [eccModeEnabled](
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp1,
                        const std::string& processorId1,
                        const std::string& objectPath,
                        const MapperServiceMap& serviceMap,
                        [[maybe_unused]] const std::string& deviceType) {
                    patchEccMode(asyncResp1, processorId1, *eccModeEnabled,
                                 objectPath, serviceMap);
                });
            }
        }
        if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
        {
            // Update ccMode
            std::optional<nlohmann::json> oemNvidiaObject;

            if (oemObject &&
                redfish::json_util::readJson(*oemObject, asyncResp->res,
                                             "Nvidia", oemNvidiaObject))
            {
                std::optional<bool> ccMode;
                std::optional<bool> ccDevMode;
                std::optional<bool> egmMode;
                if (oemNvidiaObject &&
                    redfish::json_util::readJson(
                        *oemNvidiaObject, asyncResp->res, "CCModeEnabled",
                        ccMode, "CCDevModeEnabled", ccDevMode, "EGMModeEnabled",
                        egmMode))
                {
                    if (ccMode && ccDevMode)
                    {
                        messages::queryCombinationInvalid(asyncResp->res);
                        return;
                    }

                    if (ccMode)
                    {
                        redfish::processor_utils::getProcessorObject(
                            asyncResp, processorId,
                            [ccMode](const std::shared_ptr<bmcweb::AsyncResp>&
                                         asyncResp1,
                                     const std::string& processorId1,
                                     const std::string& objectPath,
                                     const MapperServiceMap& serviceMap,
                                     [[maybe_unused]] const std::string&
                                         deviceType) {
                            redfish::nvidia_processor_utils::patchCCMode(
                                asyncResp1, processorId1, *ccMode, objectPath,
                                serviceMap);
                        });
                    }
                    if (ccDevMode)
                    {
                        redfish::processor_utils::getProcessorObject(
                            asyncResp, processorId,
                            [ccDevMode](
                                const std::shared_ptr<bmcweb::AsyncResp>&
                                    asyncResp1,
                                const std::string& processorId1,
                                const std::string& objectPath,
                                const MapperServiceMap& serviceMap,
                                [[maybe_unused]] const std::string&
                                    deviceType) {
                            redfish::nvidia_processor_utils::patchCCDevMode(
                                asyncResp1, processorId1, *ccDevMode,
                                objectPath, serviceMap);
                        });
                    }
                    if (egmMode)
                    {
                        redfish::processor_utils::getProcessorObject(
                            asyncResp, processorId,
                            [egmMode](const std::shared_ptr<bmcweb::AsyncResp>&
                                          asyncResp1,
                                      const std::string& processorId1,
                                      const std::string& objectPath,
                                      const MapperServiceMap& serviceMap,
                                      [[maybe_unused]] const std::string&
                                          deviceType) {
                            redfish::nvidia_processor_utils::patchEgmMode(
                                asyncResp1, processorId1, *egmMode, objectPath,
                                serviceMap);
                        });
                    }
                }
            }
        }
    });
}

inline void postResetType(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                          const std::string& processorId,
                          const std::string& cpuObjectPath,
                          const std::string& resetType,
                          const MapperServiceMap& serviceMap)
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

        const std::string processorResetType = getProcessorResetType(property);
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

                    BMCWEB_LOG_DEBUG("Performing Post using Async Method Call");

                    nvidia_async_operation_utils::
                        doGenericCallAsyncAndGatherResult<int>(
                            resp, std::chrono::seconds(60), conName,
                            cpuObjectPath, resetAsyncIntf, "Reset",
                            [resp, processorId](
                                const std::string& status,
                                [[maybe_unused]] const int* retValue) {
                        if (status == nvidia_async_operation_utils::
                                          asyncStatusValueSuccess)
                        {
                            BMCWEB_LOG_DEBUG("CPU:{} Reset Succeded",
                                             processorId);
                            messages::success(resp->res);
                            return;
                        }
                        BMCWEB_LOG_ERROR("CPU:{} Reset failed", processorId,
                                         status);
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
                    BMCWEB_LOG_DEBUG("CPU:{} Reset Succeded", processorId);
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

inline void requestRoutesProcessorReset(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Actions/Processor.Reset")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::optional<std::string> resetType;
        if (!json_util::readJsonAction(req, asyncResp->res, "ResetType",
                                       resetType))
        {
            return;
        }
        if (resetType)
        {
            redfish::processor_utils::getProcessorObject(
                asyncResp, processorId,
                [resetType](
                    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp1,
                    const std::string& processorId1,
                    const std::string& objectPath,
                    const MapperServiceMap& serviceMap,
                    [[maybe_unused]] const std::string& deviceType) {
                postResetType(asyncResp1, processorId1, objectPath, *resetType,
                              serviceMap);
            });
        }
    });
}

inline void requestRoutesProcessorPortCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Ports")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        BMCWEB_LOG_DEBUG("Get available system processor resource");
        crow::connections::systemBus->async_method_call(
            [processorId, asyncResp](
                const boost::system::error_code ec,
                const boost::container::flat_map<
                    std::string, boost::container::flat_map<
                                     std::string, std::vector<std::string>>>&
                    subtree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(asyncResp->res);

                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!boost::ends_with(path, processorId))
                {
                    continue;
                }
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/Processors/" + processorId + "/Ports";
                asyncResp->res.jsonValue["@odata.type"] =
                    "#PortCollection.PortCollection";
                asyncResp->res.jsonValue["Name"] = "NVLink Port Collection";

                collection_util::getCollectionMembersByAssociation(
                    asyncResp,
                    "/redfish/v1/Systems/" +
                        std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                        "/Processors/" + processorId + "/Ports",
                    path + "/all_states",
                    {"xyz.openbmc_project.Inventory.Item.Port"});
                return;
            }
            // Object not found
            messages::resourceNotFound(
                asyncResp->res, "#Processor.v1_20_0.Processor", processorId);
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/inventory", 0,
            std::array<const char*, 2>{
                "xyz.openbmc_project.Inventory.Item.Cpu",
                "xyz.openbmc_project.Inventory.Item.Accelerator"});
    });
}

inline void
    getConnectedSwitchPorts(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& portPath,
                            const std::string& fabricId,
                            const std::string& switchName)
{
    BMCWEB_LOG_DEBUG("Get connected switch ports on {}", switchName);
    crow::connections::systemBus->async_method_call(
        [asyncResp, portPath, fabricId,
         switchName](const boost::system::error_code ec,
                     std::variant<std::vector<std::string>>& resp) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("Get connected switch failed on{}", switchName);
            return;
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            return;
        }
        nlohmann::json& switchlinksArray =
            asyncResp->res.jsonValue["Links"]["ConnectedSwitchPorts"];
        for (const std::string& portPath1 : *data)
        {
            sdbusplus::message::object_path objectPath(portPath1);
            std::string portId = objectPath.filename();
            if (portId.empty())
            {
                BMCWEB_LOG_DEBUG("Unable to fetch port");
                messages::internalError(asyncResp->res);
                return;
            }
            nlohmann::json thisPort = nlohmann::json::object();
            std::string portUri = "/redfish/v1/Fabrics/" + fabricId;
            portUri += "/Switches/" + switchName + "/Ports/";
            portUri += portId;
            thisPort["@odata.id"] = portUri;
            switchlinksArray.push_back(std::move(thisPort));
        }
    },
        "xyz.openbmc_project.ObjectMapper", portPath + "/switch_port",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void
    getConnectedSwitches(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& switchPath,
                         const std::string& portPath,
                         const std::string& switchName)
{
    BMCWEB_LOG_DEBUG("Get connected switch on{}", switchName);
    crow::connections::systemBus->async_method_call(
        [asyncResp, switchPath, portPath,
         switchName](const boost::system::error_code ec,
                     std::variant<std::vector<std::string>>& resp) {
        if (ec)
        {
            return;
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            BMCWEB_LOG_DEBUG("Get connected switch failed on: {}", switchName);
            return;
        }
        for (const std::string& fabricPath : *data)
        {
            sdbusplus::message::object_path objectPath(fabricPath);
            std::string fabricId = objectPath.filename();
            if (fabricId.empty())
            {
                messages::internalError(asyncResp->res);
                return;
            }
            nlohmann::json& switchlinksArray =
                asyncResp->res.jsonValue["Links"]["ConnectedSwitches"];
            nlohmann::json thisSwitch = nlohmann::json::object();
            std::string switchUri = "/redfish/v1/Fabrics/";
            switchUri += fabricId;
            switchUri += "/Switches/";
            switchUri += switchName;
            thisSwitch["@odata.id"] = switchUri;
            switchlinksArray.push_back(std::move(thisSwitch));
            getConnectedSwitchPorts(asyncResp, portPath, fabricId, switchName);
        }
    },
        "xyz.openbmc_project.ObjectMapper", switchPath + "/fabrics",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getConnectedProcessorPorts(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& portPath, std::vector<std::string>& portNames)
{
    // This is for when the ports are connected to another processor
    crow::connections::systemBus->async_method_call(
        [asyncResp, portPath,
         portNames](const boost::system::error_code ec,
                    std::variant<std::vector<std::string>>& resp) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("Get connected processor ports failed on: {}",
                             portPath);
            return;
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            return;
        }

        nlohmann::json& connectedPortslinksArray =
            asyncResp->res.jsonValue["Links"]["ConnectedPorts"];

        unsigned int i = 0;

        for (const std::string& processorPath : *data)
        {
            if (!processorPath.empty())
            {
                sdbusplus::message::object_path connectedProcessorPath(
                    processorPath);
                std::string processorName = connectedProcessorPath.filename();
                if (processorName.empty())
                {
                    BMCWEB_LOG_DEBUG(
                        "Get connected processor path failed on: {}", portPath);
                    return;
                }

                nlohmann::json thisProcessorPort = nlohmann::json::object();

                std::string processorPortUri =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/Processors/";
                processorPortUri += processorName;
                processorPortUri += "/Ports/";
                processorPortUri += portNames[i];

                thisProcessorPort["@odata.id"] = processorPortUri;
                // Check if thisProcessorPort is not already in the array
                if (std::find(connectedPortslinksArray.begin(),
                              connectedPortslinksArray.end(),
                              thisProcessorPort) ==
                    connectedPortslinksArray.end())
                {
                    // If not found, push back the value
                    connectedPortslinksArray.push_back(
                        std::move(thisProcessorPort));
                }
                i++;
            }
        }
    },
        "xyz.openbmc_project.ObjectMapper", portPath + "/associated_processor",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void
    getProcessorPortLinks(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& portPath,
                          const std::string& processorId,
                          const std::string& port)
{
    BMCWEB_LOG_DEBUG("Get associated ports on{}", port);

    // This is for when the ports are connected to a switch
    crow::connections::systemBus->async_method_call(
        [asyncResp, portPath, processorId,
         port](const boost::system::error_code ec,
               std::variant<std::vector<std::string>>& resp) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("Get associated switch failed on: {}", port);
            return;
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            return;
        }
        nlohmann::json& switchlinksArray =
            asyncResp->res.jsonValue["Links"]["ConnectedSwitches"];
        switchlinksArray = nlohmann::json::array();
        nlohmann::json& portlinksArray =
            asyncResp->res.jsonValue["Links"]["ConnectedSwitchPorts"];
        portlinksArray = nlohmann::json::array();
        for (const std::string& switchPath : *data)
        {
            sdbusplus::message::object_path objectPath(switchPath);
            std::string switchName = objectPath.filename();
            if (switchName.empty())
            {
                messages::internalError(asyncResp->res);
                return;
            }
            getConnectedSwitches(asyncResp, switchPath, portPath, switchName);
        }
    },
        "xyz.openbmc_project.ObjectMapper", portPath + "/associated_switch",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");

    // This is for when the ports are connected to another processor
    crow::connections::systemBus->async_method_call(
        [asyncResp, portPath, processorId,
         port](const boost::system::error_code ec,
               std::variant<std::vector<std::string>>& resp) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("Get associated processor ports failed on: {}",
                             port);
            return;
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            return;
        }
        nlohmann::json& connectedPortslinksArray =
            asyncResp->res.jsonValue["Links"]["ConnectedPorts"];
        connectedPortslinksArray = nlohmann::json::array();
        std::vector<std::string> portNames;
        for (const std::string& connectedPort : *data)
        {
            sdbusplus::message::object_path connectedPortPath(connectedPort);
            std::string portName = connectedPortPath.filename();
            if (portName.empty())
            {
                messages::internalError(asyncResp->res);
                return;
            }

            portNames.push_back(portName);
        }
        getConnectedProcessorPorts(asyncResp, portPath, portNames);
    },
        "xyz.openbmc_project.ObjectMapper",
        portPath + "/associated_processor_ports",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getProcessorPortData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& processorId, const std::string& portId)
{
    BMCWEB_LOG_DEBUG("Get processor port data");
    crow::connections::systemBus->async_method_call(
        [aResp, processorId,
         portId](const boost::system::error_code& e,
                 std::variant<std::vector<std::string>>& resp) {
        if (e)
        {
            // no state sensors attached.
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

        for (const std::string& sensorpath : *data)
        {
            // Check Interface in Object or not
            BMCWEB_LOG_DEBUG("processor state sensor object path {}",
                             sensorpath);
            crow::connections::systemBus->async_method_call(
                [aResp, sensorpath, processorId,
                 portId](const boost::system::error_code ec,
                         const std::vector<std::pair<
                             std::string, std::vector<std::string>>>& object) {
                if (ec)
                {
                    // the path does not implement port interfaces
                    BMCWEB_LOG_DEBUG("no port interface on object path {}",
                                     sensorpath);
                    return;
                }

                sdbusplus::message::object_path path(sensorpath);
                if (path.filename() != portId || object.size() != 1)
                {
                    return;
                }

                std::string portUri =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/Processors/";
                portUri += processorId;
                portUri += "/Ports/";
                portUri += portId;
                aResp->res.jsonValue["@odata.id"] = portUri;
                aResp->res.jsonValue["@odata.type"] = "#Port.v1_4_0.Port";
                std::string portName = processorId + " ";
                portName += portId + " Port";
                aResp->res.jsonValue["Name"] = portName;
                aResp->res.jsonValue["Id"] = portId;

                redfish::port_utils::getCpuPortData(aResp, object.front().first,
                                                    sensorpath);
                getProcessorPortLinks(aResp, sensorpath, processorId, portId);
            },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetObject", sensorpath,
                std::array<std::string, 1>(
                    {"xyz.openbmc_project.Inventory.Item.Port"}));
        }
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_states",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void getProcessorAcceleratorPortData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& objPath,
    const std::string& processorId, const std::string& portId)
{
    BMCWEB_LOG_DEBUG("Get processor port data");
    crow::connections::systemBus->async_method_call(
        [aResp, objPath, processorId,
         portId](const boost::system::error_code e,
                 std::variant<std::vector<std::string>>& resp) {
        if (e)
        {
            // no state sensors attached.
            BMCWEB_LOG_ERROR("DBUS response error");
            messages::internalError(aResp->res);
            return;
        }

        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            BMCWEB_LOG_ERROR("No response error while getting ports");
            messages::internalError(aResp->res);
            return;
        }

        for (const std::string& sensorpath : *data)
        {
            // Check Interface in Object or not
            BMCWEB_LOG_DEBUG("processor state sensor object path {}",
                             sensorpath);
            sdbusplus::message::object_path path(sensorpath);
            if (path.filename() != portId)
            {
                continue;
            }

            crow::connections::systemBus->async_method_call(
                [aResp, sensorpath, processorId, portId](
                    const boost::system::error_code ec,
                    const boost::container::flat_map<
                        std::string,
                        boost::container::flat_map<
                            std::string, std::vector<std::string>>>& subtree1) {
                if (ec)
                {
                    // the path does not implement port interfaces
                    BMCWEB_LOG_DEBUG("no port interface on object path {}",
                                     sensorpath);
                    return;
                }

                for (const auto& [portPath, object1] : subtree1)
                {
                    sdbusplus::message::object_path pPath(portPath);
                    if (pPath.filename() != portId)
                    {
                        continue;
                    }

                    std::string portUri =
                        "/redfish/v1/Systems/" +
                        std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                        "/Processors/";
                    portUri += processorId;
                    portUri += "/Ports/";
                    portUri += portId;
                    aResp->res.jsonValue["@odata.id"] = portUri;
                    aResp->res.jsonValue["@odata.type"] = "#Port.v1_4_0.Port";
                    aResp->res.jsonValue["Name"] = portId + " Resource";
                    aResp->res.jsonValue["Id"] = portId;
                    std::string metricsURI = portUri + "/Metrics";
                    aResp->res.jsonValue["Metrics"]["@odata.id"] = metricsURI;

                    std::string portSettingURI = portUri + "/Settings";
                    aResp->res.jsonValue["@Redfish.Settings"]["@odata.type"] =
                        "#Settings.v1_3_3.Settings";
                    aResp->res.jsonValue["@Redfish.Settings"]["SettingsObject"]
                                        ["@odata.id"] = portSettingURI;
#ifndef BMCWEB_DISABLE_CONDITIONS_ARRAY
                    aResp->res.jsonValue["Status"]["Conditions"] =
                        nlohmann::json::array();
#endif // BMCWEB_DISABLE_CONDITIONS_ARRAY
                    for (const auto& [service, interfaces] : object1)
                    {
                        redfish::port_utils::getPortData(aResp, service,
                                                         sensorpath);
                        getProcessorPortLinks(aResp, sensorpath, processorId,
                                              portId);
                    }
                    return;
                }
            },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree", objPath, 0,
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Inventory.Item.Port"});
        }
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/all_states",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void requestRoutesProcessorPort(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Ports/<str>")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId, const std::string& port) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        BMCWEB_LOG_DEBUG("Get available system processor resource");
        crow::connections::systemBus->async_method_call(
            [processorId, port, asyncResp](
                const boost::system::error_code ec,
                const boost::container::flat_map<
                    std::string, boost::container::flat_map<
                                     std::string, std::vector<std::string>>>&
                    subtree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(asyncResp->res);

                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!boost::ends_with(path, processorId))
                {
                    continue;
                }
                for (const auto& [serviceName, interfacesList] : object)
                {
                    if (std::find(interfacesList.begin(), interfacesList.end(),
                                  "xyz.openbmc_project.Inventory.Item.Cpu") !=
                        interfacesList.end())
                    {
                        getProcessorPortData(asyncResp, path, processorId,
                                             port);
                    }
                    else if (
                        std::find(
                            interfacesList.begin(), interfacesList.end(),
                            "xyz.openbmc_project.Inventory.Item.Accelerator") !=
                        interfacesList.end())
                    {
                        getProcessorAcceleratorPortData(asyncResp, path,
                                                        processorId, port);
                    }
                }
                return;
            }
            // Object not found
            messages::resourceNotFound(
                asyncResp->res, "#Processor.v1_20_0.Processor", processorId);
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/inventory", 0,
            std::array<const char*, 2>{
                "xyz.openbmc_project.Inventory.Item.Cpu",
                "xyz.openbmc_project.Inventory.Item.Accelerator"});
    });
}

inline void getProcessorPortMetricsData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, service,
         path](const boost::system::error_code ec,
               // const boost::container::flat_map<std::string,
               // std::variant<size_t,uint16_t,uint32_t,uint64_t>>&
               const boost::container::flat_map<
                   std::string, dbus::utility::DbusVariantType>& properties) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error");
            messages::internalError(asyncResp->res);
            return;
        }
        for (const auto& property : properties)
        {
            if ((property.first == "TXBytes") || (property.first == "RXBytes"))
            {
                const size_t* value = std::get_if<size_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for TX/RX bytes");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue[property.first] = *value;
            }
            else if (property.first == "RXErrors")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for receive error");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["RXErrors"] = *value;
            }
            else if (property.first == "RXPkts")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for receive packets");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["Networking"]["RXFrames"] = *value;
            }
            else if (property.first == "TXPkts")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for transmit packets");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["Networking"]["TXFrames"] = *value;
            }
            else if (property.first == "TXDiscardPkts")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for transmit discard packets");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["Networking"]["TXDiscards"] = *value;
            }
            if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
            {
                if (property.first == "MalformedPkts")
                {
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for malformed packets");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                            ["MalformedPackets"] = *value;
                }
                else if (property.first == "VL15DroppedPkts")
                {
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for VL15 dropped packets");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["VL15Dropped"] =
                        *value;
                }
                else if (property.first == "VL15TXPkts")
                {
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for VL15 dropped packets");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["VL15TXPackets"] =
                        *value;
                }
                else if (property.first == "VL15TXData")
                {
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for VL15 dropped packets");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["VL15TXBytes"] =
                        *value;
                }
                else if (property.first == "LinkErrorRecoveryCounter")
                {
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for link error recovery count");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                            ["LinkErrorRecoveryCount"] = *value;
                }
                else if (property.first == "LinkDownCount")
                {
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for link down count");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res
                        .jsonValue["Oem"]["Nvidia"]["LinkDownedCount"] = *value;
                }
                else if (property.first == "TXWait")
                {
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for transmit wait");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["TXWait"] =
                        *value;
                }
                else if (property.first == "EffectiveError")
                {
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for effective error count");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res
                        .jsonValue["Oem"]["Nvidia"]["EffectiveError"] = *value;
                }
                else if (property.first == "RXNoProtocolBytes")
                {
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for RXNoProtocolBytes");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                        "#NvidiaPortMetrics.v1_4_0.NvidiaNVLinkPortMetrics";
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                            ["RXNoProtocolBytes"] = *value;
                }
                else if (property.first == "TXNoProtocolBytes")
                {
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for TXNoProtocolBytes");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                            ["TXNoProtocolBytes"] = *value;
                }
                else if (property.first == "BitErrorRate")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for bit error rate");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["BitErrorRate"] =
                        *value;
                }
                else if (property.first == "DataCRCCount")
                {
                    const uint32_t* value =
                        std::get_if<uint32_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for DataCRCCount");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]
                                            ["DataCRCCount"] = *value;
                }
                else if (property.first == "FlitCRCCount")
                {
                    const uint32_t* value =
                        std::get_if<uint32_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for FlitCRCCount");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]
                                            ["FlitCRCCount"] = *value;
                }
                else if (property.first == "RecoveryCount")
                {
                    const uint32_t* value =
                        std::get_if<uint32_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for RecoveryCount");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]
                                            ["RecoveryCount"] = *value;
                }
                else if (property.first == "ReplayErrorsCount")
                {
                    const uint32_t* value =
                        std::get_if<uint32_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for ReplayCount");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]
                                            ["ReplayCount"] = *value;
                }
                else if (property.first == "RuntimeError")
                {
                    const uint16_t* value =
                        std::get_if<uint16_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Null value returned for RuntimeError");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if (*value != 0)
                    {
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]
                                      ["RuntimeError"] = true;
                    }
                    else
                    {
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]
                                      ["RuntimeError"] = false;
                    }
                }
                else if (property.first == "TrainingError")
                {
                    const uint16_t* value =
                        std::get_if<uint16_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Null value returned for TrainingError");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    if (*value != 0)
                    {
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]
                                      ["TrainingError"] = true;
                    }
                    else
                    {
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["NVLinkErrors"]
                                      ["TrainingError"] = false;
                    }
                }
                else if (property.first == "NVLinkDataRxBandwidthGbps")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for NVLinkDataRxBandwidthGbps");
                    }
                    else
                    {
                        asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                                ["NVLinkDataRxBandwidthGbps"] =
                            *value;
                    }
                }
                else if (property.first == "NVLinkDataTxBandwidthGbps")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for NVLinkDataTxBandwidthGbps");
                    }
                    else
                    {
                        asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                                ["NVLinkDataTxBandwidthGbps"] =
                            *value;
                    }
                }
                else if (property.first == "NVLinkRawRxBandwidthGbps")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for NVLinkRawRxBandwidthGbps");
                    }
                    else
                    {
                        asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                                ["NVLinkRawRxBandwidthGbps"] =
                            *value;
                    }
                }
                else if (property.first == "NVLinkRawTxBandwidthGbps")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for NVLinkRawTxBandwidthGbps");
                    }
                    else
                    {
                        asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                                ["NVLinkRawTxBandwidthGbps"] =
                            *value;
                    }
                }
                else if (property.first == "RXErrorsPerLane")
                {
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                        "#NvidiaPortMetrics.v1_4_0.NvidiaPortMetrics";

                    const std::vector<uint32_t>* value =
                        std::get_if<std::vector<uint32_t>>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_DEBUG("Null value returned "
                                         "for RXErrorsPerLane");
                    }
                    else
                    {
                        asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                                ["RXErrorsPerLane"] = *value;
                    }
                }
            }
            if (property.first == "ceCount")
            {
                const double* value = std::get_if<double>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res
                    .jsonValue["PCIeErrors"]["CorrectableErrorCount"] =
                    nvidia::nsm_utils::tryConvertToInt64(*value);
            }
            else if (property.first == "nonfeCount")
            {
                const double* value = std::get_if<double>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["PCIeErrors"]["NonFatalErrorCount"] =
                    nvidia::nsm_utils::tryConvertToInt64(*value);
            }
            else if (property.first == "feCount")
            {
                const double* value = std::get_if<double>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["PCIeErrors"]["FatalErrorCount"] =
                    nvidia::nsm_utils::tryConvertToInt64(*value);
            }
            else if (property.first == "L0ToRecoveryCount")
            {
                const double* value = std::get_if<double>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["PCIeErrors"]["L0ToRecoveryCount"] =
                    nvidia::nsm_utils::tryConvertToInt64(*value);
            }
            else if (property.first == "ReplayCount")
            {
                const double* value = std::get_if<double>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["PCIeErrors"]["ReplayCount"] =
                    nvidia::nsm_utils::tryConvertToInt64(*value);
            }
            else if (property.first == "ReplayRolloverCount")
            {
                const double* value = std::get_if<double>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["PCIeErrors"]["ReplayRolloverCount"] =
                    nvidia::nsm_utils::tryConvertToInt64(*value);
            }
            else if (property.first == "NAKSentCount")
            {
                const double* value = std::get_if<double>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["PCIeErrors"]["NAKSentCount"] =
                    nvidia::nsm_utils::tryConvertToInt64(*value);
            }
            else if (property.first == "NAKReceivedCount")
            {
                const double* value = std::get_if<double>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["PCIeErrors"]["NAKReceivedCount"] =
                    nvidia::nsm_utils::tryConvertToInt64(*value);
            }
            else if (property.first == "UnsupportedRequestCount")
            {
                const double* value = std::get_if<double>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Invalid Data Type");
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res
                    .jsonValue["PCIeErrors"]["UnsupportedRequestCount"] =
                    nvidia::nsm_utils::tryConvertToInt64(*value);
            }
        }
    },
        service, path, "org.freedesktop.DBus.Properties", "GetAll", "");
}

inline void requestRoutesProcessorPortMetrics(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Ports/<str>/Metrics")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId, const std::string& portId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        BMCWEB_LOG_DEBUG("Get available system processor resource");
        crow::connections::systemBus->async_method_call(
            [processorId, portId, asyncResp](
                const boost::system::error_code ec,
                const boost::container::flat_map<
                    std::string, boost::container::flat_map<
                                     std::string, std::vector<std::string>>>&
                    subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(asyncResp->res);

                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!boost::ends_with(path, processorId))
                {
                    continue;
                }
                crow::connections::systemBus->async_method_call(
                    [asyncResp, processorId,
                     portId](const boost::system::error_code& e,
                             std::variant<std::vector<std::string>>& resp) {
                    if (e)
                    {
                        // no state sensors attached.
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    std::vector<std::string>* data =
                        std::get_if<std::vector<std::string>>(&resp);
                    if (data == nullptr)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (const std::string& sensorpath : *data)
                    {
                        // Check Interface in Object or not
                        BMCWEB_LOG_DEBUG(
                            "processor state sensor object path {}",
                            sensorpath);
                        crow::connections::systemBus->async_method_call(
                            [asyncResp, sensorpath, processorId, portId](
                                const boost::system::error_code ec,
                                const std::vector<std::pair<
                                    std::string, std::vector<std::string>>>&
                                    object) {
                            if (ec)
                            {
                                // the path does not implement port
                                // interfaces
                                BMCWEB_LOG_DEBUG(
                                    "no port interface on object path {}",
                                    sensorpath);
                                return;
                            }

                            sdbusplus::message::object_path path(sensorpath);
                            if (path.filename() != portId)
                            {
                                return;
                            }

                            std::string portMetricUri =
                                "/redfish/v1/Systems/" +
                                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                "/Processors/";
                            portMetricUri += processorId;
                            portMetricUri += "/Ports/";
                            portMetricUri += portId + "/Metrics";
                            asyncResp->res.jsonValue["@odata.id"] =
                                portMetricUri;
                            asyncResp->res.jsonValue["@odata.type"] =
                                "#PortMetrics.v1_3_0.PortMetrics";
                            asyncResp->res.jsonValue["Name"] = portId +
                                                               " Port Metrics";
                            asyncResp->res.jsonValue["Id"] = "Metrics";

                            for (const auto& [service, interfaces] : object)
                            {
                                getProcessorPortMetricsData(asyncResp, service,
                                                            sensorpath);
                                if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                                {
                                    if (std::find(
                                            interfaces.begin(),
                                            interfaces.end(),
                                            "xyz.openbmc_project.PCIe.ClearPCIeCounters") !=
                                        interfaces.end())
                                    {
                                        asyncResp->res.jsonValue
                                            ["Actions"]["Oem"]
                                            ["#NvidiaPortMetrics.ClearPCIeCounters"]
                                            ["target"] =
                                            portMetricUri +
                                            "/Actions/Oem/NvidiaPortMetrics.ClearPCIeCounters";
                                        asyncResp->res.jsonValue
                                            ["Actions"]["Oem"]
                                            ["#NvidiaPortMetrics.ClearPCIeCounters"]
                                            ["@Redfish.ActionInfo"] =
                                            portMetricUri +
                                            "/Oem/Nvidia/ClearPCIeCountersActionInfo";
                                    }
                                }
                            }
                        },
                            "xyz.openbmc_project.ObjectMapper",
                            "/xyz/openbmc_project/object_mapper",
                            "xyz.openbmc_project.ObjectMapper", "GetObject",
                            sensorpath,
                            std::array<std::string, 1>(
                                {"xyz.openbmc_project.Inventory.Item.Port"}));
                    }
                },
                    "xyz.openbmc_project.ObjectMapper", path + "/all_states",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
                return;
            }
            // Object not found
            messages::resourceNotFound(
                asyncResp->res, "#Processor.v1_20_0.Processor", processorId);
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/inventory", 0,
            std::array<const char*, 2>{
                "xyz.openbmc_project.Inventory.Item.Cpu",
                "xyz.openbmc_project.Inventory.Item.Accelerator"});
    });
}

inline void requestRoutesClearPCIeCountersActionInfo(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/Processors/<str>/"
                 "Ports/<str>/Metrics/Oem/Nvidia/ClearPCIeCountersActionInfo")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemId,
                   const std::string& processorId, const std::string& portId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        redfish::nvidia_processor_utils::getClearPCIeCountersActionInfo(
            asyncResp, processorId, portId);
    });
}

inline void requestRoutesPCIeClearCounter(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Processors/<str>/"
        "Ports/<str>/Metrics/Actions/Oem/NvidiaPortMetrics.ClearPCIeCounters/")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemId,
                   const std::string& processorId, const std::string& portId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::optional<std::string> counterType;
        if (!json_util::readJsonAction(req, asyncResp->res, "CounterType",
                                       counterType))
        {
            return;
        }

        redfish::nvidia_processor_utils::postPCIeClearCounter(
            asyncResp, processorId, portId, *counterType);
    });
}

inline void requestRoutesProcessorPortSettings(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Ports/<str>/Settings")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemId,
                   const std::string& processorId, const std::string& portId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        BMCWEB_LOG_DEBUG("Setting for {} Processor and {} PortID.", processorId,
                         portId);

        std::string processorPortSettingsURI =
            std::format("/redfish/v1/Systems/{}/Processors/",
                        BMCWEB_REDFISH_SYSTEM_URI_NAME);
        processorPortSettingsURI += processorId;
        processorPortSettingsURI += "/Ports/";
        processorPortSettingsURI += portId;
        processorPortSettingsURI += "/Settings";

        asyncResp->res.jsonValue["@odata.type"] = "#Port.v1_4_0.Port";
        asyncResp->res.jsonValue["@odata.id"] = processorPortSettingsURI;
        asyncResp->res.jsonValue["Id"] = "Settings";
        asyncResp->res.jsonValue["Name"] = processorId + " " + portId +
                                           " Pending Settings";

        if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
        {
            redfish::processor_utils::getProcessorObject(
                asyncResp, processorId,
                [portId](const std::shared_ptr<bmcweb::AsyncResp>& asyncResp1,
                         const std::string& processorId1,
                         const std::string& objectPath,
                         const MapperServiceMap& serviceMap,
                         [[maybe_unused]] const std::string& deviceType) {
                getPortDisableFutureStatus(asyncResp1, processorId1, objectPath,
                                           serviceMap, portId);
            });
        }
    });

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Ports/<str>/Settings")
        .privileges(redfish::privileges::patchProcessor)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemId,
                   const std::string& processorId, const std::string& portId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        BMCWEB_LOG_DEBUG("Setting for {} Processor and {} PortID.", processorId,
                         portId);
        std::optional<std::string> linkState;
        if (!redfish::json_util::readJsonAction(req, asyncResp->res,
                                                "LinkState", linkState))
        {
            return;
        }

        if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
        {
            if (linkState)
            {
                redfish::processor_utils::getProcessorObject(
                    asyncResp, processorId,
                    [linkState, portId](
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp1,
                        const std::string& processorId1,
                        const std::string& objectPath,
                        const MapperServiceMap& serviceMap,
                        [[maybe_unused]] const std::string& deviceType) {
                    redfish::nvidia_processor_utils::patchPortDisableFuture(
                        asyncResp1, processorId1, portId, *linkState,
                        "PortDisableFuture", objectPath, serviceMap);
                });
            }
        }
    });
}

} // namespace redfish
