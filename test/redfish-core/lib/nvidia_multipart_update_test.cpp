// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.

#include "bmcweb_config.h"

#include "async_resp.hpp"
#include "http/http_request.hpp"
#include "nvidia_multipart_update.hpp"
#include "task.hpp"

#include <boost/beast/core/error.hpp>
#include <boost/system/errc.hpp>

#include <format>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

// Nvidia code starts here

namespace redfish::nvidia
{
namespace
{

std::shared_ptr<UpdateCtx> makeCtx()
{
    std::error_code ec;
    crow::Request req("", ec);
    task::Payload payload(req);
    return std::make_shared<UpdateCtx>(0, std::move(payload));
}

std::string expectedSetHeadersOutput(const std::string& boundary,
                                     const std::string& paramsJson)
{
    return "--" + boundary +
           "\r\nContent-Disposition: form-data; name=\"UpdateParameters\"\r\n"
           "Content-Type: application/json\r\n"
           "\r\n" +
           paramsJson + "\r\n--" + boundary +
           "\r\nContent-Disposition: form-data; name=\"UpdateFile\"\r\n"
           "Content-Type: application/octet-stream\r\n"
           "\r\n";
}

TEST(SetHeaders, EmptyTargetsNoParams)
{
    auto ctx = makeCtx();
    std::string boundary(ctx->multipartSerializer.getBoundary());

    ctx->setHeaders({});

    EXPECT_EQ(ctx->pendingWriteBuffer,
              expectedSetHeadersOutput(boundary, "{}"));
}

TEST(SetHeaders, WithTargets)
{
    auto ctx = makeCtx();
    std::string boundary(ctx->multipartSerializer.getBoundary());

    ctx->setHeaders({"target1", "target2"});

    EXPECT_EQ(ctx->pendingWriteBuffer,
              expectedSetHeadersOutput(boundary,
                                       R"({"Targets":["target1","target2"]})"));
}

TEST(SetHeaders, WithApplyTime)
{
    auto ctx = makeCtx();
    std::string boundary(ctx->multipartSerializer.getBoundary());
    ctx->multiRet.params.applyTime = "Immediate";

    ctx->setHeaders({});

    EXPECT_EQ(
        ctx->pendingWriteBuffer,
        expectedSetHeadersOutput(boundary, R"({"ApplyTime":"Immediate"})"));
}

TEST(SetHeaders, WithForceUpdateTrue)
{
    auto ctx = makeCtx();
    std::string boundary(ctx->multipartSerializer.getBoundary());
    ctx->multiRet.params.forceUpdate = true;

    ctx->setHeaders({});

    EXPECT_EQ(ctx->pendingWriteBuffer,
              expectedSetHeadersOutput(boundary, R"({"ForceUpdate":true})"));
}

TEST(SetHeaders, AllParams)
{
    auto ctx = makeCtx();
    std::string boundary(ctx->multipartSerializer.getBoundary());
    ctx->multiRet.params.applyTime = "OnReset";
    ctx->multiRet.params.forceUpdate = false;

    ctx->setHeaders(
        {"http://bmc/redfish/v1/UpdateService/FirmwareInventory/fw0"});

    // nlohmann object_t (std::map) sorts keys alphabetically:
    // ApplyTime < ForceUpdate < Targets
    EXPECT_EQ(
        ctx->pendingWriteBuffer,
        expectedSetHeadersOutput(
            boundary,
            R"({"ApplyTime":"OnReset","ForceUpdate":false,"Targets":["http://bmc/redfish/v1/UpdateService/FirmwareInventory/fw0"]})"));
}

TEST(ParseRfaUri, EmptyUriReturnsError)
{
    EXPECT_EQ(parseRfaUri(""), TargetType::Error);
}

TEST(ParseRfaUri, UnparseableUriReturnsError)
{
    // Invalid percent-encoding can't be parsed as a relative ref.
    EXPECT_EQ(parseRfaUri("/redfish/v1/Chassis/%zz"), TargetType::Error);
}

TEST(ParseRfaUri, HmcChassisTargetOmitsTargets)
{
    std::string uri =
        std::format("/redfish/v1/Chassis/{}", BMCWEB_RFA_HMC_UPDATE_TARGET);
    EXPECT_EQ(parseRfaUri(uri), TargetType::SatelliteOmitTargets);
}

TEST(ParseRfaUri, AggregationPrefixedChassisIsSatellite)
{
    // A chassis whose id carries the aggregation prefix (but isn't the HMC
    // update target) routes to a satellite BMC.
    std::string uri = std::format("/redfish/v1/Chassis/{}_Baseboard_0",
                                  BMCWEB_REDFISH_AGGREGATION_PREFIX);
    EXPECT_EQ(parseRfaUri(uri), TargetType::Satellite);
}

TEST(ParseRfaUri, UnprefixedChassisIsLocal)
{
    EXPECT_EQ(parseRfaUri("/redfish/v1/Chassis/Baseboard_0"),
              TargetType::Local);
}

TEST(ParseRfaUri, HmcManagerTargetOmitsTargets)
{
    std::string uri =
        std::format("/redfish/v1/Managers/{}", BMCWEB_RFA_HMC_UPDATE_TARGET);
    EXPECT_EQ(parseRfaUri(uri), TargetType::SatelliteOmitTargets);
}

TEST(ParseRfaUri, NonHmcManagerIsLocal)
{
    EXPECT_EQ(parseRfaUri("/redfish/v1/Managers/bmc"), TargetType::Local);
}

TEST(ParseRfaUri, AggregationPrefixedSoftwareInventoryIsSatellite)
{
    std::string uri =
        std::format("/redfish/v1/UpdateService/SoftwareInventory/{}_FW_0",
                    BMCWEB_REDFISH_AGGREGATION_PREFIX);
    EXPECT_EQ(parseRfaUri(uri), TargetType::Satellite);
}

TEST(ParseRfaUri, UnprefixedSoftwareInventoryIsLocal)
{
    EXPECT_EQ(parseRfaUri("/redfish/v1/UpdateService/SoftwareInventory/FW_0"),
              TargetType::Local);
}

TEST(ParseRfaUri, UnrelatedUriIsLocal)
{
    EXPECT_EQ(parseRfaUri("/redfish/v1/Systems/system"), TargetType::Local);
}

TEST(ParseRfaUri, UnprefixedFirmwareInventoryIsLocal)
{
    EXPECT_EQ(parseRfaUri("/redfish/v1/UpdateService/FirmwareInventory/fw0"),
              TargetType::Local);
}

TEST(ParseRfaUri, AggregationPrefixedFirmwareInventoryIsSatellite)
{
    std::string uri =
        std::format("/redfish/v1/UpdateService/FirmwareInventory/{}_FW_BMC_0",
                    BMCWEB_REDFISH_AGGREGATION_PREFIX);
    EXPECT_EQ(parseRfaUri(uri), TargetType::Satellite);
}

TEST(ParseRfaUri, AggregationPrefixedManagerIsLocal)
{
    // Only the HMC target gets special treatment; other (aggregation-prefixed)
    // manager IDs fall through and are treated as local.
    std::string uri = std::format("/redfish/v1/Managers/{}_bmc",
                                  BMCWEB_REDFISH_AGGREGATION_PREFIX);
    EXPECT_EQ(parseRfaUri(uri), TargetType::Local);
}

TEST(SetHeaders, WithForceUpdateFalse)
{
    auto ctx = makeCtx();
    std::string boundary(ctx->multipartSerializer.getBoundary());
    ctx->multiRet.params.forceUpdate = false;

    ctx->setHeaders({});

    EXPECT_EQ(ctx->pendingWriteBuffer,
              expectedSetHeadersOutput(boundary, R"({"ForceUpdate":false})"));
}

TEST(SetHeaders, WithTargetsAndApplyTime)
{
    auto ctx = makeCtx();
    std::string boundary(ctx->multipartSerializer.getBoundary());
    ctx->multiRet.params.applyTime = "OnReset";

    ctx->setHeaders({"target1"});

    EXPECT_EQ(ctx->pendingWriteBuffer,
              expectedSetHeadersOutput(
                  boundary,
                  R"({"ApplyTime":"OnReset","Targets":["target1"]})"));
}

TEST(ErrorHandler, AlwaysReturnsSuccess)
{
    EXPECT_FALSE(errorHandler(200));
    EXPECT_FALSE(errorHandler(404));
    EXPECT_FALSE(errorHandler(500));
}

TEST(PutBytesToHttpClient, BuffersBeforeFileDataState)
{
    auto ctx = makeCtx();
    // Default state is WAITING_FOR_PART_HEADERS — not yet ready to stream.
    ctx->putBytesToHttpClient("hello");
    EXPECT_EQ(ctx->pendingWriteBuffer, "hello");
    ctx->putBytesToHttpClient(" world");
    EXPECT_EQ(ctx->pendingWriteBuffer, "hello world");
}

TEST(PutBytesToHttpClient, AppendsWhenSocketInUse)
{
    auto ctx = makeCtx();
    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA;
    ctx->socketInUse = true;

    ctx->putBytesToHttpClient("chunk1");
    ctx->putBytesToHttpClient("chunk2");

    EXPECT_EQ(ctx->pendingWriteBuffer, "chunk1chunk2");
}

TEST(OnDataAvailable, AccumulatesUpdateParametersData)
{
    auto ctx = makeCtx();
    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_PARAMETERS_DATA;

    ctx->onDataAvailable(ctx, "part1");
    ctx->onDataAvailable(ctx, "part2");

    EXPECT_EQ(ctx->updateParametersString, "part1part2");
    EXPECT_EQ(ctx->state, UpdateCtx::State::WAITING_FOR_UPDATE_PARAMETERS_DATA);
}

TEST(OnHeadersComplete, AcceptsUpdateFileBeforeUpdateParameters)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::beast::http::fields fields;
    fields.set("Content-Disposition", "form-data; name=\"UpdateFile\"");

    ctx->onHeadersComplete(ctx, fields, 1024);

    EXPECT_TRUE(ctx->updateFileHeadersSeen);
    EXPECT_EQ(ctx->state, UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA);
    EXPECT_FALSE(ctx->updateStarted);
}

TEST(OnDataAvailable, BuffersUpdateFileDataBeforeUpdateStarted)
{
    auto ctx = makeCtx();
    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA;
    ctx->updateStarted = false;

    ctx->onDataAvailable(ctx, "fw data");

    EXPECT_EQ(ctx->pendingFileDataBuffer, "fw data");
}

TEST(OnSectionComplete, MergesMultipleUpdateParametersSections)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_PARAMETERS_DATA;
    ctx->updateParametersString =
        R"({"Targets":["/redfish/v1/Chassis/HGX_Chassis_0"]})";

    ctx->onSectionComplete(ctx);

    ASSERT_TRUE(ctx->multiRet.params.targets.has_value());
    ASSERT_EQ(ctx->multiRet.params.targets.value().size(), 1U);
    EXPECT_EQ(ctx->state, UpdateCtx::State::WAITING_FOR_PART_HEADERS);

    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_PARAMETERS_DATA;
    ctx->updateParametersString = R"({"ForceUpdate":true})";

    ctx->onSectionComplete(ctx);

    EXPECT_TRUE(ctx->multiRet.params.forceUpdate.value_or(false));
    ASSERT_TRUE(ctx->multiRet.params.targets.has_value());
}

TEST(OnDataAvailable, RejectsOversizedUpdateParametersData)
{
    auto ctx = makeCtx();
    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_PARAMETERS_DATA;

    // Just under the 8192-byte limit.
    ctx->onDataAvailable(ctx, std::string(8000, 'x'));
    EXPECT_EQ(ctx->state, UpdateCtx::State::WAITING_FOR_UPDATE_PARAMETERS_DATA);

    // One more chunk that pushes past the limit.
    ctx->onDataAvailable(ctx, std::string(200, 'y'));
    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE);
}

TEST(OnDataAvailable, BuffersPendingFileDataWhileWaitingForSatInfo)
{
    auto ctx = makeCtx();
    ctx->state = UpdateCtx::State::WAITING_FOR_SAT_CONTROLLER_INFO_COMPLETE;

    ctx->onDataAvailable(ctx, "file chunk 1");
    ctx->onDataAvailable(ctx, " file chunk 2");

    EXPECT_EQ(ctx->pendingFileDataBuffer, "file chunk 1 file chunk 2");
}

TEST(OnSectionComplete, SetsFileSectionCompleteWhenWaitingForSatInfo)
{
    auto ctx = makeCtx();
    ctx->state = UpdateCtx::State::WAITING_FOR_SAT_CONTROLLER_INFO_COMPLETE;
    EXPECT_FALSE(ctx->fileSectionComplete);

    ctx->onSectionComplete(ctx);

    EXPECT_TRUE(ctx->fileSectionComplete);
    EXPECT_EQ(ctx->state,
              UpdateCtx::State::WAITING_FOR_SAT_CONTROLLER_INFO_COMPLETE);
}

TEST(OnSectionComplete, SetsFileSectionCompleteWhenUpdateNotStarted)
{
    auto ctx = makeCtx();
    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA;
    EXPECT_FALSE(ctx->fileSectionComplete);

    ctx->onSectionComplete(ctx);

    EXPECT_TRUE(ctx->fileSectionComplete);
    EXPECT_EQ(ctx->state, UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA);
}

TEST(OnSectionComplete, TransitionsToUpdateCompleteFromFileDataState)
{
    auto ctx = makeCtx();
    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA;
    ctx->updateStarted = true;

    ctx->onSectionComplete(ctx);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE);
}

TEST(OnParseComplete, SetsParseCompleteFlag)
{
    auto ctx = makeCtx();
    EXPECT_FALSE(ctx->parseComplete);

    ctx->onParseComplete(ctx);

    EXPECT_TRUE(ctx->parseComplete);
}

TEST(CloseSendSocketIfReady, DoesNotCloseWhenParseNotComplete)
{
    auto ctx = makeCtx();
    ctx->state = UpdateCtx::State::UPDATE_COMPLETE;
    // parseComplete defaults to false

    ctx->closeSendSocketIfReady();

    EXPECT_TRUE(ctx->fileSendSocket.is_open());
}

TEST(CloseSendSocketIfReady, DoesNotCloseWhenSocketInUse)
{
    auto ctx = makeCtx();
    ctx->parseComplete = true;
    ctx->socketInUse = true;
    ctx->state = UpdateCtx::State::UPDATE_COMPLETE;

    ctx->closeSendSocketIfReady();

    EXPECT_TRUE(ctx->fileSendSocket.is_open());
}

TEST(CloseSendSocketIfReady, DoesNotCloseWhenPendingDataExists)
{
    auto ctx = makeCtx();
    ctx->parseComplete = true;
    ctx->pendingWriteBuffer = "pending";
    ctx->state = UpdateCtx::State::UPDATE_COMPLETE;

    ctx->closeSendSocketIfReady();

    EXPECT_TRUE(ctx->fileSendSocket.is_open());
}

TEST(CloseSendSocketIfReady, DoesNotCloseInNonTerminalState)
{
    auto ctx = makeCtx();
    ctx->parseComplete = true;
    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA;

    ctx->closeSendSocketIfReady();

    EXPECT_TRUE(ctx->fileSendSocket.is_open());
}

TEST(CloseSendSocketIfReady, ClosesSocketWhenAllConditionsMet)
{
    auto ctx = makeCtx();
    ctx->parseComplete = true;
    ctx->socketInUse = false;
    ctx->state = UpdateCtx::State::UPDATE_COMPLETE;
    // pendingWriteBuffer is empty by default

    EXPECT_TRUE(ctx->fileSendSocket.is_open());
    ctx->closeSendSocketIfReady();
    EXPECT_FALSE(ctx->fileSendSocket.is_open());
}

TEST(CloseSendSocketIfReady, ClosesSocketOnUpdateCompleteError)
{
    auto ctx = makeCtx();
    ctx->parseComplete = true;
    ctx->socketInUse = false;
    ctx->state = UpdateCtx::State::UPDATE_COMPLETE_ERROR;

    ctx->closeSendSocketIfReady();

    EXPECT_FALSE(ctx->fileSendSocket.is_open());
}

TEST(EndClientResponseIfReady, DoesNotEndWhenOnlyResponseReady)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->responseReady = true;

    ctx->endClientResponseIfReady();

    EXPECT_FALSE(ctx->asyncResp->res.isCompleted());
}

TEST(EndClientResponseIfReady, DoesNotEndWhenOnlyParseComplete)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->parseComplete = true;

    ctx->endClientResponseIfReady();

    EXPECT_FALSE(ctx->asyncResp->res.isCompleted());
}

TEST(EndClientResponseIfReady, EndsWhenResponseReadyAndParseComplete)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->responseReady = true;
    ctx->parseComplete = true;

    ctx->endClientResponseIfReady();

    EXPECT_TRUE(ctx->asyncResp->res.isCompleted());
}

TEST(FailClientResponse, SetsErrorStateAndMarksResponseReady)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();

    ctx->failClientResponse();

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_TRUE(ctx->responseReady);
}

TEST(OnDataAvailable, DiscardsDataInTerminalState)
{
    auto ctx = makeCtx();
    ctx->state = UpdateCtx::State::UPDATE_COMPLETE;

    ctx->onDataAvailable(ctx, "late data");

    EXPECT_TRUE(ctx->pendingWriteBuffer.empty());
    EXPECT_TRUE(ctx->pendingFileDataBuffer.empty());
    EXPECT_TRUE(ctx->updateParametersString.empty());
}

TEST(AfterWritePartialData, ErrorDiscardsBodyAndResumesReads)
{
    auto ctx = makeCtx();
    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA;
    bool resumed = false;
    ctx->resumeReadCb = [&resumed]() { resumed = true; };

    boost::beast::error_code ec =
        boost::system::errc::make_error_code(boost::system::errc::broken_pipe);
    ctx->afterWritePartialData(ctx, ec, 0);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_TRUE(resumed);
}

} // namespace
} // namespace redfish::nvidia
// Nvidia code ends here
