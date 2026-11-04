// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "logging.hpp"
#include "privileges.hpp"
#include "routing/baserule.hpp"
#include "sessions.hpp"
#include "utils/dbus_utils.hpp"

#include <boost/beast/http/status.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace crow
{
enum class UserInfoResult
{
    Ok,
    // User info could not be read or was malformed.
    Error,
    // The account is disabled, or locked for a new mTLS login.
    AccountDisabled,
};

// Populate session with user information.
inline UserInfoResult populateUserInfo(
    persistent_data::UserSession& session,
    const dbus::utility::DBusPropertiesMap& userInfoMap)
{
    std::string userRole;
    bool remoteUser = false;
    std::optional<bool> passwordExpired;
    std::optional<std::vector<std::string>> userGroups;
    std::optional<bool> userEnabled;
    std::optional<bool> userLocked;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        redfish::dbus_utils::UnpackErrorPrinter(), userInfoMap, "UserPrivilege",
        userRole, "RemoteUser", remoteUser, "UserPasswordExpired",
        passwordExpired, "UserGroups", userGroups, "UserEnabled", userEnabled,
        "UserLockedForFailedAttempt", userLocked);

    if (!success)
    {
        BMCWEB_LOG_ERROR("Failed to unpack user properties.");
        return UserInfoResult::Error;
    }

    if (!remoteUser && (!passwordExpired || !userGroups))
    {
        BMCWEB_LOG_ERROR(
            "Missing UserPasswordExpired or UserGroups property for local user");
        return UserInfoResult::Error;
    }

    // Password, token and cookie auth reject disabled accounts elsewhere
    // (PAM and session purging).  mTLS authenticates from the certificate
    // alone, so enforce the account state here, which every request passes
    // through.  Remote users do not expose these properties.
    if (!remoteUser && userEnabled && !*userEnabled)
    {
        BMCWEB_LOG_WARNING("Rejecting request from disabled user {}",
                           session.username);
        return UserInfoResult::AccountDisabled;
    }
    // Sessions opened before a lockout are allowed to survive, so only mTLS
    // logins, which have no other credential check, are refused when locked.
    if (!remoteUser &&
        session.sessionType == persistent_data::SessionType::MutualTLS &&
        userLocked && *userLocked)
    {
        BMCWEB_LOG_WARNING("Rejecting mTLS request from locked user {}",
                           session.username);
        return UserInfoResult::AccountDisabled;
    }

    session.userRole = userRole;
    BMCWEB_LOG_DEBUG("userName = {} userRole = {}", session.username, userRole);

    // Set isConfigureSelfOnly based on D-Bus results.  This
    // ignores the results from both pamAuthenticateUser and the
    // value from any previous use of this session.
    session.isConfigureSelfOnly = passwordExpired.value_or(false);

    session.userGroups.clear();
    if (userGroups)
    {
        session.userGroups.swap(*userGroups);
    }

    return UserInfoResult::Ok;
}

inline bool isUserPrivileged(
    Request& req, const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    BaseRule& rule)
{
    if (req.session == nullptr)
    {
        return false;
    }
    // Get the user's privileges from the role
    redfish::Privileges userPrivileges =
        redfish::getUserPrivileges(*req.session);

    // Modify privileges if isConfigureSelfOnly.
    if (req.session->isConfigureSelfOnly)
    {
        // Remove all privileges except ConfigureSelf
        userPrivileges =
            userPrivileges.intersection(redfish::Privileges{"ConfigureSelf"});
        BMCWEB_LOG_DEBUG("Operation limited to ConfigureSelf");
    }

    if (!rule.checkPrivileges(userPrivileges))
    {
        asyncResp->res.result(boost::beast::http::status::forbidden);
        if (req.session->isConfigureSelfOnly)
        {
            redfish::messages::passwordChangeRequired(
                asyncResp->res,
                boost::urls::format("/redfish/v1/AccountService/Accounts/{}",
                                    req.session->username));
        }
        return false;
    }

    req.userRole = req.session->userRole;
    return true;
}

inline bool afterGetUserInfoValidate(
    Request& req, const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    BaseRule& rule, const dbus::utility::DBusPropertiesMap& userInfoMap)
{
    if (req.session == nullptr)
    {
        BMCWEB_LOG_ERROR("Failed to populate user information");
        asyncResp->res.result(
            boost::beast::http::status::internal_server_error);
        return false;
    }
    UserInfoResult result = populateUserInfo(*req.session, userInfoMap);
    if (result == UserInfoResult::AccountDisabled)
    {
        asyncResp->res.result(boost::beast::http::status::unauthorized);
        return false;
    }
    if (result != UserInfoResult::Ok)
    {
        BMCWEB_LOG_ERROR("Failed to populate user information");
        asyncResp->res.result(
            boost::beast::http::status::internal_server_error);
        return false;
    }

    if (!isUserPrivileged(req, asyncResp, rule))
    {
        // User is not privileged
        BMCWEB_LOG_ERROR("Insufficient Privilege");
        asyncResp->res.result(boost::beast::http::status::forbidden);
        return false;
    }

    return true;
}

inline void handleRequestUserInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    std::move_only_function<void(const dbus::utility::DBusPropertiesMap&)>
        callback,
    const dbus::utility::DBusPropertiesMap& userInfoMap,
    std::move_only_function<void()> onValidationDone = {})
{
    if (ec)
    {
        BMCWEB_LOG_WARNING("GetUserInfo failed with error of {}", ec);
        if (ec.value() == boost::system::errc::io_error)
        {
            BMCWEB_LOG_WARNING(
                "There is io error when calling the user manager service, this suggests the user doesn't have permission to access");
            asyncResp->res.result(boost::beast::http::status::unauthorized);
        }
        else if (ec.value() == boost::system::errc::host_unreachable)
        {
            BMCWEB_LOG_ERROR(
                "User manager service not reachable, this suggests the user manager service is not healthy");
            asyncResp->res.result(
                boost::beast::http::status::internal_server_error);
        }
        else
        {
            BMCWEB_LOG_ERROR("Unhandled error code {} for GetUserInfo",
                             ec.value());
            asyncResp->res.result(
                boost::beast::http::status::internal_server_error);
        }
        if (onValidationDone)
        {
            onValidationDone();
        }
        return;
    }
    callback(userInfoMap);
    if (onValidationDone)
    {
        onValidationDone();
    }
}

inline void requestUserInfo(
    const std::string& username,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::move_only_function<void(const dbus::utility::DBusPropertiesMap&)>&&
        callback,
    std::move_only_function<void()>&& onValidationDone = {})
{
    dbus::utility::async_method_call(
        asyncResp,
        [asyncResp, callback = std::move(callback),
         onValidationDone = std::move(onValidationDone)](
            const boost::system::error_code& ec,
            const dbus::utility::DBusPropertiesMap& userInfoMap) mutable {
            handleRequestUserInfo(asyncResp, ec, std::move(callback),
                                  userInfoMap, std::move(onValidationDone));
        },
        "xyz.openbmc_project.User.Manager", "/xyz/openbmc_project/user",
        "xyz.openbmc_project.User.Manager", "GetUserInfo", username);
}

inline void validatePrivilege(
    const std::shared_ptr<Request>& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, BaseRule& rule,
    std::move_only_function<void()>&& callback,
    std::move_only_function<void()>&& onValidationDone = {})
{
    if (req->session == nullptr)
    {
        if (onValidationDone)
        {
            onValidationDone();
        }
        return;
    }

    requestUserInfo(
        req->session->username, asyncResp,
        [req, asyncResp, &rule, callback = std::move(callback)](
            const dbus::utility::DBusPropertiesMap& userInfoMap) mutable {
            if (afterGetUserInfoValidate(*req, asyncResp, rule, userInfoMap))
            {
                callback();
            }
        },
        std::move(onValidationDone));
}

inline void afterGetUserInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<persistent_data::UserSession>& session,
    std::move_only_function<void()>& callback,
    const dbus::utility::DBusPropertiesMap& userInfoMap)
{
    UserInfoResult result = populateUserInfo(*session, userInfoMap);
    if (result == UserInfoResult::AccountDisabled)
    {
        asyncResp->res.result(boost::beast::http::status::unauthorized);
        return;
    }
    if (result != UserInfoResult::Ok)
    {
        BMCWEB_LOG_ERROR("Failed to populate user information");
        asyncResp->res.result(
            boost::beast::http::status::internal_server_error);
        return;
    }
    callback();
}

inline void getUserInfo(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& username,
                        std::shared_ptr<persistent_data::UserSession>& session,
                        std::move_only_function<void()>&& callback)
{
    requestUserInfo(
        username, asyncResp,
        [asyncResp, session, callback = std::move(callback)](
            const dbus::utility::DBusPropertiesMap& userInfoMap) mutable {
            afterGetUserInfo(asyncResp, session, callback, userInfoMap);
        });
}

} // namespace crow
