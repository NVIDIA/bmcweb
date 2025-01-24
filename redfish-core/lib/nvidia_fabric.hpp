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
#include <utils/systemd_utils.hpp>

#include <array>
#include <cstdint>
#include <string_view>

namespace redfish
{
inline std::string getBucketUnit(const std::string& unit)
{
    if (unit == "com.nvidia.Histogram.Decorator.Format.BucketUnits.Watts")
    {
        return "Watts";
    }
    if (unit == "com.nvidia.Histogram.Decorator.Format.BucketUnits.Percent")
    {
        return "Percent";
    }
    if (unit == "com.nvidia.Histogram.Decorator.Format.BucketUnits.Count")
    {
        return "Count";
    }
    // Unknown or others
    return "Unknown";
}

inline void
    updateHistogramData(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& service, const std::string& objPath)
{
    using PropertiesMap =
        boost::container::flat_map<std::string, dbus::utility::DbusVariantType>;

    // Get interface properties
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath](const boost::system::error_code ec,
                             const PropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Error while GetAll histogram data: {}",
                                 ec.message());
                messages::internalError(asyncResp->res);
                return;
            }

            for (const auto& property : properties)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "UnitOfMeasure")
                {
                    const std::string* value =
                        std::get_if<std::string>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for {} parameter",
                                         propertyName);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["BucketUnits"] =
                        getBucketUnit(*value);
                }
                if (propertyName == "MinSamplingTime")
                {
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for {} parameter",
                                         propertyName);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["UpdatePeriodMilliseconds"] =
                        *value;
                }
                if (propertyName == "IncrementDuration")
                {
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for {} parameter",
                                         propertyName);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["IncrementDurationMicroseconds"] =
                        *value;
                }
                if (propertyName == "AccumulationCycle")
                {
                    const uint64_t* value =
                        std::get_if<uint64_t>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for {} parameter",
                                         propertyName);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["AccumulationCycle"] = *value;
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll", "");
}

inline void getHistogramDataByAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId,
    const std::string& histogramId, const std::string& objPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, fabricId, switchId,
         histogramId](const boost::system::error_code ec,
                      std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error while getting histogram on switch: {}",
                    ec.message());
                messages::internalError(asyncResp->res);
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_ERROR(
                    "Null data response while getting histogram on switch");
                messages::internalError(asyncResp->res);
                return;
            }
            // Iterate over all retrieved ObjectPaths.
            for (const std::string& histoPath : *data)
            {
                sdbusplus::message::object_path histoObjPath(histoPath);
                if (histoObjPath.filename() != histogramId)
                {
                    continue;
                }

                std::string histogramURI = "/redfish/v1/Fabrics/";
                histogramURI += fabricId;
                histogramURI += "/Switches/";
                histogramURI += switchId;
                histogramURI += "/Oem/Nvidia/Histograms/";
                histogramURI += histogramId;
                asyncResp->res.jsonValue["@odata.type"] =
                    "#NvidiaHistogram.v1_0_0.NvidiaHistogram";
                asyncResp->res.jsonValue["@odata.id"] = histogramURI;
                asyncResp->res.jsonValue["Id"] = histogramId;
                asyncResp->res.jsonValue["Name"] =
                    switchId + "_Histogram_" + histogramId;

                std::string bucketURI = histogramURI + "/Buckets";
                asyncResp->res.jsonValue["Buckets"]["@odata.id"] = bucketURI;

                crow::connections::systemBus->async_method_call(
                    [asyncResp, histoPath, histogramId](
                        const boost::system::error_code ec,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& object) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR(
                                "Dbus response error while getting service name for histogram {}: {}",
                                histogramId, ec.message());
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        updateHistogramData(asyncResp, object.front().first,
                                            histoPath);
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetObject", histoPath,
                    std::array<const char*, 0>());
                return;
            }
            // Couldn't find an object with that name.
            // Return an error
            messages::resourceNotFound(
                asyncResp->res, "#NvidiaHistogram.v1_0_0.NvidiaHistogram",
                histogramId);
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/histograms",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void updateHistogramBucketData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Histogram Bucket Data");
    using PropertiesMap =
        boost::container::flat_map<std::string, dbus::utility::DbusVariantType>;
    // Get interface properties
    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath](const boost::system::error_code ec,
                             const PropertiesMap& properties) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Error while GetAll Bucket Data: {}",
                                 ec.message());
                messages::internalError(asyncResp->res);
                return;
            }

            for (const auto& property : properties)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "Start" || propertyName == "Value" ||
                    propertyName == "End")
                {
                    const double* value = std::get_if<double>(&property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for {} parameter",
                                         propertyName);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue[propertyName] = *value;
                }
            }
        },
        service, objPath, "org.freedesktop.DBus.Properties", "GetAll", "");
}

inline void getBucketDataByAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& fabricId, const std::string& switchId,
    const std::string& histogramId, const std::string& bucketId,
    const std::string& objPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, fabricId, switchId, histogramId,
         bucketId](const boost::system::error_code ec,
                   std::variant<std::vector<std::string>>& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error while getting bucket on histogram: {}",
                    ec.message());
                messages::internalError(asyncResp->res);
                return;
            }
            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                BMCWEB_LOG_ERROR(
                    "Null data response while getting bucket on histogram");
                messages::internalError(asyncResp->res);
                return;
            }
            // Iterate over all retrieved ObjectPaths.
            for (const std::string& bucketPath : *data)
            {
                sdbusplus::message::object_path objPath(bucketPath);
                if (objPath.filename() != bucketId)
                {
                    continue;
                }

                std::string bucketURI = "/redfish/v1/Fabrics/";
                bucketURI += fabricId;
                bucketURI += "/Switches/";
                bucketURI += switchId;
                bucketURI += "/Oem/Nvidia/Histograms/";
                bucketURI += histogramId;
                bucketURI += "/Buckets/";
                bucketURI += bucketId;
                asyncResp->res.jsonValue["@odata.type"] =
                    "#NvidiaHistogramBucket.v1_0_0.NvidiaHistogramBucket";
                asyncResp->res.jsonValue["@odata.id"] = bucketURI;
                asyncResp->res.jsonValue["Id"] = bucketId;
                asyncResp->res.jsonValue["Name"] =
                    switchId + "_Histogram_" + histogramId + "_Bucket_" +
                    bucketId;

                crow::connections::systemBus->async_method_call(
                    [asyncResp, bucketPath, bucketId](
                        const boost::system::error_code ec,
                        const std::vector<std::pair<
                            std::string, std::vector<std::string>>>& object) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR(
                                "Dbus response error while getting service name for bucket {}: {}",
                                bucketId, ec.message());
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        updateHistogramBucketData(
                            asyncResp, object.front().first, bucketPath);
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetObject", bucketPath,
                    std::array<const char*, 0>());
                return;
            }
            // Couldn't find an object with that name.
            // Return an error
            messages::resourceNotFound(
                asyncResp->res,
                "#NvidiaHistogramBucket.v1_0_0.NvidiaHistogramBucket",
                bucketId);
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/histogram_buckets",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void requestRoutesSwitchHistogramBucket(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/Histograms/<str>/Buckets/<str>")
        .privileges(redfish::privileges::getSwitch)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& fabricId,
                            const std::string& switchId,
                            const std::string& histogramId,
                            const std::string& bucketId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp, fabricId, switchId, histogramId,
                 bucketId](const boost::system::error_code ec,
                           const std::vector<std::string>& objects) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error while getting fabrics: {}",
                            ec.message());
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (const std::string& fabricObject : objects)
                    {
                        // Get the fabricId object
                        if (!boost::ends_with(fabricObject, fabricId))
                        {
                            continue;
                        }
                        crow::connections::systemBus->async_method_call(
                            [asyncResp, fabricId, switchId, histogramId,
                             bucketId](
                                const boost::system::error_code ec,
                                std::variant<std::vector<std::string>>& resp) {
                                if (ec)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "DBUS response error while getting switch on fabric: {}",
                                        ec.message());
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                std::vector<std::string>* data =
                                    std::get_if<std::vector<std::string>>(
                                        &resp);
                                if (data == nullptr)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "Null data response while getting switch on fabric");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                // Iterate over all retrieved ObjectPaths.
                                for (const std::string& switchPath : *data)
                                {
                                    sdbusplus::message::object_path objPath(
                                        switchPath);
                                    if (objPath.filename() != switchId)
                                    {
                                        continue;
                                    }
                                    crow::connections::systemBus->async_method_call(
                                        [asyncResp, fabricId, switchId,
                                         histogramId, bucketId](
                                            const boost::system::error_code ec,
                                            std::variant<std::vector<
                                                std::string>>& resp) {
                                            if (ec)
                                            {
                                                BMCWEB_LOG_ERROR(
                                                    "DBUS response error while getting histogram on switch: {}",
                                                    ec.message());
                                                messages::internalError(
                                                    asyncResp->res);
                                                return;
                                            }
                                            std::vector<std::string>* data =
                                                std::get_if<
                                                    std::vector<std::string>>(
                                                    &resp);
                                            if (data == nullptr)
                                            {
                                                BMCWEB_LOG_ERROR(
                                                    "Null data response while getting histogram on switch");
                                                messages::internalError(
                                                    asyncResp->res);
                                                return;
                                            }
                                            // Iterate over all retrieved
                                            // ObjectPaths.
                                            for (const std::string& histoPath :
                                                 *data)
                                            {
                                                sdbusplus::message::object_path
                                                    histoObjPath(histoPath);
                                                if (histoObjPath.filename() !=
                                                    histogramId)
                                                {
                                                    continue;
                                                }

                                                getBucketDataByAssociation(
                                                    asyncResp, fabricId,
                                                    switchId, histogramId,
                                                    bucketId, histoPath);
                                            }
                                        },
                                        "xyz.openbmc_project.ObjectMapper",
                                        switchPath + "/histograms",
                                        "org.freedesktop.DBus.Properties",
                                        "Get",
                                        "xyz.openbmc_project.Association",
                                        "endpoints");
                                    return;
                                }
                                // Couldn't find an object with that name.
                                // Return an error
                                messages::resourceNotFound(
                                    asyncResp->res, "#Switch.v1_8_0.Switch",
                                    switchId);
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            fabricObject + "/all_switches",
                            "org.freedesktop.DBus.Properties", "Get",
                            "xyz.openbmc_project.Association", "endpoints");
                        return;
                    }
                    // Couldn't find an object with that name. Return an error
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

inline void requestRoutesSwitchHistogramBucketCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/Histograms/<str>/Buckets")
        .privileges(redfish::privileges::getSwitch)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& fabricId,
                            const std::string& switchId,
                            const std::string& histogramId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp, fabricId, switchId,
                 histogramId](const boost::system::error_code ec,
                              const std::vector<std::string>& objects) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error while getting fabrics: {}",
                            ec.message());
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (const std::string& fabricObject : objects)
                    {
                        // Get the fabricId object
                        if (!boost::ends_with(fabricObject, fabricId))
                        {
                            continue;
                        }
                        crow::connections::systemBus->async_method_call(
                            [asyncResp, fabricId, switchId, histogramId](
                                const boost::system::error_code ec,
                                std::variant<std::vector<std::string>>& resp) {
                                if (ec)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "DBUS response error while getting switch on fabric: {}",
                                        ec.message());
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                std::vector<std::string>* data =
                                    std::get_if<std::vector<std::string>>(
                                        &resp);
                                if (data == nullptr)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "Null data response while getting switch on fabric");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                // Iterate over all retrieved ObjectPaths.
                                for (const std::string& switchPath : *data)
                                {
                                    sdbusplus::message::object_path objPath(
                                        switchPath);
                                    if (objPath.filename() != switchId)
                                    {
                                        continue;
                                    }
                                    crow::connections::systemBus->async_method_call(
                                        [asyncResp, fabricId, switchId,
                                         histogramId](
                                            const boost::system::error_code ec,
                                            std::variant<std::vector<
                                                std::string>>& resp) {
                                            if (ec)
                                            {
                                                BMCWEB_LOG_ERROR(
                                                    "DBUS response error while getting switch on fabric: {}",
                                                    ec.message());
                                                messages::internalError(
                                                    asyncResp->res);
                                                return;
                                            }
                                            std::vector<std::string>* data =
                                                std::get_if<
                                                    std::vector<std::string>>(
                                                    &resp);
                                            if (data == nullptr)
                                            {
                                                BMCWEB_LOG_ERROR(
                                                    "Null data response while getting switch on fabric");
                                                messages::internalError(
                                                    asyncResp->res);
                                                return;
                                            }
                                            // Iterate over all retrieved
                                            // ObjectPaths.
                                            for (const std::string& histoPath :
                                                 *data)
                                            {
                                                sdbusplus::message::object_path
                                                    histoObjPath(histoPath);
                                                if (histoObjPath.filename() !=
                                                    histogramId)
                                                {
                                                    continue;
                                                }

                                                std::string bucketURI =
                                                    "/redfish/v1/Fabrics/";
                                                bucketURI += fabricId;
                                                bucketURI += "/Switches/";
                                                bucketURI += switchId;
                                                bucketURI +=
                                                    "/Oem/Nvidia/Histograms/";
                                                bucketURI += histogramId;
                                                bucketURI += "/Buckets/";
                                                asyncResp->res
                                                    .jsonValue["@odata.type"] =
                                                    "#NvidiaHistogramBucketCollection.NvidiaHistogramBucketCollection";
                                                asyncResp->res
                                                    .jsonValue["@odata.id"] =
                                                    bucketURI;
                                                asyncResp->res
                                                    .jsonValue["Name"] =
                                                    switchId + "_Histogram_" +
                                                    histogramId +
                                                    "_Bucket_Collection";

                                                collection_util::
                                                    getCollectionMembersByAssociation(
                                                        asyncResp,
                                                        "/redfish/v1/Fabrics/" +
                                                            fabricId +
                                                            "/Switches/" +
                                                            switchId +
                                                            "/Oem/Nvidia/Histograms/" +
                                                            histogramId +
                                                            "/Buckets",
                                                        histoPath +
                                                            "/histogram_buckets",
                                                        {"com.nvidia.Histogram.BucketInfo"});
                                            }
                                        },
                                        "xyz.openbmc_project.ObjectMapper",
                                        switchPath + "/histograms",
                                        "org.freedesktop.DBus.Properties",
                                        "Get",
                                        "xyz.openbmc_project.Association",
                                        "endpoints");
                                    return;
                                }
                                // Couldn't find an object with that name.
                                // Return an error
                                messages::resourceNotFound(
                                    asyncResp->res, "#Switch.v1_8_0.Switch",
                                    switchId);
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            fabricObject + "/all_switches",
                            "org.freedesktop.DBus.Properties", "Get",
                            "xyz.openbmc_project.Association", "endpoints");
                        return;
                    }
                    // Couldn't find an object with that name. Return an error
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

inline void requestRoutesSwitchHistogram(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/Histograms/<str>")
        .privileges(redfish::privileges::getSwitch)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& fabricId,
                            const std::string& switchId,
                            const std::string& histogramId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp, fabricId, switchId,
                 histogramId](const boost::system::error_code ec,
                              const std::vector<std::string>& objects) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error while getting fabrics: {}",
                            ec.message());
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (const std::string& fabricObject : objects)
                    {
                        // Get the fabricId object
                        if (!boost::ends_with(fabricObject, fabricId))
                        {
                            continue;
                        }
                        crow::connections::systemBus->async_method_call(
                            [asyncResp, fabricId, switchId, histogramId](
                                const boost::system::error_code ec,
                                std::variant<std::vector<std::string>>& resp) {
                                if (ec)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "DBUS response error while getting switch on fabric: {}",
                                        ec.message());
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                std::vector<std::string>* data =
                                    std::get_if<std::vector<std::string>>(
                                        &resp);
                                if (data == nullptr)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "Null data response while getting switch on fabric");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                // Iterate over all retrieved ObjectPaths.
                                for (const std::string& switchPath : *data)
                                {
                                    sdbusplus::message::object_path
                                        switchObjPath(switchPath);
                                    if (switchObjPath.filename() != switchId)
                                    {
                                        continue;
                                    }
                                    getHistogramDataByAssociation(
                                        asyncResp, fabricId, switchId,
                                        histogramId, switchPath);
                                    return;
                                }
                                // Couldn't find an object with that name.
                                // Return an error
                                messages::resourceNotFound(
                                    asyncResp->res, "#Switch.v1_8_0.Switch",
                                    switchId);
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            fabricObject + "/all_switches",
                            "org.freedesktop.DBus.Properties", "Get",
                            "xyz.openbmc_project.Association", "endpoints");
                        return;
                    }
                    // Couldn't find an object with that name. Return an error
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

inline void requestRoutesSwitchHistogramCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */

    BMCWEB_ROUTE(
        app, "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/Histograms")
        .privileges(redfish::privileges::getSwitch)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& fabricId,
                            const std::string& switchId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp, fabricId,
                 switchId](const boost::system::error_code ec,
                           const std::vector<std::string>& objects) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error while getting fabrics: {}",
                            ec.message());
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (const std::string& fabricObject : objects)
                    {
                        // Get the fabricId object
                        if (!boost::ends_with(fabricObject, fabricId))
                        {
                            continue;
                        }
                        crow::connections::systemBus->async_method_call(
                            [asyncResp, fabricId, switchId](
                                const boost::system::error_code ec,
                                std::variant<std::vector<std::string>>& resp) {
                                if (ec)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "DBUS response error while getting switch on fabric: {}",
                                        ec.message());
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                std::vector<std::string>* data =
                                    std::get_if<std::vector<std::string>>(
                                        &resp);
                                if (data == nullptr)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "Null data response while getting switch on fabric");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                // Iterate over all retrieved ObjectPaths.
                                for (const std::string& switchPath : *data)
                                {
                                    sdbusplus::message::object_path
                                        switchObjPath(switchPath);
                                    if (switchObjPath.filename() != switchId)
                                    {
                                        continue;
                                    }
                                    std::string histoURI =
                                        "/redfish/v1/Fabrics/";
                                    histoURI += fabricId;
                                    histoURI += "/Switches/";
                                    histoURI += switchId;
                                    histoURI += "/Oem/Nvidia/Histograms/";
                                    asyncResp->res.jsonValue["@odata.type"] =
                                        "#NvidiaHistogramCollection.NvidiaHistogramCollection";
                                    asyncResp->res.jsonValue["@odata.id"] =
                                        histoURI;
                                    asyncResp->res.jsonValue["Name"] =
                                        switchId + "_Histogram_Collection";

                                    collection_util::
                                        getCollectionMembersByAssociation(
                                            asyncResp,
                                            "/redfish/v1/Fabrics/" + fabricId +
                                                "/Switches/" + switchId +
                                                "/Oem/Nvidia/Histograms",
                                            switchPath + "/histograms", {});
                                    return;
                                }
                                // Couldn't find an object with that name.
                                // Return an error
                                messages::resourceNotFound(
                                    asyncResp->res, "#Switch.v1_8_0.Switch",
                                    switchId);
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            fabricObject + "/all_switches",
                            "org.freedesktop.DBus.Properties", "Get",
                            "xyz.openbmc_project.Association", "endpoints");
                        return;
                    }
                    // Couldn't find an object with that name. Return an error
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
