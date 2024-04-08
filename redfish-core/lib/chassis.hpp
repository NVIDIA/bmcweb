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
#include "dbus_utility.hpp"
#include "led.hpp"
#include "query.hpp"
#include "redfish_util.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
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

<<<<<<< HEAD
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

=======
>>>>>>> master
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
                const auto service = object.second.front();

            BMCWEB_LOG_DEBUG("Get intrusion status by service ");

            sdbusplus::asio::getProperty<std::string>(
                *crow::connections::systemBus, service.first, object.first,
                "xyz.openbmc_project.Chassis.Intrusion", "Status",
                [asyncResp](const boost::system::error_code& ec1,
                            const std::string& value) {
                if (ec1)
                {
                    // do not add err msg in redfish response, because this is
                    // not
                    //     mandatory property
                    BMCWEB_LOG_ERROR("DBUS response error {}", ec1);
                    return;
                }
                asyncResp->res
                    .jsonValue["PhysicalSecurity"]["IntrusionSensorNumber"] = 1;
                asyncResp->res
                    .jsonValue["PhysicalSecurity"]["IntrusionSensor"] = value;
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
            BMCWEB_LOG_WARNING("Malformed downstream Chassis path {} on {}",
                               downstreamChassisPath.str, chassisId);
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

<<<<<<< HEAD
inline void
    handleChassisGet(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& chassisId)
{
    // if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    // {
    //     return;
    // }
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

#ifdef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE
            std::shared_ptr<HealthRollup> health =
                std::make_shared<HealthRollup>(
                    objPath, [asyncResp](const std::string& rootHealth,
                                         const std::string& healthRollup) {
                asyncResp->res.jsonValue["Status"]["Health"] = rootHealth;
#ifndef BMCWEB_DISABLE_HEALTH_ROLLUP
                asyncResp->res.jsonValue["Status"]["HealthRollup"] =
                    healthRollup;
#endif // BMCWEB_DISABLE_HEALTH_ROLLUP
            });
            health->start();
#else
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
#endif // ifdef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE
            if (connectionNames.empty())
            {
                BMCWEB_LOG_ERROR("Got 0 Connection names");
                continue;
            }

            asyncResp->res.jsonValue["@odata.type"] =
                "#Chassis.v1_22_0.Chassis";
            asyncResp->res.jsonValue["@odata.id"] =
                boost::urls::format("/redfish/v1/Chassis/{}", chassisId);
            asyncResp->res.jsonValue["Name"] = chassisId;
            asyncResp->res.jsonValue["Id"] = chassisId;
            asyncResp->res.jsonValue["ChassisType"] = "RackMount";
#ifdef BMCWEB_ENABLE_HOST_OS_FEATURE
            asyncResp->res.jsonValue["Actions"]["#Chassis.Reset"]["target"] =
                boost::urls::format(
                    "/redfish/v1/Chassis/{}/Actions/Chassis.Reset", chassisId);
            asyncResp->res
                .jsonValue["Actions"]["#Chassis.Reset"]["@Redfish.ActionInfo"] =
                boost::urls::format("/redfish/v1/Chassis/{}/ResetActionInfo",
                                    chassisId);
#endif
#ifdef BMCWEB_ENABLE_HOST_AUX_POWER
            if (chassisId == PLATFORMCHASSISNAME)
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
#endif
            asyncResp->res.jsonValue["PCIeDevices"]["@odata.id"] =
                boost::urls::format("/redfish/v1/Chassis/{}/PCIeDevices",
                                    chassisId);

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_LOGSERVICES

            asyncResp->res.jsonValue["LogServices"] = {
                {"@odata.id",
                 "/redfish/v1/Chassis/" + chassisId + "/LogServices"}};
#endif // BMCWEB_ENABLE_NVIDIA_OEM_LOGSERVICES

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

            bool operationalStatusPresent = false;
            const std::string operationalStatusInterface =
                "xyz.openbmc_project.State.Decorator.OperationalStatus";

            if (std::find(interfaces2.begin(), interfaces2.end(),
                          operationalStatusInterface) != interfaces2.end())
            {
                operationalStatusPresent = true;
            }

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
                            BMCWEB_LOG_ERROR(
                                "DBus response error for AssetTag: {}", ec2);
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
                            BMCWEB_LOG_ERROR(
                                "DBus response error for HotPluggable: {}",
                                ec2);
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
            redfish::nvidia_chassis_utils::getNetworkAdapters(
                asyncResp, path, interfaces2, chassisId);

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
            const std::string itemSystemInterface =
                "xyz.openbmc_project.Inventory.Item.System";

            if (std::find(interfaces2.begin(), interfaces2.end(),
                          itemSystemInterface) != interfaces2.end())
            {
                // static power hint
                redfish::nvidia_chassis_utils::getStaticPowerHintByChassis(
                    asyncResp, path);
            }
#endif // BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
            sdbusplus::asio::getAllProperties(
                *crow::connections::systemBus, connectionName, path, "",
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
                [asyncResp, chassisId(std::string(chassisId)), path,
                 operationalStatusPresent](
#else
                [asyncResp, chassisId(std::string(chassisId)), path](
#endif // BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
                    const boost::system::error_code& /*ec2*/,
                    const dbus::utility::DBusPropertiesMap& propertiesList) {
=======
inline void handleDecoratorAssetProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& path,
    const dbus::utility::DBusPropertiesMap& propertiesList)
{
>>>>>>> master
                const std::string* partNumber = nullptr;
                const std::string* serialNumber = nullptr;
                const std::string* manufacturer = nullptr;
                const std::string* model = nullptr;
                const std::string* sparePartNumber = nullptr;

                const std::string* sku = nullptr;
                const std::string* uuid = nullptr;
                const std::string* locationCode = nullptr;
                const std::string* locationType = nullptr;
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
                const std::string* state = nullptr;

                const bool success = sdbusplus::unpackPropertiesNoThrow(
<<<<<<< HEAD
                    dbus_utils::UnpackErrorPrinter(), propertiesList,
                    "PartNumber", partNumber, "SerialNumber", serialNumber,
                    "Manufacturer", manufacturer, "Model", model,
                    "SparePartNumber", sparePartNumber, "SKU", sku, "UUID",
                    uuid, "LocationCode", locationCode, "LocationType",
                    locationType, "PrettyName", prettyName, "Type", type,
                    "Height", height, "Width", width, "Depth", depth,
                    "MinPowerWatts", minPowerWatts, "MaxPowerWatts",
                    maxPowerWatts, "AssetTag", assetTag, "WriteProtected",
                    writeProtected, "WriteProtectedControl",
                    writeProtectedControl, "PCIeReferenceClockCount",
                    pCIeReferenceClockCount, "State", state);
=======
        dbus_utils::UnpackErrorPrinter(), propertiesList, "PartNumber",
        partNumber, "SerialNumber", serialNumber, "Manufacturer", manufacturer,
        "Model", model, "SparePartNumber", sparePartNumber);
>>>>>>> master

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

                if (sku != nullptr)
                {
                    asyncResp->res.jsonValue["SKU"] = *sku;
                }
                if (uuid != nullptr)
                {
                    asyncResp->res.jsonValue["UUID"] = *uuid;
                }
                if (locationCode != nullptr)
                {
                    asyncResp->res.jsonValue["Location"]["PartLocation"]
                                            ["ServiceLabel"] = *locationCode;
                }
                if (locationType != nullptr)
                {
                    asyncResp->res
                        .jsonValue["Location"]["PartLocation"]["LocationType"] =
                        redfish::dbus_utils::toLocationType(*locationType);
                }
                if (prettyName != nullptr)
                {
                    asyncResp->res.jsonValue["Name"] = *prettyName;
                }
                if (type != nullptr)
                {
                    // asyncResp->res.jsonValue["Type"] = *type;
                    asyncResp->res.jsonValue["ChassisType"] =
                        redfish::chassis_utils::getChassisType(*type);
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
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
                // default oem data
                nlohmann::json& oem = asyncResp->res.jsonValue["Oem"]["Nvidia"];
                oem["@odata.type"] = "#NvidiaChassis.v1_0_0.NvidiaChassis";

                if (writeProtected != nullptr)
                {
                    asyncResp->res
                        .jsonValue["Oem"]["Nvidia"]["HardwareWriteProtected"] =
                        *writeProtected;
                }

                if (writeProtectedControl != nullptr)
                {
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                            ["HardwareWriteProtectedControl"] =
                        *writeProtectedControl;
                }

                if (pCIeReferenceClockCount != nullptr)
                {
                    asyncResp->res
                        .jsonValue["Oem"]["Nvidia"]["PCIeReferenceClockCount"] =
                        *pCIeReferenceClockCount;
                }
                if (state != nullptr && operationalStatusPresent)
                {
                    asyncResp->res.jsonValue["Status"]["State"] =
                        redfish::chassis_utils::getPowerStateType(*state);
                }
#endif // BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
                asyncResp->res.jsonValue["Name"] = chassisId;
                asyncResp->res.jsonValue["Id"] = chassisId;
#ifdef BMCWEB_ALLOW_DEPRECATED_POWER_THERMAL
#ifdef BMCWEB_ENABLE_HOST_OS_FEATURE
                asyncResp->res.jsonValue["Thermal"]["@odata.id"] =
<<<<<<< HEAD
                    boost::urls::format("/redfish/v1/Chassis/{}/Thermal",
                                        chassisId);

=======
        boost::urls::format("/redfish/v1/Chassis/{}/Thermal", chassisId);
>>>>>>> master
                // Power object
                asyncResp->res.jsonValue["Power"]["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}/Power", chassisId);
#endif
#endif
#ifdef BMCWEB_NEW_POWERSUBSYSTEM_THERMALSUBSYSTEM
                asyncResp->res.jsonValue["ThermalSubsystem"]["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}/ThermalSubsystem",
                                        chassisId);
    asyncResp->res.jsonValue["PowerSubsystem"]["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}/PowerSubsystem", chassisId);
                asyncResp->res.jsonValue["EnvironmentMetrics"]["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}/EnvironmentMetrics",
                            chassisId);
#endif
                // SensorCollection
                asyncResp->res.jsonValue["Sensors"]["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}/Sensors", chassisId);
                asyncResp->res.jsonValue["Status"]["State"] = "Enabled";

                // Assembly collection
                asyncResp->res.jsonValue["Assembly"]["@odata.id"] =
                    boost::urls::format("/redfish/v1/Chassis/{}/Assembly",
                                        chassisId);

#ifdef BMCWEB_ENABLE_NETWORK_ADAPTERS
                // NetworkAdapters collection
                asyncResp->res.jsonValue["NetworkAdapters"]["@odata.id"] =
                    boost::urls::format(
                        "/redfish/v1/Chassis/{}/NetworkAdapters", chassisId);
#endif
                // PCIeSlots collection
                asyncResp->res.jsonValue["PCIeSlots"]["@odata.id"] =
                    boost::urls::format("/redfish/v1/Chassis/{}/PCIeSlots",
                                        chassisId);

                // TrustedComponent collection
                asyncResp->res.jsonValue["TrustedComponents"]["@odata.id"] =
                    boost::urls::format(
                        "/redfish/v1/Chassis/{}/TrustedComponents", chassisId);

                // Controls Collection
                asyncResp->res.jsonValue["Controls"] = {
                    {"@odata.id",
                     "/redfish/v1/Chassis/" + chassisId + "/Controls"}};

                nlohmann::json::array_t computerSystems;
                nlohmann::json::object_t system;
                system["@odata.id"] = "/redfish/v1/Systems/" PLATFORMSYSTEMID;
                computerSystems.emplace_back(std::move(system));
                asyncResp->res.jsonValue["Links"]["ComputerSystems"] =
                    std::move(computerSystems);

                nlohmann::json::array_t managedBy;
                nlohmann::json::object_t manager;
                manager["@odata.id"] = "/redfish/v1/Managers/" PLATFORMBMCID;
                managedBy.emplace_back(std::move(manager));
<<<<<<< HEAD
                asyncResp->res.jsonValue["Links"]["ManagedBy"] =
                    std::move(managedBy);
=======
    asyncResp->res.jsonValue["Links"]["ManagedBy"] = std::move(managedBy);
    getChassisState(asyncResp);
>>>>>>> master
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

        if (connectionNames.empty())
        {
            BMCWEB_LOG_ERROR("Got 0 Connection names");
            continue;
        }

        asyncResp->res.jsonValue["@odata.type"] = "#Chassis.v1_22_0.Chassis";
        asyncResp->res.jsonValue["@odata.id"] =
            boost::urls::format("/redfish/v1/Chassis/{}", chassisId);
        asyncResp->res.jsonValue["Name"] = "Chassis Collection";
        asyncResp->res.jsonValue["ChassisType"] = "RackMount";
        asyncResp->res.jsonValue["Actions"]["#Chassis.Reset"]["target"] =
            boost::urls::format("/redfish/v1/Chassis/{}/Actions/Chassis.Reset",
                                chassisId);
        asyncResp->res
            .jsonValue["Actions"]["#Chassis.Reset"]["@Redfish.ActionInfo"] =
            boost::urls::format("/redfish/v1/Chassis/{}/ResetActionInfo",
                                chassisId);
        dbus::utility::getAssociationEndPoints(
            path + "/drive",
            [asyncResp, chassisId](const boost::system::error_code& ec3,
                                   const dbus::utility::MapperEndPoints& resp) {
            if (ec3 || resp.empty())
            {
                return; // no drives = no failures
            }

            nlohmann::json reference;
            reference["@odata.id"] =
                boost::urls::format("/redfish/v1/Chassis/{}/Drives", chassisId);
            asyncResp->res.jsonValue["Drives"] = std::move(reference);
        });

        const std::string& connectionName = connectionNames[0].first;

        const std::vector<std::string>& interfaces2 = connectionNames[0].second;
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
                    [asyncResp, chassisId](const boost::system::error_code& ec2,
                                           const std::string& property) {
                    if (ec2)
                    {
                        BMCWEB_LOG_ERROR("DBus response error for AssetTag: {}",
                                         ec2);
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
                    [asyncResp, chassisId](const boost::system::error_code& ec2,
                                           const bool property) {
                    if (ec2)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBus response error for HotPluggable: {}", ec2);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["HotPluggable"] = property;
                });
            }
            else if (interface == revisionInterface)
            {
                sdbusplus::asio::getProperty<std::string>(
                    *crow::connections::systemBus, connectionName, path,
                    revisionInterface, "Version",
                    [asyncResp, chassisId](const boost::system::error_code& ec2,
                                           const std::string& property) {
                    if (ec2)
                    {
                        BMCWEB_LOG_ERROR("DBus response error for Version: {}",
                                         ec2);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Version"] = property;
                });
            }
        }

        for (const char* interface : hasIndicatorLed)
        {
            if (std::ranges::find(interfaces2, interface) != interfaces2.end())
            {
                getIndicatorLedState(asyncResp);
                getSystemLocationIndicatorActive(asyncResp);
                break;
            }
        }

        sdbusplus::asio::getAllProperties(
            *crow::connections::systemBus, connectionName, path,
            "xyz.openbmc_project.Inventory.Decorator.Asset",
            [asyncResp, chassisId,
             path](const boost::system::error_code&,
                   const dbus::utility::DBusPropertiesMap& propertiesList) {
            handleDecoratorAssetProperties(asyncResp, chassisId, path,
                                           propertiesList);
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
            }

            if (!operationalStatusPresent)
            {
                getChassisState(asyncResp);
            }

#ifndef BMCWEB_DISABLE_CONDITIONS_ARRAY
            redfish::conditions_utils::populateServiceConditions(asyncResp,
                                                                 chassisId);
#endif // BMCWEB_DISABLE_CONDITIONS_ARRAY
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
            // Baseboard Chassis OEM properties if exist, search by association
            redfish::nvidia_chassis_utils::getOemBaseboardChassisAssert(
                asyncResp, objPath);
#endif // BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES

            // Links association to underneath chassis
            redfish::nvidia_chassis_utils::getChassisLinksContains(asyncResp,
                                                                   objPath);
            // Links association to underneath processors
            redfish::nvidia_chassis_utils::getChassisProcessorLinks(asyncResp,
                                                                    objPath);
            // Links association to connected fabric switches
            redfish::nvidia_chassis_utils::getChassisFabricSwitchesLinks(
                asyncResp, objPath);
            // Link association to parent chassis
            redfish::chassis_utils::getChassisLinksContainedBy(asyncResp,
                                                               objPath);
            getPhysicalSecurityData(asyncResp);
            return;
        }

        // Couldn't find an object with that name.  return an error
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
<<<<<<< HEAD
    });
=======
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
        std::bind_front(handleChassisGetSubTree, asyncResp, chassisId));

    constexpr std::array<std::string_view, 1> interfaces2 = {
        "xyz.openbmc_project.Chassis.Intrusion"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project", 0, interfaces2,
        std::bind_front(handlePhysicalSecurityGetSubTree, asyncResp));
>>>>>>> master
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
        chassisId, [req, asyncResp, chassisId](bool isEROT) {
        if (isEROT)
        {
            BMCWEB_LOG_DEBUG(" EROT chassis");
            getEROTChassis(req, asyncResp, chassisId);
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
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
        [asyncResp, chassisId, locationIndicatorActive, indicatorLed,
         partNumber, serialNumber, cpuClockFrequency, workloadFactor,
         temperature]
#else
        [asyncResp, chassisId, locationIndicatorActive, indicatorLed]
#endif
        (const boost::system::error_code& ec,
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
                    setSystemLocationIndicatorActive(asyncResp,
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
        std::string objectPath = "/xyz/openbmc_project/state/chassis_system0";

        /* Look for system reset chassis path */
        if ((std::ranges::find(chassisList, objectPath)) == chassisList.end())
        {
            /* We prefer to reset the full chassis_system, but if it doesn't
             * exist on some platforms, fall back to a host-only power reset
             */
            objectPath = "/xyz/openbmc_project/state/chassis0";
        }

        setDbusProperty(asyncResp, processName, objectPath, interfaceName,
                        destProperty, "ResetType", propertyValue);
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
            [asyncResp, objectPath](const boost::system::error_code ec,
                                    const std::variant<std::string>& state) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("[mapper] Bad D-Bus request error: ", ec);
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
                        BMCWEB_LOG_DEBUG("[Set] Bad D-Bus request error: ", ec);
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
}

inline void handleChassisResetActionInfoPost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
#ifdef BMCWEB_ENABLE_EROT_RESET
    const std::string& chassisId)
#else
    const std::string& /*chassisId*/)
#endif // BMCWEB_ENABLE_EROT_RESET
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    BMCWEB_LOG_DEBUG("Post Chassis Reset.");
#ifdef BMCWEB_ENABLE_EROT_RESET
    redfish::chassis_utils::isEROTChassis(
        chassisId, [req, asyncResp, chassisId](bool isEROT) {
        if (isEROT)
        {
            handleEROTChassisResetAction(req, asyncResp, chassisId);
        }
        else
        {
#endif // BMCWEB_ENABLE_EROT_RESET
            std::string resetType;

            if (!json_util::readJsonAction(req, asyncResp->res, "ResetType",
                                           resetType))
            {
                return;
            }

            if (resetType != "PowerCycle")
            {
                BMCWEB_LOG_DEBUG("Invalid property value for ResetType: {}",
                                 resetType);
                messages::actionParameterNotSupported(asyncResp->res, resetType,
                                                      "ResetType");

                return;
            }
            powerCycle(asyncResp);
#ifdef BMCWEB_ENABLE_EROT_RESET
        }
    });
#endif // BMCWEB_ENABLE_EROT_RESET
}

#ifdef BMCWEB_ENABLE_HOST_AUX_POWER
inline void handleOemChassisResetActionInfoPost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (chassisId != PLATFORMCHASSISNAME)
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

    if (resetType != "AuxPowerCycle")
    {
        messages::actionParameterValueError(asyncResp->res, "ResetType",
                                            "NvidiaChassis.AuxPowerReset");
        return;
    }

    // check power status
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, "xyz.openbmc_project.State.Host",
        "/xyz/openbmc_project/state/host0", "xyz.openbmc_project.State.Host",
        "CurrentHostState",
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
            }, "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                "org.freedesktop.systemd1.Manager", "StartUnit",
                "nvidia-aux-power.service", "replace");
        }
        else
        {
            messages::chassisPowerStateOffRequired(asyncResp->res, "0");
        }
    });
}
#endif

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
#ifdef BMCWEB_ENABLE_HOST_AUX_POWER
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Actions/Oem/NvidiaChassis.AuxPowerReset")
        .privileges(redfish::privileges::postChassis)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleOemChassisResetActionInfoPost, std::ref(app)));

#endif
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
    parameter["DataType"] = "String";
    nlohmann::json::array_t allowed;
    allowed.emplace_back("PowerCycle");
    parameter["AllowableValues"] = std::move(allowed);
    parameters.emplace_back(std::move(parameter));

    asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
}

#ifdef BMCWEB_ENABLE_HOST_AUX_POWER
inline void handleOemChassisResetActionInfoGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (chassisId != PLATFORMCHASSISNAME)
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
#endif
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
#ifdef BMCWEB_ENABLE_HOST_AUX_POWER
    BMCWEB_ROUTE(
        app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/AuxPowerResetActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleOemChassisResetActionInfoGet, std::ref(app)));
#endif
}

} // namespace redfish
