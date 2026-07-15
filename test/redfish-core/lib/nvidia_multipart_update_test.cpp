// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.

#include "bmcweb_config.h"

#include "async_resp.hpp"
#include "http/http_request.hpp"
#include "nvidia_multipart_update.hpp"
#include "task.hpp"

#include <unistd.h>

#include <boost/beast/core/error.hpp>
#include <boost/system/errc.hpp>

#include <format>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

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

std::shared_ptr<PLDMUpdateCtx> makePLDMCtx(bool preUpdateValidation)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::error_code ec;
    crow::Request req("", ec);
    task::Payload payload(req);
    boost::asio::local::stream_protocol::socket socket(getIoContext());
    return std::make_shared<PLDMUpdateCtx>(
        asyncResp, std::move(payload), std::move(socket), "OnReset", false,
        std::vector<sdbusplus::message::object_path>{}, preUpdateValidation,
        []() {}, []() {});
}

TEST(PLDMUpdateCtx, PreservesPreUpdateValidationTrue)
{
    EXPECT_TRUE(makePLDMCtx(true)->preUpdateValidation);
}

TEST(PLDMUpdateCtx, PreservesPreUpdateValidationFalse)
{
    EXPECT_FALSE(makePLDMCtx(false)->preUpdateValidation);
}

TEST(PLDMUpdateCtx, RejectsImageDataOverLimit)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    std::error_code ec;
    crow::Request req("", ec);
    task::Payload payload(req);
    boost::asio::local::stream_protocol::socket socket(getIoContext());
    bool failed = false;
    auto ctx = std::make_shared<PLDMUpdateCtx>(
        asyncResp, std::move(payload), std::move(socket), "OnReset", false,
        std::vector<sdbusplus::message::object_path>{}, false, []() {},
        [&failed]() { failed = true; });
    ctx->bytesWritten = redfish::firmwareImageLimitBytes;

    ctx->gotBytes({}, 1U);

    EXPECT_TRUE(failed);
    EXPECT_EQ(ctx->bytesWritten, redfish::firmwareImageLimitBytes);
    EXPECT_EQ(asyncResp->res.resultInt(), 413);
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

    EXPECT_EQ(ctx->pendingWriteBuffer,
              expectedSetHeadersOutput(
                  boundary, R"({"@Redfish.OperationApplyTime":"Immediate"})"));
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

TEST(SetHeaders, WithPreUpdateValidationTrue)
{
    auto ctx = makeCtx();
    std::string boundary(ctx->multipartSerializer.getBoundary());
    ctx->multiRet.params.preUpdateValidation = true;

    ctx->setHeaders({});

    EXPECT_EQ(ctx->pendingWriteBuffer,
              expectedSetHeadersOutput(
                  boundary,
                  R"({"Oem":{"Nvidia":{"PreUpdateValidation":true}}})"));
}

TEST(SetHeaders, WithPreUpdateValidationFalse)
{
    auto ctx = makeCtx();
    std::string boundary(ctx->multipartSerializer.getBoundary());
    ctx->multiRet.params.preUpdateValidation = false;

    ctx->setHeaders({});

    EXPECT_EQ(ctx->pendingWriteBuffer,
              expectedSetHeadersOutput(
                  boundary,
                  R"({"Oem":{"Nvidia":{"PreUpdateValidation":false}}})"));
}

TEST(SetHeaders, AllParams)
{
    auto ctx = makeCtx();
    std::string boundary(ctx->multipartSerializer.getBoundary());
    ctx->multiRet.params.applyTime = "OnReset";
    ctx->multiRet.params.forceUpdate = false;

    ctx->setHeaders(
        {"http://bmc/redfish/v1/UpdateService/FirmwareInventory/fw0"});

    // nlohmann sorts keys by byte value: '@' (0x40) precedes 'F'/'T'.
    EXPECT_EQ(
        ctx->pendingWriteBuffer,
        expectedSetHeadersOutput(
            boundary,
            R"({"@Redfish.OperationApplyTime":"OnReset","ForceUpdate":false,"Targets":["http://bmc/redfish/v1/UpdateService/FirmwareInventory/fw0"]})"));
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

    EXPECT_EQ(
        ctx->pendingWriteBuffer,
        expectedSetHeadersOutput(
            boundary,
            R"({"@Redfish.OperationApplyTime":"OnReset","Targets":["target1"]})"));
}

TEST(ErrorHandler, AlwaysReturnsSuccess)
{
    EXPECT_FALSE(errorHandler(200));
    EXPECT_FALSE(errorHandler(404));
    EXPECT_FALSE(errorHandler(500));
}

TEST(PLDMUpdateCtx, DoesNotStartUpdateAfterRequestFailure)
{
    std::error_code ec;
    crow::Request req("", ec);
    task::Payload payload(req);
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::asio::local::stream_protocol::socket socket(getIoContext());
    auto ctx = std::make_shared<PLDMUpdateCtx>(
        asyncResp, std::move(payload), std::move(socket), "xyz", false,
        std::vector<sdbusplus::message::object_path>{}, false, []() {},
        []() {});
    redfish::fwUpdateInProgress = false;
    messages::unrecognizedRequestBody(asyncResp->res);

    ctx->gotBytes(boost::asio::error::eof, 0);

    EXPECT_FALSE(redfish::fwUpdateInProgress);
}

TEST(PutBytesToHttpClient, BuffersBeforeFileDataState)
{
    auto ctx = makeCtx();
    // Default state is WAITING_FOR_PART_HEADERS — not yet ready
    // to stream, so data must land in pendingWriteBuffer.
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

TEST(OnDataAvailable, RejectsOversizedUpdateParametersData)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_PARAMETERS_DATA;

    // Just under the 8192-byte limit.
    ctx->onDataAvailable(ctx, std::string(8000, 'x'));
    EXPECT_EQ(ctx->state, UpdateCtx::State::WAITING_FOR_UPDATE_PARAMETERS_DATA);

    // One more chunk that pushes past the limit fails the request.
    ctx->onDataAvailable(ctx, std::string(200, 'y'));
    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
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

TEST(OnSectionComplete, TransitionsToUpdateCompleteFromFileDataState)
{
    auto ctx = makeCtx();
    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA;

    ctx->onSectionComplete(ctx);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE);
}

TEST(OnHeadersComplete, InvalidApplyTimeReturnsSingleError)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->multiRet.params.applyTime = "Invalid";
    ctx->updateParametersReceived = true;
    ctx->state = UpdateCtx::State::WAITING_FOR_PART_HEADERS;

    boost::beast::http::fields fileFields;
    fileFields.set(boost::beast::http::field::content_disposition,
                   "form-data; name=\"UpdateFile\"");

    // The apply-time gate rejects the bad value once and stops; the flow must
    // not fall through to startRequest()/localUpdate() and emit it a second
    // time.
    ctx->onHeadersComplete(ctx, fileFields, 0);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_EQ(
        ctx->asyncResp->res.jsonValue["ApplyTime@Message.ExtendedInfo"].size(),
        1U);
}

TEST(OnParseComplete, MissingUpdateFileReturnsErrorNotHang)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->updateParametersReceived = true;
    ctx->state = UpdateCtx::State::WAITING_FOR_PART_HEADERS;

    ctx->onParseComplete(ctx);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
}

TEST(OnParseComplete, SetsParseCompleteFlag)
{
    auto ctx = makeCtx();
    ctx->state = UpdateCtx::State::UPDATE_COMPLETE;
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

TEST(ReleaseClientResponseIfReady, RetainsResponseWhenOnlyResponseReady)
{
    size_t completionCount = 0;
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->asyncResp->res.setCompleteRequestHandler(
        [&completionCount](crow::Response&) { completionCount++; });
    ctx->responseReady = true;

    ctx->releaseClientResponseIfReady();

    ASSERT_TRUE(ctx->asyncResp);
    EXPECT_FALSE(ctx->asyncResp->res.isCompleted());
    EXPECT_EQ(completionCount, 0U);
}

TEST(ReleaseClientResponseIfReady, RetainsResponseWhenOnlyParseComplete)
{
    size_t completionCount = 0;
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->asyncResp->res.setCompleteRequestHandler(
        [&completionCount](crow::Response&) { completionCount++; });
    ctx->parseComplete = true;

    ctx->releaseClientResponseIfReady();

    ASSERT_TRUE(ctx->asyncResp);
    EXPECT_FALSE(ctx->asyncResp->res.isCompleted());
    EXPECT_EQ(completionCount, 0U);
}

TEST(ReleaseClientResponseIfReady,
     ReleasesResponseWhenResponseReadyAndParseComplete)
{
    size_t completionCount = 0;
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->asyncResp->res.setCompleteRequestHandler(
        [&completionCount](crow::Response&) { completionCount++; });
    std::weak_ptr<bmcweb::AsyncResp> weakResp = ctx->asyncResp;
    ctx->responseReady = true;
    ctx->parseComplete = true;

    ctx->releaseClientResponseIfReady();

    EXPECT_FALSE(ctx->asyncResp);
    EXPECT_TRUE(weakResp.expired());
    EXPECT_EQ(completionCount, 1U);
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

boost::beast::http::fields makePartFields(std::string_view partName)
{
    boost::beast::http::fields fields;
    fields.set(boost::beast::http::field::content_disposition,
               std::format("form-data; name=\"{}\"", partName));
    return fields;
}

boost::beast::http::fields makeUpdateParametersFields()
{
    boost::beast::http::fields fields = makePartFields("UpdateParameters");
    fields.set(boost::beast::http::field::content_type, "application/json");
    return fields;
}

void completeUpdateParameters(const std::shared_ptr<UpdateCtx>& ctx,
                              std::string_view parameters)
{
    ctx->onHeadersComplete(ctx, makeUpdateParametersFields(), 0);
    ctx->onDataAvailable(ctx, parameters);
    ctx->onSectionComplete(ctx);
}

TEST(MultipartPartOrder, UpdateParametersFirstWaitsForUpdateFile)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();

    ctx->onHeadersComplete(ctx, makeUpdateParametersFields(), 0);
    ctx->onDataAvailable(ctx, R"({"ForceUpdate":)");
    ctx->onDataAvailable(ctx, "true}");
    ctx->onSectionComplete(ctx);

    EXPECT_TRUE(ctx->updateParametersReceived);
    EXPECT_EQ(ctx->multiRet.params.forceUpdate, std::optional<bool>{true});
    EXPECT_EQ(ctx->state, UpdateCtx::State::WAITING_FOR_PART_HEADERS);
    EXPECT_FALSE(ctx->stagedUpdateFile);
}

TEST(MultipartPartOrder, UpdateParametersFirstStreamsWithoutStaging)
{
    redfish::fwUpdateInProgress = false;
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    bool paused = false;
    bool resumed = false;
    ctx->pauseReadCb = [&paused]() { paused = true; };
    ctx->resumeReadCb = [&resumed]() { resumed = true; };
    completeUpdateParameters(ctx, "{}");

    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 3U);

    EXPECT_TRUE(paused);
    EXPECT_TRUE(resumed);
    EXPECT_TRUE(ctx->isLocal);
    EXPECT_EQ(ctx->state, UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA);
    EXPECT_FALSE(ctx->stagedUpdateFile);
}

TEST(MultipartPartOrder, MalformedUpdateParametersFailsBeforeUpdateFile)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();

    completeUpdateParameters(ctx, "not-json");

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_FALSE(ctx->updateParametersReceived);
    EXPECT_FALSE(ctx->stagedUpdateFile);
}

TEST(MultipartPartOrder, UnknownFirstPartIsRejected)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();

    ctx->onHeadersComplete(ctx, makePartFields("UnknownPart"), 0);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
}

TEST(OnDataAvailable, AcceptsExactLimitAndRejectsNextByte)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 0);
    ASSERT_TRUE(ctx->stagedUpdateFile);
    constexpr size_t lastByteOffset = redfish::firmwareImageLimitBytes - 1U;
    ASSERT_EQ(ftruncate(ctx->stagedUpdateFile->fd,
                        static_cast<off_t>(lastByteOffset)),
              0);
    ASSERT_EQ(lseek(ctx->stagedUpdateFile->fd,
                    static_cast<off_t>(lastByteOffset), SEEK_SET),
              static_cast<off_t>(lastByteOffset));

    ctx->onDataAvailable(ctx, "a");
    EXPECT_EQ(ctx->state,
              UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA_BEFORE_PARAMETERS);
    EXPECT_EQ(ctx->getStagedUpdateFileSize(),
              std::optional<size_t>{redfish::firmwareImageLimitBytes});

    ctx->onDataAvailable(ctx, "b");
    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 413);
}

TEST(OnHeadersComplete, UpdateFileFirstEntersStagingState)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();

    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 0);

    EXPECT_EQ(ctx->state,
              UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA_BEFORE_PARAMETERS);
}

TEST(OnHeadersComplete, MultipartOverheadDoesNotRejectFileFirstUpload)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    constexpr size_t remainingBodyIncludingMultipartOverhead =
        redfish::firmwareImageLimitBytes + 1U;

    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"),
                           remainingBodyIncludingMultipartOverhead);

    EXPECT_EQ(ctx->state,
              UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA_BEFORE_PARAMETERS);
    EXPECT_TRUE(ctx->stagedUpdateFile);
}

TEST(OnHeadersComplete, UpdateFileWithWrongContentTypeIsRejected)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();

    boost::beast::http::fields fields = makePartFields("UpdateFile");
    fields.set(boost::beast::http::field::content_type, "application/ream");

    ctx->onHeadersComplete(ctx, fields, 0);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 400);
    EXPECT_EQ(ctx->asyncResp->res
                  .jsonValue["error"]["@Message.ExtendedInfo"][0]["MessageId"],
              "Base.1.19.MissingOrMalformedPart");
}

TEST(OnHeadersComplete, UpdateFileWithOctetStreamContentTypeAccepted)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();

    boost::beast::http::fields fields = makePartFields("UpdateFile");
    fields.set(boost::beast::http::field::content_type,
               "application/octet-stream");

    ctx->onHeadersComplete(ctx, fields, 0);

    EXPECT_EQ(ctx->state,
              UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA_BEFORE_PARAMETERS);
    EXPECT_TRUE(ctx->stagedUpdateFile);
}

TEST(OnHeadersComplete, SecondUpdateFileAfterStagingRejected)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 0);
    ctx->onSectionComplete(ctx);

    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 0);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 400);
    EXPECT_EQ(ctx->asyncResp->res
                  .jsonValue["UpdateFile@Message.ExtendedInfo"][0]["MessageId"],
              "Base.1.19.PropertyDuplicate");
}

TEST(OnDataAvailable, StagesFileFirstDataToMemfd)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 0);

    ctx->onDataAvailable(ctx, "chunk1");
    ctx->onDataAvailable(ctx, "chunk2");

    EXPECT_EQ(ctx->getStagedUpdateFileSize(), std::optional<size_t>{12U});
    EXPECT_EQ(ctx->state,
              UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA_BEFORE_PARAMETERS);
}

TEST(OnDataAvailable, RejectsStagedFileOverImageLimit)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 0);
    ASSERT_TRUE(ctx->stagedUpdateFile);
    ASSERT_EQ(ftruncate(ctx->stagedUpdateFile->fd,
                        static_cast<off_t>(redfish::firmwareImageLimitBytes)),
              0);

    ctx->onDataAvailable(ctx, "x");

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 413);
    EXPECT_FALSE(ctx->stagedUpdateFile);
}

TEST(OnDataAvailable, StagingStateWithoutMemfdFailsInsteadOfCrashing)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    // Force the state without the header step that creates the memfd.
    ctx->state =
        UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA_BEFORE_PARAMETERS;

    ctx->onDataAvailable(ctx, "data");

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 500);
}

TEST(OnSectionComplete, StagedFileExpectsUpdateParametersNext)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 0);

    ctx->onSectionComplete(ctx);

    EXPECT_TRUE(ctx->stagedUpdateFile);
    EXPECT_EQ(ctx->state, UpdateCtx::State::WAITING_FOR_PART_HEADERS);
}

TEST(OnHeadersComplete, UpdateParametersAcceptedAfterStagedFile)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 0);
    ctx->onSectionComplete(ctx);

    boost::beast::http::fields fields = makeUpdateParametersFields();
    ctx->onHeadersComplete(ctx, fields, 0);

    EXPECT_EQ(ctx->state, UpdateCtx::State::WAITING_FOR_UPDATE_PARAMETERS_DATA);
}

TEST(MultipartPartOrder, FileFirstRejectsParametersWithoutContentType)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 0);
    ctx->onDataAvailable(ctx, "abc");
    ctx->onSectionComplete(ctx);

    ctx->onHeadersComplete(ctx, makePartFields("UpdateParameters"), 0);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_FALSE(ctx->stagedUpdateFile);
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 400);
    EXPECT_EQ(ctx->asyncResp->res
                  .jsonValue["error"]["@Message.ExtendedInfo"][0]["MessageId"],
              "Base.1.19.MissingOrMalformedPart");
}

TEST(MultipartPartOrder, FileFirstRejectsDuplicateUpdateParameters)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 0);
    ctx->onDataAvailable(ctx, "abc");
    ctx->onSectionComplete(ctx);
    completeUpdateParameters(ctx, "{}");
    ASSERT_EQ(ctx->state, UpdateCtx::State::WAITING_FOR_PART_HEADERS);

    ctx->onHeadersComplete(ctx, makeUpdateParametersFields(), 0);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_FALSE(ctx->stagedUpdateFile);
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 400);
    EXPECT_EQ(
        ctx->asyncResp->res
            .jsonValue["UpdateParameters@Message.ExtendedInfo"][0]["MessageId"],
        "Base.1.19.PropertyDuplicate");
}

TEST(MultipartPartOrder, EmptyUpdateFileFirstCompletesAfterParameters)
{
    redfish::fwUpdateInProgress = false;
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 0);
    ctx->onSectionComplete(ctx);
    completeUpdateParameters(ctx, "{}");

    ctx->onParseComplete(ctx);

    EXPECT_TRUE(ctx->parseComplete);
    EXPECT_TRUE(ctx->isLocal);
    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE);
    EXPECT_FALSE(ctx->stagedUpdateFile);
    EXPECT_FALSE(ctx->socketInUse);
}

TEST(OnSectionComplete, StagedFileInvalidApplyTimeFails)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 0);
    ctx->onSectionComplete(ctx);
    ctx->onHeadersComplete(ctx, makeUpdateParametersFields(), 0);
    ctx->updateParametersString =
        R"({"@Redfish.OperationApplyTime":"NotATime"})";

    ctx->onSectionComplete(ctx);
    EXPECT_EQ(ctx->state, UpdateCtx::State::WAITING_FOR_PART_HEADERS);
    ctx->onParseComplete(ctx);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
}

TEST(OnSectionComplete, StagedFileValidParamsTransfersMemfdToPldm)
{
    redfish::fwUpdateInProgress = false;
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 0);
    ctx->onDataAvailable(ctx, "abc");
    ctx->onSectionComplete(ctx);
    ASSERT_EQ(ctx->state, UpdateCtx::State::WAITING_FOR_PART_HEADERS);
    ctx->onHeadersComplete(ctx, makeUpdateParametersFields(), 0);
    ctx->updateParametersString = "{}";

    ctx->onSectionComplete(ctx);
    EXPECT_EQ(ctx->state, UpdateCtx::State::WAITING_FOR_PART_HEADERS);
    EXPECT_FALSE(ctx->socketInUse);
    int stagedFd = ctx->stagedUpdateFile->fd;
    ctx->onParseComplete(ctx);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE);
    EXPECT_TRUE(ctx->isLocal);
    EXPECT_FALSE(ctx->stagedUpdateFile);
    int copiedFd = dup(stagedFd);
    ASSERT_NE(copiedFd, -1);
    close(copiedFd);
    EXPECT_TRUE(ctx->currentWriteBuffer.empty());
    EXPECT_FALSE(ctx->socketInUse);
}

TEST(OnHeadersComplete, FileFirstThirdPartRejectedBeforeDispatch)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 0);
    ctx->onDataAvailable(ctx, "abc");
    ctx->onSectionComplete(ctx);
    ctx->onHeadersComplete(ctx, makeUpdateParametersFields(), 0);
    ctx->updateParametersString = "{}";
    ctx->onSectionComplete(ctx);

    ASSERT_EQ(ctx->state, UpdateCtx::State::WAITING_FOR_PART_HEADERS);
    ASSERT_FALSE(ctx->socketInUse);

    ctx->onHeadersComplete(ctx, makePartFields("ExtraPart"), 0);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_FALSE(ctx->isLocal);
    EXPECT_FALSE(ctx->stagedUpdateFile);
}

TEST(OnHeadersComplete, UnexpectedPartAfterDispatchFails)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA;

    ctx->onHeadersComplete(ctx, makePartFields("ExtraPart"), 0);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
}

TEST(OnHeadersComplete, UnexpectedPartDuringSatInfoWaitFails)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->state = UpdateCtx::State::WAITING_FOR_SAT_CONTROLLER_INFO_COMPLETE;

    ctx->onHeadersComplete(ctx, makePartFields("UpdateParameters"), 0);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
}

TEST(OnHeadersComplete, TrailingPartAfterCompletionIsRejected)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->state = UpdateCtx::State::UPDATE_COMPLETE;

    ctx->onHeadersComplete(ctx, makePartFields("ExtraPart"), 0);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 400);
}

TEST(ReplayStagedFileChunk, AbortsAfterRequestFailureInsteadOfWedging)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 0);
    ctx->onDataAvailable(ctx, "abc");
    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA;
    ctx->startStagedFileReplay();
    ASSERT_EQ(ctx->state, UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA);
    ASSERT_TRUE(ctx->socketInUse);

    // An async validation callback fails the request while the first
    // replay write is still in flight.
    ctx->failClientResponse();
    // The write then completes; the pump must stop, not buffer another
    // chunk or overwrite the error state.
    ctx->afterWritePartialData(ctx, {}, 3);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_FALSE(ctx->stagedUpdateFile);
    EXPECT_TRUE(ctx->pendingWriteBuffer.empty());
}

TEST(SatControllerGetComplete, BailsOutWhenRequestAlreadyFailed)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->resumeReadCb = []() {};
    ctx->state = UpdateCtx::State::UPDATE_COMPLETE_ERROR;

    std::unordered_map<std::string, boost::urls::url> satelliteInfo;
    satelliteInfo.emplace(BMCWEB_REDFISH_AGGREGATION_PREFIX,
                          boost::urls::url("https://192.168.1.1:443"));
    ctx->satControllerGetComplete(ctx, {}, 0, {}, satelliteInfo);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
}

TEST(OnParseComplete, StagedFileMissingParamsReportsUpdateParametersMissing)
{
    auto ctx = makeCtx();
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->asyncResp = asyncResp;
    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 0);
    ctx->onSectionComplete(ctx);

    ctx->onParseComplete(ctx);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_FALSE(ctx->asyncResp);
    EXPECT_NE(asyncResp->res.jsonValue.dump().find("UpdateParameters"),
              std::string::npos);
}

} // namespace
} // namespace redfish::nvidia
