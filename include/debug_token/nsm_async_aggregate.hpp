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

#include "nsm_async.hpp"

#include <utility>

namespace redfish::debug_token
{

namespace nsm_async::aggregate
{

enum class Operation
{
    GenerateTokenRequest,
    GetTokenStatus
};

using Argument = std::string;
using Output = std::variant<std::monostate, NsmError, NsmTokenStatus,
                            std::vector<uint8_t>>;
using ObjectPath = std::string;
using Result = std::tuple<ObjectPath, EndpointState, Output>;
using ResultCallback = std::function<void(const std::vector<Result>&)>;

class Handler : public std::enable_shared_from_this<Handler>
{
  public:
    // Delete copy constructor and assignment operator
    Handler(const Handler&) = delete;
    Handler& operator=(const Handler&) = delete;

    // Delete move constructor and assignment operator
    Handler(Handler&&) = delete;
    Handler& operator=(Handler&&) = delete;

    /**
     * @brief Start an aggregate NSM operation for all endpoints
     *
     * This function starts an NSM operation for all endpoints. It creates a
     * shared pointer to a new Handler object and initializes it with the
     * provided operation, argument, and callback function.
     * The logic in the handler class takes care of enumerating all endpoints
     * and starting the appropriate operation for each of them.
     * @param op The operation to be performed
     * @param arg The argument for the operation
     * @param callback The callback function to be called when the operation is
     * complete
     */
    static void startOperation(Operation op, const Argument& arg,
                               const ResultCallback& callback)
    {
        struct MakeSharedHelper : public Handler
        {
            MakeSharedHelper(Operation opParam, Argument argParam,
                             ResultCallback callbackParam) :
                Handler(opParam, std::move(argParam), std::move(callbackParam))
            {}
        };
        std::shared_ptr<Handler> t =
            std::make_shared<MakeSharedHelper>(op, arg, callback);
        t->run();
    }

  private:
    Handler(Operation opParam, Argument argParam,
            ResultCallback callbackParam) :
        op(opParam), arg(std::move(argParam)),
        callback(std::move(callbackParam))
    {}

    ~Handler()
    {
        if (callback)
        {
            callback(results);
        }
    }

    Operation op;
    Argument arg;
    std::vector<Result> results;
    ResultCallback callback;

    /**
     * @brief Run the aggregate NSM operation
     *
     * This function enumerates all endpoints with the DebugToken interface.
     * Further processing is handled by the subtree callback function.
     */
    void run()
    {
        constexpr std::array<std::string_view, 1> interfaces = {debugTokenIntf};
        dbus::utility::getSubTree(
            std::string(debugTokenBasePath), 0, interfaces,
            std::bind_front(&Handler::subTreeHandler, this,
                            shared_from_this()));
    }

    /**
     * @brief Handle the subtree response
     *
     * This function handles the subtree response from the DBus utility
     * and passes the enumerated endpoints to the execution function.
     * @param self The shared self pointer to the parent object (unused)
     * @param ec The error code from the DBus call
     * @param resp The subtree response from the DBus utility
     */
    void subTreeHandler(const std::shared_ptr<Handler>& /*unused*/,
                        const boost::system::error_code& ec,
                        const dbus::utility::MapperGetSubTreeResponse& resp)
    {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("GetSubTreePaths error: {}", ec.message());
            callback({});
            return;
        }
        if (resp.empty())
        {
            BMCWEB_LOG_ERROR("No objects with DebugToken interface found");
            callback({});
            return;
        }
        std::vector<std::pair<std::string, std::string>> objects;
        objects.reserve(resp.size());
        for (const auto& [path, serviceMap] : resp)
        {
            objects.emplace_back(serviceMap[0].first, path);
        }
        run(objects);
    }

    /**
     * @brief Run the aggregate NSM operation
     *
     * This function runs the aggregate NSM operation for all endpoints. It
     * creates a shared pointer to a new Handler object and initializes it with
     * the provided operation, argument, and callback function.
     * @param objects The vector of service and object paths of the endpoints
     */
    void run(const std::vector<std::pair<std::string, std::string>>&
                 objects) /*unused*/
    {
        single_op::Operation opType = single_op::Operation::Invalid;
        if (op == Operation::GenerateTokenRequest)
        {
            opType = single_op::Operation::GenerateTokenRequest;
        }
        else if (op == Operation::GetTokenStatus)
        {
            opType = single_op::Operation::GetTokenStatus;
        }
        else
        {
            BMCWEB_LOG_ERROR("Invalid operation");
            return;
        }
        results.reserve(objects.size());
        for (const auto& [service, objectPath] : objects)
        {
            results.emplace_back(objectPath, EndpointState::None,
                                 std::monostate());
            single_op::Handler::startOperation(
                service, objectPath, opType, arg,
                std::bind_front(&Handler::resultHandler, this,
                                shared_from_this(), std::ref(results.back())));
        }
    }

    /**
     * @brief Handle the result of the NSM operation
     *
     * This function handles the result of the NSM operation and updates the
     * aggregate result.
     * @param self The shared self pointer to the parent object (unused)
     * @param aggregateResult The aggregate result entry to be updated
     * @param result The result of the NSM operation
     */
    // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    void resultHandler(const std::shared_ptr<Handler>& /*unused*/,
                       Result& aggregateResult, const single_op::Result& result)
    {
        const auto& [state, output] = result;
        std::get<1>(aggregateResult) = state;
        std::get<2>(aggregateResult) = output;
    }
};

} // namespace nsm_async::aggregate

} // namespace redfish::debug_token
