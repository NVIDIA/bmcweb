// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION &
// AFFILIATES. All rights reserved.

#include "async_resp.hpp"
#include "http_response.hpp"
#include "nvidia_oem_l1reset.hpp"

#include <boost/beast/http/status.hpp>
#include <sdbusplus/message.hpp>

#include <memory>
#include <string_view>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

TEST(NvidiaOemL1ResetTest, HandleL1ResetErrorUnavailable)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    nvidia_oem_l1reset::handleL1ResetError(
        asyncResp, "xyz.openbmc_project.Common.Error.Unavailable");

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::service_unavailable);
}

TEST(NvidiaOemL1ResetTest, HandleL1ResetErrorInternalFailure)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    nvidia_oem_l1reset::handleL1ResetError(
        asyncResp, "xyz.openbmc_project.Common.Error.InternalFailure");

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

TEST(NvidiaOemL1ResetTest, HandleL1ResetErrorUnknown)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    nvidia_oem_l1reset::handleL1ResetError(
        asyncResp, "org.freedesktop.DBus.Error.UnknownMethod");

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

TEST(NvidiaOemL1ResetTest, HandleL1ResetErrorUnknownObject)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    nvidia_oem_l1reset::handleL1ResetError(
        asyncResp, "org.freedesktop.DBus.Error.UnknownObject");

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

TEST(NvidiaOemL1ResetTest, HandleL1ResetErrorTimeout)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    nvidia_oem_l1reset::handleL1ResetError(
        asyncResp, "xyz.openbmc_project.Common.Error.Timeout");

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::service_unavailable);
}

TEST(NvidiaOemL1ResetTest, HandleL1ResetResponseSuccess)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    boost::system::error_code ec;
    sdbusplus::message_t msg;
    nvidia_oem_l1reset::handleL1ResetResponse(asyncResp, ec, msg);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
}

} // namespace
} // namespace redfish
