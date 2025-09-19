// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
// SPDX-FileCopyrightText: Copyright 2018 Intel Corporation
#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "http_request.hpp"
#include "nvidia_managers.hpp"
#include "nvidia_service_root.hpp"
#include "persistent_data.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/manager_utils.hpp"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/url/format.hpp>
#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <string>

namespace redfish
{

inline void handleServiceRootHead(
    App& app, const crow::Request& req,
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
    // nvidida code starts here
    if (persistent_data::nvidia::getConfig().isTLSAuthEnabled())
    {
        asyncResp->res.jsonValue["Links"]["Sessions"]["@odata.id"] =
            "/redfish/v1/SessionService/Sessions";
        asyncResp->res.jsonValue["AccountService"]["@odata.id"] =
            "/redfish/v1/AccountService";
    }
    // nvidida code ends here
    if constexpr (BMCWEB_REDFISH_AGGREGATION)
    {
        asyncResp->res.jsonValue["AggregationService"]["@odata.id"] =
            "/redfish/v1/AggregationService";
    }
    asyncResp->res.jsonValue["Chassis"]["@odata.id"] = "/redfish/v1/Chassis";
    // nvidida code starts here
    if constexpr (!BMCWEB_NVIDIA_OEM_BF_PROPERTIES ||
                  BMCWEB_NVIDIA_OEM_BF3_PROPERTIES)
    {
        asyncResp->res.jsonValue["ComponentIntegrity"]["@odata.id"] =
            "/redfish/v1/ComponentIntegrity";
    }
    asyncResp->res.jsonValue["Fabrics"]["@odata.id"] = "/redfish/v1/Fabrics";
    // nvidida code ends here
    asyncResp->res.jsonValue["JsonSchemas"]["@odata.id"] =
        "/redfish/v1/JsonSchemas";
    asyncResp->res.jsonValue["Managers"]["@odata.id"] = "/redfish/v1/Managers";
    // Nvidia code starts here
    if (persistent_data::nvidia::getConfig().isTLSAuthEnabled())
    {
        asyncResp->res.jsonValue["SessionService"]["@odata.id"] =
            "/redfish/v1/SessionService";
    }
    // nvidida code ends here
    asyncResp->res.jsonValue["Systems"]["@odata.id"] = "/redfish/v1/Systems";
    asyncResp->res.jsonValue["Registries"]["@odata.id"] =
        "/redfish/v1/Registries";
    asyncResp->res.jsonValue["UpdateService"]["@odata.id"] =
        "/redfish/v1/UpdateService";
    asyncResp->res.jsonValue["UUID"] = uuid;
    // Nvidia code starts here
    if (persistent_data::nvidia::getConfig().isTLSAuthEnabled())
    {
        asyncResp->res.jsonValue["CertificateService"]["@odata.id"] =
            "/redfish/v1/CertificateService";
    }
    asyncResp->res.jsonValue["ServiceConditions"]["@odata.id"] =
        "/redfish/v1/ServiceConditions";
    // nvidida code ends here
    asyncResp->res.jsonValue["Tasks"]["@odata.id"] = "/redfish/v1/TaskService";
    asyncResp->res.jsonValue["EventService"]["@odata.id"] =
        "/redfish/v1/EventService";
    asyncResp->res.jsonValue["TelemetryService"]["@odata.id"] =
        "/redfish/v1/TelemetryService";
    manager_utils::getServiceIdentification(asyncResp, true);
    asyncResp->res.jsonValue["Cables"]["@odata.id"] = "/redfish/v1/Cables";

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
inline void handleServiceRootGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    handleServiceRootGetImpl(asyncResp);
    // Nvidia code starts here
    getBMCObject(asyncResp);
    getServiceIdentification(asyncResp);
    // Nvidia code ends here
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
