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

#include <boost/container/flat_map.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/conditions_utils.hpp>

#include <array>
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
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}, objPath,
         presetProfileURI](const boost::system::error_code ec,
                           const DbusProperties& properties) {
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
                else if (property.first == "AppliedProfilePath")
                {
                    const sdbusplus::message::object_path* value =
                        std::get_if<sdbusplus::message::object_path>(
                            &property.second);
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
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.PowerSmoothing.CurrentPowerProfile");
}

inline void getProcessorPowerSmoothingControlData(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& service,
    const std::string& objPath, const std::string& presetProfileURI)
{
    BMCWEB_LOG_DEBUG("Get processor smoothing control data.");
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}, objPath, service,
         presetProfileURI](const boost::system::error_code ec,
                           const DbusProperties& properties) {
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
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.PowerSmoothing.PowerSmoothing");
}

inline void getProcessorPowerSmoothingData(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& processorId)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    crow::connections::systemBus->async_method_call(
        [processorId, aResp{std::move(aResp)}](
            const boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, boost::container::flat_map<
                                 std::string, std::vector<std::string>>>&
                subtree) {
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
                    "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing";
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

                for (const auto& [service, interfaces] : object)
                {
                    if (std::find(interfaces.begin(), interfaces.end(),
                                  "com.nvidia.PowerSmoothing.PowerSmoothing") ==
                        interfaces.end())
                    {
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
                    getProcessorPowerSmoothingControlData(aResp, service, path,
                                                          presetProfileURI);
                    if (std::find(
                            interfaces.begin(), interfaces.end(),
                            "com.nvidia.PowerSmoothing.CurrentPowerProfile") ==
                        interfaces.end())
                    {
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
                    getProcessorCurrentProfileData(aResp, service, path,
                                                   presetProfileURI);
                }
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                processorId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 2>{
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "xyz.openbmc_project.Inventory.Item.Cpu"});
}

inline void getAdminProfileData(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                const std::string& service,
                                const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get processor current profile data.");
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}](const boost::system::error_code ec,
                                  const DbusProperties& properties) {
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
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.PowerSmoothing.AdminPowerProfile");
}

inline void getProcessorPowerSmoothingAdminOverrideData(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& processorId)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    crow::connections::systemBus->async_method_call(
        [processorId, aResp{std::move(aResp)}](
            const boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, boost::container::flat_map<
                                 std::string, std::vector<std::string>>>&
                subtree) {
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
                    "#NvidiaPowerSmoothingPresetProfile.v1_0_0.NvidiaPowerSmoothingPresetProfile";
                aResp->res.jsonValue["@odata.id"] = adminOverrideURI;
                aResp->res.jsonValue["Id"] = "AdminOverrideProfile";
                aResp->res.jsonValue["Name"] =
                    processorId + " PowerSmoothing AdminOverrideProfile";

                crow::connections::systemBus->async_method_call(
                    [aResp, processorId](
                        const boost::system::error_code ec2,
                        std::variant<std::vector<std::string>>& resp) {
                        if (ec2)
                        {
                            return; // no processors = no failures
                        }
                        std::vector<std::string>* data =
                            std::get_if<std::vector<std::string>>(&resp);
                        if (data == nullptr)
                        {
                            return;
                        }

                        for (const std::string& profilePath : *data)
                        {
                            sdbusplus::message::object_path objectPath(
                                profilePath);
                            std::string processorName = objectPath.filename();
                            if (processorName.empty())
                            {
                                messages::internalError(aResp->res);
                                return;
                            }
                            crow::connections::systemBus->async_method_call(
                                [processorId, profilePath,
                                 aResp{std::move(aResp)}](
                                    const boost::system::error_code ec,
                                    const boost::container::flat_map<
                                        std::string,
                                        boost::container::flat_map<
                                            std::string,
                                            std::vector<std::string>>>&
                                        subtree) {
                                    if (ec)
                                    {
                                        BMCWEB_LOG_ERROR("DBUS response error");
                                        messages::internalError(aResp->res);

                                        return;
                                    }
                                    for (const auto& [path, object] : subtree)
                                    {
                                        BMCWEB_LOG_ERROR("DBUS path {}",
                                                         profilePath);
                                        if (path != profilePath)
                                        {
                                            continue;
                                        }
                                        for (const auto& [service, interfaces] :
                                             object)
                                        {
                                            if (std::find(
                                                    interfaces.begin(),
                                                    interfaces.end(),
                                                    "com.nvidia.PowerSmoothing.AdminPowerProfile") !=
                                                interfaces.end())
                                            {
                                                getAdminProfileData(
                                                    aResp, service, path);
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
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    path + "/admin_override", "org.freedesktop.DBus.Properties",
                    "Get", "xyz.openbmc_project.Association", "endpoints");
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                processorId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 2>{
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "xyz.openbmc_project.Inventory.Item.Cpu"});
}

inline void getProfileData(std::shared_ptr<bmcweb::AsyncResp> aResp,
                           const std::string& service,
                           const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get processor current profile data.");
    crow::connections::systemBus->async_method_call(
        [aResp{std::move(aResp)}](const boost::system::error_code ec,
                                  const DbusProperties& properties) {
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
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.PowerSmoothing.PowerProfile");
}

inline void getProcessorPowerSmoothingPresetProfileData(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& processorId,
    const std::string& profileId)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    crow::connections::systemBus->async_method_call(
        [processorId, profileId, aResp{std::move(aResp)}](
            const boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, boost::container::flat_map<
                                 std::string, std::vector<std::string>>>&
                subtree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
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
                    "#NvidiaPowerSmoothingPresetProfile.v1_0_0.NvidiaPowerSmoothingPresetProfile";
                aResp->res.jsonValue["@odata.id"] = profileURI;
                aResp->res.jsonValue["Id"] = "Presetprofile";

                std::string profileName = processorId;
                profileName += " PowerSmoothing PresetProfile ";
                profileName += profileId;
                aResp->res.jsonValue["Name"] = profileName;

                crow::connections::systemBus->async_method_call(
                    [aResp, profileId, processorId](
                        const boost::system::error_code ec2,
                        std::variant<std::vector<std::string>>& resp) {
                        if (ec2)
                        {
                            return; // no processors = no failures
                        }
                        std::vector<std::string>* data =
                            std::get_if<std::vector<std::string>>(&resp);
                        if (data == nullptr)
                        {
                            return;
                        }
                        bool profileExists = false;

                        for (const std::string& profilePath : *data)
                        {
                            sdbusplus::message::object_path objectPath(
                                profilePath);
                            std::string profileIdOndbus = objectPath.filename();
                            if (profileIdOndbus != profileId)
                            {
                                continue;
                            }
                            profileExists = true;
                            std::string objectPathToGetProfileData =
                                profilePath;
                            BMCWEB_LOG_ERROR("Profile ID: {}", profileId);
                            crow::connections::systemBus->async_method_call(
                                [processorId, objectPathToGetProfileData,
                                 aResp{std::move(aResp)}](
                                    const boost::system::error_code ec,
                                    const std::vector<std::pair<
                                        std::string, std::vector<std::string>>>&
                                        object) {
                                    if (ec)
                                    {
                                        BMCWEB_LOG_ERROR("DBUS response error");
                                        messages::internalError(aResp->res);
                                        return;
                                    }
                                    std::string service = object.front().first;
                                    getProfileData(aResp, service,
                                                   objectPathToGetProfileData);
                                },
                                "xyz.openbmc_project.ObjectMapper",
                                "/xyz/openbmc_project/object_mapper",
                                "xyz.openbmc_project.ObjectMapper", "GetObject",
                                objectPathToGetProfileData,
                                std::array<const char*, 1>{
                                    "com.nvidia.PowerSmoothing.PowerProfile"});
                        }
                        // Object not found
                        if (!profileExists)
                        {
                            messages::resourceNotFound(
                                aResp->res,
                                "#NvidiaPowerSmoothingPresetProfile.v1_0_0.NvidiaPowerSmoothingPresetProfile",
                                profileId);
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper", path + "/power_profile",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                processorId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 2>{
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "xyz.openbmc_project.Inventory.Item.Cpu"});
}

inline void getProcessorPowerSmoothingPresetProfileCollectionData(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& processorId)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    crow::connections::systemBus->async_method_call(
        [processorId, aResp{std::move(aResp)}](
            const boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, boost::container::flat_map<
                                 std::string, std::vector<std::string>>>&
                subtree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
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

                crow::connections::systemBus->async_method_call(
                    [aResp, profileCollectionURI, processorId](
                        const boost::system::error_code ec2,
                        std::variant<std::vector<std::string>>& resp) {
                        if (ec2)
                        {
                            return; // no processors = no failures
                        }
                        std::vector<std::string>* data =
                            std::get_if<std::vector<std::string>>(&resp);
                        if (data == nullptr)
                        {
                            return;
                        }
                        nlohmann::json& addMembers =
                            aResp->res.jsonValue["Members"];
                        for (const std::string& profilePath : *data)
                        {
                            sdbusplus::message::object_path objectPath(
                                profilePath);
                            std::string profileUri = profileCollectionURI;
                            profileUri += "/";
                            profileUri += objectPath.filename();
                            addMembers.push_back({{"@odata.id", profileUri}});
                        }
                        aResp->res.jsonValue["Members@odata.count"] =
                            addMembers.size();
                    },
                    "xyz.openbmc_project.ObjectMapper", path + "/power_profile",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                processorId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 2>{
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "xyz.openbmc_project.Inventory.Item.Cpu"});
}

inline void patchPowerSmoothingFeature(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& processorId,
    std::string propName, const bool& propValue)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    crow::connections::systemBus->async_method_call(
        [processorId, propName, propValue, aResp{std::move(aResp)}](
            const boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, boost::container::flat_map<
                                 std::string, std::vector<std::string>>>&
                subtree) {
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

                for (const auto& [service, interfaces] : object)
                {
                    if (std::find(interfaces.begin(), interfaces.end(),
                                  "com.nvidia.PowerSmoothing.PowerSmoothing") !=
                        interfaces.end())
                    {
                        // Set the property, with handler to check error
                        // responses
                        crow::connections::systemBus->async_method_call(
                            [aResp, propName,
                             processorId](boost::system::error_code ec,
                                          sdbusplus::message::message& msg) {
                                if (!ec)
                                {
                                    BMCWEB_LOG_DEBUG(
                                        "Toggle Power Smoothing feature state succeeded");
                                    return;
                                }

                                BMCWEB_LOG_DEBUG(
                                    "Toggle Immediate Rampdown failed: {}, {}",
                                    processorId, propName, ec);
                                // Read and convert dbus error message to
                                // redfish error
                                const sd_bus_error* dbusError = msg.get_error();
                                if (dbusError == nullptr)
                                {
                                    messages::internalError(aResp->res);
                                    return;
                                }

                                if (strcmp(dbusError->name,
                                           "xyz.openbmc_project.Common."
                                           "Device.Error.WriteFailure") == 0)
                                {
                                    // Service failed to change the config
                                    messages::operationFailed(aResp->res);
                                }
                                else
                                {
                                    messages::internalError(aResp->res);
                                }
                            },
                            service, path, "org.freedesktop.DBus.Properties",
                            "Set", "com.nvidia.PowerSmoothing.PowerSmoothing",
                            propName, std::variant<bool>(propValue));
                    }
                }
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                processorId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 2>{
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "xyz.openbmc_project.Inventory.Item.Cpu"});
}

inline void patchAdminOverrideProfile(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                      const std::string& processorId,
                                      std::string propName, double propValue)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    crow::connections::systemBus
        ->async_method_call(
            [processorId, propName, propValue, aResp{std::move(aResp)}](
                const boost::system::error_code ec,
                const boost::container::flat_map<
                    std::string, boost::container::flat_map<
                                     std::string, std::vector<std::string>>>&
                    subtree) {
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

                    crow::
                        connections::
                            systemBus
                                ->async_method_call(
                                    [aResp, processorId, propName, propValue](
                                        const boost::system::error_code ec2,
                                        std::variant<std::vector<std::string>>&
                                            resp) {
                                        if (ec2)
                                        {
                                            return; // no processors = no
                                                    // failures
                                        }
                                        std::vector<std::string>* data =
                                            std::get_if<
                                                std::vector<std::string>>(
                                                &resp);
                                        if (data == nullptr)
                                        {
                                            return;
                                        }

                                        for (const std::string& profilePath :
                                             *data)
                                        {
                                            sdbusplus::message::object_path
                                                objectPath(profilePath);
                                            std::string processorName =
                                                objectPath.filename();
                                            if (processorName.empty())
                                            {
                                                messages::internalError(
                                                    aResp->res);
                                                return;
                                            }
                                            crow::
                                                connections::
                                                    systemBus
                                                        ->async_method_call(
                                                            [processorId,
                                                             propName,
                                                             profilePath,
                                                             propValue,
                                                             aResp{std::move(
                                                                 aResp)}](
                                                                const boost::
                                                                    system::
                                                                        error_code
                                                                            ec,
                                                                const boost::
                                                                    container::
                                                                        flat_map<
                                                                            std::
                                                                                string,
                                                                            boost::container::flat_map<std::string, std::
                                                                                                                        vector<std::string>>>& subtree) {
                                                                if (ec)
                                                                {
                                                                    BMCWEB_LOG_ERROR(
                                                                        "DBUS response error");
                                                                    messages::internalError(
                                                                        aResp
                                                                            ->res);

                                                                    return;
                                                                }
                                                                for (
                                                                    const auto& [path,
                                                                                 object] :
                                                                    subtree)
                                                                {
                                                                    BMCWEB_LOG_ERROR(
                                                                        "DBUS path {}",
                                                                        profilePath);
                                                                    if (path !=
                                                                        profilePath)
                                                                    {
                                                                        continue;
                                                                    }
                                                                    for (
                                                                        const auto& [service,
                                                                                     interfaces] :
                                                                        object)
                                                                    {
                                                                        if (std::find(
                                                                                interfaces
                                                                                    .begin(),
                                                                                interfaces
                                                                                    .end(),
                                                                                "com.nvidia.PowerSmoothing.AdminPowerProfile") !=
                                                                            interfaces
                                                                                .end())
                                                                        {
                                                                            // Set the property, with handler to check
                                                                            // error responses
                                                                            crow::connections::systemBus
                                                                                ->async_method_call(
                                                                                    [aResp,
                                                                                     processorId](
                                                                                        boost::system::
                                                                                            error_code
                                                                                                ec,
                                                                                        sdbusplus::message::
                                                                                            message&
                                                                                                msg) {
                                                                                        if (!ec)
                                                                                        {
                                                                                            BMCWEB_LOG_DEBUG(
                                                                                                "Admin Power profile update succeeded");
                                                                                            return;
                                                                                        }

                                                                                        BMCWEB_LOG_DEBUG(
                                                                                            "Admin Power profile update failed: {}",
                                                                                            processorId,
                                                                                            ec);
                                                                                        // Read and convert dbus error message
                                                                                        // to redfish error
                                                                                        const sd_bus_error*
                                                                                            dbusError =
                                                                                                msg.get_error();
                                                                                        if (dbusError ==
                                                                                            nullptr)
                                                                                        {
                                                                                            messages::internalError(
                                                                                                aResp
                                                                                                    ->res);
                                                                                            return;
                                                                                        }

                                                                                        if (strcmp(
                                                                                                dbusError
                                                                                                    ->name,
                                                                                                "xyz.openbmc_project.Common."
                                                                                                "Device.Error.WriteFailure") ==
                                                                                            0)
                                                                                        {
                                                                                            // Service failed to change the
                                                                                            // config
                                                                                            messages::operationFailed(
                                                                                                aResp
                                                                                                    ->res);
                                                                                        }
                                                                                        else
                                                                                        {
                                                                                            messages::internalError(
                                                                                                aResp
                                                                                                    ->res);
                                                                                        }
                                                                                    },
                                                                                    service,
                                                                                    path,
                                                                                    "org.freedesktop.DBus.Properties",
                                                                                    "Set",
                                                                                    "com.nvidia.PowerSmoothing.AdminPowerProfile",
                                                                                    propName,
                                                                                    std::variant<
                                                                                        double>(
                                                                                        propValue));
                                                                        }
                                                                    }
                                                                }
                                                            },
                                                            "xyz.openbmc_project.ObjectMapper",
                                                            "/xyz/openbmc_project/object_mapper",
                                                            "xyz.openbmc_project.ObjectMapper",
                                                            "GetSubTree",
                                                            "/xyz/openbmc_project/inventory",
                                                            0,
                                                            std::array<
                                                                const char*, 1>{
                                                                "com.nvidia.PowerSmoothing.AdminPowerProfile"});
                                        }
                                    },
                                    "xyz.openbmc_project.ObjectMapper",
                                    path + "/admin_override",
                                    "org.freedesktop.DBus.Properties", "Get",
                                    "xyz.openbmc_project.Association",
                                    "endpoints");
                    return;
                }
                // Object not found
                messages::resourceNotFound(
                    aResp->res,
                    "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                    processorId);
            },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/inventory", 0,
            std::array<const char*, 2>{
                "xyz.openbmc_project.Inventory.Item.Accelerator",
                "xyz.openbmc_project.Inventory.Item.Cpu"});
}

inline void patchPresetProfile(
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& processorId,
    const std::string& profileId, std::string propName, double propValue)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    crow::connections::
        systemBus
            ->async_method_call(
                [processorId, profileId, propName, propValue,
                 aResp{std::move(aResp)}](
                    const boost::system::error_code ec,
                    const boost::container::flat_map<
                        std::string,
                        boost::container::flat_map<
                            std::string, std::vector<std::string>>>& subtree) {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG("DBUS response error");
                        messages::internalError(aResp->res);

                        return;
                    }
                    for (const auto& [path, object] : subtree)
                    {
                        if (!path.ends_with(processorId))
                        {
                            continue;
                        }
                        crow::
                            connections::
                                systemBus
                                    ->async_method_call(
                                        [aResp, profileId, propName, propValue,
                                         processorId](
                                            const boost::system::error_code ec2,
                                            std::variant<std::vector<
                                                std::string>>& resp) {
                                            if (ec2)
                                            {
                                                return; // no processors = no
                                                        // failures
                                            }
                                            std::vector<std::string>* data =
                                                std::get_if<
                                                    std::vector<std::string>>(
                                                    &resp);
                                            if (data == nullptr)
                                            {
                                                return;
                                            }
                                            bool profileExists = false;

                                            for (const std::string&
                                                     profilePath : *data)
                                            {
                                                sdbusplus::message::object_path
                                                    objectPath(profilePath);
                                                std::string profileIdOndbus =
                                                    objectPath.filename();
                                                if (profileIdOndbus !=
                                                    profileId)
                                                {
                                                    continue;
                                                }
                                                profileExists = true;
                                                BMCWEB_LOG_ERROR(
                                                    "Profile ID: {}",
                                                    profileId);
                                                crow::connections::systemBus
                                                    ->async_method_call(
                                                        [processorId, profileId,
                                                         propName, propValue,
                                                         profilePath,
                                                         aResp{std::move(
                                                             aResp)}](const boost::
                                                                          system::error_code
                                                                              ec,
                                                                      const boost::
                                                                          container::
                                                                              flat_map<
                                                                                  std::string, boost::
                                                                                                   container::flat_map<std::string, std::vector<std::string>>>& subtree) {
                                                            if (ec)
                                                            {
                                                                BMCWEB_LOG_ERROR(
                                                                    "DBUS response error");
                                                                messages::
                                                                    internalError(
                                                                        aResp
                                                                            ->res);

                                                                return;
                                                            }
                                                            for (
                                                                const auto& [path,
                                                                             object] :
                                                                subtree)
                                                            {
                                                                sdbusplus::message::
                                                                    object_path
                                                                        objectPath(
                                                                            path);
                                                                BMCWEB_LOG_ERROR(
                                                                    "Profile ID as per objectpath: {}",
                                                                    objectPath
                                                                        .filename());
                                                                if (objectPath
                                                                        .filename() !=
                                                                    profileId)
                                                                {
                                                                    continue;
                                                                }
                                                                for (
                                                                    const auto& [service,
                                                                                 interfaces] :
                                                                    object)
                                                                {
                                                                    if (std::find(
                                                                            interfaces
                                                                                .begin(),
                                                                            interfaces
                                                                                .end(),
                                                                            "com.nvidia.PowerSmoothing.PowerProfile") !=
                                                                        interfaces
                                                                            .end())
                                                                    {
                                                                        // Set
                                                                        // the
                                                                        // property,
                                                                        // with
                                                                        // handler
                                                                        // to
                                                                        // check
                                                                        // error
                                                                        // responses
                                                                        crow::connections::systemBus
                                                                            ->async_method_call(
                                                                                [aResp,
                                                                                 processorId,
                                                                                 propName,
                                                                                 profileId](
                                                                                    boost::system::
                                                                                        error_code
                                                                                            ec,
                                                                                    sdbusplus::message::
                                                                                        message&
                                                                                            msg) {
                                                                                    if (!ec)
                                                                                    {
                                                                                        BMCWEB_LOG_DEBUG(
                                                                                            "Preset Power profile update succeeded");
                                                                                        return;
                                                                                    }

                                                                                    BMCWEB_LOG_DEBUG(
                                                                                        "Preset Power profile update failed  for processor {}, profileId {}, property {}",
                                                                                        processorId,
                                                                                        profileId,
                                                                                        propName,
                                                                                        ec);
                                                                                    // Read and convert dbus error message
                                                                                    // to redfish error
                                                                                    const sd_bus_error*
                                                                                        dbusError =
                                                                                            msg.get_error();
                                                                                    if (dbusError ==
                                                                                        nullptr)
                                                                                    {
                                                                                        messages::internalError(
                                                                                            aResp
                                                                                                ->res);
                                                                                        return;
                                                                                    }

                                                                                    if (strcmp(
                                                                                            dbusError
                                                                                                ->name,
                                                                                            "xyz.openbmc_project.Common."
                                                                                            "Device.Error.WriteFailure") ==
                                                                                        0)
                                                                                    {
                                                                                        // Service failed to change the
                                                                                        // config
                                                                                        messages::operationFailed(
                                                                                            aResp
                                                                                                ->res);
                                                                                    }
                                                                                    else
                                                                                    {
                                                                                        messages::internalError(
                                                                                            aResp
                                                                                                ->res);
                                                                                    }
                                                                                },
                                                                                service,
                                                                                path,
                                                                                "org.freedesktop.DBus.Properties",
                                                                                "Set",
                                                                                "com.nvidia.PowerSmoothing.PowerProfile",
                                                                                propName,
                                                                                std::variant<
                                                                                    double>(
                                                                                    propValue));
                                                                    }
                                                                }
                                                            }
                                                        },
                                                        "xyz.openbmc_project.ObjectMapper",
                                                        "/xyz/openbmc_project/object_mapper",
                                                        "xyz.openbmc_project.ObjectMapper",
                                                        "GetSubTree",
                                                        "/xyz/openbmc_project/inventory",
                                                        0,
                                                        std::array<const char*,
                                                                   1>{
                                                            "com.nvidia.PowerSmoothing.PowerProfile"});
                                            }
                                            // Object not found
                                            if (!profileExists)
                                            {
                                                messages::resourceNotFound(
                                                    aResp->res,
                                                    "#NvidiaPowerSmoothingPresetProfile.v1_0_0.NvidiaPowerSmoothingPresetProfile",
                                                    profileId);
                                            }
                                        },
                                        "xyz.openbmc_project.ObjectMapper",
                                        path + "/power_profile",
                                        "org.freedesktop.DBus.Properties",
                                        "Get",
                                        "xyz.openbmc_project.Association",
                                        "endpoints");
                        return;
                    }
                    // Object not found
                    messages::resourceNotFound(
                        aResp->res,
                        "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                        processorId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/inventory", 0,
                std::array<const char*, 2>{
                    "xyz.openbmc_project.Inventory.Item.Accelerator",
                    "xyz.openbmc_project.Inventory.Item.Cpu"});
}

inline void
    applyAdminOverride(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
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
    crow::connections::systemBus->async_method_call(
        [processorId, aResp{std::move(aResp)}](
            const boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, boost::container::flat_map<
                                 std::string, std::vector<std::string>>>&
                subtree) {
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

                for (const auto& [service, interfaces] : object)
                {
                    if (std::find(
                            interfaces.begin(), interfaces.end(),
                            "com.nvidia.PowerSmoothing.ProfileActionAsync") ==
                        interfaces.end())
                    {
                        // Object not found
                        messages::resourceNotFound(
                            aResp->res,
                            "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                            processorId);
                        return;
                    }
                    applyAdminOverride(aResp, service, path);
                }
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                processorId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 2>{
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "xyz.openbmc_project.Inventory.Item.Cpu"});
}

inline void
    activatePresetProfile(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& connection,
                          const std::string& path, const uint16_t& profileId)
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
    crow::connections::systemBus->async_method_call(
        [processorId, profileId, aResp{std::move(aResp)}](
            const boost::system::error_code ec,
            const boost::container::flat_map<
                std::string, boost::container::flat_map<
                                 std::string, std::vector<std::string>>>&
                subtree) {
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

                for (const auto& [service, interfaces] : object)
                {
                    if (std::find(
                            interfaces.begin(), interfaces.end(),
                            "com.nvidia.PowerSmoothing.ProfileActionAsync") ==
                        interfaces.end())
                    {
                        // Object not found
                        messages::resourceNotFound(
                            aResp->res,
                            "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                            processorId);
                        return;
                    }
                    activatePresetProfile(aResp, service, path, profileId);
                }
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#NvidiaPowerSmoothing.v1_1_0.NvidiaPowerSmoothing",
                processorId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 2>{
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "xyz.openbmc_project.Inventory.Item.Cpu"});
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
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::optional<bool> pwrSmoothingFeature;
                std::optional<bool> immediateRampDownFeature;
                if (!redfish::json_util::readJsonAction(
                        req, asyncResp->res, "Enabled", pwrSmoothingFeature,
                        "ImmediateRampDown", immediateRampDownFeature))
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
            nlohmann::json& parameters = asyncResp->res.jsonValue["Parameters"];
            nlohmann::json param = nlohmann::json::object();
            param["Name"] = "ProfileId";
            param["Required"] = true;
            param["MaximumValue"] = 4;
            param["MinimumValue"] = 0;
            parameters.push_back(param);
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
                if (!redfish::json_util::readJsonAction(
                        req, asyncResp->res, "TMPFloorPercent", tmpFloorPercent,
                        "RampUpWattsPerSecond", rampUpWattsPerSecond,
                        "RampDownWattsPerSecond", rampDownWattsPerSecond,
                        "RampDownHysteresisSeconds", rampDownHysteresisSeconds))
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
            });
}

inline void
    requestRoutesProcessorPowerSmoothingPresetProfileCollection(App& app)
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
                if (!redfish::json_util::readJsonAction(
                        req, asyncResp->res, "TMPFloorPercent", tmpFloorPercent,
                        "RampUpWattsPerSecond", rampUpWattsPerSecond,
                        "RampDownWattsPerSecond", rampDownWattsPerSecond,
                        "RampDownHysteresisSeconds", rampDownHysteresisSeconds))
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
            });
}

} // namespace redfish
