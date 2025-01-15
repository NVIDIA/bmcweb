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
#include "dbus_utility.hpp"
#include "generated/enums/resource.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/pcie_util.hpp"

#include <app.hpp>
#include <boost/system/linux_error.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/conditions_utils.hpp>
#include <utils/nvidia_pcie_utils.hpp>

#include <limits>

namespace redfish
{

using GetSubTreeType = std::vector<
    std::pair<std::string,
              std::vector<std::pair<std::string, std::vector<std::string>>>>>;

static constexpr const char* pcieService = "xyz.openbmc_project.PCIe";
static constexpr const char* pciePath = "/xyz/openbmc_project/PCIe";
static constexpr const char* assetInterface =
    "xyz.openbmc_project.Inventory.Decorator.Asset";
static constexpr const char* uuidInterface = "xyz.openbmc_project.Common.UUID";
static constexpr const char* stateInterface =
    "xyz.openbmc_project.State.Chassis";
static constexpr const char* pcieClockReferenceIntf =
    "xyz.openbmc_project.Inventory.Decorator.PCIeRefClock";
static constexpr const char* nvlinkClockReferenceIntf =
    "com.nvidia.NVLink.NVLinkRefClock";
static constexpr const char* pcieLtssmIntf =
    "xyz.openbmc_project.PCIe.LTSSMState";

static constexpr const char* pcieAerErrorStatusIntf =
    "com.nvidia.PCIe.AERErrorStatus";

static inline std::string getPCIeType(const std::string& pcieType)
{
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeDevice.PCIeTypes.Gen1")
    {
        return "Gen1";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeDevice.PCIeTypes.Gen2")
    {
        return "Gen2";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeDevice.PCIeTypes.Gen3")
    {
        return "Gen3";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeDevice.PCIeTypes.Gen4")
    {
        return "Gen4";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeDevice.PCIeTypes.Gen5")
    {
        return "Gen5";
    }
    if (pcieType ==
        "xyz.openbmc_project.Inventory.Item.PCIeDevice.PCIeTypes.Gen6")
    {
        return "Gen6";
    }
    // Unknown or others
    return "Unknown";
}

static constexpr const char* inventoryPath = "/xyz/openbmc_project/inventory";
static constexpr std::array<std::string_view, 1> pcieDeviceInterface = {
    "xyz.openbmc_project.Inventory.Item.PCIeDevice"};
static constexpr std::array<std::string_view, 1> pcieSlotInterface = {
    "xyz.openbmc_project.Inventory.Item.PCIeSlot"};
static constexpr const char* pcieDeviceInterfaceNV =
    "xyz.openbmc_project.PCIe.Device";

inline void handlePCIeDevicePath(
    const std::string& pcieDeviceId,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const dbus::utility::MapperGetSubTreePathsResponse& pcieDevicePaths,
    const std::function<void(const std::string& pcieDevicePath,
                             const std::string& service)>& callback)

{
    for (const std::string& pcieDevicePath : pcieDevicePaths)
    {
        std::string pciecDeviceName =
            sdbusplus::message::object_path(pcieDevicePath).filename();
        if (pciecDeviceName.empty() || pciecDeviceName != pcieDeviceId)
        {
            continue;
        }

        dbus::utility::getDbusObject(
            pcieDevicePath, pcieDeviceInterface,
            [pcieDevicePath, asyncResp,
             callback](const boost::system::error_code& ec,
                       const dbus::utility::MapperGetObject& object) {
                if (ec || object.empty())
                {
                    BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                    messages::internalError(asyncResp->res);
                    return;
                }
                callback(pcieDevicePath, object.begin()->first);
            });
        return;
    }

    BMCWEB_LOG_WARNING("PCIe Device not found");
    messages::resourceNotFound(asyncResp->res, "PCIeDevice", pcieDeviceId);
}

inline void getValidPCIeDevicePath(
    const std::string& pcieDeviceId,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::function<void(const std::string& pcieDevicePath,
                             const std::string& service)>& callback)
{
    dbus::utility::getSubTreePaths(
        inventoryPath, 0, pcieDeviceInterface,
        [pcieDeviceId, asyncResp,
         callback](const boost::system::error_code& ec,
                   const dbus::utility::MapperGetSubTreePathsResponse&
                       pcieDevicePaths) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("D-Bus response error on GetSubTree {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            handlePCIeDevicePath(pcieDeviceId, asyncResp, pcieDevicePaths,
                                 callback);
            return;
        });
}

inline void handlePCIeDeviceCollectionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
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

    asyncResp->res.addHeader(boost::beast::http::field::link,
                             "</redfish/v1/JsonSchemas/PCIeDeviceCollection/"
                             "PCIeDeviceCollection.json>; rel=describedby");
    asyncResp->res.jsonValue["@odata.type"] =
        "#PCIeDeviceCollection.PCIeDeviceCollection";
    asyncResp->res.jsonValue["@odata.id"] = std::format(
        "/redfish/v1/Systems/{}/PCIeDevices", BMCWEB_REDFISH_SYSTEM_URI_NAME);
    asyncResp->res.jsonValue["Name"] = "PCIe Device Collection";
    asyncResp->res.jsonValue["Description"] = "Collection of PCIe Devices";

    pcie_util::getPCIeDeviceList(asyncResp,
                                 nlohmann::json::json_pointer("/Members"));
}

// PCIeDevice asset properties
static inline void
    getPCIeDeviceAssetData(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& device, const std::string& path,
                           const std::string& service)
{
    auto getPCIeDeviceAssetCallback =
        [asyncResp{asyncResp}](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string, std::variant<std::string>>>& propertiesList) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(asyncResp->res);
                return;
            }
            for (const std::pair<std::string, std::variant<std::string>>&
                     property : propertiesList)
            {
                const std::string& propertyName = property.first;
                if ((propertyName == "PartNumber") ||
                    (propertyName == "SerialNumber") ||
                    (propertyName == "Manufacturer") ||
                    (propertyName == "Model"))
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        asyncResp->res.jsonValue[propertyName] = *value;
                    }
                }
            }
        };
    std::string escapedPath = std::string(path) + "/" + device;
    dbus::utility::escapePathForDbus(escapedPath);
    crow::connections::systemBus->async_method_call(
        std::move(getPCIeDeviceAssetCallback), service, escapedPath,
        "org.freedesktop.DBus.Properties", "GetAll", assetInterface);
}

// PCIeDevice UUID
static inline void
    getPCIeDeviceUUID(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& device, const std::string& path,
                      const std::string& service)
{
    auto getPCIeDeviceUUIDCallback =
        [asyncResp{asyncResp}](const boost::system::error_code ec,
                               const std::variant<std::string>& uuid) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(asyncResp->res);
                return;
            }
            const std::string* s = std::get_if<std::string>(&uuid);
            if (s != nullptr)
            {
                asyncResp->res.jsonValue["UUID"] = *s;
            }
        };
    std::string escapedPath = std::string(path) + "/" + device;
    dbus::utility::escapePathForDbus(escapedPath);
    crow::connections::systemBus->async_method_call(
        std::move(getPCIeDeviceUUIDCallback), service, escapedPath,
        "org.freedesktop.DBus.Properties", "Get", uuidInterface, "UUID");
}

// PCIeDevice getPCIeDeviceClkRefOem
static inline void
    getPCIeDeviceClkRefOem(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& device, const std::string& path,
                           const std::string& service)
{
    auto getPCIeDeviceOemCallback = [asyncResp{asyncResp}](
                                        const boost::system::error_code ec,
                                        const std::vector<std::pair<
                                            std::string, std::variant<bool>>>&
                                            propertiesList) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG(
                "DBUS response error on getting PCIeDeviceclock reference OEM properties");
            messages::internalError(asyncResp->res);
            return;
        }

        for (const std::pair<std::string, std::variant<bool>>& property :
             propertiesList)
        {
            const std::string& propertyName = property.first;
            if (propertyName == "PCIeReferenceClockEnabled")
            {
                const bool* value = std::get_if<bool>(&property.second);
                if (value != nullptr)
                {
                    asyncResp->res.jsonValue["Oem"]["Nvidia"][propertyName] =
                        *value;
                }
            }
        }
    };
    std::string escapedPath = std::string(path) + "/" + device;
    dbus::utility::escapePathForDbus(escapedPath);
    crow::connections::systemBus->async_method_call(
        std::move(getPCIeDeviceOemCallback), service, escapedPath,
        "org.freedesktop.DBus.Properties", "GetAll", pcieClockReferenceIntf);
}

// PCIeDevice nvlink clock reference OEM
static inline void getPCIeDeviceNvLinkClkRefOem(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& device, const std::string& path,
    const std::string& service)
{
    auto getPCIeDeviceOemCallback = [asyncResp{asyncResp}](
                                        const boost::system::error_code ec,
                                        const std::vector<std::pair<
                                            std::string, std::variant<bool>>>&
                                            propertiesList) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG(
                "DBUS response error on getting PCIeDeviceNVLink Clock Reference OEM properties");
            messages::internalError(asyncResp->res);
            return;
        }

        for (const std::pair<std::string, std::variant<bool>>& property :
             propertiesList)
        {
            const std::string& propertyName = property.first;
            if (propertyName == "NVLinkReferenceClockEnabled")
            {
                const bool* value = std::get_if<bool>(&property.second);
                if (value != nullptr)
                {
                    asyncResp->res.jsonValue["Oem"]["Nvidia"][propertyName] =
                        *value;
                }
            }
        }
    };
    std::string escapedPath = std::string(path) + "/" + device;
    dbus::utility::escapePathForDbus(escapedPath);
    crow::connections::systemBus->async_method_call(
        std::move(getPCIeDeviceOemCallback), service, escapedPath,
        "org.freedesktop.DBus.Properties", "GetAll", nvlinkClockReferenceIntf);
}

// PCIeDevice LTSSM State
static inline void
    getPCIeLTssmState(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& device, const std::string& path,
                      const std::string& service)
{
    BMCWEB_LOG_DEBUG("FROM getPCIeLTssmState");

    std::string escapedPath = std::string(path) + "/" + device;
    dbus::utility::escapePathForDbus(escapedPath);

    crow::connections::systemBus->async_method_call(
        [asyncResp, service](const boost::system::error_code ec,
                             std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Failed to get connected_port");
                return; // no ports = no failures
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                return;
            }

            for (const std::string& portPath : *data)
            {
                auto getPCIeLtssmCallback = [asyncResp{asyncResp}](
                                                const boost::system::error_code
                                                    ec,
                                                const std::vector<std::pair<
                                                    std::string,
                                                    std::variant<std::string>>>&
                                                    propertiesList) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error on getting PCIeDevice LTSSM State");
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (const std::pair<std::string,
                                         std::variant<std::string>>& property :
                         propertiesList)
                    {
                        const std::string& propertyName = property.first;
                        if (propertyName == "LTSSMState")
                        {
                            const std::string* value =
                                std::get_if<std::string>(&property.second);
                            if (value != nullptr)
                            {
                                std::string val =
                                    redfish::dbus_utils::getRedfishLtssmState(
                                        *value);
                                if (val.empty())
                                    asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                                            [propertyName] =
                                        nlohmann::json::value_t::null;

                                else
                                {
                                    asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                                            [propertyName] =
                                        val;
                                }
                            }
                        }
                    }
                };
                crow::connections::systemBus->async_method_call(
                    std::move(getPCIeLtssmCallback), service, portPath,
                    "org.freedesktop.DBus.Properties", "GetAll", pcieLtssmIntf);
            }
        },
        "xyz.openbmc_project.ObjectMapper", escapedPath + "/connected_port",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

// PCIeDevice State
static inline void
    getPCIeDeviceState(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& device, const std::string& path,
                       const std::string& service)
{
    std::string escapedPath = std::string(path) + "/" + device;
    dbus::utility::escapePathForDbus(escapedPath);
    auto getPCIeDeviceStateCallback =
        [escapedPath,
         asyncResp{asyncResp}](const boost::system::error_code ec,
                               const std::variant<std::string>& deviceState) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(asyncResp->res);
                return;
            }
            const std::string* s = std::get_if<std::string>(&deviceState);
            if (s == nullptr)
            {
                BMCWEB_LOG_DEBUG("Device state of illegal, non-strig type");
                messages::internalError(asyncResp->res);
                return;
            }

            if (*s == "xyz.openbmc_project.State.Chassis.PowerState.On")
            {
                asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
                if constexpr (BMCWEB_HEALTH_ROLLUP_ALTERNATIVE)
                {
                    std::shared_ptr<HealthRollup> health =
                        std::make_shared<HealthRollup>(
                            escapedPath,
                            [asyncResp](const std::string& rootHealth,
                                        const std::string& healthRollup) {
                                asyncResp->res.jsonValue["Status"]["Health"] =
                                    rootHealth;
                                if constexpr (!BMCWEB_DISABLE_HEALTH_ROLLUP)
                                {
                                    asyncResp->res
                                        .jsonValue["Status"]["HealthRollup"] =
                                        healthRollup;
                                }
                            });
                    health->start();
                }
                else
                {
                    asyncResp->res.jsonValue["Status"]["Health"] = "OK";
                    if constexpr (!BMCWEB_DISABLE_HEALTH_ROLLUP)
                    {
                        asyncResp->res.jsonValue["Status"]["HealthRollup"] =
                            "OK";
                    }
                }
            }
            else if (*s == "xyz.openbmc_project.State.Chassis.PowerState.Off")
            {
                asyncResp->res.jsonValue["Status"]["State"] = "Disabled";
                asyncResp->res.jsonValue["Status"]["Health"] = "Critical";
                if constexpr (!BMCWEB_DISABLE_HEALTH_ROLLUP)
                {
                    asyncResp->res.jsonValue["Status"]["HealthRollup"] =
                        "Critical";
                }
            }
            else
            {
                BMCWEB_LOG_DEBUG(
                    "Unrecognized 'CurrentPowerState' value: '{}'. Omitting 'Status' entry in the response",
                    *s);
            }
            };
            crow::connections::systemBus->async_method_call(
                std::move(getPCIeDeviceStateCallback), service, escapedPath,
                "org.freedesktop.DBus.Properties", "Get", stateInterface,
                "CurrentPowerState");
        }

    // inline void requestRoutesSystemPCIeDeviceCollection(App& app)
    // {
    //     /**
    //      * Functions triggers appropriate requests on DBus
    //      */
    //     BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/PCIeDevices/")
    //         .privileges(redfish::privileges::getPCIeDeviceCollection)
    //         .methods(boost::beast::http::verb::get)(
    //             std::bind_front(handlePCIeDeviceCollectionGet,
    //             std::ref(app)));
    // }

    inline void
        addPCIeSlotProperties(
            crow::Response & res, const boost::system::error_code& ec,
            const dbus::utility::DBusPropertiesMap& pcieSlotProperties)
    {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error for getAllProperties{}",
                             ec.value());
            messages::internalError(res);
            return;
        }
        std::string generation;
        size_t lanes = 0;
        std::string slotType;

        bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), pcieSlotProperties, "Generation",
            generation, "Lanes", lanes, "SlotType", slotType);

        if (!success)
        {
            messages::internalError(res);
            return;
        }

        /*TODO: Add support for Gen6 once DMTF schema is updated, to be taken
         * care while upstream sync*/
        // std::optional<pcie_device::PCIeTypes> pcieType =
        //     pcie_util::redfishPcieGenerationFromDbus(generation);

        std::optional<std::string> pcieType =
            pcie_util::redfishPcieGenerationStringFromDbus(generation);
        if (!pcieType)
        {
            BMCWEB_LOG_WARNING("Unknown PCIeType: {}", generation);
        }
        else
        {
            /*TODO: Add support for Gen6 once DMTF schema is updated, to be
             * taken care while upstream sync*/
            // if (*pcieType == pcie_device::PCIeTypes::Invalid)
            // {
            //     BMCWEB_LOG_ERROR("Invalid PCIeType: {}", generation);
            //     messages::internalError(res);
            //     return;
            // }
            res.jsonValue["Slot"]["PCIeType"] = *pcieType;
        }

        if (lanes != 0)
        {
            res.jsonValue["Slot"]["Lanes"] = lanes;
        }

        std::optional<pcie_slots::SlotTypes> redfishSlotType =
            pcie_util::dbusSlotTypeToRf(slotType);
        if (!redfishSlotType)
        {
            BMCWEB_LOG_WARNING("Unknown PCIeSlot Type: {}", slotType);
        }
        else
        {
            if (*redfishSlotType == pcie_slots::SlotTypes::Invalid)
            {
                BMCWEB_LOG_ERROR("Invalid PCIeSlot type: {}", slotType);
                messages::internalError(res);
                return;
            }
            res.jsonValue["Slot"]["SlotType"] = *redfishSlotType;
        }
    }

    inline void getPCIeDeviceSlotPath(
        const std::string& pcieDevicePath,
        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
        std::function<void(const std::string& pcieDeviceSlot)>&& callback)
    {
        std::string associationPath = pcieDevicePath + "/contained_by";
        dbus::utility::getAssociatedSubTreePaths(
            associationPath, sdbusplus::message::object_path(inventoryPath), 0,
            pcieSlotInterface,
            [callback = std::move(callback), asyncResp, pcieDevicePath](
                const boost::system::error_code& ec,
                const dbus::utility::MapperGetSubTreePathsResponse& endpoints) {
                if (ec)
                {
                    if (ec.value() == EBADR)
                    {
                        // Missing association is not an error
                        return;
                    }
                    BMCWEB_LOG_ERROR(
                        "DBUS response error for getAssociatedSubTreePaths {}",
                        ec.value());
                    messages::internalError(asyncResp->res);
                    return;
                }
                if (endpoints.size() > 1)
                {
                    BMCWEB_LOG_ERROR(
                        "PCIeDevice is associated with more than one PCIeSlot: {}",
                        endpoints.size());
                    messages::internalError(asyncResp->res);
                    return;
                }
                if (endpoints.empty())
                {
                    // If the device doesn't have an association, return without
                    // PCIe Slot properties
                    BMCWEB_LOG_DEBUG(
                        "PCIeDevice is not associated with PCIeSlot");
                    return;
                }
                callback(endpoints[0]);
            });
    }

    static inline void getPCIeDevice(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
        const std::string& device, const std::string& path = pciePath,
        const std::string& service = pcieService,
        const std::string& deviceIntf = pcieDeviceInterfaceNV)
    {
        auto getPCIeDeviceCallback =
            [asyncResp, device](
                const boost::system::error_code ec,
                const std::vector<
                    std::pair<std::string, std::variant<std::string, size_t>>>&
                    propertiesList) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG(
                        "failed to get PCIe Device properties ec: {}: {}",
                        ec.value(), ec.message());
                    if (ec.value() ==
                        boost::system::linux_error::bad_request_descriptor)
                    {
                        messages::resourceNotFound(asyncResp->res, "PCIeDevice",
                                                   device);
                    }
                    else
                    {
                        messages::internalError(asyncResp->res);
                    }
                    return;
                }

                for (const std::pair<std::string,
                                     std::variant<std::string, size_t>>&
                         property : propertiesList)
                {
                    const std::string& propertyName = property.first;
                    if ((propertyName == "Manufacturer") ||
                        (propertyName == "DeviceType"))
                    {
                        const std::string* value =
                            std::get_if<std::string>(&property.second);
                        if (value != nullptr)
                        {
                            asyncResp->res.jsonValue[propertyName] = *value;
                        }
                    }
                    else if (propertyName == "MaxLanes")
                    {
                        const size_t* value =
                            std::get_if<size_t>(&property.second);
                        if (value != nullptr)
                        {
                            asyncResp->res
                                .jsonValue["PCIeInterface"][propertyName] =
                                *value;
                        }
                    }
                    else if (propertyName == "LanesInUse")
                    {
                        const size_t* value =
                            std::get_if<size_t>(&property.second);
                        if (value != nullptr)
                        {
                            if (*value == INT_MAX)
                            {
                                asyncResp->res
                                    .jsonValue["PCIeInterface"][propertyName] =
                                    0;
                            }
                            else
                            {
                                asyncResp->res
                                    .jsonValue["PCIeInterface"][propertyName] =
                                    *value;
                            }
                        }
                    }
                    else if ((propertyName == "PCIeType") ||
                             (propertyName == "MaxPCIeType"))
                    {
                        const std::string* value =
                            std::get_if<std::string>(&property.second);
                        if (value != nullptr)
                        {
                            std::optional<std::string> propValue =
                                pcie_util::redfishPcieTypeStringFromDbus(
                                    *value);
                            if (!propValue)
                            {
                                asyncResp->res
                                    .jsonValue["PCIeInterface"][propertyName] =
                                    nullptr;
                            }
                            else
                            {
                                asyncResp->res
                                    .jsonValue["PCIeInterface"][propertyName] =
                                    *propValue;
                            }
                        }
                    }
                    else if (propertyName == "GenerationInUse")
                    {
                        const std::string* value =
                            std::get_if<std::string>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("property {} value not found.",
                                     propertyName);
                    messages::internalError(asyncResp->res);
                    return;
                }
                        std::optional<std::string> generationInUse =
                            pcie_util::redfishPcieGenerationStringFromDbus(
                                *value);
                        if (!generationInUse)
                        {
                            asyncResp->res
                                .jsonValue["PCIeInterface"]["PCIeType"] =
                                nullptr;
                        }
                        else
                        {
                            asyncResp->res
                                .jsonValue["PCIeInterface"]["PCIeType"] =
                                *generationInUse;
                        }
                    }
                }
            };
        std::string escapedPath = std::string(path) + "/" + device;
        dbus::utility::escapePathForDbus(escapedPath);
        crow::connections::systemBus->async_method_call(
            std::move(getPCIeDeviceCallback), service, escapedPath,
            "org.freedesktop.DBus.Properties", "GetAll", deviceIntf);
    }
    inline void afterGetDbusObject(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
        const std::string& pcieDeviceSlot, const boost::system::error_code& ec,
        const dbus::utility::MapperGetObject& object)
    {
        if (ec || object.empty())
        {
            BMCWEB_LOG_ERROR("DBUS response error for getDbusObject {}",
                             ec.value());
            messages::internalError(asyncResp->res);
            return;
        }
        sdbusplus::asio::getAllProperties(
            *crow::connections::systemBus, object.begin()->first,
            pcieDeviceSlot, "xyz.openbmc_project.Inventory.Item.PCIeSlot",
            [asyncResp](
                const boost::system::error_code& ec2,
                const dbus::utility::DBusPropertiesMap& pcieSlotProperties) {
                addPCIeSlotProperties(asyncResp->res, ec2, pcieSlotProperties);
            });
    }

    inline void afterGetPCIeDeviceSlotPath(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
        const std::string& pcieDeviceSlot)
    {
        dbus::utility::getDbusObject(
            pcieDeviceSlot, pcieSlotInterface,
            [asyncResp,
             pcieDeviceSlot](const boost::system::error_code& ec,
                             const dbus::utility::MapperGetObject& object) {
                afterGetDbusObject(asyncResp, pcieDeviceSlot, ec, object);
            });
    }

    inline void getPCIeDeviceHealth(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
        const std::string& pcieDevicePath, const std::string& service)
    {
        sdbusplus::asio::getProperty<bool>(
            *crow::connections::systemBus, service, pcieDevicePath,
            "xyz.openbmc_project.State.Decorator.OperationalStatus",
            "Functional",
            [asyncResp](const boost::system::error_code& ec, const bool value) {
                if (ec)
                {
                    if (ec.value() != EBADR)
                    {
                        BMCWEB_LOG_ERROR("DBUS response error for Health {}",
                                         ec.value());
                        messages::internalError(asyncResp->res);
                    }
                    return;
                }
                if (!value)
                {
                    asyncResp->res.jsonValue["Status"]["Health"] =
                        resource::Health::Critical;
                }
            });
    }
    static inline void getPCIeDeviceFunctionsList(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
        const std::string& device, const std::string& path = pciePath,
        const std::string& service = pcieService,
        const std::string& deviceIntf = pcieDeviceInterfaceNV,
        const std::string& chassisId = std::string())
    {
        auto getPCIeDeviceCallback =
            [asyncResp{asyncResp}, device,
             chassisId](const boost::system::error_code ec,
                        boost::container::flat_map<std::string,
                                                   std::variant<std::string>>&
                            pcieDevProperties) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG(
                        "failed to get PCIe Device properties ec: {}: {} ",
                        ec.value(), ec.message());

                    if (ec.value() ==
                        boost::system::linux_error::bad_request_descriptor)
                    {
                        messages::resourceNotFound(asyncResp->res, "PCIeDevice",
                                                   device);
                    }
                    else
                    {
                        messages::internalError(asyncResp->res);
                    }
                    return;
                }

                nlohmann::json& pcieFunctionList =
                    asyncResp->res.jsonValue["Members"];
                pcieFunctionList = nlohmann::json::array();
                static constexpr const int maxPciFunctionNum = 8;
                for (int functionNum = 0; functionNum < maxPciFunctionNum;
                     functionNum++)
                {
                    // Check if this function exists by looking for a device
                    // ID
                    std::string devIDProperty =
                        "Function" + std::to_string(functionNum) + "DeviceId";
                    std::string* property = std::get_if<std::string>(
                        &pcieDevProperties[devIDProperty]);
                    if (property && !property->empty())
                    {
                        if (!chassisId.empty())
                        {
                            pcieFunctionList.push_back(
                                {{"@odata.id",
                                  std::string("/redfish/v1/Chassis/")
                                      .append(chassisId)
                                      .append("/PCIeDevices/")
                                      .append(device)
                                      .append("/PCIeFunctions/")
                                      .append(std::to_string(functionNum))}});
                        }
                        else
                        {
                            pcieFunctionList.push_back(
                                {{"@odata.id",
                                  "/redfish/v1/Systems/" +
                                      std::string(
                                          BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                      "/PCIeDevices/" + device +
                                      "/PCIeFunctions/" +
                                      std::to_string(functionNum)}});
                        }
                    }
                }
                asyncResp->res.jsonValue["Members@odata.count"] =
                    pcieFunctionList.size();
            };
        std::string escapedPath = std::string(path) + "/" + device;
        dbus::utility::escapePathForDbus(escapedPath);
        crow::connections::systemBus->async_method_call(
            std::move(getPCIeDeviceCallback), service, escapedPath,
            "org.freedesktop.DBus.Properties", "GetAll", deviceIntf);
    }

    inline void getPCIeDeviceState(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
        const std::string& pcieDevicePath, const std::string& service)
    {
        sdbusplus::asio::getProperty<bool>(
            *crow::connections::systemBus, service, pcieDevicePath,
            "xyz.openbmc_project.Inventory.Item", "Present",
            [asyncResp](const boost::system::error_code& ec, bool value) {
                if (ec)
                {
                    if (ec.value() != EBADR)
                    {
                        BMCWEB_LOG_ERROR("DBUS response error for State");
                        messages::internalError(asyncResp->res);
                    }
                    return;
                }
                if (!value)
                {
                    asyncResp->res.jsonValue["Status"]["State"] =
                        resource::State::Absent;
                }
            });
    }

    static inline void getPCIeDeviceFunction(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
        const std::string& device, const std::string& function,
        const std::string& path = pciePath,
        const std::string& service = pcieService,
        const std::string& chassisId = std::string(),
        const std::string& deviceIntf = pcieDeviceInterfaceNV)
    {
        auto getPCIeDeviceCallback = [asyncResp{asyncResp}, device, function,
                                      chassisId](
                                         const boost::system::error_code ec,
                                         boost::container::flat_map<
                                             std::string,
                                             std::variant<std::string>>&
                                             pcieDevProperties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG(
                    "failed to get PCIe Device properties ec: {} : {}",
                    ec.value(), ec.message());
                if (ec.value() ==
                    boost::system::linux_error::bad_request_descriptor)
                {
                    messages::resourceNotFound(asyncResp->res, "PCIeDevice",
                                               device);
                }
                else
                {
                    messages::internalError(asyncResp->res);
                }
                return;
            }

            // Check if this function exists by looking for a device ID
            std::string devIDProperty = "Function" + function + "DeviceId";
            if (std::string* property =
                    std::get_if<std::string>(&pcieDevProperties[devIDProperty]);
                property && property->empty())
            {
                messages::resourceNotFound(asyncResp->res,
                                           "#PCIeFunction.v1_2_0.PCIeFunction",
                                           function);
                return;
            }

            // Update pcieDeviceURI based on system or chassis path
            std::string pcieDeviceURI;
            std::string pcieFunctionURI;
            if (chassisId.empty())
            {
                pcieDeviceURI =
                    boost::urls::format("/redfish/v1/Systems/{}/PCIeDevices/{}",
                                        BMCWEB_REDFISH_SYSTEM_URI_NAME, device)
                        .buffer();
                pcieFunctionURI =
                    boost::urls::format(
                        "/redfish/v1/Systems/{}/PCIeDevices/{}/PCIeFunctions/{}",
                        BMCWEB_REDFISH_SYSTEM_URI_NAME, device, function)
                        .buffer();
            }
            else
            {
                pcieDeviceURI =
                    boost::urls::format("/redfish/v1/Chassis/{}/PCIeDevices/{}",
                                        chassisId, device)
                        .buffer();
                pcieFunctionURI =
                    boost::urls::format(
                        "/redfish/v1/Chassis/{}/PCIeDevices/{}/PCIeFunctions/{}",
                        chassisId, device, function)
                        .buffer();
            }

            asyncResp->res.jsonValue = {
                {"@odata.type", "#PCIeFunction.v1_2_0.PCIeFunction"},
                {"@odata.id", pcieFunctionURI},
                {"Name", "PCIe Function"},
                {"Id", function},
                {"FunctionId", std::stoi(function)},
                {"Links", {{"PCIeDevice", {{"@odata.id", pcieDeviceURI}}}}}};

            for (const auto& property : pcieDevProperties)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "Function" + function + "DeviceId")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        asyncResp->res.jsonValue["DeviceId"] = *value;
                    }
                }
                else if (propertyName == "Function" + function + "VendorId")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        asyncResp->res.jsonValue["VendorId"] = *value;
                    }
                }
                else if (propertyName == "Function" + function + "FunctionType")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        asyncResp->res.jsonValue["FunctionType"] = *value;
                    }
                }
                else if (propertyName == "Function" + function + "DeviceClass")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        asyncResp->res.jsonValue["DeviceClass"] = *value;
                    }
                }
                else if (propertyName == "Function" + function + "ClassCode")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        asyncResp->res.jsonValue["ClassCode"] = *value;
                    }
                }
                else if (propertyName == "Function" + function + "RevisionId")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        asyncResp->res.jsonValue["RevisionId"] = *value;
                    }
                }
                else if (propertyName == "Function" + function + "SubsystemId")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        asyncResp->res.jsonValue["SubsystemId"] = *value;
                    }
                }
                else if (propertyName ==
                         "Function" + function + "SubsystemVendorId")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value != nullptr)
                    {
                        asyncResp->res.jsonValue["SubsystemVendorId"] = *value;
                    }
                }
            }
        };
        std::string escapedPath = std::string(path) + "/" + device;
        dbus::utility::escapePathForDbus(escapedPath);
        crow::connections::systemBus->async_method_call(
            std::move(getPCIeDeviceCallback), service, escapedPath,
            "org.freedesktop.DBus.Properties", "GetAll", deviceIntf);
    }

    inline void getPCIeDeviceAsset(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
        const std::string& pcieDevicePath, const std::string& service)
    {
        sdbusplus::asio::getAllProperties(
            *crow::connections::systemBus, service, pcieDevicePath,
            "xyz.openbmc_project.Inventory.Decorator.Asset",
            [pcieDevicePath, asyncResp{asyncResp}](
                const boost::system::error_code& ec,
                const dbus::utility::DBusPropertiesMap& assetList) {
                if (ec)
                {
                    if (ec.value() != EBADR)
                    {
                        BMCWEB_LOG_ERROR("DBUS response error for Properties{}",
                                         ec.value());
                        messages::internalError(asyncResp->res);
                    }
                    return;
                }

                const std::string* manufacturer = nullptr;
                const std::string* model = nullptr;
                const std::string* partNumber = nullptr;
                const std::string* serialNumber = nullptr;
                const std::string* sparePartNumber = nullptr;

                const bool success = sdbusplus::unpackPropertiesNoThrow(
                    dbus_utils::UnpackErrorPrinter(), assetList, "Manufacturer",
                    manufacturer, "Model", model, "PartNumber", partNumber,
                    "SerialNumber", serialNumber, "SparePartNumber",
                    sparePartNumber);

                if (!success)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }

                if (manufacturer != nullptr)
                {
                    asyncResp->res.jsonValue["Manufacturer"] = *manufacturer;
                }
                if (model != nullptr)
                {
                    asyncResp->res.jsonValue["Model"] = *model;
                }

                if (partNumber != nullptr)
                {
                    asyncResp->res.jsonValue["PartNumber"] = *partNumber;
                }

                if (serialNumber != nullptr)
                {
                    asyncResp->res.jsonValue["SerialNumber"] = *serialNumber;
                }

                if (sparePartNumber != nullptr && !sparePartNumber->empty())
                {
                    asyncResp->res.jsonValue["SparePartNumber"] =
                        *sparePartNumber;
                }
            });
    }

    inline void addPCIeDeviceProperties(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
        const std::string& pcieDeviceId,
        const dbus::utility::DBusPropertiesMap& pcieDevProperties)
    {
        const std::string* generationInUse = nullptr;
        const std::string* generationSupported = nullptr;
        const size_t* lanesInUse = nullptr;
        const size_t* maxLanes = nullptr;

        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), pcieDevProperties,
            "GenerationInUse", generationInUse, "GenerationSupported",
            generationSupported, "LanesInUse", lanesInUse, "MaxLanes",
            maxLanes);

        if (!success)
        {
            messages::internalError(asyncResp->res);
            return;
        }

        if (generationInUse != nullptr)
        {
            /*TODO: Add support for Gen6 once DMTF schema is updated, to be
             * taken care while upstream sync*/
            // std::optional<pcie_device::PCIeTypes> redfishGenerationInUse =
            //     pcie_util::redfishPcieGenerationFromDbus(*generationInUse);

            std::optional<std::string> redfishGenerationInUse =
                pcie_util::redfishPcieGenerationStringFromDbus(
                    *generationInUse);

            if (!redfishGenerationInUse)
            {
                BMCWEB_LOG_WARNING("Unknown PCIe Device Generation: {}",
                                   *generationInUse);
            }
            else
            {
                /*TODO: Add support for Gen6 once DMTF schema is updated, to be
                 * taken care while upstream sync*/
                // if (*redfishGenerationInUse ==
                // pcie_device::PCIeTypes::Invalid)
                // {
                //     BMCWEB_LOG_ERROR("Invalid PCIe Device Generation: {}",
                //                      *generationInUse);
                //     messages::internalError(asyncResp->res);
                //     return;
                // }
                asyncResp->res.jsonValue["PCIeInterface"]["PCIeType"] =
                    *redfishGenerationInUse;
            }
        }

        if (generationSupported != nullptr)
        {
            /*TODO: Add support for Gen6 once DMTF schema is updated, to be
             * taken care while upstream sync*/
            // std::optional<pcie_device::PCIeTypes> redfishGenerationSupported
            // =
            //     pcie_util::redfishPcieGenerationFromDbus(*generationSupported);

            std::optional<std::string> redfishGenerationSupported =
                pcie_util::redfishPcieGenerationStringFromDbus(
                    *generationSupported);

            if (!redfishGenerationSupported)
            {
                BMCWEB_LOG_WARNING("Unknown PCIe Device Generation: {}",
                                   *generationSupported);
            }
            else
            {
                /*TODO: Add support for Gen6 once DMTF schema is updated, to be
                 * taken care while upstream sync*/
                // if (*redfishGenerationSupported ==
                // pcie_device::PCIeTypes::Invalid)
                // {
                //     BMCWEB_LOG_ERROR("Invalid PCIe Device Generation: {}",
                //                      *generationSupported);
                //     messages::internalError(asyncResp->res);
                //     return;
                // }
                asyncResp->res.jsonValue["PCIeInterface"]["MaxPCIeType"] =
                    *redfishGenerationSupported;
            }
        }

        if (lanesInUse != nullptr)
        {
            if (*lanesInUse == std::numeric_limits<size_t>::max())
            {
                // The default value of LanesInUse is "maxint", and the field
                // will be null if it is a default value.
                asyncResp->res.jsonValue["PCIeInterface"]["LanesInUse"] =
                    nullptr;
            }
            else
            {
                asyncResp->res.jsonValue["PCIeInterface"]["LanesInUse"] =
                    *lanesInUse;
            }
        }
        // The default value of MaxLanes is 0, and the field will be
        // left as off if it is a default value.
        if (maxLanes != nullptr && *maxLanes != 0)
        {
            asyncResp->res.jsonValue["PCIeInterface"]["MaxLanes"] = *maxLanes;
        }

        asyncResp->res.jsonValue["PCIeFunctions"]["@odata.id"] =
            boost::urls::format(
                "/redfish/v1/Systems/{}/PCIeDevices/{}/PCIeFunctions",
                BMCWEB_REDFISH_SYSTEM_URI_NAME, pcieDeviceId);
    }

    inline void requestRoutesSystemPCIeDeviceCollection(App & app)
    {
        /**
         * Functions triggers appropriate requests on DBus
         */
        BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/PCIeDevices/")
            .privileges(redfish::privileges::getPCIeDeviceCollection)
            .methods(boost::beast::http::verb::get)(
                [&app](const crow::Request& req,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       [[maybe_unused]] const std::string& systemName) {
                    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                    {
                        return;
                    }
                    asyncResp->res.jsonValue = {
                        {"@odata.type",
                         "#PCIeDeviceCollection.PCIeDeviceCollection"},
                        {"@odata.id",
                         "/redfish/v1/Systems/" +
                             std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                             "/PCIeDevices"},
                        {"Name", "PCIe Device Collection"},
                        {"Description", "Collection of PCIe Devices"},
                        {"Members", nlohmann::json::array()},
                        {"Members@odata.count", 0}};
                    nvidia_pcie_utils::getPCIeDeviceList(asyncResp, "Members");
                });
    }

    inline void requestRoutesSystemPCIeDevice(App & app)
    {
        BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/PCIeDevices/<str>/")
            .privileges(redfish::privileges::getPCIeDevice)
            .methods(boost::beast::http::verb::get)(
                [&app](const crow::Request& req,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       [[maybe_unused]] const std::string& systemName,
                       const std::string& device) {
                    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                    {
                        return;
                    }

                    asyncResp->res.jsonValue = {
                        {"@odata.type", "#PCIeDevice.v1_4_0.PCIeDevice"},
                        {"@odata.id",
                         "/redfish/v1/Systems/" +
                             std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                             "/PCIeDevices/" + device},
                        {"Name", "PCIe Device"},
                        {"Id", device},
                        {"PCIeFunctions",
                         {{"@odata.id",
                           "/redfish/v1/Systems/" +
                               std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                               "/PCIeDevices/" + device + "/PCIeFunctions"}}}};
                    getPCIeDevice(asyncResp, device);
                });
    }
    inline void getPCIeDeviceProperties(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
        const std::string& pcieDevicePath, const std::string& service,
        const std::function<void(const dbus::utility::DBusPropertiesMap&
                                     pcieDevProperties)>&& callback)
    {
        sdbusplus::asio::getAllProperties(
            *crow::connections::systemBus, service, pcieDevicePath,
            "xyz.openbmc_project.Inventory.Item.PCIeDevice",
            [asyncResp, callback](
                const boost::system::error_code& ec,
                const dbus::utility::DBusPropertiesMap& pcieDevProperties) {
                if (ec)
                {
                    if (ec.value() != EBADR)
                    {
                        BMCWEB_LOG_ERROR("DBUS response error for Properties");
                        messages::internalError(asyncResp->res);
                    }
                    return;
                }
                callback(pcieDevProperties);
            });
    }

    inline void addPCIeDeviceCommonProperties(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
        const std::string& pcieDeviceId)
    {
        asyncResp->res.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/PCIeDevice/PCIeDevice.json>; rel=describedby");
        asyncResp->res.jsonValue["@odata.type"] =
            "#PCIeDevice.v1_9_0.PCIeDevice";
        asyncResp->res.jsonValue["@odata.id"] =
            boost::urls::format("/redfish/v1/Systems/{}/PCIeDevices/{}",
                                BMCWEB_REDFISH_SYSTEM_URI_NAME, pcieDeviceId);
        asyncResp->res.jsonValue["Name"] = "PCIe Device";
        asyncResp->res.jsonValue["Id"] = pcieDeviceId;
        asyncResp->res.jsonValue["Status"]["State"] = resource::State::Enabled;
        asyncResp->res.jsonValue["Status"]["Health"] = resource::Health::OK;
    }

    inline void afterGetValidPcieDevicePath(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
        const std::string& pcieDeviceId, const std::string& pcieDevicePath,
        const std::string& service)
    {
        addPCIeDeviceCommonProperties(asyncResp, pcieDeviceId);
        getPCIeDeviceAsset(asyncResp, pcieDevicePath, service);
        getPCIeDeviceState(asyncResp, pcieDevicePath, service);
        getPCIeDeviceHealth(asyncResp, pcieDevicePath, service);
        getPCIeDeviceProperties(
            asyncResp, pcieDevicePath, service,
            std::bind_front(addPCIeDeviceProperties, asyncResp, pcieDeviceId));
        getPCIeDeviceSlotPath(
            pcieDevicePath, asyncResp,
            std::bind_front(afterGetPCIeDeviceSlotPath, asyncResp));
    }

    inline void handlePCIeDeviceGet(
        App & app, const crow::Request& req,
        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
        const std::string& systemName, const std::string& pcieDeviceId)
    {
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

        getValidPCIeDevicePath(pcieDeviceId, asyncResp,
                               std::bind_front(afterGetValidPcieDevicePath,
                                               asyncResp, pcieDeviceId));
    }

    // inline void requestRoutesSystemPCIeDevice(App& app)
    // {
    //     BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/PCIeDevices/<str>/")
    //         .privileges(redfish::privileges::getPCIeDevice)
    //         .methods(boost::beast::http::verb::get)(
    //             std::bind_front(handlePCIeDeviceGet, std::ref(app)));
    // }

    inline void addPCIeFunctionList(
        crow::Response & res, const std::string& pcieDeviceId,
        const dbus::utility::DBusPropertiesMap& pcieDevProperties)
    {
        nlohmann::json& pcieFunctionList = res.jsonValue["Members"];
        pcieFunctionList = nlohmann::json::array();
        static constexpr const int maxPciFunctionNum = 8;

        for (int functionNum = 0; functionNum < maxPciFunctionNum;
             functionNum++)
        {
            // Check if this function exists by
            // looking for a device ID
            std::string devIDProperty =
                "Function" + std::to_string(functionNum) + "DeviceId";
            const std::string* property = nullptr;
            for (const auto& propEntry : pcieDevProperties)
            {
                if (propEntry.first == devIDProperty)
                {
                    property = std::get_if<std::string>(&propEntry.second);
                    break;
                }
            }
            if (property == nullptr || property->empty())
            {
                continue;
            }

            nlohmann::json::object_t pcieFunction;
            pcieFunction["@odata.id"] = boost::urls::format(
                "/redfish/v1/Systems/{}/PCIeDevices/{}/PCIeFunctions/{}",
                BMCWEB_REDFISH_SYSTEM_URI_NAME, pcieDeviceId,
                std::to_string(functionNum));
            pcieFunctionList.emplace_back(std::move(pcieFunction));
        }
        res.jsonValue["PCIeFunctions@odata.count"] = pcieFunctionList.size();
    }

    inline void handlePCIeFunctionCollectionGet(
        App & app, const crow::Request& req,
        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
        const std::string& systemName, const std::string& pcieDeviceId)
    {
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

        getValidPCIeDevicePath(
            pcieDeviceId, asyncResp,
            [asyncResp, pcieDeviceId](const std::string& pcieDevicePath,
                                      const std::string& service) {
                asyncResp->res.addHeader(
                    boost::beast::http::field::link,
                    "</redfish/v1/JsonSchemas/PCIeFunctionCollection/PCIeFunctionCollection.json>; rel=describedby");
                asyncResp->res.jsonValue["@odata.type"] =
                    "#PCIeFunctionCollection.PCIeFunctionCollection";
                asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
                    "/redfish/v1/Systems/{}/PCIeDevices/{}/PCIeFunctions",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME, pcieDeviceId);
                asyncResp->res.jsonValue["Name"] = "PCIe Function Collection";
                asyncResp->res.jsonValue["Description"] =
                    "Collection of PCIe Functions for PCIe Device " +
                    pcieDeviceId;
                getPCIeDeviceProperties(
                    asyncResp, pcieDevicePath, service,
                    [asyncResp, pcieDeviceId](
                        const dbus::utility::DBusPropertiesMap&
                            pcieDevProperties) {
                        addPCIeFunctionList(asyncResp->res, pcieDeviceId,
                                            pcieDevProperties);
                    });
            });
    }

    inline void requestRoutesSystemPCIeFunctionCollection(App & app)
    {
        /**
         * Functions triggers appropriate requests on DBus
         */
        BMCWEB_ROUTE(
            app, "/redfish/v1/Systems/<str>/PCIeDevices/<str>/PCIeFunctions/")
            .privileges(redfish::privileges::getPCIeFunctionCollection)
            .methods(boost::beast::http::verb::get)(
                [&app](const crow::Request& req,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       [[maybe_unused]] const std::string& systemName,
                       const std::string& device) {
                    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                    {
                        return;
                    }

                    asyncResp->res.jsonValue = {
                        {"@odata.type",
                         "#PCIeFunctionCollection.PCIeFunctionCollection"},
                        {"@odata.id",
                         "/redfish/v1/Systems/" +
                             std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                             "/PCIeDevices/" + device + "/PCIeFunctions"},
                        {"Name", "PCIe Function Collection"},
                        {"Description",
                         "Collection of PCIe Functions for PCIe Device " +
                             device}};
                    getPCIeDeviceFunctionsList(asyncResp, device);
                });
    }

    inline bool validatePCIeFunctionId(
        uint64_t pcieFunctionId,
        const dbus::utility::DBusPropertiesMap& pcieDevProperties)
    {
        std::string functionName = "Function" + std::to_string(pcieFunctionId);
        std::string devIDProperty = functionName + "DeviceId";

        const std::string* devIdProperty = nullptr;
        for (const auto& property : pcieDevProperties)
        {
            if (property.first == devIDProperty)
            {
                devIdProperty = std::get_if<std::string>(&property.second);
                break;
            }
        }
        return (devIdProperty != nullptr && !devIdProperty->empty());
    }

    inline void requestRoutesSystemPCIeFunction(App & app)
    {
        BMCWEB_ROUTE(
            app,
            "/redfish/v1/Systems/<str>/PCIeDevices/<str>/PCIeFunctions/<str>/")
            .privileges({{"Login"}})
            .methods(boost::beast::http::verb::get)(
                [&app](const crow::Request& req,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       [[maybe_unused]] const std::string& systemName,
                       const std::string& device, const std::string& function) {
                    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                    {
                        return;
                    }
                    getPCIeDeviceFunction(asyncResp, device, function);
                });
    }

    inline void requestRoutesChassisPCIeDeviceCollection(App & app)
    {
        /**
         * Functions triggers appropriate requests on DBus
         */
        BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/PCIeDevices/")
            .privileges({{"Login"}})
            .methods(boost::beast::http::verb::
                         get)([&app](const crow::Request& req,
                                     const std::shared_ptr<bmcweb::AsyncResp>&
                                         asyncResp,
                                     const std::string& chassisId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                crow::connections::systemBus->async_method_call(
                    [asyncResp,
                     chassisId](const boost::system::error_code ec,
                                const std::vector<std::string>& chassisPaths) {
                        if (ec)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        for (const std::string& chassisPath : chassisPaths)
                        {
                            // Get the chassisId object
                            sdbusplus::message::object_path objPath(
                                chassisPath);
                            if (objPath.filename() != chassisId)
                            {
                                continue;
                            }
                            const std::string& chassisPCIePath =
                                "/xyz/openbmc_project/inventory/system/chassis/" +
                                chassisId + "/PCIeDevices";
                            asyncResp->res.jsonValue = {
                                {"@odata.type",
                                 "#PCIeDeviceCollection.PCIeDeviceCollection"},
                                {"@odata.id", "/redfish/v1/Chassis/" +
                                                  chassisId + "/PCIeDevices"},
                                {"Name", "PCIe Device Collection"},
                                {"Description", "Collection of PCIe Devices"},
                                {"Members", nlohmann::json::array()},
                                {"Members@odata.count", 0}};
                            nvidia_pcie_utils::getPCIeDeviceList(
                                asyncResp, "Members", chassisPCIePath,
                                chassisId);
                            return;
                        }
                        messages::resourceNotFound(asyncResp->res,
                                                   "#Chassis.v1_15_0.Chassis",
                                                   chassisId);
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                    "/xyz/openbmc_project/inventory", 0,
                    std::array<const char*, 1>{
                        "xyz.openbmc_project.Inventory.Item.Chassis"});
            });
    }
    inline void addPCIeFunctionProperties(
        crow::Response & resp, uint64_t pcieFunctionId,
        const dbus::utility::DBusPropertiesMap& pcieDevProperties)
    {
        std::string functionName = "Function" + std::to_string(pcieFunctionId);
        for (const auto& property : pcieDevProperties)
        {
            const std::string* strProperty =
                std::get_if<std::string>(&property.second);
            if (strProperty == nullptr)
            {
                continue;
            }
            if (property.first == functionName + "DeviceId")
            {
                resp.jsonValue["DeviceId"] = *strProperty;
            }
            if (property.first == functionName + "VendorId")
            {
                resp.jsonValue["VendorId"] = *strProperty;
            }
            // TODO: FunctionType and DeviceClass are Redfish enums. The D-Bus
            // property strings should be mapped correctly to ensure these
            // strings are Redfish enum values. For now just check for empty.
            if (property.first == functionName + "FunctionType")
            {
                if (!strProperty->empty())
                {
                    resp.jsonValue["FunctionType"] = *strProperty;
                }
            }
            if (property.first == functionName + "DeviceClass")
            {
                if (!strProperty->empty())
                {
                    resp.jsonValue["DeviceClass"] = *strProperty;
                }
            }
            if (property.first == functionName + "ClassCode")
            {
                resp.jsonValue["ClassCode"] = *strProperty;
            }
            if (property.first == functionName + "RevisionId")
            {
                resp.jsonValue["RevisionId"] = *strProperty;
            }
            if (property.first == functionName + "SubsystemId")
            {
                resp.jsonValue["SubsystemId"] = *strProperty;
            }
            if (property.first == functionName + "SubsystemVendorId")
            {
                resp.jsonValue["SubsystemVendorId"] = *strProperty;
            }
        }
    }

    inline void addPCIeFunctionCommonProperties(
        crow::Response & resp, const std::string& pcieDeviceId,
        uint64_t pcieFunctionId)
    {
        resp.addHeader(
            boost::beast::http::field::link,
            "</redfish/v1/JsonSchemas/PCIeFunction/PCIeFunction.json>; rel=describedby");
        resp.jsonValue["@odata.type"] = "#PCIeFunction.v1_2_3.PCIeFunction";
        resp.jsonValue["@odata.id"] = boost::urls::format(
            "/redfish/v1/Systems/{}/PCIeDevices/{}/PCIeFunctions/{}",
            BMCWEB_REDFISH_SYSTEM_URI_NAME, pcieDeviceId,
            std::to_string(pcieFunctionId));
        resp.jsonValue["Name"] = "PCIe Function";
        resp.jsonValue["Id"] = std::to_string(pcieFunctionId);
        resp.jsonValue["FunctionId"] = pcieFunctionId;
        resp.jsonValue["Links"]["PCIeDevice"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Systems/{}/PCIeDevices/{}",
                                BMCWEB_REDFISH_SYSTEM_URI_NAME, pcieDeviceId);
    }

    inline void handlePCIeFunctionGet(
        App & app, const crow::Request& req,
        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
        const std::string& systemName, const std::string& pcieDeviceId,
        const std::string& pcieFunctionIdStr)
    {
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
        std::string_view pcieFunctionIdView = pcieFunctionIdStr;

        uint64_t pcieFunctionId = 0;
        std::from_chars_result result =
            std::from_chars(pcieFunctionIdView.begin(),
                            pcieFunctionIdView.end(), pcieFunctionId);
        if (result.ec != std::errc{} || result.ptr != pcieFunctionIdView.end())
        {
            messages::resourceNotFound(asyncResp->res, "PCIeFunction",
                                       pcieFunctionIdStr);
            return;
        }

        getValidPCIeDevicePath(
            pcieDeviceId, asyncResp,
            [asyncResp, pcieDeviceId, pcieFunctionId](
                const std::string& pcieDevicePath, const std::string& service) {
                getPCIeDeviceProperties(
                    asyncResp, pcieDevicePath, service,
                    [asyncResp, pcieDeviceId, pcieFunctionId](
                        const dbus::utility::DBusPropertiesMap&
                            pcieDevProperties) {
                        addPCIeFunctionCommonProperties(
                            asyncResp->res, pcieDeviceId, pcieFunctionId);
                        addPCIeFunctionProperties(
                            asyncResp->res, pcieFunctionId, pcieDevProperties);
                    });
            });
    }

    inline void requestRoutesChassisPCIeDevice(App & app)
    {
        BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/PCIeDevices/<str>/")
            .privileges({{"Login"}})
            .methods(boost::beast::http::verb::
                         get)([&app](const crow::Request& req,
                                     const std::shared_ptr<bmcweb::AsyncResp>&
                                         asyncResp,
                                     const std::string& chassisId,
                                     const std::string& device) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                crow::connections::systemBus->async_method_call(
                    [asyncResp, chassisId,
                     device](const boost::system::error_code ec,
                             const std::vector<std::string>& chassisPaths) {
                        if (ec)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        for (const std::string& chassisPath : chassisPaths)
                        {
                            // Get the chassisId object
                            sdbusplus::message::object_path objPath(
                                chassisPath);
                            if (objPath.filename() != chassisId)
                            {
                                continue;
                            }
                            const std::string chassisPCIePath =
                                std::string(
                                    "/xyz/openbmc_project/inventory/system/chassis/")
                                    .append(chassisId)
                                    .append("/PCIeDevices");
                            const std::string chassisPCIeDevicePath =
                                std::string(chassisPCIePath)
                                    .append("/")
                                    .append(device);
                            const std::array<const char*, 1> interface = {
                                "xyz.openbmc_project.Inventory.Item.PCIeDevice"};
                            // Get Inventory Service
                            crow::connections::systemBus->async_method_call(
                                [asyncResp, device, chassisPCIePath, interface,
                                 chassisId, chassisPCIeDevicePath, chassisPath](
                                    const boost::system::error_code ec,
                                    const GetSubTreeType& subtree) {
                                    if (ec)
                                    {
                                        BMCWEB_LOG_DEBUG("DBUS response error");
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }
                                    // Iterate over all retrieved ObjectPaths.
                                    for (const std::pair<
                                             std::string,
                                             std::vector<std::pair<
                                                 std::string,
                                                 std::vector<std::string>>>>&
                                             object : subtree)
                                    {
                                        if (object.first !=
                                            chassisPCIeDevicePath)
                                        {
                                            continue;
                                        }
                                        const std::vector<std::pair<
                                            std::string,
                                            std::vector<std::string>>>&
                                            connectionNames = object.second;
                                        if (connectionNames.size() < 1)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Got 0 Connection names");
                                            continue;
                                        }
                                        std::string pcieDeviceURI =
                                            "/redfish/v1/Chassis/";
                                        pcieDeviceURI += chassisId;
                                        pcieDeviceURI += "/PCIeDevices/";
                                        pcieDeviceURI += device;
                                        std::string pcieFunctionURI =
                                            pcieDeviceURI;
                                        pcieFunctionURI += "/PCIeFunctions";
                                        asyncResp->res.jsonValue = {
                                            {"@odata.type",
                                             "#PCIeDevice.v1_14_0.PCIeDevice"},
                                            {"@odata.id", pcieDeviceURI},
                                            {"Name", "PCIe Device"},
                                            {"Id", device},
                                            {"PCIeFunctions",
                                             {{"@odata.id", pcieFunctionURI}}}};
                                        const std::string& connectionName =
                                            connectionNames[0].first;
                                        const std::vector<std::string>&
                                            interfaces2 =
                                                connectionNames[0].second;
                                        getPCIeDevice(
                                            asyncResp, device, chassisPCIePath,
                                            connectionName, interface[0]);

                                        // get health by association
                                        redfish::nvidia_chassis_utils::
                                            getHealthByAssociation(
                                                asyncResp,
                                                chassisPCIePath + "/" + device,
                                                "chassis", device);

                                        // Get asset properties
                                        if (std::find(interfaces2.begin(),
                                                      interfaces2.end(),
                                                      assetInterface) !=
                                            interfaces2.end())
                                        {
                                            getPCIeDeviceAssetData(
                                                asyncResp, device,
                                                chassisPCIePath,
                                                connectionName);
                                        }
                                        // Get UUID
                                        if (std::find(interfaces2.begin(),
                                                      interfaces2.end(),
                                                      uuidInterface) !=
                                            interfaces2.end())
                                        {
                                            getPCIeDeviceUUID(asyncResp, device,
                                                              chassisPCIePath,
                                                              connectionName);
                                        }
                                        // Device state
                                        if (std::find(interfaces2.begin(),
                                                      interfaces2.end(),
                                                      stateInterface) !=
                                            interfaces2.end())
                                        {
                                            getPCIeDeviceState(asyncResp,
                                                               device,
                                                               chassisPCIePath,
                                                               connectionName);
                                        }
                                        redfish::nvidia_pcie_utils::
                                            getFabricSwitchLink(asyncResp,
                                                                chassisPath);
                                        if constexpr (
                                            !BMCWEB_DISABLE_CONDITIONS_ARRAY)
                                        {
                                            redfish::conditions_utils::
                                                populateServiceConditions(
                                                    asyncResp, device);
                                        }
                                        if constexpr (
                                            BMCWEB_NVIDIA_OEM_PROPERTIES)
                                        {
                                            nlohmann::json& oem =
                                                asyncResp->res
                                                    .jsonValue["Oem"]["Nvidia"];
                                            oem["@odata.type"] =
                                                "#NvidiaPCIeDevice.v1_2_0.NvidiaPCIeDevice";
                                            // Baseboard PCIeDevices Oem
                                            // properties
                                            if (std::find(
                                                    interfaces2.begin(),
                                                    interfaces2.end(),
                                                    pcieClockReferenceIntf) !=
                                                interfaces2.end())
                                            {
                                                getPCIeDeviceClkRefOem(
                                                    asyncResp, device,
                                                    chassisPCIePath,
                                                    connectionName);
                                            }

                                            if (std::find(
                                                    interfaces2.begin(),
                                                    interfaces2.end(),
                                                    pcieAerErrorStatusIntf) !=
                                                interfaces2.end())
                                            {
                                                redfish::nvidia_pcie_utils::
                                                    getAerErrorStatusOem(
                                                        asyncResp, device,
                                                        chassisPCIePath,
                                                        connectionName);
                                                asyncResp->res.jsonValue
                                                    ["Actions"]["Oem"]
                                                    ["#NvidiaPCIeDevice.ClearAERErrorStatus"]
                                                    ["target"] =
                                                    pcieDeviceURI +
                                                    "/Actions/Oem/NvidiaPCIeDevice.ClearAERErrorStatus";
                                            }

                                            getPCIeLTssmState(asyncResp, device,
                                                              chassisPCIePath,
                                                              connectionName);

                                            // Baseboard PCIeDevices nvlink Oem
                                            // properties
                                            if (std::find(
                                                    interfaces2.begin(),
                                                    interfaces2.end(),
                                                    nvlinkClockReferenceIntf) !=
                                                interfaces2.end())
                                            {
                                                getPCIeDeviceNvLinkClkRefOem(
                                                    asyncResp, device,
                                                    chassisPCIePath,
                                                    connectionName);
                                            }
                                        }
                                        return;
                                    }
                                    messages::resourceNotFound(
                                        asyncResp->res,
                                        "#PCIeDevice.v1_14_0.PCIeDevice",
                                        device);
                                },
                                "xyz.openbmc_project.ObjectMapper",
                                "/xyz/openbmc_project/object_mapper",
                                "xyz.openbmc_project.ObjectMapper",
                                "GetSubTree", "/xyz/openbmc_project/inventory",
                                0, interface);
                            return;
                        }
                        messages::resourceNotFound(asyncResp->res,
                                                   "#Chassis.v1_15_0.Chassis",
                                                   chassisId);
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                    "/xyz/openbmc_project/inventory", 0,
                    std::array<const char*, 1>{
                        "xyz.openbmc_project.Inventory.Item.Chassis"});
            });
    }

    inline void requestRoutesChassisPCIeFunctionCollection(App & app)
    {
        BMCWEB_ROUTE(
            app, "/redfish/v1/Chassis/<str>/PCIeDevices/<str>/PCIeFunctions/")
            .privileges({{"Login"}})
            .methods(boost::beast::http::verb::
                         get)([&app](const crow::Request& req,
                                     const std::shared_ptr<bmcweb::AsyncResp>&
                                         asyncResp,
                                     const std::string& chassisId,
                                     const std::string& device) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                crow::connections::systemBus->async_method_call(
                    [asyncResp, chassisId,
                     device](const boost::system::error_code ec,
                             const std::vector<std::string>& chassisPaths) {
                        if (ec)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        for (const std::string& chassisPath : chassisPaths)
                        {
                            // Get the chassisId object
                            sdbusplus::message::object_path objPath(
                                chassisPath);
                            if (objPath.filename() != chassisId)
                            {
                                continue;
                            }
                            const std::string chassisPCIePath =
                                std::string(
                                    "/xyz/openbmc_project/inventory/system/chassis/")
                                    .append(chassisId)
                                    .append("/PCIeDevices");
                            const std::string chassisPCIeDevicePath =
                                std::string(chassisPCIePath)
                                    .append("/")
                                    .append(device);
                            const std::array<const char*, 1> interface = {
                                "xyz.openbmc_project.Inventory.Item.PCIeDevice"};
                            // Response
                            std::string pcieFunctionURI =
                                "/redfish/v1/Chassis/";
                            pcieFunctionURI += chassisId;
                            pcieFunctionURI += "/PCIeDevices/";
                            pcieFunctionURI += device;
                            pcieFunctionURI += "/PCIeFunctions";
                            asyncResp->res.jsonValue = {
                                {"@odata.type",
                                 "#PCIeFunctionCollection.PCIeFunctionCollection"},
                                {"@odata.id", pcieFunctionURI},
                                {"Name", "PCIe Function Collection"},
                                {"Description",
                                 "Collection of PCIe Functions for PCIe Device " +
                                     device}};
                            // Get Inventory Service
                            crow::connections::systemBus->async_method_call(
                                [asyncResp, device, chassisPCIePath, interface,
                                 chassisId, chassisPCIeDevicePath](
                                    const boost::system::error_code ec,
                                    const GetSubTreeType& subtree) {
                                    if (ec)
                                    {
                                        BMCWEB_LOG_DEBUG("DBUS response error");
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }
                                    // Iterate over all retrieved ObjectPaths.
                                    for (const std::pair<
                                             std::string,
                                             std::vector<std::pair<
                                                 std::string,
                                                 std::vector<std::string>>>>&
                                             object : subtree)
                                    {
                                        if (object.first !=
                                            chassisPCIeDevicePath)
                                        {
                                            continue;
                                        }
                                        const std::vector<std::pair<
                                            std::string,
                                            std::vector<std::string>>>&
                                            connectionNames = object.second;
                                        if (connectionNames.size() < 1)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Got 0 Connection names");
                                            continue;
                                        }
                                        const std::string& connectionName =
                                            connectionNames[0].first;
                                        getPCIeDeviceFunctionsList(
                                            asyncResp, device, chassisPCIePath,
                                            connectionName, interface[0],
                                            chassisId);
                                        return;
                                    }
                                    messages::resourceNotFound(
                                        asyncResp->res,
                                        "#PCIeDevice.v1_14_0.PCIeDevice",
                                        device);
                                },
                                "xyz.openbmc_project.ObjectMapper",
                                "/xyz/openbmc_project/object_mapper",
                                "xyz.openbmc_project.ObjectMapper",
                                "GetSubTree", "/xyz/openbmc_project/inventory",
                                0, interface);
                            return;
                        }
                        messages::resourceNotFound(asyncResp->res,
                                                   "#Chassis.v1_15_0.Chassis",
                                                   chassisId);
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                    "/xyz/openbmc_project/inventory", 0,
                    std::array<const char*, 1>{
                        "xyz.openbmc_project.Inventory.Item.Chassis"});
            });
    }

    inline void requestRoutesChassisPCIeFunction(App & app)
    {
        BMCWEB_ROUTE(
            app,
            "/redfish/v1/Chassis/<str>/PCIeDevices/<str>/PCIeFunctions/<str>/")
            .privileges({{"Login"}})
            .methods(boost::beast::http::verb::
                         get)([&app](const crow::Request& req,
                                     const std::shared_ptr<bmcweb::AsyncResp>&
                                         asyncResp,
                                     const std::string& chassisId,
                                     const std::string& device,
                                     const std::string& function) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                crow::connections::systemBus->async_method_call(
                    [asyncResp, chassisId, device,
                     function](const boost::system::error_code ec,
                               const std::vector<std::string>& chassisPaths) {
                        if (ec)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        for (const std::string& chassisPath : chassisPaths)
                        {
                            // Get the chassisId object
                            sdbusplus::message::object_path objPath(
                                chassisPath);
                            if (objPath.filename() != chassisId)
                            {
                                continue;
                            }
                            const std::string chassisPCIePath =
                                std::string(
                                    "/xyz/openbmc_project/inventory/system/chassis/")
                                    .append(chassisId)
                                    .append("/PCIeDevices");
                            const std::string chassisPCIeDevicePath =
                                std::string(chassisPCIePath)
                                    .append("/")
                                    .append(device);
                            const std::array<const char*, 1> interface = {
                                "xyz.openbmc_project.Inventory.Item.PCIeDevice"};
                            // Get Inventory Service
                            crow::connections::systemBus->async_method_call(
                                [asyncResp, device, function, chassisPCIePath,
                                 interface, chassisId, chassisPCIeDevicePath](
                                    const boost::system::error_code ec,
                                    const GetSubTreeType& subtree) {
                                    if (ec)
                                    {
                                        BMCWEB_LOG_DEBUG("DBUS response error");
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }
                                    // Iterate over all retrieved ObjectPaths.
                                    for (const std::pair<
                                             std::string,
                                             std::vector<std::pair<
                                                 std::string,
                                                 std::vector<std::string>>>>&
                                             object : subtree)
                                    {
                                        if (object.first !=
                                            chassisPCIeDevicePath)
                                        {
                                            continue;
                                        }
                                        const std::vector<std::pair<
                                            std::string,
                                            std::vector<std::string>>>&
                                            connectionNames = object.second;
                                        if (connectionNames.size() < 1)
                                        {
                                            BMCWEB_LOG_ERROR(
                                                "Got 0 Connection names");
                                            continue;
                                        }
                                        const std::string& connectionName =
                                            connectionNames[0].first;
                                        getPCIeDeviceFunction(
                                            asyncResp, device, function,
                                            chassisPCIePath, connectionName,
                                            chassisId, interface[0]);
                                        return;
                                    }
                                    messages::resourceNotFound(
                                        asyncResp->res,
                                        "#PCIeDevice.v1_14_0.PCIeDevice",
                                        device);
                                },
                                "xyz.openbmc_project.ObjectMapper",
                                "/xyz/openbmc_project/object_mapper",
                                "xyz.openbmc_project.ObjectMapper",
                                "GetSubTree", "/xyz/openbmc_project/inventory",
                                0, interface);
                            return;
                        }
                        messages::resourceNotFound(asyncResp->res,
                                                   "#Chassis.v1_15_0.Chassis",
                                                   chassisId);
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                    "/xyz/openbmc_project/inventory", 0,
                    std::array<const char*, 1>{
                        "xyz.openbmc_project.Inventory.Item.Chassis"});
            });
        // Todo Telemetry team - Need to migrate downstream code to this
        // function BMCWEB_ROUTE(
        //     app,
        //     "/redfish/v1/Systems/<str>/PCIeDevices/<str>/PCIeFunctions/<str>/")
        //     .privileges(redfish::privileges::getPCIeFunction)
        //     .methods(boost::beast::http::verb::get)(
        //         std::bind_front(handlePCIeFunctionGet, std::ref(app)));
    }

    inline void requestRoutesClearPCIeAerErrorStatus(App & app)
    {
        BMCWEB_ROUTE(
            app,
            "/redfish/v1/Chassis/<str>/PCIeDevices/<str>/Actions/Oem/NvidiaPCIeDevice.ClearAERErrorStatus/")
            .privileges({{"Login"}})
            .methods(boost::beast::http::verb::post)(
                [&app](const crow::Request& req,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& chassisId,
                       const std::string& device) {
                    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                    {
                        return;
                    }

                    redfish::nvidia_pcie_utils::postClearAerErrorStatus(
                        asyncResp, chassisId, device);
                });
    }

} // namespace redfish
