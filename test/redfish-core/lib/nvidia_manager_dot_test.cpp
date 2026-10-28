// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "nvidia_manager_dot.hpp"

#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

TEST(NvidiaManagerDotTest, ProcessCAKDeleteResponseSuccess)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::system::error_code ec;

    processCAKDeleteResponse(asyncResp, ec, "Success");

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
    EXPECT_EQ(asyncResp->res.jsonValue["@Message.ExtendedInfo"][0]["MessageId"],
              "Base.1.19.Success");
}

TEST(NvidiaManagerDotTest, ProcessCAKDeleteResponseDbusError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::system::error_code ec =
        boost::system::errc::make_error_code(boost::system::errc::timed_out);

    processCAKDeleteResponse(asyncResp, ec, "");

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

TEST(NvidiaManagerDotTest, ProcessCAKDeleteResponseActionError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::system::error_code ec;

    processCAKDeleteResponse(asyncResp, ec, "Error: delete failed");

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

TEST(NvidiaManagerDotTest, ProcessCAKDownloadResponseEmpty)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::system::error_code ec;

    processCAKDownloadResponse(asyncResp, ec, "");

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

TEST(NvidiaManagerDotTest, ProcessCAKDownloadResponseSuccess)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::system::error_code ec;
    const std::string payload = R"({"CAKKey":{"ECDSAKey":"abc"}})";

    processCAKDownloadResponse(asyncResp, ec, payload);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
    EXPECT_EQ(asyncResp->res.jsonValue["CAKValue"], payload);
}

TEST(NvidiaManagerDotTest, DotKeydPresentRunsHandler)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    bool ran = false;
    const dbus::utility::MapperGetObject object{
        {"xyz.openbmc_project.Security.DOT",
         {"xyz.openbmc_project.Security.DOT"}}};

    afterGetDotKeydObject(asyncResp, [&ran] { ran = true; }, {}, object);

    EXPECT_TRUE(ran);
}

TEST(NvidiaManagerDotTest, DotKeydAbsentIsNotFound)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    bool ran = false;

    afterGetDotKeydObject(
        asyncResp, [&ran] { ran = true; },
        boost::system::errc::make_error_code(boost::system::errc::io_error),
        {});

    EXPECT_FALSE(ran);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

TEST(NvidiaManagerDotTest, DotKeydMapperFailureIsInternalError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    bool ran = false;

    afterGetDotKeydObject(
        asyncResp, [&ran] { ran = true; },
        boost::system::errc::make_error_code(boost::system::errc::timed_out),
        {});

    EXPECT_FALSE(ran);
    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

crow::Request dotRequest(boost::beast::http::verb verb, const std::string& url)
{
    std::error_code ec;
    crow::Request req(crow::Request::Body{verb, url, 11, ""}, ec);
    return req;
}

void expectManagerNotFound(const bmcweb::AsyncResp& asyncResp)
{
    EXPECT_EQ(asyncResp.res.result(), boost::beast::http::status::not_found);
    const nlohmann::json& args =
        asyncResp.res
            .jsonValue["error"]["@Message.ExtendedInfo"][0]["MessageArgs"];
    EXPECT_EQ(args, nlohmann::json::array({"Manager", "bogus"}));
}

// The manager id is checked before the DOT backend lookup, so an invalid id
// gives the same error whether or not dot-keyd is present.
TEST(NvidiaManagerDotTest, InvalidManagerIdIsCheckedFirst)
{
    App app;
    {
        crow::Request req =
            dotRequest(boost::beast::http::verb::get,
                       "/redfish/v1/Managers/bogus/Oem/Nvidia/DOT");
        auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
        handleManagerDOTGetRequest(app, req, asyncResp, "bogus");
        expectManagerNotFound(*asyncResp);
    }
    {
        crow::Request req = dotRequest(
            boost::beast::http::verb::post,
            "/redfish/v1/Managers/bogus/Oem/Nvidia/DOT/Actions/NvidiaDOTManagement.CAKDelete");
        auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
        handleManagerDOTCAKDeleteRequest(app, req, asyncResp, "bogus");
        expectManagerNotFound(*asyncResp);
    }
    {
        crow::Request req =
            dotRequest(boost::beast::http::verb::get,
                       "/redfish/v1/Managers/bogus/Oem/Nvidia/DOT/CAKDownload");
        auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
        handleManagerDOTCAKDownloadRequest(app, req, asyncResp, "bogus");
        expectManagerNotFound(*asyncResp);
    }
}

} // namespace
} // namespace redfish
