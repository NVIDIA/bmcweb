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

#include "health.hpp"

#include <app.hpp>
#include <boost/container/flat_map.hpp>
#include <dbus_utility.hpp>
#include <registries/privilege_registry.hpp>
#include <utils/collection.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/hex_utils.hpp>
#include <utils/json_utils.hpp>

namespace redfish
{

using DimmProperties =
    boost::container::flat_map<std::string, dbus::utility::DbusVariantType>;

inline std::string translateMemoryTypeToRedfish(const std::string& memoryType)
{
    if (memoryType == "xyz.openbmc_project.Inventory.Item.Dimm.DeviceType.DDR")
    {
        return "DDR";
    }
    if (memoryType == "xyz.openbmc_project.Inventory.Item.Dimm.DeviceType.DDR2")
    {
        return "DDR2";
    }
    if (memoryType == "xyz.openbmc_project.Inventory.Item.Dimm.DeviceType.DDR3")
    {
        return "DDR3";
    }
    if (memoryType == "xyz.openbmc_project.Inventory.Item.Dimm.DeviceType.DDR4")
    {
        return "DDR4";
    }
    if (memoryType ==
        "xyz.openbmc_project.Inventory.Item.Dimm.DeviceType.DDR4E_SDRAM")
    {
        return "DDR4E_SDRAM";
    }
    if (memoryType == "xyz.openbmc_project.Inventory.Item.Dimm.DeviceType.DDR5")
    {
        return "DDR5";
    }
    if (memoryType ==
        "xyz.openbmc_project.Inventory.Item.Dimm.DeviceType.LPDDR4_SDRAM")
    {
        return "LPDDR4_SDRAM";
    }
    if (memoryType ==
        "xyz.openbmc_project.Inventory.Item.Dimm.DeviceType.LPDDR3_SDRAM")
    {
        return "LPDDR3_SDRAM";
    }
    if (memoryType ==
        "xyz.openbmc_project.Inventory.Item.Dimm.DeviceType.DDR2_SDRAM_FB_DIMM")
    {
        return "DDR2_SDRAM_FB_DIMM";
    }
    if (memoryType ==
        "xyz.openbmc_project.Inventory.Item.Dimm.DeviceType.DDR2_SDRAM_FB_DIMM_PROB")
    {
        return "DDR2_SDRAM_FB_DIMM_PROBE";
    }
    if (memoryType ==
        "xyz.openbmc_project.Inventory.Item.Dimm.DeviceType.DDR_SGRAM")
    {
        return "DDR_SGRAM";
    }
    if (memoryType == "xyz.openbmc_project.Inventory.Item.Dimm.DeviceType.ROM")
    {
        return "ROM";
    }
    if (memoryType ==
        "xyz.openbmc_project.Inventory.Item.Dimm.DeviceType.SDRAM")
    {
        return "SDRAM";
    }
    if (memoryType == "xyz.openbmc_project.Inventory.Item.Dimm.DeviceType.EDO")
    {
        return "EDO";
    }
    if (memoryType ==
        "xyz.openbmc_project.Inventory.Item.Dimm.DeviceType.FastPageMode")
    {
        return "FastPageMode";
    }
    if (memoryType ==
        "xyz.openbmc_project.Inventory.Item.Dimm.DeviceType.PipelinedNibble")
    {
        return "PipelinedNibble";
    }
    if (memoryType ==
        "xyz.openbmc_project.Inventory.Item.Dimm.DeviceType.Logical")
    {
        return "Logical";
    }
    if (memoryType == "xyz.openbmc_project.Inventory.Item.Dimm.DeviceType.HBM")
    {
        return "HBM";
    }
    if (memoryType == "xyz.openbmc_project.Inventory.Item.Dimm.DeviceType.HBM2")
    {
        return "HBM2";
    }
    // This is values like Other or Unknown
    // Also D-Bus values:
    // DRAM
    // EDRAM
    // VRAM
    // SRAM
    // RAM
    // FLASH
    // EEPROM
    // FEPROM
    // EPROM
    // CDRAM
    // ThreeDRAM
    // RDRAM
    // FBD2
    // LPDDR_SDRAM
    // LPDDR2_SDRAM
    // LPDDR5_SDRAM
    return "";
}

inline void dimmPropToHex(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const char* key,
    const std::pair<std::string, dbus::utility::DbusVariantType>& property)
{
    const uint16_t* value = std::get_if<uint16_t>(&property.second);
    if (value == nullptr)
    {
        messages::internalError(aResp->res);
        BMCWEB_LOG_DEBUG << "Invalid property type for " << property.first;
        return;
    }

    aResp->res.jsonValue[key] = "0x" + intToHexString(*value);
}

inline void getPersistentMemoryProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const DimmProperties& properties)
{
    for (const auto& property : properties)
    {
        if (property.first == "ModuleManufacturerID")
        {
            dimmPropToHex(aResp, "ModuleManufacturerID", property);
        }
        else if (property.first == "ModuleProductID")
        {
            dimmPropToHex(aResp, "ModuleProductID", property);
        }
        else if (property.first == "SubsystemVendorID")
        {
            dimmPropToHex(aResp, "MemorySubsystemControllerManufacturerID",
                          property);
        }
        else if (property.first == "SubsystemDeviceID")
        {
            dimmPropToHex(aResp, "MemorySubsystemControllerProductID",
                          property);
        }
        else if (property.first == "VolatileRegionSizeLimitInKiB")
        {
            const uint64_t* value = std::get_if<uint64_t>(&property.second);

            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                BMCWEB_LOG_DEBUG
                    << "Invalid property type for VolatileRegionSizeLimitKiB";
                return;
            }
            aResp->res.jsonValue["VolatileRegionSizeLimitMiB"] = (*value) >> 10;
        }
        else if (property.first == "PmRegionSizeLimitInKiB")
        {
            const uint64_t* value = std::get_if<uint64_t>(&property.second);

            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                BMCWEB_LOG_DEBUG
                    << "Invalid property type for PmRegioSizeLimitKiB";
                return;
            }
            aResp->res.jsonValue["PersistentRegionSizeLimitMiB"] =
                (*value) >> 10;
        }
        else if (property.first == "VolatileSizeInKiB")
        {
            const uint64_t* value = std::get_if<uint64_t>(&property.second);

            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                BMCWEB_LOG_DEBUG
                    << "Invalid property type for VolatileSizeInKiB";
                return;
            }
            aResp->res.jsonValue["VolatileSizeMiB"] = (*value) >> 10;
        }
        else if (property.first == "PmSizeInKiB")
        {
            const uint64_t* value = std::get_if<uint64_t>(&property.second);
            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                BMCWEB_LOG_DEBUG << "Invalid property type for PmSizeInKiB";
                return;
            }
            aResp->res.jsonValue["NonVolatileSizeMiB"] = (*value) >> 10;
        }
        else if (property.first == "CacheSizeInKB")
        {
            const uint64_t* value = std::get_if<uint64_t>(&property.second);
            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                BMCWEB_LOG_DEBUG << "Invalid property type for CacheSizeInKB";
                return;
            }
            aResp->res.jsonValue["CacheSizeMiB"] = (*value >> 10);
        }

        else if (property.first == "VoltaileRegionMaxSizeInKib")
        {
            const uint64_t* value = std::get_if<uint64_t>(&property.second);

            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                BMCWEB_LOG_DEBUG
                    << "Invalid property type for VolatileRegionMaxSizeInKib";
                return;
            }
            aResp->res.jsonValue["VolatileRegionSizeMaxMiB"] = (*value) >> 10;
        }
        else if (property.first == "PmRegionMaxSizeInKiB")
        {
            const uint64_t* value = std::get_if<uint64_t>(&property.second);

            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                BMCWEB_LOG_DEBUG
                    << "Invalid property type for PmRegionMaxSizeInKiB";
                return;
            }
            aResp->res.jsonValue["PersistentRegionSizeMaxMiB"] = (*value) >> 10;
        }
        else if (property.first == "AllocationIncrementInKiB")
        {
            const uint64_t* value = std::get_if<uint64_t>(&property.second);

            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                BMCWEB_LOG_DEBUG
                    << "Invalid property type for AllocationIncrementInKiB";
                return;
            }
            aResp->res.jsonValue["AllocationIncrementMiB"] = (*value) >> 10;
        }
        else if (property.first == "AllocationAlignmentInKiB")
        {
            const uint64_t* value = std::get_if<uint64_t>(&property.second);

            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                BMCWEB_LOG_DEBUG
                    << "Invalid property type for AllocationAlignmentInKiB";
                return;
            }
            aResp->res.jsonValue["AllocationAlignmentMiB"] = (*value) >> 10;
        }
        else if (property.first == "VolatileRegionNumberLimit")
        {
            const uint64_t* value = std::get_if<uint64_t>(&property.second);
            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }
            aResp->res.jsonValue["VolatileRegionNumberLimit"] = *value;
        }
        else if (property.first == "PmRegionNumberLimit")
        {
            const uint64_t* value = std::get_if<uint64_t>(&property.second);
            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }
            aResp->res.jsonValue["PersistentRegionNumberLimit"] = *value;
        }
        else if (property.first == "SpareDeviceCount")
        {
            const uint64_t* value = std::get_if<uint64_t>(&property.second);
            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }
            aResp->res.jsonValue["SpareDeviceCount"] = *value;
        }
        else if (property.first == "IsSpareDeviceInUse")
        {
            const bool* value = std::get_if<bool>(&property.second);
            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }
            aResp->res.jsonValue["IsSpareDeviceEnabled"] = *value;
        }
        else if (property.first == "IsRankSpareEnabled")
        {
            const bool* value = std::get_if<bool>(&property.second);
            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }
            aResp->res.jsonValue["IsRankSpareEnabled"] = *value;
        }
        else if (property.first == "MaxAveragePowerLimitmW")
        {
            const auto* value =
                std::get_if<std::vector<uint32_t>>(&property.second);
            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                BMCWEB_LOG_DEBUG
                    << "Invalid property type for MaxAveragePowerLimitmW";
                return;
            }
            aResp->res.jsonValue["MaxTDPMilliWatts"] = *value;
        }
        else if (property.first == "ConfigurationLocked")
        {
            const bool* value = std::get_if<bool>(&property.second);
            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }
            aResp->res.jsonValue["ConfigurationLocked"] = *value;
        }
        else if (property.first == "AllowedMemoryModes")
        {
            const std::string* value =
                std::get_if<std::string>(&property.second);
            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                BMCWEB_LOG_DEBUG << "Invalid property type for FormFactor";
                return;
            }
            constexpr const std::array<const char*, 3> values{"Volatile",
                                                              "PMEM", "Block"};

            for (const char* v : values)
            {
                if (boost::ends_with(*value, v))
                {
                    aResp->res.jsonValue["OperatingMemoryModes "] = v;
                    break;
                }
            }
        }
        else if (property.first == "MemoryMedia")
        {
            const std::string* value =
                std::get_if<std::string>(&property.second);
            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                BMCWEB_LOG_DEBUG << "Invalid property type for MemoryMedia";
                return;
            }
            constexpr const std::array<const char*, 3> values{"DRAM", "NAND",
                                                              "Intel3DXPoint"};

            for (const char* v : values)
            {
                if (boost::ends_with(*value, v))
                {
                    aResp->res.jsonValue["MemoryMedia"] = v;
                    break;
                }
            }
        }
        // PersistantMemory.SecurityCapabilites interface
        else if (property.first == "ConfigurationLockCapable" ||
                 property.first == "DataLockCapable" ||
                 property.first == "PassphraseCapable")
        {
            const bool* value = std::get_if<bool>(&property.second);
            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }
            aResp->res.jsonValue["SecurityCapabilities"][property.first] =
                *value;
        }
        else if (property.first == "MaxPassphraseCount" ||
                 property.first == "PassphraseLockLimit")
        {
            const uint64_t* value = std::get_if<uint64_t>(&property.second);
            if (value == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }
            aResp->res.jsonValue["SecurityCapabilities"][property.first] =
                *value;
        }
    }
}

inline void
    getDimmDataByService(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& dimmId, const std::string& service,
                         const std::string& objPath)
{
#ifdef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE
    std::shared_ptr<HealthRollup> health = std::make_shared<HealthRollup>(
        crow::connections::systemBus, objPath,
        [aResp](const std::string& rootHealth,
                const std::string& healthRollup) {
            aResp->res.jsonValue["Status"]["Health"] = rootHealth;
            aResp->res.jsonValue["Status"]["HealthRollup"] = healthRollup;
        });
    health->start();
#else  // ifdef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE
    auto health = std::make_shared<HealthPopulate>(aResp);
    health->selfPath = objPath;
    health->populate();
#endif // ifdef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE

    BMCWEB_LOG_DEBUG << "Get available system components.";
    crow::connections::systemBus->async_method_call(
        [dimmId, aResp{aResp}](const boost::system::error_code ec,
                               const DimmProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error";
                messages::internalError(aResp->res);

                return;
            }
            aResp->res.jsonValue["Id"] = dimmId;
            aResp->res.jsonValue["Name"] = "DIMM Slot";

            const auto memorySizeProperty = properties.find("MemorySizeInKB");
            if (memorySizeProperty != properties.end())
            {
                const uint32_t* memorySize =
                    std::get_if<uint32_t>(&memorySizeProperty->second);
                if (memorySize == nullptr)
                {
                    // Important property not in desired type
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["CapacityMiB"] = (*memorySize >> 10);
            }
            aResp->res.jsonValue["Status"]["State"] = "Enabled";
#ifndef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE
            aResp->res.jsonValue["Status"]["Health"] = "OK";
#endif // ifndef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE
            for (const auto& property : properties)
            {
                if (property.first == "MemoryDataWidth")
                {
                    const uint16_t* value =
                        std::get_if<uint16_t>(&property.second);
                    if (value == nullptr)
                    {
                        continue;
                    }
                    aResp->res.jsonValue["DataWidthBits"] = *value;
                }
                else if (property.first == "PartNumber")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        continue;
                    }
                    aResp->res.jsonValue["PartNumber"] = *value;
                }
                else if (property.first == "SerialNumber")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        continue;
                    }
                    aResp->res.jsonValue["SerialNumber"] = *value;
                }
                else if (property.first == "Manufacturer")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        continue;
                    }
                    aResp->res.jsonValue["Manufacturer"] = *value;
                }
                else if (property.first == "RevisionCode")
                {
                    const uint16_t* value =
                        std::get_if<uint16_t>(&property.second);

                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        BMCWEB_LOG_DEBUG
                            << "Invalid property type for RevisionCode";
                        return;
                    }
                    aResp->res.jsonValue["FirmwareRevision"] =
                        std::to_string(*value);
                }
                else if (property.first == "Present")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        BMCWEB_LOG_DEBUG
                            << "Invalid property type for Dimm Presence";
                        return;
                    }
                    if (*value == false)
                    {
                        aResp->res.jsonValue["Status"]["State"] = "Absent";
                    }
                }
                else if (property.first == "MemoryTotalWidth")
                {
                    const uint16_t* value =
                        std::get_if<uint16_t>(&property.second);
                    if (value == nullptr)
                    {
                        continue;
                    }
                    aResp->res.jsonValue["BusWidthBits"] = *value;
                }
                else if (property.first == "ECC")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        BMCWEB_LOG_DEBUG << "Invalid property type for ECC";
                        return;
                    }
                    constexpr const std::array<const char*, 4> values{
                        "NoECC", "SingleBitECC", "MultiBitECC",
                        "AddressParity"};

                    for (const char* v : values)
                    {
                        if (boost::ends_with(*value, v))
                        {
                            aResp->res.jsonValue["ErrorCorrection"] = v;
                            break;
                        }
                    }
                }
                else if (property.first == "FormFactor")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        BMCWEB_LOG_DEBUG
                            << "Invalid property type for FormFactor";
                        return;
                    }
                    constexpr const std::array<const char*, 11> values{
                        "RDIMM",        "UDIMM",        "SO_DIMM",
                        "LRDIMM",       "Mini_RDIMM",   "Mini_UDIMM",
                        "SO_RDIMM_72b", "SO_UDIMM_72b", "SO_DIMM_16b",
                        "SO_DIMM_32b",  "Die"};

                    for (const char* v : values)
                    {
                        if (boost::ends_with(*value, v))
                        {
                            aResp->res.jsonValue["BaseModuleType"] = v;
                            break;
                        }
                    }
                }
                else if (property.first == "AllowedSpeedsMT")
                {
                    const std::vector<uint16_t>* value =
                        std::get_if<std::vector<uint16_t>>(&property.second);
                    if (value == nullptr)
                    {
                        continue;
                    }
                    nlohmann::json& jValue =
                        aResp->res.jsonValue["AllowedSpeedsMHz"];
                    jValue = nlohmann::json::array();
                    for (uint16_t subVal : *value)
                    {
                        jValue.push_back(subVal);
                    }
                }
                else if (property.first == "MemoryAttributes")
                {
                    const uint8_t* value =
                        std::get_if<uint8_t>(&property.second);

                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        BMCWEB_LOG_DEBUG
                            << "Invalid property type for MemoryAttributes";
                        return;
                    }
                    aResp->res.jsonValue["RankCount"] =
                        static_cast<uint64_t>(*value);
                }
                else if (property.first == "MemoryConfiguredSpeedInMhz")
                {
                    const uint16_t* value =
                        std::get_if<uint16_t>(&property.second);
                    if (value == nullptr)
                    {
                        continue;
                    }
                    aResp->res.jsonValue["OperatingSpeedMhz"] = *value;
                }
                else if (property.first == "MemoryType")
                {
                    const auto* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        std::string memoryDeviceType =
                            translateMemoryTypeToRedfish(*value);
                        // Values like "Unknown" or "Other" will return empty
                        // so just leave off
                        if (!memoryDeviceType.empty())
                        {
                            aResp->res.jsonValue["MemoryDeviceType"] =
                                memoryDeviceType;
                        }
                        if ((value->find("DDR") != std::string::npos) ||
                            (boost::ends_with(*value, "HBM2")))
                        {
                            aResp->res.jsonValue["MemoryType"] = "DRAM";
                        }
                        else if (boost::ends_with(*value, "Logical"))
                        {
                            aResp->res.jsonValue["MemoryType"] = "IntelOptane";
                        }
                    }
                }
                // memory location interface
                else if (property.first == "Channel" ||
                         property.first == "MemoryController" ||
                         property.first == "Slot" || property.first == "Socket")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["MemoryLocation"][property.first] =
                        *value;
                }
                else if (property.first == "SparePartNumber")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["SparePartNumber"] = *value;
                }
                else if (property.first == "Model")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["Model"] = *value;
                }
                else if (property.first == "LocationCode")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res
                        .jsonValue["Location"]["PartLocation"]["ServiceLabel"] =
                        *value;
                }
                else if (property.first == "LocationType")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res
                        .jsonValue["Location"]["PartLocation"]["LocationType"] =
                        redfish::dbus_utils::toLocationType(*value);
                }
                else
                {
                    getPersistentMemoryProperties(aResp, properties);
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll", "");
}

inline void
    getDimmPartitionData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& service, const std::string& path)
{
    crow::connections::systemBus->async_method_call(
        [aResp{aResp}](
            const boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, dbus::utility::DbusVariantType>& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error";
                messages::internalError(aResp->res);

                return;
            }

            nlohmann::json& partition =
                aResp->res.jsonValue["Regions"].emplace_back(
                    nlohmann::json::object());
            for (const auto& [key, val] : properties)
            {
                if (key == "MemoryClassification")
                {
                    const std::string* value = std::get_if<std::string>(&val);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    partition[key] = *value;
                }
                else if (key == "OffsetInKiB")
                {
                    const uint64_t* value = std::get_if<uint64_t>(&val);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }

                    partition["OffsetMiB"] = (*value >> 10);
                }
                else if (key == "PartitionId")
                {
                    const std::string* value = std::get_if<std::string>(&val);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    partition["RegionId"] = *value;
                }

                else if (key == "PassphraseState")
                {
                    const bool* value = std::get_if<bool>(&val);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    partition["PassphraseEnabled"] = *value;
                }
                else if (key == "SizeInKiB")
                {
                    const uint64_t* value = std::get_if<uint64_t>(&val);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        BMCWEB_LOG_DEBUG
                            << "Invalid property type for SizeInKiB";
                        return;
                    }
                    partition["SizeMiB"] = (*value >> 10);
                }
            }
        },

        service, path, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Inventory.Item.PersistentMemory.Partition");
}

/**
 * @brief Fill out links association to parent processor by
 * requesting data from the given D-Bus association object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void
    getMemoryProcessorLink(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                           const std::string& objPath)
{
    BMCWEB_LOG_DEBUG << "Get parent processor link";
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
                    {{"@odata.id", "/redfish/v1/Systems/system/Processors/" +
                                       processorName}});
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
inline void
    getMemoryChassisLink(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& objPath)
{
    BMCWEB_LOG_DEBUG << "Get parent chassis link";
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec2,
                std::variant<std::vector<std::string>>& resp) {
            if (ec2)
            {
                return; // no chassis = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr && data->size() > 1)
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

inline void getDimmData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                        const std::string& dimmId)
{
    BMCWEB_LOG_DEBUG << "Get available system dimm resources.";
    crow::connections::systemBus->async_method_call(
        [dimmId, aResp{aResp}](
            const boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, boost::container::flat_map<
                                 std::string, std::vector<std::string>>>&
                subtree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error";
                messages::internalError(aResp->res);

                return;
            }
            bool found = false;
            for (const auto& [path, object] : subtree)
            {
                if (path.find(dimmId) != std::string::npos)
                {
                    for (const auto& [service, interfaces] : object)
                    {
                        if (!found &&
                            (std::find(
                                 interfaces.begin(), interfaces.end(),
                                 "xyz.openbmc_project.Inventory.Item.Dimm") !=
                             interfaces.end()))
                        {
                            getDimmDataByService(aResp, dimmId, service, path);
                            found = true;
                            // Link association to parent processor
                            getMemoryProcessorLink(aResp, path);
                            // Link association to parent chassis
                            getMemoryChassisLink(aResp, path);
                        }

                        // partitions are separate as there can be multiple per
                        // device, i.e.
                        // /xyz/openbmc_project/Inventory/Item/Dimm1/Partition1
                        // /xyz/openbmc_project/Inventory/Item/Dimm1/Partition2
                        if (std::find(
                                interfaces.begin(), interfaces.end(),
                                "xyz.openbmc_project.Inventory.Item.PersistentMemory.Partition") !=
                            interfaces.end())
                        {
                            getDimmPartitionData(aResp, service, path);
                        }
                    }
                }
            }
            // Object not found
            if (!found)
            {
                messages::resourceNotFound(aResp->res, "Memory", dimmId);
            }
            return;
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 2>{
            "xyz.openbmc_project.Inventory.Item.Dimm",
            "xyz.openbmc_project.Inventory.Item.PersistentMemory.Partition"});
}

inline void requestRoutesMemoryCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/system/Memory/")
        .privileges(redfish::privileges::getMemoryCollection)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                asyncResp->res.jsonValue["@odata.type"] =
                    "#MemoryCollection.MemoryCollection";
                asyncResp->res.jsonValue["Name"] = "Memory Module Collection";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Systems/system/Memory";

                collection_util::getCollectionMembers(
                    asyncResp, "/redfish/v1/Systems/system/Memory",
                    {"xyz.openbmc_project.Inventory.Item.Dimm"});
            });
}

inline void requestRoutesMemory(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/system/Memory/<str>/")
        .privileges(redfish::privileges::getMemory)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& dimmId) {
                asyncResp->res.jsonValue["@odata.type"] =
                    "#Memory.v1_11_0.Memory";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Systems/system/Memory/" + dimmId;
                std::string memoryMetricsURI =
                    "/redfish/v1/Systems/system/Memory/";
                memoryMetricsURI += dimmId;
                std::string environmentMetricsURI = memoryMetricsURI;
                memoryMetricsURI += "/MemoryMetrics";
                asyncResp->res.jsonValue["Metrics"]["@odata.id"] =
                    memoryMetricsURI;
                environmentMetricsURI += "/EnvironmentMetrics";
                asyncResp->res.jsonValue["EnvironmentMetrics"]["@odata.id"] =
                    environmentMetricsURI;

                getDimmData(asyncResp, dimmId);
            });
}

inline void getMemoryDataByService(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                   const std::string& service,
                                   const std::string& objPath)
{
    BMCWEB_LOG_DEBUG << "Get memory metrics data.";
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}](const boost::system::error_code ec,
                                  const DimmProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error";
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
    BMCWEB_LOG_DEBUG << "Get memory ecc data.";
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}](const boost::system::error_code ec,
                                  const DimmProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error";
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

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
inline void getMemoryRowRemappings(std::shared_ptr<bmcweb::AsyncResp> aResp,
                             const std::string& service,
                             const std::string& objPath)
{
    BMCWEB_LOG_DEBUG << "Get memory row remapping counts.";
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}](const boost::system::error_code ec,
                                  const DimmProperties& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "DBUS response error";
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
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res
                        .jsonValue["Oem"]["Nvidia"]
                                  ["RowRemapping"]["CorrectableRowRemappingCount"] =
                        *value;
                }
                else if (property.first == "ueRowRemappingCount")
                {
                    const uint32_t* value =
                        std::get_if<uint32_t>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res
                        .jsonValue["Oem"]["Nvidia"]
                                  ["RowRemapping"]["UncorrectableRowRemappingCount"] =
                        *value;
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.MemoryRowRemapping");
}
#endif  //BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES

inline void getMemoryMetricsData(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                 const std::string& dimmId)
{
    BMCWEB_LOG_DEBUG << "Get available system memory resources.";
    crow::connections::systemBus->async_method_call(
        [dimmId, aResp{std::move(aResp)}](
            const boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, boost::container::flat_map<
                                 std::string, std::vector<std::string>>>&
                subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << " DBUS response error";
                messages::internalError(aResp->res);

                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!boost::ends_with(path, dimmId))
                {
                    continue;
                }
                std::string memoryMetricsURI =
                    "/redfish/v1/Systems/system/Memory/";
                memoryMetricsURI += dimmId;
                memoryMetricsURI += "/MemoryMetrics";
                aResp->res.jsonValue["@odata.type"] =
                    "#MemoryMetrics.v1_4_1.MemoryMetrics";
                aResp->res.jsonValue["@odata.id"] = memoryMetricsURI;
                aResp->res.jsonValue["Id"] = "MemoryMetrics";
                aResp->res.jsonValue["Name"] = dimmId + " Memory Metrics";
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
                aResp->res.jsonValue["Oem"]["Nvidia"]
                                    ["@odata.id"] = "#NvidiaMemoryMetrics.v1_0_0.NvidiaMemoryMetrics";
#endif  //BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
                for (const auto& [service, interfaces] : object)
                {
                    if (std::find(interfaces.begin(), interfaces.end(),
                                  "xyz.openbmc_project.Inventory.Item.Dimm") !=
                        interfaces.end())
                    {
                        getMemoryDataByService(aResp, service, path);
                    }
                    if (std::find(interfaces.begin(), interfaces.end(),
                                  "xyz.openbmc_project.Memory.MemoryECC") !=
                        interfaces.end())
                    {
                        getMemoryECCData(aResp, service, path);
                    }
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
                    if (std::find(interfaces.begin(), interfaces.end(),
                                  "com.nvidia.MemoryRowRemapping") !=
                        interfaces.end())
                    {
                       getMemoryRowRemappings(aResp, service, path);
                    }
#endif  //BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
                }
                return;
            }
            // Object not found
            messages::resourceNotFound(aResp->res, "#Memory.v1_11_0.Memory",
                                       dimmId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 1>{"xyz.openbmc_project.Inventory.Item.Dimm"});
}

inline void requestRoutesMemoryMetrics(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/system/Memory/<str>/MemoryMetrics")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& dimmId) {
                getMemoryMetricsData(asyncResp, dimmId);
            });
}

} // namespace redfish
