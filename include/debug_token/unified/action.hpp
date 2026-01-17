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

#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "debug_token/base.hpp"
#include "debug_token/unified/utils.hpp"
#include "logging.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/memfd_utils.hpp"

#include <boost/asio/post.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/unpack_properties.hpp>

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace redfish::debug_token::unified::action
{

enum class Operation
{
    EraseToken,
    InstallToken,
    GetTokenStatus
};

using Argument =
    std::variant<std::monostate, uint32_t, std::shared_ptr<MemoryFD>,
                 std::pair<std::string, std::string>>;

using Result = std::variant<std::monostate, NsmResult, TokenStatus>;
using ResultCallback = std::function<void(Result)>;

/**
 * @brief Handler class for managing debug token operations
 *
 * This class provides asynchronous handling of debug token operations including
 * token erasure, installation, and status retrieval. It manages D-Bus
 * communication with the debug token service and handles async operations
 * through property change monitoring.
 *
 * The class is designed to be used with shared_ptr and manages its own
 * lifecycle through enable_shared_from_this. Copy and move operations are
 * disabled to prevent issues with shared state management.
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
     * @brief Start a debug token operation by chassis ID
     *
     * Initiates a debug token operation by first discovering the appropriate
     * D-Bus service and object path for the given chassis ID, then executing
     * the specified operation.
     *
     * @param chassisId The chassis identifier to locate the debug token service
     * @param op The operation to perform (EraseToken, InstallToken,
     * GetTokenStatus)
     * @param arg The operation argument (token type for erase, token data for
     * install, none for status)
     * @param callback Function to call when the operation completes
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
     * @brief Start a debug token operation with known service and object path
     *
     * Initiates a debug token operation using the provided D-Bus service name
     * and object path, bypassing the chassis ID discovery process.
     *
     * @param service The D-Bus service name hosting the debug token interface
     * @param objectPath The D-Bus object path for the debug token
     * @param op The operation to perform (EraseToken, InstallToken,
     * GetTokenStatus)
     * @param arg The operation argument (token type for erase, token data for
     * install, none for status)
     * @param callback Function to call when the operation completes
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

    /**
     * @brief Destructor that ensures callback is invoked with final result
     *
     * The destructor guarantees that the callback function is called with the
     * current result state if it hasn't been called already. This ensures
     * that callers always receive a response, even if the operation fails
     * or the handler is destroyed unexpectedly.
     */
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

    std::string debugTokenService;
    std::string debugTokenObjectPath, asyncObjectPath;
    std::unique_ptr<sdbusplus::bus::match_t> match;

    /**
     * @brief Start operation by discovering service for given chassis ID
     *
     * Initiates the D-Bus service discovery process to find the debug token
     * service associated with the specified chassis ID.
     *
     * @param chassisId The chassis identifier to locate the debug token service
     */
    void run(const std::string& chassisId)
    {
        constexpr std::array<std::string_view, 1> interfaces = {
            debugTokenActionIntf};
        dbus::utility::getSubTree(
            std::string(debugTokenBasePath), 0, interfaces,
            std::bind_front(&Handler::subTreeHandler, this, shared_from_this(),
                            chassisId));
    }

    /**
     * @brief Handle D-Bus subtree discovery response
     *
     * Processes the response from D-Bus subtree discovery to find the debug
     * token service and object path for the specified chassis ID.
     *
     * @param unused Unused shared_ptr parameter for callback compatibility
     * @param chassisId The chassis identifier being searched for
     * @param ec Error code from the D-Bus operation
     * @param resp Response containing discovered services and object paths
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
     * @brief Execute the debug token operation with known service and object
     * path
     *
     * Performs the actual debug token operation using the provided D-Bus
     * service and object path. Handles different operation types and sets up
     * appropriate async monitoring for operations that require it.
     *
     * @param service The D-Bus service name hosting the debug token interface
     * @param objectPath The D-Bus object path for the debug token
     */
    void run(const std::string& service, const std::string& objectPath)
    {
        debugTokenService = service;
        debugTokenObjectPath = objectPath;
        std::function<void(const boost::system::error_code&,
                           const sdbusplus::message::object_path&)>
            asyncHandler = std::bind_front(&Handler::asyncMethodHandler, this,
                                           shared_from_this());
        switch (operation)
        {
            case Operation::EraseToken:
            {
                createMatch();
                std::pair<std::string, std::string>* eraseParams =
                    std::get_if<std::pair<std::string, std::string>>(&argument);
                if (eraseParams == nullptr)
                {
                    BMCWEB_LOG_ERROR("Invalid argument");
                    generalErrorHandler();
                    return;
                }
                crow::connections::systemBus->async_method_call(
                    asyncHandler, service, objectPath,
                    std::string(debugTokenActionIntf), "EraseToken",
                    eraseParams->first, eraseParams->second);
                break;
            }

            case Operation::InstallToken:
            {
                createMatch();
                std::shared_ptr<MemoryFD>* token =
                    std::get_if<std::shared_ptr<MemoryFD>>(&argument);
                if (token == nullptr)
                {
                    BMCWEB_LOG_ERROR("Invalid argument");
                    generalErrorHandler();
                    return;
                }
                crow::connections::systemBus->async_method_call(
                    asyncHandler, service, objectPath,
                    std::string(debugTokenActionIntf), "InstallToken",
                    sdbusplus::message::unix_fd(token->get()->fd));
                break;
            }

            case Operation::GetTokenStatus:
            {
                dbus::utility::getAllProperties(
                    debugTokenService, debugTokenObjectPath,
                    std::string(debugTokenStatusIntf),
                    std::bind_front(&Handler::tokenStatusHandler, this,
                                    shared_from_this()));
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
     * @brief Create D-Bus property change match for async operation monitoring
     *
     * Sets up a D-Bus match rule to monitor property changes on the async
     * operation status interface. This is used to detect when async operations
     * complete.
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
     * @brief Destroy the D-Bus property change match
     *
     * Cleans up the D-Bus match object to stop monitoring property changes.
     * This is called when the async operation completes or fails.
     *
     * @param unused Unused shared_ptr parameter for callback compatibility
     */
    void destroyMatch(const std::shared_ptr<Handler>& /*unused*/)
    {
        match.reset(nullptr);
    }

    /**
     * @brief Handle D-Bus property change notifications
     *
     * Processes property change messages from D-Bus to detect when async
     * operations complete. Extracts the status and processes it accordingly.
     *
     * @param self Shared pointer to this handler for lifecycle management
     * @param msg D-Bus message containing property change information
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
        std::string dbusStatus;
        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), propertiesMap, "Status",
            dbusStatus);
        if (!success)
        {
            BMCWEB_LOG_ERROR("Failed to unpack Status");
            return;
        }
        if (!asyncStatusHandler(self, dbusStatus))
        {
            return;
        }
        if (match)
        {
            boost::asio::post(
                crow::connections::systemBus->get_io_context(),
                std::bind_front(&Handler::destroyMatch, this, self));
        }
    }

    /**
     * @brief Handle async method call response
     *
     * Processes the response from async D-Bus method calls (EraseToken,
     * InstallToken). Stores the async object path and retrieves the current
     * status.
     *
     * @param self Shared pointer to this handler for lifecycle management
     * @param ec Error code from the D-Bus method call
     * @param objectPath The async operation object path returned by the method
     */
    void asyncMethodHandler(const std::shared_ptr<Handler>& self,
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
            *crow::connections::systemBus, debugTokenService, objectPath,
            std::string(asyncStatusIntf), "Status",
            std::bind_front(&Handler::getAsyncStatusHandler, this, self));
    }

    /**
     * @brief Handle async status retrieval response
     *
     * Processes the response from getting the async operation status property.
     * Determines if the operation is complete and handles the result
     * accordingly.
     *
     * @param self Shared pointer to this handler for lifecycle management
     * @param ec Error code from the D-Bus property get operation
     * @param dbusStatus The current status of the async operation
     */
    void getAsyncStatusHandler(const std::shared_ptr<Handler>& self,
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
        if (match)
        {
            destroyMatch(self);
        }
    }

    /**
     * @brief Process async operation status
     *
     * Analyzes the async operation status to determine if the operation is
     * complete. If complete, retrieves the result; if still in progress,
     * continues monitoring.
     *
     * @param self Shared pointer to this handler for lifecycle management
     * @param dbusStatus The status string from the async operation
     * @return true if the operation is complete, false if still in progress
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
        else
        {
            sdbusplus::asio::getProperty<std::variant<NsmResult>>(
                *crow::connections::systemBus, debugTokenService,
                asyncObjectPath, std::string(asyncValueIntf), "Value",
                std::bind_front(&Handler::asyncResultHandler, this, self));
        }
        return true;
    }

    /**
     * @brief Handle async operation result
     *
     * Processes the final result from an async operation, extracting the
     * NSM result code and message for the callback.
     *
     * @param unused Unused shared_ptr parameter for callback compatibility
     * @param ec Error code from the D-Bus property get operation
     * @param dbusResult The NSM result variant from the async operation
     */
    void asyncResultHandler(const std::shared_ptr<Handler>& /*unused*/,
                            const boost::system::error_code ec,
                            const std::variant<NsmResult>& dbusResult)
    {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBus Get error: {}", ec.message());
            generalErrorHandler();
            return;
        }
        const auto* nsmResult = std::get_if<NsmResult>(&dbusResult);
        if (nsmResult == nullptr)
        {
            BMCWEB_LOG_ERROR("Failed to read NSM code and message");
            generalErrorHandler();
            return;
        }
        result = *nsmResult;
    }

    /**
     * @brief Handle token status property retrieval response
     *
     * Processes the response from getting debug token status properties.
     * Extracts installation status, processing status, and token type
     * information.
     *
     * @param unused Unused shared_ptr parameter for callback compatibility
     * @param ec Error code from the D-Bus properties get operation
     * @param propertiesMap Map containing the retrieved properties
     */
    void tokenStatusHandler(
        const std::shared_ptr<Handler>& /*unused*/,
        const boost::system::error_code& ec,
        const dbus::utility::DBusPropertiesMap& propertiesMap)
    {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBus Get error: {}", ec.message());
            generalErrorHandler();
            return;
        }
        bool installationStatus = false;
        bool processingStatus = false;
        std::string tokenDeviceID;
        std::vector<std::tuple<std::string, std::vector<std::string>>>
            tokenTypesSubtypes;
        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), propertiesMap,
            "InstallationStatus", installationStatus, "ProcessingStatus",
            processingStatus, "TokenType", tokenTypesSubtypes, "TokenDeviceID",
            tokenDeviceID);
        if (!success)
        {
            BMCWEB_LOG_ERROR("Failed to unpack properties");
            generalErrorHandler();
            return;
        }
        result = TokenStatus(installationStatus, processingStatus,
                             tokenTypesSubtypes, tokenDeviceID);
    }

    /**
     * @brief Handle general errors during operation
     *
     * Sets the result to monostate (indicating an error) and cleans up
     * any active D-Bus matches. This is called when unrecoverable errors occur.
     */
    void generalErrorHandler()
    {
        result = std::monostate();
        if (match)
        {
            boost::asio::post(crow::connections::systemBus->get_io_context(),
                              std::bind_front(&Handler::destroyMatch, this,
                                              shared_from_this()));
        }
    }

    /**
     * @brief Handle unsupported token operation errors
     *
     * Sets the result to indicate that the debug token operation is not
     * supported on this system. This is called when the service discovery
     * fails or the operation is not available.
     */
    void tokenUnsupportedHandler()
    {
        result = NsmResult(debugTokenUnsupportedNsmErrorCode,
                           "Debug token unsupported");
    }
};

} // namespace redfish::debug_token::unified::action
