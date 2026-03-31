#pragma once

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "human_sort.hpp"
#include "logging.hpp"
#include "utils/origin_utils.hpp"

#include <asm-generic/errno.h>

#include <boost/url/format.hpp>
#include <boost/url/parse.hpp>
#include <boost/url/url.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/message.hpp>

#include <array>
#include <charconv>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace redfish
{
namespace write_protect_domains
{

using ObjectServicePair =
    std::pair<sdbusplus::message::object_path, std::string>;

inline void getAssociatedDomains(
    const std::string& chassisId,
    std::function<void(const boost::system::error_code& ec,
                       const dbus::utility::MapperGetSubTreeResponse& subTree)>
        callback)
{
    sdbusplus::message::object_path endpointPath(
        "/xyz/openbmc_project/inventory/system/chassis");
    endpointPath /= chassisId;
    endpointPath /= "write_protect_domains";

    static constexpr std::array<std::string_view, 1> interfaces = {
        "com.nvidia.Software.WriteProtection",
    };

    dbus::utility::getAssociatedSubTree(
        endpointPath,
        sdbusplus::message::object_path("/xyz/openbmc_project/state"), 0,
        interfaces, std::move(callback));
}

inline std::optional<boost::urls::url> redfishUriForObject(
    const sdbusplus::message::object_path& objectPath)
{
    const std::string& path = objectPath;
    const std::string deviceName = objectPath.filename();

    if (deviceName == "BOARD_FRU_ASSEMBLY" ||
        deviceName == "PRODUCT_FRU_ASSEMBLY" ||
        deviceName == "CHASSIS_FRU_ASSEMBLY")
    {
        return boost::urls::format("/redfish/v1/Chassis/{}/Assembly",
                                   BMCWEB_PLATFORM_CHASSIS_NAME);
    }

    for (const auto& [dbusPrefix, redfishPrefix] :
         origin_utils::dBusToRedfishURI)
    {
        if (!path.starts_with(dbusPrefix))
        {
            continue;
        }

        std::string redfishPath = redfishPrefix;
        while (redfishPath.ends_with('/'))
        {
            redfishPath.pop_back();
        }
        redfishPath += '/';
        redfishPath += path.substr(dbusPrefix.length());
        auto parsed = boost::urls::parse_relative_ref(redfishPath);
        if (!parsed)
        {
            BMCWEB_LOG_ERROR("Error constructing Redfish URI from: {}", path);
            return std::nullopt;
        }
        return boost::urls::url(*parsed);
    }

    return std::nullopt;
}

inline void processAssociatedDomains(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId,
    const std::function<void(std::vector<ObjectServicePair>&&)>& callback,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subTree)
{
    if (ec)
    {
        if (ec == boost::system::errc::io_error || ec.value() == EBADR)
        {
            BMCWEB_LOG_WARNING("Chassis not found");
            messages::resourceNotFound(asyncResp->res, "Chassis", chassisId);
        }
        else
        {
            BMCWEB_LOG_ERROR("DBUS response error: {}", ec.value());
            messages::internalError(asyncResp->res);
        }
        return;
    }

    // Sort by object path filename
    std::vector<ObjectServicePair> domains;
    domains.reserve(subTree.size());

    for (const auto& [p, serviceMap] : subTree)
    {
        if (serviceMap.empty())
        {
            BMCWEB_LOG_WARNING("Domain {} is not implemented by any services",
                               p);
            continue;
        }
        if (serviceMap.size() > 1)
        {
            BMCWEB_LOG_WARNING(
                "Domain {} is implemented by multiple services ({})", p,
                serviceMap.size());
        }
        const auto& [service, _interfaces] = serviceMap[0];
        domains.emplace_back(sdbusplus::message::object_path(p), service);
    }

    auto sortByFileName =
        [](const ObjectServicePair& a, const ObjectServicePair& b) {
            return alphanumComp(a.first.filename(), b.first.filename()) < 0;
        };
    std::ranges::sort(domains, sortByFileName);
    callback(std::move(domains));
}

// Resultant vector is indexed by domain ID
inline void getSortedAssociatedDomainPaths(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId,
    std::function<void(std::vector<ObjectServicePair>&&)>&& callback)
{
    getAssociatedDomains(chassisId,
                         std::bind_front(processAssociatedDomains, asyncResp,
                                         chassisId, std::move(callback)));
}

inline void afterGetAssociatedProtectedComponents(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const nlohmann::json::json_pointer& domainPointer,
    const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& affectedObjectPaths)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "DBUS get write_protects associations response error: {}",
            ec.value());
        if (ec.value() != EBADR)
        {
            messages::internalError(asyncResp->res);
        }
        return;
    }

    auto& domainJson = asyncResp->res.jsonValue[domainPointer];
    auto& affectedComponents = domainJson["AffectedComponents"];
    affectedComponents = nlohmann::json::array();
    for (const std::string& path : affectedObjectPaths)
    {
        std::optional<boost::urls::url> uri = redfishUriForObject(path);
        if (!uri)
        {
            BMCWEB_LOG_ERROR("Error converting {} into Redfish URI", path);
            continue;
        }
        nlohmann::json entry;
        entry["@odata.id"] = *uri;
        affectedComponents.emplace_back(std::move(entry));
    }
}

inline void afterGetWriteProtection(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const nlohmann::json::json_pointer& domainPointer,
    const boost::system::error_code& ec, const bool& writeProtected)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS get property response error: {}", ec.value());
        if (ec.value() != EBADR)
        {
            messages::internalError(asyncResp->res);
        }
        return;
    }

    auto& domainJson = asyncResp->res.jsonValue[domainPointer];
    domainJson["WriteProtected"] = writeProtected;
}

inline void getDomainProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const nlohmann::json::json_pointer& domainPointer,
    const sdbusplus::message::object_path& domainPath,
    const std::string& domainService)
{
    dbus::utility::getProperty<bool>(
        domainService, domainPath, "com.nvidia.Software.WriteProtection",
        "WriteProtected",
        std::bind_front(afterGetWriteProtection, asyncResp, domainPointer));
    sdbusplus::message::object_path writeProtects(
        domainPath / "write_protects");
    dbus::utility::getAssociationEndPoints(
        writeProtects, std::bind_front(afterGetAssociatedProtectedComponents,
                                       asyncResp, domainPointer));
}

inline void makeDefaultDomainJson(
    nlohmann::json& object, const std::string& chassisId, uint16_t domainId)
{
    object["@odata.id"] = boost::urls::format(
        "/redfish/v1/Chassis/{}/Oem/Nvidia/WriteProtectDomains/{}", chassisId,
        domainId);
    object["@odata.type"] =
        "#NvidiaWriteProtectDomain.v1_0_0.NvidiaWriteProtectDomain";
    object["Id"] = std::to_string(domainId);
    object["Name"] = std::format("Nvidia Write Protect Domain {}", domainId);
    object["WriteProtected"] = nullptr;
    object["AffectedComponents"] = nlohmann::json::array();
}

inline std::optional<uint16_t> parseDomainId(std::string_view domainIdStr)
{
    uint16_t domainId = 0;
    std::from_chars_result res =
        std::from_chars(domainIdStr.begin(), domainIdStr.end(), domainId);
    if (res.ec != std::errc{} || res.ptr != domainIdStr.end())
    {
        return {};
    }
    return domainId;
}

} // namespace write_protect_domains
} // namespace redfish
