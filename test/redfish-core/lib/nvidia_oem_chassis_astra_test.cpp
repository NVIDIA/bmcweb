// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.

#include "async_resp.hpp"
#include "error_messages.hpp"
#include "generated/enums/nvidia_astra.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "nvidia_oem_chassis_astra.hpp"
#include "task.hpp"
#include "utils/nvidia_astra_utils.hpp"
#include "utils/nvidia_log_entry_utils.hpp"

#include <asm-generic/errno.h>

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>

#include <cerrno>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

using namespace nvidia_oem_chassis_astra;

const nlohmann::json* findMember(const nlohmann::json& object,
                                 std::string_view name)
{
    if (!object.is_object())
    {
        return nullptr;
    }
    auto member = object.find(name);
    return member == object.end() ? nullptr : &*member;
}

const nlohmann::json* firstErrorMessage(const crow::Response& response)
{
    const nlohmann::json* error = findMember(response.jsonValue, "error");
    if (error == nullptr)
    {
        return nullptr;
    }
    const nlohmann::json* messages =
        findMember(*error, "@Message.ExtendedInfo");
    if (messages == nullptr || !messages->is_array() || messages->empty())
    {
        return nullptr;
    }
    return &messages->front();
}

const std::string* messageId(const nlohmann::json& message)
{
    const nlohmann::json* id = findMember(message, "MessageId");
    return id == nullptr ? nullptr : id->get_ptr<const std::string*>();
}

const std::string* firstMessageArg(const nlohmann::json& message)
{
    const nlohmann::json* args = findMember(message, "MessageArgs");
    if (args == nullptr || !args->is_array() || args->empty())
    {
        return nullptr;
    }
    return args->front().get_ptr<const std::string*>();
}

AstraResource testResource()
{
    return AstraResource{
        "HGX_Chassis_0",
        "/xyz/openbmc_project/inventory/system/chassis/HGX_Chassis_0/Oem/"
        "Nvidia/Astra",
        "xyz.openbmc_project.NSM"};
}

crow::Request actionRequest(std::string_view body)
{
    std::error_code ec;
    crow::Request request(std::string(body), ec);
    request.addHeader(boost::beast::http::field::content_type,
                      "application/json");
    return request;
}

TEST(NvidiaAstra, MapsAllDbusStates)
{
    EXPECT_EQ(deviceStateFromDbus(stateEnabled), DeviceState::Enabled);
    EXPECT_EQ(deviceStateFromDbus(stateDisabled), DeviceState::Disabled);
    EXPECT_EQ(deviceStateFromDbus(stateError), DeviceState::Error);
    EXPECT_EQ(deviceStateFromDbus(stateUnknown), DeviceState::Error);
    EXPECT_FALSE(deviceStateFromDbus("com.nvidia.Astra.State.Bogus"));
    EXPECT_FALSE(deviceStateFromDbus("Enabled"));
}

TEST(NvidiaAstra, FoldsActiveAndPendingIntoReportedState)
{
    using nvidia_astra::AstraState;
    EXPECT_EQ(foldAstraState(DeviceState::Enabled, DeviceState::Enabled),
              AstraState::Enabled);
    EXPECT_EQ(foldAstraState(DeviceState::Disabled, DeviceState::Disabled),
              AstraState::Disabled);
    EXPECT_EQ(foldAstraState(DeviceState::Disabled, DeviceState::Enabled),
              AstraState::PendingEnable);
    EXPECT_EQ(foldAstraState(DeviceState::Enabled, DeviceState::Disabled),
              AstraState::PendingDisable);
    EXPECT_EQ(foldAstraState(DeviceState::Error, DeviceState::Enabled),
              AstraState::Error);
    EXPECT_EQ(foldAstraState(DeviceState::Enabled, DeviceState::Error),
              AstraState::Error);
    EXPECT_EQ(foldAstraState(DeviceState::Error, DeviceState::Error),
              AstraState::Error);
}

TEST(NvidiaAstra, BuildsResourceAndFabricUris)
{
    const AstraResource resource = testResource();
    EXPECT_EQ(resource.uri(),
              "/redfish/v1/Chassis/HGX_Chassis_0/Oem/Nvidia/Astra");
    EXPECT_EQ(resource.actionUri(),
              "/redfish/v1/Chassis/HGX_Chassis_0/Oem/Nvidia/Astra/"
              "Actions/NvidiaAstra.SetAstraMode");
    EXPECT_EQ(resource.actionInfoUri(),
              "/redfish/v1/Chassis/HGX_Chassis_0/Oem/Nvidia/Astra/"
              "SetAstraModeActionInfo");
    EXPECT_EQ(fabricUri("/xyz/openbmc_project/inventory/system/fabrics/"
                        "HGX_PCIeTopology_8"),
              "/redfish/v1/Fabrics/HGX_PCIeTopology_8");
}

// An adapter whose Redfish URI cannot be found is left out of the links.
TEST(NvidiaAstra, LinksEachAdapterWithARedfishUri)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    afterAdapterUrlFound(
        asyncResp,
        "/xyz/openbmc_project/inventory/system/chassis/HGX_ConnectX_0/"
        "NetworkAdapters/ConnectX_NIC_0",
        true,
        "/redfish/v1/Chassis/HGX_ConnectX_0/NetworkAdapters/ConnectX_NIC_0");
    afterAdapterUrlFound(asyncResp, "/xyz/openbmc_project/inventory/elsewhere",
                         false, "");

    const nlohmann::json& links = asyncResp->res.jsonValue["Links"];
    EXPECT_EQ(links["NetworkAdapters@odata.count"], 1);
    EXPECT_EQ(links["NetworkAdapters"][0]["@odata.id"],
              "/redfish/v1/Chassis/HGX_ConnectX_0/NetworkAdapters/"
              "ConnectX_NIC_0");
}

// A missing association links nothing; any other read failure is an error.
TEST(NvidiaAstra, OnlyAMissingAssociationIsNotAnError)
{
    const boost::system::error_code missing(EBADR,
                                            boost::system::system_category());
    const boost::system::error_code failed(EIO,
                                           boost::system::system_category());

    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    afterNetworkAdaptersFound(asyncResp, missing, {});
    afterPCIeTopologiesFound(asyncResp, missing, {});
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);
    EXPECT_EQ(firstErrorMessage(asyncResp->res), nullptr);

    auto adaptersResp = std::make_shared<bmcweb::AsyncResp>();
    afterNetworkAdaptersFound(adaptersResp, failed, {});
    EXPECT_EQ(adaptersResp->res.result(),
              boost::beast::http::status::internal_server_error);

    auto fabricsResp = std::make_shared<bmcweb::AsyncResp>();
    afterPCIeTopologiesFound(fabricsResp, failed, {});
    EXPECT_EQ(fabricsResp->res.result(),
              boost::beast::http::status::internal_server_error);
}

TEST(NvidiaAstra, ActionInfoAdvertisesOnlyRequestableModes)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    populateActionInfo(asyncResp, testResource());

    const nlohmann::json& body = asyncResp->res.jsonValue;
    EXPECT_EQ(body["Id"], "SetAstraModeActionInfo");
    ASSERT_EQ(body["Parameters"].size(), 1);
    const nlohmann::json& parameter = body["Parameters"].front();
    EXPECT_EQ(parameter["Name"], "AstraMode");
    EXPECT_EQ(parameter["DataType"], "String");
    EXPECT_EQ(parameter["Required"], true);
    EXPECT_EQ(parameter["AllowableValues"],
              nlohmann::json::array({"Enabled", "Disabled"}));
}

TEST(NvidiaAstra, InvalidChassisUsesStandardChassisError)
{
    bool calledBack = false;
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    afterChassisValidatedForLookup(
        asyncResp, "Missing_Chassis",
        [&calledBack](const std::optional<AstraResource>&) {
            calledBack = true;
        },
        std::nullopt);

    EXPECT_FALSE(calledBack);
    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::not_found);
    const nlohmann::json* error = firstErrorMessage(asyncResp->res);
    ASSERT_NE(error, nullptr);
    const std::string* arg = firstMessageArg(*error);
    ASSERT_NE(arg, nullptr);
    EXPECT_EQ(*arg, "Chassis");
}

TEST(NvidiaAstra, SetAstraModeRejectsValuesOutsideTheEnum)
{
    auto mode = nvidia_astra::AstraMode::Invalid;
    crow::Response enabledResponse;
    crow::Request enabledRequest = actionRequest(R"({"AstraMode":"Enabled"})");
    EXPECT_TRUE(readSetAstraModeAction(enabledRequest, enabledResponse, mode));
    EXPECT_EQ(mode, nvidia_astra::AstraMode::Enabled);

    crow::Response disabledResponse;
    crow::Request disabledRequest =
        actionRequest(R"({"AstraMode":"Disabled"})");
    EXPECT_TRUE(
        readSetAstraModeAction(disabledRequest, disabledResponse, mode));
    EXPECT_EQ(mode, nvidia_astra::AstraMode::Disabled);

    crow::Response missingResponse;
    crow::Request missingRequest = actionRequest("{}");
    EXPECT_FALSE(readSetAstraModeAction(missingRequest, missingResponse, mode));
    EXPECT_EQ(missingResponse.result(),
              boost::beast::http::status::bad_request);

    crow::Response typeResponse;
    crow::Request typeRequest = actionRequest(R"({"AstraMode":true})");
    EXPECT_FALSE(readSetAstraModeAction(typeRequest, typeResponse, mode));
    EXPECT_EQ(typeResponse.result(), boost::beast::http::status::bad_request);

    // Error is a state the aggregate reports, not a mode a client may ask for.
    crow::Response stateResponse;
    crow::Request stateRequest = actionRequest(R"({"AstraMode":"Error"})");
    EXPECT_FALSE(readSetAstraModeAction(stateRequest, stateResponse, mode));
    EXPECT_EQ(stateResponse.result(), boost::beast::http::status::bad_request);
    const nlohmann::json* stateError = firstErrorMessage(stateResponse);
    ASSERT_NE(stateError, nullptr);
    const std::string* stateArg = firstMessageArg(*stateError);
    ASSERT_NE(stateArg, nullptr);
    EXPECT_EQ(*stateArg, "Error");
}

TEST(NvidiaAstra, MissingAstraInterfaceUsesResourceAndActionErrors)
{
    auto expectNotFound =
        [](const crow::Response& res, const std::string& resource) {
            EXPECT_EQ(res.result(), boost::beast::http::status::not_found);
            const nlohmann::json* error = firstErrorMessage(res);
            ASSERT_NE(error, nullptr);
            const std::string* arg = firstMessageArg(*error);
            ASSERT_NE(arg, nullptr);
            EXPECT_EQ(*arg, resource);
        };

    auto getResponse = std::make_shared<bmcweb::AsyncResp>();
    afterResourceFoundForGet(getResponse, std::nullopt);
    expectNotFound(getResponse->res, "NvidiaAstra");

    auto actionInfoResponse = std::make_shared<bmcweb::AsyncResp>();
    afterResourceFoundForActionInfo(actionInfoResponse, std::nullopt);
    expectNotFound(actionInfoResponse->res, "ActionInfo");

    crow::Request request = actionRequest(R"({"AstraMode":"Enabled"})");
    auto postResponse = std::make_shared<bmcweb::AsyncResp>();
    afterResourceFoundForPost(postResponse,
                              std::make_shared<task::Payload>(request),
                              nvidia_astra::AstraMode::Enabled, std::nullopt);
    expectNotFound(postResponse->res, "Action");
}

TEST(NvidiaAstra, UnavailableMapsToResourceInUse)
{
    const boost::system::error_code methodError(
        EIO, boost::system::system_category());
    crow::Response unavailable;
    mapSetAstraModeError(unavailable, methodError,
                         "xyz.openbmc_project.Common.Error.Unavailable");
    EXPECT_EQ(unavailable.result(), boost::beast::http::status::conflict);
    const nlohmann::json* unavailableError = firstErrorMessage(unavailable);
    ASSERT_NE(unavailableError, nullptr);
    const std::string* unavailableId = messageId(*unavailableError);
    ASSERT_NE(unavailableId, nullptr);
    const nlohmann::json resourceInUse = messages::resourceInUse();
    const std::string* expectedUnavailableId = messageId(resourceInUse);
    ASSERT_NE(expectedUnavailableId, nullptr);
    EXPECT_EQ(*unavailableId, *expectedUnavailableId);

    crow::Response other;
    mapSetAstraModeError(other, methodError,
                         "xyz.openbmc_project.Common.Error.InternalFailure");
    EXPECT_EQ(other.result(),
              boost::beast::http::status::internal_server_error);
    const nlohmann::json* otherError = firstErrorMessage(other);
    ASSERT_NE(otherError, nullptr);
    const std::string* otherId = messageId(*otherError);
    ASSERT_NE(otherId, nullptr);
    const nlohmann::json internalError = messages::internalError();
    const std::string* expectedOtherId = messageId(internalError);
    ASSERT_NE(expectedOtherId, nullptr);
    EXPECT_EQ(*otherId, *expectedOtherId);
}

// The Astra object can go away between the subtree lookup and the call.
TEST(NvidiaAstra, SetAstraModeOnAVanishedObjectIsNotFound)
{
    crow::Response response;
    mapSetAstraModeError(
        response,
        boost::system::error_code(EBADR, boost::system::system_category()),
        "org.freedesktop.DBus.Error.UnknownObject");
    EXPECT_EQ(response.result(), boost::beast::http::status::not_found);
    const nlohmann::json* error = firstErrorMessage(response);
    ASSERT_NE(error, nullptr);
    const std::string* arg = firstMessageArg(*error);
    ASSERT_NE(arg, nullptr);
    EXPECT_EQ(*arg, "Action");
}

TEST(NvidiaAstra, PropertyValueModifiedKeepsABooleanBare)
{
    for (const char* value : {"true", "false"})
    {
        nvidia_log_entry::LogEntryProperties entry;
        entry.messageId = "Base.1.19.PropertyValueModified";
        entry.messageArgs = {
            "/redfish/v1/Chassis/HGX_ConnectX_0/NetworkAdapters/ConnectX_NIC_0/"
            "Settings#/Oem/Nvidia/EastWestControlEnabled",
            value};
        auto message = formatTaskLogMessage(entry);
        if (!message)
        {
            FAIL() << "message is empty";
        }
        const nlohmann::json* args = findMember(*message, "MessageArgs");
        ASSERT_NE(args, nullptr);
        ASSERT_TRUE(args->is_array());
        ASSERT_EQ(args->size(), 2);
        const std::string* second = (*args)[1].get_ptr<const std::string*>();
        ASSERT_NE(second, nullptr);
        EXPECT_EQ(*second, value);
    }
}

TEST(NvidiaAstra, DeviceFailuresReuseTheDriverErrorMessage)
{
    nvidia_log_entry::LogEntryProperties entry;
    entry.messageId = "NvidiaResourceEvent.1.0.DeviceDriverErrorsDetected";
    entry.messageArgs = {"Astra SetAstraMode", "ConnectX_NIC_5",
                         "device is not reachable"};
    entry.severity = "Warning";
    entry.resolution = "Ensure the device is present and reachable.";
    auto deviceError = formatTaskLogMessage(entry);
    if (!deviceError)
    {
        FAIL() << "deviceError is empty";
    }
    const std::string* deviceErrorId = messageId(*deviceError);
    ASSERT_NE(deviceErrorId, nullptr);
    EXPECT_EQ(*deviceErrorId,
              "NvidiaResourceEvent.1.0.DeviceDriverErrorsDetected");
    // The registry defaults are Critical; the log entry decides both.
    EXPECT_EQ((*deviceError)["MessageSeverity"], "Warning");
    EXPECT_EQ((*deviceError)["Resolution"],
              "Ensure the device is present and reachable.");
}

TEST(NvidiaAstra, UnrelatedMessageIdsAreNotTaskMessages)
{
    nvidia_log_entry::LogEntryProperties entry;
    entry.messageId = "OpenBMC.0.4.InventoryAdded";
    entry.messageArgs = {"ConnectX_NIC_0"};
    EXPECT_FALSE(formatTaskLogMessage(entry));
}

} // namespace
} // namespace redfish
