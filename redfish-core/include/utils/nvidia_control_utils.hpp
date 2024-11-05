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

#include "utils/nvidia_async_set_callbacks.hpp"

#include <utils/nvidia_async_set_utils.hpp>

namespace redfish
{
namespace nvidia_control_utils
{

static std::map<std::string, std::string> clockLimitModes = {
    {"com.nvidia.ClockMode.Mode.MaximumPerformance", "Automatic"},
    {"com.nvidia.ClockMode.Mode.OEM", "Override"},
    {"com.nvidia.ClockMode.Mode.PowerSaving", "Manual"},
    {"com.nvidia.ClockMode.Mode.Static", "Disabled"}};

inline void getClockLimitControlObjects(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisID, const std::string& chassisPath)
{
    nlohmann::json& members = asyncResp->res.jsonValue["Members"];
    members = nlohmann::json::array();
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID,
         &members](const boost::system::error_code,
                   std::variant<std::vector<std::string>>& resp) {
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            return;
        }
        for (const auto& object : *data)
        {
            sdbusplus::message::object_path objPath(object);
            members.push_back(
                {{"@odata.id", "/redfish/v1/Chassis/" + chassisID +
                                   "/Controls/" + objPath.filename()}});
        }
        asyncResp->res.jsonValue["Members@odata.count"] = members.size();
    },
        "xyz.openbmc_project.ObjectMapper", chassisPath + "/clock_controls",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void
    getChassisClockLimit(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& path,
                         const std::string& /*chassisPath*/)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, path](
            const boost::system::error_code errorno,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
        if (errorno)
        {
            BMCWEB_LOG_ERROR("ObjectMapper::GetObject call failed: {}",
                             errorno);
            messages::internalError(asyncResp->res);
            return;
        }

        for (const auto& element : objInfo)
        {
            for (const auto& interface : element.second)
            {
                if ((interface == "com.nvidia.ClockMode") ||
                    (interface ==
                     "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig") ||
                    (interface ==
                     "xyz.openbmc_project.Inventory.Decorator.Area"))
                {
                    crow::connections::systemBus->async_method_call(
                        [asyncResp, path, interface](
                            const boost::system::error_code errorno,
                            const std::vector<
                                std::pair<std::string,
                                          std::variant<uint32_t, std::string>>>&
                                propertiesList) {
                        if (errorno)
                        {
                            BMCWEB_LOG_ERROR(
                                "ObjectMapper::GetObject call failed:{}",
                                errorno);
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        for (const std::pair<
                                 std::string,
                                 std::variant<uint32_t, std::string>>&
                                 property : propertiesList)
                        {
                            std::string propertyName = property.first;
                            if (propertyName == "MaxSpeed")
                            {
                                propertyName = "AllowableMax";
                                const uint32_t* value =
                                    std::get_if<uint32_t>(&property.second);
                                if (value == nullptr)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "Internal errror for AllowableMax");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                asyncResp->res.jsonValue[propertyName] = *value;
                                continue;
                            }
                            else if (propertyName == "MinSpeed")
                            {
                                propertyName = "AllowableMin";
                                const uint32_t* value =
                                    std::get_if<uint32_t>(&property.second);
                                if (value == nullptr)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "Internal errror for AllowableMin");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                asyncResp->res.jsonValue[propertyName] = *value;
                                continue;
                            }
                            else if (propertyName == "RequestedSpeedLimitMax")
                            {
                                propertyName = "SettingMax";
                                const uint32_t* value =
                                    std::get_if<uint32_t>(&property.second);
                                if (value == nullptr)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "Internal errror for SettingMax");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                asyncResp->res.jsonValue[propertyName] = *value;
                                continue;
                            }
                            else if (propertyName == "RequestedSpeedLimitMin")
                            {
                                propertyName = "SettingMin";
                                const uint32_t* value =
                                    std::get_if<uint32_t>(&property.second);
                                if (value == nullptr)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "Internal errror for SettingMin");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                asyncResp->res.jsonValue[propertyName] = *value;
                                continue;
                            }
                            else if (propertyName == "PhysicalContext")
                            {
                                const std::string* physicalcontext =
                                    std::get_if<std::string>(&property.second);
                                asyncResp->res.jsonValue[propertyName] =
                                    redfish::dbus_utils::toPhysicalContext(
                                        *physicalcontext);
                                continue;
                            }
                            else if (propertyName == "ClockMode")
                            {
                                propertyName = "ControlMode";
                                const std::string* mode =
                                    std::get_if<std::string>(&property.second);
                                std::map<std::string, std::string>::iterator
                                    itr;
                                for (auto& itr : clockLimitModes)
                                {
                                    if (*mode == itr.first)
                                    {
                                        asyncResp->res.jsonValue[propertyName] =
                                            itr.second;
                                        break;
                                    }
                                }
                            }
                        }
                    },
                        element.first, path, "org.freedesktop.DBus.Properties",
                        "GetAll", interface);
                }
            }
        }
    },

        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", path,
        std::array<const char*, 0>());
}

inline void
    getClockLimitControl(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& chassisID,
                         const std::string& controlID,
                         const std::optional<std::string>& validChassisPath,
                         const std::string& processorName)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("Not a valid chassis ID:{}", chassisID);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisID);
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] = "#Control.v1_3_0.Control";
    asyncResp->res.jsonValue["SetPointUnits"] = "MHz";
    asyncResp->res.jsonValue["Id"] = controlID;
    asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/Chassis/" + chassisID +
                                            "/Controls/" + controlID;
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID, controlID, validChassisPath,
         processorName](const boost::system::error_code ec,
                        std::variant<std::vector<std::string>>& resp) {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "ObjectMapper::Get Associated clock control object call failed: {}",
                ec);
            messages::internalError(asyncResp->res);
            return;
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            BMCWEB_LOG_ERROR("control id resource not found");
            messages::resourceNotFound(asyncResp->res, "ControlID", controlID);
            return;
        }

        auto validendpoint = false;
        for (const auto& object : *data)
        {
            sdbusplus::message::object_path objPath(object);
            if (objPath.filename() == controlID)
            {
                asyncResp->res.jsonValue["Name"] =
                    "Control for " + processorName + " " + controlID;
                asyncResp->res.jsonValue["ControlType"] = "FrequencyMHz";
                asyncResp->res.jsonValue["Status"]["Health"] = "OK";
                asyncResp->res.jsonValue["Status"]["HealthRollup"] = "OK";
                nlohmann::json& relatedItemsArray =
                    asyncResp->res.jsonValue["RelatedItem"];
                relatedItemsArray = nlohmann::json::array();
                relatedItemsArray.push_back(
                    {{"@odata.id",
                      "/redfish/v1/Systems/HGX_Baseboard_0/Processors/" +
                          processorName}});

                asyncResp->res.jsonValue["Actions"]["#Control.ResetToDefaults"]
                                        ["target"] =
                    "/redfish/v1/Chassis/" + chassisID + "/Controls/" +
                    controlID + "/Actions/Control.ResetToDefaults";
                redfish::nvidia_control_utils::getChassisClockLimit(
                    asyncResp, object, *validChassisPath);
                validendpoint = true;
                break;
            }
        }
        if (!validendpoint)
        {
            BMCWEB_LOG_ERROR("control id resource not found");
            messages::resourceNotFound(asyncResp->res, "ControlID", controlID);
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        *validChassisPath + "/clock_controls",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
};

inline void
    changeClockLimitControl(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& path, const uint32_t& value,
                            const std::string& patchProp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, path, value, patchProp](
            const boost::system::error_code& errorno,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
        if (errorno)
        {
            BMCWEB_LOG_ERROR("ObjectMapper::GetObject call failed: {}",
                             errorno);
            messages::internalError(asyncResp->res);
            return;
        }
        for (const auto& element : objInfo)
        {
            if (patchProp == "SettingMin")

            {
                nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
                    asyncResp, std::chrono::seconds(60), element.first, path,
                    "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig",
                    "RequestedSpeedLimitMin", std::variant<uint32_t>(value),
                    nvidia_async_operation_utils::
                        PatchClockLimitControlCallback{asyncResp});
            }
            else if (patchProp == "SettingMax")

            {
                nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
                    asyncResp, std::chrono::seconds(60), element.first, path,
                    "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig",
                    "RequestedSpeedLimitMax", std::variant<uint32_t>(value),
                    nvidia_async_operation_utils::
                        PatchClockLimitControlCallback{asyncResp});
            }
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", path,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Item.Cpu.OperatingConfig"});
}

inline void
    patchClockLimitControl(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& chassisID,
                           const std::string& controlID,
                           const crow::Request& req,
                           const std::optional<std::string>& validChassisPath,
                           const std::string& processorName)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("Not a valid chassis ID:{}", chassisID);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisID);
        return;
    }
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID, controlID, validChassisPath, processorName,
         req](const boost::system::error_code ec,
              std::variant<std::vector<std::string>>& resp) {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "ObjectMapper::Get Associated clock control object call failed: {}",
                ec);
            messages::internalError(asyncResp->res);
            return;
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            BMCWEB_LOG_ERROR("control id resource not found");
            messages::resourceNotFound(asyncResp->res, "ControlID", controlID);
            return;
        }

        auto validendpoint = false;
        std::optional<uint32_t> settingMin;
        std::optional<uint32_t> settingMax;
        if (!json_util::readJsonAction(req, asyncResp->res, "SettingMin",
                                       settingMin, "SettingMax", settingMax))
        {
            return;
        }
        for (const auto& object : *data)
        {
            sdbusplus::message::object_path objPath(object);
            if (objPath.filename() == controlID)
            {
                if (settingMin)
                {
                    changeClockLimitControl(asyncResp, object, *settingMin,
                                            "SettingMin");
                }
                else if (settingMax)
                {
                    changeClockLimitControl(asyncResp, object, *settingMax,
                                            "SettingMax");
                }
                validendpoint = true;
                break;
            }
        }
        if (!validendpoint)
        {
            BMCWEB_LOG_ERROR("control id resource not found");
            messages::resourceNotFound(asyncResp->res, "ControlID", controlID);
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        *validChassisPath + "/clock_controls",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
};

inline void
    resetClockLimitControl(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connection,
                           const std::string& path)
{
    dbus::utility::getDbusObject(
        path,
        std::array<std::string_view, 1>{"com.nvidia.Common.ClearClockLimAsync"},
        [asyncResp, path,
         connection](const boost::system::error_code& ec,
                     const dbus::utility::MapperGetObject& object) {
        if (!ec)
        {
            for (const auto& [serv, _] : object)
            {
                if (serv != connection)
                {
                    continue;
                }

                BMCWEB_LOG_DEBUG("Performing Post using Async Method Call");

                nvidia_async_operation_utils::doGenericCallAsyncAndGatherResult<
                    int>(asyncResp, std::chrono::seconds(60), connection, path,
                         "com.nvidia.Common.ClearClockLimAsync",
                         "ClearClockLimit",
                         [asyncResp](const std::string& status,
                                     [[maybe_unused]] const int* retValue) {
                    if (status ==
                        nvidia_async_operation_utils::asyncStatusValueSuccess)
                    {
                        BMCWEB_LOG_DEBUG(
                            "Clear Requested clock Limit Succeeded");
                        messages::success(asyncResp->res);
                        return;
                    }
                    BMCWEB_LOG_ERROR(
                        "Clear Requested clock Limit Throws error {}", status);
                    messages::internalError(asyncResp->res);
                });

                return;
            }
        }
    });
};

inline void
    postClockLimitControl(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& chassisID,
                          const std::string& controlID,
                          const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        BMCWEB_LOG_ERROR("Not a valid chassis ID:{}", chassisID);
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisID);
        return;
    }
    crow::connections::systemBus->async_method_call(
        [asyncResp, chassisID, controlID,
         validChassisPath](const boost::system::error_code ec,
                           std::variant<std::vector<std::string>>& resp) {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "ObjectMapper::Get Associated clock control object call failed: {}",
                ec);
            messages::internalError(asyncResp->res);
            return;
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            BMCWEB_LOG_ERROR("control id resource not found");
            messages::resourceNotFound(asyncResp->res, "ControlID", controlID);
            return;
        }

        for (auto sensorpath : *data)
        {
            crow::connections::systemBus->async_method_call(
                [asyncResp, sensorpath](
                    const boost::system::error_code ec,
                    const std::vector<std::pair<
                        std::string, std::vector<std::string>>>& object) {
                if (ec)
                {
                    // the path does not implement clear clock limit interface
                    // interfaces
                    BMCWEB_LOG_DEBUG(
                        "no clear clock Limit interface on object path {}",
                        sensorpath);
                    return;
                }
                for (auto [connection, interfaces] : object)
                {
                    resetClockLimitControl(asyncResp, connection, sensorpath);
                }
            },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetObject", sensorpath,
                std::array<std::string, 1>(
                    {"com.nvidia.Common.ClearClockLimAsync"}));
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        *validChassisPath + "/clock_controls",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
};

inline void getControlSettingRelatedItems(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisPath)
{
    // Get the Processors Associations to cover all processors'
    // cases, If the object has `all_processors`, need to check
    // the further endpoint under the all_processors, which is
    // the actual processor
    nlohmann::json& relatedItemsArray = asyncResp->res.jsonValue["RelatedItem"];
    relatedItemsArray = nlohmann::json::array();
    sdbusplus::asio::getProperty<std::vector<std::string>>(
        *crow::connections::systemBus, "xyz.openbmc_project.ObjectMapper",
        chassisPath + "/all_processors", "xyz.openbmc_project.Association",
        "endpoints",
        [asyncResp, chassisPath,
         &relatedItemsArray](const boost::system::error_code ec,
                             const std::vector<std::string>& resp) {
        std::string getObjectPath;
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error");
            getObjectPath = chassisPath;
        }
        else
        {
            getObjectPath = resp.front();
        }

        sdbusplus::message::object_path objectPath(getObjectPath);
        std::string chassisName = objectPath.filename();
        if (chassisName.empty())
        {
            return;
        }
        std::string chassisLink = "/redfish/v1/Chassis/" + chassisName;
        // Only add chassisLink if it doesn't already exist
        if (std::find_if(relatedItemsArray.begin(), relatedItemsArray.end(),
                         [&chassisLink](const nlohmann::json& item) {
            return item["@odata.id"] == chassisLink;
        }) == relatedItemsArray.end())
        {
            relatedItemsArray.push_back({{"@odata.id", chassisLink}});
        }
    });
}

inline void
    getControlCpuObjects(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const auto getControlCpu,
                         const std::optional<std::string>& validChassisPath)
{
    // Get the Processors Associations to cover all processors' cases,
    // to ensure the object has `all_processors` and go ahead.
    sdbusplus::asio::getProperty<std::vector<std::string>>(
        *crow::connections::systemBus, "xyz.openbmc_project.ObjectMapper",
        *validChassisPath + "/all_processors",
        "xyz.openbmc_project.Association", "endpoints",
        [asyncResp, getControlCpu,
         validChassisPath](const boost::system::error_code ec,
                           const std::vector<std::string>& resp) {
        std::string objPath;
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error");
            objPath = *validChassisPath;
        }
        else
        {
            objPath = resp.front();
        }

        crow::connections::systemBus->async_method_call(
            [asyncResp, getControlCpu, objPath,
             validChassisPath](const boost::system::error_code ec,
                               const dbus::utility::MapperGetObject& objType) {
            if (ec || objType.empty())
            {
                BMCWEB_LOG_ERROR("GetObject for path {}", (objPath).c_str());
                return;
            }
            for (auto [service, interfaces] : objType)
            {
                if (std::find(interfaces.begin(), interfaces.end(),
                              "xyz.openbmc_project.Inventory.Item.Cpu") !=
                        interfaces.end() ||
                    std::find(
                        interfaces.begin(), interfaces.end(),
                        "xyz.openbmc_project.Inventory.Item.ProcessorModule") !=
                        interfaces.end())
                {
                    getControlCpu(objPath);
                    return;
                }
            }
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
            std::array<const char*, 0>{});
    });
}

} // namespace nvidia_control_utils
} // namespace redfish
