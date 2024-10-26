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

inline
void handle_nvidia_resolution(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                                   const std::optional<std::string>& password)
{
    size_t maxrepeat = 3;
    if (!password.value().empty())
    {
        maxrepeat = static_cast<size_t>(
            3 + (0.09 * static_cast<double>(
                            strlen(password.value().c_str()))));
    }
    std::string resolution =
        " Password should be " + std::to_string(minPasswordLength) +
        " character long including " +
        std::to_string(minUcaseCharacters) +
        " uppercase character, " +
        std::to_string(minLcaseCharacters) +
        " lower case character, " + std::to_string(minDigits) +
        " digit, " + std::to_string(minSpecCharacters) +
        " special character and " + std::to_string(maxrepeat) +
        " maximum number of consecutive character pairs";

    // update the resolution message and add the password
    // policy
    redfish::message_registries::updateResolution(
        asyncResp, "Password", resolution);
}

inline
void handle_nvidia_delete_error(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
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
            messages::resourceNotFound(
                asyncResp->res, "#ManagerAccount.v1_4_0.ManagerAccount",
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