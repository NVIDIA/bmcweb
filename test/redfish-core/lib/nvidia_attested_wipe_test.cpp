// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http_response.hpp"
#include "nvidia_attested_wipe.hpp"
#include "task.hpp"
#include "task_messages.hpp"

#include <systemd/sd-bus.h>

#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/system/error_code.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace redfish::nvidia
{
namespace
{

TEST(AttestedWipeTest, ValidatesFixedLengthHexValues)
{
    EXPECT_TRUE(isValidHexValue(std::string(p384ValueHexLength, 'a'),
                                p384ValueHexLength));
    EXPECT_TRUE(isValidHexValue(std::string(p384ValueHexLength, 'F'),
                                p384ValueHexLength));
    EXPECT_FALSE(isValidHexValue(std::string(p384ValueHexLength - 1, 'a'),
                                 p384ValueHexLength));
    EXPECT_FALSE(isValidHexValue(std::string(p384ValueHexLength, 'z'),
                                 p384ValueHexLength));
}

TEST(AttestedWipeTest, ValidatesConfigData)
{
    EXPECT_TRUE(isValidConfigData("eyJDUFVTZWN0aW9uIjoiTm9uZSJ9"));
    EXPECT_FALSE(isValidConfigData(""));
    EXPECT_FALSE(isValidConfigData("not base64!"));
}

TEST(AttestedWipeTest, ValidatesEmptyActionBody)
{
    EXPECT_TRUE(isEmptyActionBodyText(""));
    EXPECT_TRUE(isEmptyActionBodyText(" \n { } \t"));
    EXPECT_FALSE(isEmptyActionBodyText(R"({"Unexpected":true})"));
}

TEST(AttestedWipeSupportTest, ReadsEachInterfaceFromFactoryResetManager)
{
    const std::string service(factoryResetService);
    const std::string key(attestedWipeKeyInterface);
    const std::string wipe(attestedWipeInterface);

    AttestedWipeSupport both = parseAttestedWipeSupport(
        {{service, {"org.freedesktop.DBus.Peer", key, wipe}}});
    EXPECT_TRUE(both.key);
    EXPECT_TRUE(both.wipe);

    AttestedWipeSupport keyOnly = parseAttestedWipeSupport({{service, {key}}});
    EXPECT_TRUE(keyOnly.key);
    EXPECT_FALSE(keyOnly.wipe);

    AttestedWipeSupport wipeOnly =
        parseAttestedWipeSupport({{service, {wipe}}});
    EXPECT_FALSE(wipeOnly.key);
    EXPECT_TRUE(wipeOnly.wipe);

    AttestedWipeSupport none = parseAttestedWipeSupport({});
    EXPECT_FALSE(none.key);
    EXPECT_FALSE(none.wipe);
}

TEST(AttestedWipeSupportTest, IgnoresInterfacesFromAnotherService)
{
    AttestedWipeSupport support = parseAttestedWipeSupport(
        {{"xyz.openbmc_project.Other",
          {std::string(attestedWipeKeyInterface),
           std::string(attestedWipeInterface)}}});
    EXPECT_FALSE(support.key);
    EXPECT_FALSE(support.wipe);
}

TEST(AttestedWipeResourceTest, KeyOnlyListsKeyActions)
{
    crow::Response res;
    populateAttestedWipe(res, "bmc", {.key = true, .wipe = false});

    EXPECT_EQ(res.result(), boost::beast::http::status::ok);
    EXPECT_EQ(res.jsonValue["@odata.id"],
              "/redfish/v1/Managers/bmc/Oem/Nvidia/AttestedWipe");
    const nlohmann::json& actions = res.jsonValue["Actions"];
    EXPECT_EQ(actions["#NvidiaAttestedWipe.GetWipeNonce"]["target"],
              "/redfish/v1/Managers/bmc/Oem/Nvidia/AttestedWipe/Actions/"
              "NvidiaAttestedWipe.GetWipeNonce");
    EXPECT_EQ(actions["#NvidiaAttestedWipe.EnrollWipeKey"]["target"],
              "/redfish/v1/Managers/bmc/Oem/Nvidia/AttestedWipe/Actions/"
              "NvidiaAttestedWipe.EnrollWipeKey");
    EXPECT_FALSE(actions.contains("#NvidiaAttestedWipe.AttestedWipe"));
    EXPECT_FALSE(actions.contains("#NvidiaAttestedWipe.RetrieveFlashLog"));
    EXPECT_FALSE(res.jsonValue.contains("Links"));
}

TEST(AttestedWipeResourceTest, WipeOnlyListsWipeActionsAndLinks)
{
    crow::Response res;
    populateAttestedWipe(res, "bmc", {.key = false, .wipe = true});

    EXPECT_EQ(res.result(), boost::beast::http::status::ok);
    const nlohmann::json& actions = res.jsonValue["Actions"];
    EXPECT_FALSE(actions.contains("#NvidiaAttestedWipe.GetWipeNonce"));
    EXPECT_FALSE(actions.contains("#NvidiaAttestedWipe.EnrollWipeKey"));
    EXPECT_EQ(actions["#NvidiaAttestedWipe.AttestedWipe"]["target"],
              "/redfish/v1/Managers/bmc/Oem/Nvidia/AttestedWipe/Actions/"
              "NvidiaAttestedWipe.AttestedWipe");
    EXPECT_EQ(
        actions["#NvidiaAttestedWipe.AttestedWipe"]["@Redfish.ActionInfo"],
        "/redfish/v1/Managers/bmc/Oem/Nvidia/AttestedWipe/"
        "AttestedWipeActionInfo");
    EXPECT_EQ(actions["#NvidiaAttestedWipe.RetrieveFlashLog"]["target"],
              "/redfish/v1/Managers/bmc/Oem/Nvidia/AttestedWipe/Actions/"
              "NvidiaAttestedWipe.RetrieveFlashLog");
    EXPECT_EQ(res.jsonValue["Links"]["ComponentIntegrity"],
              nlohmann::json::array());
    EXPECT_EQ(res.jsonValue["Links"]["ComponentIntegrity@odata.count"], 0);
}

TEST(AttestedWipeResourceTest, NoInterfaceIsNotFound)
{
    crow::Response res;
    populateAttestedWipe(res, "bmc", {});

    EXPECT_EQ(res.result(), boost::beast::http::status::not_found);
    EXPECT_FALSE(res.jsonValue.contains("Actions"));
}

TEST(AttestedWipeResourceTest, ActionInfoRequiresWipeInterface)
{
    crow::Response found;
    populateAttestedWipeActionInfo(found, "bmc", {.key = false, .wipe = true});
    EXPECT_EQ(found.result(), boost::beast::http::status::ok);
    EXPECT_EQ(found.jsonValue["Id"], "AttestedWipeActionInfo");
    EXPECT_EQ(found.jsonValue["Parameters"].size(), 3U);

    crow::Response missing;
    populateAttestedWipeActionInfo(missing, "bmc",
                                   {.key = true, .wipe = false});
    EXPECT_EQ(missing.result(), boost::beast::http::status::not_found);
}

class AttestedWipeProgressTest : public ::testing::Test
{
  protected:
    boost::asio::io_context ioc;
    std::unique_ptr<sdbusplus::asio::connection> conn;

    void SetUp() override
    {
        try
        {
            conn = std::make_unique<sdbusplus::asio::connection>(ioc);
        }
        catch (const std::exception& e)
        {
            GTEST_SKIP() << "D-Bus not available: " << e.what();
        }
        crow::connections::systemBus = conn.get();
    }

    void TearDown() override
    {
        task::TaskRegistry::getInstance().getTasks().clear();
        crow::connections::systemBus = nullptr;
    }

    static std::shared_ptr<task::TaskData> createTestTask()
    {
        return task::TaskData::createTask(
            [](boost::system::error_code, sdbusplus::message_t&,
               const std::shared_ptr<task::TaskData>&) {
                return !task::completed;
            },
            "");
    }

    static dbus::utility::DBusPropertiesMap progressProps(uint8_t progress)
    {
        dbus::utility::DBusPropertiesMap props;
        props.emplace_back("Progress",
                           dbus::utility::DbusVariantType(progress));
        return props;
    }

    static dbus::utility::DBusPropertiesMap statusProps(
        const std::string& status)
    {
        dbus::utility::DBusPropertiesMap props;
        props.emplace_back("Status", dbus::utility::DbusVariantType(status));
        return props;
    }

    sdbusplus::message_t progressMessage(
        const std::string& iface,
        const dbus::utility::DBusPropertiesMap& values)
    {
        auto msg = conn->new_method_call(
            "xyz.openbmc_project.FactoryResetManager",
            "/xyz/openbmc_project/control/factory_reset",
            "org.freedesktop.DBus.Properties", "PropertiesChanged");
        msg.append(iface, values);
        sd_bus_message_seal(msg.get(), 0, 0);
        sd_bus_message_rewind(msg.get(), 1);
        return msg;
    }

    static dbus::utility::DBusPropertiesMap logEntryProps(
        std::vector<std::string> additionalData,
        const std::string& resolution = "")
    {
        dbus::utility::DBusPropertiesMap props;
        props.emplace_back("AdditionalData", dbus::utility::DbusVariantType(
                                                 std::move(additionalData)));
        if (!resolution.empty())
        {
            props.emplace_back("Resolution",
                               dbus::utility::DbusVariantType(resolution));
        }
        return props;
    }

    // InterfacesAdded as the Logging service emits it for a new entry.
    sdbusplus::message_t logEntryMessage(std::string iface,
                                         dbus::utility::DBusPropertiesMap props)
    {
        auto msg = conn->new_method_call(
            "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
            "org.freedesktop.DBus.ObjectManager", "InterfacesAdded");
        std::vector<std::pair<std::string, dbus::utility::DBusPropertiesMap>>
            interfaces;
        interfaces.emplace_back(std::move(iface), std::move(props));
        msg.append(
            sdbusplus::object_path("/xyz/openbmc_project/logging/entry/1"),
            interfaces);
        sd_bus_message_seal(msg.get(), 0, 0);
        sd_bus_message_rewind(msg.get(), 1);
        return msg;
    }

    static bool hasMessage(const nlohmann::json& messages,
                           const nlohmann::json::object_t& expected)
    {
        const std::string id = expected.at("MessageId").get<std::string>();
        for (const auto& message : messages)
        {
            if (message.value("MessageId", "") == id)
            {
                return true;
            }
        }
        return false;
    }
};

TEST_F(AttestedWipeProgressTest, ProgressUpdatesPercentAndStaysRunning)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    sdbusplus::message_t msg =
        progressMessage(std::string(progressInterface), progressProps(20));

    bool done = attestedWipeProgress({}, msg, taskData);

    EXPECT_FALSE(done);
    EXPECT_EQ(taskData->state, "Running");
    EXPECT_EQ(taskData->percentComplete, 20);
    EXPECT_TRUE(hasMessage(
        taskData->messages,
        messages::taskProgressChanged(std::to_string(taskData->index), 20)));
}

TEST_F(AttestedWipeProgressTest, LaterProgressValuesStayRunning)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    for (uint8_t progress : {uint8_t{10}, uint8_t{20}, uint8_t{30}})
    {
        sdbusplus::message_t msg = progressMessage(
            std::string(progressInterface), progressProps(progress));
        EXPECT_FALSE(attestedWipeProgress({}, msg, taskData));
        EXPECT_EQ(taskData->percentComplete, progress);
        EXPECT_EQ(taskData->state, "Running");
    }
}

TEST_F(AttestedWipeProgressTest, InProgressStatusDoesNotFinish)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    dbus::utility::DBusPropertiesMap props = progressProps(10);
    props.emplace_back(
        "Status",
        dbus::utility::DbusVariantType(std::string(
            "xyz.openbmc_project.Common.Progress.OperationStatus.InProgress")));
    sdbusplus::message_t msg =
        progressMessage(std::string(progressInterface), props);

    bool done = attestedWipeProgress({}, msg, taskData);

    EXPECT_FALSE(done);
    EXPECT_EQ(taskData->state, "Running");
    EXPECT_EQ(taskData->percentComplete, 10);
}

TEST_F(AttestedWipeProgressTest, FailedStatusEndsTheTask)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    dbus::utility::DBusPropertiesMap props = progressProps(20);
    props.emplace_back("Status", dbus::utility::DbusVariantType(
                                     std::string(progressStatusFailed)));
    sdbusplus::message_t msg =
        progressMessage(std::string(progressInterface), props);

    bool done = attestedWipeProgress({}, msg, taskData);

    EXPECT_TRUE(done);
    EXPECT_EQ(taskData->state, "Exception");
    EXPECT_EQ(taskData->percentComplete, 20);
    EXPECT_TRUE(
        hasMessage(taskData->messages,
                   messages::taskAborted(std::to_string(taskData->index))));
}

TEST_F(AttestedWipeProgressTest, AbortedStatusEndsTheTask)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    sdbusplus::message_t msg =
        progressMessage(std::string(progressInterface),
                        statusProps(std::string(progressStatusAborted)));

    bool done = attestedWipeProgress({}, msg, taskData);

    EXPECT_TRUE(done);
    EXPECT_EQ(taskData->state, "Exception");
    EXPECT_TRUE(
        hasMessage(taskData->messages,
                   messages::taskAborted(std::to_string(taskData->index))));
}

TEST_F(AttestedWipeProgressTest, CompletedStatusEndsTheTask)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    sdbusplus::message_t msg =
        progressMessage(std::string(progressInterface),
                        statusProps(std::string(progressStatusCompleted)));

    bool done = attestedWipeProgress({}, msg, taskData);

    EXPECT_TRUE(done);
    EXPECT_EQ(taskData->state, "Completed");
    EXPECT_TRUE(
        hasMessage(taskData->messages,
                   messages::taskCompletedOK(std::to_string(taskData->index))));
}

TEST_F(AttestedWipeProgressTest, IgnoresOtherInterfaces)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    sdbusplus::message_t msg =
        progressMessage("xyz.openbmc_project.Other", progressProps(50));

    bool done = attestedWipeProgress({}, msg, taskData);

    EXPECT_FALSE(done);
    EXPECT_EQ(taskData->percentComplete, 0);
    EXPECT_TRUE(taskData->messages.empty());
}

TEST_F(AttestedWipeProgressTest, WatchdogErrorStopsWithoutChangingState)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    taskData->state = "Cancelled";
    sdbusplus::message_t msg =
        progressMessage(std::string(progressInterface), {});
    boost::system::error_code ec = boost::asio::error::operation_aborted;

    bool done = attestedWipeProgress(ec, msg, taskData);

    EXPECT_TRUE(done);
    EXPECT_EQ(taskData->state, "Cancelled");
    EXPECT_EQ(taskData->percentComplete, 0);
}

TEST_F(AttestedWipeProgressTest, WrongProgressTypeEndsTheTask)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    dbus::utility::DBusPropertiesMap props;
    props.emplace_back("Progress",
                       dbus::utility::DbusVariantType(std::string("20")));
    sdbusplus::message_t msg =
        progressMessage(std::string(progressInterface), props);

    bool done = attestedWipeProgress({}, msg, taskData);

    EXPECT_TRUE(done);
    EXPECT_EQ(taskData->state, "Exception");
    EXPECT_TRUE(hasMessage(taskData->messages, messages::internalError()));
}

TEST_F(AttestedWipeProgressTest, WrongStatusTypeEndsTheTask)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    dbus::utility::DBusPropertiesMap props;
    props.emplace_back("Status", dbus::utility::DbusVariantType(uint8_t{1}));
    sdbusplus::message_t msg =
        progressMessage(std::string(progressInterface), props);

    bool done = attestedWipeProgress({}, msg, taskData);

    EXPECT_TRUE(done);
    EXPECT_EQ(taskData->state, "Exception");
    EXPECT_TRUE(hasMessage(taskData->messages, messages::internalError()));
}

TEST_F(AttestedWipeProgressTest, UnknownStatusDoesNotFinish)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    sdbusplus::message_t msg = progressMessage(
        std::string(progressInterface),
        statusProps(
            "xyz.openbmc_project.Common.Progress.OperationStatus.Other"));

    bool done = attestedWipeProgress({}, msg, taskData);

    EXPECT_FALSE(done);
    EXPECT_EQ(taskData->state, "Running");
}

TEST_F(AttestedWipeProgressTest, ResponseIsAcceptedWhileRunning)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    sdbusplus::message_t msg =
        progressMessage(std::string(progressInterface), {});

    handleAttestedWipeResponse(taskData, asyncResp, {}, msg);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::accepted);
    EXPECT_EQ(asyncResp->res.jsonValue["TaskState"], "Running");
    EXPECT_FALSE(asyncResp->res.jsonValue.contains("EndTime"));
}

TEST_F(AttestedWipeProgressTest, ResponseStaysAcceptedAfterCompletion)
{
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    taskData->state = "Completed";
    taskData->finishTask();
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    sdbusplus::message_t msg =
        progressMessage(std::string(progressInterface), {});

    handleAttestedWipeResponse(taskData, asyncResp, {}, msg);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::accepted);
    EXPECT_EQ(asyncResp->res.jsonValue["TaskState"], "Completed");
    EXPECT_TRUE(asyncResp->res.jsonValue.contains("EndTime"));
    EXPECT_TRUE(taskData->endTime);
}

constexpr const char* loggingEntryInterface =
    "xyz.openbmc_project.Logging.Entry";
constexpr const char* attestedWipeFailedId =
    "NvidiaFactoryReset.1.0.AttestedWipeFailed";

TEST_F(AttestedWipeProgressTest, LogEntryInAttestedWipeNamespaceIsAdded)
{
    nlohmann::json::array_t messages;
    sdbusplus::message_t msg = logEntryMessage(
        loggingEntryInterface,
        logEntryProps(
            {std::string("REDFISH_MESSAGE_ID=") + attestedWipeFailedId,
             "REDFISH_MESSAGE_ARGS=ConfigDataSignature failed",
             "namespace=AttestedWipe"}));

    logAttestedWipeFailed(msg, messages);

    ASSERT_EQ(messages.size(), 1U);
    EXPECT_EQ(messages[0]["@odata.type"], "#Message.v1_1_1.Message");
    EXPECT_EQ(messages[0]["MessageId"], attestedWipeFailedId);
    EXPECT_EQ(messages[0]["Message"],
              "Attested wipe failed. Error: ConfigDataSignature failed.");
    EXPECT_EQ(messages[0]["MessageArgs"],
              nlohmann::json::array({"ConfigDataSignature failed"}));
    EXPECT_EQ(messages[0]["MessageSeverity"], "Critical");
}

TEST_F(AttestedWipeProgressTest, LogEntryUsesTheRegistryResolution)
{
    nlohmann::json::array_t messages;
    sdbusplus::message_t msg = logEntryMessage(
        loggingEntryInterface,
        logEntryProps(
            {std::string("REDFISH_MESSAGE_ID=") + attestedWipeFailedId,
             "REDFISH_MESSAGE_ARGS=error", "namespace=AttestedWipe"}));

    logAttestedWipeFailed(msg, messages);

    ASSERT_EQ(messages.size(), 1U);
    EXPECT_EQ(messages[0]["Resolution"],
              "Read the error in this message, correct that condition, and "
              "retry the attested wipe.");
}

TEST_F(AttestedWipeProgressTest, LogEntryResolutionOverridesTheRegistry)
{
    nlohmann::json::array_t messages;
    sdbusplus::message_t msg = logEntryMessage(
        loggingEntryInterface,
        logEntryProps(
            {std::string("REDFISH_MESSAGE_ID=") + attestedWipeFailedId,
             "REDFISH_MESSAGE_ARGS=error", "namespace=AttestedWipe"},
            "Re-enroll the key."));

    logAttestedWipeFailed(msg, messages);

    ASSERT_EQ(messages.size(), 1U);
    EXPECT_EQ(messages[0]["Resolution"], "Re-enroll the key.");
}

TEST_F(AttestedWipeProgressTest, LogEntryInAnotherNamespaceIsIgnored)
{
    nlohmann::json::array_t messages;
    sdbusplus::message_t msg = logEntryMessage(
        loggingEntryInterface,
        logEntryProps(
            {std::string("REDFISH_MESSAGE_ID=") + attestedWipeFailedId,
             "REDFISH_MESSAGE_ARGS=error", "namespace=FWUpdate"}));

    logAttestedWipeFailed(msg, messages);

    EXPECT_TRUE(messages.empty());
}

TEST_F(AttestedWipeProgressTest, LogEntryWithoutNamespaceIsIgnored)
{
    nlohmann::json::array_t messages;
    sdbusplus::message_t msg = logEntryMessage(
        loggingEntryInterface,
        logEntryProps(
            {std::string("REDFISH_MESSAGE_ID=") + attestedWipeFailedId,
             "REDFISH_MESSAGE_ARGS=error"}));

    logAttestedWipeFailed(msg, messages);

    EXPECT_TRUE(messages.empty());
}

TEST_F(AttestedWipeProgressTest, OtherInterfacesAreIgnored)
{
    nlohmann::json::array_t messages;
    sdbusplus::message_t msg = logEntryMessage(
        "xyz.openbmc_project.Other",
        logEntryProps(
            {std::string("REDFISH_MESSAGE_ID=") + attestedWipeFailedId,
             "REDFISH_MESSAGE_ARGS=error", "namespace=AttestedWipe"}));

    logAttestedWipeFailed(msg, messages);

    EXPECT_TRUE(messages.empty());
}

TEST_F(AttestedWipeProgressTest, UnknownMessageIdIsIgnored)
{
    nlohmann::json::array_t messages;
    sdbusplus::message_t msg = logEntryMessage(
        loggingEntryInterface,
        logEntryProps(
            {"REDFISH_MESSAGE_ID=NvidiaFactoryReset.1.0.NoSuchMessage",
             "REDFISH_MESSAGE_ARGS=error", "namespace=AttestedWipe"}));

    logAttestedWipeFailed(msg, messages);

    EXPECT_TRUE(messages.empty());
}

TEST_F(AttestedWipeProgressTest, LogEntryWithoutAdditionalDataIsIgnored)
{
    nlohmann::json::array_t messages;
    dbus::utility::DBusPropertiesMap props;
    props.emplace_back("Resolution",
                       dbus::utility::DbusVariantType(std::string("None.")));
    sdbusplus::message_t msg =
        logEntryMessage(loggingEntryInterface, std::move(props));

    logAttestedWipeFailed(msg, messages);

    EXPECT_TRUE(messages.empty());
}

TEST_F(AttestedWipeProgressTest, EmptyOrUnrelatedAdditionalDataIsIgnored)
{
    nlohmann::json::array_t messages;
    sdbusplus::message_t emptyMsg =
        logEntryMessage(loggingEntryInterface, logEntryProps({}));
    sdbusplus::message_t unrelatedMsg = logEntryMessage(
        loggingEntryInterface, logEntryProps({"FOO=bar", "BAZ=qux"}));

    logAttestedWipeFailed(emptyMsg, messages);
    logAttestedWipeFailed(unrelatedMsg, messages);

    EXPECT_TRUE(messages.empty());
}

TEST_F(AttestedWipeProgressTest, LogEntryAfterTheTaskFailedKeepsBothMessages)
{
    // dpu-manager creates the log entry and then fails the progress, so the
    // task can end before the entry arrives.
    std::shared_ptr<task::TaskData> taskData = createTestTask();
    sdbusplus::message_t failed =
        progressMessage(std::string(progressInterface),
                        statusProps(std::string(progressStatusFailed)));
    EXPECT_TRUE(attestedWipeProgress({}, failed, taskData));
    ASSERT_EQ(taskData->state, "Exception");

    sdbusplus::message_t msg = logEntryMessage(
        loggingEntryInterface,
        logEntryProps(
            {std::string("REDFISH_MESSAGE_ID=") + attestedWipeFailedId,
             "REDFISH_MESSAGE_ARGS=error", "namespace=AttestedWipe"}));
    logAttestedWipeFailed(msg, taskData->messages);

    EXPECT_EQ(taskData->state, "Exception");
    EXPECT_TRUE(
        hasMessage(taskData->messages,
                   messages::taskAborted(std::to_string(taskData->index))));
    bool found = false;
    for (const auto& message : taskData->messages)
    {
        found = found || message.value("MessageId", "") == attestedWipeFailedId;
    }
    EXPECT_TRUE(found);
}

} // namespace
} // namespace redfish::nvidia
