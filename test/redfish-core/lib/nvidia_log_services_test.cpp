// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.

#include "async_resp.hpp"
#include "nvidia_log_services.hpp"

#include <boost/beast/http/status.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

using CreateDumpParams =
    std::vector<std::pair<std::string, std::variant<std::string, uint64_t>>>;

TEST(ParseBmcOemDumpRequest, MissingOemTypeIsBadRequest)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    CreateDumpParams params;
    std::string requested;
    EXPECT_FALSE(
        parseBmcOemDumpRequest(asyncResp, std::nullopt, params, requested));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
    EXPECT_TRUE(params.empty());
    EXPECT_TRUE(requested.empty());
}

TEST(ParseBmcOemDumpRequest, NoDiagnosticTypeIsBadRequest)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    CreateDumpParams params;
    std::string requested;
    EXPECT_FALSE(parseBmcOemDumpRequest(
        asyncResp, std::string("Duration=300;SamplingRate=1"), params,
        requested));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

// An empty DiagnosticType must not reach the dump manager, which would
// run a standard dump from the forwarded parameters instead.
TEST(ParseBmcOemDumpRequest, EmptyDiagnosticTypeIsBadRequest)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    CreateDumpParams params;
    std::string requested;
    EXPECT_FALSE(parseBmcOemDumpRequest(
        asyncResp, std::string("DiagnosticType=;Duration=300;SamplingRate=1"),
        params, requested));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

// The first DiagnosticType decides, as it does in the dump manager, which
// keeps the first copy of a repeated create parameter.
TEST(ParseBmcOemDumpRequest, FirstDiagnosticTypeDecides)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    CreateDumpParams params;
    std::string requested;
    EXPECT_FALSE(parseBmcOemDumpRequest(
        asyncResp,
        std::string("DiagnosticType=;DiagnosticType=ContainerRuntimeMetrics"),
        params, requested));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

TEST(ParseBmcOemDumpRequest, CompositeIsForwarded)
{
    const std::string request =
        "DiagnosticType=ContainerRuntimeMetrics;Duration=300;SamplingRate=1";
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    CreateDumpParams params;
    std::string requested;
    EXPECT_TRUE(parseBmcOemDumpRequest(asyncResp, request, params, requested));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
    EXPECT_EQ(requested, request);
    ASSERT_EQ(params.size(), 3U);
    EXPECT_EQ(params[0].first, "DiagnosticType");
    EXPECT_EQ(std::get<std::string>(params[0].second),
              "ContainerRuntimeMetrics");
    EXPECT_EQ(params[1].first, "Duration");
    EXPECT_EQ(std::get<std::string>(params[1].second), "300");
    EXPECT_EQ(params[2].first, "SamplingRate");
    EXPECT_EQ(std::get<std::string>(params[2].second), "1");
}

// The form the dump manager advertises for the Manager dump.
const std::vector<std::string> managerOemValues = {
    "DiagnosticType=ContainerRuntimeMetrics;Duration=300;SamplingRate=1",
    "DiagnosticType=ContainerRuntimeMetrics;Duration=600;SamplingRate=5"};

TEST(CheckBmcOemDiagnosticDataType, AdvertisedValueIsAccepted)
{
    for (const std::string& value : managerOemValues)
    {
        auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
        EXPECT_TRUE(
            checkBmcOemDiagnosticDataType(asyncResp, managerOemValues, value));
        EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
    }
}

// Each of these names an advertised collection, or carries create
// parameters of its own; only an exact advertised value is forwarded.
TEST(CheckBmcOemDiagnosticDataType, AnythingElseIsBadRequest)
{
    const std::string& advertised = managerOemValues[0];
    const std::string createParameter =
        "xyz.openbmc_project.Dump.Create.CreateParameters.";
    const std::string dumpType = "xyz.openbmc_project.Dump.Create.DumpType.";
    const std::vector<std::string> requests = {
        advertised + ";Garbage=x",
        "Duration=300;DiagnosticType=ContainerRuntimeMetrics;SamplingRate=1",
        advertised + ";junk",
        advertised + ";",
        advertised + ";" + createParameter + "OriginatorId=x",
        "DiagnosticType=;" + createParameter + "DumpType=" + dumpType +
            "ApplicationCored;" + createParameter + "FilePath=/any/file",
        "DiagnosticType=ContainerRuntimeMetrics;Duration=300;SamplingRate=5",
        "DiagnosticType=ContainerRuntimeMetrics",
        "ContainerRuntimeMetrics",
        "",
    };
    for (const std::string& request : requests)
    {
        auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
        EXPECT_FALSE(
            checkBmcOemDiagnosticDataType(asyncResp, managerOemValues, request))
            << request;
        EXPECT_EQ(asyncResp->res.result(),
                  boost::beast::http::status::bad_request)
            << request;
    }
}

// No advertised values (dump manager unreachable, or none configured):
// every OEM request is refused.
TEST(CheckBmcOemDiagnosticDataType, NoAdvertisedValuesRejectsAll)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    EXPECT_FALSE(
        checkBmcOemDiagnosticDataType(asyncResp, {}, managerOemValues[0]));
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

} // namespace
} // namespace redfish
