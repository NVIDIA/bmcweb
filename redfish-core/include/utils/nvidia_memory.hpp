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

inline std::string toMemoryPerformanceStateType(const std::string& state)
{
    if (state == "com.nvidia.MemoryPerformance.PerformanceStates.Normal")
    {
        return "Normal";
    }
    if (state == "com.nvidia.MemoryPerformance.PerformanceStates.Throttled")
    {
        return "Throttled";
    }
    if (state == "com.nvidia.MemoryPerformance.PerformanceStates.Degraded")
    {
        return "Degraded";
    }
    if (state == "com.nvidia.MemoryPerformance.PerformanceStates.Unknown")
    {
        return "Unknown";
    }
    return "";
}

inline void populatePerformanceData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const boost::system::error_code& ec, const std::string& value)
{
    if (ec)
    {
        return;
    }
    nlohmann::json& json = aResp->res.jsonValue;
    bool performanceDegrade = false;
    auto state = toMemoryPerformanceStateType(value);
    if (state != "Normal")
    {
        performanceDegrade = true;
    }
    json["HealthData"]["PerformanceDegraded"] = performanceDegrade;
}

inline void getStateSensorData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const std::string& path,
                               const boost::system::error_code& ec,
                               const dbus::utility::MapperGetObject& object)
{
    BMCWEB_LOG_DEBUG("Get state sensor data.");
    if (ec)
    {
        return;
    }
    for (const auto& [service, interfaces] : object)
    {
        if (std::find(interfaces.begin(), interfaces.end(),
                      "com.nvidia.MemoryPerformance") != interfaces.end())
        {
            sdbusplus::asio::getProperty<std::string>(
                *crow::connections::systemBus, service, path,
                "com.nvidia.MemoryPerformance", "Value",
                [aResp](const boost::system::error_code& ec,
                        const std::string& property) {
                    populatePerformanceData(aResp, ec, property);
                });
        }
    }
}

inline void
    getStateSensorHandler(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                          const boost::system::error_code& ec,
                          const std::variant<std::vector<std::string>>& assoc)
{
    if (ec)
    {
        return;
    }
    const std::vector<std::string>* data =
        std::get_if<std::vector<std::string>>(&assoc);
    if (data == nullptr)
    {
        return;
    }
    for (const std::string& sensorPath : *data)
    {
        dbus::utility::getDbusObject(
            sensorPath,
            std::array<std::string_view, 1>{"com.nvidia.MemoryPerformance"},
            [aResp, sensorPath](const boost::system::error_code& ec,
                                const dbus::utility::MapperGetObject& object) {
                getStateSensorData(aResp, sensorPath, ec, object);
            });
    }
}

inline void getStateSensors(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get memory state sensors.");
    dbus::utility::findAssociations(
        objPath + "/all_states",
        [aResp](const boost::system::error_code& ec,
                std::variant<std::vector<std::string>>& assoc) {
            getStateSensorHandler(aResp, ec, assoc);
        });
}
