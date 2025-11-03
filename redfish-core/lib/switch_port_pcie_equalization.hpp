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

#include "dbus_utility.hpp"
#include "redfish_util.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/nvidia_async_set_callbacks.hpp"
#include "utils/pcie_util.hpp"

#include <app.hpp>
#include <utils/collection.hpp>
#include <utils/conditions_utils.hpp>
#include <utils/nvidia_async_set_utils.hpp>
#include <utils/nvidia_fabric_utils.hpp>
#include <utils/nvidia_histogram_utils.hpp>
#include <utils/nvidia_utils.hpp>
#include <utils/port_utils.hpp>
#include <utils/processor_utils.hpp>

#include <cstdint>
#include <variant>

namespace redfish
{

inline uint8_t getPresetfromRedfishString(const std::string& value)
{
    if (value == "DeviceDefault")
    {
        return 0;
    }
    if (value == "Preset0")
    {
        return 1;
    }
    if (value == "Preset1")
    {
        return 2;
    }
    if (value == "Preset2")
    {
        return 3;
    }
    if (value == "Preset3")
    {
        return 4;
    }
    if (value == "Preset4")
    {
        return 5;
    }
    if (value == "Preset5")
    {
        return 6;
    }
    if (value == "Preset6")
    {
        return 7;
    }
    if (value == "Preset7")
    {
        return 8;
    }
    if (value == "Preset8")
    {
        return 9;
    }
    if (value == "Preset9")
    {
        return 10;
    }
    return 0;
}

inline std::string getStringfromDbusTxPreset(const std::string& value)
{
    if (value ==
        "xyz.openbmc_project.PCIe.PCIePortConfigurationInfo.PCIePresetIndex.DeviceDefault")
    {
        return "DeviceDefault";
    }
    if (value ==
        "xyz.openbmc_project.PCIe.PCIePortConfigurationInfo.PCIePresetIndex.Preset0")
    {
        return "Preset0";
    }
    if (value ==
        "xyz.openbmc_project.PCIe.PCIePortConfigurationInfo.PCIePresetIndex.Preset1")
    {
        return "Preset1";
    }
    if (value ==
        "xyz.openbmc_project.PCIe.PCIePortConfigurationInfo.PCIePresetIndex.Preset2")
    {
        return "Preset2";
    }
    if (value ==
        "xyz.openbmc_project.PCIe.PCIePortConfigurationInfo.PCIePresetIndex.Preset3")
    {
        return "Preset3";
    }
    if (value ==
        "xyz.openbmc_project.PCIe.PCIePortConfigurationInfo.PCIePresetIndex.Preset4")
    {
        return "Preset4";
    }
    if (value ==
        "xyz.openbmc_project.PCIe.PCIePortConfigurationInfo.PCIePresetIndex.Preset5")
    {
        return "Preset5";
    }
    if (value ==
        "xyz.openbmc_project.PCIe.PCIePortConfigurationInfo.PCIePresetIndex.Preset6")
    {
        return "Preset6";
    }
    if (value ==
        "xyz.openbmc_project.PCIe.PCIePortConfigurationInfo.PCIePresetIndex.Preset7")
    {
        return "Preset7";
    }
    if (value ==
        "xyz.openbmc_project.PCIe.PCIePortConfigurationInfo.PCIePresetIndex.Preset8")
    {
        return "Preset8";
    }
    if (value ==
        "xyz.openbmc_project.PCIe.PCIePortConfigurationInfo.PCIePresetIndex.Preset9")
    {
        return "Preset9";
    }
    return "";
}

/*
 * @brief Get PCIe Equalization info by requesting data
 * from the given D-Bus object.
 *
 * @param[in,out]   aResp   Async HTTP response.
 * @param[in]       portObjectPath     D-Bus object to query.
 * @param[in]       object     D-Bus object to query.
 */
inline void getPCIeEqualization(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& portObjectPath,
    const std::vector<std::pair<std::string, std::vector<std::string>>>& object)
{
    BMCWEB_LOG_DEBUG("Get PCIe Equalization");
    dbus::utility::async_method_call(
        [asyncResp, portObjectPath, object](
            const boost::system::error_code& ec2,
            const boost::container::flat_map<
                std::string, std::variant<uint64_t, std::vector<std::string>>>&
                properties) {
            if (ec2)
            {
                BMCWEB_LOG_ERROR("DBUS response error for PCIe Equalization {}",
                                 ec2.message());
                messages::internalError(asyncResp->res);
                return;
            }
            std::vector<std::string> txPresetGen3 = {"", ""};
            std::vector<std::string> txPresetGen4 = {"", ""};
            std::vector<std::string> txPresetGen5 = {"", ""};
            std::vector<std::string> txPresetGen6 = {"", ""};

            // Get port protocol
            for (const auto& property : properties)
            {
                if (property.first == "TxAmplitude")
                {
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for TxAmplitude");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["TxAmplitude"] = *value;
                }
                else if (property.first == "Preset0" ||
                         property.first == "Preset1")
                {
                    const std::vector<std::string>* value =
                        std::get_if<std::vector<std::string>>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for TxPreemphasis");
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    uint8_t rfTxPresetIndex = 0;
                    if (property.first == "Preset0")
                    {
                        rfTxPresetIndex = 0;
                    }
                    else if (property.first == "Preset1")
                    {
                        rfTxPresetIndex = 1;
                    }

                    for (size_t i = 0; i < (*value).size(); i++)
                    {
                        std::string txPresetValue =
                            getStringfromDbusTxPreset((*value)[i]);
                        if (i == 0)
                        {
                            txPresetGen3[rfTxPresetIndex] = txPresetValue;
                        }
                        else if (i == 1)
                        {
                            txPresetGen4[rfTxPresetIndex] = txPresetValue;
                        }
                        else if (i == 2)
                        {
                            txPresetGen5[rfTxPresetIndex] = txPresetValue;
                        }
                        else if (i == 3)
                        {
                            txPresetGen6[rfTxPresetIndex] = txPresetValue;
                        }
                    }
                    asyncResp->res.jsonValue["TxPresets"]["Gen3"] =
                        txPresetGen3;
                    asyncResp->res.jsonValue["TxPresets"]["Gen4"] =
                        txPresetGen4;
                    asyncResp->res.jsonValue["TxPresets"]["Gen5"] =
                        txPresetGen5;
                    asyncResp->res.jsonValue["TxPresets"]["Gen6"] =
                        txPresetGen6;
                }
            }
        },
        object.front().first, portObjectPath, "org.freedesktop.DBus.Properties",
        "GetAll", "xyz.openbmc_project.PCIe.PCIePortConfigurationInfo");
}

inline void requestRoutesPCIeEqualization(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Fabrics/<str>/Switches/<str>/Ports/<str>/Oem/Nvidia/PCIeEqualization")
        .privileges({{"Login"}})
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& fabricId, const std::string& switchId,
                   const std::string& portId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                crow::connections::systemBus->async_method_call(
                    [asyncResp{asyncResp}, fabricId, switchId,
                     portId](const boost::system::error_code ec,
                             const std::vector<std::string>& objects) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR("DBUS response error");
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        for (const std::string& fabricPath : objects)
                        {
                            // Get the fabricId object
                            if (!fabricPath.ends_with(fabricId))
                            {
                                continue;
                            }

                            dbus::utility::getProperty<std::vector<
                                std::
                                    string>>("xyz.openbmc_project.ObjectMapper",
                                             fabricPath + "/all_switches",
                                             "xyz.openbmc_project.Association",
                                             "endpoints",
                                             [asyncResp, fabricId, switchId,
                                              portId](
                                                 const boost::system::error_code
                                                     ec3,
                                                 const std::vector<std::string>&
                                                     resp3) {
                                                 if (ec3)
                                                 {
                                                     BMCWEB_LOG_ERROR(
                                                         "DBUS response error");
                                                     messages::internalError(
                                                         asyncResp->res);
                                                     return;
                                                 }

                                                 for (const std::string&
                                                          switchPath : resp3)
                                                 {
                                                     if (!switchPath.ends_with(
                                                             switchId))
                                                     {
                                                         continue;
                                                     }

                                                     dbus::utility::getProperty<
                                                         std::vector<std::string>>("xyz.openbmc_project.ObjectMapper",
                                                                                   switchPath +
                                                                                       "/all_states",
                                                                                   "xyz.openbmc_project.Association",
                                                                                   "endpoints",
                                                                                   [asyncResp,
                                                                                    fabricId,
                                                                                    switchId,
                                                                                    portId](
                                                                                       const boost::
                                                                                           system::error_code
                                                                                               ec4,
                                                                                       const std::
                                                                                           vector<
                                                                                               std::
                                                                                                   string>&
                                                                                               resp4) {
                                                                                       if (ec4)
                                                                                       {
                                                                                           BMCWEB_LOG_ERROR(
                                                                                               "DBUS response error");
                                                                                           messages::internalError(
                                                                                               asyncResp
                                                                                                   ->res);
                                                                                           return;
                                                                                       }

                                                                                       for (
                                                                                           const std::
                                                                                               string&
                                                                                                   portPath :
                                                                                           resp4)
                                                                                       {
                                                                                           // Get the portId object
                                                                                           sdbusplus::
                                                                                               message::object_path
                                                                                                   pPath(
                                                                                                       portPath);
                                                                                           if (pPath
                                                                                                   .filename() !=
                                                                                               portId)
                                                                                           {
                                                                                               continue;
                                                                                           }
                                                                                           crow::connections::systemBus
                                                                                               ->async_method_call(
                                                                                                   [asyncResp,
                                                                                                    portPath,
                                                                                                    fabricId,
                                                                                                    switchId,
                                                                                                    portId](
                                                                                                       const boost::
                                                                                                           system::error_code&
                                                                                                               ec5,
                                                                                                       const std::vector<std::pair<
                                                                                                           std::
                                                                                                               string,
                                                                                                           std::vector<
                                                                                                               std::
                                                                                                                   string>>>&
                                                                                                           object) {
                                                                                                       if (ec5 ||
                                                                                                           object
                                                                                                               .empty())
                                                                                                       {
                                                                                                           BMCWEB_LOG_DEBUG(
                                                                                                               "No PCIe Equalization found {}",
                                                                                                               portPath);
                                                                                                           return;
                                                                                                       }

                                                                                                       std::string
                                                                                                           portEqualizationURI =
                                                                                                               "/redfish/v1/Fabrics/";
                                                                                                       portEqualizationURI +=
                                                                                                           fabricId;
                                                                                                       portEqualizationURI +=
                                                                                                           "/Switches/";
                                                                                                       portEqualizationURI +=
                                                                                                           switchId;
                                                                                                       portEqualizationURI +=
                                                                                                           "/Ports/";
                                                                                                       portEqualizationURI +=
                                                                                                           portId;
                                                                                                       portEqualizationURI +=
                                                                                                           "/Oem/Nvidia/PCIeEqualization";

                                                                                                       asyncResp
                                                                                                           ->res
                                                                                                           .jsonValue
                                                                                                               ["@odata.type"] =
                                                                                                           "#NvidiaPCIeEqualization.v1_0_0.NvidiaPCIeEqualization";
                                                                                                       asyncResp
                                                                                                           ->res
                                                                                                           .jsonValue
                                                                                                               ["@odata.id"] =
                                                                                                           portEqualizationURI;
                                                                                                       asyncResp
                                                                                                           ->res
                                                                                                           .jsonValue
                                                                                                               ["Name"] =
                                                                                                           "HGX_PCIeTopology_0 Switches ConnectX_Switch_0 Ports UP_0 Oem Nvidia PCIeEqualization";
                                                                                                       asyncResp
                                                                                                           ->res
                                                                                                           .jsonValue
                                                                                                               ["Id"] =
                                                                                                           "PCIeEqualization";

                                                                                                       getPCIeEqualization(
                                                                                                           asyncResp,
                                                                                                           portPath,
                                                                                                           object);
                                                                                                   },
                                                                                                   "xyz.openbmc_project.ObjectMapper",
                                                                                                   "/xyz/openbmc_project/object_mapper",
                                                                                                   "xyz.openbmc_project.ObjectMapper",
                                                                                                   "GetObject",
                                                                                                   portPath,
                                                                                                   std::array<
                                                                                                       std::
                                                                                                           string,
                                                                                                       1>(
                                                                                                       {"xyz.openbmc_project.PCIe.PCIePortConfigurationInfo"}));
                                                                                       }
                                                                                   });
                                                     return;
                                                 }

                                                 // Couldn't find an
                                                 // object with that
                                                 // name. Return an error
                                                 messages::resourceNotFound(
                                                     asyncResp->res,
                                                     "#Switch.v1_8_0.Switch",
                                                     switchId);
                                             });
                            return;
                        }

                        // Couldn't find an object with that name.
                        // Return an error
                        messages::resourceNotFound(
                            asyncResp->res, "#Fabric.v1_2_0.Fabric", fabricId);
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                    "/xyz/openbmc_project/inventory", 0,
                    std::array<const char*, 1>{
                        "xyz.openbmc_project.Inventory.Item.Fabric"});
            });

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Fabrics/<str>/Switches/<str>/Ports/<str>/Oem/Nvidia/PCIeEqualization")
        .privileges({{"Login"}})
        .methods(
            boost::beast::http::verb::
                patch)([&app](
                           const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& fabricId,
                           const std::string& switchId,
                           const std::string& portId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            std::vector<std::tuple<std::string, uint32_t>> portEqualizationData;
            std::optional<nlohmann::json> txPresets;
            std::optional<nlohmann::json> txAmplitudeJson;
            if (!redfish::json_util::readJsonAction(
                    req, asyncResp->res, "TxAmplitude", txAmplitudeJson,
                    "TxPresets", txPresets))
            {
                BMCWEB_LOG_ERROR("Missing property TxAmplitude or TxPresets");
                return;
            }
            std::optional<uint32_t> txAmplitude;
            if (txAmplitudeJson)
            {
                // Accept numeric or numeric-string values
                if (txAmplitudeJson->is_number_unsigned())
                {
                    txAmplitude = txAmplitudeJson->get<uint32_t>();
                }
                else if (txAmplitudeJson->is_number_integer())
                {
                    auto v = txAmplitudeJson->get<int64_t>();
                    if (v >= 0 && v <= static_cast<int64_t>(UINT32_MAX))
                    {
                        txAmplitude = static_cast<uint32_t>(v);
                    }
                }
                else if (txAmplitudeJson->is_string())
                {
                    const std::string& s =
                        txAmplitudeJson->get_ref<const std::string&>();
                    try
                    {
                        unsigned long ul = std::stoul(s);
                        if (ul <= UINT32_MAX)
                        {
                            txAmplitude = static_cast<uint32_t>(ul);
                        }
                    }
                    catch (const std::exception&)
                    {
                        BMCWEB_LOG_DEBUG(
                            "Invalid TxAmplitude string value; falling back to type error");
                        // fall through to type error
                    }
                }

                if (!txAmplitude)
                {
                    messages::propertyValueTypeError(
                        asyncResp->res, *txAmplitudeJson, "TxAmplitude");
                    return;
                }

                portEqualizationData.emplace_back("TxAmplitude", *txAmplitude);
            }

            std::optional<std::vector<std::string>> txPresetGen3;
            std::optional<std::vector<std::string>> txPresetGen4;
            std::optional<std::vector<std::string>> txPresetGen5;
            std::optional<std::vector<std::string>> txPresetGen6;
            if (txPresets &&
                redfish::json_util::readJson(
                    *txPresets, asyncResp->res, "Gen3", txPresetGen3, "Gen4",
                    txPresetGen4, "Gen5", txPresetGen5, "Gen6", txPresetGen6))
            {
                if (txPresetGen3)
                {
                    uint32_t txPresetGen3Value = 0;
                    uint8_t gen3Preset0 =
                        getPresetfromRedfishString(txPresetGen3->at(0));
                    uint8_t gen3Preset1 =
                        getPresetfromRedfishString(txPresetGen3->at(1));
                    uint8_t presetValue = static_cast<uint8_t>(
                        (gen3Preset1 << 4) | (gen3Preset0 & 0x0F));
                    txPresetGen3Value = static_cast<uint32_t>(presetValue);
                    portEqualizationData.emplace_back("PresetGen3",
                                                      txPresetGen3Value);
                }
                if (txPresetGen4)
                {
                    uint32_t txPresetGen4Value = 0;
                    uint8_t gen4Preset0 =
                        getPresetfromRedfishString(txPresetGen4->at(0));
                    uint8_t gen4Preset1 =
                        getPresetfromRedfishString(txPresetGen4->at(1));
                    uint8_t presetValue = static_cast<uint8_t>(
                        (gen4Preset1 << 4) | (gen4Preset0 & 0x0F));
                    txPresetGen4Value = static_cast<uint32_t>(presetValue);
                    portEqualizationData.emplace_back("PresetGen4",
                                                      txPresetGen4Value);
                }
                if (txPresetGen5)
                {
                    uint32_t txPresetGen5Value = 0;
                    uint8_t gen5Preset0 =
                        getPresetfromRedfishString(txPresetGen5->at(0));
                    uint8_t gen5Preset1 =
                        getPresetfromRedfishString(txPresetGen5->at(1));
                    uint8_t presetValue = static_cast<uint8_t>(
                        (gen5Preset1 << 4) | (gen5Preset0 & 0x0F));
                    txPresetGen5Value = static_cast<uint32_t>(presetValue);
                    portEqualizationData.emplace_back("PresetGen5",
                                                      txPresetGen5Value);
                }
                if (txPresetGen6)
                {
                    uint32_t txPresetGen6Value = 0;
                    uint8_t gen6Preset0 =
                        getPresetfromRedfishString(txPresetGen6->at(0));
                    uint8_t gen6Preset1 =
                        getPresetfromRedfishString(txPresetGen6->at(1));
                    uint8_t presetValue = static_cast<uint8_t>(
                        (gen6Preset1 << 4) | (gen6Preset0 & 0x0F));
                    txPresetGen6Value = static_cast<uint32_t>(presetValue);
                    portEqualizationData.emplace_back("PresetGen6",
                                                      txPresetGen6Value);
                }
            }
            if (portEqualizationData.empty())
            {
                BMCWEB_LOG_ERROR("Missing property TxAmplitude, TxPreset");
                return;
            }
            crow::connections::systemBus->async_method_call(
                [asyncResp{asyncResp}, fabricId, switchId, portId,
                 portEqualizationData](
                    const boost::system::error_code ec,
                    const std::vector<std::string>& objects) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("DBUS response error");
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (const std::string& fabricPath : objects)
                    {
                        // Get the fabricId object
                        if (!fabricPath.ends_with(fabricId))
                        {
                            continue;
                        }

                        dbus::utility::getProperty<std::vector<
                            std::string>>("xyz.openbmc_project.ObjectMapper",
                                          fabricPath + "/all_switches",
                                          "xyz.openbmc_project.Association",
                                          "endpoints",
                                          [asyncResp, fabricId, switchId,
                                           portId, portEqualizationData](
                                              const boost::system::error_code
                                                  ec3,
                                              const std::vector<std::string>&
                                                  resp3) {
                                              if (ec3)
                                              {
                                                  BMCWEB_LOG_ERROR(
                                                      "DBUS response error");
                                                  messages::internalError(
                                                      asyncResp->res);
                                                  return;
                                              }

                                              for (const std::string&
                                                       switchPath : resp3)
                                              {
                                                  if (!switchPath.ends_with(
                                                          switchId))
                                                  {
                                                      continue;
                                                  }

                                                  dbus::utility::
                                                      getProperty<
                                                          std::vector<std::string>>("xyz.openbmc_project.ObjectMapper",
                                                                                    switchPath +
                                                                                        "/all_states",
                                                                                    "xyz.openbmc_project.Association",
                                                                                    "endpoints",
                                                                                    [asyncResp,
                                                                                     fabricId,
                                                                                     switchId,
                                                                                     portId,
                                                                                     portEqualizationData](
                                                                                        const boost::
                                                                                            system::error_code
                                                                                                ec4,
                                                                                        const std::vector<
                                                                                            std::
                                                                                                string>&
                                                                                            resp4) {
                                                                                        if (ec4)
                                                                                        {
                                                                                            BMCWEB_LOG_ERROR(
                                                                                                "DBUS response error");
                                                                                            messages::internalError(
                                                                                                asyncResp
                                                                                                    ->res);
                                                                                            return;
                                                                                        }

                                                                                        for (
                                                                                            const std::
                                                                                                string&
                                                                                                    portPath :
                                                                                            resp4)
                                                                                        {
                                                                                            // Get the portId object
                                                                                            sdbusplus::
                                                                                                message::object_path
                                                                                                    pPath(
                                                                                                        portPath);
                                                                                            if (pPath
                                                                                                    .filename() !=
                                                                                                portId)
                                                                                            {
                                                                                                continue;
                                                                                            }
                                                                                            crow::connections::systemBus
                                                                                                ->async_method_call(
                                                                                                    [asyncResp,
                                                                                                     portPath,
                                                                                                     fabricId,
                                                                                                     switchId,
                                                                                                     portId,
                                                                                                     portEqualizationData](
                                                                                                        const boost::
                                                                                                            system::error_code&
                                                                                                                ec5,
                                                                                                        const std::vector<std::pair<
                                                                                                            std::
                                                                                                                string,
                                                                                                            std::vector<
                                                                                                                std::
                                                                                                                    string>>>&
                                                                                                            object) {
                                                                                                        if (ec5 ||
                                                                                                            object
                                                                                                                .empty())
                                                                                                        {
                                                                                                            BMCWEB_LOG_DEBUG(
                                                                                                                "No PCIe Equalization found {}",
                                                                                                                portPath);
                                                                                                            return;
                                                                                                        }

                                                                                                        nvidia_async_operation_utils::doGenericSetAsyncAndGatherResult(
                                                                                                            asyncResp,
                                                                                                            std::chrono::
                                                                                                                seconds(
                                                                                                                    60),
                                                                                                            object
                                                                                                                .front()
                                                                                                                .first,
                                                                                                            portPath,
                                                                                                            "xyz.openbmc_project.PCIe.PCIePortConfigurationInfo",
                                                                                                            "TxAmplitude",
                                                                                                            std::variant<std::vector<
                                                                                                                std::tuple<
                                                                                                                    std::
                                                                                                                        string,
                                                                                                                    uint32_t>>>(
                                                                                                                portEqualizationData),
                                                                                                            nvidia_async_operation_utils::
                                                                                                                PatchPCIeEqualizationCallback{
                                                                                                                    asyncResp});
                                                                                                    },
                                                                                                    "xyz.openbmc_project.ObjectMapper",
                                                                                                    "/xyz/openbmc_project/object_mapper",
                                                                                                    "xyz.openbmc_project.ObjectMapper",
                                                                                                    "GetObject",
                                                                                                    portPath,
                                                                                                    std::array<
                                                                                                        std::
                                                                                                            string,
                                                                                                        1>(
                                                                                                        {"xyz.openbmc_project.PCIe.PCIePortConfigurationInfo"}));
                                                                                        }
                                                                                    });
                                                  return;
                                              }

                                              // Couldn't find an
                                              // object with that
                                              // name. Return an error
                                              messages::resourceNotFound(
                                                  asyncResp->res,
                                                  "#Switch.v1_8_0.Switch",
                                                  switchId);
                                          });
                        return;
                    }

                    // Couldn't find an object with that name.
                    // Return an error
                    messages::resourceNotFound(
                        asyncResp->res, "#Fabric.v1_2_0.Fabric", fabricId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
                "/xyz/openbmc_project/inventory", 0,
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Inventory.Item.Fabric"});
        });
}
} // namespace redfish
