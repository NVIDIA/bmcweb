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

inline void requestRoutesSwitchHistogramBuckets(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Fabrics/<str>/Switches/<str>/Oem/Nvidia/Histograms/<str>/Buckets")
        .privileges(redfish::privileges::getSwitch)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& fabricId, const std::string& switchId,
                   const std::string& histogramId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                dbus::utility::getSubTreePaths(
                    "/xyz/openbmc_project/inventory", 0,
                    std::array<std::string_view, 1>{
                        "xyz.openbmc_project.Inventory.Item.Fabric"},
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
                            if (!fabricObject.ends_with(fabricId))
                            {
                                continue;
                            }
                            dbus::utility::getProperty<
                                std::vector<
                                    std::string>>("xyz.openbmc_project.ObjectMapper",
                                                  fabricObject +
                                                      "/all_switches",
                                                  "xyz.openbmc_project.Association",
                                                  "endpoints",
                                                  [asyncResp, fabricId,
                                                   switchId, histogramId](
                                                      const boost::system::
                                                          error_code ec1,
                                                      const std::vector<
                                                          std::string>& resp2) {
                                                      if (ec1)
                                                      {
                                                          BMCWEB_LOG_ERROR(
                                                              "DBUS response error while getting switch on fabric: {}",
                                                              ec1.message());
                                                          messages::
                                                              internalError(
                                                                  asyncResp
                                                                      ->res);
                                                          return;
                                                      }

                                                      // Iterate over all
                                                      // retrieved ObjectPaths.
                                                      for (const std::string&
                                                               switchPath :
                                                           resp2)
                                                      {
                                                          sdbusplus::object_path
                                                              objPath(
                                                                  switchPath);
                                                          if (objPath
                                                                  .filename() !=
                                                              switchId)
                                                          {
                                                              continue;
                                                          }
                                                          dbus::utility::getProperty<
                                                              std::vector<
                                                                  std::string>>("xyz.openbmc_project.ObjectMapper",
                                                                                switchPath +
                                                                                    "/histograms",
                                                                                "xyz.openbmc_project.Association",
                                                                                "endpoints",
                                                                                [asyncResp,
                                                                                 fabricId,
                                                                                 switchId,
                                                                                 histogramId](
                                                                                    const boost::
                                                                                        system::error_code
                                                                                            ec2,
                                                                                    const std::
                                                                                        vector<std::
                                                                                                   string>& resp3) {
                                                                                    if (ec2)
                                                                                    {
                                                                                        BMCWEB_LOG_ERROR(
                                                                                            "DBUS response error while getting switch on fabric: {}",
                                                                                            ec2.message());
                                                                                        messages::internalError(
                                                                                            asyncResp
                                                                                                ->res);
                                                                                        return;
                                                                                    }
                                                                                    // Iterate over all retrieved
                                                                                    // ObjectPaths.
                                                                                    for (
                                                                                        const std::
                                                                                            string&
                                                                                                histoPath :
                                                                                        resp3)
                                                                                    {
                                                                                        sdbusplus::object_path
                                                                                            histoObjPath(
                                                                                                histoPath);
                                                                                        if (histoObjPath
                                                                                                .filename() !=
                                                                                            histogramId)
                                                                                        {
                                                                                            continue;
                                                                                        }

                                                                                        std::string
                                                                                            bucketURI =
                                                                                                "/redfish/v1/Fabrics/";
                                                                                        bucketURI +=
                                                                                            fabricId;
                                                                                        bucketURI +=
                                                                                            "/Switches/";
                                                                                        bucketURI +=
                                                                                            switchId;
                                                                                        bucketURI +=
                                                                                            "/Oem/Nvidia/Histograms/";
                                                                                        bucketURI +=
                                                                                            histogramId;
                                                                                        bucketURI +=
                                                                                            "/Buckets";
                                                                                        asyncResp
                                                                                            ->res
                                                                                            .jsonValue
                                                                                                ["@odata.type"] =
                                                                                            "#NvidiaHistogramBuckets.v1_0_0.NvidiaHistogramBuckets";
                                                                                        asyncResp
                                                                                            ->res
                                                                                            .jsonValue
                                                                                                ["@odata.id"] =
                                                                                            bucketURI;
                                                                                        std::string
                                                                                            name =
                                                                                                switchId;
                                                                                        name +=
                                                                                            "_Histogram_";
                                                                                        name +=
                                                                                            histogramId;
                                                                                        name +=
                                                                                            "_Buckets";
                                                                                        asyncResp
                                                                                            ->res
                                                                                            .jsonValue
                                                                                                ["Name"] =
                                                                                            name;
                                                                                        asyncResp
                                                                                            ->res
                                                                                            .jsonValue
                                                                                                ["Id"] =
                                                                                            "Buckets";
                                                                                        asyncResp
                                                                                            ->res
                                                                                            .jsonValue
                                                                                                ["Buckets"] =
                                                                                            nlohmann::json::
                                                                                                array();

                                                                                        redfish::nvidia_histogram_utils::
                                                                                            updateHistogramBucketData(
                                                                                                asyncResp,
                                                                                                histoPath);
                                                                                    }
                                                                                });
                                                          return;
                                                      }
                                                      // Couldn't find an object
                                                      // with that name. Return
                                                      // an error
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
                    });
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

            dbus::utility::getSubTreePaths(
                "/xyz/openbmc_project/inventory", 0,
                std::array<std::string_view, 1>{
                    "xyz.openbmc_project.Inventory.Item.Fabric"},
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
                        if (!fabricObject.ends_with(fabricId))
                        {
                            continue;
                        }
                        dbus::utility::getProperty<std::vector<std::string>>(
                            "xyz.openbmc_project.ObjectMapper",
                            fabricObject + "/all_switches",
                            "xyz.openbmc_project.Association", "endpoints",
                            [asyncResp, fabricId, switchId, histogramId](
                                const boost::system::error_code ec1,
                                const std::vector<std::string>& resp) {
                                if (ec1)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "DBUS response error while getting switch on fabric: {}",
                                        ec1.message());
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                // Iterate over all retrieved ObjectPaths.
                                for (const std::string& switchPath : resp)
                                {
                                    sdbusplus::object_path switchObjPath(
                                        switchPath);
                                    if (switchObjPath.filename() != switchId)
                                    {
                                        continue;
                                    }

                                    std::string histogramURI =
                                        "/redfish/v1/Fabrics/";
                                    histogramURI += fabricId;
                                    histogramURI += "/Switches/";
                                    histogramURI += switchId;
                                    histogramURI += "/Oem/Nvidia/Histograms/";
                                    histogramURI += histogramId;
                                    asyncResp->res.jsonValue["@odata.type"] =
                                        "#NvidiaHistogram.v1_1_0.NvidiaHistogram";
                                    asyncResp->res.jsonValue["@odata.id"] =
                                        histogramURI;
                                    asyncResp->res.jsonValue["Id"] =
                                        histogramId;
                                    std::string name2 = switchId;
                                    name2 += "_Histogram_";
                                    name2 += histogramId;
                                    asyncResp->res.jsonValue["Name"] = name2;

                                    std::string bucketURI = histogramURI;
                                    bucketURI += "/Buckets";
                                    asyncResp->res.jsonValue["HistogramBuckets"]
                                                            ["@odata.id"] =
                                        bucketURI;

                                    redfish::nvidia_histogram_utils::
                                        getHistogramDataByAssociation(
                                            asyncResp, histogramId, switchPath);
                                    return;
                                }
                                // Couldn't find an object with that name.
                                // Return an error
                                messages::resourceNotFound(
                                    asyncResp->res, "#Switch.v1_8_0.Switch",
                                    switchId);
                            });
                        return;
                    }
                    // Couldn't find an object with that name. Return an error
                    messages::resourceNotFound(
                        asyncResp->res, "#Fabric.v1_2_0.Fabric", fabricId);
                });
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

            dbus::utility::getSubTreePaths(
                "/xyz/openbmc_project/inventory", 0,
                std::array<std::string_view, 1>{
                    "xyz.openbmc_project.Inventory.Item.Fabric"},
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
                        if (!fabricObject.ends_with(fabricId))
                        {
                            continue;
                        }
                        dbus::utility::getProperty<std::vector<std::string>>(
                            "xyz.openbmc_project.ObjectMapper",
                            fabricObject + "/all_switches",
                            "xyz.openbmc_project.Association", "endpoints",
                            [asyncResp, fabricId,
                             switchId](const boost::system::error_code ec2,
                                       const std::vector<std::string>& resp) {
                                if (ec2)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "DBUS response error while getting switch on fabric: {}",
                                        ec2.message());
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                // Iterate over all retrieved ObjectPaths.
                                for (const std::string& switchPath : resp)
                                {
                                    sdbusplus::object_path switchObjPath(
                                        switchPath);
                                    if (switchObjPath.filename() != switchId)
                                    {
                                        continue;
                                    }
                                    std::string histoURI =
                                        "/redfish/v1/Fabrics/";
                                    histoURI += fabricId;
                                    histoURI += "/Switches/";
                                    histoURI += switchId;
                                    histoURI += "/Oem/Nvidia/Histograms";
                                    asyncResp->res.jsonValue["@odata.type"] =
                                        "#NvidiaHistogramCollection.NvidiaHistogramCollection";
                                    asyncResp->res.jsonValue["@odata.id"] =
                                        histoURI;
                                    asyncResp->res.jsonValue["Name"] =
                                        switchId + "_Histogram_Collection";

                                    std::string collectionUri =
                                        "/redfish/v1/Fabrics/";
                                    collectionUri += fabricId;
                                    collectionUri += "/Switches/";
                                    collectionUri += switchId;
                                    collectionUri += "/Oem/Nvidia/Histograms";
                                    collection_util::
                                        getCollectionMembersByAssociation(
                                            asyncResp, collectionUri,
                                            switchPath + "/histograms", {});
                                    return;
                                }
                                // Couldn't find an object with that name.
                                // Return an error
                                messages::resourceNotFound(
                                    asyncResp->res, "#Switch.v1_8_0.Switch",
                                    switchId);
                            });
                        return;
                    }
                    // Couldn't find an object with that name. Return an error
                    messages::resourceNotFound(
                        asyncResp->res, "#Fabric.v1_2_0.Fabric", fabricId);
                });
        });
}

inline void requestRoutesSwitchPortHistogramBuckets(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Fabrics/<str>/Switches/<str>/Ports/<str>/Oem/Nvidia/Histograms/<str>/Buckets")
        .privileges(redfish::privileges::getSwitch)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& fabricId,
                            const std::string& switchId,
                            const std::string& portId,
                            const std::string& histogramId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            dbus::utility::getSubTreePaths(
                "/xyz/openbmc_project/inventory", 0,
                std::array<std::string_view, 1>{
                    "xyz.openbmc_project.Inventory.Item.Fabric"},
                [asyncResp, fabricId, switchId, portId,
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
                        if (!fabricObject.ends_with(fabricId))
                        {
                            continue;
                        }
                        dbus::utility::getProperty<std::vector<std::string>>(
                            "xyz.openbmc_project.ObjectMapper",
                            fabricObject + "/all_switches",
                            "xyz.openbmc_project.Association", "endpoints",
                            [asyncResp, fabricId, switchId, portId,
                             histogramId](
                                const boost::system::error_code ec1,
                                const std::vector<std::string>& resp3) {
                                if (ec1)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "DBUS response error while getting switch on fabric: {}",
                                        ec1.message());
                                    messages::internalError(asyncResp->res);
                                    return;
                                }

                                // Iterate over all
                                // retrieved
                                // ObjectPaths.
                                for (const std::string& switchPath : resp3)
                                {
                                    sdbusplus::object_path switchObjPath(
                                        switchPath);
                                    if (switchObjPath.filename() != switchId)
                                    {
                                        continue;
                                    }
                                    dbus::utility::getProperty<std::vector<
                                        std::
                                            string>>("xyz.openbmc_project.ObjectMapper",
                                                     switchPath + "/all_states",
                                                     "xyz.openbmc_project.Association",
                                                     "endpoints",
                                                     [asyncResp, fabricId,
                                                      switchId, portId,
                                                      histogramId](
                                                         const boost::system::
                                                             error_code ec2,
                                                         const std::vector<
                                                             std::string>&
                                                             resp4) {
                                                         if (ec2)
                                                         {
                                                             BMCWEB_LOG_ERROR(
                                                                 "DBUS response error while getting port on switch: {}",
                                                                 ec2.message());
                                                             messages::
                                                                 internalError(
                                                                     asyncResp
                                                                         ->res);
                                                             return;
                                                         }

                                                         // Iterate over all
                                                         // retrieved
                                                         // ObjectPaths.
                                                         for (const std::string&
                                                                  portPath :
                                                              resp4)
                                                         {
                                                             sdbusplus::object_path
                                                                 switchPortObjPath(
                                                                     portPath);
                                                             if (switchPortObjPath
                                                                     .filename() !=
                                                                 portId)
                                                             {
                                                                 continue;
                                                             }

                                                             dbus::utility::getProperty<std::vector<
                                                                 std::string>>("xyz.openbmc_project.ObjectMapper",
                                                                               portPath +
                                                                                   "/histograms",
                                                                               "xyz.openbmc_project.Association",
                                                                               "endpoints",
                                                                               [asyncResp,
                                                                                fabricId,
                                                                                switchId,
                                                                                portId,
                                                                                histogramId](
                                                                                   const boost::
                                                                                       system::error_code
                                                                                           ec3,
                                                                                   const std::vector<
                                                                                       std::
                                                                                           string>&
                                                                                       resp5) {
                                                                                   if (ec3)
                                                                                   {
                                                                                       BMCWEB_LOG_ERROR(
                                                                                           "DBUS response error while getting switch on fabric: {}",
                                                                                           ec3.message());
                                                                                       messages::internalError(
                                                                                           asyncResp
                                                                                               ->res);
                                                                                       return;
                                                                                   }

                                                                                   // Iterate over all retrieved ObjectPaths.
                                                                                   for (
                                                                                       const std::
                                                                                           string&
                                                                                               histoPath :
                                                                                       resp5)
                                                                                   {
                                                                                       sdbusplus::object_path
                                                                                           histoObjPath(
                                                                                               histoPath);
                                                                                       if (histoObjPath
                                                                                               .filename() !=
                                                                                           histogramId)
                                                                                       {
                                                                                           continue;
                                                                                       }

                                                                                       std::string
                                                                                           bucketURI =
                                                                                               "/redfish/v1/Fabrics/";
                                                                                       bucketURI +=
                                                                                           fabricId;
                                                                                       bucketURI +=
                                                                                           "/Switches/";
                                                                                       bucketURI +=
                                                                                           switchId;
                                                                                       bucketURI +=
                                                                                           "/Ports/";
                                                                                       bucketURI +=
                                                                                           portId;
                                                                                       bucketURI +=
                                                                                           "/Oem/Nvidia/Histograms/";
                                                                                       bucketURI +=
                                                                                           histogramId;
                                                                                       bucketURI +=
                                                                                           "/Buckets";
                                                                                       asyncResp
                                                                                           ->res
                                                                                           .jsonValue
                                                                                               ["@odata.type"] =
                                                                                           "#NvidiaHistogramBuckets.v1_0_0.NvidiaHistogramBuckets";
                                                                                       asyncResp
                                                                                           ->res
                                                                                           .jsonValue
                                                                                               ["@odata.id"] =
                                                                                           bucketURI;
                                                                                       std::string
                                                                                           name3 =
                                                                                               switchId;
                                                                                       name3 +=
                                                                                           "_";
                                                                                       name3 +=
                                                                                           portId;
                                                                                       name3 +=
                                                                                           "_Histogram_";
                                                                                       name3 +=
                                                                                           histogramId;
                                                                                       name3 +=
                                                                                           "_Buckets";
                                                                                       asyncResp
                                                                                           ->res
                                                                                           .jsonValue
                                                                                               ["Name"] =
                                                                                           name3;
                                                                                       asyncResp
                                                                                           ->res
                                                                                           .jsonValue
                                                                                               ["Id"] =
                                                                                           "Buckets";
                                                                                       asyncResp
                                                                                           ->res
                                                                                           .jsonValue
                                                                                               ["Buckets"] =
                                                                                           nlohmann::json::
                                                                                               array();

                                                                                       redfish::nvidia_histogram_utils::
                                                                                           updateHistogramBucketData(
                                                                                               asyncResp,
                                                                                               histoPath);
                                                                                   }
                                                                               });
                                                             return;
                                                         }
                                                     });
                                    return;
                                }
                                // Couldn't find an
                                // object with that
                                // name. Return an
                                // error
                                messages::resourceNotFound(
                                    asyncResp->res, "#Switch.v1_8_0.Switch",
                                    switchId);
                            });
                        return;
                    }
                    // Couldn't find an object with that name. Return an
                    // error
                    messages::resourceNotFound(
                        asyncResp->res, "#Fabric.v1_2_0.Fabric", fabricId);
                });
        });
}

inline void requestRoutesSwitchPortHistogram(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Fabrics/<str>/Switches/<str>/Ports/<str>/Oem/Nvidia/Histograms/<str>")
        .privileges(redfish::privileges::getSwitch)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& fabricId,
                            const std::string& switchId,
                            const std::string& portId,
                            const std::string& histogramId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            dbus::utility::getSubTreePaths(
                "/xyz/openbmc_project/inventory", 0,
                std::array<std::string_view, 1>{
                    "xyz.openbmc_project.Inventory.Item.Fabric"},
                [asyncResp, fabricId, switchId, portId,
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
                        if (!fabricObject.ends_with(fabricId))
                        {
                            continue;
                        }
                        dbus::utility::getProperty<std::vector<std::string>>(
                            "xyz.openbmc_project.ObjectMapper",
                            fabricObject + "/all_switches",
                            "xyz.openbmc_project.Association", "endpoints",
                            [asyncResp, fabricId, switchId, portId,
                             histogramId](
                                const boost::system::error_code ec2,
                                const std::vector<std::string>& resp) {
                                if (ec2)
                                {
                                    BMCWEB_LOG_ERROR(
                                        "DBUS response error while getting switch on fabric: {}",
                                        ec2.message());
                                    messages::internalError(asyncResp->res);
                                    return;
                                }

                                // Iterate over all
                                // retrieved ObjectPaths.
                                for (const std::string& switchPath : resp)
                                {
                                    sdbusplus::object_path switchObjPath(
                                        switchPath);
                                    if (switchObjPath.filename() != switchId)
                                    {
                                        continue;
                                    }
                                    dbus::utility::getProperty<
                                        std::vector<std::string>>(
                                        "xyz.openbmc_project.ObjectMapper",
                                        switchPath + "/all_states",
                                        "xyz.openbmc_project.Association",
                                        "endpoints",
                                        [asyncResp, fabricId, switchId, portId,
                                         histogramId](
                                            const boost::system::error_code ec3,
                                            const std::vector<std::string>&
                                                portResp) {
                                            if (ec3)
                                            {
                                                BMCWEB_LOG_ERROR(
                                                    "DBUS response error while getting port on switch: {}",
                                                    ec3.message());
                                                messages::internalError(
                                                    asyncResp->res);
                                                return;
                                            }
                                            // Iterate over all retrieved
                                            // ObjectPaths.
                                            for (const std::string& portPath :
                                                 portResp)
                                            {
                                                sdbusplus::object_path
                                                    switchPortObjPath(portPath);
                                                if (switchPortObjPath
                                                        .filename() != portId)
                                                {
                                                    continue;
                                                }

                                                std::string histogramURI =
                                                    "/redfish/v1/Fabrics/";
                                                histogramURI += fabricId;
                                                histogramURI += "/Switches/";
                                                histogramURI += switchId;
                                                histogramURI += "/Ports/";
                                                histogramURI += portId;
                                                histogramURI +=
                                                    "/Oem/Nvidia/Histograms/";
                                                histogramURI += histogramId;
                                                asyncResp->res
                                                    .jsonValue["@odata.type"] =
                                                    "#NvidiaHistogram.v1_1_0.NvidiaHistogram";
                                                asyncResp->res
                                                    .jsonValue["@odata.id"] =
                                                    histogramURI;
                                                asyncResp->res.jsonValue["Id"] =
                                                    histogramId;
                                                std::string name4 = switchId;
                                                name4 += "_";
                                                name4 += portId;
                                                name4 += "_Histogram_";
                                                name4 += histogramId;
                                                asyncResp->res
                                                    .jsonValue["Name"] = name4;

                                                std::string bucketURI =
                                                    histogramURI;
                                                bucketURI += "/Buckets";
                                                asyncResp->res.jsonValue
                                                    ["HistogramBuckets"]
                                                    ["@odata.id"] = bucketURI;
                                                redfish::nvidia_histogram_utils::
                                                    getHistogramDataByAssociation(
                                                        asyncResp, histogramId,
                                                        portPath);
                                                return;
                                            }
                                        });
                                    return;
                                }
                                // Couldn't find an object
                                // with that name. Return an
                                // error
                                messages::resourceNotFound(
                                    asyncResp->res, "#Switch.v1_8_0.Switch",
                                    switchId);
                            });
                        return;
                    }
                    // Couldn't find an object with that name.
                    // Return an error
                    messages::resourceNotFound(
                        asyncResp->res, "#Fabric.v1_2_0.Fabric", fabricId);
                });
        });
}

inline void requestRoutesSwitchPortHistogramCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Fabrics/<str>/Switches/<str>/Ports/<str>/Oem/Nvidia/Histograms")
        .privileges(redfish::privileges::getSwitch)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& fabricId,
                            const std::string& switchId,
                            const std::string& portId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            dbus::utility::getSubTreePaths(
                "/xyz/openbmc_project/inventory", 0,
                std::array<std::string_view, 1>{
                    "xyz.openbmc_project.Inventory.Item.Fabric"},
                [asyncResp, fabricId, switchId,
                 portId](const boost::system::error_code ec,
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
                        if (!fabricObject.ends_with(fabricId))
                        {
                            continue;
                        }
                        dbus::utility::getProperty<
                            std::
                                vector<std::string>>("xyz.openbmc_project.ObjectMapper",
                                                     fabricObject +
                                                         "/all_switches",
                                                     "xyz.openbmc_project.Association",
                                                     "endpoints",
                                                     [asyncResp, fabricId,
                                                      switchId, portId](
                                                         const boost::system::
                                                             error_code ec2,
                                                         const std::vector<
                                                             std::string>&
                                                             resp) {
                                                         if (ec2)
                                                         {
                                                             BMCWEB_LOG_ERROR(
                                                                 "DBUS response error while getting switch on fabric: {}",
                                                                 ec2.message());
                                                             messages::
                                                                 internalError(
                                                                     asyncResp
                                                                         ->res);
                                                             return;
                                                         }

                                                         // Iterate over all
                                                         // retrieved
                                                         // ObjectPaths.
                                                         for (const std::string&
                                                                  switchPath :
                                                              resp)
                                                         {
                                                             sdbusplus::object_path
                                                                 switchObjPath(
                                                                     switchPath);
                                                             if (switchObjPath
                                                                     .filename() !=
                                                                 switchId)
                                                             {
                                                                 continue;
                                                             }
                                                             dbus::utility::getProperty<std::vector<
                                                                 std::string>>("xyz.openbmc_project.ObjectMapper",
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
                                                                                           ec3,
                                                                                   const std::vector<
                                                                                       std::
                                                                                           string>& portResp) {
                                                                                   if (ec3)
                                                                                   {
                                                                                       BMCWEB_LOG_ERROR(
                                                                                           "DBUS response error while getting port on switch: {}",
                                                                                           ec3.message());
                                                                                       messages::internalError(
                                                                                           asyncResp
                                                                                               ->res);
                                                                                       return;
                                                                                   }

                                                                                   // Iterate over all retrieved
                                                                                   // ObjectPaths.
                                                                                   for (
                                                                                       const std::
                                                                                           string&
                                                                                               portPath :
                                                                                       portResp)
                                                                                   {
                                                                                       sdbusplus::object_path
                                                                                           switchPortObjPath(
                                                                                               portPath);
                                                                                       if (switchPortObjPath
                                                                                               .filename() !=
                                                                                           portId)
                                                                                       {
                                                                                           continue;
                                                                                       }
                                                                                       std::string
                                                                                           histoURI =
                                                                                               "/redfish/v1/Fabrics/";
                                                                                       histoURI +=
                                                                                           fabricId;
                                                                                       histoURI +=
                                                                                           "/Switches/";
                                                                                       histoURI +=
                                                                                           switchId;
                                                                                       histoURI +=
                                                                                           "/Ports/";
                                                                                       histoURI +=
                                                                                           portId;
                                                                                       histoURI +=
                                                                                           "/Oem/Nvidia/Histograms";
                                                                                       asyncResp
                                                                                           ->res
                                                                                           .jsonValue
                                                                                               ["@odata.type"] =
                                                                                           "#NvidiaHistogramCollection.NvidiaHistogramCollection";
                                                                                       asyncResp
                                                                                           ->res
                                                                                           .jsonValue
                                                                                               ["@odata.id"] =
                                                                                           histoURI;
                                                                                       std::string
                                                                                           name5 =
                                                                                               switchId;
                                                                                       name5 +=
                                                                                           "_";
                                                                                       name5 +=
                                                                                           portId;
                                                                                       name5 +=
                                                                                           "_Histogram_Collection";
                                                                                       asyncResp
                                                                                           ->res
                                                                                           .jsonValue
                                                                                               ["Name"] =
                                                                                           name5;

                                                                                       std::string
                                                                                           portCollectionUri =
                                                                                               "/redfish/v1/Fabrics/";
                                                                                       portCollectionUri +=
                                                                                           fabricId;
                                                                                       portCollectionUri +=
                                                                                           "/Switches/";
                                                                                       portCollectionUri +=
                                                                                           switchId;
                                                                                       portCollectionUri +=
                                                                                           "/Ports/";
                                                                                       portCollectionUri +=
                                                                                           portId;
                                                                                       portCollectionUri +=
                                                                                           "/Oem/Nvidia/Histograms";
                                                                                       collection_util::getCollectionMembersByAssociation(
                                                                                           asyncResp,
                                                                                           portCollectionUri,
                                                                                           portPath +
                                                                                               "/histograms",
                                                                                           {});
                                                                                       return;
                                                                                   }
                                                                               });
                                                             return;
                                                         }
                                                         // Couldn't find an
                                                         // object with that
                                                         // name. Return an
                                                         // error
                                                         messages::resourceNotFound(
                                                             asyncResp->res,
                                                             "#Switch.v1_8_0.Switch",
                                                             switchId);
                                                     });
                        return;
                    }
                    // Couldn't find an object with that name. Return an
                    // error
                    messages::resourceNotFound(
                        asyncResp->res, "#Fabric.v1_2_0.Fabric", fabricId);
                });
        });
}

} // namespace redfish
