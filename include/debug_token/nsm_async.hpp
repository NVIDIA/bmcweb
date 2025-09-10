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
#include "debug_token/endpoint.hpp"
#include "debug_token/nsm_status_utils.hpp"
#include "debug_token/request_utils.hpp"
#include "utils/dbus_utils.hpp"

#include <boost/asio/post.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace redfish
{
namespace debug_token
{

using NsmError = std::tuple<uint16_t, std::string>;

namespace nsm_async::single_op
{

constexpr const std::string_view asyncStatusIntf = "com.nvidia.Async.Status";
constexpr const std::string_view asyncStatusProperty = "Status";
constexpr const std::string_view asyncValueIntf = "com.nvidia.Async.Value";
constexpr const std::string_view asyncValueProperty = "Value";
constexpr const std::string_view asyncOperationBasePath =
    "/com/nvidia/nsmd/AsyncOperation";

enum class Operation
{
    DisableTokens,
    GenerateTokenRequest,
    GetTokenStatus,
    InstallToken,
    Invalid
};

using Argument =
    std::variant<std::monostate, std::string, std::vector<uint8_t>>;
using Output = std::variant<std::monostate, NsmError, NsmTokenStatus,
                            std::vector<uint8_t>>;
using Result = std::tuple<EndpointState, Output>;
using ResultCallback = std::function<void(Result)>;

using RequestType = std::variant<sdbusplus::message::unix_fd>;
using StatusType = std::variant<NsmDbusTokenStatus>;
using ErrorType = std::variant<NsmError>;

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
     * @brief Start an NSM operation for a specific chassis
     *
     * This function starts an NSM operation for a specific chassis. It creates
     * a shared pointer to a new Handler object and initializes it with the
     * provided operation, argument, and callback function.
     * The logic in the handler class takes care of finding the DBus object
     * and service associated with the chassis.
     * @param chassisId The chassis ID for which the operation is to be
     * performed
     * @param op The operation to be performed
     * @param arg The argument for the operation
     * @param callback The callback function to be called when the operation is
     * complete
     */
    static void startOperation(const std::string& chassisId, Operation op,
                               const Argument& arg, ResultCallback callback)
    {
        struct MakeSharedHelper : public Handler
        {
            MakeSharedHelper(Operation op, Argument arg, ResultCallback cb) :
                Handler(op, std::move(arg), std::move(cb))
            {}
        };
        std::shared_ptr<Handler> t =
            std::make_shared<MakeSharedHelper>(op, arg, std::move(callback));
        t->run(chassisId);
    }

    /**
     * @brief Start an NSM operation for a specific DBus object and service
     *
     * This function starts an NSM operation for a specific DBus object and
     * service. It creates a shared pointer to a new Handler object and
     * initializes it with the provided operation, argument, and callback
     * function.
     * @param service The DBus service for the operation
     * @param objectPath The DBus object path for the operation
     * @param op The operation to be performed
     * @param arg The argument for the operation
     * @param callback The callback function to be called when the operation is
     * complete
     */
    static void startOperation(const std::string& service,
                               const std::string& objectPath, Operation op,
                               const Argument& arg, ResultCallback callback)
    {
        struct MakeSharedHelper : public Handler
        {
            MakeSharedHelper(Operation op, Argument arg, ResultCallback cb) :
                Handler(op, std::move(arg), std::move(cb))
            {}
        };
        std::shared_ptr<Handler> t =
            std::make_shared<MakeSharedHelper>(op, arg, std::move(callback));
        t->run(service, objectPath);
    }

    ~Handler()
    {
        if (callback)
        {
            callback(result);
        }
    }

  private:
    Handler(Operation op, Argument arg, ResultCallback cb) :
        operation(op), argument(std::move(arg)), callback(std::move(cb))
    {}

    Operation operation;
    Argument argument;
    Result result;
    ResultCallback callback;

    std::string asyncObjectPath, asyncObjectService;
    std::unique_ptr<sdbusplus::bus::match_t> match;

    /**
     * @brief Run the operation for a specific chassis
     *
     * This function triggers the operation for a specific chassis. It gets the
     * DBus object and service associated with the chassis.
     * @param chassisId The chassis ID for which the operation is to be
     * performed
     */
    void run(const std::string& chassisId)
    {
        constexpr std::array<std::string_view, 1> interfaces = {debugTokenIntf};
        dbus::utility::getSubTree(
            std::string(debugTokenBasePath), 0, interfaces,
            std::bind_front(&Handler::subTreeHandler, this, shared_from_this(),
                            chassisId));
    }

    /**
     * @brief Handle the sub-tree response for a specific chassis
     *
     * This function handles the sub-tree response for a specific chassis. It
     * gets the DBus object and service associated with the chassis.
     * @param chassisId The chassis ID for which the operation is to be
     * performed
     */
    void subTreeHandler(const std::shared_ptr<Handler>& /*unused*/,
                        const std::string& chassisId,
                        const boost::system::error_code& ec,
                        const dbus::utility::MapperGetSubTreeResponse& resp)
    {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("GetSubTreePaths error: {}", ec.message());
            tokenUnsupportedHandler();
            return;
        }
        if (resp.empty())
        {
            BMCWEB_LOG_ERROR("No objects with DebugToken interface found");
            tokenUnsupportedHandler();
            return;
        }
        std::string service;
        std::string objectPath;
        for (const auto& [path, serviceMap] : resp)
        {
            auto pathChassisId =
                std::filesystem::path(path).filename().string();
            if (chassisId == pathChassisId)
            {
                service = serviceMap[0].first;
                objectPath = path;
                break;
            }
        }
        if (objectPath.empty())
        {
            BMCWEB_LOG_ERROR("DebugToken interface not implemented for {}",
                             chassisId);
            tokenUnsupportedHandler();
            return;
        }
        run(service, objectPath);
    }

    /**
     * @brief Run the operation for a specific DBus object and service
     *
     * This function triggers the operation for a specific DBus object and
     * service.
     * @param service The DBus service for the operation
     * @param objectPath The DBus object path for the operation
     */
    void run(const std::string& service, const std::string& objectPath)
    {
        asyncObjectService = service;
        createMatch();

        std::function<void(const boost::system::error_code&,
                           const sdbusplus::message::object_path&)>
            handler = std::bind_front(&Handler::methodHandler, this,
                                      shared_from_this());
        switch (operation)
        {
            case Operation::DisableTokens:
            {
                crow::connections::systemBus->async_method_call(
                    handler, service, objectPath, std::string(debugTokenIntf),
                    "DisableTokens");
                break;
            }

            case Operation::GenerateTokenRequest:
            {
                std::string* tokenOpcode = std::get_if<std::string>(&argument);
                if (tokenOpcode == nullptr)
                {
                    BMCWEB_LOG_ERROR("Invalid argument");
                    generalErrorHandler();
                    return;
                }
                std::string arg =
                    std::string(debugTokenOpcodesEnumPrefix) + *tokenOpcode;
                crow::connections::systemBus->async_method_call(
                    handler, service, objectPath, std::string(debugTokenIntf),
                    "GetRequest", arg);
                break;
            }

            case Operation::GetTokenStatus:
            {
                std::string* tokenType = std::get_if<std::string>(&argument);
                if (tokenType == nullptr)
                {
                    BMCWEB_LOG_ERROR("Invalid argument");
                    generalErrorHandler();
                    return;
                }
                std::string arg =
                    std::string(debugTokenTypesEnumPrefix) + *tokenType;
                crow::connections::systemBus->async_method_call(
                    handler, service, objectPath, std::string(debugTokenIntf),
                    "GetStatus", arg);
                break;
            }

            case Operation::InstallToken:
            {
                std::vector<uint8_t>* token =
                    std::get_if<std::vector<uint8_t>>(&argument);
                if (token == nullptr)
                {
                    BMCWEB_LOG_ERROR("Invalid argument");
                    generalErrorHandler();
                    return;
                }
                crow::connections::systemBus->async_method_call(
                    handler, service, objectPath, std::string(debugTokenIntf),
                    "InstallToken", *token);
                break;
            }

            default:
            {
                BMCWEB_LOG_ERROR("Invalid token operation");
                generalErrorHandler();
                break;
            }
        }
    }

    /**
     * @brief Create a match for the NSM operation
     */
    void createMatch()
    {
        std::string matchRule =
            sdbusplus::bus::match::rules::propertiesChangedNamespace(
                asyncOperationBasePath, asyncStatusIntf);
        match = std::make_unique<sdbusplus::bus::match_t>(
            *crow::connections::systemBus, matchRule.c_str(),
            std::bind_front(&Handler::matchHandler, this, shared_from_this()));
    }

    /**
     * @brief Destroy the match for the NSM operation
     *
     * @param self The shared self pointer to the parent object (unused)
     */
    void destroyMatch(const std::shared_ptr<Handler>& /*unused*/)
    {
        match.reset(nullptr);
    }

    /**
     * @brief Match callback function
     *
     * @param self The shared self pointer to the parent object
     * @param msg DBus message from the match
     */
    void matchHandler(const std::shared_ptr<Handler>& self,
                      sdbusplus::message::message& msg)
    {
        if (msg.get_path() != asyncObjectPath)
        {
            return;
        }
        std::string interface;
        dbus::utility::DBusPropertiesMap propertiesMap;
        msg.read(interface, propertiesMap);
        const std::string* dbusStatus = nullptr;
        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), propertiesMap, "Status",
            dbusStatus);
        if (!success)
        {
            BMCWEB_LOG_ERROR("Failed to unpack Status");
            return;
        }
        if (!asyncStatusHandler(self, *dbusStatus))
        {
            return;
        }
        boost::asio::post(crow::connections::systemBus->get_io_context(),
                          std::bind_front(&Handler::destroyMatch, this, self));
    }

    /**
     * @brief Debug token interface method call callback function
     *
     * @param self The shared self pointer to the parent object
     * @param ec The error code from the DBus call
     * @param objectPath The async operation object path from the DBus call
     */
    void methodHandler(const std::shared_ptr<Handler>& self,
                       const boost::system::error_code& ec,
                       const sdbusplus::message::object_path& objectPath)
    {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBus error: {}", ec.message());
            generalErrorHandler();
            return;
        }
        asyncObjectPath = objectPath;
        sdbusplus::asio::getProperty<std::string>(
            *crow::connections::systemBus, asyncObjectService, asyncObjectPath,
            std::string(asyncStatusIntf), "Status",
            std::bind_front(&Handler::getStatusHandler, this, self));
    }

    /**
     * @brief Get async status callback function
     *
     * @param self The shared self pointer to the parent object
     * @param ec The error code from the DBus call
     * @param dbusStatus The async status value
     */
    void getStatusHandler(const std::shared_ptr<Handler>& self,
                          const boost::system::error_code& ec,
                          const std::string& dbusStatus)
    {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBus Get error: {}", ec.message());
            generalErrorHandler();
            return;
        }
        if (!asyncStatusHandler(self, dbusStatus))
        {
            return;
        }
        destroyMatch(self);
    }

    /**
     * @brief Async status handler function
     *
     * This function parses the async status value and calls appropriate handler
     * functions based on the status.
     * @param self The shared self pointer to the parent object
     * @param dbusStatus The async status value
     * @return True if the status indicates that the operation is finished,
     * false otherwise
     */
    bool asyncStatusHandler(const std::shared_ptr<Handler>& self,
                            const std::string& dbusStatus)
    {
        std::string status =
            dbusStatus.substr(dbusStatus.find_last_of('.') + 1);
        if (status.empty() || status == "InProgress")
        {
            return false;
        }
        if (status == "UnsupportedRequest")
        {
            tokenUnsupportedHandler();
        }
        else if (status == "Success")
        {
            switch (operation)
            {
                case Operation::GenerateTokenRequest:
                    resultHandler<RequestType>(
                        std::bind_front(&Handler::requestHandler, this, self));
                    break;

                case Operation::GetTokenStatus:
                    resultHandler<StatusType>(
                        std::bind_front(&Handler::statusHandler, this, self));
                    break;

                default:
                    successHandler();
                    break;
            }
        }
        else
        {
            resultHandler<ErrorType>(
                std::bind_front(&Handler::errorHandler, this, self));
        }
        return true;
    }

    /**
     * @brief Binding function for getting the async value
     *
     * @param cb The callback function to be called when the async value
     * is obtained
     */
    template <typename T, typename U>
    void resultHandler(U&& cb)
    {
        sdbusplus::asio::getProperty<T>(
            *crow::connections::systemBus, asyncObjectService, asyncObjectPath,
            std::string(asyncValueIntf), "Value", std::forward<U>(cb));
    }

    /**
     * @brief Token request handler function
     *
     * @param self The shared self pointer to the parent object (unused)
     * @param ec The error code from the DBus call
     * @param dbusFd The file descriptor containing the token request
     */
    void requestHandler(const std::shared_ptr<Handler>& /*unused*/,
                        const boost::system::error_code ec,
                        const RequestType& dbusFd)
    {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBus Get error: {}", ec.message());
            generalErrorHandler();
            return;
        }
        const auto* unixFd = std::get_if<sdbusplus::message::unix_fd>(&dbusFd);
        if (unixFd == nullptr)
        {
            BMCWEB_LOG_ERROR("Failed to read NSM token request fd");
            generalErrorHandler();
            return;
        }
        std::vector<uint8_t> request;
        if (!readNsmTokenRequestFd(unixFd->fd, request))
        {
            BMCWEB_LOG_ERROR("Invalid NSM token request");
            generalErrorHandler();
            return;
        }
        if (request.size() < sizeof(NsmDebugTokenRequest))
        {
            BMCWEB_LOG_ERROR("Invalid NSM token request size: {}", request.size());
            result = std::make_tuple(EndpointState::Error, std::monostate());
            return;
        }
        NsmDebugTokenRequest nsmReq{};
        std::memcpy(&nsmReq, request.data(), sizeof(NsmDebugTokenRequest));
        switch (nsmReq.status)
        {
            case NsmDebugTokenChallengeQueryStatus::OK:
                result =
                    std::make_tuple(EndpointState::RequestAcquired, request);
                break;
            case NsmDebugTokenChallengeQueryStatus::TokenAlreadyApplied:
                result =
                    std::make_tuple(EndpointState::TokenInstalled, request);
                break;
            case NsmDebugTokenChallengeQueryStatus::TokenNotSupported:
                result = std::make_tuple(EndpointState::DebugTokenUnsupported,
                                         std::monostate());
                break;
            default:
                BMCWEB_LOG_ERROR("NSM token request - status: {}",
                                 nsmReq.status);
                result =
                    std::make_tuple(EndpointState::Error, std::monostate());
                break;
        }
    }

    /**
     * @brief Token status handler function
     *
     * @param self The shared self pointer to the parent object (unused)
     * @param ec The error code from the DBus call
     * @param dbusStatus The token status value
     */
    void statusHandler(const std::shared_ptr<Handler>& /*unused*/,
                       const boost::system::error_code& ec,
                       const StatusType& dbusStatus)
    {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBus Get error: {}", ec.message());
            generalErrorHandler();
            return;
        }
        const auto* status = std::get_if<NsmDbusTokenStatus>(&dbusStatus);
        if (status == nullptr)
        {
            BMCWEB_LOG_ERROR("Failed to read NSM token status");
            generalErrorHandler();
            return;
        }
        try
        {
            NsmTokenStatus nsmStatus(*status);
            result = std::make_tuple(EndpointState::StatusAcquired, nsmStatus);
        }
        catch (const std::exception&)
        {
            BMCWEB_LOG_ERROR("Invalid NSM token status");
            generalErrorHandler();
        }
    }

    /**
     * @brief Token error handler function
     *
     * @param self The shared self pointer to the parent object (unused)
     * @param ec The error code from the DBus call
     * @param dbusError The error value
     */
    void errorHandler(const std::shared_ptr<Handler>& /*unused*/,
                      const boost::system::error_code ec,
                      const ErrorType& dbusError)
    {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBus Get error: {}", ec.message());
            generalErrorHandler();
            return;
        }
        const auto* error = std::get_if<NsmError>(&dbusError);
        if (error == nullptr)
        {
            BMCWEB_LOG_ERROR("Failed to read NSM error");
            generalErrorHandler();
            return;
        }
        const auto& [code, message] = *error;
        if (code == debugTokenUnsupportedNsmErrorCode)
        {
            tokenUnsupportedHandler();
            return;
        }
        BMCWEB_LOG_ERROR("NSM error: {} - {}", code, message);
        result = std::make_tuple(EndpointState::Error, *error);
    }

    /**
     * @brief General error handler function
     *
     * This function sets the result to an error state.
     */
    void generalErrorHandler()
    {
        result = std::make_tuple(EndpointState::Error, std::monostate());
        boost::asio::post(
            crow::connections::systemBus->get_io_context(),
            std::bind_front(&Handler::destroyMatch, this, shared_from_this()));
    }

    /**
     * @brief Success handler function
     *
     * Used for operations which do not return any data.
     */
    void successHandler()
    {
        result = std::make_tuple(EndpointState::None, std::monostate());
    }

    /**
     * @brief Token unsupported handler function
     *
     * This function sets the result to a token unsupported state.
     */
    void tokenUnsupportedHandler()
    {
        result = std::make_tuple(EndpointState::DebugTokenUnsupported,
                                 std::monostate());
    }
};

} // namespace nsm_async::single_op

} // namespace debug_token
} // namespace redfish
