/*
 * SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION &
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

#include "dbus_utility.hpp"
#include "human_sort.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"

#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/message/native_types.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace redfish
{
/**
 * @brief Fill cable specific properties.
 * @param[in,out]   resp        HTTP response.
 * @param[in]       ec          Error code corresponding to Async method call.
 * @param[in]       properties  List of Cable Properties key/value pairs.
 */
inline void updateCableNameProperty(
    crow::Response& resp, const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error {}", ec);
        messages::internalError(resp);
        return;
    }

    const std::string* name = nullptr;
    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties, "Name", name);
    if (!success)
    {
        messages::internalError(resp);
        return;
    }
    if (name != nullptr)
    {
        resp.jsonValue["Name"] = *name;
    }
}

// Minimum supported revision for CBC Tray Topology data
constexpr uint8_t trayTopologyMinRevision = 2;

inline void handleVendorInformationProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& cableObjectPath,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG(
            "Failed to get VendorInformation properties for CBC {} from service {}: {}",
            cableObjectPath, service, ec.message());
        return;
    }

    // Try to unpack the decoded properties exposed by CBCTrayTopologyParser
    const uint8_t* chassisPhysicalSlotNumber = nullptr;
    const uint8_t* computeTrayIndex = nullptr;
    const uint8_t* revisionId = nullptr;
    const uint8_t* topologyId = nullptr;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties,
        "ChassisPhysicalSlotNumber", chassisPhysicalSlotNumber,
        "ComputeTrayIndex", computeTrayIndex, "RevisionId", revisionId,
        "TopologyId", topologyId);

    if (!success)
    {
        BMCWEB_LOG_DEBUG(
            "Failed to unpack CBC properties for {} from service {}",
            cableObjectPath, service);
        return;
    }

    // Check if the decoded properties are available from this service.
    // Only the CBCTrayTopologyParser service will have these properties.
    if (chassisPhysicalSlotNumber == nullptr || computeTrayIndex == nullptr ||
        revisionId == nullptr || topologyId == nullptr)
    {
        BMCWEB_LOG_DEBUG(
            "Decoded CBC properties not available for {} from service {}",
            cableObjectPath, service);
        return;
    }

    // Validate revision - must be at least trayTopologyMinRevision
    if (*revisionId < trayTopologyMinRevision)
    {
        BMCWEB_LOG_DEBUG("CBC Tray ID revision {} must be >= {} for {}",
                         static_cast<int>(*revisionId),
                         static_cast<int>(trayTopologyMinRevision),
                         cableObjectPath);
        return;
    }

    auto& oem = asyncResp->res.jsonValue["Oem"]["Nvidia"];
    oem["@odata.type"] = "#NvidiaCable.v0_7_0.NvidiaCBC";
    oem["ChassisPhysicalSlotNumber"] = *chassisPhysicalSlotNumber;
    oem["ComputeTrayIndex"] = *computeTrayIndex;
    oem["RevisionId"] = *revisionId;
    oem["TopologyId"] = *topologyId;
}

/**
 * @brief Fetch CBC OEM properties from the VendorInformation interface.
 *
 * The CBCTrayTopologyParser D-Bus service exposes decoded tray topology
 * properties (ChassisPhysicalSlotNumber, ComputeTrayIndex, RevisionId,
 * TopologyId) on the same object path with the VendorInformation interface.
 * Since multiple services may expose this interface on the same path, this
 * function tries to read the decoded properties and silently skips if they
 * are not available from the given service.
 *
 * @param[in,out] asyncResp       Async HTTP response.
 * @param[in]     service         D-Bus service name.
 * @param[in]     cableObjectPath D-Bus object path of the cable.
 */
inline void fetchCBCOemProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& cableObjectPath)
{
    constexpr std::string_view vendorInfoInterface =
        "xyz.openbmc_project.Inventory.Decorator.VendorInformation";

    // Get all properties from this service's VendorInformation interface.
    // The CBCTrayTopologyParser service exposes decoded tray topology
    // properties, while other services may only have raw CustomField1.
    dbus::utility::getAllProperties(
        service, cableObjectPath, std::string(vendorInfoInterface),
        [asyncResp, cableObjectPath,
         service](const boost::system::error_code& ec,
                  const dbus::utility::DBusPropertiesMap& properties) {
            handleVendorInformationProperties(asyncResp, service,
                                              cableObjectPath, ec, properties);
        });
}

inline void handleCableAssetPropertiesResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& cableObjectPath, const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG(
            "get Asset properties failed for Cable {} with error {}",
            cableObjectPath, ec);
        return;
    }

    const std::string* manufacturer = nullptr;
    const std::string* model = nullptr;
    sdbusplus::unpackPropertiesNoThrow(dbus_utils::UnpackErrorPrinter(),
                                       properties, "Manufacturer", manufacturer,
                                       "Model", model);

    if (manufacturer != nullptr)
    {
        asyncResp->res.jsonValue["Manufacturer"] = *manufacturer;
    }
    if (model != nullptr)
    {
        asyncResp->res.jsonValue["Model"] = *model;
    }
}

inline void handleCableLocationCode(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& cableObjectPath, const boost::system::error_code& ec,
    const std::string& locationCode)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("get presence failed for Cable {} with error {}",
                         cableObjectPath, ec);
        return;
    }
    asyncResp->res.jsonValue["Location"]["PartLocation"]["ServiceLabel"] =
        locationCode;
}

inline void handleCableLocationContext(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& cableObjectPath, const boost::system::error_code& ec,
    const std::string& locationContext)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("get presence failed for Cable {} with error {}",
                         cableObjectPath, ec);
        return;
    }
    asyncResp->res.jsonValue["Location"]["PartLocationContext"] =
        locationContext;
}

inline void addChassisLink(nlohmann::json& links, const std::string& key,
                           const sdbusplus::message::object_path& objPath)
{
    nlohmann::json obj = nlohmann::json::object();
    nlohmann::json list = nlohmann::json::array();
    obj["@odata.id"] =
        boost::urls::format("/redfish/v1/Chassis/{}", objPath.filename());
    list.emplace_back(std::move(obj));
    links[key] = std::move(list);
}

inline void handleCableConnectingEndpoints(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, const std::vector<std::string>& resp)
{
    if (ec)
    {
        return; // no switches = no failures
    }
    if (resp.empty())
    {
        return;
    }
    std::vector<std::string> sortedData = resp;
    std::ranges::sort(sortedData, AlphanumLess<std::string>());
    sdbusplus::message::object_path objPathUp(sortedData.front());
    addChassisLink(asyncResp->res.jsonValue["Links"], "UpstreamChassis",
                   objPathUp);
    sdbusplus::message::object_path objPathDown(sortedData.back());
    addChassisLink(asyncResp->res.jsonValue["Links"], "DownstreamChassis",
                   objPathDown);
}

inline void fetchCableInventoryProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& cableObjectPath)
{
    dbus::utility::getAllProperties(
        service, cableObjectPath,
        "xyz.openbmc_project.Inventory.Decorator.Asset",
        std::bind_front(handleCableAssetPropertiesResponse, asyncResp,
                        cableObjectPath));

    dbus::utility::getProperty<std::string>(
        service, cableObjectPath,
        "xyz.openbmc_project.Inventory.Decorator.LocationCode", "LocationCode",
        [asyncResp, cableObjectPath](const boost::system::error_code& ec3,
                                     const std::string& locationCode) {
            handleCableLocationCode(asyncResp, cableObjectPath, ec3,
                                    locationCode);
        });

    dbus::utility::getProperty<std::string>(
        service, cableObjectPath,
        "xyz.openbmc_project.Inventory.Decorator.LocationContext",
        "LocationContext",
        [asyncResp, cableObjectPath](const boost::system::error_code& ec4,
                                     const std::string& locationContext) {
            handleCableLocationContext(asyncResp, cableObjectPath, ec4,
                                       locationContext);
        });

    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", cableObjectPath + "/connecting",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp](const boost::system::error_code& ec1,
                    const std::vector<std::string>& resp1) {
            handleCableConnectingEndpoints(asyncResp, ec1, resp1);
        });
}
} // namespace redfish
