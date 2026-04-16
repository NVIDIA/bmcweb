/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2026 NVIDIA CORPORATION &
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
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_write_protect_domains_util.hpp"

#include <boost/beast/http/status.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/message.hpp>

#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace redfish
{
namespace write_protect_domains
{

inline void afterHandleWriteProtectDomainCollectionGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId,
    const std::vector<ObjectServicePair>& domainObjects)
{
    if (domainObjects.empty())
    {
        messages::resourceNotFound(asyncResp->res, "WriteProtectDomain",
                                   chassisId);
        return;
    }

    nlohmann::json& resp = asyncResp->res.jsonValue;
    resp["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/Oem/Nvidia/WriteProtectDomains", chassisId);
    resp["@odata.type"] = "#NvidiaWriteProtectDomainCollection."
                          "NvidiaWriteProtectDomainCollection";
    resp["Name"] = std::format("{} Nvidia Write Protect Domains", chassisId);
    resp["Members"] = nlohmann::json::array();
    nlohmann::json::json_pointer membersPointer("/Members");

    uint16_t domainId = 0;
    for (const auto& [objectPath, service] : domainObjects)
    {
        nlohmann::json object;
        makeDefaultDomainJson(object, chassisId, domainId);
        resp["Members"].emplace_back(std::move(object));

        auto domainPointer = membersPointer / domainId;
        getDomainProperties(asyncResp, domainPointer, objectPath, service);

        if (domainId == std::numeric_limits<uint16_t>::max())
        {
            break;
        }
        domainId++;
    }
    resp["Members@odata.count"] = resp["Members"].size();
}

inline void handleWriteProtectDomainCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    getSortedAssociatedDomainPaths(
        asyncResp, chassisId,
        std::bind_front(afterHandleWriteProtectDomainCollectionGet, asyncResp,
                        chassisId));
}

inline void afterHandleWriteProtectDomainGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& domainIdStr,
    const std::vector<ObjectServicePair>& domainObjects)
{
    if (domainObjects.empty())
    {
        messages::resourceNotFound(asyncResp->res, "WriteProtectDomain",
                                   chassisId);
        return;
    }

    std::optional<uint16_t> domainId = parseDomainId(domainIdStr);
    if (!domainId || *domainId >= domainObjects.size())
    {
        messages::resourceNotFound(asyncResp->res, "NvidiaWriteProtectDomainId",
                                   domainIdStr);
        return;
    }

    makeDefaultDomainJson(asyncResp->res.jsonValue, chassisId, *domainId);

    const auto& [objectPath, service] = domainObjects[*domainId];
    nlohmann::json::json_pointer domainPointer("");
    getDomainProperties(asyncResp, domainPointer, objectPath, service);
}

inline void handleWriteProtectDomainGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& domainIdStr)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    getSortedAssociatedDomainPaths(
        asyncResp, chassisId,
        std::bind_front(afterHandleWriteProtectDomainGet, asyncResp, chassisId,
                        domainIdStr));
}

inline void afterSetWriteProtected(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, const sdbusplus::message::message& m)
{
    if (!ec)
    {
        asyncResp->res.result(boost::beast::http::status::no_content);
        return;
    }

    if (m.is_method_error())
    {
        const sd_bus_error* e = m.get_error();
        const std::string_view notAllowedError =
            "xyz.openbmc_project.Common.Error.NotAllowed";
        if (e != nullptr && notAllowedError == e->name)
        {
            messages::propertyNotWritable(asyncResp->res, "WriteProtected");
        }
        else
        {
            messages::internalError(asyncResp->res);
        }
    }
    else
    {
        messages::internalError(asyncResp->res);
    }
}

inline void setWriteProtected(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service,
    const sdbusplus::message::object_path& objectPath, bool value)
{
    dbus::utility::async_method_call(
        [asyncResp](const boost::system::error_code& ec,
                    const sdbusplus::message::message& msg) {
            afterSetWriteProtected(asyncResp, ec, msg);
        },
        service, objectPath, "com.nvidia.Software.WriteProtection",
        "SetWriteProtected", value);
}

inline void afterHandleWriteProtectDomainPatch(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& domainIdStr,
    std::optional<bool> writeProtected,
    const std::vector<ObjectServicePair>& domainObjects)
{
    if (domainObjects.empty())
    {
        messages::resourceNotFound(asyncResp->res, "WriteProtectDomain",
                                   chassisId);
        return;
    }

    std::optional<uint16_t> domainId = parseDomainId(domainIdStr);
    if (!domainId || *domainId >= domainObjects.size())
    {
        messages::resourceNotFound(asyncResp->res, "NvidiaWriteProtectDomainId",
                                   domainIdStr);
        return;
    }

    if (!writeProtected)
    {
        return;
    }

    const auto& [objectPath, service] = domainObjects[*domainId];
    setWriteProtected(asyncResp, service, objectPath, *writeProtected);
}

inline void handleWriteProtectDomainPatch(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& domainIdStr)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::optional<bool> writeProtected;
    if (!json_util::readJsonPatch(req, asyncResp->res, "WriteProtected",
                                  writeProtected))
    {
        return;
    }

    getSortedAssociatedDomainPaths(
        asyncResp, chassisId,
        std::bind_front(afterHandleWriteProtectDomainPatch, asyncResp,
                        chassisId, domainIdStr, writeProtected));
}

} // namespace write_protect_domains

inline void requestRoutesWriteProtectDomain(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Chassis/<str>/Oem/Nvidia/WriteProtectDomains/")
        .privileges(redfish::privileges::privilegeSetLogin)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            write_protect_domains::handleWriteProtectDomainCollectionGet,
            std::ref(app)));

    BMCWEB_ROUTE(
        app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/WriteProtectDomains/<str>/")
        .privileges(redfish::privileges::privilegeSetLogin)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            write_protect_domains::handleWriteProtectDomainGet, std::ref(app)));

    BMCWEB_ROUTE(
        app, "/redfish/v1/Chassis/<str>/Oem/Nvidia/WriteProtectDomains/<str>/")
        .privileges(redfish::privileges::privilegeSetConfigureComponents)
        .methods(boost::beast::http::verb::patch)(std::bind_front(
            write_protect_domains::handleWriteProtectDomainPatch,
            std::ref(app)));
}

} // namespace redfish
