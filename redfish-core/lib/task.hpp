// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
// SPDX-FileCopyrightText: Copyright 2020 Intel Corporation
#pragma once

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "error_messages.hpp"
#include "event_service_manager.hpp"
#include "generated/enums/resource.hpp"
#include "generated/enums/task_service.hpp"
#include "http/parsing.hpp"
#include "http_request.hpp"
#include "http_response.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "task_messages.hpp"
#include "utils/collection.hpp"
#include "utils/etag_utils.hpp"
#include "utils/nvidia_time_utils.hpp"
#include "utils/privilege_utils.hpp"
#include "utils/time_utils.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/url/format.hpp>
#include <boost/url/url.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/message.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace redfish
{

namespace task
{
constexpr size_t maxTaskCount = 100; // arbitrary limit

struct TaskData;

class TaskRegistry
{
  public:
    TaskRegistry(const TaskRegistry&) = delete;
    TaskRegistry& operator=(const TaskRegistry&) = delete;
    TaskRegistry(TaskRegistry&&) = delete;
    TaskRegistry& operator=(TaskRegistry&&) = delete;
    ~TaskRegistry() = default;

    static TaskRegistry& getInstance()
    {
        static TaskRegistry instance;
        return instance;
    }

    std::deque<std::shared_ptr<TaskData>>& getTasks()
    {
        return allTasks;
    }

    const std::deque<std::shared_ptr<TaskData>>& getTasks() const
    {
        return allTasks;
    }

  private:
    TaskRegistry() = default;

    std::deque<std::shared_ptr<TaskData>> allTasks;
};

constexpr bool completed = true;

struct Payload
{
    explicit Payload(const crow::Request& req) :
        targetUri(req.url().encoded_path()), httpOperation(req.methodString()),
        httpHeaders(nlohmann::json::array())
    {
        using field_ns = boost::beast::http::field;
        constexpr const std::array<boost::beast::http::field, 7>
            headerWhitelist = {field_ns::accept,     field_ns::accept_encoding,
                               field_ns::user_agent, field_ns::host,
                               field_ns::connection, field_ns::content_length,
                               field_ns::upgrade};

        JsonParseResult ret = parseRequestAsJson(req, jsonBody);
        if (ret != JsonParseResult::Success)
        {
            return;
        }

        for (const auto& field : req.fields())
        {
            if (std::ranges::find(headerWhitelist, field.name()) ==
                headerWhitelist.end())
            {
                continue;
            }
            std::string header;
            header.reserve(
                field.name_string().size() + 2 + field.value().size());
            header += field.name_string();
            header += ": ";
            header += field.value();
            httpHeaders.emplace_back(std::move(header));
        }
    }
    Payload() = delete;

    std::string targetUri;
    std::string httpOperation;
    nlohmann::json httpHeaders;
    nlohmann::json jsonBody;
};

/**
 * @brief Container to hold result of the operation for long running task. Once
 * task completes task response should be set, which will be returned by the
 * task monitor URI
 *
 */
using TaskResponseCallback =
    std::move_only_function<void(const std::shared_ptr<bmcweb::AsyncResp>&)>;

/*
A task response might have json, or a callback to get the binary data.
*/
using TaskResponse =
    std::variant<std::monostate, nlohmann::json, TaskResponseCallback>;

struct TaskData : std::enable_shared_from_this<TaskData>
{
  private:
    TaskData(
        std::function<bool(boost::system::error_code, sdbusplus::message_t&,
                           const std::shared_ptr<TaskData>&)>&& handler,
        const std::string& matchIn, size_t idx) :
        callback(std::move(handler)), matchStr(matchIn), index(idx),
        startTime(std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now())),
        status("OK"), state("Running"), messages(nlohmann::json::array()),
        timer(crow::connections::systemBus->get_io_context())

    {}

  public:
    TaskData() = delete;

    static std::shared_ptr<TaskData>& createTask(
        std::function<bool(boost::system::error_code, sdbusplus::message_t&,
                           const std::shared_ptr<TaskData>&)>&& handler,
        const std::string& match)
    {
        static size_t lastTask = 0;
        struct MakeSharedHelper : public TaskData
        {
            MakeSharedHelper(
                std::function<bool(boost::system::error_code,
                                   sdbusplus::message_t&,
                                   const std::shared_ptr<TaskData>&)>&& handler,
                const std::string& match2, size_t idx) :
                TaskData(std::move(handler), match2, idx)
            {}
        };

        std::deque<std::shared_ptr<TaskData>>& tasks =
            TaskRegistry::getInstance().getTasks();

        if (tasks.size() >= maxTaskCount)
        {
            const auto taskToRemove = getTaskToRemove();

            // destroy all references
            (*taskToRemove)
                ->messages.emplace_back(messages::taskAborted(
                    std::to_string((*taskToRemove)->index)));
            (*taskToRemove)->timer.cancel();
            (*taskToRemove)->match.reset();
            tasks.erase(taskToRemove);
        }

        return tasks.emplace_back(std::make_shared<MakeSharedHelper>(
            std::move(handler), match, lastTask++));
    }

    /**
     * @brief Returns true if task is evictable (state not in activeStates).
     */
    static bool isTaskEvictable(const std::shared_ptr<TaskData>& task)
    {
        static constexpr std::array<std::string_view, 5> activeStates = {
            "Running", "Pending", "Starting", "Suspended", "Interrupted"};
        return std::ranges::find(activeStates, task->state) ==
               activeStates.end();
    }

    /**
     * @brief Comparator: returns true if a should be evicted before b (a has
     * older EndTime or is evictable when b is not).
     */
    static bool shouldEvictTaskBefore(const std::shared_ptr<TaskData>& a,
                                      const std::shared_ptr<TaskData>& b)
    {
        if (!isTaskEvictable(a))
        {
            return false;
        }
        if (!isTaskEvictable(b))
        {
            return true;
        }
        bool aHasEnd = a->endTime.has_value();
        bool bHasEnd = b->endTime.has_value();
        if (aHasEnd && !bHasEnd)
        {
            return true;
        }
        if (!aHasEnd && bHasEnd)
        {
            return false;
        }
        if (aHasEnd && bHasEnd && *a->endTime != *b->endTime)
        {
            return *a->endTime < *b->endTime;
        }
        return a->index < b->index;
    }

    /**
     * @brief Get the task to remove when at capacity.
     * Prefers evicting completed tasks (state not in activeStates) by oldest
     * EndTime (Redfish CompletedTaskOverWritePolicy: Oldest). Falls back to
     * oldest by creation only when all tasks are still running.
     */
    static std::deque<std::shared_ptr<TaskData>>::iterator getTaskToRemove()
    {
        std::deque<std::shared_ptr<TaskData>>& tasks =
            TaskRegistry::getInstance().getTasks();

        auto completedIt =
            std::ranges::min_element(tasks, shouldEvictTaskBefore);

        if (completedIt != tasks.end() && isTaskEvictable(*completedIt))
        {
            return completedIt;
        }

        // Fallback: all tasks in active states — evict oldest by creation
        // (may cause data loss for long-running tasks e.g. firmware update)
        BMCWEB_LOG_DEBUG(
            "TaskService at capacity with no completed tasks; evicting running "
            "task {} (oldest by creation). Long-running task result may be lost.",
            (*tasks.begin())->index);
        return tasks.begin();
    }

    void populateResp(crow::Response& res, size_t retryAfterSeconds = 30)
    {
        if (!endTime)
        {
            res.result(boost::beast::http::status::accepted);
            std::string strIdx = std::to_string(index);
            boost::urls::url uri =
                boost::urls::format("/redfish/v1/TaskService/Tasks/{}", strIdx);

            res.jsonValue["@odata.id"] = uri;
            res.jsonValue["@odata.type"] = "#Task.v1_4_3.Task";
            res.jsonValue["Id"] = strIdx;
            res.jsonValue["TaskState"] = state;
            res.jsonValue["TaskStatus"] = status;

            boost::urls::url taskMonitor = boost::urls::format(
                "/redfish/v1/TaskService/TaskMonitors/{}", strIdx);

            res.addHeader(boost::beast::http::field::location,
                          taskMonitor.buffer());
            res.addHeader(boost::beast::http::field::retry_after,
                          std::to_string(retryAfterSeconds));
            res.jsonValue["Name"] = "Task " + strIdx;
            res.jsonValue["StartTime"] =
                redfish::time_utils::getDateTimeStdtime(startTime);
            res.jsonValue["Messages"] = messages;
            res.jsonValue["TaskMonitor"] = taskMonitor;
            res.jsonValue["HidePayload"] = !payload;
            if (payload)
            {
                const task::Payload& p = *payload;
                nlohmann::json::object_t payloadObj;
                payloadObj["TargetUri"] = p.targetUri;
                payloadObj["HttpOperation"] = p.httpOperation;
                payloadObj["HttpHeaders"] = p.httpHeaders;
                if (p.jsonBody.is_object())
                {
                    payloadObj["JsonBody"] = p.jsonBody.dump(
                        -1, ' ', true,
                        nlohmann::json::error_handler_t::replace);
                }
                res.jsonValue["Payload"] = std::move(payloadObj);
            }
            res.jsonValue["PercentComplete"] = percentComplete;
        }
        else if (!gave204)
        {
            res.result(boost::beast::http::status::no_content);
            gave204 = true;
        }
    }

    void finishTask()
    {
        if (endTime.has_value())
        {
            return;
        }
        endTime = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
        // nvidia code
        setTaskStatus();
    }

    void extendTimer(const std::chrono::seconds& timeout)
    {
        timer.expires_after(timeout);
        timer.async_wait(
            [self = shared_from_this()](boost::system::error_code ec) {
                if (ec == boost::asio::error::operation_aborted)
                {
                    return; // completed successfully
                }
                if (!ec)
                {
                    // change ec to error as timer expired
                    ec = boost::asio::error::operation_aborted;
                }
                self->match.reset();
                sdbusplus::message_t msg;
                self->finishTask();
                self->state = "Cancelled";
                self->status = "Warning";
                self->messages.emplace_back(
                    messages::taskAborted(std::to_string(self->index)));
                // Send event :TaskAborted
                sendTaskEvent(self->state, self->index);
                self->callback(ec, msg, self);
            });
    }

    static void sendTaskEvent(std::string_view state, size_t index)
    {
        // TaskState enums which should send out an event are:
        // "Starting" = taskResumed
        // "Running" = taskStarted
        // "Suspended" = taskPaused
        // "Interrupted" = taskPaused
        // "Pending" = taskPaused
        // "Stopping" = taskAborted
        // "Completed" = taskCompletedOK
        // "Killed" = taskRemoved
        // "Exception" = taskCompletedWarning
        // "Cancelled" = taskCancelled
        nlohmann::json::object_t event;
        std::string indexStr = std::to_string(index);
        if (state == "Starting")
        {
            event = redfish::messages::taskResumed(indexStr);
        }
        else if (state == "Running")
        {
            event = redfish::messages::taskStarted(indexStr);
        }
        else if ((state == "Suspended") || (state == "Interrupted") ||
                 (state == "Pending"))
        {
            event = redfish::messages::taskPaused(indexStr);
        }
        else if (state == "Stopping")
        {
            event = redfish::messages::taskAborted(indexStr);
        }
        else if (state == "Completed")
        {
            event = redfish::messages::taskCompletedOK(indexStr);
        }
        else if (state == "Killed")
        {
            event = redfish::messages::taskRemoved(indexStr);
        }
        else if (state == "Exception")
        {
            event = redfish::messages::taskCompletedWarning(indexStr);
        }
        else if (state == "Cancelled")
        {
            event = redfish::messages::taskCancelled(indexStr);
        }
        else
        {
            BMCWEB_LOG_INFO("sendTaskEvent: No events to send");
            return;
        }
        boost::urls::url origin =
            boost::urls::format("/redfish/v1/TaskService/Tasks/{}", index);
        EventServiceManager::getInstance().sendEvent(event, origin.buffer(),
                                                     "Task");
    }

    void startTimer(const std::chrono::seconds& timeout)
    {
        if (match)
        {
            return;
        }

        if (matchStr != "0")
        {
            match = std::make_unique<sdbusplus::bus::match_t>(
                static_cast<sdbusplus::bus_t&>(*crow::connections::systemBus),
                matchStr,
                [self = shared_from_this()](sdbusplus::message_t& message) {
                    boost::system::error_code ec;

                    // callback to return True if callback is done, callback
                    // needs to update status itself if needed
                    if (self->callback(ec, message, self) == task::completed)
                    {
                        self->timer.cancel();
                        self->finishTask();

                        // Send event
                        sendTaskEvent(self->state, self->index);

                        // reset the match after the callback was successful
                        boost::asio::post(
                            crow::connections::systemBus->get_io_context(),
                            [self] { self->match.reset(); });
                        return;
                    }
                });
        }

        extendTimer(timeout);
        messages.emplace_back(messages::taskStarted(std::to_string(index)));
        // Send event : TaskStarted
        sendTaskEvent(state, index);
    }

    // nvidia code start
    /**
     * @brief Set the Task Status. Order of severity is Critical > Warning > OK.
     * Default is OK.
     *
     * @param[in] newStatus
     */
    void setTaskStatus()
    {
        for (const auto& message : messages)
        {
            std::string severity;
            if (message.contains("Severity"))
            {
                // Severity is deprecated but there are still providers that
                // use 1.0 schema.
                severity = message["Severity"].get<std::string>();
            }
            else if (message.contains("MessageSeverity"))
            {
                severity = message["MessageSeverity"].get<std::string>();
            }
            if (!severity.empty())
            {
                if (severity == "Critical")
                {
                    status = "Critical";
                    break;
                }
                if (severity == "Warning" && status != "Critical")
                {
                    status = "Warning";
                }
            }
        }
    }
    // nvidia code end
    std::function<bool(boost::system::error_code, sdbusplus::message_t&,
                       const std::shared_ptr<TaskData>&)>
        callback;
    std::string matchStr;
    size_t index;
    time_t startTime;
    std::string status;
    std::string state;
    nlohmann::json messages;
    boost::asio::steady_timer timer;
    std::unique_ptr<sdbusplus::bus::match_t> match;
    std::optional<time_t> endTime;
    std::optional<Payload> payload;
    bool taskComplete = false;
    bool gave204 = false;
    int percentComplete = 0;
    TaskResponse taskResponse;
};

} // namespace task

inline void requestRoutesTaskMonitor(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/TaskService/TaskMonitors/<str>/")
        .privileges(redfish::privileges::getTask)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& strParam) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::deque<std::shared_ptr<task::TaskData>>& tasks =
                    task::TaskRegistry::getInstance().getTasks();
                auto find = std::ranges::find_if(
                    tasks,
                    [&strParam](const std::shared_ptr<task::TaskData>& task) {
                        if (!task)
                        {
                            return false;
                        }

                        // we compare against the string version as on failure
                        // strtoul returns 0
                        return std::to_string(task->index) == strParam;
                    });

                if (find == tasks.end())
                {
                    messages::resourceNotFound(asyncResp->res, "Task",
                                               strParam);
                    return;
                }
                std::shared_ptr<task::TaskData>& ptr = *find;
                if (ptr->endTime.has_value())
                {
                    nlohmann::json* resp =
                        std::get_if<nlohmann::json>(&ptr->taskResponse);
                    if (resp != nullptr)
                    {
                        asyncResp->res.jsonValue = *resp;
                        return;
                    }
                    std::move_only_function<void(
                        const std::shared_ptr<bmcweb::AsyncResp>&)>*
                        getBodyCallback =
                            std::get_if<task::TaskResponseCallback>(
                                &ptr->taskResponse);
                    if (getBodyCallback != nullptr)
                    {
                        (*getBodyCallback)(asyncResp);
                        return;
                    }
                }
                // monitor expires after 204
                if (ptr->gave204)
                {
                    messages::resourceNotFound(asyncResp->res, "Task",
                                               strParam);
                    return;
                }
                ptr->populateResp(asyncResp->res);
            });
}

inline void requestRoutesTask(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/TaskService/Tasks/<str>/")
        .privileges(redfish::privileges::getTask)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& strParam) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::deque<std::shared_ptr<task::TaskData>>& tasks =
                    task::TaskRegistry::getInstance().getTasks();
                auto find = std::ranges::find_if(
                    tasks,
                    [&strParam](const std::shared_ptr<task::TaskData>& task) {
                        if (!task)
                        {
                            return false;
                        }

                        // we compare against the string version as on failure
                        // strtoul returns 0
                        return std::to_string(task->index) == strParam;
                    });

                if (find == tasks.end())
                {
                    messages::resourceNotFound(asyncResp->res, "Task",
                                               strParam);
                    return;
                }

                const std::shared_ptr<task::TaskData>& ptr = *find;

                asyncResp->res.jsonValue["@odata.type"] = "#Task.v1_4_3.Task";
                asyncResp->res.jsonValue["Id"] = strParam;
                asyncResp->res.jsonValue["Name"] = "Task " + strParam;
                asyncResp->res.jsonValue["TaskState"] = ptr->state;
                asyncResp->res.jsonValue["StartTime"] =
                    redfish::time_utils::getDateTimeStdtime(ptr->startTime);
                if (ptr->endTime)
                {
                    asyncResp->res.jsonValue["EndTime"] =
                        redfish::time_utils::getDateTimeStdtime(
                            *(ptr->endTime));
                }
                asyncResp->res.jsonValue["TaskStatus"] = ptr->status;
                asyncResp->res.jsonValue["Messages"] = ptr->messages;
                asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
                    "/redfish/v1/TaskService/Tasks/{}", strParam);
                if (!ptr->gave204)
                {
                    asyncResp->res.jsonValue["TaskMonitor"] =
                        boost::urls::format(
                            "/redfish/v1/TaskService/TaskMonitors/{}",
                            strParam);
                }

                asyncResp->res.jsonValue["HidePayload"] = !ptr->payload;

                if (ptr->payload)
                {
                    const task::Payload& p = *(ptr->payload);
                    asyncResp->res.jsonValue["Payload"]["TargetUri"] =
                        p.targetUri;
                    asyncResp->res.jsonValue["Payload"]["HttpOperation"] =
                        p.httpOperation;
                    asyncResp->res.jsonValue["Payload"]["HttpHeaders"] =
                        p.httpHeaders;
                    asyncResp->res.jsonValue["Payload"]["JsonBody"] =
                        p.jsonBody.dump(
                            -1, ' ', true,
                            nlohmann::json::error_handler_t::replace);
                }
                asyncResp->res.jsonValue["PercentComplete"] =
                    ptr->percentComplete;
            });
}

inline void requestRoutesTaskCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/TaskService/Tasks/")
        .privileges(redfish::privileges::getTaskCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                asyncResp->res.jsonValue["@odata.type"] =
                    "#TaskCollection.TaskCollection";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/TaskService/Tasks";
                asyncResp->res.jsonValue["Name"] = "Task Collection";
                const std::deque<std::shared_ptr<task::TaskData>>& tasks =
                    task::TaskRegistry::getInstance().getTasks();
                nlohmann::json::array_t& members =
                    collection_util::getJsonArray(asyncResp->res.jsonValue,
                                                  "Members");

                for (const std::shared_ptr<task::TaskData>& task : tasks)
                {
                    if (task == nullptr)
                    {
                        continue; // shouldn't be possible
                    }
                    nlohmann::json::object_t member;
                    member["@odata.id"] =
                        boost::urls::format("/redfish/v1/TaskService/Tasks/{}",
                                            std::to_string(task->index));
                    members.emplace_back(std::move(member));
                }
                asyncResp->res.jsonValue["Members@odata.count"] =
                    members.size();
            });
}

inline void requestRoutesTaskService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/TaskService/")
        .privileges(redfish::privileges::getTaskService)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                asyncResp->res.jsonValue["@odata.type"] =
                    "#TaskService.v1_1_4.TaskService";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/TaskService";
                asyncResp->res.jsonValue["Name"] = "Task Service";
                asyncResp->res.jsonValue["Id"] = "TaskService";
                asyncResp->res.jsonValue["DateTime"] =
                    redfish::time_utils::getDateTimeOffsetNow().first;
                asyncResp->res.jsonValue["CompletedTaskOverWritePolicy"] =
                    task_service::OverWritePolicy::Oldest;

                asyncResp->res.jsonValue["LifeCycleEventOnTaskStateChange"] =
                    true;

                asyncResp->res.jsonValue["Status"]["State"] =
                    resource::State::Enabled;
                asyncResp->res.jsonValue["ServiceEnabled"] = true;
                asyncResp->res.jsonValue["Tasks"]["@odata.id"] =
                    "/redfish/v1/TaskService/Tasks";

                etag_utils::setEtagOmitDateTimeHandler(asyncResp);
            });
}

} // namespace redfish
