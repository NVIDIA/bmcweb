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

#include "component_integrity.hpp"
#include "dbus_utility.hpp"
#include "debug_token/request.hpp"
#include "debug_token/request_utils.hpp"
#include "logging.hpp"
#include "task.hpp"

#include <boost/system/error_code.hpp>

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace redfish::debug_token::dot_request
{

using Result = std::string;
using ResultCallback =
    std::function<void(const std::shared_ptr<task::TaskData>&, Result)>;

/**
 * @brief Handler class for managing aggregated SPDM token requests
 *
 * This class handles asynchronous SPDM token requests for debug token
 * operations across multiple SPDM responders. It manages the complete lifecycle
 * of SPDM operations including DBus communication, timeout handling, error
 * processing, and result aggregation.
 *
 * The class aggregates results from multiple SPDM responders and provides
 * a unified callback interface. It uses std::enable_shared_from_this to
 * ensure proper lifetime management during asynchronous operations.
 *
 * The class provides two operation initiation methods:
 * - Chassis-based discovery: Automatically discovers all SPDM responders
 *   and aggregates their results
 * - Direct service/object path: Uses provided service and object path
 *   directly for single responder operations
 *
 * @note This class is non-copyable and non-movable due to shared state
 *       management
 * @note All operations are asynchronous and use callbacks for completion
 *       notification
 * @note Results are aggregated into a vector of single operation results
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
     * @brief Start an aggregated SPDM operation for all discovered responders
     *
     * This function initiates SPDM token request operations for all SPDM
     * responders discovered in the system. It creates a shared pointer to a new
     * Handler object and initializes it with the provided token type and
     * callback function. The handler automatically discovers all available
     * SPDM responder services and aggregates their results.
     *
     * @param tokenRequestType The token type for the SPDM operation
     * @param taskPointer The task data for the operation
     * @param cb The callback function to be called when all operations
     *           complete (successfully or with error)
     *
     * @note The operation is asynchronous and the callback will be invoked
     *       when all operations complete or timeout
     * @note If no SPDM responders are found, the callback will be invoked
     *       with an empty result vector
     */
    static void startOperation(
        const TokenType& tokenRequestType,
        const std::shared_ptr<task::TaskData>& taskPointer, ResultCallback cb)
    {
        struct MakeSharedHelper : public Handler
        {
            MakeSharedHelper(const TokenType& tokenRequestType,
                             const std::shared_ptr<task::TaskData>& taskPointer,
                             ResultCallback cb) :
                Handler(tokenRequestType, taskPointer, std::move(cb))
            {}
        };
        std::shared_ptr<Handler> handler = std::make_shared<MakeSharedHelper>(
            tokenRequestType, taskPointer, std::move(cb));
        handler->run();
    }

    /**
     * @brief Destructor for the SPDM Handler
     *
     * Ensures that any pending callback is invoked with the current result
     * before the handler is destroyed. This guarantees that callers are
     * always notified of the operation outcome, even if the handler is
     * destroyed before explicit completion.
     */
    ~Handler()
    {
        if (callback)
        {
            callback(task, result);
        }
    }

  private:
    /**
     * @brief Private constructor for the SPDM Handler
     *
     * Constructs a new Handler instance with the specified token type and
     * callback function. This constructor is private to enforce the use of
     * the static startOperation methods for creating handler instances.
     *
     * @param tokenRequestType The token type for the SPDM operation
     * @param taskPointer The task data for the operation
     * @param cb The callback function to be called when all operations
     *           complete (successfully or with error)
     */
    Handler(const TokenType& tokenRequestType,
            const std::shared_ptr<task::TaskData>& taskPointer,
            ResultCallback cb) :
        requestType(tokenRequestType), task(taskPointer),
        callback(std::move(cb))
    {}

    TokenType requestType;
    const std::shared_ptr<task::TaskData> task;
    Result result;
    ResultCallback callback;

    /**
     * @brief Acquire token requests for all SPDM responders
     *
     * This function initiates the SPDM operation by discovering all available
     * SPDM responder interfaces in the system. It performs a subtree query
     * to find all SPDM responder services and object paths.
     *
     * @note This is an internal method called by the public startOperation
     *       methods
     * @note If no SPDM responders are found, the operation will be terminated
     *       with an error
     */
    void run()
    {
        constexpr std::array<std::string_view, 1> cpuInterface = {
            "xyz.openbmc_project.Inventory.Item.Cpu"};
        dbus::utility::getSubTree("/", 0, cpuInterface,
                                  std::bind_front(&Handler::cpuSubTreeHandler,
                                                  this, shared_from_this()));
    }

    /**
     * @brief Handle the sub-tree response for CPU discovery
     *
     * This function processes the response from the DBus subtree query used
     * to discover CPU objects in the system. It extracts CPU object names
     * from the response and uses them to filter SPDM responder interfaces
     * in the subsequent discovery phase.
     *
     * @param self The shared pointer to this handler instance (unused)
     * @param ec Error code from the DBus subtree query
     * @param resp The subtree response containing available CPU objects
     *
     * @note If an error occurs during CPU discovery, the operation is
     *       terminated with an appropriate error message
     * @note CPU object names are extracted from the filesystem path and
     *       used to filter SPDM responders in the next discovery phase
     */
    void cpuSubTreeHandler(const std::shared_ptr<Handler>& /*unused*/,
                           const boost::system::error_code& ec,
                           const dbus::utility::MapperGetSubTreeResponse& resp)
    {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DebugToken SPDM error: "
                             "GetSubTreePaths error: {}",
                             ec.message());
            return;
        }
        std::vector<std::string> cpuObjects;
        cpuObjects.reserve(resp.size());
        for (const auto& [path, serviceMap] : resp)
        {
            cpuObjects.emplace_back(
                std::filesystem::path(path).filename().string());
        }
        constexpr std::array<std::string_view, 1> interfaces = {
            spdmResponderIntf};
        dbus::utility::getSubTree(
            std::string(rootSPDMDbusPath), 0, interfaces,
            std::bind_front(&Handler::spdmSubTreeHandler, this,
                            shared_from_this(), cpuObjects));
    }

    /**
     * @brief Handle the sub-tree response for SPDM responder discovery
     *
     * This function processes the response from the DBus subtree query used
     * to discover SPDM responder interfaces. It filters the discovered
     * responders based on CPU objects found in the previous discovery phase
     * and initiates operations only for matching responders.
     *
     * @param self The shared pointer to this handler instance (unused)
     * @param cpuObjects Vector of CPU object names to filter responders
     * @param ec Error code from the DBus subtree query
     * @param resp The subtree response containing available services and
     *             object paths
     *
     * @note If no SPDM responders are found or an error occurs, the operation
     *       is terminated with an appropriate error message
     * @note Only SPDM responders matching the discovered CPU objects will
     *       have operations initiated
     */
    void spdmSubTreeHandler(const std::shared_ptr<Handler>& /*unused*/,
                            const std::vector<std::string>& cpuObjects,
                            const boost::system::error_code& ec,
                            const dbus::utility::MapperGetSubTreeResponse& resp)
    {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DebugToken SPDM error: "
                             "GetSubTreePaths error: {}",
                             ec.message());
            return;
        }
        if (resp.empty())
        {
            BMCWEB_LOG_ERROR("DebugToken SPDM error: "
                             "no objects with SPDM responder interface found");
            return;
        }
        std::vector<std::tuple<std::string, std::string>> objects;
        objects.reserve(cpuObjects.size());
        for (const auto& [path, serviceMap] : resp)
        {
            for (const auto& cpuObject : cpuObjects)
            {
                if (path.find(cpuObject) != std::string::npos)
                {
                    objects.emplace_back(serviceMap[0].first, path);
                    break;
                }
            }
        }
        run(objects);
    }

    /**
     * @brief Run SPDM operations for specified responders
     *
     * This function initiates SPDM token requests for specified SPDM responder
     * services. It creates individual operations for each responder and sets up
     * result aggregation.
     *
     * @param objects Vector of service and object path pairs for SPDM
     * responders
     *
     * @note This method sets up asynchronous operations for each specified
     *       responder
     * @note Results are aggregated and passed to the callback when all
     *       operations complete
     */
    void run(const std::vector<std::tuple<std::string, std::string>>& objects)
    {
        debug_token::request::Handler::startOperation(
            objects, requestType, task,
            std::bind_front(&Handler::resultHandler, this, shared_from_this()));
    }

    /**
     * @brief Handle the aggregated result of SPDM operations
     *
     * This function stores the aggregated results from all SPDM operations
     * as the final result. The result will be passed to the callback when
     * the handler is destroyed or when the operation completes.
     *
     * @param self The shared self pointer to the parent object (unused)
     * @param task The task data for the operation (unused)
     * @param requestResult The aggregated results from all SPDM operations
     */
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void resultHandler(const std::shared_ptr<Handler>& /*unused*/,
                       const std::shared_ptr<task::TaskData>& /*unused*/,
                       const std::string& requestResult)
    {
        result = requestResult;
    }
};

} // namespace redfish::debug_token::dot_request
