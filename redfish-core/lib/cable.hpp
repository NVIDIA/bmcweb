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
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"

#include <asm-generic/errno.h>

#include <boost/beast/http/verb.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <boost/url/url.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/message/native_types.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/nvidia_cable_util.hpp>

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace redfish
{

/**
 * @brief Convert D-Bus CableClass to Redfish CableClass
 * Handles D-Bus enum paths and validates simple strings
 */
inline std::string dbusToRedfishCableClass(const std::string& dbusCableClass)
{
    // Handle D-Bus enum path for General (the only one we actually use)
    if (dbusCableClass ==
        "xyz.openbmc_project.Inventory.Item.Cable.CableClass.General")
    {
        return "General";
    }

    // Pass through if already a valid Redfish value
    if (dbusCableClass == "General")
    {
        return dbusCableClass;
    }

    // Default fallback for any unexpected values
    return "General";
}

/**
 * @brief Convert D-Bus ConnectorType to Redfish ConnectorType
 * Handles D-Bus enum paths and validates simple strings
 */
inline std::string dbusToRedfishConnectorType(
    const std::string& dbusConnectorType)
{
    // Handle D-Bus enum path for Proprietary (the only one we actually use)
    if (dbusConnectorType ==
        "xyz.openbmc_project.Inventory.Item.Connector.ConnectorType.Proprietary")
    {
        return "Proprietary";
    }

    // Pass through if already a valid Redfish value
    if (dbusConnectorType == "Proprietary")
    {
        return dbusConnectorType;
    }

    // Default fallback for any unexpected values
    return "Proprietary";
}

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

    if (cableTypeDescription != nullptr)
    {
        asyncResp->res.jsonValue["CableType"] = *cableTypeDescription;
    }

    if (length != nullptr)
    {
        if (!std::isfinite(*length))
        {
            // Cable length is NaN by default, do not throw an error
            if (!std::isnan(*length))
            {
                BMCWEB_LOG_ERROR("Cable length value is invalid");
                messages::internalError(asyncResp->res);
                return;
            }
        }
        else
        {
            asyncResp->res.jsonValue["LengthMeters"] = *length;
        }
    }

    if (cableClass != nullptr)
    {
        asyncResp->res.jsonValue["CableClass"] =
            dbusToRedfishCableClass(*cableClass);
    }

    if (downstreamConnectorTypes != nullptr)
    {
        nlohmann::json::array_t downstreamArray;
        for (const auto& connectorType : *downstreamConnectorTypes)
        {
            downstreamArray.emplace_back(
                dbusToRedfishConnectorType(connectorType));
        }
        asyncResp->res.jsonValue["DownstreamConnectorTypes"] = downstreamArray;
    }

    if (upstreamConnectorTypes != nullptr)
    {
        nlohmann::json::array_t upstreamArray;
        for (const auto& connectorType : *upstreamConnectorTypes)
        {
            upstreamArray.emplace_back(
                dbusToRedfishConnectorType(connectorType));
        }
        asyncResp->res.jsonValue["UpstreamConnectorTypes"] = upstreamArray;
    }
}

inline void fillCableHealthState(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& cableObjectPath, const std::string& service)
{
    dbus::utility::getProperty<bool>(
        *crow::connections::systemBus, service, cableObjectPath,
        "xyz.openbmc_project.Inventory.Item", "Present",
        [asyncResp,
         cableObjectPath](const boost::system::error_code& ec, bool present) {
            if (ec)
            {
                if (ec.value() != EBADR)
                {
                    BMCWEB_LOG_ERROR(
                        "get presence failed for Cable {} with error {}",
                        cableObjectPath, ec.value());
                    messages::internalError(asyncResp->res);
                }
                return;
            }

            if (!present)
            {
                asyncResp->res.jsonValue["Status"]["State"] =
                    resource::State::Absent;
            }
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
                    *crow::connections::systemBus, service, cableObjectPath,
                    interface, std::bind_front(fillCableProperties, asyncResp));
            }
            else if (interface == "xyz.openbmc_project.Inventory.Item")
            {
                fillCableHealthState(asyncResp, cableObjectPath, service);
            }
            // Nvidia code starts here
            else if (
                interface == "xyz.openbmc_project.Inventory.Decorator.Asset" ||
                interface ==
                    "xyz.openbmc_project.Inventory.Decorator.LocationCode" ||
                interface ==
                    "xyz.openbmc_project.Inventory.Decorator.LocationContext")
            {
                fetchCableInventoryProperties(asyncResp, service,
                                              cableObjectPath);
            }
            // Nvidia code ends here
        }
    }

    // Nvidia code starts here
    // Fetch CBC OEM properties. The function queries ObjectMapper internally
    // to find all services providing VendorInformation on this path, including
    // services like CBCTrayTopologyParser that may not have been included in
    // the original GetSubTree query (which filtered by Cable interface).
    fetchCBCOemProperties(asyncResp, cableObjectPath);
    // Nvidia code ends here
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
        sdbusplus::message::object_path path(objectPath);
        if (path.filename() != cableId)
        {
            continue;
        }

        asyncResp->res.jsonValue["@odata.type"] = "#Cable.v1_0_0.Cable";
        asyncResp->res.jsonValue["@odata.id"] =
            boost::urls::format("/redfish/v1/Cables/{}", cableId);
        asyncResp->res.jsonValue["Id"] = cableId;
        asyncResp->res.jsonValue["Name"] = cableId;
        asyncResp->res.jsonValue["Status"]["State"] = resource::State::Enabled;
        asyncResp->res.jsonValue["Status"]["Health"] = resource::Health::OK;

        // Add Assembly link
        asyncResp->res.jsonValue["Assembly"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Cables/{}/Assembly", cableId);

        // Add ManagedBy link
        nlohmann::json::array_t managedBy;
        nlohmann::json::object_t manager;
        manager["@odata.id"] = boost::urls::format(
            "/redfish/v1/Managers/{}", BMCWEB_REDFISH_MANAGER_URI_NAME);
        managedBy.emplace_back(std::move(manager));
        asyncResp->res.jsonValue["Links"]["ManagedBy"] = std::move(managedBy);

        // Add DownstreamChassis link from "downstream_chassis" association
        dbus::utility::getAssociationEndPoints(
            objectPath + "/downstream_chassis",
            [asyncResp, cableId](const boost::system::error_code& ec1,
                                 const dbus::utility::MapperEndPoints& resp) {
                if (ec1 || resp.empty())
                {
                    BMCWEB_LOG_DEBUG(
                        "No downstream_chassis associations found for Cable {}",
                        cableId);
                    return;
                }

                nlohmann::json::array_t downstreamChassis;
                for (const std::string& chassisPath : resp)
                {
                    sdbusplus::message::object_path chassisObjPath(chassisPath);
                    std::string chassisId = chassisObjPath.filename();

                    nlohmann::json::object_t chassis;
                    chassis["@odata.id"] = boost::urls::format(
                        "/redfish/v1/Chassis/{}", chassisId);
                    downstreamChassis.emplace_back(std::move(chassis));
                }
                asyncResp->res.jsonValue["Links"]["DownstreamChassis"] =
                    std::move(downstreamChassis);
            });

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

/**
 * Cable Assembly endpoint
 */
inline void requestRoutesCableAssembly(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Cables/<str>/Assembly/")
        .privileges({{"Login"}})
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& cableId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            BMCWEB_LOG_DEBUG("Cable Assembly doGet enter for {}", cableId);

            const std::array<const char*, 2> interfaces = {
                "xyz.openbmc_project.Inventory.Item.Cable",
                "xyz.openbmc_project.Inventory.Item.CableCartridge"};

            // Get cable object
            crow::connections::systemBus->async_method_call(
                [asyncResp, cableId](
                    const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG("DBUS response error");
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    // Find the cable object
                    for (const auto& [objectPath, serviceMap] : subtree)
                    {
                        sdbusplus::message::object_path path(objectPath);
                        if (path.filename() != cableId)
                        {
                            continue;
                        }

                        if (serviceMap.empty())
                        {
                            BMCWEB_LOG_ERROR("Got 0 Connection names");
                            continue;
                        }

                        const std::string& connectionName = serviceMap[0].first;

                        // Set up Assembly response
                        asyncResp->res.jsonValue["@odata.type"] =
                            "#Assembly.v1_3_0.Assembly";
                        asyncResp->res.jsonValue["@odata.id"] =
                            "/redfish/v1/Cables/" + cableId + "/Assembly";
                        asyncResp->res.jsonValue["Id"] = "Assembly";
                        asyncResp->res.jsonValue["Name"] =
                            "Assembly data for " + cableId;

                        // Get assembly associations
                        crow::connections::systemBus->async_method_call(
                            [asyncResp, cableId, connectionName, objectPath](
                                const boost::system::error_code& ec1,
                                const std::variant<std::vector<std::string>>&
                                    resp) {
                                if (ec1)
                                {
                                    BMCWEB_LOG_DEBUG(
                                        "No assembly associations found");
                                    asyncResp->res.jsonValue["Assemblies"] =
                                        nlohmann::json::array();
                                    return;
                                }

                                const std::vector<std::string>* assemblyList =
                                    std::get_if<std::vector<std::string>>(
                                        &resp);

                                if (assemblyList == nullptr ||
                                    assemblyList->empty())
                                {
                                    asyncResp->res.jsonValue["Assemblies"] =
                                        nlohmann::json::array();
                                    return;
                                }

                                // Sort assembly list by numeric suffix to
                                // ensure 0, 1, 2 ordering
                                std::vector<std::string> sortedAssemblies =
                                    *assemblyList;
                                std::ranges::sort(
                                    sortedAssemblies, [](const std::string& a,
                                                         const std::string& b) {
                                        // Extract the number at the end
                                        // (Assembly0, Assembly1, etc.)
                                        auto getNumber =
                                            [](const std::string& str) -> int {
                                            size_t pos = str.find_last_not_of(
                                                "0123456789");
                                            if (pos != std::string::npos &&
                                                pos < str.length() - 1)
                                            {
                                                return std::stoi(
                                                    str.substr(pos + 1));
                                            }
                                            return 0;
                                        };
                                        return getNumber(a) < getNumber(b);
                                    });

                                asyncResp->res.jsonValue["Assemblies"] =
                                    nlohmann::json::array();
                                asyncResp->res
                                    .jsonValue["Assemblies@odata.count"] =
                                    sortedAssemblies.size();

                                // Process each assembly
                                for (const std::string& assembly :
                                     sortedAssemblies)
                                {
                                    BMCWEB_LOG_DEBUG("Found Assembly Path: {}",
                                                     assembly);

                                    // Get assembly properties
                                    crow::connections::systemBus->async_method_call(
                                        [asyncResp, assembly, cableId](
                                            const boost::system::error_code&
                                                ec2,
                                            const dbus::utility::
                                                DBusPropertiesMap& properties) {
                                            if (ec2)
                                            {
                                                BMCWEB_LOG_ERROR(
                                                    "Error getting assembly properties: {}",
                                                    ec2);
                                                return;
                                            }

                                            nlohmann::json assemblyObj =
                                                nlohmann::json::object();

                                            // Extract properties
                                            const std::string* model = nullptr;
                                            const std::string* partNumber =
                                                nullptr;
                                            const std::string* serialNumber =
                                                nullptr;
                                            const std::string* manufacturer =
                                                nullptr;
                                            const std::string* version =
                                                nullptr;
                                            const std::string* buildDate =
                                                nullptr;

                                            for (const auto& [key, value] :
                                                 properties)
                                            {
                                                if (key == "Model")
                                                {
                                                    model = std::get_if<
                                                        std::string>(&value);
                                                }
                                                else if (key == "PartNumber")
                                                {
                                                    partNumber = std::get_if<
                                                        std::string>(&value);
                                                }
                                                else if (key == "SerialNumber")
                                                {
                                                    serialNumber = std::get_if<
                                                        std::string>(&value);
                                                }
                                                else if (key == "Manufacturer")
                                                {
                                                    manufacturer = std::get_if<
                                                        std::string>(&value);
                                                }
                                                else if (key == "Version")
                                                {
                                                    version = std::get_if<
                                                        std::string>(&value);
                                                }
                                                else if (key == "BuildDate")
                                                {
                                                    buildDate = std::get_if<
                                                        std::string>(&value);
                                                }
                                            }

                                            // Get assembly name from path
                                            sdbusplus::message::object_path
                                                assemblyPath(assembly);
                                            std::string assemblyName =
                                                assemblyPath.filename();

                                            // Extract MemberId (last number in
                                            // the name)
                                            std::string memberId = "0";
                                            size_t lastDigitPos =
                                                assemblyName.find_last_not_of(
                                                    "0123456789");
                                            if (lastDigitPos !=
                                                    std::string::npos &&
                                                lastDigitPos <
                                                    assemblyName.length() - 1)
                                            {
                                                memberId = assemblyName.substr(
                                                    lastDigitPos + 1);
                                            }

                                            assemblyObj["@odata.id"] =
                                                boost::urls::format(
                                                    "/redfish/v1/Cables/{}/Assembly#/Assemblies/{}",
                                                    cableId, memberId);
                                            assemblyObj["MemberId"] = memberId;
                                            assemblyObj["Name"] = assemblyName;

                                            if (model != nullptr)
                                            {
                                                assemblyObj["Model"] = *model;
                                            }
                                            if (partNumber != nullptr)
                                            {
                                                assemblyObj["PartNumber"] =
                                                    *partNumber;
                                            }
                                            if (serialNumber != nullptr)
                                            {
                                                assemblyObj["SerialNumber"] =
                                                    *serialNumber;
                                            }
                                            if (manufacturer != nullptr)
                                            {
                                                assemblyObj["Vendor"] =
                                                    *manufacturer;
                                            }
                                            if (version != nullptr)
                                            {
                                                assemblyObj["Version"] =
                                                    *version;
                                            }
                                            if (buildDate != nullptr)
                                            {
                                                assemblyObj["ProductionDate"] =
                                                    *buildDate;
                                            }

                                            asyncResp->res
                                                .jsonValue["Assemblies"]
                                                .push_back(
                                                    std::move(assemblyObj));
                                        },
                                        connectionName, assembly,
                                        "org.freedesktop.DBus.Properties",
                                        "GetAll",
                                        "xyz.openbmc_project.Inventory.Decorator.Asset");
                                }
                            },
                            "xyz.openbmc_project.ObjectMapper",
                            objectPath + "/assembly",
                            "org.freedesktop.DBus.Properties", "Get",
                            "xyz.openbmc_project.Association", "endpoints");

                        return;
                    }

                    messages::resourceNotFound(asyncResp->res, "Cable",
                                               cableId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/inventory", 0, interfaces);
        });
}

} // namespace redfish
