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
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <cstdint>
#include <string_view>

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

/**
 * @brief Helper to fetch CBC OEM properties from a specific service.
 *
 * Tries to read decoded tray topology properties from the given service.
 * Only the CBCTrayTopologyParser service will have these properties.
 *
 * @param[in,out] asyncResp       Async HTTP response.
 * @param[in]     service         D-Bus service name.
 * @param[in]     cableObjectPath D-Bus object path of the cable.
 */
inline void fetchCBCOemPropertiesFromService(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& cableObjectPath)
{
    constexpr std::string_view vendorInfoInterface =
        "xyz.openbmc_project.Inventory.Decorator.VendorInformation";

    dbus::utility::getAllProperties(
        *crow::connections::systemBus, service, cableObjectPath,
        std::string(vendorInfoInterface),
        [asyncResp, cableObjectPath,
         service](const boost::system::error_code& ec,
                  const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG(
                    "Failed to get VendorInformation properties for CBC {} from service {}: {}",
                    cableObjectPath, service, ec.message());
                return;
            }

            // Try to unpack the decoded properties exposed by
            // CBCTrayTopologyParser
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
            if (chassisPhysicalSlotNumber == nullptr ||
                computeTrayIndex == nullptr || revisionId == nullptr ||
                topologyId == nullptr)
            {
                BMCWEB_LOG_DEBUG(
                    "Decoded CBC properties not available for {} from service {}",
                    cableObjectPath, service);
                return;
            }

            // Validate revision - must be at least trayTopologyMinRevision
            if (*revisionId < trayTopologyMinRevision)
            {
                BMCWEB_LOG_DEBUG(
                    "CBC Tray ID revision {} must be >= {} for {}",
                    static_cast<int>(*revisionId),
                    static_cast<int>(trayTopologyMinRevision), cableObjectPath);
                return;
            }

            auto& oem = asyncResp->res.jsonValue["Oem"]["Nvidia"];
            oem["@odata.type"] = "#NvidiaCable.v0_7_0.NvidiaCBC";
            oem["ChassisPhysicalSlotNumber"] = *chassisPhysicalSlotNumber;
            oem["ComputeTrayIndex"] = *computeTrayIndex;
            oem["RevisionId"] = *revisionId;
            oem["TopologyId"] = *topologyId;
        });
}

/**
 * @brief Fetch CBC OEM properties from the VendorInformation interface.
 *
 * Queries the ObjectMapper for all services that provide VendorInformation
 * on the given cable path, then tries to read decoded tray topology properties
 * from each. The CBCTrayTopologyParser D-Bus service exposes these decoded
 * properties (ChassisPhysicalSlotNumber, ComputeTrayIndex, RevisionId,
 * TopologyId), while other services may only have raw CustomField1.
 *
 * @param[in,out] asyncResp       Async HTTP response.
 * @param[in]     cableObjectPath D-Bus object path of the cable.
 */
inline void fetchCBCOemProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& cableObjectPath)
{
    constexpr std::array<std::string_view, 1> vendorInfoInterfaces = {
        "xyz.openbmc_project.Inventory.Decorator.VendorInformation"};

    // Query ObjectMapper for all services providing VendorInformation on this
    // path. This finds services like CBCTrayTopologyParser that may not have
    // been included in the original GetSubTree query (which filtered by Cable
    // interface).
    dbus::utility::getDbusObject(
        cableObjectPath, vendorInfoInterfaces,
        [asyncResp,
         cableObjectPath](const boost::system::error_code& ec,
                          const dbus::utility::MapperGetObject& objectMap) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG(
                    "No VendorInformation services found for cable {}: {}",
                    cableObjectPath, ec.message());
                return;
            }

            // Try to fetch decoded CBC properties from each service.
            // Only CBCTrayTopologyParser will have them.
            for (const auto& [service, interfaces] : objectMap)
            {
                fetchCBCOemPropertiesFromService(asyncResp, service,
                                                 cableObjectPath);
            }
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

    const std::string* partNumber = nullptr;
    const std::string* manufacturer = nullptr;
    const std::string* model = nullptr;
    const std::string* serialNumber = nullptr;
    sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties, "PartNumber", partNumber,
        "Manufacturer", manufacturer, "Model", model, "SerialNumber",
        serialNumber);

    if (partNumber != nullptr)
    {
        asyncResp->res.jsonValue["PartNumber"] = *partNumber;
    }
    if (manufacturer != nullptr)
    {
        asyncResp->res.jsonValue["Manufacturer"] = *manufacturer;
    }
    if (model != nullptr)
    {
        asyncResp->res.jsonValue["Model"] = *model;
    }
    if (serialNumber != nullptr)
    {
        asyncResp->res.jsonValue["SerialNumber"] = *serialNumber;
    }
}

inline void fetchCableInventoryProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& cableObjectPath)
{
    dbus::utility::getAllProperties(
        *crow::connections::systemBus, service, cableObjectPath,
        "xyz.openbmc_project.Inventory.Decorator.Asset",
        std::bind_front(handleCableAssetPropertiesResponse, asyncResp,
                        cableObjectPath));

    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, cableObjectPath,
        "xyz.openbmc_project.Inventory.Decorator.LocationCode", "LocationCode",
        [asyncResp, cableObjectPath](const boost::system::error_code& ec3,
                                     const std::string& locationCode) {
            if (ec3)
            {
                BMCWEB_LOG_DEBUG(
                    "get presence failed for Cable {} with error {}",
                    cableObjectPath, ec3);
                return;
            }
            asyncResp->res
                .jsonValue["Location"]["PartLocation"]["ServiceLabel"] =
                locationCode;
        });

    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, cableObjectPath,
        "xyz.openbmc_project.Inventory.Decorator.LocationContext",
        "LocationContext",
        [asyncResp, cableObjectPath](const boost::system::error_code& ec4,
                                     const std::string& locationContext) {
            if (ec4)
            {
                BMCWEB_LOG_DEBUG(
                    "get presence failed for Cable {} with error {}",
                    cableObjectPath, ec4);
                return;
            }
            asyncResp->res.jsonValue["Location"]["PartLocationContext"] =
                locationContext;
        });

    crow::connections::systemBus->async_method_call(
        [asyncResp, cableObjectPath{cableObjectPath}](
            const boost::system::error_code& ec1,
            std::variant<std::vector<std::string>>& resp1) {
            if (ec1)
            {
                return; // no switches = no failures
            }
            std::vector<std::string>* data1 =
                std::get_if<std::vector<std::string>>(&resp1);
            if (data1 == nullptr)
            {
                return;
            }
            std::ranges::sort(*data1, AlphanumLess<std::string>());
            sdbusplus::message::object_path objPathUp(data1->front());
            nlohmann::json upstreamObj = nlohmann::json::object();
            nlohmann::json upstreamList = nlohmann::json::array();
            upstreamObj["@odata.id"] = boost::urls::format(
                "/redfish/v1/Chassis/{}", objPathUp.filename());
            upstreamList.emplace_back(std::move(upstreamObj));
            asyncResp->res.jsonValue["Links"]["UpstreamChassis"] = upstreamList;

            sdbusplus::message::object_path objPathDown(data1->back());
            nlohmann::json downstreamObj = nlohmann::json::object();
            nlohmann::json downstreamList = nlohmann::json::array();
            downstreamObj["@odata.id"] = boost::urls::format(
                "/redfish/v1/Chassis/{}", objPathDown.filename());
            downstreamList.emplace_back(std::move(downstreamObj));
            asyncResp->res.jsonValue["Links"]["DownstreamChassis"] =
                downstreamList;
            return;
        },
        "xyz.openbmc_project.ObjectMapper", cableObjectPath + "/connecting",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}
} // namespace redfish
