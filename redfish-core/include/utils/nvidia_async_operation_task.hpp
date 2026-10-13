/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"
#include "task.hpp"
#include "task_messages.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/nvidia_async_set_utils.hpp"
#include "utils/nvidia_log_entry_utils.hpp"

#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/exception.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// A task that follows a D-Bus operation's status and its log entries.
namespace redfish::nvidia_async_operation_utils
{

constexpr std::string_view operationLogPath = "/xyz/openbmc_project/logging";
// Bounds the entries kept while the method call is still pending.
constexpr size_t maxEarlyLogs = 64;

/** @brief The task message for a log entry, or empty to leave it out. */
using TaskMessageFormatter = std::function<std::optional<nlohmann::json>(
    const nvidia_log_entry::LogEntryProperties&)>;

inline void appendTaskOutcomeMessages(nlohmann::json& taskMessages,
                                      size_t taskIndex, bool success)
{
    const std::string index = std::to_string(taskIndex);
    if (success)
    {
        taskMessages.emplace_back(messages::success());
        taskMessages.emplace_back(messages::taskCompletedOK(index));
        return;
    }

    taskMessages.emplace_back(messages::operationFailed());
    taskMessages.emplace_back(messages::taskAborted(index));
}

inline void completeTask(const std::shared_ptr<task::TaskData>& taskData,
                         bool succeeded)
{
    taskData->taskComplete = true;
    taskData->state = succeeded ? "Completed" : "Exception";
    if (succeeded)
    {
        taskData->percentComplete = 100;
    }
    appendTaskOutcomeMessages(taskData->messages, taskData->index, succeeded);
    taskData->finishTask();
}

/** @brief One operation's task, and the log entries it collects. */
class OperationTask : public std::enable_shared_from_this<OperationTask>
{
  public:
    /** @brief Start collecting log entries, optionally from one namespace. */
    static std::shared_ptr<OperationTask> watch(std::string logNamespace = {});

    /** @brief Answer the request with a task for the operation. */
    void start(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               task::Payload&& payload, const std::string& service,
               const std::string& path, TaskMessageFormatter formatter,
               std::chrono::seconds timeout);

    /** @brief Add a log entry to the task, or hold it until the task starts. */
    void addLog(nvidia_log_entry::LogEntryProperties&& entry);

    /** @brief Apply a status read outside task.hpp's match. */
    void handleStatusRead(const boost::system::error_code& ec,
                          const std::string& status);

  private:
    enum class State
    {
        Watching,
        Running,
        Finished,
    };

    struct LogReceived
    {
        nvidia_log_entry::LogEntryProperties entry;
    };
    // From task.hpp's match or the one-time read.
    struct StatusReceived
    {
        std::string status;
    };
    // From task.hpp's timer, which has already marked the task Cancelled.
    struct TimedOut
    {};

    using Event = std::variant<LogReceived, StatusReceived, TimedOut>;

    /** @brief Apply @p event as the current state allows. */
    void handle(Event event);
    void holdLog(LogReceived& event);
    void onRunning(LogReceived& event);
    void onRunning(const StatusReceived& event);
    void onRunning(const TimedOut& event);

    static void handleLogSignal(const std::weak_ptr<OperationTask>& weakTask,
                                sdbusplus::message_t& msg);
    bool handleStatusSignal(const boost::system::error_code& ec,
                            sdbusplus::message_t& msg,
                            const std::shared_ptr<task::TaskData>& taskData);

    /** @brief Whether @p entry has an EventId and the expected namespace. */
    bool shouldAddLog(const nvidia_log_entry::LogEntryProperties& entry) const;

    State state = State::Watching;
    std::unique_ptr<sdbusplus::bus::match_t> logMatch;
    std::string namespaceFilter;
    // Entries logged before the operation path was known.
    std::vector<nvidia_log_entry::LogEntryProperties> earlyLogs;
    std::string operationPath;
    TaskMessageFormatter formatMessage;
    std::weak_ptr<task::TaskData> task;
};

inline std::shared_ptr<OperationTask> OperationTask::watch(
    std::string logNamespace)
{
    auto operationTask = std::make_shared<OperationTask>();
    operationTask->namespaceFilter = std::move(logNamespace);
    operationTask->logMatch = std::make_unique<sdbusplus::bus::match_t>(
        *crow::connections::systemBus,
        sdbusplus::bus::match::rules::interfacesAdded(
            std::string(operationLogPath)),
        std::bind_front(handleLogSignal,
                        std::weak_ptr<OperationTask>(operationTask)));
    return operationTask;
}

inline void OperationTask::start(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    task::Payload&& payload, const std::string& service,
    const std::string& path, TaskMessageFormatter formatter,
    std::chrono::seconds timeout)
{
    operationPath = path;
    formatMessage = std::move(formatter);

    std::shared_ptr<task::TaskData> taskData = task::TaskData::createTask(
        std::bind_front(&OperationTask::handleStatusSignal, shared_from_this()),
        sdbusplus::bus::match::rules::propertiesChanged(
            operationPath, asyncStatusInterfaceName));
    task = taskData;
    state = State::Running;

    taskData->startTimer(timeout);
    for (auto& entry : std::exchange(earlyLogs, {}))
    {
        handle(LogReceived{std::move(entry)});
    }
    taskData->populateResp(asyncResp->res);
    taskData->payload.emplace(std::move(payload));

    dbus::utility::getProperty<std::string>(
        service, operationPath, asyncStatusInterfaceName,
        asyncStatusPropertyName,
        std::bind_front(&OperationTask::handleStatusRead, shared_from_this()));
}

inline void OperationTask::addLog(nvidia_log_entry::LogEntryProperties&& entry)
{
    handle(LogReceived{std::move(entry)});
}

inline void OperationTask::handleStatusRead(const boost::system::error_code& ec,
                                            const std::string& status)
{
    if (ec)
    {
        // The status match can still report the outcome.
        BMCWEB_LOG_ERROR("Unable to read the status of {}: {}", operationPath,
                         ec.message());
        return;
    }

    const bool wasRunning = state == State::Running;
    handle(StatusReceived{status});
    auto taskData = task.lock();
    if (!wasRunning || state != State::Finished || !taskData)
    {
        return;
    }
    // task.hpp does this only for its own match.
    taskData->timer.cancel();
    taskData->match.reset();
    task::TaskData::sendTaskEvent(taskData->state, taskData->index);
}

inline void OperationTask::handle(Event event)
{
    switch (state)
    {
        case State::Watching:
            // Only log entries arrive before start creates the task.
            if (auto* log = std::get_if<LogReceived>(&event))
            {
                holdLog(*log);
            }
            return;
        case State::Running:
            std::visit([this](auto& alternative) { onRunning(alternative); },
                       event);
            return;
        case State::Finished:
        default:
            return;
    }
}

inline void OperationTask::holdLog(LogReceived& event)
{
    if (shouldAddLog(event.entry) && earlyLogs.size() < maxEarlyLogs)
    {
        earlyLogs.emplace_back(std::move(event.entry));
    }
}

inline void OperationTask::onRunning(LogReceived& event)
{
    if (!shouldAddLog(event.entry) || event.entry.eventId != operationPath)
    {
        return;
    }
    auto taskData = task.lock();
    if (!taskData)
    {
        return;
    }
    auto message = formatMessage(event.entry);
    if (!message)
    {
        BMCWEB_LOG_DEBUG("Skipping operation log with message id {}",
                         event.entry.messageId);
        return;
    }
    taskData->messages.emplace_back(std::move(*message));
}

inline void OperationTask::onRunning(const StatusReceived& event)
{
    auto taskData = task.lock();
    if (event.status == asyncStatusValueInProgress || !taskData)
    {
        return;
    }
    completeTask(taskData, event.status == asyncStatusValueSuccess);
    state = State::Finished;
    logMatch.reset();
}

inline void OperationTask::onRunning(const TimedOut& /*event*/)
{
    state = State::Finished;
    logMatch.reset();
}

inline void OperationTask::handleLogSignal(
    const std::weak_ptr<OperationTask>& weakTask, sdbusplus::message_t& msg)
{
    auto operationTask = weakTask.lock();
    if (!operationTask)
    {
        return;
    }

    auto entry = nvidia_log_entry::LogEntryProperties::fromInterfacesAdded(msg);
    if (entry)
    {
        operationTask->addLog(std::move(*entry));
    }
}

inline bool OperationTask::handleStatusSignal(
    const boost::system::error_code& ec, sdbusplus::message_t& msg,
    const std::shared_ptr<task::TaskData>& /*taskData*/)
{
    // task.hpp passes an error only from its timer.
    if (ec)
    {
        handle(TimedOut{});
        return task::completed;
    }

    std::string interface;
    dbus::utility::DBusPropertiesMap properties;
    try
    {
        msg.read(interface, properties);
    }
    catch (const sdbusplus::exception_t& e)
    {
        BMCWEB_LOG_ERROR("Unable to read PropertiesChanged: {}", e.what());
        return !task::completed;
    }

    if (interface != asyncStatusInterfaceName)
    {
        return !task::completed;
    }

    const std::string* status = nullptr;
    if (!sdbusplus::unpackPropertiesNoThrow(dbus_utils::UnpackErrorPrinter(),
                                            properties, "Status", status) ||
        status == nullptr)
    {
        return !task::completed;
    }

    const bool wasRunning = state == State::Running;
    handle(StatusReceived{*status});
    return wasRunning && state == State::Finished
               ? task::completed
               : !task::completed;
}

inline bool OperationTask::shouldAddLog(
    const nvidia_log_entry::LogEntryProperties& entry) const
{
    if (entry.eventId.empty())
    {
        return false;
    }
    return namespaceFilter.empty() || entry.logNamespace == namespaceFilter;
}

} // namespace redfish::nvidia_async_operation_utils
