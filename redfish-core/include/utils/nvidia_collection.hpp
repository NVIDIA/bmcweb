// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http/utility.hpp"
#include "human_sort.hpp"
#include "logging.hpp"

#include <boost/container/flat_set.hpp>
#include <boost/url/url.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <algorithm>
#include <functional>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace redfish
{
namespace collection_util
{

/**
 * @brief Populate the collection "Members" from a Association search of
 *        inventory
 *
 * @param[i,o] aResp  Async response object
 * @param[i]   collectionPath  Redfish collection path which is used for the
 *             Members Redfish Path
 * @param[i]   objPath  Assocaition object path to search
 * @param[i]   interfaces  List of interfaces to constrain the object search
 *
 * @return void
 */
inline void getCollectionMembersByAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& collectionPath, const std::string& objPath,
    const std::vector<const char*>& interfaces)
{
    BMCWEB_LOG_DEBUG("Get collection members by association for: {}",
                     collectionPath);
    crow::connections::systemBus->async_method_call(
        [aResp, collectionPath,
         interfaces](const boost::system::error_code& e,
                     std::variant<std::vector<std::string>>& resp) {
            if (e)
            {
                // no members attached.
                aResp->res.jsonValue["Members"] = nlohmann::json::array();
                aResp->res.jsonValue["Members@odata.count"] = 0;
                return;
            }

            std::vector<std::string>* data =
                std::get_if<std::vector<std::string>>(&resp);
            if (data == nullptr)
            {
                messages::internalError(aResp->res);
                return;
            }

            // Collection members
            nlohmann::json& members = aResp->res.jsonValue["Members"];

            members = nlohmann::json::array();
            for (const std::string& sensorpath : *data)
            {
                // Check Interface in Object or not
                crow::connections::systemBus->async_method_call(
                    [aResp, collectionPath, sensorpath, &members](
                        const boost::system::error_code& ec,
                        const std::vector<
                            std::pair<std::string, std::vector<std::string>>>&
                        /*object*/) {
                        if (ec)
                        {
                            // the path does not implement any interfaces
                            return;
                        }

                        // Found member
                        sdbusplus::message::object_path path(sensorpath);
                        if (path.filename().empty())
                        {
                            return;
                        }
                        members.push_back({{"@odata.id", collectionPath + "/" +
                                                             path.filename()}});
                        aResp->res.jsonValue["Members@odata.count"] =
                            members.size();
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetObject", sensorpath,
                    interfaces);
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath,
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

} // namespace collection_util
} // namespace redfish