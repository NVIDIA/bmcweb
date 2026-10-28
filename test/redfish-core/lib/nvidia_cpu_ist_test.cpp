// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
// AFFILIATES. All rights reserved.

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "nvidia_cpu_ist.hpp"
#include "task.hpp"

#include <boost/beast/http/status.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>

#include <memory>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

const dbus::utility::MapperGetObject istObject{
    {"com.nvidia.vera.ist", {"xyz.openbmc_project.ist.Control"}}};

boost::system::error_code notFound()
{
    return boost::system::errc::make_error_code(boost::system::errc::io_error);
}

TEST(NvidiaCpuIstTest, SystemAdvertisesStartIstWhenPresent)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    nvidia_cpu_ist::afterGetIstObjectForSystem(asyncResp, {}, istObject);

    const nlohmann::json& action =
        asyncResp->res
            .jsonValue["Actions"]["Oem"]["#NvidiaComputerSystem.StartIST"];
    EXPECT_TRUE(action.contains("target"));
    EXPECT_TRUE(action.contains("@Redfish.ActionInfo"));
}

TEST(NvidiaCpuIstTest, SystemOmitsStartIstWhenAbsent)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    nvidia_cpu_ist::afterGetIstObjectForSystem(asyncResp, notFound(), {});
    nvidia_cpu_ist::afterGetIstObjectForSystem(asyncResp, {}, {});

    EXPECT_FALSE(asyncResp->res.jsonValue.contains("Actions"));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
}

TEST(NvidiaCpuIstTest, ActionInfoFilledWhenPresent)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    nvidia_cpu_ist::afterGetIstObjectForActionInfo(asyncResp, "System_0", {},
                                                   istObject);

    EXPECT_EQ(asyncResp->res.jsonValue["Id"], "StartISTActionInfo");
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
}

TEST(NvidiaCpuIstTest, ActionInfoNotFoundWhenAbsent)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    nvidia_cpu_ist::afterGetIstObjectForActionInfo(asyncResp, "System_0",
                                                   notFound(), {});

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

TEST(NvidiaCpuIstTest, ActionInfoMapperFailureIsInternalError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    nvidia_cpu_ist::afterGetIstObjectForActionInfo(
        asyncResp, "System_0",
        boost::system::errc::make_error_code(boost::system::errc::timed_out),
        {});

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

TEST(NvidiaCpuIstTest, StartNotFoundWhenAbsent)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    crow::Request req;

    nvidia_cpu_ist::afterGetIstObjectForStart(asyncResp, task::Payload(req), {},
                                              notFound(), {});

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
}

} // namespace
} // namespace redfish
