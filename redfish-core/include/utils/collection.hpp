#pragma once

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http/utility.hpp"
#include "human_sort.hpp"

#include <boost/container/flat_set.hpp>
#include <boost/url/url.hpp>
#include <nlohmann/json.hpp>

#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace redfish
{
namespace collection_util
{

inline void handleCollectionMembers(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::urls::url& collectionPath,
    const nlohmann::json::json_pointer& jsonKeyName,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& objects)
{
    if (jsonKeyName.empty())
    {
        messages::internalError(asyncResp->res);
        BMCWEB_LOG_ERROR("Json Key called empty.  Did you mean /Members?");
        return;
    }
    nlohmann::json::json_pointer jsonCountKeyName = jsonKeyName;
    std::string back = jsonCountKeyName.back();
    jsonCountKeyName.pop_back();
    jsonCountKeyName /= back + "@odata.count";

    if (ec == boost::system::errc::io_error)
    {
        asyncResp->res.jsonValue[jsonKeyName] = nlohmann::json::array();
        asyncResp->res.jsonValue[jsonCountKeyName] = 0;
        return;
    }

    if (ec)
    {
        BMCWEB_LOG_DEBUG("DBUS response error {}", ec.value());
        messages::internalError(asyncResp->res);
        return;
    }

    boost::container::flat_set<std::string> pathNames;
    for (const auto& object : objects)
    {
        sdbusplus::message::object_path path(object);
        std::string leaf = path.filename();
        if (leaf.empty())
        {
            continue;
        }
        pathNames.emplace(std::move(leaf));
    }

    nlohmann::json& members = asyncResp->res.jsonValue[jsonKeyName];
    members = nlohmann::json::array();

    for (const std::string& leaf : pathNames)
    {
        boost::urls::url url = collectionPath;
        crow::utility::appendUrlPieces(url, leaf);
        nlohmann::json::object_t member;
        member["@odata.id"] = std::move(url);
        members.emplace_back(std::move(member));
    }
    asyncResp->res.jsonValue[jsonCountKeyName] = members.size();
}

/**
 * @brief Populate the collection members from a GetSubTreePaths search of
 *        inventory
 *
 * @param[i,o] asyncResp  Async response object
 * @param[i]   collectionPath  Redfish collection path which is used for the
 *             Members Redfish Path
 * @param[i]   interfaces  List of interfaces to constrain the GetSubTree search
 * @param[in]  subtree     D-Bus base path to constrain search to.
 * @param[in]  jsonKeyName Key name in which the collection members will be
 *             stored.
 *
 * @return void
 */
inline void
    getCollectionToKey(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const boost::urls::url& collectionPath,
                       std::span<const std::string_view> interfaces,
                       const std::string& subtree,
                       const nlohmann::json::json_pointer& jsonKeyName)
{
    BMCWEB_LOG_DEBUG("Get collection members for: {}", collectionPath.buffer());
    dbus::utility::getSubTreePaths(subtree, 0, interfaces,
                                   std::bind_front(handleCollectionMembers,
                                                   asyncResp, collectionPath,
                                                   jsonKeyName));
}
inline void
    getCollectionMembers(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const boost::urls::url& collectionPath,
                         std::span<const std::string_view> interfaces,
                         const std::string& subtree)
{
    getCollectionToKey(asyncResp, collectionPath, interfaces, subtree,
                       nlohmann::json::json_pointer("/Members"));
}

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
                    const boost::system::error_code ec,
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
                members.push_back(
                    {{"@odata.id", collectionPath + "/" + path.filename()}});
                aResp->res.jsonValue["Members@odata.count"] = members.size();
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
