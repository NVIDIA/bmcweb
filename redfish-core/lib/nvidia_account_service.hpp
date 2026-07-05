#pragma once

#include "app.hpp"
#include "boost_formatters.hpp"
#include "certificate_service.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/account_service.hpp"
#include "nvidia_error_messages.hpp"
#include "persistent_data.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "sessions.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"

#include <boost/url/format.hpp>
#include <boost/url/url.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/privilege_utils.hpp>
#include <utils/registry_utils.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
namespace redfish
{

inline bool isUserPrivilege(const dbus::utility::DBusInterfacesMap& interfaces,
                            const std::string& privilege)
{
    for (const auto& [iface, props] : interfaces)
    {
        if (iface != "xyz.openbmc_project.User.Attributes")
        {
            continue;
        }
        auto it = std::ranges::find_if(props, [](const auto& p) {
            return p.first == "UserPrivilege";
        });
        if (it == props.end())
        {
            return false;
        }
        const auto* priv = std::get_if<std::string>(&it->second);
        return priv != nullptr && *priv == privilege;
    }
    return false;
}

inline bool addServiceAccountTypes(std::string_view userGroup,
                                   std::vector<std::string>& accountTypes)
{
    if (userGroup != "service")
    {
        return false;
    }

    // The 'service' group marks the built-in service account.
    // It is provisioned for Redfish and WebUI but holds NoAccess role,
    // so it cannot authenticate despite appearing in those interfaces.
    accountTypes.emplace_back("Redfish");
    accountTypes.emplace_back("WebUI");
    return true;
}

inline void handleNvidiaResolution(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::optional<std::string>& password)
{
    if (!password)
    {
        messages::internalError(asyncResp->res);
        return;
    }
    redfish::message_registries::updateResolution(asyncResp, *password,
                                                  "resolution");
}

inline void handleNvidiaDeleteError(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& username, sdbusplus::message::message& m)
{
    const sd_bus_error* dbusError = m.get_error();
    if ((dbusError != nullptr) && (dbusError->name != nullptr))
    {
        if (strcmp(dbusError->name,
                   "xyz.openbmc_project.Common.Error.NotAllowed") == 0)
        {
            messages::resourceCannotBeDeleted(
                asyncResp->res, "#ManagerAccount.v1_4_0.ManagerAccount",
                username);
        }
        else if (strcmp(dbusError->name,
                        "org.freedesktop.DBus.Error.UnknownObject") == 0)
        {
            messages::resourceNotFound(asyncResp->res,
                                       "#ManagerAccount.v1_4_0.ManagerAccount",
                                       username);
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
inline void handleNvidiaBootstrapSelfDelete(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& username)
{
    sdbusplus::object_path userObjPath("/xyz/openbmc_project/user/");
    userObjPath /= username;
    const std::string userPath(userObjPath);

    privilege_utils::isBiosPrivilege(
        username,
        [asyncResp, username,
         userPath](const boost::system::error_code& ec, const bool isBios) {
            if (ec || !isBios)
            {
                messages::operationNotAllowed(asyncResp->res);
                return;
            }
            dbus::utility::async_method_call(
                asyncResp,
                [asyncResp, username](const boost::system::error_code& ec2,
                                      sdbusplus::message::message& m) {
                    if (ec2)
                    {
                        handleNvidiaDeleteError(asyncResp, username, m);
                        return;
                    }
                    messages::accountRemoved(asyncResp->res);
                },
                "xyz.openbmc_project.User.Manager", userPath,
                "xyz.openbmc_project.Object.Delete", "Delete");
        });
}

} // namespace redfish
