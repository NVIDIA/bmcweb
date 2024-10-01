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
#include "utils/nvidia_async_set_utils.hpp"

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
using OperatingConfigProperties =
    std::vector<std::pair<std::string, dbus::utility::DbusVariantType>>;

using ErrorInjectionPatchMap = std::map<std::string, bool>;

inline ErrorInjectionPatchMap
    parseErrorInjectionJson(const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    ErrorInjectionPatchMap properties;

    std::optional<bool> errorInjectionModeEnabled;
    std::optional<nlohmann::json> errorInjectionCapabilities;
    std::map<std::string, std::optional<nlohmann::json>> capabilities = {
        {"MemoryErrors", {}},
        {"PCIeErrors", {}},
        {"NVLinkErrors", {}},
        {"ThermalErrors", {}}};
    if (!redfish::json_util::readJsonAction(
            req, aResp->res, "ErrorInjectionModeEnabled",
            errorInjectionModeEnabled, "ErrorInjectionCapabilities",
            errorInjectionCapabilities))
    {
        return properties;
    }
    if (errorInjectionModeEnabled)
    {
        properties["ErrorInjectionModeEnabled"] = *errorInjectionModeEnabled;
    }
    if (errorInjectionCapabilities &&
        redfish::json_util::readJson(
            *errorInjectionCapabilities, aResp->res, "MemoryErrors",
            capabilities["MemoryErrors"], "PCIeErrors",
            capabilities["PCIeErrors"], "NVLinkErrors",
            capabilities["NVLinkErrors"], "ThermalErrors",
            capabilities["ThermalErrors"]))
    {
        for (auto& [name, json] : capabilities)
        {
            std::optional<bool> enabled;
            if (json &&
                redfish::json_util::readJson(*json, aResp->res, "Enabled",
                                             enabled) &&
                enabled)
            {
                properties[name] = *enabled;
            }
        }
    }

    return properties;
}

/**
 * @brief Fill out error injection processor nvidia specific info by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       capability  Capability name
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getErrorInjectionCapabilityData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& capability, const std::string& service,
    const std::string& objPath)

{
    crow::connections::systemBus->async_method_call(
        [aResp, capability](const boost::system::error_code ec,
                            const OperatingConfigProperties& properties) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error");
            messages::internalError(aResp->res);
            return;
        }
        auto& json =
            aResp->res.jsonValue["ErrorInjectionCapabilities"][capability];
        for (const auto& property : properties)
        {
            if (property.first == "Supported")
            {
                const bool* supported = std::get_if<bool>(&property.second);
                if (supported == nullptr)
                {
                    BMCWEB_LOG_ERROR("Get Supported property failed");
                    messages::internalError(aResp->res);
                    return;
                }
                json["Supported"] = *supported;
            }
            else if (property.first == "Enabled")
            {
                const bool* enabled = std::get_if<bool>(&property.second);
                if (enabled == nullptr)
                {
                    BMCWEB_LOG_ERROR("Get Enabled property failed");
                    messages::internalError(aResp->res);
                    return;
                }
                json["Enabled"] = *enabled;
            }
        }
    },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.ErrorInjection.ErrorInjectionCapability");
}

/**
 * @brief Fill out error injection nvidia specific info by
 * requesting data from the given D-Bus object.
 *
 * @param[in,out]   aResp       Async HTTP response.
 * @param[in]       baseUri     Redfish base uri
 * @param[in]       service     D-Bus service to query.
 * @param[in]       objPath     D-Bus object to query.
 */
inline void getErrorInjectionData(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, const std::string& baseUri,
    const std::string& service, const std::string& objPath)

{
    crow::connections::systemBus->async_method_call(
        [aResp, baseUri, service,
         objPath](const boost::system::error_code ec,
                  const OperatingConfigProperties& properties) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error");
            messages::internalError(aResp->res);
            return;
        }
        nlohmann::json& json = aResp->res.jsonValue;
        json["@odata.type"] =
            "#NvidiaErrorInjection.v1_0_0.NvidiaErrorInjection";
        json["@odata.id"] = baseUri + "/Oem/Nvidia/ErrorInjection";
        json["Id"] = "ErrorInjection";
        json["Name"] = baseUri.substr(baseUri.find_last_of('/') + 1) +
                       " Error Injection";
        for (const auto& property : properties)
        {
            if (property.first == "ErrorInjectionModeEnabled")
            {
                const bool* errorInjectionModeEnabled =
                    std::get_if<bool>(&property.second);
                if (errorInjectionModeEnabled == nullptr)
                {
                    BMCWEB_LOG_ERROR(
                        "Get ErrorInjectionModeEnabled property failed");
                    messages::internalError(aResp->res);
                    return;
                }
                json["ErrorInjectionModeEnabled"] = *errorInjectionModeEnabled;
            }
            else if (property.first == "PersistentDataModified")
            {
                const bool* persistentDataModified =
                    std::get_if<bool>(&property.second);
                if (persistentDataModified == nullptr)
                {
                    BMCWEB_LOG_ERROR(
                        "Get PersistentDataModified property failed");
                    messages::internalError(aResp->res);
                    return;
                }
                json["PersistentDataModified"] = *persistentDataModified;
            }
        }
        std::vector<std::string> capabilities = {
            "MemoryErrors", "PCIeErrors", "NVLinkErrors", "ThermalErrors"};
        for (auto& cap : capabilities)
        {
            getErrorInjectionCapabilityData(aResp, cap, service,
                                            objPath + "/" + cap);
        }
    },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.ErrorInjection.ErrorInjection");
}

inline void patchErrorInjectionData(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                    const std::string& service,
                                    const std::string& path,
                                    const ErrorInjectionPatchMap& properties)
{
    for (const auto& [name, value] : properties)
    {
        if (name == "ErrorInjectionModeEnabled")
        {
            nvidia_async_operation_utils::patch(
                aResp, service, path,
                "com.nvidia.ErrorInjection.ErrorInjection", name, value);
        }
        else
        {
            nvidia_async_operation_utils::patch(
                aResp, service, path + "/" + name,
                "com.nvidia.ErrorInjection.ErrorInjectionCapability", "Enabled",
                value);
        }
    }
}

template <typename Handler>
inline void getErrorInjectionService(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                     const std::string& path, Handler&& handler)
{
    const auto eiPath = path + "/ErrorInjection";
    crow::connections::systemBus->async_method_call(
        [aResp, eiPath, handler{std::forward<Handler>(handler)}](
            const boost::system::error_code ec,
            const MapperServiceMap& serviceMap) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Error while fetching service for {}", eiPath);
            messages::internalError(aResp->res);
            return;
        }

        for (const auto& [service, interfaces] : serviceMap)
        {
            if (std::find(interfaces.begin(), interfaces.end(),
                          "com.nvidia.ErrorInjection.ErrorInjection") ==
                interfaces.end())
            {
                continue;
            }
            handler(service, eiPath);
            return;
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", eiPath,
        std::array<const char*, 0>());
}

template <typename Handler>
inline void getProcessor(std::shared_ptr<bmcweb::AsyncResp> aResp,
                         const std::string& processorId, Handler&& handler)
{
    crow::connections::systemBus->async_method_call(
        [processorId, aResp, handler{std::forward<Handler>(handler)}](
            const boost::system::error_code ec,
            const dbus::utility::MapperGetSubTreePathsResponse& paths) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error");
            messages::internalError(aResp->res);

            return;
        }
        for (const auto& path : paths)
        {
            if (!boost::ends_with(path, processorId))
            {
                continue;
            }

            getErrorInjectionService(
                aResp, path,
                [processorId, aResp, handler](const std::string& service,
                                              const std::string& path) {
                handler("/redfish/v1/Systems/" +
                            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                            "/Processors/" + processorId,
                        service, path);
            });
            return;
        }
        // Object not found
        messages::resourceNotFound(
            aResp->res, "#NvidiaErrorInjection.v1_0_0.NvidiaErrorInjection",
            processorId);
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 2>{
            "xyz.openbmc_project.Inventory.Item.Accelerator",
            "xyz.openbmc_project.Inventory.Item.Cpu"});
}

inline void
    getProcessorErrorInjectionData(App& app, const crow::Request& req,
                                   std::shared_ptr<bmcweb::AsyncResp> aResp,
                                   const std::string& processorId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    getProcessor(aResp, processorId,
                 [aResp](const std::string& uri, const std::string& service,
                         const std::string& path) {
        getErrorInjectionData(aResp, uri, service, path);
    });
}

inline void
    patchProcessorErrorInjectionData(App& app, const crow::Request& req,
                                     std::shared_ptr<bmcweb::AsyncResp> aResp,
                                     const std::string& processorId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    auto properties = parseErrorInjectionJson(req, aResp);
    getProcessor(aResp, processorId,
                 [aResp, properties]([[maybe_unused]] const std::string& uri,
                                     const std::string& service,
                                     const std::string& path) {
        patchErrorInjectionData(aResp, service, path, properties);
    });
}

template <typename Handler>
inline void getNetworkAdapter(std::shared_ptr<bmcweb::AsyncResp> aResp,
                              const std::string& chassisId,
                              const std::string& networkAdapterId,
                              Handler&& handler)
{
    crow::connections::systemBus->async_method_call(
        [chassisId, networkAdapterId, aResp,
         handler{std::forward<Handler>(handler)}](
            const boost::system::error_code ec,
            const dbus::utility::MapperGetSubTreePathsResponse& paths) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error");
            messages::internalError(aResp->res);

            return;
        }
        for (const auto& path : paths)
        {
            if (!boost::ends_with(path, networkAdapterId) ||
                path.find(chassisId) == path.npos)
            {
                continue;
            }

            getErrorInjectionService(
                aResp, path,
                [chassisId, networkAdapterId, aResp,
                 handler](const std::string& service, const std::string& path) {
                handler("/redfish/v1/Chassis/" + chassisId +
                            "/NetworkAdapters/" + networkAdapterId,
                        service, path);
            });
            return;
        }
        // Object not found
        messages::resourceNotFound(
            aResp->res, "#NvidiaErrorInjection.v1_0_0.NvidiaErrorInjection",
            networkAdapterId);
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/inventory/", 0,
        std::array<std::string, 1>{
            "xyz.openbmc_project.Inventory.Item.NetworkInterface"});
}

inline void getNetworkAdapterErrorInjectionData(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& chassisId, const std::string& networkAdapterId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    getNetworkAdapter(aResp, chassisId, networkAdapterId,
                      [aResp](const std::string& uri,
                              const std::string& service,
                              const std::string& path) {
        getErrorInjectionData(aResp, uri, service, path);
    });
}
inline void patchNetworkAdapterErrorInjectionData(
    App& app, const crow::Request& req,
    std::shared_ptr<bmcweb::AsyncResp> aResp, const std::string& chassisId,
    const std::string& networkAdapterId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    auto properties = parseErrorInjectionJson(req, aResp);
    getNetworkAdapter(aResp, chassisId, networkAdapterId,
                      [aResp, properties](
                          [[maybe_unused]] const std::string& uri,
                          const std::string& service, const std::string& path) {
        patchErrorInjectionData(aResp, service, path, properties);
    });
}

template <typename Handler>
inline void getSwitch(std::shared_ptr<bmcweb::AsyncResp> aResp,
                      const std::string& fabricId, const std::string& switchId,
                      Handler&& handler)
{
    crow::connections::systemBus->async_method_call(
        [fabricId, switchId, aResp, handler{std::forward<Handler>(handler)}](
            const boost::system::error_code ec,
            const dbus::utility::MapperGetSubTreePathsResponse& paths) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error");
            messages::internalError(aResp->res);

            return;
        }
        for (const auto& path : paths)
        {
            if (!boost::ends_with(path, switchId) ||
                path.find(fabricId) == path.npos)
            {
                continue;
            }

            getErrorInjectionService(
                aResp, path,
                [fabricId, switchId, aResp, handler](const std::string& service,
                                                     const std::string& path) {
                handler("/redfish/v1/Fabrics/" + fabricId + "/Switches/" +
                            switchId,
                        service, path);
            });
            return;
        }
        // Object not found
        messages::resourceNotFound(
            aResp->res, "#NvidiaErrorInjection.v1_0_0.NvidiaErrorInjection",
            switchId);
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/inventory/", 0,
        std::array<std::string, 1>{
            "xyz.openbmc_project.Inventory.Item.NvSwitch"});
}

inline void
    getSwitchErrorInjectionData(App& app, const crow::Request& req,
                                std::shared_ptr<bmcweb::AsyncResp> aResp,
                                const std::string& fabricId,
                                const std::string& switchId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    getSwitch(aResp, fabricId, switchId,
              [aResp](const std::string& uri, const std::string& service,
                      const std::string& path) {
        getErrorInjectionData(aResp, uri, service, path);
    });
}
inline void
    patchSwitchErrorInjectionData(App& app, const crow::Request& req,
                                  std::shared_ptr<bmcweb::AsyncResp> aResp,
                                  const std::string& fabricId,
                                  const std::string& switchId)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    auto properties = parseErrorInjectionJson(req, aResp);
    getSwitch(aResp, fabricId, switchId,
              [aResp, properties]([[maybe_unused]] const std::string& uri,
                                  const std::string& service,
                                  const std::string& path) {
        patchErrorInjectionData(aResp, service, path, properties);
    });
}

inline void requestRoutesErrorInjection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/Processors/<str>/Oem/Nvidia/ErrorInjection/")
        .privileges(redfish::privileges::getProcessorCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(getProcessorErrorInjectionData, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/Processors/<str>/Oem/Nvidia/ErrorInjection/")
        .privileges(redfish::privileges::patchProcessorCollection)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(patchProcessorErrorInjectionData, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>"
                      "/Oem/Nvidia/ErrorInjection/")
        .privileges(redfish::privileges::getNetworkAdapterCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            getNetworkAdapterErrorInjectionData, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/NetworkAdapters/<str>"
                      "/Oem/Nvidia/ErrorInjection/")
        .privileges(redfish::privileges::patchNetworkAdapterCollection)
        .methods(boost::beast::http::verb::patch)(std::bind_front(
            patchNetworkAdapterErrorInjectionData, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Switches/<str>"
                      "/Oem/Nvidia/ErrorInjection/")
        .privileges(redfish::privileges::getSwitchCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(getSwitchErrorInjectionData, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Fabrics/<str>/Switches/<str>"
                      "/Oem/Nvidia/ErrorInjection/")
        .privileges(redfish::privileges::patchSwitchCollection)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(patchSwitchErrorInjectionData, std::ref(app)));
}

} // namespace redfish
