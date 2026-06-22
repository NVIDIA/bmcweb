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

#include "bmcweb_config.h"

#include "app.hpp"
#include "dbus_utility.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/hex_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_async_call_utils.hpp"
#include "utils/nvidia_async_set_callbacks.hpp"
#include "utils/nvidia_async_set_utils.hpp"

#include <boost/container/flat_map.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/conditions_utils.hpp>
#include <utils/nvidia_power_smoothing_util.hpp>

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
namespace redfish
{
using DbusProperties =
    std::vector<std::pair<std::string, dbus::utility::DbusVariantType>>;

inline void getProcessorCurrentProfileData(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& service,
    const std::string& objPath, const std::string& presetProfileURI)
{
    BMCWEB_LOG_DEBUG("Get processor current profile data.");
    dbus::utility::getAllProperties(
        service, objPath, "com.nvidia.PowerSmoothing.CurrentPowerProfile",
        [aResp{std::move(aResp)}, objPath,
         presetProfileURI](const boost::system::error_code& ec,
                           const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& property : properties)
            {
                if (property.first == "RampDownHysteresis")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("RampDownHysteresis nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["RampDownHysteresisSeconds"] = *value;
                }
                else if (property.first == "RampDownHysteresisApplied")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("RampDownHysteresisApplied nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["AdminOverrideActiveMask"]
                                        ["RampDownHysteresisSecondsApplied"] =
                        *value;
                }
                else if (property.first == "RampDownRate")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("RampDownRate nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["RampDownWattsPerSecond"] = *value;
                }
                else if (property.first == "RampDownRateApplied")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("RampDownRateApplied nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["AdminOverrideActiveMask"]
                                        ["RampDownWattsPerSecondApplied"] =
                        *value;
                }
                else if (property.first == "RampUpRate")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("RampUpRate nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["RampUpWattsPerSecond"] = *value;
                }
                else if (property.first == "RampUpRateApplied")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("RampUpRateApplied nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["AdminOverrideActiveMask"]
                                        ["RampUpWattsPerSecondApplied"] =
                        *value;
                }
                else if (property.first == "TMPFloorPercent")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("TMPFloorPercent nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["TMPFloorPercent"] = *value;
                }
                else if (property.first == "TMPFloorPercentApplied")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("TMPFloorPercentApplied nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["AdminOverrideActiveMask"]
                                        ["TMPFloorPercentApplied"] = *value;
                }
                else if (property.first == "SecondaryPowerFloorSettingApplied")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "SecondaryPowerFloorSettingApplied nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["AdminOverrideActiveMask"]
                                        ["SecondaryPowerFloorSettingApplied"] =
                        *value;
                }
                else if (property.first == "SecondaryPowerFloorSetting")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("SecondaryPowerFloorSetting nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["SecondaryPowerFloorWatts"] = *value;
                }
                else if (property.first ==
                         "PrimaryFloorActivationWindowMultiplier")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "PrimaryFloorActivationWindowMultiplier nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["PFAWindowMultiplier"] = *value;
                }
                else if (property.first ==
                         "PrimaryFloorActivationWindowMultiplierApplied")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "PrimaryFloorActivationWindowMultiplierApplied nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["AdminOverrideActiveMask"]
                                        ["PFAWindowMultiplierSettingApplied"] =
                        *value;
                }
                else if (property.first == "PrimaryFloorTargetWindowMultiplier")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "PrimaryFloorTargetWindowMultiplier nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["PFTWindowMultiplier"] = *value;
                }
                else if (property.first ==
                         "PrimaryFloorTargetWindowMultiplierApplied")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "PrimaryFloorTargetWindowMultiplierApplied nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["AdminOverrideActiveMask"]
                                        ["PFTWindowMultiplierSettingApplied"] =
                        *value;
                }
                else if (property.first ==
                         "PrimaryFloorTargetWindowMultiplierApplied")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "PrimaryFloorTargetWindowMultiplierApplied nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["AdminOverrideActiveMask"]
                                        ["PFTWindowMultiplierSettingApplied"] =
                        *value;
                }
                else if (property.first == "PrimaryFloorActivationOffset")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "PrimaryFloorActivationOffset nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["PFAOffsetWatts"] = *value;
                }
                else if (property.first ==
                         "PrimaryFloorActivationOffsetApplied")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "PrimaryFloorActivationOffsetApplied nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["AdminOverrideActiveMask"]
                                        ["PFAOffsetSettingApplied"] = *value;
                }
                else if (property.first == "AppliedProfilePath")
                {
                    const sdbusplus::object_path* value =
                        std::get_if<sdbusplus::object_path>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("AppliedPresetProfile nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    if (*value != objPath)
                    {
                        std::string appliedProfile = presetProfileURI;
                        appliedProfile += "/";
                        appliedProfile += value->filename();
                        aResp->res
                            .jsonValue["AppliedPresetProfile"]["@odata.id"] =
                            appliedProfile;
                    }
                    else
                    {
                        BMCWEB_LOG_ERROR("Invalud AppliedPresetProfile");
                    }
                }
            }
        });
}

inline void getProcessorPowerSmoothingControlData(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& service,
    const std::string& objPath, const std::string& presetProfileURI)
{
    BMCWEB_LOG_DEBUG("Get processor smoothing control data.");
    dbus::utility::getAllProperties(
        service, objPath, "com.nvidia.PowerSmoothing.PowerSmoothing",
        [aResp{std::move(aResp)}, objPath, service,
         presetProfileURI](const boost::system::error_code& ec,
                           const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& property : properties)
            {
                if (property.first == "PowerSmoothingEnabled")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("PowerSmoothingEnable nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["Enabled"] = *value;
                }
                else if (property.first == "ImmediateRampDownEnabled")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("ImmediateRampDownEnabled nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["ImmediateRampDown"] = *value;
                }
                else if (property.first == "CurrentTempSetting")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("CurrentTempSetting nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["TMPWatts"] = *value;
                }
                else if (property.first == "CurrentTempFloorSetting")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("CurrentTempFloorSetting nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["TMPFloorWatts"] = *value;
                }
                else if (property.first == "MaxAllowedTmpFloorPercent")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("MaxAllowedTmpFloorPercent nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["MaxAllowedTMPFloorPercent"] = *value;
                }
                else if (property.first == "MinAllowedTmpFloorPercent")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("MinAllowedTmpFloorPercent nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["MinAllowedTMPFloorPercent"] = *value;
                }
                else if (property.first == "LifeTimeRemaining")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("LifeTimeRemaining nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["RemainingLifetimeCircuitryPercent"] =
                        *value;
                }
                else if (property.first == "FeatureSupported")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("FeatureSupported nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["PowerSmoothingSupported"] = *value;
                }
                else if (property.first == "DelayedPowerSmoothingSupported")
                {
                    const bool* value = std::get_if<bool>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "DelayedPowerSmoothingSupported nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["DelayedPowerSmoothingSupported"] =
                        *value;
                }
                else if (property.first == "FloorWindowMultiplier")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("FloorWindowMultiplier nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["FloorWindowMultiplierPeriod"] =
                        *value;
                }
                else if (property.first == "MinPrimaryFloorActivationOffset")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "MinPrimaryFloorActivationOffset nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["MinAllowedPFAOffsetWatts"] = *value;
                }
                else if (property.first == "MinPrimaryFloorActivationPoint")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "MinPrimaryFloorActivationPoint nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["MinAllowedPFAPointWatts"] = *value;
                }
            }
        });
}

/**
 * @brief Get StateOfChargeFeatures (MaxACPowerRampRateWattsPerSecond,
 * PowerSmoothingEnabled) from processor object and add to PowerSmoothing JSON.
 */
inline void getProcessorPowerSmoothingStateOfChargeFeatures(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& service,
    const std::string& objPath)
{
    constexpr std::string_view iface =
        "com.nvidia.PowerSmoothing.StateOfChargeFeatures";
    dbus::utility::getAllProperties(
        service, objPath, std::string(iface),
        [aResp](const boost::system::error_code& ec,
                const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG(
                    "StateOfChargeFeatures not available or error: {}", ec);
                return;
            }
            nlohmann::json& soc = aResp->res.jsonValue["StateOfChargeFeatures"];
            for (const auto& [name, value] : properties)
            {
                if (name == "MaxACPowerRampRateWattsPerSecond")
                {
                    const uint32_t* v = std::get_if<uint32_t>(&value);
                    if (v == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "MaxACPowerRampRateWattsPerSecond has invalid type");
                        messages::internalError(aResp->res);
                        return;
                    }
                    soc["MaxACPowerRampRateWattsPerSecond"] = *v;
                }
                else if (name == "PowerSmoothingEnabled")
                {
                    const bool* v = std::get_if<bool>(&value);
                    if (v == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "PowerSmoothingEnabled has invalid type");
                        messages::internalError(aResp->res);
                        return;
                    }
                    soc["PowerSmoothingEnabled"] = *v;
                }
            }
        });
}

inline void getProcessorPowerSmoothingData(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& processorId)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    std::array<std::string_view, 3> processorifaces = {
        "xyz.openbmc_project.Inventory.Item.Accelerator",
        "xyz.openbmc_project.Inventory.Item.Cpu",
        "com.nvidia.PowerSmoothing.PowerSmoothing"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, processorifaces,
        [processorId, aResp{std::move(aResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!path.ends_with(processorId))
                {
                    continue;
                }
                std::string pwrSmoothingURI =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/Processors/";
                pwrSmoothingURI += processorId;
                pwrSmoothingURI += "/Oem/Nvidia/PowerSmoothing";
                aResp->res.jsonValue["@odata.type"] =
                    "#NvidiaPowerSmoothing.v1_4_0.NvidiaPowerSmoothing";
                aResp->res.jsonValue["@odata.id"] = pwrSmoothingURI;
                aResp->res.jsonValue["Id"] = "PowerSmoothing";
                aResp->res.jsonValue["Name"] = processorId + " Power Smoothing";

                std::string presetProfileURI = pwrSmoothingURI;
                presetProfileURI += "/PresetProfiles";
                aResp->res.jsonValue["PresetProfiles"]["@odata.id"] =
                    presetProfileURI;

                std::string adminOverrideProfileURI = pwrSmoothingURI;
                adminOverrideProfileURI += "/AdminOverrideProfile";
                aResp->res.jsonValue["AdminOverrideProfile"]["@odata.id"] =
                    adminOverrideProfileURI;

                std::string activatePresetProfilesInfoURI = pwrSmoothingURI;
                activatePresetProfilesInfoURI +=
                    "/ActivatePresetProfileActionInfo";
                aResp->res
                    .jsonValue["Actions"]
                              ["#NvidiaPowerSmoothing.ActivatePresetProfile"]
                              ["@Redfish.ActionInfo"] =
                    activatePresetProfilesInfoURI;

                std::string activatePresetProfilesURI = pwrSmoothingURI;
                activatePresetProfilesURI +=
                    "/Actions/NvidiaPowerSmoothing.ActivatePresetProfile";
                aResp->res
                    .jsonValue["Actions"]
                              ["#NvidiaPowerSmoothing.ActivatePresetProfile"]
                              ["target"] = activatePresetProfilesURI;

                std::string adminOverrideURI = pwrSmoothingURI;
                adminOverrideURI +=
                    "/Actions/NvidiaPowerSmoothing.ApplyAdminOverrides";
                aResp->res
                    .jsonValue["Actions"]
                              ["#NvidiaPowerSmoothing.ApplyAdminOverrides"]
                              ["target"] = adminOverrideURI;

                for (const auto& [service, interfaceList] : object)
                {
                    if (std::ranges::find(
                            interfaceList,
                            "com.nvidia.PowerSmoothing.PowerSmoothing") ==
                        interfaceList.end())
                    {
                        // no interface = no failures
                        continue;
                    }
                    getProcessorPowerSmoothingControlData(aResp, service, path,
                                                          presetProfileURI);
                    if (std::ranges::find(
                            interfaceList,
                            "com.nvidia.PowerSmoothing.CurrentPowerProfile") ==
                        interfaceList.end())
                    {
                        continue;
                    }
                    getProcessorCurrentProfileData(aResp, service, path,
                                                   presetProfileURI);

                    if (std::ranges::find(
                            interfaceList,
                            "com.nvidia.PowerSmoothing.StateOfChargeFeatures") !=
                        interfaceList.end())
                    {
                        getProcessorPowerSmoothingStateOfChargeFeatures(
                            aResp, service, path);
                    }
                    return;
                }
                // Object not found
                BMCWEB_LOG_ERROR(
                    "Resource not found #NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing for {}",
                    processorId);
                messages::resourceNotFound(
                    aResp->res,
                    "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                    processorId);
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                processorId);
        });
}

inline void getAdminProfileData(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                const std::string& service,
                                const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get processor current profile data.");
    dbus::utility::getAllProperties(
        service, objPath, "com.nvidia.PowerSmoothing.AdminPowerProfile",
        [aResp{std::move(aResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& property : properties)
            {
                if (property.first == "RampDownHysteresis")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("RampDownHysteresis nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["RampDownHysteresisSeconds"] = *value;
                }
                else if (property.first == "RampDownRate")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("RampDownRate nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["RampDownWattsPerSecond"] = *value;
                }
                else if (property.first == "RampUpRate")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("RampUpRate nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["RampUpWattsPerSecond"] = *value;
                }
                else if (property.first == "TMPFloorPercent")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("TMPFloorPercent nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["TMPFloorPercent"] = *value;
                }
                else if (property.first == "SecondaryPowerFloorSetting")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("SecondaryPowerFloorSetting nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["SecondaryPowerFloorWatts"] = *value;
                }
                else if (property.first ==
                         "PrimaryFloorActivationWindowMultiplier")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "PrimaryFloorActivationWindowMultiplier nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["PFAWindowMultiplier"] = *value;
                }
                else if (property.first == "PrimaryFloorTargetWindowMultiplier")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "PrimaryFloorTargetWindowMultiplier nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["PFTWindowMultiplier"] = *value;
                }
                else if (property.first == "PrimaryFloorActivationOffset")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "PrimaryFloorActivationOffset nullptr");
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["PFAOffsetWatts"] = *value;
                }
            }
        });
}

inline void getProcessorPowerSmoothingAdminOverrideData(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& processorId)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    std::array<std::string_view, 3> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Accelerator",
        "xyz.openbmc_project.Inventory.Item.Cpu",
        "com.nvidia.PowerSmoothing.PowerSmoothing"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [processorId, aResp{std::move(aResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!path.ends_with(processorId))
                {
                    continue;
                }
                std::string adminOverrideURI =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/Processors/";
                adminOverrideURI += processorId;
                adminOverrideURI +=
                    "/Oem/Nvidia/PowerSmoothing/AdminOverrideProfile";
                aResp->res.jsonValue["@odata.type"] =
                    "#NvidiaPowerSmoothingPresetProfile.v1_1_0.NvidiaPowerSmoothingPresetProfile";
                aResp->res.jsonValue["@odata.id"] = adminOverrideURI;
                aResp->res.jsonValue["Id"] = "AdminOverrideProfile";
                aResp->res.jsonValue["Name"] =
                    processorId + " PowerSmoothing AdminOverrideProfile";

                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper",
                    path + "/admin_override", "xyz.openbmc_project.Association",
                    "endpoints",
                    [aResp, processorId](const boost::system::error_code& ec1,
                                         const std::vector<std::string>& resp) {
                        if (ec1)
                        {
                            return; // no processors = no failures
                        }

                        for (const std::string& profilePath : resp)
                        {
                            sdbusplus::object_path objectPath(profilePath);
                            std::string processorName = objectPath.filename();
                            if (processorName.empty())
                            {
                                messages::internalError(aResp->res);
                                return;
                            }
                            dbus::utility::async_method_call(
                                [processorId, profilePath, aResp{aResp}](
                                    const boost::system::error_code ec2,
                                    const boost::container::flat_map<
                                        std::string,
                                        boost::container::flat_map<
                                            std::string,
                                            std::vector<std::string>>>&
                                        subbTree) {
                                    if (ec2)
                                    {
                                        BMCWEB_LOG_ERROR("DBUS response error");
                                        messages::internalError(aResp->res);

                                        return;
                                    }
                                    for (const auto& [pathInner, objectInner] :
                                         subbTree)
                                    {
                                        BMCWEB_LOG_ERROR("DBUS path {}",
                                                         profilePath);
                                        if (pathInner != profilePath)
                                        {
                                            continue;
                                        }
                                        for (const auto& [service,
                                                          interfaceList] :
                                             objectInner)
                                        {
                                            if (std::ranges::find(
                                                    interfaceList,

                                                    "com.nvidia.PowerSmoothing.AdminPowerProfile") !=
                                                interfaceList.end())
                                            {
                                                getAdminProfileData(
                                                    aResp, service, pathInner);
                                            }
                                        }
                                    }
                                },
                                "xyz.openbmc_project.ObjectMapper",
                                "/xyz/openbmc_project/object_mapper",
                                "xyz.openbmc_project.ObjectMapper",
                                "GetSubTree", "/xyz/openbmc_project/inventory",
                                0,
                                std::array<const char*, 1>{
                                    "com.nvidia.PowerSmoothing.AdminPowerProfile"});
                        }
                    });
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                processorId);
        });
}

inline void getProfileData(std::shared_ptr<bmcweb::AsyncResp> aResp,
                           const std::string& service,
                           const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get processor current profile data.");
    dbus::utility::getAllProperties(
        service, objPath, "com.nvidia.PowerSmoothing.PowerProfile",
        [aResp{std::move(aResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::DBusPropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }

            for (const auto& property : properties)
            {
                if (property.first == "RampDownHysteresis")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["RampDownHysteresisSeconds"] = *value;
                }
                else if (property.first == "RampDownRate")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["RampDownWattsPerSecond"] = *value;
                }
                else if (property.first == "RampUpRate")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["RampUpWattsPerSecond"] = *value;
                }
                else if (property.first == "TMPFloorPercent")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["TMPFloorPercent"] = *value;
                }
                else if (property.first == "SecondaryPowerFloorSetting")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["SecondaryPowerFloorWatts"] = *value;
                }
                else if (property.first ==
                         "PrimaryFloorActivationWindowMultiplier")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["PFAWindowMultiplier"] = *value;
                }
                else if (property.first == "PrimaryFloorTargetWindowMultiplier")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["PFTWindowMultiplier"] = *value;
                }
                else if (property.first == "PrimaryFloorActivationOffset")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }
                    aResp->res.jsonValue["PFAOffsetWatts"] = *value;
                }
            }
        });
}

inline void getProcessorPowerSmoothingPresetProfileData(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& processorId,
    const std::string& profileId)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    std::array<std::string_view, 3> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Accelerator",
        "xyz.openbmc_project.Inventory.Item.Cpu",
        "com.nvidia.PowerSmoothing.PowerSmoothing"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [processorId, profileId, aResp{std::move(aResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);

                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!path.ends_with(processorId))
                {
                    continue;
                }
                std::string profileURI =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/Processors/";
                profileURI += processorId;
                profileURI += "/Oem/Nvidia/PowerSmoothing/PresetProfiles/";
                profileURI += profileId;
                aResp->res.jsonValue["@odata.type"] =
                    "#NvidiaPowerSmoothingPresetProfile.v1_1_0.NvidiaPowerSmoothingPresetProfile";
                aResp->res.jsonValue["@odata.id"] = profileURI;
                aResp->res.jsonValue["Id"] = profileId;

                std::string profileName = processorId;
                profileName += " PowerSmoothing PresetProfile ";
                profileName += profileId;
                aResp->res.jsonValue["Name"] = profileName;

                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper", path + "/power_profile",
                    "xyz.openbmc_project.Association", "endpoints",
                    [aResp, profileId,
                     processorId](const boost::system::error_code& ec1,
                                  const std::vector<std::string>& resp) {
                        if (ec1)
                        {
                            return; // no processors = no failures
                        }
                        bool profileExists = false;

                        for (const std::string& profilePath : resp)
                        {
                            sdbusplus::object_path objectPath(profilePath);
                            std::string profileIdOndbus = objectPath.filename();
                            if (profileIdOndbus != profileId)
                            {
                                continue;
                            }

                            profileExists = true;
                            dbus::utility::getDbusObject(
                                profilePath,
                                std::array<std::string_view, 1>{
                                    "com.nvidia.PowerSmoothing.PowerProfile"},
                                [processorId, profilePath, aResp{aResp}](
                                    const boost::system::error_code&
                                        innerErrorCode,
                                    const dbus::utility::MapperGetObject&
                                        objectData) {
                                    if (innerErrorCode)
                                    {
                                        BMCWEB_LOG_ERROR("DBUS response error");
                                        messages::internalError(aResp->res);
                                        return;
                                    }
                                    std::string service =
                                        objectData.front().first;
                                    getProfileData(aResp, service, profilePath);
                                });
                        }
                        // Object not found
                        if (!profileExists)
                        {
                            messages::resourceNotFound(
                                aResp->res,
                                "#NvidiaPowerSmoothingPresetProfile.v1_1_0.NvidiaPowerSmoothingPresetProfile",
                                profileId);
                        }
                    });
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                processorId);
        });
}

inline void getProcessorPowerSmoothingPresetProfileCollectionData(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& processorId)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    std::array<std::string_view, 3> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Accelerator",
        "xyz.openbmc_project.Inventory.Item.Cpu",
        "com.nvidia.PowerSmoothing.PowerSmoothing"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [processorId, aResp{std::move(aResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);

                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!path.ends_with(processorId))
                {
                    continue;
                }
                std::string profileCollectionURI =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/Processors/";
                profileCollectionURI += processorId;
                profileCollectionURI +=
                    "/Oem/Nvidia/PowerSmoothing/PresetProfiles";
                aResp->res.jsonValue["@odata.type"] =
                    "#NvidiaPowerSmoothingPresetProfileCollection.NvidiaPowerSmoothingPresetProfileCollection";
                aResp->res.jsonValue["@odata.id"] = profileCollectionURI;

                std::string name = processorId;
                name += " PowerSmoothing PresetProfile Collection";
                aResp->res.jsonValue["Name"] = name;
                aResp->res.jsonValue["Members"] = nlohmann::json::array();
                aResp->res.jsonValue["Members@odata.count"] = 0;

                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper", path + "/power_profile",
                    "xyz.openbmc_project.Association", "endpoints",
                    [aResp, profileCollectionURI,
                     processorId](const boost::system::error_code& ec1,
                                  const std::vector<std::string>& resp) {
                        if (ec1)
                        {
                            return; // no processors = no failures
                        }
                        nlohmann::json& addMembers =
                            aResp->res.jsonValue["Members"];
                        for (const std::string& profilePath : resp)
                        {
                            sdbusplus::object_path objectPath(profilePath);
                            std::string profileUri = profileCollectionURI;
                            profileUri += "/";
                            profileUri += objectPath.filename();
                            addMembers.push_back({{"@odata.id", profileUri}});
                        }
                        aResp->res.jsonValue["Members@odata.count"] =
                            addMembers.size();
                    });
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                processorId);
        });
}

inline void patchPowerSmoothingFeature(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& processorId,
    const std::string& propName, const bool& propValue)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    std::array<std::string_view, 3> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Accelerator",
        "xyz.openbmc_project.Inventory.Item.Cpu",
        "com.nvidia.PowerSmoothing.PowerSmoothing"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [processorId, propName, propValue, aResp{std::move(aResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);

                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!path.ends_with(processorId))
                {
                    continue;
                }
                const std::string* inventoryService = nullptr;
                for (const auto& [service, interfaceList] : object)
                {
                    if (std::ranges::find(
                            interfaceList,
                            "com.nvidia.PowerSmoothing.PowerSmoothing") !=
                        interfaceList.end())
                    {
                        inventoryService = &service;
                        break;
                    }
                }
                if (inventoryService == nullptr)
                {
                    // no interface = no failures
                    return;
                }
                dbus::utility::getDbusObject(
                    path,
                    std::array<std::string_view, 1>{
                        nvidia_async_operation_utils::setAsyncInterfaceName},
                    [aResp, propValue, propName, processorId, path,
                     service = *inventoryService](
                        const boost::system::error_code& ec1,
                        const dbus::utility::MapperGetObject& obj) {
                        if (!ec1)
                        {
                            for (const auto& [serv, _] : obj)
                            {
                                if (serv != service)
                                {
                                    continue;
                                }
                                BMCWEB_LOG_DEBUG(
                                    "Performing Patch using Set Async Method Call");
                                nvidia_async_operation_utils::
                                    doGenericSetAsyncAndGatherResult(
                                        aResp, std::chrono::seconds(60),
                                        service, path,
                                        "com.nvidia.PowerSmoothing.PowerSmoothing",
                                        propName, std::variant<bool>(propValue),
                                        nvidia_async_operation_utils::
                                            PatchGenericCallback{aResp});

                                return;
                            }
                        }
                    });
            }
        });
}

/**
 * @brief PATCH StateOfChargeFeatures (MaxACPowerRampRateWattsPerSecond,
 * PowerSmoothingEnabled) on processor PowerSmoothing. Same object path as
 * other processor PowerSmoothing interfaces.
 */
inline void patchProcessorStateOfChargeFeatures(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& processorId,
    const std::optional<uint32_t>& maxAcPowerRampRateWattsPerSecond,
    const std::optional<bool>& powerSmoothingEnabled)
{
    if (!maxAcPowerRampRateWattsPerSecond && !powerSmoothingEnabled)
    {
        return;
    }
    constexpr std::string_view stateOfChargeIface =
        "com.nvidia.PowerSmoothing.StateOfChargeFeatures";
    std::array<std::string_view, 2> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Accelerator", stateOfChargeIface};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [aResp, processorId, maxAcPowerRampRateWattsPerSecond,
         powerSmoothingEnabled, stateOfChargeIface](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!path.ends_with(processorId))
                {
                    continue;
                }
                const std::string* servicePtr = nullptr;
                for (const auto& [service, interfaceList] : object)
                {
                    if (std::ranges::find(interfaceList, stateOfChargeIface) !=
                        interfaceList.end())
                    {
                        servicePtr = &service;
                        break;
                    }
                }
                if (servicePtr == nullptr)
                {
                    BMCWEB_LOG_ERROR(
                        "StateOfChargeFeatures service not found for processor: {}",
                        processorId);
                    return;
                }
                const std::string& service = *servicePtr;
                const std::string iface(stateOfChargeIface);
                if (maxAcPowerRampRateWattsPerSecond)
                {
                    nvidia_async_operation_utils::patch(
                        aResp, service, path, iface,
                        "MaxACPowerRampRateWattsPerSecond",
                        *maxAcPowerRampRateWattsPerSecond);
                }
                if (powerSmoothingEnabled)
                {
                    nvidia_async_operation_utils::patch(
                        aResp, service, path, iface, "PowerSmoothingEnabled",
                        *powerSmoothingEnabled);
                }
                return;
            }
        });
}

inline void patchAdminOverrideProfile(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& processorId,
    const std::string& propName, double propValue)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    std::array<std::string_view, 3> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Accelerator",
        "xyz.openbmc_project.Inventory.Item.Cpu",
        "com.nvidia.PowerSmoothing.PowerSmoothing"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [processorId, propName, propValue, aResp{std::move(aResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!path.ends_with(processorId))
                {
                    continue;
                }

                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper",
                    path + "/admin_override", "xyz.openbmc_project.Association",
                    "endpoints",
                    [aResp, processorId, propName,
                     propValue](const boost::system::error_code& ec1,
                                const std::vector<std::string>& resp) {
                        if (ec1)
                        {
                            return; // no processors = no
                                    // failures
                        }

                        for (const std::string& profilePath : resp)
                        {
                            sdbusplus::object_path objectPath(profilePath);
                            std::string adminProfile = objectPath.filename();
                            if (adminProfile.empty())
                            {
                                messages::internalError(aResp->res);
                                BMCWEB_LOG_ERROR("Empty adminProfile");
                                return;
                            }
                            const std::array<std::string_view, 1>
                                adminProfileIface = {
                                    "com.nvidia.PowerSmoothing.AdminPowerProfile"};
                            dbus::utility::getDbusObject(
                                profilePath, adminProfileIface,
                                [processorId, propName, profilePath, propValue,
                                 aResp{aResp}](
                                    const boost::system::error_code ec2,
                                    const dbus::utility::MapperGetObject& obj) {
                                    if (ec2)
                                    {
                                        BMCWEB_LOG_ERROR("DBUS response error");
                                        messages::internalError(aResp->res);

                                        return;
                                    }
                                    for (const auto& [service, interfaceList] :
                                         obj)
                                    {
                                        BMCWEB_LOG_DEBUG(
                                            "Performing Patch using Set Async Method Call");
                                        nvidia_async_operation_utils::
                                            doGenericSetAsyncAndGatherResult(
                                                aResp, std::chrono::seconds(60),
                                                service, profilePath,
                                                "com.nvidia.PowerSmoothing.AdminPowerProfile",
                                                propName,
                                                std::variant<double>(propValue),
                                                nvidia_async_operation_utils::
                                                    PatchGenericCallback{
                                                        aResp});
                                    }
                                });
                        }
                    });
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                processorId);
        });
}

inline void patchPresetProfile(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& processorId,
    const std::string& profileId, const std::string& propName, double propValue)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    std::array<std::string_view, 3> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Accelerator",
        "xyz.openbmc_project.Inventory.Item.Cpu",
        "com.nvidia.PowerSmoothing.PowerSmoothing"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [processorId, profileId, propName, propValue, aResp{std::move(aResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec || subtree.empty())
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                messages::internalError(aResp->res);
                return;
            }
            for (const auto& [path, object] : subtree)
            {
                BMCWEB_LOG_ERROR("path : {}", path.ends_with(processorId));
                if (!path.ends_with(processorId))
                {
                    continue;
                }
                dbus::utility::getProperty<std::vector<std::string>>(
                    "xyz.openbmc_project.ObjectMapper", path + "/power_profile",
                    "xyz.openbmc_project.Association", "endpoints",
                    [aResp, profileId, propName, propValue,
                     processorId](const boost::system::error_code& ec1,
                                  const std::vector<std::string>& resp) {
                        if (ec1)
                        {
                            return; // no processors = no
                                    // failures
                        }
                        bool profileExists = false;

                        for (const std::string& profilePath : resp)
                        {
                            sdbusplus::object_path objectPath(profilePath);
                            std::string profileIdOndbus = objectPath.filename();
                            if (profileIdOndbus != profileId)
                            {
                                continue;
                            }
                            profileExists = true;
                            const std::array<std::string_view, 1>
                                powerProfileIface = {
                                    "com.nvidia.PowerSmoothing.PowerProfile"};
                            dbus::utility::getDbusObject(
                                profilePath, powerProfileIface,
                                [processorId, profileId, propName, propValue,
                                 profilePath, aResp{aResp}](
                                    const boost::system::error_code&
                                        getObjectError,
                                    const dbus::utility::MapperGetObject&
                                        objInner) {
                                    if (getObjectError)
                                    {
                                        BMCWEB_LOG_ERROR("DBUS response error");
                                        messages::internalError(aResp->res);
                                        return;
                                    }
                                    for (const auto& [service, interfaceList] :
                                         objInner)
                                    {
                                        BMCWEB_LOG_DEBUG(
                                            "Performing Patch using Set Async Method Call");

                                        nvidia_async_operation_utils::
                                            doGenericSetAsyncAndGatherResult(
                                                aResp, std::chrono::seconds(60),
                                                service, profilePath,
                                                "com.nvidia.PowerSmoothing.PowerProfile",
                                                propName,
                                                std::variant<double>(propValue),
                                                nvidia_async_operation_utils::
                                                    PatchGenericCallback{
                                                        aResp});
                                    }
                                });
                        }
                        // Object not found
                        if (!profileExists)
                        {
                            messages::resourceNotFound(
                                aResp->res,
                                "#NvidiaPowerSmoothingPresetProfile.v1_0_0.NvidiaPowerSmoothingPresetProfile",
                                profileId);
                        }
                    });
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                processorId);
        });
}

inline void applyAdminOverride(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connection, const std::string& path)
{
    dbus::utility::getDbusObject(
        path,
        std::array<std::string_view, 1>{
            "com.nvidia.PowerSmoothing.ProfileActionAsync"},
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

                    nvidia_async_operation_utils::
                        doGenericCallAsyncAndGatherResult<int>(
                            asyncResp, std::chrono::seconds(60), connection,
                            path,
                            "com.nvidia.PowerSmoothing.ProfileActionAsync",
                            "ApplyAdminOverride",
                            [asyncResp](const std::string& status,
                                        [[maybe_unused]] const int* retValue) {
                                if (status == nvidia_async_operation_utils::
                                                  asyncStatusValueSuccess)
                                {
                                    BMCWEB_LOG_DEBUG(
                                        "ApplyAdminOverride Succeeded");
                                    messages::success(asyncResp->res);
                                    return;
                                }
                                BMCWEB_LOG_ERROR(
                                    "ApplyAdminOverride Throws error {}",
                                    status);
                                messages::internalError(asyncResp->res);
                            });
                    return;
                }
            }
        });
}

inline void postApplyAdminOverride(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                   const std::string& processorId)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    std::array<std::string_view, 3> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Accelerator",
        "xyz.openbmc_project.Inventory.Item.Cpu",
        "com.nvidia.PowerSmoothing.PowerSmoothing"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [processorId, aResp{std::move(aResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);

                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!path.ends_with(processorId))
                {
                    continue;
                }

                for (const auto& [service, interfaceList] : object)
                {
                    if (std::ranges::find(
                            interfaceList,
                            "com.nvidia.PowerSmoothing.ProfileActionAsync") ==
                        interfaceList.end())
                    {
                        // no interface = no failures
                        continue;
                    }
                    applyAdminOverride(aResp, service, path);
                    return;
                }
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                processorId);
        });
}

inline void activatePresetProfile(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connection, const std::string& path,
    const uint16_t& profileId)
{
    dbus::utility::getDbusObject(
        path,
        std::array<std::string_view, 1>{
            "com.nvidia.PowerSmoothing.ProfileActionAsync"},
        [asyncResp, path, profileId,
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

                    nvidia_async_operation_utils::
                        doGenericCallAsyncAndGatherResult<int>(
                            asyncResp, std::chrono::seconds(60), connection,
                            path,
                            "com.nvidia.PowerSmoothing.ProfileActionAsync",
                            "ActivatePresetProfile",
                            [asyncResp](const std::string& status,
                                        [[maybe_unused]] const int* retValue) {
                                if (status == nvidia_async_operation_utils::
                                                  asyncStatusValueSuccess)
                                {
                                    BMCWEB_LOG_DEBUG(
                                        "ActivatePresetProfile Succeeded");
                                    messages::success(asyncResp->res);
                                    return;
                                }
                                BMCWEB_LOG_ERROR(
                                    "ActivatePresetProfile Throws error {}",
                                    status);
                                messages::internalError(asyncResp->res);
                            },
                            profileId);
                    return;
                }
            }
        });
}

inline void postActivatePresetProfile(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                      const std::string& processorId,
                                      const uint16_t profileId)
{
    BMCWEB_LOG_DEBUG(
        "activatePresetProfile: Get available system processor resource");
    std::array<std::string_view, 3> interfaces = {
        "xyz.openbmc_project.Inventory.Item.Accelerator",
        "xyz.openbmc_project.Inventory.Item.Cpu",
        "com.nvidia.PowerSmoothing.PowerSmoothing"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, interfaces,
        [processorId, profileId, aResp{std::move(aResp)}](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("DBUS response error");
                messages::internalError(aResp->res);

                return;
            }
            for (const auto& [path, object] : subtree)
            {
                if (!path.ends_with(processorId))
                {
                    continue;
                }

                for (const auto& [service, interfaceList] : object)
                {
                    if (std::ranges::find(
                            interfaceList,
                            "com.nvidia.PowerSmoothing.ProfileActionAsync") ==
                        interfaceList.end())
                    {
                        // no interface = no failures
                        continue;
                    }
                    activatePresetProfile(aResp, service, path, profileId);
                    return;
                }
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                processorId);
        });
}

// Chassis SOC PowerSmoothing (StateOfChargeFeatures) - same D-Bus path as
// chassis
static constexpr std::string_view chassisPowerSmoothingStateOfChargeInterface =
    "com.nvidia.PowerSmoothing.StateOfChargeFeatures";

inline void chassisPowerSmoothingOnGetObject(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& chassisId, const std::string& path,
    const std::function<void(const std::string&, const std::string&)>& handler,
    const boost::system::error_code& ecObj,
    const dbus::utility::MapperGetObject& object)
{
    if (ecObj || object.empty())
    {
        messages::resourceNotFound(
            aResp->res, "#NvidiaPowerSmoothing.v1_4_0.NvidiaPowerSmoothing",
            chassisId);
        return;
    }
    const std::string& service = object.begin()->first;
    handler(service, path);
}

inline void chassisPowerSmoothingOnSubTreePaths(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& chassisId,
    const std::function<void(const std::string&, const std::string&)>& handler,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& paths)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS error getting chassis paths: {}", ec);
        messages::internalError(aResp->res);
        return;
    }
    constexpr std::array<std::string_view, 1> ifaces = {
        chassisPowerSmoothingStateOfChargeInterface};
    for (const auto& path : paths)
    {
        if (!path.ends_with(chassisId))
        {
            continue;
        }
        dbus::utility::getDbusObject(
            path, ifaces,
            std::function<void(const boost::system::error_code&,
                               const dbus::utility::MapperGetObject&)>(
                std::bind_front(chassisPowerSmoothingOnGetObject, aResp,
                                chassisId, path, handler)));
        return;
    }
    messages::resourceNotFound(
        aResp->res, "#NvidiaPowerSmoothing.v1_4_0.NvidiaPowerSmoothing",
        chassisId);
}

/**
 * @brief Resolve chassis to (service, path) that implements
 * StateOfChargeFeatures. On success invokes handler(service, path); on failure
 * returns 404.
 */
template <typename Handler>
inline void getChassisPowerSmoothingService(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& chassisId, Handler&& handler)
{
    std::function<void(const std::string&, const std::string&)> invokeHandler =
        [h = std::forward<Handler>(handler)](
            const std::string& service, const std::string& objPath) mutable {
            h(service, objPath);
        };
    constexpr std::array<std::string_view, 1> chassisItemChassisOnly = {
        "xyz.openbmc_project.Inventory.Item.Chassis"};
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/inventory", 0, chassisItemChassisOnly,
        std::function<void(
            const boost::system::error_code&,
            const dbus::utility::MapperGetSubTreePathsResponse&)>(
            std::bind_front(chassisPowerSmoothingOnSubTreePaths, aResp,
                            chassisId, std::move(invokeHandler))));
}

/**
 * @brief GET chassis PowerSmoothing (SOC StateOfChargeFeatures) data.
 * Fills JSON with #NvidiaPowerSmoothing.v1_4_0 schema.
 */
inline void getChassisPowerSmoothingData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& chassisId)
{
    getChassisPowerSmoothingService(
        aResp, chassisId,
        [aResp,
         chassisId](const std::string& service, const std::string& objPath) {
            dbus::utility::getAllProperties(
                service, objPath,
                std::string(chassisPowerSmoothingStateOfChargeInterface),
                [aResp, chassisId](
                    const boost::system::error_code& ec,
                    const dbus::utility::DBusPropertiesMap& properties) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS error getting StateOfChargeFeatures: {}", ec);
                        messages::internalError(aResp->res);
                        return;
                    }
                    nlohmann::json& json = aResp->res.jsonValue;
                    const std::string baseUri =
                        "/redfish/v1/Chassis/" + chassisId;
                    json["@odata.id"] = baseUri + "/Oem/Nvidia/PowerSmoothing";
                    json["@odata.type"] =
                        "#NvidiaPowerSmoothing.v1_4_0.NvidiaPowerSmoothing";
                    json["Id"] = "PowerSmoothing";
                    json["Name"] = chassisId + " Oem Nvidia PowerSmoothing";

                    nlohmann::json& soc = json["StateOfChargeFeatures"];
                    for (const auto& [name, value] : properties)
                    {
                        if (name == "MaxACPowerRampRateWattsPerSecond")
                        {
                            const uint32_t* v = std::get_if<uint32_t>(&value);
                            if (v != nullptr)
                            {
                                soc["MaxACPowerRampRateWattsPerSecond"] = *v;
                            }
                        }
                        else if (name == "PowerSmoothingEnabled")
                        {
                            const bool* v = std::get_if<bool>(&value);
                            if (v != nullptr)
                            {
                                soc["PowerSmoothingEnabled"] = *v;
                            }
                        }
                        else if (name == "ProfileName")
                        {
                            const std::string* v =
                                std::get_if<std::string>(&value);
                            if (v != nullptr)
                            {
                                soc["ProfileName"] = *v;
                            }
                        }
                        else if (name == "AvailableProfileNames")
                        {
                            const std::vector<std::string>* v =
                                std::get_if<std::vector<std::string>>(&value);
                            if (v != nullptr)
                            {
                                soc["AvailableProfileNames"] = *v;
                            }
                        }
                        else if (name == "PowerBrakeEnabled")
                        {
                            const bool* v = std::get_if<bool>(&value);
                            if (v != nullptr)
                            {
                                soc["PowerBrakeEnabled"] = *v;
                            }
                        }
                    }
                });
        });
}

/**
 * @brief Apply StateOfChargeFeatures property patches to the given D-Bus
 * service/path. Called after chassis resolution.
 */
inline void applyChassisPowerSmoothingPatches(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objPath,
    const std::string& iface,
    const std::optional<uint32_t>& maxAcPowerRampRateWattsPerSecond,
    const std::optional<bool>& powerSmoothingEnabled,
    const std::optional<std::string>& profileName,
    const std::optional<bool>& powerBrakeEnabled)
{
    if (maxAcPowerRampRateWattsPerSecond)
    {
        nvidia_async_operation_utils::patch(asyncResp, service, objPath, iface,
                                            "MaxACPowerRampRateWattsPerSecond",
                                            *maxAcPowerRampRateWattsPerSecond);
    }
    if (powerSmoothingEnabled)
    {
        nvidia_async_operation_utils::patch(asyncResp, service, objPath, iface,
                                            "PowerSmoothingEnabled",
                                            *powerSmoothingEnabled);
    }
    if (profileName)
    {
        nvidia_async_operation_utils::patch(asyncResp, service, objPath, iface,
                                            "ProfileName", *profileName);
    }
    if (powerBrakeEnabled)
    {
        nvidia_async_operation_utils::patch(asyncResp, service, objPath, iface,
                                            "PowerBrakeEnabled",
                                            *powerBrakeEnabled);
    }
}

/**
 * @brief PATCH chassis PowerSmoothing (SOC StateOfChargeFeatures).
 * Parses StateOfChargeFeatures in body and calls D-Bus patch per property.
 */
inline void patchChassisPowerSmoothingData(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        BMCWEB_LOG_ERROR(
            "Failed to set up route for chassis PowerSmoothing PATCH");
        return;
    }
    nlohmann::json reqJson;
    if (!redfish::json_util::processJsonFromRequest(asyncResp->res, req,
                                                    reqJson))
    {
        BMCWEB_LOG_ERROR(
            "Failed to parse request body for chassis PowerSmoothing PATCH");
        return;
    }
    std::optional<nlohmann::json> stateOfChargeFeatures;
    if (!redfish::json_util::readJson(reqJson, asyncResp->res,
                                      "StateOfChargeFeatures",
                                      stateOfChargeFeatures))
    {
        BMCWEB_LOG_ERROR(
            "Failed to read StateOfChargeFeatures from chassis PowerSmoothing PATCH");
        return;
    }
    if (!stateOfChargeFeatures || !stateOfChargeFeatures->is_object())
    {
        BMCWEB_LOG_ERROR(
            "StateOfChargeFeatures is missing or not an object in chassis PowerSmoothing PATCH");
        return;
    }
    std::optional<uint32_t> maxAcPowerRampRateWattsPerSecond;
    std::optional<bool> powerSmoothingEnabled;
    std::optional<std::string> profileName;
    std::optional<bool> powerBrakeEnabled;
    nlohmann::json& socJson = *stateOfChargeFeatures;
    if (!redfish::json_util::readJson(
            socJson, asyncResp->res, "MaxACPowerRampRateWattsPerSecond",
            maxAcPowerRampRateWattsPerSecond, "PowerSmoothingEnabled",
            powerSmoothingEnabled, "ProfileName", profileName,
            "PowerBrakeEnabled", powerBrakeEnabled))
    {
        BMCWEB_LOG_ERROR(
            "Invalid StateOfChargeFeatures fields in chassis PowerSmoothing PATCH");
        return;
    }
    if (!maxAcPowerRampRateWattsPerSecond && !powerSmoothingEnabled &&
        !profileName && !powerBrakeEnabled)
    {
        BMCWEB_LOG_ERROR(
            "No StateOfChargeFeatures properties provided in chassis PowerSmoothing PATCH");
        messages::noOperation(asyncResp->res);
        return;
    }
    const std::string iface(chassisPowerSmoothingStateOfChargeInterface);
    getChassisPowerSmoothingService(
        asyncResp, chassisId,
        [asyncResp, iface, maxAcPowerRampRateWattsPerSecond,
         powerSmoothingEnabled, profileName, powerBrakeEnabled](
            const std::string& service, const std::string& objPath) {
            applyChassisPowerSmoothingPatches(
                asyncResp, service, objPath, iface,
                maxAcPowerRampRateWattsPerSecond, powerSmoothingEnabled,
                profileName, powerBrakeEnabled);
        });
}

inline void requestRoutesChassisPowerSmoothing(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/PowerSmoothing/")
        .privileges(redfish::privileges::getChassisCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& chassisId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                getChassisPowerSmoothingData(asyncResp, chassisId);
            });

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/PowerSmoothing/")
        .privileges(redfish::privileges::patchChassisCollection)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(patchChassisPowerSmoothingData, std::ref(app)));
}

inline void requestRoutesProcessorPowerSmoothing(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Processors/<str>/Oem/Nvidia/PowerSmoothing/")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                getProcessorPowerSmoothingData(asyncResp, processorId);
            });

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Processors/<str>/Oem/Nvidia/PowerSmoothing/")
        .privileges(redfish::privileges::patchProcessor)
        .methods(
            boost::beast::http::verb::
                patch)([&app](
                           const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           [[maybe_unused]] const std::string& systemName,
                           const std::string& processorId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            std::optional<bool> pwrSmoothingFeature;
            std::optional<bool> immediateRampDownFeature;
            std::optional<nlohmann::json> stateOfChargeFeatures;
            if (!redfish::json_util::readJsonAction(
                    req, asyncResp->res, "Enabled", pwrSmoothingFeature,
                    "ImmediateRampDown", immediateRampDownFeature,
                    "StateOfChargeFeatures", stateOfChargeFeatures))
            {
                return;
            }
            if (pwrSmoothingFeature)
            {
                patchPowerSmoothingFeature(asyncResp, processorId,
                                           "PowerSmoothingEnabled",
                                           *pwrSmoothingFeature);
            }
            if (immediateRampDownFeature)
            {
                patchPowerSmoothingFeature(asyncResp, processorId,
                                           "ImmediateRampDownEnabled",
                                           *immediateRampDownFeature);
            }
            if (stateOfChargeFeatures && stateOfChargeFeatures->is_object())
            {
                std::optional<uint32_t> maxAcPowerRampRateWattsPerSecond;
                std::optional<bool> socPowerSmoothingEnabled;
                nlohmann::json& socJson = *stateOfChargeFeatures;
                if (!redfish::json_util::readJson(
                        socJson, asyncResp->res,
                        "MaxACPowerRampRateWattsPerSecond",
                        maxAcPowerRampRateWattsPerSecond,
                        "PowerSmoothingEnabled", socPowerSmoothingEnabled))
                {
                    BMCWEB_LOG_ERROR(
                        "Failed to parse StateOfChargeFeatures in processor PowerSmoothing PATCH");
                    return;
                }
                patchProcessorStateOfChargeFeatures(
                    asyncResp, processorId, maxAcPowerRampRateWattsPerSecond,
                    socPowerSmoothingEnabled);
            }
        });

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Processors/<str>/Oem/Nvidia/PowerSmoothing/Actions/NvidiaPowerSmoothing.ApplyAdminOverrides/")
        .privileges(redfish::privileges::postProcessor)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                postApplyAdminOverride(asyncResp, processorId);
            });

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Processors/<str>/Oem/Nvidia/PowerSmoothing/Actions/NvidiaPowerSmoothing.ActivatePresetProfile")
        .privileges(redfish::privileges::postProcessor)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::optional<uint16_t> profileId;

                if (!redfish::json_util::readJsonAction(req, asyncResp->res,
                                                        "ProfileId", profileId))
                {
                    return;
                }
                if (profileId)
                {
                    postActivatePresetProfile(asyncResp, processorId,
                                              *profileId);
                }
            });

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Processors/<str>/Oem/Nvidia/PowerSmoothing/ActivatePresetProfileActionInfo")
        .privileges(redfish::privileges::getProcessor)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            [[maybe_unused]] const std::string& systemName,
                            const std::string& processorId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            std::string actionInfoURI =
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/Processors/";
            actionInfoURI += processorId;
            actionInfoURI +=
                "/Oem/Nvidia/PowerSmoothing/ActivatePresetProfileActionInfo";
            asyncResp->res.jsonValue["@odata.id"] = actionInfoURI;
            asyncResp->res.jsonValue["@odata.type"] =
                "#ActionInfo.v1_2_0.ActionInfo";
            asyncResp->res.jsonValue["Id"] = "ActivatePresetProfileActionInfo";
            asyncResp->res.jsonValue["Name"] =
                "ActivatePresetProfile Action Info";
            nvidia_power_smoothing_utils::
                getPowerSmoothingPresetProfileParameters(processorId,
                                                         asyncResp);
        });
}

inline void requestRoutesProcessorPowerSmoothingAdminProfile(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Processors/<str>/Oem/Nvidia/PowerSmoothing/AdminOverrideProfile/")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                getProcessorPowerSmoothingAdminOverrideData(asyncResp,
                                                            processorId);
            });

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Processors/<str>/Oem/Nvidia/PowerSmoothing/AdminOverrideProfile/")
        .privileges(redfish::privileges::patchProcessor)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::optional<double> tmpFloorPercent;
                std::optional<double> rampUpWattsPerSecond;
                std::optional<double> rampDownWattsPerSecond;
                std::optional<double> rampDownHysteresisSeconds;
                std::optional<double> secondaryPowerFloorSetting;
                std::optional<double> primaryFloorActivationWindowMultiplier;
                std::optional<double> primaryFloorTargetWindowMultiplier;
                std::optional<double> primaryFloorActivationOffset;
                if (!redfish::json_util::readJsonAction(
                        req, asyncResp->res, "TMPFloorPercent", tmpFloorPercent,
                        "RampUpWattsPerSecond", rampUpWattsPerSecond,
                        "RampDownWattsPerSecond", rampDownWattsPerSecond,
                        "RampDownHysteresisSeconds", rampDownHysteresisSeconds,
                        "SecondaryPowerFloorWatts", secondaryPowerFloorSetting,
                        "PFAWindowMultiplier",
                        primaryFloorActivationWindowMultiplier,
                        "PFTWindowMultiplier",
                        primaryFloorTargetWindowMultiplier, "PFAOffsetWatts",
                        primaryFloorActivationOffset))
                {
                    return;
                }
                if (tmpFloorPercent)
                {
                    patchAdminOverrideProfile(asyncResp, processorId,
                                              "TMPFloorPercent",
                                              *tmpFloorPercent);
                }
                if (rampUpWattsPerSecond)
                {
                    patchAdminOverrideProfile(asyncResp, processorId,
                                              "RampUpRate",
                                              *rampUpWattsPerSecond);
                }
                if (rampDownWattsPerSecond)
                {
                    patchAdminOverrideProfile(asyncResp, processorId,
                                              "RampDownRate",
                                              *rampDownWattsPerSecond);
                }
                if (rampDownHysteresisSeconds)
                {
                    patchAdminOverrideProfile(asyncResp, processorId,
                                              "RampDownHysteresis",
                                              *rampDownHysteresisSeconds);
                }
                if (secondaryPowerFloorSetting)
                {
                    patchAdminOverrideProfile(asyncResp, processorId,
                                              "SecondaryPowerFloorSetting",
                                              *secondaryPowerFloorSetting);
                }
                if (primaryFloorActivationWindowMultiplier)
                {
                    patchAdminOverrideProfile(
                        asyncResp, processorId,
                        "PrimaryFloorActivationWindowMultiplier",
                        *primaryFloorActivationWindowMultiplier);
                }
                if (primaryFloorTargetWindowMultiplier)
                {
                    patchAdminOverrideProfile(
                        asyncResp, processorId,
                        "PrimaryFloorTargetWindowMultiplier",
                        *primaryFloorTargetWindowMultiplier);
                }
                if (primaryFloorActivationOffset)
                {
                    patchAdminOverrideProfile(asyncResp, processorId,
                                              "PrimaryFloorActivationOffset",
                                              *primaryFloorActivationOffset);
                }
            });
}

inline void requestRoutesProcessorPowerSmoothingPresetProfileCollection(
    App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Processors/<str>/Oem/Nvidia/PowerSmoothing/PresetProfiles/")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                getProcessorPowerSmoothingPresetProfileCollectionData(
                    asyncResp, processorId);
            });
}

inline void requestRoutesProcessorPowerSmoothingPresetProfile(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Processors/<str>/Oem/Nvidia/PowerSmoothing/PresetProfiles/<str>/")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId,
                   const std::string& profileId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                getProcessorPowerSmoothingPresetProfileData(
                    asyncResp, processorId, profileId);
            });

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Processors/<str>/Oem/Nvidia/PowerSmoothing/PresetProfiles/<str>/")
        .privileges(redfish::privileges::patchProcessor)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId,
                   const std::string& profileId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::optional<double> tmpFloorPercent;
                std::optional<double> rampUpWattsPerSecond;
                std::optional<double> rampDownWattsPerSecond;
                std::optional<double> rampDownHysteresisSeconds;
                std::optional<double> secondaryPowerFloorSetting;
                std::optional<double> primaryFloorActivationWindowMultiplier;
                std::optional<double> primaryFloorTargetWindowMultiplier;
                std::optional<double> primaryFloorActivationOffset;
                if (!redfish::json_util::readJsonAction(
                        req, asyncResp->res, "TMPFloorPercent", tmpFloorPercent,
                        "RampUpWattsPerSecond", rampUpWattsPerSecond,
                        "RampDownWattsPerSecond", rampDownWattsPerSecond,
                        "RampDownHysteresisSeconds", rampDownHysteresisSeconds,
                        "SecondaryPowerFloorWatts", secondaryPowerFloorSetting,
                        "PFAWindowMultiplier",
                        primaryFloorActivationWindowMultiplier,
                        "PFTWindowMultiplier",
                        primaryFloorTargetWindowMultiplier, "PFAOffsetWatts",
                        primaryFloorActivationOffset))
                {
                    return;
                }
                if (tmpFloorPercent)
                {
                    patchPresetProfile(asyncResp, processorId, profileId,
                                       "TMPFloorPercent", *tmpFloorPercent);
                }
                if (rampUpWattsPerSecond)
                {
                    patchPresetProfile(asyncResp, processorId, profileId,
                                       "RampUpRate", *rampUpWattsPerSecond);
                }
                if (rampDownWattsPerSecond)
                {
                    patchPresetProfile(asyncResp, processorId, profileId,
                                       "RampDownRate", *rampDownWattsPerSecond);
                }
                if (rampDownHysteresisSeconds)
                {
                    patchPresetProfile(asyncResp, processorId, profileId,
                                       "RampDownHysteresis",
                                       *rampDownHysteresisSeconds);
                }
                if (secondaryPowerFloorSetting)
                {
                    patchPresetProfile(asyncResp, processorId, profileId,
                                       "SecondaryPowerFloorSetting",
                                       *secondaryPowerFloorSetting);
                }
                if (primaryFloorActivationWindowMultiplier)
                {
                    patchPresetProfile(asyncResp, processorId, profileId,
                                       "PrimaryFloorActivationWindowMultiplier",
                                       *primaryFloorActivationWindowMultiplier);
                }
                if (primaryFloorTargetWindowMultiplier)
                {
                    patchPresetProfile(asyncResp, processorId, profileId,
                                       "PrimaryFloorTargetWindowMultiplier",
                                       *primaryFloorTargetWindowMultiplier);
                }
                if (primaryFloorActivationOffset)
                {
                    patchPresetProfile(asyncResp, processorId, profileId,
                                       "PrimaryFloorActivationOffset",
                                       *primaryFloorActivationOffset);
                }
            });
}

} // namespace redfish
