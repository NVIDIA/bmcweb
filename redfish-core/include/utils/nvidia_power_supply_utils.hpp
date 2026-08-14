/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION &
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
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "dbus_utils.hpp"
#include "error_messages.hpp"
#include "logging.hpp"
#include "str_utility.hpp"

#include <boost/container/flat_map.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/asio/property.hpp>

#include <array>
#include <cerrno>
#include <string>
#include <string_view>
#include <variant>
namespace redfish
{
// Forward declaration
inline void getValidPowerSupplyPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& powerSupplyId,
    std::function<void(const std::string& powerSupplyPath,
                       const std::string& service)>&& callback);
namespace nvidia_power_supply_utils
{

/**
 * @brief Fill or override properties of power supply uri
 * as expected
 */
inline void getNvidiaPowerSupply(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path,
    const std::string& powerSupplyId, const std::string& chassisId)
{
    asyncResp->res.jsonValue["Name"] = powerSupplyId;
    dbus::utility::getProperty<std::string>(
        service, path, "com.nvidia.PowerSupply.PowerSupplyInfo",
        "PowerSupplyType",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& property) {
            if (ec)
            {
                if (ec.value() != EBADR)
                {
                    BMCWEB_LOG_ERROR("DBUS response error for State {}",
                                     ec.value());
                    messages::internalError(asyncResp->res);
                }
                return;
            }
            asyncResp->res.jsonValue["PowerSupplyType"] =
                redfish::dbus_utils::toPowerSupplyType(property);
        });

    std::string powerSupplyURI = "/redfish/v1/Chassis/";
    powerSupplyURI += chassisId;
    powerSupplyURI += "/PowerSubsystem/PowerSupplies/";
    powerSupplyURI += powerSupplyId;
    std::string powerSupplyMetricURI = powerSupplyURI;
    powerSupplyMetricURI += "/Metrics";
    asyncResp->res.jsonValue["Metrics"] = {{"@odata.id", powerSupplyMetricURI}};
}

inline void getNvidiaPowerSupplyMetrics(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& powerSupplyId,
    const std::string& powerSupplyPath)
{
    asyncResp->res.jsonValue["@odata.type"] =
        "#PowerSupplyMetrics.v1_0_1.PowerSupplyMetrics";
    std::string name = powerSupplyId + " Power Supply Metrics";
    asyncResp->res.jsonValue["Name"] = name;
    asyncResp->res.jsonValue["Id"] = "Metrics";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/PowerSubsystem/PowerSupplies/{}/Metrics",
        chassisId, powerSupplyId);
    // Construct the association path by appending "/all_sensors"
    std::string sensorsAssociationPath = powerSupplyPath + "/all_sensors";
    // Retrieve all associated sensor objects
    dbus::utility::getAssociationEndPoints(
        sensorsAssociationPath,
        [asyncResp,
         chassisId](const boost::system::error_code& ec,
                    const dbus::utility::MapperEndPoints& sensorPaths) {
            if (ec || sensorPaths.empty())
            {
                messages::internalError(asyncResp->res);
                BMCWEB_LOG_ERROR(
                    "callback for getAssociationEndPoints in getNvidiaPowerSupplyMetrics fails: ",
                    ec.message());
                return;
            }
            // Iterate through the sensor paths to extract relevant data
            for (const std::string& sensorPath : sensorPaths)
            {
                constexpr std::array<std::string_view, 1>
                    sensorValueInterface = {
                        "xyz.openbmc_project.Sensor.Value"};
                // Filter on Sensor.Value so the mapper cannot be returned as
                // the owning service: it reports itself on sensor paths that
                // carry associations, but serves no properties there.
                dbus::utility::getDbusObject(
                    sensorPath, sensorValueInterface,
                    [asyncResp, chassisId,
                     sensorPath](const boost::system::error_code& ec2,
                                 const dbus::utility::MapperGetObject& object) {
                        if (ec2 || object.empty())
                        {
                            messages::internalError(asyncResp->res);
                            BMCWEB_LOG_ERROR(
                                "callback for getDbusObject in getNvidiaPowerSupplyMetrics fails: ",
                                ec2.message());
                            return;
                        }
                        const std::string& serviceName = object.begin()->first;
                        // Fetch sensor data
                        dbus::utility::getAllProperties(
                            serviceName, sensorPath,
                            "xyz.openbmc_project.Sensor.Value",
                            [asyncResp, chassisId,
                             sensorPath](const boost::system::error_code& ec3,
                                         const dbus::utility::DBusPropertiesMap&
                                             properties) {
                                if (ec3)
                                {
                                    messages::internalError(asyncResp->res);
                                    BMCWEB_LOG_ERROR(
                                        "Error in Fetching sensor data in getNvidiaPowerSupplyMetrics",
                                        ec3.message());
                                    return;
                                }
                                auto it = std::ranges::find_if(
                                    properties, [](const auto& property) {
                                        return property.first == "Value";
                                    });
                                if (it != properties.end())
                                {
                                    const double* attributeValue =
                                        std::get_if<double>(&it->second);
                                    if (attributeValue != nullptr)
                                    {
                                        std::vector<std::string> split;
                                        split.reserve(6);
                                        bmcweb::split(split, sensorPath, '/');
                                        if (split.size() >= 6)
                                        {
                                            const std::string& sensorType =
                                                split[4];
                                            const std::string& sensorName =
                                                split[5];
                                            std::string sensorURI =
                                                boost::urls::format(
                                                    "/redfish/v1/Chassis/{}/Sensors/{}",
                                                    chassisId, sensorName)
                                                    .buffer();
                                            if (sensorType == "temperature")
                                            {
                                                asyncResp->res.jsonValue
                                                    ["TemperatureCelsius"] = {
                                                    {"Reading",
                                                     *attributeValue},
                                                    {"DataSourceUri",
                                                     sensorURI},
                                                };
                                            }
                                            else if (sensorType == "power")
                                            {
                                                asyncResp->res.jsonValue
                                                    ["OutputPowerWatts"] = {
                                                    {"Reading",
                                                     *attributeValue},
                                                    {"DataSourceUri",
                                                     sensorURI},
                                                };
                                            }
                                        }
                                    }
                                }
                            });
                    });
            }
        });
}

inline void doPowerSupplyMetricsGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& powerSupplyId,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    // Get the correct Path and Service that match the input parameters
    getValidPowerSupplyPath(
        asyncResp, chassisId, powerSupplyId,
        [asyncResp, chassisId,
         powerSupplyId](const std::string& powerSupplyPath,
                        const std::string& /*service*/) {
            redfish::nvidia_power_supply_utils::getNvidiaPowerSupplyMetrics(
                asyncResp, chassisId, powerSupplyId, powerSupplyPath);
        });
}

} // namespace nvidia_power_supply_utils
} // namespace redfish
