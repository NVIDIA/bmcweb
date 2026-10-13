// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.

#include "bmcweb_config.h"

#include "async_resp.hpp"
#include "error_messages.hpp"
#include "http/http_body.hpp"
#include "http/http_request.hpp"
#include "http_response.hpp"
#include "io_context_singleton.hpp"
#include "multipart_parser.hpp"
#include "nvidia_multipart_update.hpp"
#include "nvidia_update_service.hpp"
#include "task.hpp"

#include <sys/types.h>
#include <unistd.h>

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/system/errc.hpp>
#include <boost/url/url.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <cstddef>
#include <cstdio>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

// Nvidia code starts here

namespace redfish::nvidia
{
namespace
{

// First message of an error response, as rendered to the client.
nlohmann::json& errorMessage(crow::Response& res)
{
    return res.jsonValue["error"]["@Message.ExtendedInfo"][0];
}

std::shared_ptr<UpdateCtx> makeCtx()
{
    std::error_code ec;
    crow::Request req("", ec);
    task::Payload payload(req);
    std::shared_ptr<UpdateCtx> ctx =
        std::make_shared<UpdateCtx>(0, std::move(payload));
    // handleUpdateServiceMultipartUpdatePostHeaders() assigns this straight
    // after construction, so no UpdateCtx reaches a callback without it.
    // Tests that report an error would otherwise dereference a null pointer.
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    return ctx;
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
        std::vector<sdbusplus::object_path>{}, FirmwarePackageInfo{},
        preUpdateValidation, []() {}, []() {});
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
        std::vector<sdbusplus::object_path>{},
        FirmwarePackageInfo{"nvfw_release.fwpkg",
                            redfish::firmwareImageLimitBytes + 1U},
        false, []() {}, [&failed]() { failed = true; });
    ctx->bytesWritten = redfish::firmwareImageLimitBytes;

    ctx->gotBytes({}, 1U);

    EXPECT_TRUE(failed);
    EXPECT_EQ(ctx->bytesWritten, redfish::firmwareImageLimitBytes);
    EXPECT_EQ(asyncResp->res.resultInt(), 413);
    EXPECT_EQ(asyncResp->res
                  .jsonValue["error"]["@Message.ExtendedInfo"][0]["MessageId"],
              "NvidiaUpdate.1.2.FirmwarePackageSizeExceeded");
    EXPECT_EQ(asyncResp->res.jsonValue["error"]["@Message.ExtendedInfo"][0]
                                      ["MessageArgs"][0],
              "nvfw_release.fwpkg");
}

std::string expectedSetHeadersOutput(const std::string& boundary,
                                     const std::string& paramsJson,
                                     const std::string& fileName = "UpdateFile")
{
    return "--" + boundary +
           "\r\nContent-Disposition: form-data; name=\"UpdateParameters\"\r\n"
           "Content-Type: application/json\r\n"
           "\r\n" +
           paramsJson + "\r\n--" + boundary +
           "\r\nContent-Disposition: form-data; name=\"UpdateFile\"; filename=\"" +
           fileName +
           "\"\r\n"
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

TEST(SetHeaders, ForwardsThePackageNameToTheSatellite)
{
    auto ctx = makeCtx();
    ctx->package.name = "nvfw_release.fwpkg";

    ctx->setHeaders({});

    // Without the filename the satellite has no way to name the package in
    // its own messages and falls back to the part name.
    EXPECT_EQ(ctx->pendingWriteBuffer,
              expectedSetHeadersOutput(
                  std::string(ctx->multipartSerializer.getBoundary()), "{}",
                  "nvfw_release.fwpkg"));
}

TEST(ParseFormPartFileName, StripsCharactersUnsafeInAQuotedString)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::beast::http::fields fields;
    fields.set(boost::beast::http::field::content_disposition,
               R"(form-data; name="UpdateFile"; filename="nv\"fw.fwpkg")");

    ctx->onHeadersComplete(ctx, fields, 0);

    // A quote would otherwise terminate the forwarded Content-Disposition
    // early when the request is relayed to a satellite.
    EXPECT_EQ(ctx->package.name.find('"'), std::string::npos);
    EXPECT_EQ(ctx->package.name.find('\\'), std::string::npos);
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

TEST(HandleStartUpdateError, UnreachableUpdateAgentIsRetryable)
{
    for (std::string_view name : updateAgentUnreachableErrors)
    {
        auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
        handleStartUpdateError(asyncResp, name);
        // The agent never saw the request, so this is transient, not a
        // service failure.
        EXPECT_EQ(asyncResp->res.resultInt(), 503) << name;
        EXPECT_EQ(errorMessage(asyncResp->res)["MessageId"],
                  "Base.1.19.ServiceTemporarilyUnavailable")
            << name;
        EXPECT_EQ(asyncResp->res.getHeaderValue("Retry-After"), "60") << name;
    }
}

TEST(HandleStartUpdateError, InvalidImageStaysAClientError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    handleStartUpdateError(
        asyncResp, "xyz.openbmc_project.Software.Update.Error.InvalidImage");

    // The package itself is bad; telling the client to retry later would be
    // misleading.
    EXPECT_EQ(asyncResp->res.resultInt(), 400);
}

TEST(HandleStartUpdateError, UnknownErrorRemainsInternal)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    handleStartUpdateError(asyncResp, "xyz.openbmc_project.Some.Other.Error");

    EXPECT_EQ(asyncResp->res.resultInt(), 500);
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
        std::vector<sdbusplus::object_path>{}, FirmwarePackageInfo{}, false,
        []() {}, []() {});
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

TEST(OnDataAvailable, BuffersUpdateFileDataBeforeUpdateStarted)
{
    auto ctx = makeCtx();
    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA;
    ctx->updateStarted = false;

    ctx->onDataAvailable(ctx, "fw data");

    EXPECT_EQ(ctx->pendingFileDataBuffer, "fw data");
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
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 400);
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageId"],
              "Base.1.19.UnrecognizedRequestBody");
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

TEST(OnParseComplete, InvalidApplyTimeIsNotReportedTwice)
{
    auto ctx = makeCtx();
    // onParseComplete() releases ctx->asyncResp once the error is final, so
    // hold our own reference to inspect the response afterwards.
    std::shared_ptr<bmcweb::AsyncResp> asyncResp = ctx->asyncResp;
    ctx->multiRet.params.applyTime = "Invalid";
    ctx->updateParametersReceived = true;
    ctx->state = UpdateCtx::State::WAITING_FOR_PART_HEADERS;

    boost::beast::http::fields fileFields;
    fileFields.set(boost::beast::http::field::content_disposition,
                   "form-data; name=\"UpdateFile\"");

    ctx->onHeadersComplete(ctx, fileFields, 0);
    // The request has already failed; finishing the body must not re-run the
    // apply-time gate and append the message a second time.
    ctx->onParseComplete(ctx);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_EQ(asyncResp->res.jsonValue["ApplyTime@Message.ExtendedInfo"].size(),
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

TEST(OnParseError, ReturnsBadRequestAfterBodyIsConsumed)
{
    auto ctx = makeCtx();
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->asyncResp = asyncResp;

    ctx->onParseError(ctx, ParserError::ERROR_BOUNDARY_FORMAT);

    EXPECT_EQ(asyncResp->res.resultInt(), 400);
    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_TRUE(ctx->responseReady);
    EXPECT_FALSE(ctx->parseComplete);
    EXPECT_TRUE(ctx->asyncResp);

    ctx->onParseComplete(ctx);

    EXPECT_TRUE(ctx->parseComplete);
    EXPECT_FALSE(ctx->asyncResp);
}

TEST(HandleMultipartHeaders, MalformedBodyReturnsBadRequest)
{
    constexpr std::string_view malformedBody = "--x--\r\n";
    std::error_code reqEc;
    crow::Request req("", reqEc);
    req.addHeader(boost::beast::http::field::content_type,
                  "multipart/form-data; boundary=x");
    req.addHeader(boost::beast::http::field::content_length,
                  std::to_string(malformedBody.size()));
    size_t completionCount = 0;
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    asyncResp->res.setCompleteRequestHandler(
        [&completionCount](crow::Response&) { completionCount++; });

    handleUpdateServiceMultipartUpdatePostHeaders(req, asyncResp);
    bmcweb::HttpBody::reader reader(req.req.base(), req.req.body());
    boost::beast::error_code ec;
    reader.init(malformedBody.size(), ec);
    ASSERT_FALSE(ec);

    EXPECT_EQ(reader.put(boost::asio::buffer(malformedBody), ec),
              malformedBody.size());
    EXPECT_FALSE(ec);
    reader.finish(ec);
    EXPECT_FALSE(ec);
    EXPECT_EQ(asyncResp->res.resultInt(), 400);

    asyncResp.reset();
    EXPECT_EQ(completionCount, 1U);
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

TEST(UpdateInProgressGate, LocalUpdateRejectedOnceTargetsKnown)
{
    redfish::fwUpdateInProgress = true;
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    completeUpdateParameters(ctx, "{}");

    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 3U);
    redfish::fwUpdateInProgress = false;

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 409);
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageId"],
              "NvidiaUpdate.1.2.FirmwareUpdateInProgress");
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageArgs"][0],
              "/redfish/v1/TaskService/Tasks");
}

TEST(UpdateInProgressGate, SoftwareUpdateRejectedAtDispatch)
{
    std::error_code ec;
    crow::Request req("", ec);
    task::Payload payload(req);
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::asio::local::stream_protocol::socket socket(getIoContext());
    bool failed = false;
    redfish::fwUpdateInProgress = true;

    startSoftwareUpdate(
        asyncResp, std::move(payload), socket, "Immediate", "xyz",
        sdbusplus::object_path("/xyz/openbmc_project/software/x"), "pkg",
        []() {}, [&failed]() { failed = true; });
    redfish::fwUpdateInProgress = false;

    EXPECT_TRUE(failed);
    EXPECT_EQ(asyncResp->res.resultInt(), 409);
    EXPECT_EQ(errorMessage(asyncResp->res)["MessageId"],
              "NvidiaUpdate.1.2.FirmwareUpdateInProgress");
}

TEST(UpdateInProgressGate, PldmUpdateRejectedAtDispatch)
{
    std::error_code ec;
    crow::Request req("", ec);
    task::Payload payload(req);
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::asio::local::stream_protocol::socket socket(getIoContext());
    bool failed = false;
    auto ctx = std::make_shared<PLDMUpdateCtx>(
        asyncResp, std::move(payload), std::move(socket), "xyz", false,
        std::vector<sdbusplus::object_path>{}, FirmwarePackageInfo{}, false,
        []() {}, [&failed]() { failed = true; });
    redfish::fwUpdateInProgress = true;

    ctx->doUpdate();
    redfish::fwUpdateInProgress = false;

    EXPECT_TRUE(failed);
    EXPECT_EQ(asyncResp->res.resultInt(), 409);
    EXPECT_EQ(errorMessage(asyncResp->res)["MessageId"],
              "NvidiaUpdate.1.2.FirmwareUpdateInProgress");
}

TEST(UpdateInProgressGate, HeaderStageDoesNotRejectWhileUpdateInFlight)
{
    // A satellite-targeted update must reach target parsing even while a
    // local update is in flight, so the request may not be rejected before
    // the body (which carries Targets) has been read.
    redfish::fwUpdateInProgress = true;
    std::error_code ec;
    crow::Request req("", ec);
    req.addHeader(boost::beast::http::field::content_type,
                  "multipart/form-data; boundary=aaa");
    req.addHeader(boost::beast::http::field::content_length, "1024");
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    handleUpdateServiceMultipartUpdatePostHeaders(req, asyncResp);
    redfish::fwUpdateInProgress = false;

    EXPECT_EQ(asyncResp->res.resultInt(), 200);
    EXPECT_TRUE(asyncResp->res.jsonValue.empty());
}

TEST(MultipartPartOrder, UnknownFirstPartIsRejected)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();

    ctx->onHeadersComplete(ctx, makePartFields("UnknownPart"), 0);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 400);
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageId"],
              "NvidiaUpdate.1.2.MalformedMultipartRequest");
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
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageId"],
              "NvidiaUpdate.1.2.FirmwarePackageSizeExceeded");
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
              "NvidiaUpdate.1.2.MalformedMultipartRequest");
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
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageId"],
              "NvidiaUpdate.1.2.MalformedMultipartRequest");
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageArgs"][0],
              "duplicate UpdateFile part");
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
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageId"],
              "NvidiaUpdate.1.2.FirmwarePackageSizeExceeded");
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
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 507);
    EXPECT_EQ(ctx->asyncResp->res
                  .jsonValue["error"]["@Message.ExtendedInfo"][0]["MessageId"],
              "NvidiaUpdate.1.2.FirmwarePackageStagingError");
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
              "NvidiaUpdate.1.2.MalformedMultipartRequest");
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
    EXPECT_EQ(ctx->asyncResp->res
                  .jsonValue["error"]["@Message.ExtendedInfo"][0]["MessageId"],
              "NvidiaUpdate.1.2.MalformedMultipartRequest");
}

TEST(MultipartPartOrder, FileFirstRejectsDuplicateUpdateFile)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 0);
    ctx->onDataAvailable(ctx, "abc");
    ctx->onSectionComplete(ctx);
    completeUpdateParameters(ctx, "{}");
    ASSERT_EQ(ctx->state, UpdateCtx::State::WAITING_FOR_PART_HEADERS);

    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 0);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_FALSE(ctx->stagedUpdateFile);
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 400);
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageId"],
              "NvidiaUpdate.1.2.MalformedMultipartRequest");
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageArgs"][0],
              "duplicate UpdateFile part");
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
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageId"],
              "NvidiaUpdate.1.2.MalformedMultipartRequest");
}

TEST(OnHeadersComplete, UnexpectedPartAfterDispatchFails)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA;

    ctx->onHeadersComplete(ctx, makePartFields("ExtraPart"), 0);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 400);
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageId"],
              "NvidiaUpdate.1.2.MalformedMultipartRequest");
}

TEST(OnHeadersComplete, UnexpectedPartDuringSatInfoWaitFails)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->state = UpdateCtx::State::WAITING_FOR_SAT_CONTROLLER_INFO_COMPLETE;

    ctx->onHeadersComplete(ctx, makePartFields("UpdateParameters"), 0);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 400);
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageId"],
              "NvidiaUpdate.1.2.MalformedMultipartRequest");
}

TEST(OnHeadersComplete, TrailingPartAfterCompletionIsRejected)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->state = UpdateCtx::State::UPDATE_COMPLETE;

    ctx->onHeadersComplete(ctx, makePartFields("ExtraPart"), 0);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 400);
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageId"],
              "NvidiaUpdate.1.2.MalformedMultipartRequest");
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
    ctx->satControllerGetComplete(ctx, {}, 0, boost::system::error_code{},
                                  satelliteInfo);

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

TEST(OnHeadersComplete, UpdateFilePartFileNameNamesThePackage)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::beast::http::fields fields;
    fields.set(boost::beast::http::field::content_disposition,
               "form-data; name=\"UpdateFile\"; "
               "filename=\"nvfw_release.fwpkg\"");

    ctx->onHeadersComplete(ctx, fields, 0);

    EXPECT_EQ(ctx->package.name, "nvfw_release.fwpkg");
}

TEST(OnHeadersComplete, UpdateFilePartFileNameIsReducedToItsBasename)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::beast::http::fields fields;
    fields.set(boost::beast::http::field::content_disposition,
               "form-data; name=\"UpdateFile\"; "
               "filename=\"/home/user/images/nvfw_release.fwpkg\"");

    ctx->onHeadersComplete(ctx, fields, 0);

    // A client-supplied path must not be echoed back in the error message.
    EXPECT_EQ(ctx->package.name, "nvfw_release.fwpkg");
}

TEST(OnHeadersComplete, UpdateFilePartFileNameSplitsOnWindowsSeparators)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::beast::http::fields fields;
    fields.set(boost::beast::http::field::content_disposition,
               "form-data; name=\"UpdateFile\"; "
               "filename=\"C:\\\\images\\\\nvfw_release.fwpkg\"");

    ctx->onHeadersComplete(ctx, fields, 0);

    // std::filesystem::path does not treat '\\' as a separator here, so the
    // directory segments would otherwise survive into the message.
    EXPECT_EQ(ctx->package.name, "nvfw_release.fwpkg");
}

TEST(OnHeadersComplete, UpdateFilePartFileNameOfOnlySeparatorsIsIgnored)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::beast::http::fields fields;
    fields.set(boost::beast::http::field::content_disposition,
               R"(form-data; name="UpdateFile"; filename="images/")");

    ctx->onHeadersComplete(ctx, fields, 0);

    // Trailing separator leaves an empty leaf; fall back to the part name
    // rather than rendering '' in the message.
    EXPECT_EQ(ctx->package.name, "UpdateFile");
}

TEST(OnHeadersComplete, UpdateFilePartWithoutFileNameKeepsThePartName)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();

    ctx->onHeadersComplete(ctx, makePartFields("UpdateFile"), 0);

    EXPECT_EQ(ctx->package.name, "UpdateFile");
}

TEST(AfterWritePartialData, SatelliteWriteFailureReportsOperationFailed)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA;
    ctx->isLocal = false;

    boost::beast::error_code ec =
        boost::system::errc::make_error_code(boost::system::errc::broken_pipe);
    ctx->afterWritePartialData(ctx, ec, 0);

    // Without a message the response ends with an empty body.
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 502);
    EXPECT_EQ(ctx->asyncResp->res
                  .jsonValue["error"]["@Message.ExtendedInfo"][0]["MessageId"],
              "Base.1.19.OperationFailed");
}

TEST(AfterWritePartialData, SatelliteWriteFailureKeepsTheSatelliteAnswer)
{
    auto ctx = makeCtx();
    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA;
    ctx->isLocal = false;
    // The satellite rejected the update while the last write was in flight.
    messages::unrecognizedRequestBody(ctx->asyncResp->res);
    ctx->responseReady = true;

    boost::beast::error_code ec =
        boost::system::errc::make_error_code(boost::system::errc::broken_pipe);
    ctx->afterWritePartialData(ctx, ec, 0);

    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 400);
    EXPECT_EQ(ctx->asyncResp->res.jsonValue.dump().find("OperationFailed"),
              std::string::npos);
}

TEST(AfterWritePartialData, SatelliteWriteFailureAfterReleaseDoesNotCrash)
{
    auto ctx = makeCtx();
    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA;
    ctx->isLocal = false;
    // The body was fully parsed and the satellite answered while the last
    // write was in flight, so the response has already been released.
    ctx->parseComplete = true;
    ctx->responseReady = true;
    ctx->releaseClientResponseIfReady();
    ASSERT_FALSE(ctx->asyncResp);

    boost::beast::error_code ec =
        boost::system::errc::make_error_code(boost::system::errc::broken_pipe);
    ctx->afterWritePartialData(ctx, ec, 0);

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
}

TEST(AfterWritePartialData, LocalWriteFailureIsNotReportedAsBadGateway)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->state = UpdateCtx::State::WAITING_FOR_UPDATE_FILE_DATA;
    ctx->isLocal = true;

    boost::beast::error_code ec =
        boost::system::errc::make_error_code(boost::system::errc::broken_pipe);
    ctx->afterWritePartialData(ctx, ec, 0);

    EXPECT_NE(ctx->asyncResp->res.resultInt(), 502);
}

TEST(HandlePostHeaders, NonMultipartContentTypeNamesTheExpectedValue)
{
    std::error_code ec;
    crow::Request req("", ec);
    req.addHeader(boost::beast::http::field::content_type,
                  "application/octet-stream");
    req.addHeader(boost::beast::http::field::content_length, "1024");
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    handleUpdateServiceMultipartUpdatePostHeaders(req, asyncResp);

    EXPECT_EQ(asyncResp->res.resultInt(), 415);
    EXPECT_EQ(asyncResp->res
                  .jsonValue["error"]["@Message.ExtendedInfo"][0]["MessageId"],
              "NvidiaUpdate.1.2.HeaderValueInvalid");
    EXPECT_EQ(asyncResp->res.jsonValue["error"]["@Message.ExtendedInfo"][0]
                                      ["MessageArgs"][2],
              "multipart/form-data");
}

// ---------------------------------------------------------------------------
// Stage 2: inventory lookup and target validation
// ---------------------------------------------------------------------------

std::shared_ptr<boost::asio::local::stream_protocol::socket> makeSocketPtr()
{
    return std::make_shared<boost::asio::local::stream_protocol::socket>(
        getIoContext());
}

task::Payload makePayload()
{
    std::error_code ec;
    crow::Request req("", ec);
    return task::Payload(req);
}

TEST(AfterGetSubtreePaths, InventoryLookupFailureIsRetryable)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    bool failed = false;

    afterGetSubtreePaths(
        asyncResp, makePayload(), makeSocketPtr(), "OnReset", false, {},
        FirmwarePackageInfo{}, false,
        boost::system::errc::make_error_code(boost::system::errc::timed_out),
        {}, []() {}, [&failed]() { failed = true; });

    // The lookup service is restarting; the client can retry rather than
    // being told the service failed internally.
    EXPECT_TRUE(failed);
    EXPECT_EQ(asyncResp->res.resultInt(), 503);
    EXPECT_EQ(errorMessage(asyncResp->res)["MessageId"],
              "Base.1.19.ServiceTemporarilyUnavailable");
}

TEST(AfterGetSubtreePathsSoftware, InventoryLookupFailureIsRetryable)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    bool failed = false;

    afterGetSubtreePathsSoftware(
        asyncResp, makePayload(), makeSocketPtr(), "HGX_FW_GPU_0", "OnReset",
        "nvfw.fwpkg",
        boost::system::errc::make_error_code(boost::system::errc::timed_out),
        {}, []() {}, [&failed]() { failed = true; });

    EXPECT_TRUE(failed);
    EXPECT_EQ(asyncResp->res.resultInt(), 503);
    EXPECT_EQ(errorMessage(asyncResp->res)["MessageId"],
              "Base.1.19.ServiceTemporarilyUnavailable");
}

TEST(AfterGetSubtreePaths, UnknownTargetIsNamedInTheError)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    bool failed = false;
    const std::string target =
        "/redfish/v1/UpdateService/FirmwareInventory/HGX_FW_GPU_0";

    // No inventory path matches, so the target is unknown.
    afterGetSubtreePaths(
        asyncResp, makePayload(), makeSocketPtr(), "OnReset", false, {target},
        FirmwarePackageInfo{}, false, boost::system::error_code{}, {}, []() {},
        [&failed]() { failed = true; });

    EXPECT_TRUE(failed);
    EXPECT_EQ(asyncResp->res.resultInt(), 400);
    EXPECT_EQ(errorMessage(asyncResp->res)["MessageId"],
              "NvidiaUpdate.1.2.FirmwareUpdateTargetInvalid");
    // The operator needs to know which entry was rejected, not just that
    // "Targets" was bad.
    EXPECT_EQ(errorMessage(asyncResp->res)["MessageArgs"][0], target);
}

TEST(AfterGetSubtreePaths, UnparsableTargetDoesNotAbort)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    bool failed = false;
    // Spaces make this unparsable as a relative reference.  The message
    // takes the value as a plain string, so no boost::urls::url_view is
    // built from it; url_view aborts on such input under BOOST_NO_EXCEPTIONS.
    const std::string target = "/redfish/v1/Chassis/HGX Chassis 0";

    afterGetSubtreePaths(
        asyncResp, makePayload(), makeSocketPtr(), "OnReset", false, {target},
        FirmwarePackageInfo{}, false, boost::system::error_code{}, {}, []() {},
        [&failed]() { failed = true; });

    EXPECT_TRUE(failed);
    EXPECT_EQ(asyncResp->res.resultInt(), 400);
    EXPECT_EQ(errorMessage(asyncResp->res)["MessageId"],
              "NvidiaUpdate.1.2.FirmwareUpdateTargetInvalid");
    EXPECT_EQ(errorMessage(asyncResp->res)["MessageArgs"][0], target);
}

// ---------------------------------------------------------------------------
// Stage 4: satellite forwarding
// ---------------------------------------------------------------------------

TEST(SatControllerGetComplete, DiscoveryFailureIsRetryable)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->state = UpdateCtx::State::WAITING_FOR_SAT_CONTROLLER_INFO_COMPLETE;

    ctx->satControllerGetComplete(
        ctx, {}, 0,
        boost::system::errc::make_error_code(boost::system::errc::timed_out),
        {});

    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 503);
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageId"],
              "Base.1.19.ServiceTemporarilyUnavailable");
}

TEST(SatControllerGetComplete, NoSatelliteConfiguredIsNotFound)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    ctx->state = UpdateCtx::State::WAITING_FOR_SAT_CONTROLLER_INFO_COMPLETE;
    const std::string target =
        "/redfish/v1/UpdateService/FirmwareInventory/HGX_FW_GPU_0";
    ctx->multiRet.params.targets = std::vector<std::string>{target};

    ctx->satControllerGetComplete(ctx, {target}, 0, boost::system::error_code{},
                                  {});

    // The resource named by the request does not exist on this system.
    EXPECT_EQ(ctx->state, UpdateCtx::State::UPDATE_COMPLETE_ERROR);
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 404);
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageId"],
              "Base.1.19.ResourceNotFound");
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageArgs"][1], target);
}

TEST(SatControllerGetComplete, NoSatelliteConfiguredNamesEveryTarget)
{
    auto ctx = makeCtx();
    ctx->state = UpdateCtx::State::WAITING_FOR_SAT_CONTROLLER_INFO_COMPLETE;
    const std::vector<std::string> targets{
        "/redfish/v1/UpdateService/FirmwareInventory/HGX_FW_GPU_0",
        "/redfish/v1/UpdateService/FirmwareInventory/HGX_FW_GPU_1"};
    ctx->multiRet.params.targets = targets;

    ctx->satControllerGetComplete(ctx, targets, 0, boost::system::error_code{},
                                  {});

    const nlohmann::json& messages =
        ctx->asyncResp->res.jsonValue["error"]["@Message.ExtendedInfo"];
    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 404);
    ASSERT_EQ(messages.size(), 2U);
    EXPECT_EQ(messages[0]["MessageArgs"][1], targets[0]);
    EXPECT_EQ(messages[1]["MessageArgs"][1], targets[1]);
}

TEST(SatControllerGetComplete, NoSatelliteConfiguredNamesTheSatelliteItself)
{
    auto ctx = makeCtx();
    ctx->state = UpdateCtx::State::WAITING_FOR_SAT_CONTROLLER_INFO_COMPLETE;
    const std::string target =
        std::format("/redfish/v1/Chassis/{}", BMCWEB_RFA_HMC_UPDATE_TARGET);
    ctx->multiRet.params.targets = std::vector<std::string>{target};

    // A target naming the satellite itself is dropped from the forwarded
    // list, so the callback receives none.
    ctx->satControllerGetComplete(ctx, {}, 0, boost::system::error_code{}, {});

    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 404);
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageId"],
              "Base.1.19.ResourceNotFound");
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageArgs"][0], "Chassis");
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageArgs"][1], target);
}

TEST(OnHttpClientDataSendComplete, UnreachableSatelliteNamesTheHost)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::urls::url host("https://172.31.13.241:8080");

    // The http client reports a connection it never established as 502 with
    // an empty body; without a message the client sees a bare status.
    crow::Response res;
    res.result(boost::beast::http::status::bad_gateway);
    ctx->onHttpClientDataSendComplete(
        ctx, std::string(BMCWEB_REDFISH_AGGREGATION_PREFIX), host, false, 0,
        res);

    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 502);
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageId"],
              "Base.1.19.CouldNotEstablishConnection");
    EXPECT_EQ(errorMessage(ctx->asyncResp->res)["MessageArgs"][0],
              host.buffer());
}

TEST(OnHttpClientDataSendComplete, SatelliteRejectionIsRelayedUnchanged)
{
    auto ctx = makeCtx();
    ctx->asyncResp = std::make_shared<bmcweb::AsyncResp>();
    boost::urls::url host("https://172.31.13.241:8080");

    // A real answer from the satellite must not be overwritten with a
    // connection error.
    crow::Response res;
    res.result(boost::beast::http::status::bad_request);
    ctx->onHttpClientDataSendComplete(
        ctx, std::string(BMCWEB_REDFISH_AGGREGATION_PREFIX), host, false, 0,
        res);

    EXPECT_EQ(ctx->asyncResp->res.resultInt(), 400);
    EXPECT_EQ(ctx->asyncResp->res.jsonValue.dump().find(
                  "CouldNotEstablishConnection"),
              std::string::npos);
}

// ---------------------------------------------------------------------------
// Request intake headers
// ---------------------------------------------------------------------------

TEST(HandlePostHeaders, MissingContentLengthIsReported)
{
    std::error_code ec;
    crow::Request req("", ec);
    req.addHeader(boost::beast::http::field::content_type,
                  "multipart/form-data; boundary=aaa");
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    handleUpdateServiceMultipartUpdatePostHeaders(req, asyncResp);

    EXPECT_EQ(asyncResp->res.resultInt(), 400);
    EXPECT_EQ(errorMessage(asyncResp->res)["MessageId"],
              "Base.1.19.HeaderMissing");
}

TEST(HandlePostHeaders, UnparsableContentLengthIsReported)
{
    std::error_code ec;
    crow::Request req("", ec);
    req.addHeader(boost::beast::http::field::content_type,
                  "multipart/form-data; boundary=aaa");
    req.addHeader(boost::beast::http::field::content_length, "not-a-number");
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();

    handleUpdateServiceMultipartUpdatePostHeaders(req, asyncResp);

    EXPECT_EQ(asyncResp->res.resultInt(), 400);
    EXPECT_EQ(errorMessage(asyncResp->res)["MessageId"],
              "Base.1.19.HeaderInvalid");
}

TEST(GetUpdateMessage, MctpDiscoveryCommandFailedRendersFromRegistry)
{
    std::vector<std::string> args{"SetEndpointID", "13",
                                  "no response received before timeout"};

    nlohmann::json msg = getUpdateMessage(
        "NvidiaResourceEvent.1.0.MCTPDiscoveryCommandFailed", args);

    ASSERT_FALSE(msg.empty());
    EXPECT_EQ(msg["MessageId"],
              "NvidiaResourceEvent.1.0.MCTPDiscoveryCommandFailed");
    EXPECT_EQ(
        msg["Message"],
        "MCTP endpoint discovery command 'SetEndpointID' failed for EID '13': no response received before timeout.");
    EXPECT_EQ(msg["MessageArgs"], nlohmann::json(args));
    EXPECT_EQ(msg["MessageSeverity"], "Critical");
    EXPECT_EQ(
        msg["Resolution"],
        "Collect the BMC logs, power-cycle the baseboard, then retry the firmware update. If the issue persists, contact support.");
}

} // namespace
} // namespace redfish::nvidia
// Nvidia code ends here
