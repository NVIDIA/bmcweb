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

#include "query.hpp"
#include "utils/collection.hpp"

#include <app.hpp>
#include <async_resp.hpp>
#include <boost/url/url.hpp>
#include <http_request.hpp>
#include <http_response.hpp>
#include <redfish_util.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/unpack_properties.hpp>

namespace redfish
{

namespace nvidia_oem_power_profile
{

/**
 * @brief Handles GET request for PowerProfileCollection which contains a
 * collection of PowerCompliance DBus objects.
 * @param app - crow application
 * @param req - crow request
 * @param asyncResp - response object
 * @param managerId - id of Manager
 * @return None
 */
inline void handlePowerProfileCollectionGetRequest(
    App& app, const sdbusplus::message::object_path& dbusPath,
    const std::string& memberDbusIntf, const boost::urls::url& redfishUri,
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "#Manager",
                                   BMCWEB_REDFISH_MANAGER_URI_NAME);
        return;
    }

    asyncResp->res.jsonValue["@odata.id"] = redfishUri;

    std::array<std::string_view, 1> interfaces{memberDbusIntf};
    collection_util::getCollectionMembers(asyncResp, redfishUri, interfaces,
                                          dbusPath.str);
}

} // namespace nvidia_oem_power_profile

} // namespace redfish
