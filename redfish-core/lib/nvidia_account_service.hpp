#pragma once

#include "app.hpp"
#include "boost_formatters.hpp"
#include "certificate_service.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/account_service.hpp"
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
#include <utils/registry_utils.hpp>

#include <array>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
namespace redfish
{

inline void handle_nvidia_resolution(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::optional<std::string> password)
{
    redfish::message_registries::updateResolution(asyncResp, *password,
                                                  "resolution");
}

inline void handle_nvidia_delete_error(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& username, sdbusplus::message::message& m)
{
    const sd_bus_error* dbusError = m.get_error();
    if (dbusError && dbusError->name)
    {
        if (!strcmp(dbusError->name,
                    "xyz.openbmc_project.Common.Error.NotAllowed"))
        {
            messages::resourceCannotBeDeleted(
                asyncResp->res, "#ManagerAccount.v1_4_0.ManagerAccount",
                username);
        }
        else if (!strcmp(dbusError->name,
                         "org.freedesktop.DBus.Error.UnknownObject"))
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
} // namespace redfish
