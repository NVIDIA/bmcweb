/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION &
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

#include "bmcweb_config.h"

#include "async_resp.hpp"
#include "http_request.hpp"
#include "logging.hpp"
#include "nvidia_error_messages.hpp"
#include "task.hpp"

#include <boost/system/error_code.hpp>
#include <sdbusplus/message.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace redfish::debug_token
{

/**
 * @brief Handles task completion for debug token operations
 *
 * This function is called when a debug token task times out or an error occurs.
 *
 * @param ec Error code from the task operation
 * @param unused Unused sdbusplus message parameter
 * @param taskData Shared pointer to the task data containing operation details
 * @return bool Always returns task::completed to indicate task completion
 */
static bool taskHandler(boost::system::error_code ec,
                        sdbusplus::message_t& /*unused*/,
                        const std::shared_ptr<task::TaskData>& taskData)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Debug token operation task error: {}", ec.message());
        if (ec != boost::asio::error::operation_aborted)
        {
            taskData->messages.emplace_back(
                messages::resourceErrorsDetectedFormatError("Debug token task",
                                                            ec.message()));
        }
    }

    return task::completed;
}

/**
 * @brief Creates a new debug token task with the specified parameters
 *
 * This function creates a new task for debug token operations, sets up the
 * task handler, populates the response, and starts a timer for timeout
 * handling. The task is bound to the provided request and response objects.
 *
 * @param req The HTTP request object containing request details
 * @param asyncResp Shared pointer to the asynchronous response object
 * @param timeoutSec Timeout duration in seconds for the task
 * @return std::shared_ptr<task::TaskData> Shared pointer to the created task
 * data
 */
inline std::shared_ptr<task::TaskData> createTask(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, int timeoutSec)
{
    std::shared_ptr<task::TaskData> task =
        task::TaskData::createTask(std::bind_front(taskHandler), "0");
    task::Payload payload(req);
    task->payload.emplace(std::move(payload));
    task->populateResp(asyncResp->res);
    task->startTimer(std::chrono::seconds(timeoutSec));

    return task;
}

/**
 * @brief Adds a data location header to the task payload
 *
 * This function constructs a Redfish URI for the debug token entry and adds
 * it as a Location header to the task's HTTP headers. The URI points to the
 * specific debug token entry's attachment endpoint.
 *
 * @param task Shared pointer to the task data to add the location to
 * @param index The index of the debug token entry for the URI
 */
inline void addDataLocation(const std::shared_ptr<task::TaskData>& task,
                            const std::string& path)
{
    if (task->payload.has_value())
    {
        std::string location = "Location: " + path;
        task->payload->httpHeaders.emplace_back(std::move(location));
    }
}

/**
 * @brief Completes a debug token task and sends task events
 *
 * This function finalizes a debug token task by canceling its timer,
 * and marking the task as finished.
 *
 * @param task Shared pointer to the task data to finish
 */
inline void finishTask(const std::shared_ptr<task::TaskData>& task)
{
    task->timer.cancel();
    task->finishTask();
    task::TaskData::sendTaskEvent(task->state, task->index);
}

} // namespace redfish::debug_token
