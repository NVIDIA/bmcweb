#include "account_service.hpp"
#include "http_response.hpp"

#include <boost/beast/http/status.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::Eq;
using ::testing::Optional;

namespace redfish
{
namespace
{

using json = nlohmann::json;

TEST(Conversion, PositiveToUint64)
{
    std::optional<uint64_t> passwordExpiration;

    passwordExpiration = passwordExpirationToUint64(nullptr);
    EXPECT_THAT(passwordExpiration, Optional(Eq(0)));

    passwordExpiration = passwordExpirationToUint64("1970-01-01T00:00:00");
    EXPECT_THAT(passwordExpiration, Eq(std::nullopt));

    passwordExpiration = passwordExpirationToUint64("2024-10-17T18:12:04");
    EXPECT_THAT(passwordExpiration, Optional(Eq(1729188724)));

    passwordExpiration = passwordExpirationToUint64("2024-10-17T18:12:04Z");
    EXPECT_THAT(passwordExpiration, Optional(Eq(1729188724)));

    passwordExpiration =
        passwordExpirationToUint64("2024-10-17T18:12:04+03:00");
    EXPECT_THAT(passwordExpiration, Optional(Eq(1729177924)));
}

TEST(Conversion, NegativeToUint64)
{
    EXPECT_EQ(passwordExpirationToUint64("01-02-03"), std::nullopt);

    EXPECT_EQ(passwordExpirationToUint64("1912-02-03"), std::nullopt);

    EXPECT_EQ(passwordExpirationToUint64("2024-10-17T00:00"), std::nullopt);

    EXPECT_EQ(passwordExpirationToUint64("ABC"), std::nullopt);
}

TEST(Conversion, PositiveToJson)
{
    constexpr uint64_t unexpiringPasswordExpiration = 0;

    json value = passwordExpirationToJson(unexpiringPasswordExpiration);
    EXPECT_TRUE(value.is_null());

    value = passwordExpirationToJson(1729188724);
    EXPECT_TRUE(value.is_string());
    EXPECT_EQ(value, R"("2024-10-17T18:12:04+00:00")"_json);
}

// The account ETag state is process wide, so every test uses its own user
// names. The handlers call these helpers on the following paths:
//   password PATCH   -> accountPasswordChanged(user)
//   account create   -> accountPasswordChanged(user)
//   account delete   -> accountRemoved(user)
//   account rename   -> accountRemoved(old); accountPasswordChanged(new)
//   account GET      -> setAccountEtagSalt(res, user)

// ETag of an account GET. The body is the same for every account, so any
// difference between two ETags comes from the salt.
std::string accountEtag(const std::string& user)
{
    crow::Response res;
    res.result(boost::beast::http::status::ok);
    res.jsonValue["Password"] = nullptr;
    res.jsonValue["Enabled"] = true;
    setAccountEtagSalt(res, user);
    return res.getCurrentEtag();
}

TEST(AccountEtag, StableWithoutChange)
{
    std::string first = accountEtag("etag_stable");
    EXPECT_FALSE(first.empty());
    EXPECT_EQ(accountEtag("etag_stable"), first);
}

TEST(AccountEtag, PasswordPatchChangesEtag)
{
    std::string before = accountEtag("etag_patch");
    accountPasswordChanged("etag_patch");
    std::string after = accountEtag("etag_patch");
    EXPECT_NE(after, before);
    // Stable until the next change
    EXPECT_EQ(accountEtag("etag_patch"), after);

    // Every further password change gives a new ETag
    accountPasswordChanged("etag_patch");
    std::string third = accountEtag("etag_patch");
    EXPECT_NE(third, after);
    EXPECT_NE(third, before);
}

TEST(AccountEtag, PasswordPatchDoesNotChangeOtherAccounts)
{
    std::string otherBefore = accountEtag("etag_other");
    accountPasswordChanged("etag_changed");
    EXPECT_EQ(accountEtag("etag_other"), otherBefore);
}

TEST(AccountEtag, CreateChangesEtag)
{
    // An account nobody has created yet has the default (unset) salt
    std::string unknown = accountEtag("etag_create");
    accountPasswordChanged("etag_create");
    EXPECT_NE(accountEtag("etag_create"), unknown);
}

TEST(AccountEtag, DeleteDropsStateAndRecreateNeverRepeatsEtag)
{
    std::string defaultEtag = accountEtag("etag_never_seen");

    accountPasswordChanged("etag_delete");
    std::string created = accountEtag("etag_delete");
    ASSERT_NE(created, defaultEtag);

    accountRemoved("etag_delete");
    // State is gone: back to the default salt
    EXPECT_EQ(accountEtag("etag_delete"), defaultEtag);

    // Same name created again must not get the ETag of the earlier account
    accountPasswordChanged("etag_delete");
    std::string recreated = accountEtag("etag_delete");
    EXPECT_NE(recreated, created);
    EXPECT_NE(recreated, defaultEtag);
}

TEST(AccountEtag, RenameMovesStateToNewName)
{
    std::string defaultEtag = accountEtag("etag_never_seen2");

    accountPasswordChanged("etag_old_name");
    std::string oldBefore = accountEtag("etag_old_name");
    ASSERT_NE(oldBefore, defaultEtag);

    // What handleAccountPatch does after a successful RenameUser
    accountRemoved("etag_old_name");
    accountPasswordChanged("etag_new_name");

    EXPECT_EQ(accountEtag("etag_old_name"), defaultEtag);
    EXPECT_NE(accountEtag("etag_new_name"), defaultEtag);
    EXPECT_NE(accountEtag("etag_new_name"), oldBefore);
}

TEST(AccountEtag, CounterOnlyMovesForward)
{
    AccountEtagState& state = getAccountEtagState();

    uint64_t start = state.counter;
    accountPasswordChanged("etag_counter");
    EXPECT_EQ(state.counter, start + 1);
    accountPasswordChanged("etag_counter");
    EXPECT_EQ(state.counter, start + 2);

    // Removing an account must not move the counter back, otherwise a value
    // that was handed out before could be handed out again
    accountRemoved("etag_counter");
    EXPECT_EQ(state.counter, start + 2);
    accountPasswordChanged("etag_counter");
    EXPECT_EQ(state.counter, start + 3);
    EXPECT_EQ(state.passwordChange["etag_counter"], start + 3);
}

TEST(AccountEtag, RemoveUnknownAccountIsHarmless)
{
    accountRemoved("etag_unknown_account");
    EXPECT_FALSE(accountEtag("etag_unknown_account").empty());
}

} // namespace
} // namespace redfish
