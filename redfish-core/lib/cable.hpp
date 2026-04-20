// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/resource.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "logging.hpp"
#include "nvidia_cables.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"

#include <asm-generic/errno.h>

#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <boost/url/url.hpp>
#include <sdbusplus/message/native_types.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace redfish
{

constexpr std::array<std::string_view, 1> cableInterfaces = {
    "xyz.openbmc_project.Inventory.Item.Cable"};

/**
 * @brief Fill cable specific properties.
 * @param[in,out]   asyncResp   Async HTTP response.
 * @param[in]       ec          Error code corresponding to Async method call.
 * @param[in]       properties  List of Cable Properties key/value pairs.
 */
inline void fillCableProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    const std::string* cableTypeDescription = nullptr;
    const double* length = nullptr;
    const std::string* cableClass = nullptr;
    const std::vector<std::string>* downstreamConnectorTypes = nullptr;
    const std::vector<std::string>* upstreamConnectorTypes = nullptr;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties, "CableTypeDescription",
        cableTypeDescription, "Length", length, "CableClass", cableClass,
        "DownstreamConnectorTypes", downstreamConnectorTypes,
        "UpstreamConnectorTypes", upstreamConnectorTypes);

    if (!success)
    {
        messages::internalError(asyncResp->res);
        return;
    }

    // Nvidia: convert D-Bus enum paths to Redfish strings
    nvidia_cables::fillNvidiaCableProperties(
        asyncResp, cableClass, downstreamConnectorTypes,
        upstreamConnectorTypes);
}

inline void handleCablePresence(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& cableObjectPath, const boost::system::error_code& ec,
    bool present)
{
    if (ec)
    {
        if (ec.value() != EBADR)
        {
            BMCWEB_LOG_ERROR("get presence failed for Cable {} with error {}",
                             cableObjectPath, ec.value());
            messages::internalError(asyncResp->res);
        }
        return;
    }

    if (!present)
    {
        asyncResp->res.jsonValue["Status"]["State"] = resource::State::Absent;
    }
}

inline void fillCableHealthState(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& cableObjectPath, const std::string& service)
{
    dbus::utility::getProperty<bool>(
        service, cableObjectPath, "xyz.openbmc_project.Inventory.Item",
        "Present",
        [asyncResp,
         cableObjectPath](const boost::system::error_code& ec, bool present) {
            handleCablePresence(asyncResp, cableObjectPath, ec, present);
        });
}

/**
 * @brief Api to get Cable properties.
 * @param[in,out]   asyncResp       Async HTTP response.
 * @param[in]       cableObjectPath Object path of the Cable.
 * @param[in]       serviceMap      A map to hold Service and corresponding
 * interface list for the given cable id.
 */
inline void getCableProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& cableObjectPath,
    const dbus::utility::MapperServiceMap& serviceMap)
{
    BMCWEB_LOG_DEBUG("Get Properties for cable {}", cableObjectPath);

    for (const auto& [service, interfaces] : serviceMap)
    {
        for (const auto& interface : interfaces)
        {
            if (interface == "xyz.openbmc_project.Inventory.Item.Cable")
            {
                dbus::utility::getAllProperties(
                    service, cableObjectPath, interface,
                    std::bind_front(fillCableProperties, asyncResp));
            }
            else if (interface == "xyz.openbmc_project.Inventory.Item")
            {
                fillCableHealthState(asyncResp, cableObjectPath, service);
            }
        }
    }

    // Nvidia: fetch inventory, location, and OEM properties
    nvidia_cables::getNvidiaCableProperties(asyncResp, cableObjectPath,
                                            serviceMap);
}

inline void afterHandleCableGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& cableId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec.value() == EBADR)
    {
        messages::resourceNotFound(asyncResp->res, "Cable", cableId);
        return;
    }

    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
        messages::internalError(asyncResp->res);
        return;
    }

    for (const auto& [objectPath, serviceMap] : subtree)
    {
        sdbusplus::object_path path(objectPath);
        if (path.filename() != cableId)
        {
            continue;
        }

        asyncResp->res.jsonValue["@odata.type"] = "#Cable.v1_0_0.Cable";
        asyncResp->res.jsonValue["@odata.id"] =
            boost::urls::format("/redfish/v1/Cables/{}", cableId);
        asyncResp->res.jsonValue["Id"] = cableId;
        // Nvidia modified: use cableId as Name instead of "Cable"
        asyncResp->res.jsonValue["Name"] = cableId;
        asyncResp->res.jsonValue["Status"]["State"] = resource::State::Enabled;
        asyncResp->res.jsonValue["Status"]["Health"] = resource::Health::OK;

        // Nvidia: add Assembly link and DownstreamChassis association
        nvidia_cables::addNvidiaCableLinks(asyncResp, cableId, objectPath);

        getCableProperties(asyncResp, objectPath, serviceMap);
        return;
    }
    messages::resourceNotFound(asyncResp->res, "Cable", cableId);
}

inline void handleCableGet(App& app, const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& cableId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    BMCWEB_LOG_DEBUG("Cable Id: {}", cableId);

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, cableInterfaces,
        std::bind_front(afterHandleCableGet, asyncResp, cableId));
}

inline void handleCableCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] =
        "#CableCollection.CableCollection";
    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/Cables";
    asyncResp->res.jsonValue["Name"] = "Cable Collection";
    asyncResp->res.jsonValue["Description"] = "Collection of Cable Entries";
    collection_util::getCollectionMembers(
        asyncResp, boost::urls::url("/redfish/v1/Cables"), cableInterfaces,
        "/xyz/openbmc_project/inventory");
}

/**
 * The Cable schema
 */
inline void requestRoutesCable(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Cables/<str>/")
        .privileges(redfish::privileges::getCable)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleCableGet, std::ref(app)));

    // Nvidia: Cable Assembly endpoint
    nvidia_cables::requestRoutesCableAssembly(app);
}

/**
 * Collection of Cable resource instances
 */
inline void requestRoutesCableCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Cables/")
        .privileges(redfish::privileges::getCableCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleCableCollectionGet, std::ref(app)));
}

} // namespace redfish
