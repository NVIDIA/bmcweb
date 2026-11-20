// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#include "app.hpp"
#include "async_resp.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "redfish_v1.hpp"
#include "sessions.hpp"

#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>

#include <memory>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

crow::Request makeRequest(boost::beast::http::verb method,
                          const std::string& role)
{
    std::error_code ec;
    crow::Request req("{}", ec);
    req.method(method);
    EXPECT_TRUE(req.target(
        "/redfish/v1/Systems/HGX_Baseboard_0/LogServices/FDR/Actions/LogService.ClearLog"));
    if (!role.empty())
    {
        auto session = std::make_shared<persistent_data::UserSession>();
        session->username = "user";
        session->userRole = role;
        req.session = session;
    }
    return req;
}

TEST(FallbackRequestAllowed, ReadsAllowedForReadOnly)
{
    for (auto method :
         {boost::beast::http::verb::get, boost::beast::http::verb::head})
    {
        auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
        EXPECT_TRUE(fallbackRequestAllowed(makeRequest(method, "priv-user"),
                                           asyncResp));
    }
}

TEST(FallbackRequestAllowed, WritesDeniedForReadOnlyAndOperator)
{
    for (const char* role : {"priv-user", "priv-operator"})
    {
        for (auto method :
             {boost::beast::http::verb::post, boost::beast::http::verb::patch,
              boost::beast::http::verb::put, boost::beast::http::verb::delete_})
        {
            auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
            EXPECT_FALSE(
                fallbackRequestAllowed(makeRequest(method, role), asyncResp));
            EXPECT_EQ(asyncResp->res.result(),
                      boost::beast::http::status::forbidden);
        }
    }
}

TEST(FallbackRequestAllowed, WritesAllowedForAdministrator)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    EXPECT_TRUE(fallbackRequestAllowed(
        makeRequest(boost::beast::http::verb::post, "priv-admin"), asyncResp));
}

TEST(FallbackRequestAllowed, NoSessionKeepsCurrentBehavior)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    EXPECT_TRUE(fallbackRequestAllowed(
        makeRequest(boost::beast::http::verb::post, ""), asyncResp));
}

TEST(RedfishFallbacks, ReadOnlyWriteIsRejectedBeforeForwarding)
{
    App app;
    crow::Request req =
        makeRequest(boost::beast::http::verb::post, "priv-user");

    auto notFound = std::make_shared<bmcweb::AsyncResp>();
    redfish404(app, req, notFound, "unused");
    EXPECT_EQ(notFound->res.result(), boost::beast::http::status::forbidden);

    auto notAllowed = std::make_shared<bmcweb::AsyncResp>();
    redfish405(app, req, notAllowed, "unused");
    EXPECT_EQ(notAllowed->res.result(), boost::beast::http::status::forbidden);
}

} // namespace
} // namespace redfish
