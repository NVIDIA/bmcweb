// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "async_resp.hpp"
#include "dbus_privileges.hpp"
#include "dbus_utility.hpp"
#include "http_request.hpp"
#include "routing/baserule.hpp"
#include "sessions.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace crow
{
namespace
{

TEST(HandleRequestUserInfo, NoError)
{
    const std::shared_ptr<bmcweb::AsyncResp> asyncResp =
        std::make_shared<bmcweb::AsyncResp>();
    const boost::system::error_code ec = {};
    bool called = false;

    handleRequestUserInfo(
        asyncResp, ec,
        [&called](const dbus::utility::DBusPropertiesMap& /* userInfoMap */) {
            called = true;
        },
        {});
    EXPECT_TRUE(called);
}

TEST(HandleRequestUserInfo, GenericError)
{
    const std::shared_ptr<bmcweb::AsyncResp> asyncResp =
        std::make_shared<bmcweb::AsyncResp>();
    const boost::system::error_code ec = {1, boost::system::system_category()};
    bool called = false;
    handleRequestUserInfo(
        asyncResp, ec,
        [&called](const dbus::utility::DBusPropertiesMap& /* userInfoMap */) {
            called = true;
        },
        {});
    EXPECT_FALSE(called);
    EXPECT_EQ(asyncResp->res.resultInt(), 500);
}

TEST(HandleRequestUserInfo, UserManagerUnreachableError)
{
    const std::shared_ptr<bmcweb::AsyncResp> asyncResp =
        std::make_shared<bmcweb::AsyncResp>();
    const boost::system::error_code ec = {boost::system::errc::host_unreachable,
                                          boost::system::system_category()};

    bool called = false;
    handleRequestUserInfo(
        asyncResp, ec,
        [&called](const dbus::utility::DBusPropertiesMap& /* userInfoMap */) {
            called = true;
        },
        {});
    EXPECT_FALSE(called);
    EXPECT_EQ(asyncResp->res.resultInt(), 500);
}

TEST(HandleRequestUserInfo, UnauthorizedError)
{
    const std::shared_ptr<bmcweb::AsyncResp> asyncResp =
        std::make_shared<bmcweb::AsyncResp>();
    const boost::system::error_code ec = {boost::system::errc::io_error,
                                          boost::system::system_category()};

    bool called = false;
    handleRequestUserInfo(
        asyncResp, ec,
        [&called](const dbus::utility::DBusPropertiesMap& /* userInfoMap */) {
            called = true;
        },
        {});
    EXPECT_FALSE(called);
    EXPECT_EQ(asyncResp->res.resultInt(), 401);
}
dbus::utility::DBusPropertiesMap makeUserInfo(bool enabled, bool locked,
                                              bool remote = false)
{
    dbus::utility::DBusPropertiesMap map;
    map.emplace_back("UserPrivilege", std::string("priv-admin"));
    map.emplace_back("RemoteUser", remote);
    map.emplace_back("UserPasswordExpired", false);
    map.emplace_back("UserGroups",
                     std::vector<std::string>{"redfish-hostiface"});
    map.emplace_back("UserEnabled", enabled);
    map.emplace_back("UserLockedForFailedAttempt", locked);
    return map;
}

persistent_data::UserSession makeSession(persistent_data::SessionType type)
{
    persistent_data::UserSession session;
    session.username = "secadm";
    session.sessionType = type;
    return session;
}

TEST(PopulateUserInfo, EnabledUnlockedOk)
{
    persistent_data::UserSession session =
        makeSession(persistent_data::SessionType::MutualTLS);
    EXPECT_EQ(populateUserInfo(session, makeUserInfo(true, false)),
              UserInfoResult::Ok);
    EXPECT_EQ(session.userRole, "priv-admin");
}

TEST(PopulateUserInfo, DisabledRejectedForAllSessionTypes)
{
    for (persistent_data::SessionType type :
         {persistent_data::SessionType::MutualTLS,
          persistent_data::SessionType::Session,
          persistent_data::SessionType::Cookie,
          persistent_data::SessionType::Basic})
    {
        persistent_data::UserSession session = makeSession(type);
        EXPECT_EQ(populateUserInfo(session, makeUserInfo(false, false)),
                  UserInfoResult::AccountDisabled);
    }
}

TEST(PopulateUserInfo, LockedRejectedForMutualTls)
{
    persistent_data::UserSession session =
        makeSession(persistent_data::SessionType::MutualTLS);
    EXPECT_EQ(populateUserInfo(session, makeUserInfo(true, true)),
              UserInfoResult::AccountDisabled);
}

TEST(PopulateUserInfo, LockedPreOpenSessionSurvives)
{
    persistent_data::UserSession session =
        makeSession(persistent_data::SessionType::Session);
    EXPECT_EQ(populateUserInfo(session, makeUserInfo(true, true)),
              UserInfoResult::Ok);
}

TEST(PopulateUserInfo, MissingEnabledPropertyIsNotRejected)
{
    dbus::utility::DBusPropertiesMap map = makeUserInfo(true, false);
    map.erase(map.begin() + 4, map.end());
    persistent_data::UserSession session =
        makeSession(persistent_data::SessionType::MutualTLS);
    EXPECT_EQ(populateUserInfo(session, map), UserInfoResult::Ok);
}

TEST(AfterGetUserInfo, DisabledUserGets401)
{
    const std::shared_ptr<bmcweb::AsyncResp> asyncResp =
        std::make_shared<bmcweb::AsyncResp>();
    bool called = false;
    auto session = std::make_shared<persistent_data::UserSession>(
        makeSession(persistent_data::SessionType::MutualTLS));
    std::move_only_function<void()> callback = [&called]() { called = true; };
    afterGetUserInfo(asyncResp, session, callback, makeUserInfo(false, false));
    EXPECT_FALSE(called);
    EXPECT_EQ(asyncResp->res.resultInt(), 401);
}

class StubRule : public BaseRule
{
  public:
    StubRule() : BaseRule("/stub") {}
    void validate() override {}
    void handle(Request& /*req*/, const std::shared_ptr<bmcweb::AsyncResp>&,
                const std::vector<std::string>& /*params*/) override {}
};

TEST(AfterGetUserInfoValidate, DisabledUserGets401)
{
    const std::shared_ptr<bmcweb::AsyncResp> asyncResp =
        std::make_shared<bmcweb::AsyncResp>();
    Request req;
    req.session = std::make_shared<persistent_data::UserSession>(
        makeSession(persistent_data::SessionType::MutualTLS));
    StubRule rule;
    EXPECT_FALSE(afterGetUserInfoValidate(req, asyncResp, rule,
                                          makeUserInfo(false, false)));
    EXPECT_EQ(asyncResp->res.resultInt(), 401);
}
} // namespace
} // namespace crow
