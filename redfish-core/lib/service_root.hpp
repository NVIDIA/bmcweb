/*
Copyright (c) 2018 Intel Corporation

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "http_request.hpp"
#include "persistent_data.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/systemd_utils.hpp"

#include <managers.hpp>
#include "nvidia_managers.hpp"
#include <nlohmann/json.hpp>
#include <persistent_data.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <utils/systemd_utils.hpp>

namespace redfish
{

constexpr std::array<const char*, 1> bmcInterfaces = {
    "xyz.openbmc_project.Inventory.Item.BMC"};

inline void
    handleServiceRootHead(App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/ServiceRoot/ServiceRoot.json>; rel=describedby");
}

/**
 * Fill out Asset information from from given D-Bus object
 *
 * @param[in,out]   asyncResp       Async HTTP response.
 * @param[in]       service         D-Bus service to query.
 * @param[in]       objpath         D-Bus object to query.
 *                                  successfully finding object.
 */
inline void getBmcAssetData(std::shared_ptr<bmcweb::AsyncResp> asyncResp,
                            const std::string& service,
                            const std::string& objPath)
{
    BMCWEB_LOG_DEBUG("Get BMC Asset Data");
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, objPath,
        "xyz.openbmc_project.Inventory.Decorator.Asset",
        [objPath, asyncResp{std::move(asyncResp)}](
            const boost::system::error_code ec,
            const dbus::utility::DBusPropertiesMap& properties) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error");
            messages::internalError(asyncResp->res);
            return;
        }

        const std::string* name = nullptr;
        const std::string* model = nullptr;
        const std::string* manufacturer = nullptr;

        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), properties, "Name", name, "Model",
            model, "Manufacturer", manufacturer);

        if (!success)
        {
            BMCWEB_LOG_ERROR("Unpack Error while fetching BMC Asset data");
            return;
        }

        if (name != nullptr && !name->empty())
        {
            std::string description = "Redfish Service On ";
            description += *name;
            asyncResp->res.jsonValue["Description"] = description;
        }

        if ((model != nullptr) && !model->empty())
        {
            asyncResp->res.jsonValue["Product"] = *model;
        }

        if (manufacturer != nullptr)
        {
            asyncResp->res.jsonValue["Vendor"] = *manufacturer;
        }
    });
}

inline void getBMCObject(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    BMCWEB_LOG_DEBUG("Get available BMC resources.");

    // GetSubTree on all interfaces which provide info about BMC
    crow::connections::systemBus->async_method_call(
        [asyncResp](
            boost::system::error_code ec,
            const dbus::utility::MapperGetSubTreeResponse& subtree) mutable {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }
        for (const auto& [objectPath, serviceMap] : subtree)
        {
            // Ignore any objects which don't end with our desired bmcid
            if (!boost::ends_with(objectPath, BMCWEB_REDFISH_MANAGER_URI_NAME))
            {
                continue;
            }

            bool found = false;
            // Filter out objects that don't have the BMC-specific
            // interfaces to make sure we can return 404 on non-BMC
            for (const auto& [serviceName, interfaceList] : serviceMap)
            {
                if (std::find_first_of(
                        interfaceList.begin(), interfaceList.end(),
                        bmcInterfaces.begin(),
                        bmcInterfaces.end()) != interfaceList.end())
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                continue;
            }

            for (const auto& [serviceName, interfaceList] : serviceMap)
            {
                for (const auto& interface : interfaceList)
                {
                    if (interface ==
                        "xyz.openbmc_project.Inventory.Decorator.Asset")
                    {
                        getBmcAssetData(asyncResp, serviceName, objectPath);
                    }
                }
            }

            return;
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Decorator.Asset"});
}

inline void handleServiceRootGetImpl(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    asyncResp->res.addHeader(
        boost::beast::http::field::link,
        "</redfish/v1/JsonSchemas/ServiceRoot/ServiceRoot.json>; rel=describedby");

    std::string uuid = persistent_data::getConfig().systemUuid;
    asyncResp->res.jsonValue["@odata.type"] =
        "#ServiceRoot.v1_15_0.ServiceRoot";
    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1";
    asyncResp->res.jsonValue["Id"] = "RootService";
    asyncResp->res.jsonValue["Name"] = "Root Service";
    asyncResp->res.jsonValue["RedfishVersion"] = "1.17.0";
    if (persistent_data::getConfig().isTLSAuthEnabled())
    {
        asyncResp->res.jsonValue["Links"]["Sessions"]["@odata.id"] =
            "/redfish/v1/SessionService/Sessions";
        asyncResp->res.jsonValue["AccountService"]["@odata.id"] =
            "/redfish/v1/AccountService";
    }
    if constexpr (BMCWEB_REDFISH_AGGREGATION)
    {
        asyncResp->res.jsonValue["AggregationService"]["@odata.id"] =
            "/redfish/v1/AggregationService";
    }
    asyncResp->res.jsonValue["Chassis"]["@odata.id"] = "/redfish/v1/Chassis";
    if constexpr (!BMCWEB_NVIDIA_OEM_BF_PROPERTIES)
    {
        asyncResp->res.jsonValue["ComponentIntegrity"]["@odata.id"] =
            "/redfish/v1/ComponentIntegrity";
    }
    asyncResp->res.jsonValue["Fabrics"]["@odata.id"] = "/redfish/v1/Fabrics";
    asyncResp->res.jsonValue["JsonSchemas"]["@odata.id"] =
        "/redfish/v1/JsonSchemas";
    asyncResp->res.jsonValue["Managers"]["@odata.id"] = "/redfish/v1/Managers";
    if (persistent_data::getConfig().isTLSAuthEnabled())
    {
        asyncResp->res.jsonValue["SessionService"]["@odata.id"] =
            "/redfish/v1/SessionService";
    }
    asyncResp->res.jsonValue["Systems"]["@odata.id"] = "/redfish/v1/Systems";
    asyncResp->res.jsonValue["Registries"]["@odata.id"] =
        "/redfish/v1/Registries";
    asyncResp->res.jsonValue["UpdateService"]["@odata.id"] =
        "/redfish/v1/UpdateService";
    asyncResp->res.jsonValue["UUID"] = uuid;
    if (persistent_data::getConfig().isTLSAuthEnabled())
    {
        asyncResp->res.jsonValue["CertificateService"]["@odata.id"] =
            "/redfish/v1/CertificateService";
    }
    asyncResp->res.jsonValue["ServiceConditions"]["@odata.id"] =
        "/redfish/v1/ServiceConditions";
    asyncResp->res.jsonValue["Tasks"]["@odata.id"] = "/redfish/v1/TaskService";
    asyncResp->res.jsonValue["EventService"]["@odata.id"] =
        "/redfish/v1/EventService";
    asyncResp->res.jsonValue["TelemetryService"]["@odata.id"] =
        "/redfish/v1/TelemetryService";

    if constexpr (BMCWEB_HOST_OS_FEATURES)
    {
        asyncResp->res.jsonValue["Cables"]["@odata.id"] = "/redfish/v1/Cables";
    }
    asyncResp->res.jsonValue["Links"]["ManagerProvidingService"]["@odata.id"] =
        boost::urls::format("/redfish/v1/Managers/{}",
                            BMCWEB_REDFISH_MANAGER_URI_NAME);

    nlohmann::json& protocolFeatures =
        asyncResp->res.jsonValue["ProtocolFeaturesSupported"];
    protocolFeatures["ExcerptQuery"] = false;

    protocolFeatures["ExpandQuery"]["ExpandAll"] =
        BMCWEB_INSECURE_ENABLE_REDFISH_QUERY;
    // This is the maximum level defined in ServiceRoot.v1_13_0.json
    if constexpr (BMCWEB_INSECURE_ENABLE_REDFISH_QUERY)
    {
        protocolFeatures["ExpandQuery"]["MaxLevels"] = 6;
    }
    protocolFeatures["ExpandQuery"]["Levels"] =
        BMCWEB_INSECURE_ENABLE_REDFISH_QUERY;
    protocolFeatures["ExpandQuery"]["Links"] =
        BMCWEB_INSECURE_ENABLE_REDFISH_QUERY;
    protocolFeatures["ExpandQuery"]["NoLinks"] =
        BMCWEB_INSECURE_ENABLE_REDFISH_QUERY;
    protocolFeatures["FilterQuery"] = BMCWEB_INSECURE_ENABLE_REDFISH_QUERY;
    protocolFeatures["OnlyMemberQuery"] = true;
    protocolFeatures["SelectQuery"] = true;
    protocolFeatures["DeepOperations"]["DeepPOST"] = false;
    protocolFeatures["DeepOperations"]["DeepPATCH"] = false;
}
inline void
    handleServiceRootGet(App& app, const crow::Request& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    handleServiceRootGetImpl(asyncResp);
    getBMCObject(asyncResp);
    getServiceIdentification(asyncResp);
}

inline void requestRoutesServiceRoot(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/")
        .privileges(redfish::privileges::headServiceRoot)
        .methods(boost::beast::http::verb::head)(
            std::bind_front(handleServiceRootHead, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/")
        .privileges(redfish::privileges::getServiceRoot)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleServiceRootGet, std::ref(app)));
}

} // namespace redfish
