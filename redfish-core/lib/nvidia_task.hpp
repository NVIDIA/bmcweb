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
inline void requestRoutesTaskUpdate(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/TaskService/Tasks/<str>/Update/")
        .privileges(redfish::privileges::patchTask)
        .methods(
            boost::beast::http::verb::
                patch)([&app](
                           const crow::Request& req,
                           const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& strParam) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            privilege_utils::isBiosPrivilege(
                req.session->username,
                [&req, asyncResp, strParam](const boost::system::error_code ec,
                                            const bool isBios) {
                    if (ec || !isBios)
                    {
                        asyncResp->res.addHeader(
                            boost::beast::http::field::allow, "");
                        messages::resourceNotFound(asyncResp->res, "",
                                                   "Update");
                        return;
                    }

                    auto find = std::find_if(
                        task::tasks.begin(), task::tasks.end(),
                        [&strParam](
                            const std::shared_ptr<task::TaskData>& task) {
                            if (!task)
                            {
                                return false;
                            }

                            // we compare against the string version as on
                            // failure strtoul returns 0
                            return std::to_string(task->index) == strParam;
                        });

                    if (find == task::tasks.end())
                    {
                        messages::resourceNotFound(asyncResp->res, "Tasks",
                                                   strParam);
                        return;
                    }

                    const std::shared_ptr<task::TaskData>& ptr = *find;

                    std::optional<std::string> taskState;
                    std::optional<nlohmann::json> messages;
                    if (!json_util::readJsonPatch(req, asyncResp->res,
                                                  "TaskState", taskState,
                                                  "Messages", messages))
                    {
                        BMCWEB_LOG_DEBUG(
                            "/redfish/v1/TaskService/Tasks/<str>/Update/ readJsonPatch error");
                        return;
                    }

                    if (messages)
                    {
                        ptr->messages = *messages;
                    }

                    if (taskState && ptr->state != *taskState)
                    {
                        ptr->state = *taskState;
                        if (ptr->state == "Completed" ||
                            ptr->state == "Cancelled" ||
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

                    asyncResp->res.result(
                        boost::beast::http::status::no_content);
                });
        });
}

} // namespace redfish
