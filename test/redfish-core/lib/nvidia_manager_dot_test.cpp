// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

#include "async_resp.hpp"
#include "http_response.hpp"
#include "nvidia_manager_dot.hpp"

#include <boost/beast/http/status.hpp>
#include <boost/system/errc.hpp>

#include <memory>
#include <string>

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
    EXPECT_EQ(asyncResp->res.jsonValue["MessageId"], "Base.1.19.Success");
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

} // namespace
} // namespace redfish
