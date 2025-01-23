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
#include <utils/nvidia_chassis_util.hpp>
#include <utils/nvidia_histogram_utils.hpp>
#include <utils/systemd_utils.hpp>

#include <array>
#include <cstdint>
#include <string_view>

namespace redfish
{

inline void requestRoutesProcessorPortHistogramBuckets(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Ports/<str>/Oem/Nvidia/Histograms/<str>/Buckets")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId, const std::string& portId,
                   const std::string& histogramId) {
        if (!redfish::setUpRedfishRoute(app, req, aResp))
        {
            return;
        }

        BMCWEB_LOG_DEBUG("Get available system processor resource");
        crow::connections::systemBus->async_method_call(
            [processorId, portId, histogramId, aResp](
                const boost::system::error_code ec,
                const boost::container::flat_map<
                    std::string, boost::container::flat_map<
                                     std::string, std::vector<std::string>>>&
                    subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error while getting processor: {}",
                    ec.message());
                messages::internalError(aResp->res);
                return;
            }
            for (const auto& [objPath, object] : subtree)
            {
                if (!boost::ends_with(objPath, processorId))
                {
                    continue;
                }

                crow::connections::systemBus->async_method_call(
                    [aResp, objPath, processorId, portId, histogramId](
                        const boost::system::error_code ec,
                        std::variant<std::vector<std::string>>& resp) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error while getting port on processor: {}",
                            ec.message());
                        messages::internalError(aResp->res);
                        return;
                    }

                    std::vector<std::string>* data =
                        std::get_if<std::vector<std::string>>(&resp);
                    if (data == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Null data response while getting port on processor");
                        messages::internalError(aResp->res);
                        return;
                    }

                    for (const std::string& sensorpath : *data)
                    {
                        // Check Interface in Object or not
                        BMCWEB_LOG_DEBUG(
                            "processor state sensor object path {}",
                            sensorpath);
                        sdbusplus::message::object_path path(sensorpath);
                        if (path.filename() != portId)
                        {
                            continue;
                        }

                        crow::connections::systemBus->async_method_call(
                            [aResp, processorId, portId, histogramId](
                                const boost::system::error_code ec,
                                std::variant<std::vector<std::string>>& resp) {
                            if (ec)
                            {
                                BMCWEB_LOG_ERROR(
                                    "DBUS response error while getting switch on fabric: {}",
                                    ec.message());
                                messages::internalError(aResp->res);
                                return;
                            }
                            std::vector<std::string>* data =
                                std::get_if<std::vector<std::string>>(&resp);
                            if (data == nullptr)
                            {
                                BMCWEB_LOG_ERROR(
                                    "Null data response while getting switch on fabric");
                                messages::internalError(aResp->res);
                                return;
                            }
                            // Iterate over all retrieved ObjectPaths.
                            for (const std::string& histoPath : *data)
                            {
                                sdbusplus::message::object_path histoObjPath(
                                    histoPath);
                                if (histoObjPath.filename() != histogramId)
                                {
                                    continue;
                                }

                                std::string histoURI = "/redfish/v1/Systems/";
                                histoURI +=
                                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME);
                                histoURI += "/Processors/";
                                histoURI += processorId;
                                histoURI += "/Ports/";
                                histoURI += portId;
                                histoURI += "/Oem/Nvidia/Histograms/";
                                histoURI += histogramId;
                                histoURI += "/Buckets";
                                aResp->res.jsonValue["@odata.type"] =
                                    "#NvidiaHistogramBuckets.v1_0_0.NvidiaHistogramBuckets";
                                aResp->res.jsonValue["@odata.id"] = histoURI;
                                aResp->res.jsonValue["Name"] =
                                    processorId + "_" + portId + "_Histogram_" +
                                    histogramId + "_Buckets";
                                aResp->res.jsonValue["Id"] = "Buckets";
                                aResp->res.jsonValue["Buckets"] =
                                    nlohmann::json::array();
                                redfish::nvidia_histogram_utils::
                                    updateHistogramBucketData(aResp, histoPath);
                            }
                        },
                            "xyz.openbmc_project.ObjectMapper",
                            sensorpath + "/histograms",
                            "org.freedesktop.DBus.Properties", "Get",
                            "xyz.openbmc_project.Association", "endpoints");

                        return;
                    }
                    // Couldn't find an object with that name.
                    // Return an error
                    messages::resourceNotFound(aResp->res, "#Port.v1_0_0.Port",
                                               portId);
                },
                    "xyz.openbmc_project.ObjectMapper", objPath + "/all_states",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#Processor.v1_20_0.Processor", processorId);
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/inventory", 0,
            std::array<const char*, 1>{
                "xyz.openbmc_project.Inventory.Item.Accelerator"});
    });
}

inline void requestRoutesProcessorPortHistogram(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Ports/<str>/Oem/Nvidia/Histograms/<str>")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId, const std::string& portId,
                   const std::string& histogramId) {
        if (!redfish::setUpRedfishRoute(app, req, aResp))
        {
            return;
        }

        BMCWEB_LOG_DEBUG("Get available system processor resource");
        crow::connections::systemBus->async_method_call(
            [processorId, portId, histogramId, aResp](
                const boost::system::error_code ec,
                const boost::container::flat_map<
                    std::string, boost::container::flat_map<
                                     std::string, std::vector<std::string>>>&
                    subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error while getting processor: {}",
                    ec.message());
                messages::internalError(aResp->res);
                return;
            }
            for (const auto& [objPath, object] : subtree)
            {
                if (!boost::ends_with(objPath, processorId))
                {
                    continue;
                }

                crow::connections::systemBus->async_method_call(
                    [aResp, processorId, portId, histogramId](
                        const boost::system::error_code ec,
                        std::variant<std::vector<std::string>>& resp) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error while getting port on processor: {}",
                            ec.message());
                        messages::internalError(aResp->res);
                        return;
                    }

                    std::vector<std::string>* data =
                        std::get_if<std::vector<std::string>>(&resp);
                    if (data == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Null data response while getting port on processor");
                        messages::internalError(aResp->res);
                        return;
                    }

                    for (const std::string& sensorpath : *data)
                    {
                        // Check Interface in Object or not
                        BMCWEB_LOG_DEBUG(
                            "processor state sensor object path {}",
                            sensorpath);
                        sdbusplus::message::object_path path(sensorpath);
                        if (path.filename() != portId)
                        {
                            continue;
                        }

                        std::string histoURI = "/redfish/v1/Systems/";
                        histoURI += std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME);
                        histoURI += "/Processors/";
                        histoURI += processorId;
                        histoURI += "/Ports/";
                        histoURI += portId;
                        histoURI += "/Oem/Nvidia/Histograms/";
                        histoURI += histogramId;
                        aResp->res.jsonValue["@odata.type"] =
                            "#NvidiaHistogram.v1_1_0.NvidiaHistogram";
                        aResp->res.jsonValue["@odata.id"] = histoURI;
                        aResp->res.jsonValue["Id"] = histogramId;
                        aResp->res.jsonValue["Name"] = processorId + "_" +
                                                       portId + "_Histogram_" +
                                                       histogramId;

                        std::string bucketURI = histoURI + "/Buckets";
                        aResp->res.jsonValue["HistogramBuckets"]["@odata.id"] =
                            bucketURI;
                        redfish::nvidia_histogram_utils::
                            getHistogramDataByAssociation(aResp, histogramId,
                                                          sensorpath);

                        return;
                    }
                    // Couldn't find an object with that name.
                    // Return an error
                    messages::resourceNotFound(aResp->res, "#Port.v1_0_0.Port",
                                               portId);
                },
                    "xyz.openbmc_project.ObjectMapper", objPath + "/all_states",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#Processor.v1_20_0.Processor", processorId);
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/inventory", 0,
            std::array<const char*, 1>{
                "xyz.openbmc_project.Inventory.Item.Accelerator"});
    });
}

inline void requestRoutesProcessorPortHistogramCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Processors/<str>/"
                      "Ports/<str>/Oem/Nvidia/Histograms")
        .privileges(redfish::privileges::getProcessor)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& processorId, const std::string& portId) {
        if (!redfish::setUpRedfishRoute(app, req, aResp))
        {
            return;
        }

        BMCWEB_LOG_DEBUG("Get available system processor resource");
        crow::connections::systemBus->async_method_call(
            [processorId, portId, aResp](
                const boost::system::error_code ec,
                const boost::container::flat_map<
                    std::string, boost::container::flat_map<
                                     std::string, std::vector<std::string>>>&
                    subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error while getting processor: {}",
                    ec.message());
                messages::internalError(aResp->res);
                return;
            }
            for (const auto& [objPath, object] : subtree)
            {
                if (!boost::ends_with(objPath, processorId))
                {
                    continue;
                }

                crow::connections::systemBus->async_method_call(
                    [aResp, objPath, processorId,
                     portId](const boost::system::error_code ec,
                             std::variant<std::vector<std::string>>& resp) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error while getting port on processor: {}",
                            ec.message());
                        messages::internalError(aResp->res);
                        return;
                    }

                    std::vector<std::string>* data =
                        std::get_if<std::vector<std::string>>(&resp);
                    if (data == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Null data response while getting port on processor");
                        messages::internalError(aResp->res);
                        return;
                    }

                    for (const std::string& sensorpath : *data)
                    {
                        // Check Interface in Object or not
                        BMCWEB_LOG_DEBUG(
                            "processor state sensor object path {}",
                            sensorpath);
                        sdbusplus::message::object_path path(sensorpath);
                        if (path.filename() != portId)
                        {
                            continue;
                        }

                        std::string histoURI = "/redfish/v1/Systems/";
                        histoURI += std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME);
                        histoURI += "/Processors/";
                        histoURI += processorId;
                        histoURI += "/Ports/";
                        histoURI += portId;
                        histoURI += "/Oem/Nvidia/Histograms";
                        aResp->res.jsonValue["@odata.type"] =
                            "#NvidiaHistogramCollection.NvidiaHistogramCollection";
                        aResp->res.jsonValue["@odata.id"] = histoURI;
                        aResp->res.jsonValue["Name"] = processorId + "_" +
                                                       portId +
                                                       "_Histogram_Collection";

                        collection_util::getCollectionMembersByAssociation(
                            aResp,
                            "/redfish/v1/Systems/" +
                                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                "/Processors/" + processorId + "/Ports/" +
                                portId + "/Oem/Nvidia/Histograms",
                            sensorpath + "/histograms", {});
                        return;
                    }
                    // Couldn't find an object with that name.
                    // Return an error
                    messages::resourceNotFound(aResp->res, "#Port.v1_0_0.Port",
                                               portId);
                },
                    "xyz.openbmc_project.ObjectMapper", objPath + "/all_states",
                    "org.freedesktop.DBus.Properties", "Get",
                    "xyz.openbmc_project.Association", "endpoints");
                return;
            }
            // Object not found
            messages::resourceNotFound(
                aResp->res, "#Processor.v1_20_0.Processor", processorId);
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/inventory", 0,
            std::array<const char*, 1>{
                "xyz.openbmc_project.Inventory.Item.Accelerator"});
    });
}

} // namespace redfish
