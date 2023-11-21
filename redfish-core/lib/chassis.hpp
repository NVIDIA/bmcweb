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

#include "bmcweb_config.h"

#include "app.hpp"
#include "dbus_utility.hpp"
#include "health.hpp"
#include "led.hpp"
#include <boost/container/flat_map.hpp>
#include <dbus_utility.hpp>
#include <erot_chassis.hpp>
#include <openbmc_dbus_rest.hpp>
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"

#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/chassis_utils.hpp>
#include <utils/conditions_utils.hpp>

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
            storage["@odata.id"] = boost::urls::format(
                "/redfish/v1/Systems/system/Storage/{}", id);
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
        if (chassisState == "xyz.openbmc_project.State.Chassis.PowerState.On")
        {
            asyncResp->res.jsonValue["PowerState"] = "On";
            asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
        }
        else if (chassisState ==
                 "xyz.openbmc_project.State.Chassis.PowerState.Off")
        {
            asyncResp->res.jsonValue["PowerState"] = "Off";
            asyncResp->res.jsonValue["Status"]["State"] = "StandbyOffline";
        }
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
inline void getChassisDimensions(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                 const std::string& service,
                                 const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get chassis dimensions");
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string, dbus::utility::DbusVariantType>>& propertiesList) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error for " "Chassis dimensions");
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
                    BMCWEB_LOG_DEBUG("Null value returned " "for Height");
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
                    BMCWEB_LOG_DEBUG("Null value returned " "for Width");
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
                    BMCWEB_LOG_DEBUG("Null value returned " "for Depth");
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

inline void getIntrusionByService(std::shared_ptr<bmcweb::AsyncResp> asyncResp,
                                  const std::string& service,
                                  const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get intrusion status by service ");

    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, objPath,
        "xyz.openbmc_project.Chassis.Intrusion", "Status",
        [asyncResp{std::move(asyncResp)}](const boost::system::error_code& ec,
                                          const std::string& value) {
        if (ec)
        {
            // do not add err msg in redfish response, because this is not
            //     mandatory property
            BMCWEB_LOG_ERROR("DBUS response error {}", ec);
            return;
        }

        asyncResp->res.jsonValue["PhysicalSecurity"]["IntrusionSensorNumber"] =
            1;
        asyncResp->res.jsonValue["PhysicalSecurity"]["IntrusionSensor"] = value;
        });
}

/**
 * Retrieves physical security properties over dbus
 */
inline void
    getPhysicalSecurityData(std::shared_ptr<bmcweb::AsyncResp> asyncResp)
{
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Chassis.Intrusion"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project", 0, interfaces,
        [asyncResp{std::move(asyncResp)}](
            const boost::system::error_code& ec,
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
                getIntrusionByService(asyncResp, service.first, object.first);
                return;
            }
        }
        });
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
    const dbus::utility::MapperEndPoints& upstreamChassisPaths)
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
        BMCWEB_LOG_ERROR("{} is contained by mutliple chassis", chassisId);
        messages::internalError(asyncResp->res);
        return;
    }

    sdbusplus::message::object_path upstreamChassisPath(
        upstreamChassisPaths[0]);
    std::string upstreamChassis = upstreamChassisPath.filename();
    if (upstreamChassis.empty())
    {
        BMCWEB_LOG_WARNING("Malformed upstream Chassis path {} on {}", upstreamChassisPath.str, chassisId);
        return;
    }

    asyncResp->res.jsonValue["Links"]["ContainedBy"]["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}", upstreamChassis);
}

inline void getChassisContains(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& downstreamChassisPaths)
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
            BMCWEB_LOG_WARNING("Malformed downstream Chassis path {} on {}", downstreamChassisPath.str, chassisId);
            continue;
        }
        nlohmann::json link;
        link["@odata.id"] = boost::urls::format("/redfish/v1/Chassis/{}",
                                                downstreamChassis);
        jValue.push_back(std::move(link));
    }
    asyncResp->res.jsonValue["Links"]["Contains@odata.count"] = jValue.size();
}

inline void
    getChassisConnectivity(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& chassisId,
                           const std::string& chassisPath)
{
    BMCWEB_LOG_DEBUG("Get chassis connectivity");

    dbus::utility::getAssociationEndPoints(
        chassisPath + "/contained_by",
        std::bind_front(getChassisContainedBy, asyncResp, chassisId));

    dbus::utility::getAssociationEndPoints(
        chassisPath + "/containing",
        std::bind_front(getChassisContains, asyncResp, chassisId));
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

inline void
    getChassisLocationCode(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& path)
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

        asyncResp->res.jsonValue["Location"]["PartLocation"]["ServiceLabel"] =
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

inline void
    handleChassisGet(App& app, const crow::Request& req,
                     const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    constexpr std::array<std::string_view, 2> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Board",
        "xyz.openbmc_project.Inventory.Item.Chassis"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [asyncResp, chassisId(std::string(chassisId))](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }
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

            getChassisConnectivity(asyncResp, chassisId, path);

            auto health = std::make_shared<HealthPopulate>(asyncResp);

            if constexpr (bmcwebEnableHealthPopulate)
            {
                dbus::utility::getAssociationEndPoints(
                    path + "/all_sensors",
                    [health](const boost::system::error_code& ec2,
                             const dbus::utility::MapperEndPoints& resp) {
                    if (ec2)
                    {
                        return; // no sensors = no failures
                    }
                    health->inventory = resp;
                    });

                health->populate();
            }

            if (connectionNames.empty())
            {
                BMCWEB_LOG_ERROR("Got 0 Connection names");
                continue;
            }

            asyncResp->res.jsonValue["@odata.type"] =
                "#Chassis.v1_22_0.Chassis";
            asyncResp->res.jsonValue["@odata.id"] =
                boost::urls::format("/redfish/v1/Chassis/{}", chassisId);
            asyncResp->res.jsonValue["Name"] = "Chassis Collection";
            asyncResp->res.jsonValue["ChassisType"] = "RackMount";
            asyncResp->res.jsonValue["Actions"]["#Chassis.Reset"]["target"] =
                boost::urls::format(
                    "/redfish/v1/Chassis/{}/Actions/Chassis.Reset", chassisId);
            asyncResp->res
                .jsonValue["Actions"]["#Chassis.Reset"]["@Redfish.ActionInfo"] =
                boost::urls::format("/redfish/v1/Chassis/{}/ResetActionInfo",
                                    chassisId);
            asyncResp->res.jsonValue["PCIeDevices"]["@odata.id"] =
                "/redfish/v1/Systems/system/PCIeDevices";

            dbus::utility::getAssociationEndPoints(
                path + "/drive",
                [asyncResp,
                 chassisId](const boost::system::error_code& ec3,
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

            const std::string& connectionName = connectionNames[0].first;

            const std::vector<std::string>& interfaces2 =
                connectionNames[0].second;
            const std::array<const char*, 2> hasIndicatorLed = {
                "xyz.openbmc_project.Inventory.Item.Panel",
                "xyz.openbmc_project.Inventory.Item.Board.Motherboard"};

            const std::string assetTagInterface =
                "xyz.openbmc_project.Inventory.Decorator.AssetTag";
            const std::string replaceableInterface =
                "xyz.openbmc_project.Inventory.Decorator.Replaceable";
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
                            BMCWEB_LOG_ERROR( "DBus response error for AssetTag: {}", ec2);
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res.jsonValue["AssetTag"] = property;
                        });
                }
                else if (interface == replaceableInterface)
                {
                    sdbusplus::asio::getProperty<bool>(
                        *crow::connections::systemBus, connectionName, path,
                        replaceableInterface, "HotPluggable",
                        [asyncResp,
                         chassisId](const boost::system::error_code& ec2,
                                    const bool property) {
                        if (ec2)
                        {
                            BMCWEB_LOG_ERROR( "DBus response error for HotPluggable: {}", ec2);
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        asyncResp->res.jsonValue["HotPluggable"] = property;
                        });
                }
            }

            for (const char* interface : hasIndicatorLed)
            {
                if (std::ranges::find(interfaces2, interface) !=
                    interfaces2.end())
                {
                    getIndicatorLedState(asyncResp);
                    getLocationIndicatorActive(asyncResp);
                    break;
                }
            }

            sdbusplus::asio::getAllProperties(
                *crow::connections::systemBus, connectionName, path,
                "xyz.openbmc_project.Inventory.Decorator.Asset",
                [asyncResp, chassisId(std::string(chassisId)),
                 path](const boost::system::error_code& /*ec2*/,
                       const dbus::utility::DBusPropertiesMap& propertiesList) {
                const std::string* partNumber = nullptr;
                const std::string* serialNumber = nullptr;
                const std::string* manufacturer = nullptr;
                const std::string* model = nullptr;
                const std::string* sparePartNumber = nullptr;

                const bool success = sdbusplus::unpackPropertiesNoThrow(
                    dbus_utils::UnpackErrorPrinter(), propertiesList,
                    "PartNumber", partNumber, "SerialNumber", serialNumber,
                    "Manufacturer", manufacturer, "Model", model,
                    "SparePartNumber", sparePartNumber);

                if (!success)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                // Iterate over all retrieved ObjectPaths.
                for (const std::pair<
                         std::string,
                         std::vector<
                             std::pair<std::string, std::vector<std::string>>>>&
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

                    const std::string& connectionName =
                        connectionNames[0].first;
                    const std::vector<std::string>& interfaces1 =
                        connectionNames[0].second;

                    bool operationalStatusPresent = false;
                    const std::string operationalStatusInterface =
                        "xyz.openbmc_project.State.Decorator.OperationalStatus";

                    if (std::find(interfaces1.begin(), interfaces1.end(),
                                  operationalStatusInterface) !=
                        interfaces1.end())
                    {
                        operationalStatusPresent = true;
                    }
<<<<<<< HEAD
                    getChassisData(asyncResp, path, connectionName, chassisId,
                                   operationalStatusPresent);

=======
                asyncResp->res.jsonValue["Name"] = chassisId;
                asyncResp->res.jsonValue["Id"] = chassisId;
#ifdef BMCWEB_ALLOW_DEPRECATED_POWER_THERMAL
                asyncResp->res.jsonValue["Thermal"]["@odata.id"] =
                    boost::urls::format("/redfish/v1/Chassis/{}/Thermal",
                                        chassisId);
                // Power object
                asyncResp->res.jsonValue["Power"]["@odata.id"] =
                    boost::urls::format("/redfish/v1/Chassis/{}/Power",
                                        chassisId);
#endif
#ifdef BMCWEB_NEW_POWERSUBSYSTEM_THERMALSUBSYSTEM
                asyncResp->res.jsonValue["ThermalSubsystem"]["@odata.id"] =
                    boost::urls::format(
                        "/redfish/v1/Chassis/{}/ThermalSubsystem", chassisId);
                asyncResp->res.jsonValue["PowerSubsystem"]["@odata.id"] =
                    boost::urls::format("/redfish/v1/Chassis/{}/PowerSubsystem",
                                        chassisId);
                asyncResp->res.jsonValue["EnvironmentMetrics"]["@odata.id"] =
                    boost::urls::format(
                        "/redfish/v1/Chassis/{}/EnvironmentMetrics", chassisId);
#endif
                // SensorCollection
                asyncResp->res.jsonValue["Sensors"]["@odata.id"] =
                    boost::urls::format("/redfish/v1/Chassis/{}/Sensors",
                                        chassisId);
                asyncResp->res.jsonValue["Status"]["State"] = "Enabled";

                nlohmann::json::array_t computerSystems;
                nlohmann::json::object_t system;
                system["@odata.id"] = "/redfish/v1/Systems/system";
                computerSystems.emplace_back(std::move(system));
                asyncResp->res.jsonValue["Links"]["ComputerSystems"] =
                    std::move(computerSystems);

                nlohmann::json::array_t managedBy;
                nlohmann::json::object_t manager;
                manager["@odata.id"] = "/redfish/v1/Managers/bmc";
                managedBy.emplace_back(std::move(manager));
                asyncResp->res.jsonValue["Links"]["ManagedBy"] =
                    std::move(managedBy);
                getChassisState(asyncResp);
                getStorageLink(asyncResp, path);
                });
>>>>>>> origin/master-october-10

                    getNetworkAdapters(asyncResp, path, interfaces1, chassisId);

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
                    const std::string itemSystemInterface =
                        "xyz.openbmc_project.Inventory.Item.System";

<<<<<<< HEAD
                    if (std::find(interfaces1.begin(), interfaces1.end(),
                                  itemSystemInterface) != interfaces1.end())
                    {
                        // static power hint
                        getStaticPowerHintByChassis(asyncResp, path);
                    }
#endif // BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES

                    sdbusplus::asio::getProperty<std::vector<std::string>>(
                        *crow::connections::systemBus,
                        "xyz.openbmc_project.ObjectMapper", path + "/drive",
                        "xyz.openbmc_project.Association", "endpoints",
                        [asyncResp,
                         chassisId](const boost::system::error_code ec3,
                                    const std::vector<std::string>& resp) {
                        if (ec3 || resp.empty())
                        {
                            return; // no drives = no failures
                        }

                        nlohmann::json reference;
                        reference["@odata.id"] = crow::utility::urlFromPieces(
                            "redfish", "v1", "Chassis", chassisId, "Drives");
                        asyncResp->res.jsonValue["Drives"] =
                            std::move(reference);
                    });
=======
        // Couldn't find an object with that name.  return an error
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        });
>>>>>>> origin/master-october-10

                    return;
                }
                // Couldn't find an object with that name.  return an error
                messages::resourceNotFound(
                    asyncResp->res, "#Chassis.v1_21_0.Chassis", chassisId);
            },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/inventory", 0, interfaces);
        }
    });
}

inline void
    handleChassisPatch(const crow::Request& req,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& param)
{
    nlohmann::json reqJson;
    std::optional<bool> locationIndicatorActive;
    std::optional<std::string> indicatorLed;
    std::optional<nlohmann::json> oemJsonObj;

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
    std::optional<std::string> partNumber;
    std::optional<std::string> serialNumber;

    std::optional<double> cpuClockFrequency;
    std::optional<double> workloadFactor;
    std::optional<double> temperature;
#endif
    if (param.empty())
    {
        return;
    }

    if (!json_util::readJsonPatch(req, asyncResp->res,
                                  "LocationIndicatorActive",
                                  locationIndicatorActive, "IndicatorLED",
                                  indicatorLed, "Oem", oemJsonObj))
    {
        return;
    }

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
    if (oemJsonObj)
    {
        std::optional<nlohmann::json> nvidiaJsonObj;
        if (json_util::readJson(*oemJsonObj, asyncResp->res, "Nvidia",
                                nvidiaJsonObj))
        {
            std::optional<nlohmann::json> staticPowerHintJsonObj;
            json_util::readJson(*nvidiaJsonObj, asyncResp->res, "PartNumber",
                                partNumber, "SerialNumber", serialNumber,
                                "StaticPowerHint", staticPowerHintJsonObj);

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
                    json_util::readJson(*workloadFactorJsonObj, asyncResp->res,
                                        "SetPoint", workloadFactor);
                }
            }
        }
    }
#endif
    // TODO (Gunnar): Remove IndicatorLED after enough time has passed
    if (!locationIndicatorActive && !indicatorLed)
    {
        // return; // delete this when we support more patch properties
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

<<<<<<< HEAD
    crow::connections::systemBus->async_method_call(
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
        [asyncResp, chassisId, locationIndicatorActive, indicatorLed,
         partNumber, serialNumber, cpuClockFrequency, workloadFactor,
         temperature](
#else
        [asyncResp, chassisId, locationIndicatorActive, indicatorLed](
#endif
            const boost::system::error_code ec,
            const crow::openbmc_mapper::GetSubTreeType& subtree) {
=======
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [asyncResp, chassisId, locationIndicatorActive,
         indicatorLed](const boost::system::error_code& ec,
                       const dbus::utility::MapperGetSubTreeResponse& subtree) {
>>>>>>> origin/master-october-10
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }

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

            if (connectionNames.empty())
            {
                BMCWEB_LOG_ERROR("Got 0 Connection names");
                continue;
            }

            const std::vector<std::string>& interfaces3 =
                connectionNames[0].second;

            const std::array<const char*, 2> hasIndicatorLed = {
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
                    setLocationIndicatorActive(asyncResp,
                                               *locationIndicatorActive);
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
                    messages::propertyUnknown(asyncResp->res, "IndicatorLED");
                }
            }
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
            if (partNumber)
            {
                setOemBaseboardChassisAssert(asyncResp, path, "PartNumber",
                                             *partNumber);
            }
            if (serialNumber)
            {
                setOemBaseboardChassisAssert(asyncResp, path, "SerialNumber",
                                             *serialNumber);
            }
            if (cpuClockFrequency || workloadFactor || temperature)
            {
                if (cpuClockFrequency && workloadFactor && temperature)
                {
                    setStaticPowerHintByChassis(asyncResp, path,
                                                *cpuClockFrequency,
                                                *workloadFactor, *temperature);
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
#endif
            return;
        }

        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
<<<<<<< HEAD
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0, interfaces);
=======
        });
>>>>>>> origin/master-october-10
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
    redfish::chassis_utils::isEROTChassis(param,
                                          [req, asyncResp, param](bool isEROT) {
        if (isEROT)
        {
            BMCWEB_LOG_DEBUG(" EROT chassis");
            handleEROTChassisPatch(req, asyncResp, param);
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
            std::bind_front(handleChassisGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/")
        .privileges(redfish::privileges::patchChassis)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleChassisPatchReq, std::ref(app)));
}

/**
 * Handle error responses from d-bus for chassis power cycles
 */
inline void handleChassisPowerCycleError(const boost::system::error_code& ec,
                                         const sdbusplus::message_t& eMsg,
                                         crow::Response& res)
{
    if (eMsg.get_error() == nullptr)
    {
        BMCWEB_LOG_ERROR("D-Bus response error: {}", ec);
        messages::internalError(res);
        return;
    }
    std::string_view errorMessage = eMsg.get_error()->name;

    // If operation failed due to BMC not being in Ready state, tell
    // user to retry in a bit
    if (errorMessage ==
        std::string_view("xyz.openbmc_project.State.Chassis.Error.BMCNotReady"))
    {
        BMCWEB_LOG_DEBUG("BMC not ready, operation not allowed right now");
        messages::serviceTemporarilyUnavailable(res, "10");
        return;
    }

    BMCWEB_LOG_ERROR("Chassis Power Cycle fail {} sdbusplus:{}", ec, errorMessage);
    messages::internalError(res);
}

inline void
    doChassisPowerCycle(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
<<<<<<< HEAD
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const std::vector<std::string>& chassisList) {
=======
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.State.Chassis"};

    // Use mapper to get subtree paths.
    dbus::utility::getSubTreePaths(
        "/", 0, interfaces,
        [asyncResp](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreePathsResponse& chassisList) {
>>>>>>> origin/master-october-10
        if (ec)
        {
            BMCWEB_LOG_ERROR("[mapper] Bad D-Bus request error: {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }
        std::string objectPath = "/xyz/openbmc_project/state/chassis_system0";
<<<<<<< HEAD
        if ((std::find(chassisList.begin(), chassisList.end(), objectPath)) ==
            chassisList.end())
=======

        /* Look for system reset chassis path */
        if ((std::ranges::find(chassisList, objectPath)) == chassisList.end())
>>>>>>> origin/master-october-10
        {
            objectPath = "/xyz/openbmc_project/state/chassis0";
        }
<<<<<<< HEAD
        crow::connections::systemBus->async_method_call(
            [asyncResp, objectPath](const boost::system::error_code ec) {
            // Use "Set" method to set the property
            // value.
            if (ec)
            {
                BMCWEB_LOG_DEBUG("[Set] Bad D-Bus request error: {}", ec);
                messages::internalError(asyncResp->res);
=======

        sdbusplus::asio::setProperty(
            *crow::connections::systemBus, processName, objectPath,
            interfaceName, destProperty, propertyValue,
            [asyncResp](const boost::system::error_code& ec2,
                        sdbusplus::message_t& sdbusErrMsg) {
            // Use "Set" method to set the property value.
            if (ec2)
            {
                handleChassisPowerCycleError(ec2, sdbusErrMsg, asyncResp->res);

>>>>>>> origin/master-october-10
                return;
            }

            messages::success(asyncResp->res);
<<<<<<< HEAD
        },
            "xyz.openbmc_project.State.Chassis", objectPath,
            "org.freedesktop.DBus.Properties", "Set",
            "xyz.openbmc_project.State.Chassis ", "RequestedPowerTransition",
            dbus::utility::DbusVariantType{
                "xyz.openbmc_project.State.Chassis.Transition.PowerCycle"});
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths", "/", 0,
        std::array<std::string, 1>{"xyz.openbmc_project.State.Chassis"});
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
            [asyncResp, objectPath](const boost::system::error_code ec,
                                    const std::variant<std::string>& state) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("[mapper] Bad D-Bus request error: {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            const std::string* hostState = std::get_if<std::string>(&state);
            if (*hostState ==
                "xyz.openbmc_project.State.Host.HostState.Running")
            {
                crow::connections::systemBus->async_method_call(
                    [asyncResp,
                     objectPath](const boost::system::error_code ec) {
                    // Use "Set" method to set the property value.
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG("[Set] Bad D-Bus request error: {}", ec);
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    messages::success(asyncResp->res);
                },
                    "xyz.openbmc_project.State.Host", objectPath,
                    "org.freedesktop.DBus.Properties", "Set",
                    "xyz.openbmc_project.State.Host", "RequestedHostTransition",
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
=======
            });
        });
>>>>>>> origin/master-october-10
}

inline void handleChassisResetActionInfoPost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& /*chassisId*/)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    BMCWEB_LOG_DEBUG("Post Chassis Reset.");

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
<<<<<<< HEAD
    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/Chassis/" + chassisId +
                                            "/ResetActionInfo";
=======
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/ResetActionInfo", chassisId);
>>>>>>> origin/master-october-10
    asyncResp->res.jsonValue["Name"] = "Reset Action Info";

    asyncResp->res.jsonValue["Id"] = "ResetActionInfo";
    nlohmann::json::array_t parameters;
    nlohmann::json::object_t parameter;
    parameter["Name"] = "ResetType";
    parameter["Required"] = true;
    parameter["DataType"] = "String";
    nlohmann::json::array_t allowed;
    allowed.emplace_back("PowerCycle");
    parameter["AllowableValues"] = std::move(allowed);
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
}

} // namespace redfish
