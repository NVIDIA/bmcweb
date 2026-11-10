// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.

#include "async_resp.hpp"
#include "error_messages.hpp"
#include "http_response.hpp"
#include "nvidia_account_service.hpp"

#include <boost/beast/http/status.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

// Body of a PATCH request, as handleAccountPatch() passes it on
nlohmann::json::object_t patchBody(const nlohmann::json& body)
{
    return body.get<nlohmann::json::object_t>();
}

// True if `res` holds exactly one PropertyNotWritable message for `property`
bool hasPropertyNotWritable(const crow::Response& res,
                            const std::string& property)
{
    auto it = res.jsonValue.find(property + "@Message.ExtendedInfo");
    return it != res.jsonValue.end() &&
           *it ==
               nlohmann::json::array({messages::propertyNotWritable(property)});
}

TEST(ValidateServiceAccountPatch, OtherAccountsAreNotRestricted)
{
    for (const char* username : {"root", "admin", "service2", "Service"})
    {
        auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
        EXPECT_TRUE(validateServiceAccountPatch(
            asyncResp, username,
            patchBody({{"RoleId", "Administrator"},
                       {"Enabled", false},
                       {"UserName", "x"}})))
            << username;
        EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok)
            << username;
        EXPECT_TRUE(asyncResp->res.jsonValue.empty()) << username;
    }
}

TEST(ValidateServiceAccountPatch, PasswordOnlyIsAllowed)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    EXPECT_TRUE(
        validateServiceAccountPatch(asyncResp, serviceAccountName,
                                    patchBody({{"Password", "NewPassw0rd!"}})));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
    EXPECT_TRUE(asyncResp->res.jsonValue.empty());
}

TEST(ValidateServiceAccountPatch, EmptyBodyIsLeftToTheCaller)
{
    // An empty body is rejected earlier by readJsonPatchHelper()
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    EXPECT_TRUE(validateServiceAccountPatch(asyncResp, serviceAccountName,
                                            nlohmann::json::object_t{}));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
    EXPECT_TRUE(asyncResp->res.jsonValue.empty());
}

TEST(ValidateServiceAccountPatch, EachSupportedPropertyIsRejected)
{
    const nlohmann::json bodies = {
        {{"RoleId", "Administrator"}},
        {{"Enabled", false}},
        {{"Locked", false}},
        {{"AccountTypes", {"Redfish"}}},
        {{"PasswordExpiration", nullptr}},
        {{"UserName", "service2"}},
        {{"UserName", "service"}},
    };
    for (const nlohmann::json& body : bodies)
    {
        const std::string& property = body.begin().key();
        auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
        EXPECT_FALSE(validateServiceAccountPatch(asyncResp, serviceAccountName,
                                                 patchBody(body)))
            << body.dump();
        EXPECT_EQ(asyncResp->res.result(),
                  boost::beast::http::status::bad_request)
            << body.dump();
        EXPECT_TRUE(hasPropertyNotWritable(asyncResp->res, property))
            << body.dump() << " -> " << asyncResp->res.jsonValue.dump();
    }
}

TEST(ValidateServiceAccountPatch, PasswordWithOtherPropertyIsRejected)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    EXPECT_FALSE(validateServiceAccountPatch(
        asyncResp, serviceAccountName,
        patchBody(
            {{"Password", "NewPassw0rd!"}, {"RoleId", "Administrator"}})));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
    EXPECT_TRUE(hasPropertyNotWritable(asyncResp->res, "RoleId"));
    // Password itself is not reported
    EXPECT_FALSE(
        asyncResp->res.jsonValue.contains("Password@Message.ExtendedInfo"));
    EXPECT_EQ(asyncResp->res.jsonValue.size(), 1U);
}

TEST(ValidateServiceAccountPatch, EveryRejectedPropertyIsReported)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    EXPECT_FALSE(validateServiceAccountPatch(
        asyncResp, serviceAccountName,
        patchBody({{"RoleId", "Administrator"},
                   {"Enabled", false},
                   {"Locked", false}})));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
    EXPECT_TRUE(hasPropertyNotWritable(asyncResp->res, "RoleId"));
    EXPECT_TRUE(hasPropertyNotWritable(asyncResp->res, "Enabled"));
    EXPECT_TRUE(hasPropertyNotWritable(asyncResp->res, "Locked"));
    EXPECT_EQ(asyncResp->res.jsonValue.size(), 3U);
}

TEST(ValidateServiceAccountPatch, UnknownPropertiesAreLeftToTheCaller)
{
    // Unknown properties are left to handleAccountPatch(), which reports them
    // as PropertyUnknown, the same as for any other account. Id is answered by
    // the Id check before this helper.
    const nlohmann::json bodies = {
        {{"Foo", 1}},
        {{"Id", "foo"}},
        {{"Description", "x"}},
        {{"Password", "NewPassw0rd!"}, {"Foo", 1}},
    };
    for (const nlohmann::json& body : bodies)
    {
        auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
        EXPECT_TRUE(validateServiceAccountPatch(asyncResp, serviceAccountName,
                                                patchBody(body)))
            << body.dump();
        EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok)
            << body.dump();
        EXPECT_TRUE(asyncResp->res.jsonValue.empty()) << body.dump();
    }
}

TEST(ValidateServiceAccountPatch, UnknownPropertyIsNotReportedAsNotWritable)
{
    // handleAccountPatch() adds PropertyUnknown for Foo when it parses the body
    // afterwards
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    EXPECT_FALSE(validateServiceAccountPatch(
        asyncResp, serviceAccountName,
        patchBody({{"RoleId", "Administrator"}, {"Foo", 1}})));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
    EXPECT_TRUE(hasPropertyNotWritable(asyncResp->res, "RoleId"));
    EXPECT_FALSE(asyncResp->res.jsonValue.contains("Foo@Message.ExtendedInfo"));
    EXPECT_EQ(asyncResp->res.jsonValue.size(), 1U);
}

} // namespace
} // namespace redfish
