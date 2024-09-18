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
#include <cstdint>
#include <string_view>
namespace redfish
{
using DbusProperties =
    std::vector<std::pair<std::string, dbus::utility::DbusVariantType>>;

inline void
    getProcessorWorkloadPowerInfo(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                  const std::string& service,
                                  const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get processor smoothing control data.");
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
            if (property.first == "EnforcedProfileMask")
            {
                const std::vector<uint8_t>* value =
                    std::get_if<std::vector<uint8_t>>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("EnforcedProfileMask nullptr");
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["EnforcedProfileMask"] =
                    vectorTo256BitHexString(*value);
            }
            else if (property.first == "RequestedProfileMask")
            {
                const std::vector<uint8_t>* value =
                    std::get_if<std::vector<uint8_t>>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("RequestedProfileMask nullptr");
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["RequestedProfileMask"] =
                    vectorTo256BitHexString(*value);
            }
            else if (property.first == "SupportedProfileMask")
            {
                const std::vector<uint8_t>* value =
                    std::get_if<std::vector<uint8_t>>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("SupportedProfileMask nullptr");
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["SupportedProfileMask"] =
                    vectorTo256BitHexString(*value);
            }
        }
    },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.PowerProfile.ProfileInfo");
}

inline void validateProcessorAndGetWorkloadPowerInfo(
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
            if (!boost::ends_with(path, processorId))
            {
                continue;
            }
            std::string workLoadPowerURI =
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/Processors/";
            workLoadPowerURI += processorId;
            workLoadPowerURI += "/Oem/Nvidia/WorkloadPowerProfile";
            aResp->res.jsonValue["@odata.type"] =
                "#NvidiaWorkloadPower.v1_0_0.NvidiaWorkloadPower";
            aResp->res.jsonValue["@odata.id"] = workLoadPowerURI;
            aResp->res.jsonValue["Id"] = "WorkloadPowerProfile";
            aResp->res.jsonValue["Name"] = processorId +
                                           " Workload Power Profile";

            std::string presetProfileURI = workLoadPowerURI;
            presetProfileURI += "/Profiles";
            aResp->res.jsonValue["Profiles"]["@odata.id"] = presetProfileURI;

            std::string enableProfilesInfoURI = workLoadPowerURI;
            enableProfilesInfoURI += "/EnableProfilesActionInfo";
            aResp->res
                .jsonValue["Actions"]["#NvidiaWorkloadPower.EnableProfiles"]
                          ["@Redfish.ActionInfo"] = enableProfilesInfoURI;

            std::string enableProfilesURI = workLoadPowerURI;
            enableProfilesURI += "/Actions/NvidiaWorkloadPower.EnableProfiles";
            aResp->res
                .jsonValue["Actions"]["#NvidiaWorkloadPower.EnableProfiles"]
                          ["target"] = enableProfilesURI;

            std::string disableProfilesInfoURI = workLoadPowerURI;
            disableProfilesInfoURI += "/DisableProfilesActionInfo";
            aResp->res
                .jsonValue["Actions"]["#NvidiaWorkloadPower.DisableProfiles"]
                          ["@Redfish.ActionInfo"] = disableProfilesInfoURI;

            std::string disableProfilesURI = workLoadPowerURI;
            disableProfilesURI +=
                "/Actions/NvidiaWorkloadPower.DisableProfiles";
            aResp->res
                .jsonValue["Actions"]["#NvidiaWorkloadPower.DisableProfiles"]
                          ["target"] = disableProfilesURI;

            for (const auto& [service, interfaces] : object)
            {
                if (std::find(interfaces.begin(), interfaces.end(),
                              "com.nvidia.PowerProfile.ProfileInfo") ==
                    interfaces.end())
                {
                    // Object not found
                    messages::resourceNotFound(
                        aResp->res,
                        "#NvidiaWorkloadPower.v1_0_0.NvidiaWorkloadPower",
                        processorId);
                    return;
                }
                getProcessorWorkloadPowerInfo(aResp, service, path);
            }
            return;
        }
        // Object not found
        messages::resourceNotFound(
            aResp->res, "#NvidiaWorkloadPower.v1_0_0.NvidiaWorkloadPower",
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

inline void getWorkLoadProfileData(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                   const std::string& service,
                                   const std::string& objPath,
                                   const std::string& processorId)
{
    BMCWEB_LOG_DEBUG("Get processor current profile data.");
    crow::connections::systemBus->async_method_call(
        [processorId,
         aResp{std::move(aResp)}](const boost::system::error_code ec,
                                  const DbusProperties& properties) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error");
            messages::internalError(aResp->res);
            return;
        }

        for (const auto& property : properties)
        {
            if (property.first == "ConflictMask")
            {
                const std::vector<uint8_t>* value =
                    std::get_if<std::vector<uint8_t>>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["ConflictingMask"] =
                    vectorTo256BitHexString(*value);
            }
            else if (property.first == "Priority")
            {
                const uint16_t* value = std::get_if<uint16_t>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["Priority"] = *value;
            }
            else if (property.first == "ProfileName")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value == nullptr)
                {
                    messages::internalError(aResp->res);
                    return;
                }
                std::string profileName = processorId;
                profileName += "  Workload Power Profile ";
                profileName += *value;
                aResp->res.jsonValue["Name"] = profileName;
            }
        }
    },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.PowerProfile.Profile");
}

inline void validateProcessorWorkloadPowerProfile(
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
            if (!boost::ends_with(path, processorId))
            {
                continue;
            }
            std::string profileURI =
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/Processors/";
            profileURI += processorId;
            profileURI += "/Oem/Nvidia/WorkloadPowerProfile/Profiles/";
            profileURI += profileId;
            aResp->res.jsonValue["@odata.type"] =
                "#NvidiaWorkloadPowerProfile.v1_0_0.NvidiaWorkloadPowerProfile";
            aResp->res.jsonValue["@odata.id"] = profileURI;
            aResp->res.jsonValue["Id"] = profileId;

            crow::connections::systemBus->async_method_call(
                [aResp, profileId,
                 processorId](const boost::system::error_code ec2,
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
                    sdbusplus::message::object_path objectPath(profilePath);
                    std::string profileIdOndbus = objectPath.filename();
                    if (profileIdOndbus != profileId)
                    {
                        continue;
                    }
                    profileExists = true;
                    std::string objectPathToGetProfileData = profilePath;
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
                        getWorkLoadProfileData(aResp, service,
                                               objectPathToGetProfileData,
                                               processorId);
                    },
                        "xyz.openbmc_project.ObjectMapper",
                        "/xyz/openbmc_project/object_mapper",
                        "xyz.openbmc_project.ObjectMapper", "GetObject",
                        objectPathToGetProfileData,
                        std::array<const char*, 1>{
                            "com.nvidia.PowerProfile.Profile"});
                }
                // Object not found
                if (!profileExists)
                {
                    messages::resourceNotFound(
                        aResp->res,
                        "#NvidiaWorkloadPowerProfile.v1_0_0.NvidiaWorkloadPowerProfile",
                        profileId);
                }
            },
                "xyz.openbmc_project.ObjectMapper",
                path + "/workload_power_profile",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Association", "endpoints");
            return;
        }
        // Object not found
        messages::resourceNotFound(aResp->res, "#Processor.v1_20_0.Processor",
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

inline void getProcessorWorkloadPowerProfileCollectionData(
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
            if (!boost::ends_with(path, processorId))
            {
                continue;
            }
            std::string profileCollectionURI =
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/Processors/";
            profileCollectionURI += processorId;
            profileCollectionURI += "/Oem/Nvidia/WorkloadPowerProfile/Profiles";
            aResp->res.jsonValue["@odata.type"] =
                "#NvidiaWorkloadPowerProfileCollection.NvidiaWorkloadPowerProfileCollection";
            aResp->res.jsonValue["@odata.id"] = profileCollectionURI;

            std::string name = processorId;
            name += " Workload Power Profile Collection";
            aResp->res.jsonValue["Name"] = name;
            aResp->res.jsonValue["Members"] = nlohmann::json::array();
            aResp->res.jsonValue["Members@odata.count"] = 0;

            crow::connections::systemBus->async_method_call(
                [aResp, profileCollectionURI,
                 processorId](const boost::system::error_code ec2,
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
                nlohmann::json& addMembers = aResp->res.jsonValue["Members"];
                for (const std::string& profilePath : *data)
                {
                    sdbusplus::message::object_path objectPath(profilePath);
                    std::string profileUri = profileCollectionURI;
                    profileUri += "/";
                    profileUri += objectPath.filename();
                    addMembers.push_back({{"@odata.id", profileUri}});
                }
                aResp->res.jsonValue["Members@odata.count"] = addMembers.size();
            },
                "xyz.openbmc_project.ObjectMapper",
                path + "/workload_power_profile",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Association", "endpoints");
            return;
        }
        // Object not found
        messages::resourceNotFound(
            aResp->res,
            "#NvidiaWorkloadPowerProfileCollection.NvidiaWorkloadPowerProfileCollection",
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

inline void enableWorkLoadPowerProfile(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connection, const std::string& path,
    const std::vector<uint8_t>& profileMask)
{
    dbus::utility::getDbusObject(
        path,
        std::array<std::string_view, 1>{
            "com.nvidia.PowerProfile.ProfileInfoAsync"},
        [asyncResp, path, profileMask,
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
                    int>(
                    asyncResp, std::chrono::seconds(60), connection, path,
                    "com.nvidia.PowerProfile.ProfileInfoAsync",
                    "EnablePresetProfile",
                    [asyncResp,
                     profileMask](const std::string& status,
                                  [[maybe_unused]] const int* retValue) {
                    if (status ==
                        nvidia_async_operation_utils::asyncStatusValueSuccess)
                    {
                        BMCWEB_LOG_DEBUG("EnablePresetProfile Succeeded");
                        messages::success(asyncResp->res);
                        return;
                    }
                    BMCWEB_LOG_ERROR("EnablePresetProfile Throws error {}",
                                     status);
                    messages::internalError(asyncResp->res);
                },
                    profileMask);
                return;
            }
        }
    });
}

inline void
    postEnableWorkLoadPowerProfile(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                   const std::string& processorId,
                                   std::vector<uint8_t>& profileMask)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    crow::connections::systemBus->async_method_call(
        [processorId, aResp{std::move(aResp)}, profileMask](
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
            if (!boost::ends_with(path, processorId))
            {
                continue;
            }
            for (const auto& [service, interfaces] : object)
            {
                if (std::find(interfaces.begin(), interfaces.end(),
                              "com.nvidia.PowerProfile.ProfileInfoAsync") ==
                    interfaces.end())
                {
                    // Object not found
                    messages::resourceNotFound(
                        aResp->res,
                        "#NvidiaWorkloadPower.v1_0_0.NvidiaWorkloadPower",
                        processorId);
                    return;
                }
                enableWorkLoadPowerProfile(aResp, service, path, profileMask);
            }

            return;
        }
        // Object not found
        messages::resourceNotFound(
            aResp->res, "#NvidiaWorkloadPower.v1_0_0.NvidiaWorkloadPower",
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

inline void disableWorkLoadPowerProfile(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connection, const std::string& path,
    const std::vector<uint8_t>& profileMask)
{
    dbus::utility::getDbusObject(
        path,
        std::array<std::string_view, 1>{
            "com.nvidia.PowerProfile.ProfileInfoAsync"},
        [asyncResp, path, profileMask,
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
                    int>(
                    asyncResp, std::chrono::seconds(60), connection, path,
                    "com.nvidia.PowerProfile.ProfileInfoAsync",
                    "DisablePresetProfile",
                    [asyncResp,
                     profileMask](const std::string& status,
                                  [[maybe_unused]] const int* retValue) {
                    if (status ==
                        nvidia_async_operation_utils::asyncStatusValueSuccess)
                    {
                        BMCWEB_LOG_DEBUG("DisablePresetProfile Succeeded");
                        messages::success(asyncResp->res);
                        return;
                    }
                    BMCWEB_LOG_ERROR("DisablePresetProfile Throws error {}",
                                     status);
                    messages::internalError(asyncResp->res);
                },
                    profileMask);
                return;
            }
        }
    });
}

inline void
    postDisableWorkLoadPowerProfile(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                    const std::string& processorId,
                                    std::vector<uint8_t>& profileMask)
{
    BMCWEB_LOG_DEBUG("Get available system processor resource");
    crow::connections::systemBus->async_method_call(
        [processorId, aResp{std::move(aResp)}, profileMask](
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
            if (!boost::ends_with(path, processorId))
            {
                continue;
            }
            for (const auto& [service, interfaces] : object)
            {
                if (std::find(interfaces.begin(), interfaces.end(),
                              "com.nvidia.PowerProfile.ProfileInfoAsync") ==
                    interfaces.end())
                {
                    // Object not found
                    messages::resourceNotFound(
                        aResp->res,
                        "#NvidiaWorkloadPower.v1_0_0.NvidiaWorkloadPower",
                        processorId);
                    return;
                }
                disableWorkLoadPowerProfile(aResp, service, path, profileMask);
            }

            return;
        }
        // Object not found
        messages::resourceNotFound(
            aResp->res, "#NvidiaWorkloadPower.v1_0_0.NvidiaWorkloadPower",
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

inline void requestRoutesProcessorWorkloadPower(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/Processors/<str>/Oem/Nvidia/WorkloadPowerProfile/")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& processorId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        validateProcessorAndGetWorkloadPowerInfo(asyncResp, processorId);
    });

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/Processors/<str>/Oem/Nvidia/WorkloadPowerProfile/Actions/NvidiaWorkloadPower.EnableProfiles")
        .privileges(redfish::privileges::postProcessor)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& processorId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::optional<std::vector<uint8_t>> profileMask;

        if (!redfish::json_util::readJsonAction(req, asyncResp->res,
                                                "ProfileMask", profileMask))
        {
            return;
        }
        if (profileMask)
        {
            postEnableWorkLoadPowerProfile(asyncResp, processorId,
                                           *profileMask);
        }
    });

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/Processors/<str>/Oem/Nvidia/WorkloadPowerProfile/EnableProfilesActionInfo")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
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
            "/Oem/Nvidia/WorkloadPowerProfile/EnableProfilesActionInfo";
        asyncResp->res.jsonValue["@odata.id"] = actionInfoURI;
        asyncResp->res.jsonValue["@odata.type"] =
            "#ActionInfo.v1_3_0.ActionInfo";
        asyncResp->res.jsonValue["Id"] = "EnableProfilesActionInfo";
        asyncResp->res.jsonValue["Name"] =
            "WorkloadPowerProfile EnableProfiles Action Info";
        nlohmann::json& parameters = asyncResp->res.jsonValue["Parameters"];
        nlohmann::json param = nlohmann::json::object();
        param["Name"] = "ProfileMask";
        param["Required"] = true;
        param["DataType"] = "String";
        param["AllowablePattern"] = "^0x[0-9A-Fa-f]+$";
        parameters.push_back(param);
    });

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/Processors/<str>/Oem/Nvidia/WorkloadPowerProfile/Actions/NvidiaWorkloadPower.DisableProfiles")
        .privileges(redfish::privileges::postProcessor)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& processorId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::optional<std::vector<uint8_t>> profileMask;

        if (!redfish::json_util::readJsonAction(req, asyncResp->res,
                                                "ProfileMask", profileMask))
        {
            return;
        }
        if (profileMask)
        {
            postDisableWorkLoadPowerProfile(asyncResp, processorId,
                                            *profileMask);
        }
    });

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/Processors/<str>/Oem/Nvidia/WorkloadPowerProfile/DisableProfilesActionInfo")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
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
            "/Oem/Nvidia/WorkloadPowerProfile/DisableProfilesActionInfo";
        asyncResp->res.jsonValue["@odata.id"] = actionInfoURI;
        asyncResp->res.jsonValue["@odata.type"] =
            "#ActionInfo.v1_3_0.ActionInfo";
        asyncResp->res.jsonValue["Id"] = "DisableProfilesActionInfo";
        asyncResp->res.jsonValue["Name"] =
            "WorkloadPowerProfile DisableProfiles Action Info";
        nlohmann::json& parameters = asyncResp->res.jsonValue["Parameters"];
        nlohmann::json param = nlohmann::json::object();
        param["Name"] = "ProfileMask";
        param["Required"] = true;
        param["DataType"] = "String";
        param["AllowablePattern"] = "^0x[0-9A-Fa-f]+$";
        parameters.push_back(param);
    });
}

inline void requestRoutesProcessorWorkloadPowerProfileCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/" +
                 std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                 "/Processors/<str>/Oem/Nvidia/WorkloadPowerProfile/Profiles/")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& processorId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        getProcessorWorkloadPowerProfileCollectionData(asyncResp, processorId);
    });
}

inline void requestRoutesProcessorWorkloadPowerProfile(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/Processors/<str>/Oem/Nvidia/WorkloadPowerProfile/Profiles/<str>/")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& processorId,
                   const std::string& profileId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        validateProcessorWorkloadPowerProfile(asyncResp, processorId,
                                              profileId);
    });
}

} // namespace redfish
