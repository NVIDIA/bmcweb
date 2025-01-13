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
#include "generated/enums/action_info.hpp"
#include "generated/enums/chassis.hpp"
#include "generated/enums/resource.hpp"
#include "led.hpp"
#include "nvidia_debug_token.hpp"
#include "nvidia_protected_component.hpp"
#include "query.hpp"
#include "redfish_util.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/health_utils.hpp"
#include "utils/json_utils.hpp"

#include <boost/container/flat_map.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <dbus_utility.hpp>
#include <erot_chassis.hpp>
#include <openbmc_dbus_rest.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/chassis_utils.hpp>
#include <utils/conditions_utils.hpp>
#include <utils/nvidia_chassis_util.hpp>

#include <array>
#include <ranges>
#include <string_view>

namespace redfish
{

/**
 * @brief Retrieves resources over dbus to link to the chassis
 *
 * @param[in] asyncResp  - Shared pointer for completing asynchronous
 * calls
 * @param[in] path       - Chassis dbus path to look for the storage.
 *
 * Calls the Association endpoints on the path + "/storage" and add the link of
 * json["Links"]["Storage@odata.count"] =
 *    {"@odata.id", "/redfish/v1/Storage/" + resourceId}
 *
 * @return None.
 */
inline void getStorageLink(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const sdbusplus::message::object_path& path)
{
    sdbusplus::asio::getProperty<std::vector<std::string>>(
        *crow::connections::systemBus, "xyz.openbmc_project.ObjectMapper",
        (path / "storage").str, "xyz.openbmc_project.Association", "endpoints",
        [asyncResp](const boost::system::error_code& ec,
                    const std::vector<std::string>& storageList) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("getStorageLink got DBUS response error");
                return;
            }

            nlohmann::json::array_t storages;
            for (const std::string& storagePath : storageList)
            {
                std::string id =
                    sdbusplus::message::object_path(storagePath).filename();
                if (id.empty())
                {
                    continue;
                }

                nlohmann::json::object_t storage;
                storage["@odata.id"] =
                    boost::urls::format("/redfish/v1/Systems/{}/Storage/{}",
                                        BMCWEB_REDFISH_SYSTEM_URI_NAME, id);
                storages.emplace_back(std::move(storage));
            }
            asyncResp->res.jsonValue["Links"]["Storage@odata.count"] =
                storages.size();
            asyncResp->res.jsonValue["Links"]["Storage"] = std::move(storages);
        });
}

/**
 * @brief Retrieves chassis state properties over dbus
 *
 * @param[in] asyncResp - Shared pointer for completing asynchronous calls.
 *
 * @return None.
 */
inline void getChassisState(std::shared_ptr<bmcweb::AsyncResp> asyncResp)
{
    // crow::connections::systemBus->async_method_call(
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, "xyz.openbmc_project.State.Chassis",
        "/xyz/openbmc_project/state/chassis0",
        "xyz.openbmc_project.State.Chassis", "CurrentPowerState",
        [asyncResp{std::move(asyncResp)}](const boost::system::error_code& ec,
                                          const std::string& chassisState) {
            if (ec)
            {
                if (ec == boost::system::errc::host_unreachable)
                {
                    // Service not available, no error, just don't return
                    // chassis state info
                    BMCWEB_LOG_DEBUG("Service not available {}", ec);
                    return;
                }
                BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }

            BMCWEB_LOG_DEBUG("Chassis state: {}", chassisState);
            // Verify Chassis State
            if (chassisState ==
                "xyz.openbmc_project.State.Chassis.PowerState.On")
            {
                asyncResp->res.jsonValue["PowerState"] =
                    resource::PowerState::On;
                asyncResp->res.jsonValue["Status"]["State"] =
                    resource::State::Enabled;
            }
            else if (chassisState ==
                     "xyz.openbmc_project.State.Chassis.PowerState.Off")
            {
                asyncResp->res.jsonValue["PowerState"] =
                    resource::PowerState::Off;
                asyncResp->res.jsonValue["Status"]["State"] =
                    resource::State::StandbyOffline;
            }
        });
}

/**
 * @brief Wrapper function to retrieves chassis state properties over dbus
 *
 * @param[in] asyncResp - Shared pointer for completing asynchronous calls.
 * @param[in] propertiesList - D-Bus properties list.
 * @param[in] service - D-Bus service to query.
 * @param[in] path - D-Bus object to query.
 *
 * @return None.
 */
inline void getChassisStateWrapper(
    std::shared_ptr<bmcweb::AsyncResp> asyncResp,
    const dbus::utility::DBusPropertiesMap& propertiesList, std::string service,
    std::string path)
{
    const std::string* state = nullptr;
    const std::string* currentPowerState = nullptr;
    sdbusplus::unpackPropertiesNoThrow(dbus_utils::UnpackErrorPrinter(),
                                       propertiesList, "CurrentPowerState",
                                       currentPowerState, "State", state);

    if ((service == "xyz.openbmc_project.State.Chassis" &&
         path == "/xyz/openbmc_project/state/chassis0") ||
        state == nullptr || currentPowerState == nullptr)
    {
        getChassisState(asyncResp);
    }
    else
    {
        asyncResp->res.jsonValue["Status"]["State"] =
            redfish::chassis_utils::getPowerStateType(*state);
        asyncResp->res.jsonValue["PowerState"] =
            currentPowerState->substr(currentPowerState->rfind('.') + 1);
    }
}

/**
 * Retrieves physical security properties over dbus
 */
inline void handlePhysicalSecurityGetSubTree(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
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
            const auto& service = object.second.front();

            BMCWEB_LOG_DEBUG("Get intrusion status by service ");

            sdbusplus::asio::getProperty<std::string>(
                *crow::connections::systemBus, service.first, object.first,
                "xyz.openbmc_project.Chassis.Intrusion", "Status",
                [asyncResp](const boost::system::error_code& ec1,
                            const std::string& value) {
                    if (ec1)
                    {
                        // do not add err msg in redfish response, because this
                        // is not
                        //     mandatory property
                        BMCWEB_LOG_ERROR("DBUS response error {}", ec1);
                        return;
                    }
                    asyncResp->res.jsonValue["PhysicalSecurity"]
                                            ["IntrusionSensorNumber"] = 1;
                    asyncResp->res
                        .jsonValue["PhysicalSecurity"]["IntrusionSensor"] =
                        value;
                });

            return;
        }
    }
}

inline void handleChassisCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] =
        "#ChassisCollection.ChassisCollection";
    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/Chassis";
    asyncResp->res.jsonValue["Name"] = "Chassis Collection";

    constexpr std::array<std::string_view, 2> interfaces{
        "xyz.openbmc_project.Inventory.Item.Board",
        "xyz.openbmc_project.Inventory.Item.Chassis"};
    collection_util::getCollectionMembers(
        asyncResp, boost::urls::url("/redfish/v1/Chassis"), interfaces,
        "/xyz/openbmc_project/inventory");
}

inline void getChassisContainedBy(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& upstreamChassisPaths)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("DBUS response error {}", ec);
            messages::internalError(asyncResp->res);
        }
        return;
    }
    if (upstreamChassisPaths.empty())
    {
        return;
    }
    if (upstreamChassisPaths.size() > 1)
    {
        BMCWEB_LOG_ERROR("{} is contained by multiple chassis", chassisId);
        messages::internalError(asyncResp->res);
        return;
    }

    sdbusplus::message::object_path upstreamChassisPath(
        upstreamChassisPaths[0]);
    std::string upstreamChassis = upstreamChassisPath.filename();
    if (upstreamChassis.empty())
    {
        BMCWEB_LOG_WARNING("Malformed upstream Chassis path {} on {}",
                           upstreamChassisPath.str, chassisId);
        return;
    }

    asyncResp->res.jsonValue["Links"]["ContainedBy"]["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}", upstreamChassis);
}

inline void getChassisContains(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& downstreamChassisPaths)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("DBUS response error {}", ec);
            messages::internalError(asyncResp->res);
        }
        return;
    }
    if (downstreamChassisPaths.empty())
    {
        return;
    }
    nlohmann::json& jValue = asyncResp->res.jsonValue["Links"]["Contains"];
    if (!jValue.is_array())
    {
        // Create the array if it was empty
        jValue = nlohmann::json::array();
    }
    for (const auto& p : downstreamChassisPaths)
    {
        sdbusplus::message::object_path downstreamChassisPath(p);
        std::string downstreamChassis = downstreamChassisPath.filename();
        if (downstreamChassis.empty())
        {
            BMCWEB_LOG_WARNING("Malformed downstream Chassis path {} on {}",
                               downstreamChassisPath.str, chassisId);
            continue;
        }
        nlohmann::json link;
        link["@odata.id"] =
            boost::urls::format("/redfish/v1/Chassis/{}", downstreamChassis);
        jValue.push_back(std::move(link));
    }
    asyncResp->res.jsonValue["Links"]["Contains@odata.count"] = jValue.size();
}

inline void getChassisConnectivity(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& chassisPath)
{
    BMCWEB_LOG_DEBUG("Get chassis connectivity");

    constexpr std::array<std::string_view, 2> interfaces{
        "xyz.openbmc_project.Inventory.Item.Board",
        "xyz.openbmc_project.Inventory.Item.Chassis"};

    dbus::utility::getAssociatedSubTreePaths(
        chassisPath + "/contained_by",
        sdbusplus::message::object_path("/xyz/openbmc_project/inventory"), 0,
        interfaces,
        std::bind_front(getChassisContainedBy, asyncResp, chassisId));

    dbus::utility::getAssociatedSubTreePaths(
        chassisPath + "/containing",
        sdbusplus::message::object_path("/xyz/openbmc_project/inventory"), 0,
        interfaces, std::bind_front(getChassisContains, asyncResp, chassisId));
}

/**
 * ChassisCollection derived class for delivering Chassis Collection Schema
 *  Functions triggers appropriate requests on DBus
 */
inline void requestRoutesChassisCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/")
        .privileges(redfish::privileges::getChassisCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleChassisCollectionGet, std::ref(app)));
}

inline void getChassisLocationCode(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.LocationCode", "LocationCode",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& property) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error for Location");
                messages::internalError(asyncResp->res);
                return;
            }

            asyncResp->res
                .jsonValue["Location"]["PartLocation"]["ServiceLabel"] =
                property;
        });
}

inline void getChassisUUID(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Common.UUID", "UUID",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& chassisUUID) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error for UUID");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["UUID"] = chassisUUID;
        });
}

inline void handleDecoratorAssetProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& path,
    const dbus::utility::DBusPropertiesMap& propertiesList)
{
    const std::string* partNumber = nullptr;
    const std::string* serialNumber = nullptr;
    const std::string* manufacturer = nullptr;
    const std::string* model = nullptr;
    const std::string* sparePartNumber = nullptr;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), propertiesList, "PartNumber",
        partNumber, "SerialNumber", serialNumber, "Manufacturer", manufacturer,
        "Model", model, "SparePartNumber", sparePartNumber);

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

    asyncResp->res.jsonValue["Name"] = chassisId;
    asyncResp->res.jsonValue["Id"] = chassisId;

    if constexpr (BMCWEB_REDFISH_ALLOW_DEPRECATED_POWER_THERMAL)
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
    asyncResp->res.jsonValue["Status"]["State"] = resource::State::Enabled;

    if constexpr (BMCWEB_REDFISH_LEAK_DETECT)
    {
        // PolicyCollection
        asyncResp->res.jsonValue["Policies"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Chassis/{}/Policies", chassisId);
    }

    nlohmann::json::array_t computerSystems;
    nlohmann::json::object_t system;
    system["@odata.id"] =
        std::format("/redfish/v1/Systems/{}", BMCWEB_REDFISH_SYSTEM_URI_NAME);
    computerSystems.emplace_back(std::move(system));
    asyncResp->res.jsonValue["Links"]["ComputerSystems"] =
        std::move(computerSystems);

    nlohmann::json::array_t managedBy;
    nlohmann::json::object_t manager;
    manager["@odata.id"] = boost::urls::format("/redfish/v1/Managers/{}",
                                               BMCWEB_REDFISH_MANAGER_URI_NAME);
    managedBy.emplace_back(std::move(manager));
    asyncResp->res.jsonValue["Links"]["ManagedBy"] = std::move(managedBy);
    getChassisState(asyncResp);
    getStorageLink(asyncResp, path);
}

inline void handleChassisGetSubTree(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    bool isFoundChassisObject = false;
    // Iterate over all retrieved ObjectPaths.
    for (const std::pair<
             std::string,
             std::vector<std::pair<std::string, std::vector<std::string>>>>&
             object : subtree)
    {
        const std::string& path = object.first;
        const std::vector<std::pair<std::string, std::vector<std::string>>>&
            connectionNames = object.second;

        sdbusplus::message::object_path objPath(path);
        if (objPath.filename() != chassisId)
        {
            continue;
        }
        redfish::nvidia_chassis_utils::handleFruAssetInformation(
            asyncResp, chassisId, path);
        getChassisConnectivity(asyncResp, chassisId, path);

        if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
        {
            // get debug token resource
            redfish::debug_token::getChassisDebugToken(asyncResp, chassisId);
        }
        if constexpr (BMCWEB_HEALTH_ROLLUP_ALTERNATIVE)
        {
            std::shared_ptr<HealthRollup> health =
                std::make_shared<HealthRollup>(
                    objPath, [asyncResp](const std::string& rootHealth,
                                         const std::string& healthRollup) {
                        asyncResp->res.jsonValue["Status"]["Health"] =
                            rootHealth;
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
            /** NOTES: This is a temporary solution to avoid performance issues
             * may impact other Redfish services. Please call for architecture
             * decisions from all NvBMC teams if want to use it in other places.
             */

            if constexpr (BMCWEB_HEALTH_ROLLUP_ALTERNATIVE)
            {
//#error "Conflicts! Please set health-rollup-alternative=disabled."
            }

            if constexpr (BMCWEB_DISABLE_HEALTH_ROLLUP)
            {
//#error "Conflicts! Please set disable-health-rollup=disabled."
            }

            health_utils::getDeviceHealthInfo(asyncResp->res, chassisId);
        }

        if (connectionNames.empty())
        {
            BMCWEB_LOG_ERROR("Got 0 Connection names");
            continue;
        }

        asyncResp->res.jsonValue["@odata.type"] = "#Chassis.v1_22_0.Chassis";
        asyncResp->res.jsonValue["@odata.id"] =
            boost::urls::format("/redfish/v1/Chassis/{}", chassisId);
        asyncResp->res.jsonValue["Name"] = "Chassis Collection";
        asyncResp->res.jsonValue["ChassisType"] =
            chassis::ChassisType::RackMount;
        if constexpr (BMCWEB_HOST_OS_FEATURES)
        {
            asyncResp->res.jsonValue["Actions"]["#Chassis.Reset"]["target"] =
                boost::urls::format(
                    "/redfish/v1/Chassis/{}/Actions/Chassis.Reset", chassisId);
            asyncResp->res
                .jsonValue["Actions"]["#Chassis.Reset"]["@Redfish.ActionInfo"] =
                boost::urls::format("/redfish/v1/Chassis/{}/ResetActionInfo",
                                    chassisId);
        }
        if constexpr (BMCWEB_HOST_AUXPOWER_FEATURES)
        {
            if (chassisId == BMCWEB_PLATFORM_CHASSIS_NAME)
            {
                asyncResp->res
                    .jsonValue["Actions"]["Oem"]["#NvidiaChassis.AuxPowerReset"]
                              ["target"] =
                    "/redfish/v1/Chassis/" + chassisId +
                    "/Actions/Oem/NvidiaChassis.AuxPowerReset";
                asyncResp->res
                    .jsonValue["Actions"]["Oem"]["#NvidiaChassis.AuxPowerReset"]
                              ["@Redfish.ActionInfo"] =
                    "/redfish/v1/Chassis/" + chassisId +
                    "/Oem/Nvidia/AuxPowerResetActionInfo";
            }
        }
        asyncResp->res.jsonValue["PCIeDevices"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Chassis/{}/PCIeDevices",
                                chassisId);
        if constexpr (BMCWEB_NVIDIA_OEM_LOGSERVICES)
        {
            asyncResp->res.jsonValue["LogServices"] = {
                {"@odata.id",
                 "/redfish/v1/Chassis/" + chassisId + "/LogServices"}};
        }

        dbus::utility::getAssociationEndPoints(
            path + "/drive",
            [asyncResp, chassisId](const boost::system::error_code& ec3,
                                   const dbus::utility::MapperEndPoints& resp) {
                if (ec3 || resp.empty())
                {
                    return; // no drives = no failures
                }

                nlohmann::json reference;
                reference["@odata.id"] = boost::urls::format(
                    "/redfish/v1/Chassis/{}/Drives", chassisId);
                asyncResp->res.jsonValue["Drives"] = std::move(reference);
            });

        for (const auto& [connectionName, interfaces2] : connectionNames)
        {
            const std::array<const char*, 3> hasIndicatorLed = {
                "xyz.openbmc_project.Inventory.Item.Chassis",
                "xyz.openbmc_project.Inventory.Item.Panel",
                "xyz.openbmc_project.Inventory.Item.Board.Motherboard"};

            const std::string assetTagInterface =
                "xyz.openbmc_project.Inventory.Decorator.AssetTag";
            const std::string replaceableInterface =
                "xyz.openbmc_project.Inventory.Decorator.Replaceable";
            const std::string revisionInterface =
                "xyz.openbmc_project.Inventory.Decorator.Revision";

            for (const auto& interface : interfaces2)
            {
                if (interface == assetTagInterface)
                {
                    sdbusplus::asio::getProperty<std::string>(
                        *crow::connections::systemBus, connectionName, path,
                        assetTagInterface, "AssetTag",
                        [asyncResp,
                         chassisId](const boost::system::error_code& ec2,
                                    const std::string& property) {
                            if (ec2)
                            {
                                BMCWEB_LOG_ERROR(
                                    "DBus response error for AssetTag: {}",
                                    ec2);
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            asyncResp->res.jsonValue["AssetTag"] = property;
                        });
                }
                else if (interface == replaceableInterface)
                {
                    redfish::chassis_utils::getChassisReplaceable(
                        asyncResp, connectionName, path);

                    sdbusplus::asio::getProperty<bool>(
                        *crow::connections::systemBus, connectionName, path,
                        replaceableInterface, "HotPluggable",
                        [asyncResp,
                         chassisId](const boost::system::error_code& ec2,
                                    const bool property) {
                            if (ec2)
                            {
                                BMCWEB_LOG_ERROR(
                                    "DBus response error for HotPluggable: {}",
                                    ec2);
                                // not abort the resource display
                            }
                            else
                            {
                                asyncResp->res.jsonValue["HotPluggable"] =
                                    property;
                            }
                        });
                }
                else if (interface == revisionInterface)
                {
                    sdbusplus::asio::getProperty<std::string>(
                        *crow::connections::systemBus, connectionName, path,
                        revisionInterface, "Version",
                        [asyncResp,
                         chassisId](const boost::system::error_code& ec2,
                                    const std::string& property) {
                            if (ec2)
                            {
                                BMCWEB_LOG_ERROR(
                                    "DBus response error for Version: {}", ec2);
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            asyncResp->res.jsonValue["Version"] = property;
                        });
                }
            }

            for (const char* interface : hasIndicatorLed)
            {
                if (std::ranges::find(interfaces2, interface) !=
                    interfaces2.end())
                {
                    getIndicatorLedState(asyncResp);
                    getSystemLocationIndicatorActive(asyncResp);
                    break;
                }
            }

            if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
            {
                const std::string itemSystemInterface =
                    "xyz.openbmc_project.Inventory.Item.System";

                if (std::find(interfaces2.begin(), interfaces2.end(),
                              itemSystemInterface) != interfaces2.end())
                {
                    // static power hint
                    redfish::nvidia_chassis_utils::getStaticPowerHintByChassis(
                        asyncResp, path);
                }
            }

            sdbusplus::asio::getAllProperties(
                *crow::connections::systemBus, connectionName, path, "",
                [asyncResp, chassisId, connectionName,
                 path](const boost::system::error_code&,
                       const dbus::utility::DBusPropertiesMap& propertiesList) {
                    redfish::nvidia_chassis_utils::
                        handleChassisGetAllProperties(asyncResp, chassisId,
                                                      path, propertiesList);
                    getChassisStateWrapper(asyncResp, propertiesList,
                                           connectionName, path);
                    getStorageLink(asyncResp, path);
                });

            for (const auto& interface : interfaces2)
            {
                if (interface == "xyz.openbmc_project.Common.UUID")
                {
                    getChassisUUID(asyncResp, connectionName, path);
                }
                else if (interface ==
                         "xyz.openbmc_project.Inventory.Decorator.LocationCode")
                {
                    getChassisLocationCode(asyncResp, connectionName, path);
                }
                else if (
                    interface ==
                    "xyz.openbmc_project.Inventory.Decorator.LocationContext")
                {
                    redfish::chassis_utils::getChassisLocationContext(
                        asyncResp, connectionName, path);
                }
                if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                {
                    if (interface ==
                        "xyz.openbmc_project.Inventory.Decorator.VendorInformation")
                    {
                        // CBC chassis extention
                        redfish::nvidia_chassis_utils::getOemCBCChassisAsset(
                            asyncResp, connectionName, path);
                    }
                }
            }

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
            redfish::nvidia_chassis_utils::getChassisLinksContains(
                asyncResp, objPath);
            // Links association to underneath processors
            redfish::nvidia_chassis_utils::getChassisProcessorLinks(
                asyncResp, objPath);
            // Links association to connected fabric switches
            redfish::nvidia_chassis_utils::getChassisFabricSwitchesLinks(
                asyncResp, objPath);
            // Link association to parent chassis
            redfish::chassis_utils::getChassisLinksContainedBy(asyncResp,
                                                               objPath);
            redfish::nvidia_chassis_utils::getPhysicalSecurityData(asyncResp);
            // get network adapter
            redfish::nvidia_chassis_utils::getNetworkAdapters(
                asyncResp, path, interfaces2, chassisId);
            if constexpr (BMCWEB_NVIDIA_DEVICE_STATUS_FROM_ASSOCIATION)
            {
                // get health for network adapter and nvswitches chassis by
                // association
                redfish::nvidia_chassis_utils::getHealthByAssociation(
                    asyncResp, path, "all_states", chassisId);
            }
        }
        isFoundChassisObject = true;
        // need to check all objpath because a few configs are set by another
        // service
    }

    if (isFoundChassisObject == false)
    {
        // Couldn't find an object with that name.  return an error
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
    }
}

inline void
    handleChassisGet(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& chassisId)
{
    constexpr std::array<std::string_view, 3> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Board",
        "xyz.openbmc_project.Inventory.Item.Chassis",
        "xyz.openbmc_project.Inventory.Item.Component"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        std::bind_front(handleChassisGetSubTree, asyncResp, chassisId));

    constexpr std::array<std::string_view, 1> interfaces2 = {
        "xyz.openbmc_project.Chassis.Intrusion"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project", 0, interfaces2,
        std::bind_front(handlePhysicalSecurityGetSubTree, asyncResp));
}

inline void handleChassisGetPreCheck(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    redfish::chassis_utils::isEROTChassis(
        chassisId, [req, asyncResp, chassisId](bool isEROT, bool isCpuEROT) {
            if (isEROT)
            {
                BMCWEB_LOG_DEBUG(" EROT chassis");
                getEROTChassis(req, asyncResp, chassisId, isCpuEROT);
            }
            else
            {
                handleChassisGet(asyncResp, chassisId);
            }
        });
}

inline void
    handleChassisPatch(const crow::Request& req,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& param)
{
    // if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    // {
    //     return;
    // }
    std::optional<bool> locationIndicatorActive;
    std::optional<std::string> indicatorLed;
    std::optional<nlohmann::json> oemJsonObj;

    std::optional<std::string> partNumber;
    std::optional<std::string> serialNumber;

    std::optional<double> cpuClockFrequency;
    std::optional<double> workloadFactor;
    std::optional<double> temperature;
    std::optional<bool> hardwareWriteProtectEnable;

    if (param.empty())
    {
        return;
    }

    if (!json_util::readJsonPatch( //
            req, asyncResp->res, //
            "IndicatorLED", indicatorLed, //
            "LocationIndicatorActive", locationIndicatorActive, //
            "Oem", oemJsonObj //
            ))
    {
        return;
    }

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        if (oemJsonObj)
        {
            std::optional<nlohmann::json> nvidiaJsonObj;
            if (json_util::readJson(*oemJsonObj, asyncResp->res, "Nvidia",
                                    nvidiaJsonObj))
            {
                std::optional<nlohmann::json> staticPowerHintJsonObj;
                json_util::readJson(
                    *nvidiaJsonObj, asyncResp->res, "PartNumber", partNumber,
                    "SerialNumber", serialNumber, "StaticPowerHint",
                    staticPowerHintJsonObj, "HardwareWriteProtectEnable",
                    hardwareWriteProtectEnable);

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

    if (indicatorLed)
    {
        asyncResp->res.addHeader(
            boost::beast::http::field::warning,
            "299 - \"IndicatorLED is deprecated. Use LocationIndicatorActive instead.\"");
    }

    constexpr std::array<std::string_view, 2> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Board",
        "xyz.openbmc_project.Inventory.Item.Chassis"};

    const std::string& chassisId = param;

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [asyncResp, chassisId, locationIndicatorActive, indicatorLed,
         partNumber, serialNumber, cpuClockFrequency, workloadFactor,
         temperature, hardwareWriteProtectEnable](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }

            // Iterate over all retrieved ObjectPaths.
            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                     object : subtree)
            {
                const std::string& path = object.first;
                const std::vector<
                    std::pair<std::string, std::vector<std::string>>>&
                    connectionNames = object.second;

                sdbusplus::message::object_path objPath(path);
                if (objPath.filename() != chassisId)
                {
                    continue;
                }

                if (connectionNames.empty())
                {
                    BMCWEB_LOG_ERROR("Got 0 Connection names");
                    continue;
                }

                const std::vector<std::string>& interfaces3 =
                    connectionNames[0].second;

                const std::array<const char*, 3> hasIndicatorLed = {
                    "xyz.openbmc_project.Inventory.Item.Chassis",
                    "xyz.openbmc_project.Inventory.Item.Panel",
                    "xyz.openbmc_project.Inventory.Item.Board.Motherboard"};
                bool indicatorChassis = false;
                for (const char* interface : hasIndicatorLed)
                {
                    if (std::ranges::find(interfaces3, interface) !=
                        interfaces3.end())
                    {
                        indicatorChassis = true;
                        break;
                    }
                }
                if (locationIndicatorActive)
                {
                    if (indicatorChassis)
                    {
                        setSystemLocationIndicatorActive(
                            asyncResp, *locationIndicatorActive);
                    }
                    else
                    {
                        messages::propertyUnknown(asyncResp->res,
                                                  "LocationIndicatorActive");
                    }
                }
                if (indicatorLed)
                {
                    if (indicatorChassis)
                    {
                        setIndicatorLedState(asyncResp, *indicatorLed);
                    }
                    else
                    {
                        messages::propertyUnknown(asyncResp->res,
                                                  "IndicatorLED");
                    }
                }
                if (BMCWEB_NVIDIA_OEM_PROPERTIES)
                {
                    if (hardwareWriteProtectEnable)
                    {
                        redfish::nvidia_chassis_utils::
                            OemChassisHardwareWriteProtectEnable(
                                asyncResp, chassisId,
                                *hardwareWriteProtectEnable);
                    }
                    if (partNumber)
                    {
                        redfish::nvidia_chassis_utils::
                            setOemBaseboardChassisAssert(
                                asyncResp, path, "PartNumber", *partNumber);
                    }
                    if (serialNumber)
                    {
                        redfish::nvidia_chassis_utils::
                            setOemBaseboardChassisAssert(
                                asyncResp, path, "SerialNumber", *serialNumber);
                    }
                    if (cpuClockFrequency || workloadFactor || temperature)
                    {
                        if (cpuClockFrequency && workloadFactor && temperature)
                        {
                            redfish::nvidia_chassis_utils::
                                setStaticPowerHintByChassis(
                                    asyncResp, path, *cpuClockFrequency,
                                    *workloadFactor, *temperature);
                        }
                        else
                        {
                            if (!cpuClockFrequency)
                            {
                                messages::propertyMissing(
                                    asyncResp->res, "CpuClockFrequencyHz");
                            }
                            if (!workloadFactor)
                            {
                                messages::propertyMissing(asyncResp->res,
                                                          "WorkloadFactor");
                            }
                            if (!temperature)
                            {
                                messages::propertyMissing(asyncResp->res,
                                                          "TemperatureCelsius");
                            }
                        }
                    }
                }

                return;
            }

            messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        });
}

inline void
    handleChassisPatchReq(App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& param)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    redfish::chassis_utils::isEROTChassis(
        param, [req, asyncResp, param](bool isEROT, bool isCpuEROT) {
            if (isEROT)
            {
                BMCWEB_LOG_DEBUG(" EROT chassis");
                handleEROTChassisPatch(req, asyncResp, param, isCpuEROT);
            }
            else
            {
                handleChassisPatch(req, asyncResp, param);
            }
        });
}

/**
 * Chassis override class for delivering Chassis Schema
 * Functions triggers appropriate requests on DBus
 */
inline void requestRoutesChassis(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/")
        .privileges(redfish::privileges::getChassis)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleChassisGetPreCheck, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/")
        .privileges(redfish::privileges::patchChassis)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleChassisPatchReq, std::ref(app)));
}

inline void
    doChassisPowerCycle(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.State.Chassis"};

    // Use mapper to get subtree paths.
    dbus::utility::getSubTreePaths(
        "/", 0, interfaces,
        [asyncResp](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreePathsResponse& chassisList) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("[mapper] Bad D-Bus request error: {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }

            const char* processName = "xyz.openbmc_project.State.Chassis";
            const char* interfaceName = "xyz.openbmc_project.State.Chassis";
            const char* destProperty = "RequestedPowerTransition";
            const std::string propertyValue =
                "xyz.openbmc_project.State.Chassis.Transition.PowerCycle";
            std::string objectPath =
                "/xyz/openbmc_project/state/chassis_system0";

            /* Look for system reset chassis path */
            if ((std::ranges::find(chassisList, objectPath)) ==
                chassisList.end())
            {
                /* We prefer to reset the full chassis_system, but if it doesn't
                 * exist on some platforms, fall back to a host-only power reset
                 */
                objectPath = "/xyz/openbmc_project/state/chassis0";
            }

            setDbusProperty(asyncResp, "ResetType", processName, objectPath,
                            interfaceName, destProperty, propertyValue);
        });
}

inline void powerCycle(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const std::vector<std::string>& hostList) {
            if (ec)
            {
                doChassisPowerCycle(asyncResp);
            }
            std::string objectPath = "/xyz/openbmc_project/state/host_system0";
            if ((std::find(hostList.begin(), hostList.end(), objectPath)) ==
                hostList.end())
            {
                objectPath = "/xyz/openbmc_project/state/host0";
            }
            crow::connections::systemBus->async_method_call(
                [asyncResp,
                 objectPath](const boost::system::error_code ec,
                             const std::variant<std::string>& state) {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG("[mapper] Bad D-Bus request error: ",
                                         ec);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    const std::string* hostState =
                        std::get_if<std::string>(&state);
                    if (*hostState ==
                        "xyz.openbmc_project.State.Host.HostState.Running")
                    {
                        crow::connections::systemBus->async_method_call(
                            [asyncResp,
                             objectPath](const boost::system::error_code ec) {
                                // Use "Set" method to set the property value.
                                if (ec)
                                {
                                    BMCWEB_LOG_DEBUG(
                                        "[Set] Bad D-Bus request error: ", ec);
                                    messages::internalError(asyncResp->res);
                                    return;
                                }

                                messages::success(asyncResp->res);
                            },
                            "xyz.openbmc_project.State.Host", objectPath,
                            "org.freedesktop.DBus.Properties", "Set",
                            "xyz.openbmc_project.State.Host",
                            "RequestedHostTransition",
                            dbus::utility::DbusVariantType{
                                "xyz.openbmc_project.State.Host.Transition.Reboot"});
                    }
                    else
                    {
                        doChassisPowerCycle(asyncResp);
                    }
                },
                "xyz.openbmc_project.State.Host", objectPath,
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.State.Host", "CurrentHostState");
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths", "/", 0,
        std::array<std::string, 1>{"xyz.openbmc_project.State.Host"});
}

inline void doChassisReset(const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::string resetType;

    if (!json_util::readJsonAction(req, asyncResp->res, "ResetType", resetType))
    {
        return;
    }

    if (resetType != "PowerCycle")
    {
        BMCWEB_LOG_DEBUG("Invalid property value for ResetType: {}", resetType);
        messages::actionParameterNotSupported(asyncResp->res, resetType,
                                              "ResetType");

        return;
    }
    powerCycle(asyncResp);
}

inline void handleChassisResetActionInfoPost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId [[maybe_unused]])
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    BMCWEB_LOG_DEBUG("Post Chassis Reset.");

    if constexpr (BMCWEB_EROT_RESET)
    {
        redfish::chassis_utils::isEROTChassis(
            chassisId,
            [req, asyncResp, chassisId](bool isEROT, bool /*isCpuEROT*/) {
            if (isEROT)
            {
                handleEROTChassisResetAction(req, asyncResp, chassisId);
            }
            else
            {
                doChassisReset(req, asyncResp);
            }
        });
    }
}

inline void handleOemChassisResetActionInfoPost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (chassisId != BMCWEB_PLATFORM_CHASSIS_NAME)
    {
        messages::internalError(asyncResp->res);
        return;
    }
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
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

/**
 * ChassisResetAction class supports the POST method for the Reset
 * action.
 * Function handles POST method request.
 * Analyzes POST body before sending Reset request data to D-Bus.
 */

inline void requestRoutesChassisResetAction(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Actions/Chassis.Reset/")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleChassisResetActionInfoPost, std::ref(app)));
    if constexpr (BMCWEB_HOST_AUXPOWER_FEATURES)
    {
        BMCWEB_ROUTE(
            app,
            "/redfish/v1/Chassis/<str>/Actions/Oem/NvidiaChassis.AuxPowerReset/")
            .privileges(redfish::privileges::postChassis)
            .methods(boost::beast::http::verb::post)(std::bind_front(
                handleOemChassisResetActionInfoPost, std::ref(app)));
    }
}

inline void handleChassisResetActionInfoGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_1_2.ActionInfo";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/ResetActionInfo", chassisId);
    asyncResp->res.jsonValue["Name"] = "Reset Action Info";

    asyncResp->res.jsonValue["Id"] = "ResetActionInfo";
    nlohmann::json::array_t parameters;
    nlohmann::json::object_t parameter;
    parameter["Name"] = "ResetType";
    parameter["Required"] = true;
    parameter["DataType"] = action_info::ParameterTypes::String;
    nlohmann::json::array_t allowed;
    allowed.emplace_back("PowerCycle");
    parameter["AllowableValues"] = std::move(allowed);
    parameters.emplace_back(std::move(parameter));

    asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
}

inline void handleOemChassisResetActionInfoGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (chassisId != BMCWEB_PLATFORM_CHASSIS_NAME)
    {
        messages::internalError(asyncResp->res);
        return;
    }
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_2_0.ActionInfo";
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Chassis/" + chassisId +
        "/Oem/Nvidia/AuxPowerResetActionInfo";
    asyncResp->res.jsonValue["Name"] = "Auxillary Power Reset Action Info";
    asyncResp->res.jsonValue["Id"] = "AuxPowerResetActionInfo";
    nlohmann::json::array_t parameters;
    nlohmann::json::object_t parameter;

    parameter["Name"] = "ResetType";
    parameter["Required"] = true;
    parameter["DataType"] = "String";
    nlohmann::json::array_t allowableValues;
    allowableValues.emplace_back("AuxPowerCycle");
    parameter["AllowableValues"] = std::move(allowableValues);
    parameters.emplace_back(std::move(parameter));

    asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
}
/**
 * ChassisResetActionInfo derived class for delivering Chassis
 * ResetType AllowableValues using ResetInfo schema.
 */
inline void requestRoutesChassisResetActionInfo(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/ResetActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleChassisResetActionInfoGet, std::ref(app)));
    if constexpr (BMCWEB_HOST_AUXPOWER_FEATURES)
    {
        BMCWEB_ROUTE(
            app,
            "/redfish/v1/Chassis/<str>/Oem/Nvidia/AuxPowerResetActionInfo/")
            .privileges(redfish::privileges::getActionInfo)
            .methods(boost::beast::http::verb::get)(std::bind_front(
                handleOemChassisResetActionInfoGet, std::ref(app)));
    }
}

} // namespace redfish
