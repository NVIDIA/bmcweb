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
#include "debug_token/nic/request_utils.hpp"
#include "debug_token/nic/status_utils.hpp"
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

namespace redfish::debug_token
{

namespace nic::action
{

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
using Output =
    std::variant<std::monostate, NsmResult, TokenStatus, std::vector<uint8_t>>;
using Result = std::tuple<EndpointState, Output>;
using ResultCallback = std::function<void(Result)>;

using RequestType = std::variant<sdbusplus::message::unix_fd>;
using StatusType = std::variant<DbusTokenStatus>;
using ErrorType = std::variant<NsmResult>;

/**
 * @brief Handler class for managing NSM async operations
 *
 * This class handles asynchronous operations for debug tokens including
 * disabling tokens, generating token requests, getting token status, and
 * installing tokens. It manages the complete lifecycle of async operations
 * including DBus method calls, property monitoring, and result handling.
 * The class uses shared_from_this to ensure proper lifetime management
 * during async operations.
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
        std::shared_ptr<Handler> handler =
            std::make_shared<MakeSharedHelper>(op, arg, std::move(callback));
        handler->run(chassisId);
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
        std::shared_ptr<Handler> handler =
            std::make_shared<MakeSharedHelper>(op, arg, std::move(callback));
        handler->run(service, objectPath);
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
                dbus::utility::async_method_call(handler, service, objectPath,
                                                 std::string(debugTokenIntf),
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
                dbus::utility::async_method_call(handler, service, objectPath,
                                                 std::string(debugTokenIntf),
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
                dbus::utility::async_method_call(handler, service, objectPath,
                                                 std::string(debugTokenIntf),
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
                dbus::utility::async_method_call(handler, service, objectPath,
                                                 std::string(debugTokenIntf),
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
     * @brief Create a DBus property match for monitoring async operation status
     *
     * This function creates a DBus match rule to monitor property changes
     * on the async operation object. The match will trigger when the
     * Status property changes, allowing the handler to respond to
     * operation completion or status updates.
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
     * @brief Destroy the DBus property match for the NSM operation
     *
     * This function cleans up the DBus match that was created to monitor
     * the async operation status. It should be called when the operation
     * is complete or when an error occurs.
     *
     * @param self The shared self pointer to the parent object (unused)
     */
    void destroyMatch(const std::shared_ptr<Handler>& /*unused*/)
    {
        match.reset(nullptr);
    }

    /**
     * @brief DBus property change match callback function
     *
     * This function is called when a property change is detected on the
     * async operation object. It processes the Status property change
     * and determines if the operation has completed or needs further
     * monitoring.
     *
     * @param self The shared self pointer to the parent object
     * @param msg DBus message containing the property change notification
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
     * This function is called when the initial DBus method call completes.
     * It handles any errors from the method call and initiates monitoring
     * of the async operation by getting the current status.
     *
     * @param self The shared self pointer to the parent object
     * @param ec The error code from the DBus call
     * @param objectPath The async operation object path returned from the DBus
     * call
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
        dbus::utility::getProperty<std::string>(
            asyncObjectService, asyncObjectPath, std::string(asyncStatusIntf),
            "Status", std::bind_front(&Handler::getStatusHandler, this, self));
    }

    /**
     * @brief Get async status property callback function
     *
     * This function is called when the initial status property read completes.
     * It processes the current status and either continues monitoring or
     * completes the operation based on the status value.
     *
     * @param self The shared self pointer to the parent object
     * @param ec The error code from the DBus call
     * @param dbusStatus The current async status value from the property
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
     * @brief Template function for getting the async operation result value
     *
     * This function retrieves the Value property from the async operation
     * object. It is used for operations that return data (like token requests
     * or status information) after the operation completes successfully.
     *
     * @tparam T The expected type of the result value
     * @tparam U The callback function type
     * @param cb The callback function to be called when the async value is
     * obtained
     */
    template <typename T, typename U>
    void resultHandler(U&& cb)
    {
        dbus::utility::getProperty<T>(asyncObjectService, asyncObjectPath,
                                      std::string(asyncValueIntf), "Value",
                                      std::forward<U>(cb));
    }

    /**
     * @brief Token request result handler function
     *
     * This function processes the result of a GenerateTokenRequest operation.
     * It reads the file descriptor containing the token request data,
     * parses the NSM debug token request structure, and sets the appropriate
     * result based on the request status.
     *
     * @param self The shared self pointer to the parent object (unused)
     * @param ec The error code from the DBus call
     * @param dbusFd The file descriptor containing the token request data
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
        if (!readTokenRequestFd(unixFd->fd, request))
        {
            BMCWEB_LOG_ERROR("Invalid NSM token request");
            generalErrorHandler();
            return;
        }
        if (request.size() < sizeof(DebugTokenRequest))
        {
            BMCWEB_LOG_ERROR("Invalid NSM token request size: {}",
                             request.size());
            result = std::make_tuple(EndpointState::Error, std::monostate());
            return;
        }
        DebugTokenRequest nsmReq{};
        std::memcpy(&nsmReq, request.data(), sizeof(DebugTokenRequest));
        switch (nsmReq.status)
        {
            case DebugTokenChallengeQueryStatus::OK:
                result =
                    std::make_tuple(EndpointState::RequestAcquired, request);
                break;
            case DebugTokenChallengeQueryStatus::TokenAlreadyApplied:
                result =
                    std::make_tuple(EndpointState::TokenInstalled, request);
                break;
            case DebugTokenChallengeQueryStatus::TokenNotSupported:
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
     * @brief Token status result handler function
     *
     * This function processes the result of a GetTokenStatus operation.
     * It converts the DBus token status to the internal TokenStatus
     * format and sets the result accordingly.
     *
     * @param self The shared self pointer to the parent object (unused)
     * @param ec The error code from the DBus call
     * @param dbusStatus The token status value from the async operation
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
        const auto* status = std::get_if<DbusTokenStatus>(&dbusStatus);
        if (status == nullptr)
        {
            BMCWEB_LOG_ERROR("Failed to read NSM token status");
            generalErrorHandler();
            return;
        }
        try
        {
            TokenStatus nsmStatus(*status);
            result = std::make_tuple(EndpointState::StatusAcquired, nsmStatus);
        }
        catch (const std::exception&)
        {
            BMCWEB_LOG_ERROR("Invalid NSM token status");
            generalErrorHandler();
        }
    }

    /**
     * @brief Token error result handler function
     *
     * This function processes error results from async operations.
     * It handles NSM-specific error codes and converts them to appropriate
     * endpoint states, with special handling for unsupported token errors.
     *
     * @param self The shared self pointer to the parent object (unused)
     * @param ec The error code from the DBus call
     * @param dbusError The error value from the async operation
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
        const auto* error = std::get_if<NsmResult>(&dbusError);
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
     * This function handles general errors that occur during async operations.
     * It sets the result to an error state and cleans up the DBus match.
     * This is used for errors that don't have specific NSM error codes.
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
     * This function handles successful completion of operations that do not
     * return any data (like DisableTokens and InstallToken operations).
     * It sets the result to indicate successful completion with no data.
     */
    void successHandler()
    {
        result = std::make_tuple(EndpointState::None, std::monostate());
    }

    /**
     * @brief Token unsupported handler function
     *
     * This function handles cases where the debug token functionality is
     * not supported on the target system. It sets the result to indicate
     * that debug tokens are unsupported for the current endpoint.
     */
    void tokenUnsupportedHandler()
    {
        result = std::make_tuple(EndpointState::DebugTokenUnsupported,
                                 std::monostate());
    }
};

} // namespace nic::action

} // namespace redfish::debug_token
