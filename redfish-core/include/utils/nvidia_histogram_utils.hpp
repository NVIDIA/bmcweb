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

#include <cstdint>
namespace redfish
{

namespace nvidia_histogram_utils
{
// Map of service name to list of interfaces
using MapperServiceMap =
    std::vector<std::pair<std::string, std::vector<std::string>>>;

// Map of object paths to MapperServiceMaps
using MapperGetSubTreeResponse =
    std::vector<std::pair<std::string, MapperServiceMap>>;

inline std::string getBucketUnit(const std::string& unit)
{
    if (unit == "com.nvidia.Histogram.Decorator.Format.BucketUnits.Watts")
    {
        return "Watts";
    }
    if (unit == "com.nvidia.Histogram.Decorator.Format.BucketUnits.Percent")
    {
        return "HundredthsPercent";
    }
    if (unit == "com.nvidia.Histogram.Decorator.Format.BucketUnits.Count")
    {
        return "Counts";
    }
    // Unknown or others
    return "";
}

inline std::string getBucketDataFormat(const std::string& unit)
{
    if (unit == "com.nvidia.Histogram.Decorator.Format.BucketDataTypes.NvU8")
    {
        return "NvU8";
    }
    if (unit == "com.nvidia.Histogram.Decorator.Format.BucketDataTypes.NvS8")
    {
        return "NvS8";
    }
    if (unit == "com.nvidia.Histogram.Decorator.Format.BucketDataTypes.NvU16")
    {
        return "NvU16";
    }
    if (unit == "com.nvidia.Histogram.Decorator.Format.BucketDataTypes.NvS16")
    {
        return "NvS16";
    }
    if (unit == "com.nvidia.Histogram.Decorator.Format.BucketDataTypes.NvU32")
    {
        return "NvU32";
    }
    if (unit == "com.nvidia.Histogram.Decorator.Format.BucketDataTypes.NvS32")
    {
        return "NvS32";
    }
    if (unit == "com.nvidia.Histogram.Decorator.Format.BucketDataTypes.NvU64")
    {
        return "NvU64";
    }
    if (unit == "com.nvidia.Histogram.Decorator.Format.BucketDataTypes.NvS64")
    {
        return "NvS64";
    }
    if (unit == "com.nvidia.Histogram.Decorator.Format.BucketDataTypes.NvS24_8")
    {
        return "NvS24_8";
    }
    // Unknown or others
    return "";
}

inline void
    getHistogramLink(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& formedURI,
                     const std::string& objectPath,
                     const std::string& odataType)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, formedURI,
         odataType](const boost::system::error_code ec,
                    std::variant<std::vector<std::string>>& resp) {
        if (ec)
        {
            // no associated histograms = no failure
            BMCWEB_LOG_DEBUG("No associated histograms on {}", formedURI);
            return;
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            BMCWEB_LOG_ERROR("Null data error while getting histograms");
            messages::internalError(asyncResp->res);
            return;
        }

        std::string histogramURI = formedURI;
        histogramURI += "/Oem/Nvidia/Histograms";

        asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] = odataType;
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["Histograms"]["@odata.id"] =
            histogramURI;
    },
        "xyz.openbmc_project.ObjectMapper", objectPath + "/histograms",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
    return;
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
                asyncResp->res.jsonValue["BucketUnits"] = getBucketUnit(*value);
            }
            if (propertyName == "BucketDataType")
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
                asyncResp->res.jsonValue["BucketFormat"] =
                    getBucketDataFormat(*value);
            }
            if (propertyName == "MinSamplingTime")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for {} parameter",
                                     propertyName);
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue["UpdatePeriodMilliseconds"] = *value;
            }
            if (propertyName == "IncrementDuration")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
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
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
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
    const std::string& histogramId, const std::string& objPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, histogramId](const boost::system::error_code ec,
                                 std::variant<std::vector<std::string>>& resp) {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "DBUS response error while getting histogram on: {}",
                ec.message());
            messages::internalError(asyncResp->res);
            return;
        }
        std::vector<std::string>* data =
            std::get_if<std::vector<std::string>>(&resp);
        if (data == nullptr)
        {
            BMCWEB_LOG_ERROR("Null data response while getting histogram");
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

                updateHistogramData(asyncResp, object.front().first, histoPath);
            },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetObject", histoPath,
                std::array<const char*, 0>());
            return;
        }
        // Couldn't find an object with that name.
        // Return an error
        messages::resourceNotFound(asyncResp->res,
                                   "#NvidiaHistogram.v1_1_0.NvidiaHistogram",
                                   histogramId);
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/histograms",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

inline void updateHistogramBucketData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get Histogram Bucket Data");
    using PropertiesMap =
        boost::container::flat_map<std::string, dbus::utility::DbusVariantType>;

    crow::connections::systemBus->async_method_call(
        [asyncResp, objPath](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                object) {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "Dbus response error while getting service name for {}: {}",
                objPath, ec.message());
            messages::internalError(asyncResp->res);
            return;
        }
        // Get interface properties
        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code ec,
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
                if (propertyName == "BucketData")
                {
                    const std::vector<std::tuple<
                        uint16_t, std::tuple<double, double, double>>>* value =
                        std::get_if<std::vector<std::tuple<
                            uint16_t, std::tuple<double, double, double>>>>(
                            &property.second);
                    if (value == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Null value returned "
                                         "for {} parameter",
                                         propertyName);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    // populate bucket data arrays
                    for (const auto& [index, data] : *value)
                    {
                        nlohmann::json bucketsArr;
                        const auto& [start, end, value] = data;

                        bucketsArr["BucketId"] = static_cast<uint16_t>(index);
                        bucketsArr["Start"] = static_cast<double>(start);
                        bucketsArr["End"] = static_cast<double>(end);
                        bucketsArr["Value"] = static_cast<uint64_t>(value);

                        nlohmann::json& jRespForBuckets =
                            asyncResp->res.jsonValue["Buckets"];
                        std::string sortField = "BucketId";
                        redfish::nvidia_chassis_utils::insertSorted(
                            jRespForBuckets, bucketsArr, sortField);
                    }
                }
            }
        },
            object.front().first, objPath, "org.freedesktop.DBus.Properties",
            "GetAll", "");
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", objPath,
        std::array<const char*, 0>());
}

} // namespace nvidia_histogram_utils
} // namespace redfish
