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

namespace redfish
{
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
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, path,
        "com.nvidia.PowerSupply.PowerSupplyInfo", "PowerSupplyType",
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
                dbus::utility::getDbusObject(
                    sensorPath, {},
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
                        crow::connections::systemBus->async_method_call(
                            [asyncResp, chassisId, sensorPath](
                                const boost::system::error_code& ec3,
                                const boost::container::flat_map<
                                    std::string,
                                    std::variant<std::string, double, uint64_t,
                                                 std::vector<std::string>>>&
                                    properties) {
                                if (ec3)
                                {
                                    messages::internalError(asyncResp->res);
                                    BMCWEB_LOG_ERROR(
                                        "Error in Fetching sensor data in getNvidiaPowerSupplyMetrics",
                                        ec3.message());
                                    return;
                                }
                                auto it = properties.find("Value");
                                if (it != properties.end())
                                {
                                    const double* attributeValue =
                                        std::get_if<double>(&it->second);
                                    if (attributeValue)
                                    {
                                        std::vector<std::string> split;
                                        boost::algorithm::split(
                                            split, sensorPath,
                                            boost::is_any_of("/"));
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
                            },
                            serviceName, sensorPath,
                            "org.freedesktop.DBus.Properties", "GetAll", "");
                    });
            }
        });
}

} // namespace nvidia_power_supply_utils
} // namespace redfish
