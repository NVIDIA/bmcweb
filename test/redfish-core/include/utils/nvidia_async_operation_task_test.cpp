// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.

#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http/http_request.hpp"
#include "task.hpp"
#include "task_messages.hpp"
#include "utils/nvidia_async_operation_task.hpp"
#include "utils/nvidia_async_set_utils.hpp"
#include "utils/nvidia_log_entry_utils.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace redfish::nvidia_async_operation_utils
{
namespace
{

const std::string operationPath = "/xyz/openbmc_project/async/status/3";
const std::string noSuchService = "xyz.openbmc_project.Test.NoSuchService";

std::optional<nlohmann::json> formatEntry(
    const nvidia_log_entry::LogEntryProperties& entry)
{
    return nlohmann::json{{"MessageId", entry.messageId},
                          {"Entry", entry.objectPath}};
}

nvidia_log_entry::LogEntryProperties logEntry(
    std::string path, std::string eventId, std::string logNamespace = {})
{
    nvidia_log_entry::LogEntryProperties entry;
    entry.objectPath = std::move(path);
    entry.eventId = std::move(eventId);
    entry.logNamespace = std::move(logNamespace);
    entry.messageId = "Base.1.19.PropertyValueModified";
    return entry;
}

task::Payload payload()
{
    boost::system::error_code ec;
    crow::Request req("", ec);
    return task::Payload(req);
}

TEST(NvidiaAsyncOperationTask, ReadsALogEntryFromItsInterfaces)
{
    const sdbusplus::object_path logPath(
        "/xyz/openbmc_project/logging/entry/42");
    dbus::utility::DBusInterfacesMap interfaces{{
        "xyz.openbmc_project.Logging.Entry",
        {
            {"AdditionalData",
             std::vector<std::pair<std::string, std::string>>{
                 {"REDFISH_MESSAGE_ID",
                  "NvidiaResourceEvent.1.0.DeviceDriverErrorsDetected"},
                 {"REDFISH_MESSAGE_ARGS",
                  "Astra SetAstraMode,ConnectX_NIC_5,device is not reachable"},
                 {"namespace", "Astra"}}},
            {"EventId", operationPath},
            {"Resolution", "Retry the operation."},
            {"Severity", "xyz.openbmc_project.Logging.Entry.Level.Warning"},
        },
    }};

    auto entry = nvidia_log_entry::LogEntryProperties::fromInterfaces(
        logPath, interfaces);
    if (!entry)
    {
        FAIL() << "entry is empty";
    }
    EXPECT_EQ(entry->objectPath, logPath.str);
    EXPECT_EQ(entry->eventId, operationPath);
    EXPECT_EQ(entry->messageId,
              "NvidiaResourceEvent.1.0.DeviceDriverErrorsDetected");
    EXPECT_EQ(entry->messageArgs.size(), 3);
    EXPECT_EQ(entry->logNamespace, "Astra");
    EXPECT_EQ(entry->severity, "Warning");
}

TEST(NvidiaAsyncOperationTask, EndsWithTheOverallOutcome)
{
    nlohmann::json success = nlohmann::json::array();
    appendTaskOutcomeMessages(success, 7, true);
    ASSERT_EQ(success.size(), 2);
    EXPECT_EQ(success[0]["MessageId"], messages::success()["MessageId"]);
    EXPECT_EQ(success[1]["MessageId"],
              messages::taskCompletedOK("7")["MessageId"]);

    nlohmann::json failure = nlohmann::json::array();
    appendTaskOutcomeMessages(failure, 8, false);
    ASSERT_EQ(failure.size(), 2);
    EXPECT_EQ(failure[0]["MessageId"],
              messages::operationFailed()["MessageId"]);
    EXPECT_EQ(failure[1]["MessageId"], messages::taskAborted("8")["MessageId"]);
}

// Tasks and the log watch install matches, so these need a bus.
class NvidiaAsyncOperationTaskOnBus : public ::testing::Test
{
  protected:
    boost::asio::io_context ioc;
    std::unique_ptr<sdbusplus::asio::connection> conn;

    void SetUp() override
    {
        conn = std::make_unique<sdbusplus::asio::connection>(ioc);
        crow::connections::systemBus = conn.get();
    }

    void TearDown() override
    {
        task::TaskRegistry::getInstance().getTasks().clear();
        crow::connections::systemBus = nullptr;
    }

    static std::shared_ptr<task::TaskData> startTask()
    {
        auto taskData = task::TaskData::createTask(
            [](const boost::system::error_code&, sdbusplus::message_t&,
               const std::shared_ptr<task::TaskData>&) {
                return !task::completed;
            },
            "0");
        taskData->startTimer(std::chrono::seconds(1));
        return taskData;
    }

    /** @brief The log entries the newest task lists. */
    static std::vector<std::string> listedEntries()
    {
        std::vector<std::string> listed;
        for (const auto& message :
             task::TaskRegistry::getInstance().getTasks().back()->messages)
        {
            if (message.contains("Entry"))
            {
                listed.emplace_back(message["Entry"]);
            }
        }
        return listed;
    }
};

// Entries come from before the method returned and after; only those tagged
// with this operation's path and namespace are its own.
TEST_F(NvidiaAsyncOperationTaskOnBus, ListsOnlyTheOperationsOwnLogs)
{
    const std::string otherPath = "/xyz/openbmc_project/async/status/4";
    auto operationTask = OperationTask::watch("Astra");
    operationTask->addLog(logEntry("/xyz/openbmc_project/logging/entry/42",
                                   operationPath, "Astra"));
    operationTask->addLog(
        logEntry("/xyz/openbmc_project/logging/entry/43", otherPath, "Astra"));
    operationTask->addLog(logEntry("/xyz/openbmc_project/logging/entry/44",
                                   operationPath, "Other"));

    operationTask->start(std::make_shared<bmcweb::AsyncResp>(), payload(),
                         noSuchService, operationPath, formatEntry,
                         std::chrono::seconds(60));
    operationTask->addLog(logEntry("/xyz/openbmc_project/logging/entry/45",
                                   operationPath, "Astra"));
    operationTask->addLog(
        logEntry("/xyz/openbmc_project/logging/entry/46", otherPath, "Astra"));
    operationTask->addLog(
        logEntry("/xyz/openbmc_project/logging/entry/47", operationPath));

    EXPECT_EQ(
        listedEntries(),
        (std::vector<std::string>{"/xyz/openbmc_project/logging/entry/42",
                                  "/xyz/openbmc_project/logging/entry/45"}));
}

TEST_F(NvidiaAsyncOperationTaskOnBus, KeepsABoundedNumberOfEarlyLogs)
{
    auto operationTask = OperationTask::watch();
    for (size_t i = 0; i < maxEarlyLogs + 10; ++i)
    {
        operationTask->addLog(
            logEntry("/xyz/openbmc_project/logging/entry/" + std::to_string(i),
                     operationPath));
    }

    operationTask->start(std::make_shared<bmcweb::AsyncResp>(), payload(),
                         noSuchService, operationPath, formatEntry,
                         std::chrono::seconds(60));

    EXPECT_EQ(listedEntries().size(), maxEarlyLogs);
}

// Only the status match in task.hpp stops the timer itself; a status read
// that completes the task must too, or the timer rewrites it as Cancelled.
TEST_F(NvidiaAsyncOperationTaskOnBus, StatusReadOutsideTheMatchStopsTheTimer)
{
    auto operationTask = OperationTask::watch();
    operationTask->start(std::make_shared<bmcweb::AsyncResp>(), payload(),
                         noSuchService, operationPath, formatEntry,
                         std::chrono::seconds(1));
    auto taskData = task::TaskRegistry::getInstance().getTasks().back();

    operationTask->handleStatusRead({}, std::string(asyncStatusValueSuccess));
    const size_t messageCount = taskData->messages.size();
    ioc.run_for(std::chrono::milliseconds(1500));

    EXPECT_EQ(taskData->state, "Completed");
    EXPECT_EQ(taskData->messages.size(), messageCount);
}

// A status read that answers after the timeout leaves the task Cancelled.
TEST_F(NvidiaAsyncOperationTaskOnBus, LateStatusReadAfterTimeoutIsIgnored)
{
    auto operationTask = OperationTask::watch();
    operationTask->start(std::make_shared<bmcweb::AsyncResp>(), payload(),
                         noSuchService, operationPath, formatEntry,
                         std::chrono::seconds(1));
    auto taskData = task::TaskRegistry::getInstance().getTasks().back();
    ioc.run_for(std::chrono::milliseconds(1500));
    ASSERT_EQ(taskData->state, "Cancelled");
    const size_t messageCount = taskData->messages.size();

    operationTask->handleStatusRead({}, std::string(asyncStatusValueSuccess));

    EXPECT_EQ(taskData->state, "Cancelled");
    EXPECT_EQ(taskData->messages.size(), messageCount);
}

// Eviction drops a running task without calling it back.
TEST_F(NvidiaAsyncOperationTaskOnBus, EvictedTaskReleasesItsOperationTask)
{
    std::weak_ptr<OperationTask> released;
    {
        auto operationTask = OperationTask::watch();
        released = operationTask;
        operationTask->start(std::make_shared<bmcweb::AsyncResp>(), payload(),
                             noSuchService, operationPath, formatEntry,
                             std::chrono::seconds(60));
    }

    for (size_t i = 0; i < task::maxTaskCount; ++i)
    {
        startTask();
    }
    // The status read holds the task until the missing service's error reply.
    ioc.run_for(std::chrono::milliseconds(200));

    EXPECT_TRUE(released.expired());
}

} // namespace
} // namespace redfish::nvidia_async_operation_utils
