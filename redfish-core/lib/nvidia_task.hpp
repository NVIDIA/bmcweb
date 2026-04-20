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
#include "task.hpp"
#include "task_messages.hpp"
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

inline void applyTaskServiceTaskUpdatePatch(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& taskId)
{
    std::deque<std::shared_ptr<task::TaskData>>& tasks =
        task::TaskRegistry::getInstance().getTasks();
    auto find = std::ranges::find_if(
        tasks, [&taskId](const std::shared_ptr<task::TaskData>& task) {
            if (!task)
            {
                return false;
            }
            return std::to_string(task->index) == taskId;
        });

    if (find == tasks.end())
    {
        BMCWEB_LOG_WARNING(
            "TaskService Tasks/{} Update: no local task (count={}); client may use stale id, different BMC, or task evicted",
            taskId, tasks.size());
        messages::resourceNotFound(asyncResp->res, "Tasks", taskId);
        return;
    }

    const std::shared_ptr<task::TaskData>& ptr = *find;

    std::optional<std::string> taskState;
    std::optional<nlohmann::json> messages;
    if (!json_util::readJsonPatch(req, asyncResp->res, "TaskState", taskState,
                                  "Messages", messages))
    {
        BMCWEB_LOG_WARNING(
            "TaskService Tasks/{} Update: invalid PATCH body or unsupported properties",
            taskId);
        return;
    }

    if (messages)
    {
        ptr->messages = *messages;
    }

    if (taskState && ptr->state != *taskState)
    {
        ptr->state = *taskState;
        if (ptr->state == "Completed" || ptr->state == "Cancelled" ||
            ptr->state == "Exception" || ptr->state == "Killed")
        {
            ptr->timer.cancel();
            ptr->finishTask();
            if (ptr->state == "Completed")
            {
                ptr->percentComplete = 100;
            }
        }
        task::TaskData::sendTaskEvent(ptr->state, ptr->index);
    }

    if (req.session != nullptr)
    {
        BMCWEB_LOG_DEBUG(
            "TaskService Tasks/{} Update: completed user={} state={}", taskId,
            req.session->username, ptr->state);
    }
    else
    {
        BMCWEB_LOG_DEBUG("TaskService Tasks/{} Update: completed state={}",
                         taskId, ptr->state);
    }
    asyncResp->res.result(boost::beast::http::status::no_content);
}

inline void afterTaskUpdateBiosPrivilegeCheck(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& taskId, const boost::system::error_code& ec, bool isBios)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "TaskService Tasks/{} Update: GetUserInfo failed for user {}: {}",
            taskId, req.session->username, ec.message());
        messages::internalError(asyncResp->res);
        return;
    }
    if (!isBios)
    {
        BMCWEB_LOG_WARNING(
            "TaskService Tasks/{} Update: insufficient privilege for user {} (requires redfish-hostiface)",
            taskId, req.session->username);
        messages::insufficientPrivilege(asyncResp->res);
        return;
    }

    BMCWEB_LOG_DEBUG(
        "TaskService Tasks/{} Update: hostiface authorized user={} via GetUserInfo",
        taskId, req.session->username);
    applyTaskServiceTaskUpdatePatch(req, asyncResp, taskId);
}

inline void handleTaskServiceTaskUpdate(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& strParam)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (req.session == nullptr)
    {
        BMCWEB_LOG_ERROR(
            "TaskService Tasks/{} Update: session missing after route setup",
            strParam);
        messages::insufficientPrivilege(asyncResp->res);
        return;
    }

    const std::string& taskId = strParam;

    BMCWEB_LOG_DEBUG(
        "TaskService Tasks/{} Update: PATCH user={} role={} client={} session_group_count={}",
        taskId, req.session->username, req.session->userRole,
        req.ipAddress.to_string(), req.session->userGroups.size());

    const auto& sessionGroups = req.session->userGroups;
    if (std::ranges::find(sessionGroups, "redfish-hostiface") !=
        sessionGroups.end())
    {
        BMCWEB_LOG_DEBUG(
            "TaskService Tasks/{} Update: using session redfish-hostiface user={}",
            taskId, req.session->username);
        applyTaskServiceTaskUpdatePatch(req, asyncResp, taskId);
        return;
    }

    BMCWEB_LOG_DEBUG(
        "TaskService Tasks/{} Update: resolving hostiface via GetUserInfo user={}",
        taskId, req.session->username);
    privilege_utils::isBiosPrivilege(
        req.session->username,
        [&req, asyncResp,
         taskId](const boost::system::error_code ec, const bool isBios) {
            afterTaskUpdateBiosPrivilegeCheck(req, asyncResp, taskId, ec,
                                              isBios);
        });
}

inline void requestRoutesTaskUpdate(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/TaskService/Tasks/<str>/Update/")
        .privileges(redfish::privileges::patchTask)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleTaskServiceTaskUpdate, std::ref(app)));
}

} // namespace redfish
