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

#include "app.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"

namespace redfish
{
static constexpr auto leakDetectorStateInterface =
    "xyz.openbmc_project.State.LeakDetector";
static constexpr auto leakDetectorInventoryInterface =
    "xyz.openbmc_project.Inventory.Item.LeakDetector";

constexpr std::array<std::string_view, 1> leakDetectorInventoryInterfaces = {
    leakDetectorInventoryInterface};

constexpr std::array<std::string_view, 1> leakDetectorStateInterfaces = {
    leakDetectorStateInterface};

inline void getValidLeakDetectorPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& leakDetectorId,
    const std::function<void(const std::string& leakDetectorPath,
                             const std::string& service)>& callback)
{
    sdbusplus::message::object_path inventoryPath(
        "/xyz/openbmc_project/inventory/leakdetectors/");
    sdbusplus::message::object_path leakDetectorPath = inventoryPath /
                                                       leakDetectorId;

    dbus::utility::getDbusObject(
        leakDetectorPath, leakDetectorInventoryInterfaces,
        [leakDetectorPath, leakDetectorId, asyncResp,
         callback](const boost::system::error_code& ec,
                   const dbus::utility::MapperGetObject& object) {
        if (ec || object.empty())
        {
            BMCWEB_LOG_ERROR("DBUS response error on getDbusObject {}",
                             ec.value());
            messages::internalError(asyncResp->res);
            return;
        }

        callback(leakDetectorPath, object.begin()->first);
    });
}

inline void addLeakDetectorCommonProperties(crow::Response& resp,
                                            const std::string& chassisId,
                                            const std::string& leakDetectorId)
{
    resp.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/LeakDetector/LeakDetector.json>; rel=describedby");
    resp.jsonValue["@odata.type"] = "#LeakDetector.v1_1_0.LeakDetector";
    resp.jsonValue["Name"] = "Leak Detector";
    resp.jsonValue["Id"] = leakDetectorId;
    resp.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/ThermalSubsystem/LeakDetection/LeakDetectors/{}",
        chassisId, leakDetectorId);
    resp.jsonValue["Status"]["State"] = "Enabled";
    resp.jsonValue["Status"]["Health"] = "OK";
}

inline void
    getLeakDetectorState(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& leakDetectorPath,
                         const std::string& service)
{
    dbus::utility::getAssociatedSubTreePaths(
        leakDetectorPath + "/leak_detecting",
        sdbusplus::message::object_path("/xyz/openbmc_project/state"), 0,
        leakDetectorStateInterfaces,
        [asyncResp, service](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreePathsResponse& subtreePaths) {
        if (ec)
        {
            if (ec.value() != EBADR)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error for getAssociatedSubTreePaths {}",
                    ec.value());
                messages::internalError(asyncResp->res);
            }
            return;
        }

        if (subtreePaths.size() != 1)
        {
            BMCWEB_LOG_ERROR(
                "Unexpected number of paths returned by getSubTree: {}",
                subtreePaths.size());
            messages::internalError(asyncResp->res);
            return;
        }

        sdbusplus::asio::getAllProperties(
            *crow::connections::systemBus, service, subtreePaths.front(),
            leakDetectorStateInterface,
            [asyncResp](
                const boost::system::error_code& ec,
                const dbus::utility::DBusPropertiesMap& propertiesList) {
            if (ec)
            {
                if (ec.value() != EBADR)
                {
                    BMCWEB_LOG_ERROR("DBUS response error for State {}",
                                     ec.value());
                    messages::internalError(asyncResp->res);
                }
                return;
            }

            const std::string* detectorState = nullptr;

            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), propertiesList,
                "DetectorState", detectorState);

            if (!success)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            if (detectorState != nullptr)
            {
                asyncResp->res.jsonValue["DetectorState"] = *detectorState;
                asyncResp->res.jsonValue["Status"]["Health"] = *detectorState;
            }
        });
    });
}

inline void
    getLeakDetectorItem(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& leakDetectorPath,
                        const std::string& service)
{
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, leakDetectorPath,
        leakDetectorInventoryInterface,
        [asyncResp, leakDetectorPath](
            const boost::system::error_code& ec,
            const dbus::utility::DBusPropertiesMap& propertiesList) {
        if (ec.value() == EBADR)
        {
            messages::resourceNotFound(
                asyncResp->res, "LeakDetector",
                sdbusplus::message::object_path(leakDetectorPath).filename());
            return;
        }
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error for State {}", ec.value());
            messages::internalError(asyncResp->res);
            return;
        }

        const std::string* leakDetectorType = nullptr;

        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), propertiesList,
            "LeakDetectorType", leakDetectorType);

        if (!success)
        {
            messages::internalError(asyncResp->res);
            return;
        }

        if (leakDetectorType != nullptr)
        {
            asyncResp->res.jsonValue["LeakDetectorType"] = *leakDetectorType;
        }
    });
}

inline void afterGetValidLeakDetectorPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& leakDetectorId,
    const std::string& leakDetectorPath, const std::string& service)
{
    addLeakDetectorCommonProperties(asyncResp->res, chassisId, leakDetectorId);
    getLeakDetectorState(asyncResp, leakDetectorPath, service);
    getLeakDetectorItem(asyncResp, leakDetectorPath, service);
}

inline void
    doLeakDetectorGet(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& chassisId,
                      const std::string& leakDetectorId,
                      const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }

    getValidLeakDetectorPath(asyncResp, leakDetectorId,
                             std::bind_front(afterGetValidLeakDetectorPath,
                                             asyncResp, chassisId,
                                             leakDetectorId));
}

inline void
    handleLeakDetectorGet(App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& chassisId,
                          const std::string& leakDetectorId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(doLeakDetectorGet, asyncResp, chassisId,
                        leakDetectorId));
}

inline void doLeakDetectorCollection(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId,
    const std::optional<std::string>& validChassisPath)
{
    if (!validChassisPath)
    {
        messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        return;
    }
    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/LeakDetectorCollection/LeakDetectorCollection.json>; rel=describedby");
    asyncResp->res.jsonValue["@odata.type"] =
        "#LeakDetectorCollection.LeakDetectorCollection";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/ThermalSubsystem/LeakDetection/LeakDetectors",
        chassisId);
    asyncResp->res.jsonValue["Name"] = "Leak Detector Collection";
    asyncResp->res.jsonValue["Description"] =
        "Collection of Leak Detectors for Chassis " + chassisId;

    boost::urls::url collectionUrl = boost::urls::format(
        "/redfish/v1/Chassis/{}/ThermalSubsystem/LeakDetection/LeakDetectors",
        chassisId);
    collection_util::getCollectionMembersByAssociation(
        asyncResp, std::string(collectionUrl.data(), collectionUrl.size()),
        *validChassisPath + "/contained_by", {leakDetectorInventoryInterface});
}

inline void handleLeakDetectorCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    redfish::chassis_utils::getValidChassisPath(
        asyncResp, chassisId,
        std::bind_front(doLeakDetectorCollection, asyncResp, chassisId));
}

inline void requestRoutesLeakDetector(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/ThermalSubsystem/LeakDetection/LeakDetectors/")
        .privileges(redfish::privileges::getLeakDetectorCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleLeakDetectorCollectionGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/ThermalSubsystem/LeakDetection/LeakDetectors/<str>/")
        .privileges(redfish::privileges::getLeakDetector)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleLeakDetectorGet, std::ref(app)));
}

} // namespace redfish
