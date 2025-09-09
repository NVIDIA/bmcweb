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

#include "dbus_utility.hpp"
#include "debug_token/base.hpp"
#include "debug_token/unified/action.hpp"
#include "debug_token/unified/utils.hpp"
#include "logging.hpp"
#include "nvidia_messages.hpp"
#include "task.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace redfish::debug_token::status
{

using Result = std::string;
using ResultCallback =
    std::function<void(const std::shared_ptr<task::TaskData>&, Result)>;

/**
 * @brief Handler class for managing debug token status operations
 *
 * This class handles the asynchronous retrieval of debug token status
 * information from D-Bus objects. It manages the lifecycle of status
 * operations, including progress tracking, error handling, and result
 * aggregation.
 */
class Handler : public std::enable_shared_from_this<Handler>
{
  public:
    // Delete copy constructor and assignment operator since this class manages
    // shared state and should not be copied
    Handler(const Handler&) = delete;
    Handler& operator=(const Handler&) = delete;

    // Delete move constructor and assignment operator since this class manages
    // shared state and should not be moved
    Handler(Handler&&) = delete;
    Handler& operator=(Handler&&) = delete;

    /**
     * @brief Start a debug token status operation for all available objects
     *
     * This method initiates a status operation that will discover and query all
     * D-Bus objects implementing the debug token status interface.
     *
     * @param taskPointer Reference to the task data for progress tracking
     * @param cb Callback function to be called when the operation completes
     */
    static void startOperation(std::shared_ptr<task::TaskData>& taskPointer,
                               ResultCallback cb)
    {
        struct MakeSharedHelper : public Handler
        {
            MakeSharedHelper(const std::shared_ptr<task::TaskData>& taskPointer,
                             ResultCallback cb) :
                Handler(taskPointer, std::move(cb))
            {}
        };
        std::shared_ptr<Handler> handler =
            std::make_shared<MakeSharedHelper>(taskPointer, std::move(cb));
        handler->run();
    }

    /**
     * @brief Start a debug token status operation for specific objects
     *
     * This method initiates a status operation for a predefined list of D-Bus
     * objects, bypassing the discovery phase.
     *
     * @param objects Vector of tuples containing service names and object paths
     * @param taskPointer Reference to the task data for progress tracking
     * @param cb Callback function to be called when the operation completes
     */
    static void startOperation(
        const std::vector<std::tuple<std::string, std::string>>& objects,
        std::shared_ptr<task::TaskData>& taskPointer, ResultCallback cb)
    {
        struct MakeSharedHelper : public Handler
        {
            MakeSharedHelper(const std::shared_ptr<task::TaskData>& taskPointer,
                             ResultCallback cb) :
                Handler(taskPointer, std::move(cb))
            {}
        };
        std::shared_ptr<Handler> handler =
            std::make_shared<MakeSharedHelper>(taskPointer, std::move(cb));
        handler->run(objects);
    }

    /**
     * @brief Destructor that finalizes the status operation
     *
     * Updates task progress, sets appropriate task state based on completion
     * status, and invokes the result callback with the aggregated status data.
     */
    ~Handler()
    {
        if (enumeratedObjects > 0)
        {
            task->percentComplete =
                static_cast<int>(100 * completedObjects / enumeratedObjects);
        }
        if (completedObjects == 0)
        {
            task->messages.emplace_back(
                messages::resourceErrorsDetectedFormatError(
                    "Debug token status acquisition",
                    "No valid debug token status data"));
            task->state = "Stopping";
            task->messages.emplace_back(
                messages::taskAborted(std::to_string(task->index)));
        }
        else if (completedObjects == enumeratedObjects)
        {
            task->state = "Completed";
            task->messages.emplace_back(
                messages::taskCompletedOK(std::to_string(task->index)));
        }
        else
        {
            task->state = "Exception";
            task->messages.emplace_back(
                messages::taskCompletedWarning(std::to_string(task->index)));
        }
        nlohmann::json statusOutput;
        statusOutput["DebugTokenStatus"] = std::move(statusArray);
        result = statusOutput.dump(4);
        if (callback)
        {
            callback(task, result);
        }
    }

  private:
    Handler(const std::shared_ptr<task::TaskData>& taskPointer,
            ResultCallback cb) : task(taskPointer), callback(std::move(cb))
    {}

    const std::shared_ptr<task::TaskData> task;
    size_t enumeratedObjects{0};
    size_t completedObjects{0};
    Result result;
    ResultCallback callback;
    nlohmann::json statusArray;

    /**
     * @brief Start the status operation by discovering D-Bus objects
     *
     * Initiates the discovery process by querying the D-Bus mapper for all
     * objects implementing the debug token status interface.
     */
    void run()
    {
        constexpr std::array<std::string_view, 1> interfaces = {
            debugTokenStatusIntf};
        dbus::utility::getSubTree(
            std::string(debugTokenBasePath), 0, interfaces,
            std::bind_front(&Handler::subTreeHandler, this,
                            shared_from_this()));
    }

    /**
     * @brief Handle the response from D-Bus object discovery
     *
     * Processes the response from the D-Bus mapper, extracts object
     * information, and initiates status queries for each discovered object.
     *
     * @param unused Unused shared_ptr parameter (required for callback
     * signature)
     * @param ec Error code from the D-Bus operation
     * @param resp Response containing discovered objects and their services
     */
    void subTreeHandler(const std::shared_ptr<Handler>& /*unused*/,
                        const boost::system::error_code& ec,
                        const dbus::utility::MapperGetSubTreeResponse& resp)
    {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DebugToken error: "
                             "GetSubTreePaths error: {}",
                             ec.message());
            return;
        }
        if (resp.empty())
        {
            BMCWEB_LOG_ERROR(
                "DebugToken error: "
                "no objects with debug token status interface found");
            return;
        }
        std::vector<std::tuple<std::string, std::string>> objects;
        objects.reserve(resp.size());
        for (const auto& [path, serviceMap] : resp)
        {
            objects.emplace_back(serviceMap[0].first, path);
        }
        run(objects);
    }

    /**
     * @brief Execute status queries for the provided objects
     *
     * Initiates status queries for each object in the provided list using
     * the unified operation framework.
     *
     * @param objects Vector of tuples containing service names and object paths
     */
    void run(const std::vector<std::tuple<std::string, std::string>>& objects)
    {
        enumeratedObjects = objects.size();
        for (const auto& [service, objectPath] : objects)
        {
            unified::action::Handler::startOperation(
                service, objectPath, unified::action::Operation::GetTokenStatus,
                std::monostate(),
                std::bind_front(&Handler::resultHandler, this,
                                shared_from_this(), objectPath));
        }
    }

    /**
     * @brief Handle the result from a single object status query
     *
     * Processes the result from a unified operation, converts it to JSON
     * format, and updates the task progress. Handles both successful status
     * retrieval and error cases.
     *
     * @param unused Unused shared_ptr parameter (required for callback
     * signature)
     * @param objectPath The D-Bus object path that was queried
     * @param opResult The result from the action
     */
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void resultHandler(const std::shared_ptr<Handler>& /*unused*/,
                       const std::string& objectPath,
                       const unified::action::Result& actionResult)
    {
        std::string objectName =
            std::filesystem::path(objectPath).filename().string();
        if (std::holds_alternative<unified::TokenStatus>(actionResult))
        {
            unified::TokenStatus status =
                std::get<unified::TokenStatus>(actionResult);
            nlohmann::json jsonOutput;
            jsonOutput["@odata.id"] =
                std::format("/redfish/v1/Chassis/{}", objectName);
            unified::tokenStatusToJson(status, jsonOutput);
            statusArray.push_back(std::move(jsonOutput));
            task->messages.emplace_back(
                messages::debugTokenStatusSuccess(objectName));
            ++completedObjects;
            task->percentComplete =
                static_cast<int>(100 * completedObjects / enumeratedObjects);
        }
        else
        {
            task->messages.emplace_back(
                messages::resourceErrorsDetectedFormatError(
                    objectName, "failed to get debug token status"));
        }
    }
};

} // namespace redfish::debug_token::status
