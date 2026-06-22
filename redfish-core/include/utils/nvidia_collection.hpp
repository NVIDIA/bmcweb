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
    dbus::utility::getProperty<std::vector<std::string>>(
        "xyz.openbmc_project.ObjectMapper", objPath,
        "xyz.openbmc_project.Association", "endpoints",
        [aResp, collectionPath,
         interfaces](const boost::system::error_code& e,
                     const std::vector<std::string>& resp) {
            if (e)
            {
                // no members attached.
                aResp->res.jsonValue["Members"] = nlohmann::json::array();
                aResp->res.jsonValue["Members@odata.count"] = 0;
                return;
            }

            // Collection members
            nlohmann::json& members = aResp->res.jsonValue["Members"];

            members = nlohmann::json::array();
            for (const std::string& sensorpath : resp)
            {
                // Check Interface in Object or not
                std::vector<std::string_view> interfacesVec;
                interfacesVec.reserve(interfaces.size());
                for (const char* iface : interfaces)
                {
                    interfacesVec.emplace_back(iface);
                }
                dbus::utility::getDbusObject(
                    sensorpath, interfacesVec,
                    [aResp, collectionPath, sensorpath,
                     &members](const boost::system::error_code& ec,
                               const dbus::utility::MapperGetObject&
                               /*object*/) {
                        if (ec)
                        {
                            // the path does not implement any interfaces
                            return;
                        }

                        // Found member
                        sdbusplus::object_path path(sensorpath);
                        if (path.filename().empty())
                        {
                            return;
                        }
                        members.push_back({{"@odata.id", collectionPath + "/" +
                                                             path.filename()}});
                        aResp->res.jsonValue["Members@odata.count"] =
                            members.size();
                    });
            }
        });
}

} // namespace collection_util
} // namespace redfish
