/*
Copyright (c) 2018 Intel Corporation

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#pragma once
#include "app.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/processor.hpp"
#include "health.hpp"
#include "nvidia_processor.hpp"
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
    asyncResp->res.jsonValue["Status"]["State"] = resource::State::Enabled;
    asyncResp->res.jsonValue["Status"]["Health"] = resource::Health::OK;

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
                    asyncResp->res.jsonValue["Status"]["State"] =
                        resource::State::Absent;
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
                    asyncResp->res.jsonValue["Status"]["Health"] =
                        resource::Health::Critical;
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

inline void getCpuDataByService(
    std::shared_ptr<bmcweb::AsyncResp> asyncResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath)
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
            asyncResp->res.jsonValue["ProcessorType"] =
                processor::ProcessorType::CPU;

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
                        if (interface.first ==
                            "xyz.openbmc_project.Inventory.Item")
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

inline void getThrottleProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objectPath)
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
                dbus_utils::UnpackErrorPrinter(), properties, "Version",
                version);

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

inline void getAcceleratorDataByService(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& acclrtrId,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get available system Accelerator resources by service.");

    if constexpr (BMCWEB_HEALTH_ROLLUP_ALTERNATIVE)
    {
        std::shared_ptr<HealthRollup> health = std::make_shared<HealthRollup>(
            objPath, [aResp](const std::string& rootHealth,
                             const std::string& healthRollup) {
                aResp->res.jsonValue["Status"]["Health"] = rootHealth;
                if constexpr (!BMCWEB_DISABLE_HEALTH_ROLLUP)
                {
                    aResp->res.jsonValue["Status"]["HealthRollup"] =
                        healthRollup;
                }
            });
        health->start();
    }

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

            // Nvidia added accType and operationalState
            const std::string* accType = nullptr;
            const std::string* operationalState = nullptr;

            // Nvidia added "Type" and "State" fields
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
            std::string health = "";
            if constexpr (!BMCWEB_HEALTH_ROLLUP_ALTERNATIVE)
            {
                health = "OK";
            }

            if (present != nullptr && !*present)
            {
                state = "Absent";
            }
            if constexpr (!BMCWEB_HEALTH_ROLLUP_ALTERNATIVE)
            {
                if (functional != nullptr && !*functional)
                {
                    if (state == "Enabled")
                    {
                        health = "Critical";
                    }
                }
            }
            else
            {
                (void)functional;
            }

            aResp->res.jsonValue["Id"] = acclrtrId;
            aResp->res.jsonValue["Name"] = "Processor";
            aResp->res.jsonValue["Status"]["State"] = state;
            if constexpr (!BMCWEB_HEALTH_ROLLUP_ALTERNATIVE)
            {
                aResp->res.jsonValue["Status"]["Health"] = health;
            }

            // Nvidia Added Code Block: Handling for Health, ProcessorType
            // ,State

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

                health_utils::getDeviceHealthInfo(aResp->res, acclrtrId);

                if (accType != nullptr && !accType->empty())
                {
                    aResp->res.jsonValue["ProcessorType"] =
                        processor::ProcessorType::Accelerator;
                }
                if (operationalState != nullptr && !operationalState->empty())
                {
                    aResp->res.jsonValue["Status"]["State"] =
                        redfish::chassis_utils::getPowerStateType(
                            *operationalState);
                }
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
inline void getCpuConfigData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath)
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
                json["AppliedOperatingConfig"] =
                    std::move(appliedOperatingConfig);

                // Once we found the current applied config, queue another
                // request to read the base freq core ids out of that
                // config.
                sdbusplus::asio::getProperty<BaseSpeedPrioritySettingsProperty>(
                    *crow::connections::systemBus, service, dbusPath,
                    "xyz.openbmc_project.Inventory.Item.Cpu."
                    "OperatingConfig",
                    "BaseSpeedPrioritySettings",
                    [aResp](const boost::system::error_code& ec2,
                            const BaseSpeedPrioritySettingsProperty&
                                baseSpeedList) {
                        if (ec2)
                        {
                            BMCWEB_LOG_WARNING("D-Bus Property Get error: {}",
                                               ec2);
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
        [objPath, asyncResp{std::move(aResp)}](
            const boost::system::error_code& ec, const std::string& property) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(asyncResp->res);
                return;
            }

            asyncResp->res
                .jsonValue["Location"]["PartLocation"]["ServiceLabel"] =
                property;
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
            aResp->res
                .jsonValue["ProcessorId"]["ProtectedIdentificationNumber"] = id;
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
                dbus_utils::UnpackErrorPrinter(), properties,
                "AvailableCoreCount", availableCoreCount, "BaseSpeed",
                baseSpeed, "DefaultBoostClockSpeedMHz",
                defaultBoostClockSpeedMHz, "MaxJunctionTemperature",
                maxJunctionTemperature, "MaxSpeed", maxSpeed, "PowerLimit",
                powerLimit, "TurboProfile", turboProfile,
                "BaseSpeedPrioritySettings", baseSpeedPrioritySettings,
                "MinSpeed", minSpeed, "OperatingSpeed", operatingSpeed,
                "SpeedConfig", speedConfig);

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
                nlohmann::json& baseSpeedArray =
                    json["BaseSpeedPrioritySettings"];
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

inline void getProcessorMigModeData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG(" get GpuMIGMode data");
    redfish::nvidia_processor::getMigModeData(aResp, cpuId, service, objPath);
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

inline void getProcessorCCModeData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& cpuId,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG(" get GpuCCMode data");
    redfish::nvidia_processor_utils::getCCModeData(aResp, cpuId, service,
                                                   objPath);
}

inline void getPowerSmoothingInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, std::string processorId,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG(" get getPowerSmoothingInfo data");
    redfish::nvidia_processor_utils::getPowerSmoothingInfo(
        aResp, processorId, service, objPath);
}

inline void getResetMetricsInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, std::string processorId,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG(" get getResetMetricsInfo data");
    redfish::nvidia_processor_utils::getResetMetricsInfo(aResp, processorId,
                                                         service, objPath);
}

inline void getWorkLoadPowerInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, std::string processorId)
{
    BMCWEB_LOG_DEBUG(" get getWorkLoadPowerInfo data");
    redfish::nvidia_processor_utils::getWorkLoadPowerInfo(aResp, processorId);
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
                    redfish::nvidia_processor::getRemoteDebugState(
                        aResp, serviceName, objectPath);
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
                redfish::nvidia_processor::getProcessorLocationContext(
                    aResp, serviceName, objectPath);
            }
            else if (interface == "xyz.openbmc_project.Inventory."
                                  "Decorator.Location")
            {
                redfish::nvidia_processor::getCpuLocationType(
                    aResp, serviceName, objectPath);
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
                redfish::nvidia_processor::getProcessorMemoryData(
                    aResp, processorId, serviceName, objectPath);
            }
            else if (interface == "xyz.openbmc_project.Memory.MemoryECC")
            {
                redfish::nvidia_processor::getProcessorEccModeData(
                    aResp, processorId, serviceName, objectPath);
            }
            else if (interface == "xyz.openbmc_project.Inventory."
                                  "Decorator.FpgaType")
            {
                redfish::nvidia_processor::getFpgaTypeData(aResp, serviceName,
                                                           objectPath);
            }
            else if (interface == "xyz.openbmc_project.Control.Processor.Reset")
            {
                redfish::nvidia_processor::getProcessorResetTypeData(
                    aResp, processorId, serviceName, objectPath);
            }
            else if (interface ==
                     "xyz.openbmc_project.Inventory.Decorator.Replaceable")
            {
                redfish::nvidia_processor::getProcessorReplaceable(
                    aResp, serviceName, objectPath);
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
        {"@odata.id",
         "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
             "/Processors/" + processorId + "/Settings"}};

    aResp->res.jsonValue["Ports"] = {
        {"@odata.id",
         "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
             "/Processors/" + processorId + "/Ports"}};
    // Links association to underneath memory
    redfish::nvidia_processor::getProcessorMemoryLinks(aResp, objectPath);
    // Link association to parent chassis
    for (const auto& [serviceName, interfaceList] : serviceMap)
    {
        redfish::nvidia_processor::getProcessorChassisLink(aResp, objectPath,
                                                           serviceName);
    }
    // Get system and fpga interfaces properties
    redfish::nvidia_processor::getProcessorSystemPCIeInterface(
        aResp, objectPath);
    redfish::nvidia_processor::getProcessorFPGAPCIeInterface(aResp, objectPath);

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        nvidia_processor_utils::getInbandReconfigPermissionsData(
            aResp, processorId, objectPath);
        nvidia_processor_utils::populateErrorInjectionData(aResp, processorId);
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

    // Generate the D-Bus path of the OperatingConfig object, by assuming
    // it's a direct child of the CPU object. Strip the expectedPrefix from
    // the config URI to get the "filename", and append to the CPU's path.
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

inline void handleProcessorHead(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& /* systemName */, const std::string& /* processorId */)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/Processor/Processor.json>; rel=describedby");
}

inline void handleProcessorCollectionHead(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& /* systemName */)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/ProcessorCollection/ProcessorCollection.json>; rel=describedby");
}

inline void requestRoutesOperatingConfigCollection(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/Processors/<str>/OperatingConfigs/")
        .privileges(redfish::privileges::getOperatingConfigCollection)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            [[maybe_unused]] const std::string& systemName,
                            const std::string& cpuName) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
            {
                // Option currently returns no systems.  TBD
                messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                           systemName);
                return;
            }

            if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
            {
                messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                           systemName);
                return;
            }
            asyncResp->res.jsonValue["@odata.type"] =
                "#OperatingConfigCollection.OperatingConfigCollection";
            asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
                "/redfish/v1/Systems/{}/Processors/{}/OperatingConfigs",
                BMCWEB_REDFISH_SYSTEM_URI_NAME, cpuName);
            asyncResp->res.jsonValue["Name"] = "Operating Config Collection";

            // First find the matching CPU object so we know how to
            // constrain our search for related Config objects.
            crow::connections::systemBus->async_method_call(
                [asyncResp,
                 cpuName](const boost::system::error_code ec,
                          const dbus::utility::MapperGetSubTreePathsResponse&
                              objects) {
                    if (ec)
                    {
                        BMCWEB_LOG_WARNING("D-Bus error: {}, {}", ec,
                                           ec.message());
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
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            [[maybe_unused]] const std::string& systemName,
                            const std::string& cpuName,
                            const std::string& configName) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
            {
                // Option currently returns no systems.  TBD
                messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                           systemName);
                return;
            }

            if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
            {
                messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                           systemName);
                return;
            }

            // Ask for all objects implementing OperatingConfig so we can
            // search for one with a matching name
            crow::connections::systemBus->async_method_call(
                [asyncResp, cpuName, configName, reqUrl{req.url()}](
                    boost::system::error_code ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
                    if (ec)
                    {
                        BMCWEB_LOG_WARNING("D-Bus error: {}, {}", ec,
                                           ec.message());
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    const std::string expectedEnding =
                        cpuName + '/' + configName;
                    for (const auto& [objectPath, serviceMap] : subtree)
                    {
                        // Ignore any configs without matching cpuX/configY
                        if (!objectPath.ends_with(expectedEnding) ||
                            serviceMap.empty())
                        {
                            continue;
                        }

                        nlohmann::json& json = asyncResp->res.jsonValue;
                        json["@odata.type"] =
                            "#OperatingConfig.v1_0_0.OperatingConfig";
                        json["@odata.id"] = reqUrl;
                        json["Name"] = "Processor Profile";
                        json["Id"] = configName;

                        std::string deviceType =
                            "xyz.openbmc_project.Inventory.Item.Cpu";
                        // Just use the first implementation of the object -
                        // not expected that there would be multiple
                        // matching services
                        getOperatingConfigData(asyncResp,
                                               serviceMap.begin()->first,
                                               objectPath, deviceType);
                        return;
                    }
                    messages::resourceNotFound(asyncResp->res,
                                               "OperatingConfig", configName);
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
        .privileges(redfish::privileges::headProcessorCollection)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleProcessorCollectionHead, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/")
        .privileges(redfish::privileges::getProcessorCollection)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& systemName) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
            {
                // Option currently returns no systems.  TBD
                messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                           systemName);
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
                std::format("/redfish/v1/Systems/{}/Processors",
                            BMCWEB_REDFISH_SYSTEM_URI_NAME);

            asyncResp->res.jsonValue["Members"] = nlohmann::json::array();

            collection_util::getCollectionMembers(
                asyncResp,
                boost::urls::format("/redfish/v1/Systems/{}/Processors",
                                    BMCWEB_REDFISH_SYSTEM_URI_NAME),
                processorInterfaces, "/xyz/openbmc_project/inventory");
        });
}

inline void requestRoutesProcessor(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/")
        .privileges(redfish::privileges::headProcessor)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleProcessorHead, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/")
        .privileges(redfish::privileges::getProcessor)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& systemName,
                            const std::string& processorId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
            {
                // Option currently returns no systems.  TBD
                messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                           systemName);
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
            asyncResp->res.jsonValue["Metrics"]["@odata.id"] =
                processorMetricsURI;

            redfish::processor_utils::getProcessorObject(asyncResp, processorId,
                                                         getProcessorData);
            if constexpr (!BMCWEB_DISABLE_CONDITIONS_ARRAY)
            {
                redfish::conditions_utils::populateServiceConditions(
                    asyncResp, processorId);
            }
        });

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/")
        .privileges(redfish::privileges::patchProcessor)
        .methods(
            boost::beast::http::verb::
                patch)([&app](
                           const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& systemName,
                           const std::string& processorId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
            {
                // Option currently returns no systems.  TBD
                messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                           systemName);
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
                    req, asyncResp->res, "SpeedLimitMHz", speedLimit,
                    "SpeedLocked", speedLocked,
                    "AppliedOperatingConfig/@odata.id", appliedConfigUri, "Oem",
                    oemObject, "OperatingSpeedRangeMHz",
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
                        redfish::nvidia_processor::patchSpeedConfig(
                            asyncResp1, processorId1, reqSpeedConfig,
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
                                patchOperatingSpeedRangeMHz(
                                    asyncResp, processorId, *settingMax,
                                    "SettingMax", objectPath);
                        }
                        else if (settingMin)
                        {
                            redfish::nvidia_processor_utils::
                                patchOperatingSpeedRangeMHz(
                                    asyncResp, processorId, *settingMin,
                                    "SettingMin", objectPath);
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

                    if (oemNvidiaObject &&
                        redfish::json_util::readJson(
                            *oemNvidiaObject, asyncResp->res, "MIGModeEnabled",
                            migMode, "RemoteDebugEnabled", remoteDebugEnabled,
                            "InbandReconfigPermissions",
                            inbandReconfigPermissions))
                    {
                        if (migMode)
                        {
                            redfish::processor_utils::getProcessorObject(
                                asyncResp, processorId,
                                [migMode](
                                    const std::shared_ptr<bmcweb::AsyncResp>&
                                        asyncResp1,
                                    const std::string& processorId1,
                                    const std::string& objectPath,
                                    const MapperServiceMap& serviceMap,
                                    [[maybe_unused]] const std::string&
                                        deviceType) {
                                    redfish::nvidia_processor::patchMigMode(
                                        asyncResp1, processorId1, *migMode,
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
                                    redfish::nvidia_processor::patchRemoteDebug(
                                        asyncResp, processorId,
                                        *remoteDebugEnabled, objectPath);
                                });
                        }

                        if (inbandReconfigPermissions)
                        {
                            nvidia_processor_utils::
                                patchInbandReconfigPermissions(
                                    asyncResp, processorId,
                                    *inbandReconfigPermissions);
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
                                                    *appliedConfigUri,
                                                    objectPath, serviceMap);
                    });
            }
        });
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
                redfish::nvidia_processor::getProcessorMetricsData(asyncResp,
                                                                   processorId);
            });
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
                redfish::nvidia_processor::getProcessorMemoryMetricsData(
                    asyncResp, processorId);
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
                redfish::nvidia_processor::getProcessorSettingsData(
                    asyncResp, processorId);
            });

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Settings")
        .privileges(redfish::privileges::patchProcessor)
        .methods(
            boost::beast::http::verb::
                patch)([&app](
                           const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           [[maybe_unused]] const std::string& systemName,
                           const std::string& processorId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            std::optional<nlohmann::json> memSummary;
            std::optional<nlohmann::json> oemObject;
            if (!redfish::json_util::readJsonAction(
                    req, asyncResp->res, "MemorySummary", memSummary, "Oem",
                    oemObject))
            {
                return;
            }
            std::optional<bool> eccModeEnabled;
            if (memSummary)
            {
                if (redfish::json_util::readJson(*memSummary, asyncResp->res,
                                                 "ECCModeEnabled",
                                                 eccModeEnabled))
                {
                    redfish::processor_utils::getProcessorObject(
                        asyncResp, processorId,
                        [eccModeEnabled](
                            const std::shared_ptr<bmcweb::AsyncResp>&
                                asyncResp1,
                            const std::string& processorId1,
                            const std::string& objectPath,
                            const MapperServiceMap& serviceMap,
                            [[maybe_unused]] const std::string& deviceType) {
                            redfish::nvidia_processor::patchEccMode(
                                asyncResp1, processorId1, *eccModeEnabled,
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
                            ccMode, "CCDevModeEnabled", ccDevMode,
                            "EGMModeEnabled", egmMode))
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
                                [ccMode](
                                    const std::shared_ptr<bmcweb::AsyncResp>&
                                        asyncResp1,
                                    const std::string& processorId1,
                                    const std::string& objectPath,
                                    const MapperServiceMap& serviceMap,
                                    [[maybe_unused]] const std::string&
                                        deviceType) {
                                    redfish::nvidia_processor_utils::
                                        patchCCMode(asyncResp1, processorId1,
                                                    *ccMode, objectPath,
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
                                    redfish::nvidia_processor_utils::
                                        patchCCDevMode(asyncResp1, processorId1,
                                                       *ccDevMode, objectPath,
                                                       serviceMap);
                                });
                        }
                        if (egmMode)
                        {
                            redfish::processor_utils::getProcessorObject(
                                asyncResp, processorId,
                                [egmMode](
                                    const std::shared_ptr<bmcweb::AsyncResp>&
                                        asyncResp1,
                                    const std::string& processorId1,
                                    const std::string& objectPath,
                                    const MapperServiceMap& serviceMap,
                                    [[maybe_unused]] const std::string&
                                        deviceType) {
                                    redfish::nvidia_processor_utils::
                                        patchEgmMode(asyncResp1, processorId1,
                                                     *egmMode, objectPath,
                                                     serviceMap);
                                });
                        }
                    }
                }
            }
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
                            const std::shared_ptr<bmcweb::AsyncResp>&
                                asyncResp1,
                            const std::string& processorId1,
                            const std::string& objectPath,
                            const MapperServiceMap& serviceMap,
                            [[maybe_unused]] const std::string& deviceType) {
                            redfish::nvidia_processor::postResetType(
                                asyncResp1, processorId1, objectPath,
                                *resetType, serviceMap);
                        });
                }
            });
}

} // namespace redfish
