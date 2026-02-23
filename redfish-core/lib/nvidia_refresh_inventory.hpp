#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"

#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>

#include <memory>
#include <string>

namespace redfish
{

inline void afterRefreshInventoryGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec || subtree.empty())
    {
        return;
    }

    for (const auto& [path, serviceMap] : subtree)
    {
        if (path.find("InventoryData") == std::string::npos)
        {
            continue;
        }
        asyncResp->res.jsonValue["Actions"]["Oem"]
                                ["#NvidiaComputerSystem.RefreshInventory"]
                                ["target"] = boost::urls::format(
            "/redfish/v1/Systems/{}/Actions/Oem/NvidiaComputerSystem.RefreshInventory",
            BMCWEB_REDFISH_SYSTEM_URI_NAME);
        return;
    }
}

inline void handleRefreshInventoryGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Control.Trigger"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/control", 0, interfaces,
        std::bind_front(afterRefreshInventoryGet, asyncResp));
}

inline void afterRefreshInventorySubtree(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("D-Bus error getting Trigger subtree: {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    bool foundMatch = false;
    for (const auto& [path, serviceMap] : subtree)
    {
        if (path.find("InventoryData") == std::string::npos)
        {
            continue;
        }
        if (serviceMap.empty())
        {
            continue;
        }

        foundMatch = true;
        const std::string& service = serviceMap[0].first;

        dbus::utility::async_method_call(
            [asyncResp, path](const boost::system::error_code& ec2) {
                if (ec2)
                {
                    BMCWEB_LOG_ERROR("Failed to set Refresh on {}: {}", path,
                                     ec2);
                    messages::internalError(asyncResp->res);
                    return;
                }
                BMCWEB_LOG_DEBUG("Refresh triggered on {}", path);
            },
            service, path, "org.freedesktop.DBus.Properties", "Set",
            "xyz.openbmc_project.Control.Trigger", "Refresh",
            dbus::utility::DbusVariantType(true));
    }

    if (!foundMatch)
    {
        messages::resourceNotFound(asyncResp->res, "Action",
                                   "RefreshInventory");
    }
}

inline void handleRefreshInventoryAction(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    constexpr std::array<std::string_view, 1> interfaces = {
        "xyz.openbmc_project.Control.Trigger"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/control", 0, interfaces,
        std::bind_front(afterRefreshInventorySubtree, asyncResp));
}

inline void requestRoutesRefreshInventory(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Actions/Oem/NvidiaComputerSystem.RefreshInventory")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleRefreshInventoryAction, std::ref(app)));
}

} // namespace redfish
