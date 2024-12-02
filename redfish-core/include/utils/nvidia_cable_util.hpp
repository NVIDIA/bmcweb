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
#include <dbus_utility.hpp>
#include <query.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <array>
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

inline void fetchCableInventoryProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& cableObjectPath)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, cableObjectPath,
        "xyz.openbmc_project.Inventory.Decorator.Asset", "PartNumber",
        [asyncResp, cableObjectPath](const boost::system::error_code& ec2,
                                     const std::string& PartNumber) {
            if (ec2)
            {
                BMCWEB_LOG_DEBUG(
                    "get presence failed for Cable {} with error {}",
                    cableObjectPath, ec2.what());
                return;
            }
            asyncResp->res.jsonValue["PartNumber"] = PartNumber;
        });

    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, cableObjectPath,
        "xyz.openbmc_project.Inventory.Decorator.LocationCode", "LocationCode",
        [asyncResp, cableObjectPath](const boost::system::error_code& ec3,
                                     const std::string& LocationCode) {
            if (ec3)
            {
                BMCWEB_LOG_DEBUG(
                    "get presence failed for Cable {} with error {}",
                    cableObjectPath, ec3);
                return;
            }
            asyncResp->res
                .jsonValue["Location"]["PartLocation"]["ServiceLabel"] =
                LocationCode;
        });

    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, cableObjectPath,
        "xyz.openbmc_project.Inventory.Decorator.LocationContext",
        "LocationContext",
        [asyncResp, cableObjectPath](const boost::system::error_code& ec4,
                                     const std::string& LocationContext) {
            if (ec4)
            {
                BMCWEB_LOG_DEBUG(
                    "get presence failed for Cable {} with error {}",
                    cableObjectPath, ec4);
                return;
            }
            asyncResp->res.jsonValue["Location"]["PartLocationContext"] =
                LocationContext;
        });

    crow::connections::systemBus->async_method_call(
        [asyncResp, cableObjectPath{cableObjectPath}](
            const boost::system::error_code ec1,
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
