// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
// AFFILIATES. All rights reserved.

#include "async_resp.hpp"
#include "http_response.hpp"
#include "nvidia_pcore_dump.hpp"
#include "utils/nvidia_dump_utils.hpp"

#include <boost/beast/http/status.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

using nvidia_dump_utils::mapCreateDumpDbusError;
using nvidia_pcore_dump::buildCollectPCoreDumpActionInfo;
using nvidia_pcore_dump::buildPCoreCreateDumpParams;
using nvidia_pcore_dump::buildPCoreDumpAdvertisement;
using nvidia_pcore_dump::collectPCoreDumpActionName;
using nvidia_pcore_dump::firstOutOfRange;
using nvidia_pcore_dump::formatPCoreIds;
using nvidia_pcore_dump::normalizePCoreIds;
using nvidia_pcore_dump::PCoreIdRange;

// Pulls MessageArgs out of the first extended-info entry of an error response.
nlohmann::json firstMessageArgs(const crow::Response& res)
{
    return res.jsonValue["error"]["@Message.ExtendedInfo"][0]["MessageArgs"];
}

// Reads a string-valued CreateDump parameter, or "" when the key is absent.
std::string paramValue(const nvidia_dump_utils::DumpCreateParams& params,
                       std::string_view key)
{
    for (const auto& [name, value] : params)
    {
        if (name == key)
        {
            const std::string* asString = std::get_if<std::string>(&value);
            return asString == nullptr ? std::string{} : *asString;
        }
    }
    return {};
}

bool hasParam(const nvidia_dump_utils::DumpCreateParams& params,
              std::string_view key)
{
    for (const auto& [name, value] : params)
    {
        if (name == key)
        {
            return true;
        }
    }
    return false;
}

TEST(NvidiaPCoreDumpTest, MapCreateDumpDbusErrorQuotaExceeded)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    mapCreateDumpDbusError(
        asyncResp, "xyz.openbmc_project.Dump.Create.Error.QuotaExceeded",
        collectPCoreDumpActionName, "PCoreIds");

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
}

// A collection already in flight must read as 503, not as a bad request.
TEST(NvidiaPCoreDumpTest, MapCreateDumpDbusErrorUnavailableIsResourceInUse)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    mapCreateDumpDbusError(asyncResp,
                           "xyz.openbmc_project.Common.Error.Unavailable",
                           collectPCoreDumpActionName, "PCoreIds");

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::service_unavailable);
}

// The generic dump path maps InvalidArgument onto DiagnosticType, dereferencing
// an OEMDiagnosticDataType optional this action never populates. It must name
// PCoreIds instead.
TEST(NvidiaPCoreDumpTest, MapCreateDumpDbusErrorInvalidArgumentNamesPCoreIds)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    mapCreateDumpDbusError(asyncResp,
                           "xyz.openbmc_project.Common.Error.InvalidArgument",
                           collectPCoreDumpActionName, "PCoreIds");

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::bad_request);
    const nlohmann::json args = firstMessageArgs(asyncResp->res);
    ASSERT_EQ(args.size(), 2);
    // actionParameterValueError takes its first argument as const
    // nlohmann::json& and dump()s it, so a bare parameter name comes back
    // quoted.
    EXPECT_EQ(args[0], "\"PCoreIds\"");
    EXPECT_EQ(args[1], "NvidiaProcessor.CollectPCoreDump");
}

TEST(NvidiaPCoreDumpTest, MapCreateDumpDbusErrorUnknownIsInternalError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    mapCreateDumpDbusError(asyncResp,
                           "org.freedesktop.DBus.Error.UnknownMethod",
                           collectPCoreDumpActionName, "PCoreIds");

    EXPECT_EQ(asyncResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

// PCoreIds [2,2,2] must trigger one collection, not three.
TEST(NvidiaPCoreDumpTest, NormalizeCollapsesDuplicatesAndSorts)
{
    EXPECT_EQ(normalizePCoreIds({2, 2, 2}), (std::vector<uint64_t>{2}));
    EXPECT_EQ(normalizePCoreIds({3, 1, 3}), (std::vector<uint64_t>{1, 3}));
    EXPECT_TRUE(normalizePCoreIds({}).empty());
}

TEST(NvidiaPCoreDumpTest, FormatRendersCommaSeparatedDecimals)
{
    EXPECT_EQ(formatPCoreIds({1, 3}), "1,3");
    EXPECT_EQ(formatPCoreIds({2}), "2");
}

// An omitted PCoreIds and an explicit [] must both reach the dump manager as
// the empty string, which it reads as "every PCore".
TEST(NvidiaPCoreDumpTest, FormatRendersEmptyListAsEmptyString)
{
    EXPECT_EQ(formatPCoreIds({}), "");
    EXPECT_EQ(formatPCoreIds(normalizePCoreIds({})), "");
}

TEST(NvidiaPCoreDumpTest, RangeCheckAcceptsBoundaryValues)
{
    const PCoreIdRange bounds{1, 6};

    EXPECT_FALSE(firstOutOfRange({1, 6}, bounds).has_value());
    EXPECT_FALSE(firstOutOfRange({}, bounds).has_value());
}

// A mixed list must be rejected on its bad element, so [1,7] creates nothing.
TEST(NvidiaPCoreDumpTest, RangeCheckNamesTheFirstOutOfRangeElement)
{
    const PCoreIdRange bounds{1, 6};

    EXPECT_EQ(firstOutOfRange({0}, bounds), 0U);
    EXPECT_EQ(firstOutOfRange({7}, bounds), 7U);
    EXPECT_EQ(firstOutOfRange({1, 7}, bounds), 7U);
}

// The bounds are whatever the device advertised, not a compiled-in 1..6.
TEST(NvidiaPCoreDumpTest, RangeCheckUsesDeviceAdvertisedBounds)
{
    const PCoreIdRange bounds{0, 3};

    EXPECT_FALSE(firstOutOfRange({0, 3}, bounds).has_value());
    EXPECT_EQ(firstOutOfRange({4}, bounds), 4U);
}

TEST(NvidiaPCoreDumpTest, CreateDumpParamsCarryTypeDeviceAndSelectors)
{
    const auto params = buildPCoreCreateDumpParams("CPU_0", "1,3", "");

    EXPECT_EQ(paramValue(params, "DiagnosticType"), "PCoreDump");
    EXPECT_EQ(paramValue(params, "DeviceType"), "CPU_0");
    EXPECT_EQ(paramValue(params, "PCoreIds"), "1,3");
}

// PCoreIds must be present even when empty: its absence and an empty value mean
// the same thing to the dump manager, but sending it keeps the request
// self-describing in the journal.
TEST(NvidiaPCoreDumpTest, CreateDumpParamsSendEmptySelectorsForAllPCores)
{
    const auto params = buildPCoreCreateDumpParams("CPU_1", "", "");

    EXPECT_TRUE(hasParam(params, "PCoreIds"));
    EXPECT_EQ(paramValue(params, "PCoreIds"), "");
    EXPECT_EQ(paramValue(params, "DeviceType"), "CPU_1");
}

TEST(NvidiaPCoreDumpTest, CreateDumpParamsOmitOriginatorWhenUnknown)
{
    const auto params = buildPCoreCreateDumpParams("CPU_0", "1", "");

    EXPECT_FALSE(hasParam(
        params,
        "xyz.openbmc_project.Dump.Create.CreateParameters.OriginatorId"));
    EXPECT_FALSE(hasParam(
        params,
        "xyz.openbmc_project.Dump.Create.CreateParameters.OriginatorType"));
}

TEST(NvidiaPCoreDumpTest, CreateDumpParamsCarryOriginatorWhenKnown)
{
    const auto params = buildPCoreCreateDumpParams("CPU_0", "1", "10.0.0.7");

    EXPECT_EQ(
        paramValue(
            params,
            "xyz.openbmc_project.Dump.Create.CreateParameters.OriginatorId"),
        "10.0.0.7");
    EXPECT_EQ(
        paramValue(
            params,
            "xyz.openbmc_project.Dump.Create.CreateParameters.OriginatorType"),
        "xyz.openbmc_project.Common.OriginatedBy.OriginatorTypes.Client");
}

// Must match the committed redfish mockup byte for byte, including the optional
// PCoreIds being the only parameter -- the CPU is named by the URI.
TEST(NvidiaPCoreDumpTest, ActionInfoMatchesMockup)
{
    nlohmann::json json;

    buildCollectPCoreDumpActionInfo(json, "HGX_Baseboard_0", "CPU_0",
                                    PCoreIdRange{1, 6});

    EXPECT_EQ(json["@odata.type"], "#ActionInfo.v1_2_0.ActionInfo");
    EXPECT_EQ(
        json["@odata.id"],
        "/redfish/v1/Systems/HGX_Baseboard_0/Processors/CPU_0/Oem/Nvidia/CollectPCoreDumpActionInfo");
    EXPECT_EQ(json["Id"], "CollectPCoreDumpActionInfo");
    EXPECT_EQ(json["Name"], "Collect PCore Dump Action Info");
    EXPECT_EQ(json["Description"],
              "Parameters for collecting per-PCore dumps from this Vera CPU");

    ASSERT_EQ(json["Parameters"].size(), 1);
    const nlohmann::json& parameter = json["Parameters"][0];
    EXPECT_EQ(parameter["Name"], "PCoreIds");
    EXPECT_EQ(parameter["Required"], false);
    EXPECT_EQ(parameter["DataType"], "NumberArray");
    EXPECT_EQ(parameter["MinimumValue"], 1);
    EXPECT_EQ(parameter["MaximumValue"], 6);
}

// The advertised bounds track the device, so a differently-provisioned CPU
// advertises its own range rather than a shared constant.
TEST(NvidiaPCoreDumpTest, ActionInfoBoundsComeFromTheTrigger)
{
    nlohmann::json json;

    buildCollectPCoreDumpActionInfo(json, "HGX_Baseboard_0", "CPU_1",
                                    PCoreIdRange{0, 11});

    EXPECT_EQ(json["Parameters"][0]["MinimumValue"], 0);
    EXPECT_EQ(json["Parameters"][0]["MaximumValue"], 11);
    EXPECT_EQ(
        json["@odata.id"],
        "/redfish/v1/Systems/HGX_Baseboard_0/Processors/CPU_1/Oem/Nvidia/CollectPCoreDumpActionInfo");
}

// Must match the Actions.Oem block committed to the redfish Processor mockups.
TEST(NvidiaPCoreDumpTest, AdvertisementMatchesMockup)
{
    nlohmann::json json;

    buildPCoreDumpAdvertisement(json, "HGX_Baseboard_0", "CPU_0");

    const nlohmann::json& action =
        json["Actions"]["Oem"]["#NvidiaProcessor.CollectPCoreDump"];
    EXPECT_EQ(
        action["target"],
        "/redfish/v1/Systems/HGX_Baseboard_0/Processors/CPU_0/Actions/Oem/NvidiaProcessor.CollectPCoreDump");
    EXPECT_EQ(
        action["@Redfish.ActionInfo"],
        "/redfish/v1/Systems/HGX_Baseboard_0/Processors/CPU_0/Oem/Nvidia/CollectPCoreDumpActionInfo");
}

} // namespace
} // namespace redfish
