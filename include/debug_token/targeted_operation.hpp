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
#include "debug_token/request_utils.hpp"
#include "debug_token/status_utils.hpp"

#include <memory>
#include <variant>
#include <vector>

namespace redfish::debug_token
{

// NSMD request handler has 3 retries at 2 second interval. Updating the timer
// to 7 seconds now. However timer dependency needs to be removed by relying on
// signals and bmcweb default timeout handler.
constexpr const int targetedOpTimeoutSeconds = 7;

enum class TargetedOperation
{
    DisableTokens,
    GenerateTokenRequest,
    GetTokenStatus,
    InstallToken
};

using TargetedOperationArgument =
    std::variant<std::monostate, std::string, std::vector<uint8_t>>;

using TargetedOperationResult =
    std::variant<std::monostate, uint16_t, NsmTokenStatus,
                 std::vector<uint8_t>>;

using TargetedOperationResultCallback =
    std::function<void(EndpointState, TargetedOperationResult)>;

class TargetedOperationHandler
{
  public:
    TargetedOperationHandler(const std::string& chassisId, TargetedOperation op,
                             TargetedOperationResultCallback&& cb,
                             TargetedOperationArgument arg = std::monostate()) :
        operation(op), argument(arg), callback(cb)
    {
        constexpr std::array<std::string_view, 1> interfaces = {debugTokenIntf};
        dbus::utility::getSubTree(
            std::string(debugTokenBasePath), 0, interfaces,
            [this,
             chassisId](const boost::system::error_code& ec,
                        const dbus::utility::MapperGetSubTreeResponse& resp) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("GetSubTreePaths error: {}", ec);
                    tokenUnsupportedHandler();
                    return;
                }
                if (resp.size() == 0)
                {
                    BMCWEB_LOG_ERROR(
                        "No objects with DebugToken interface found");
                    tokenUnsupportedHandler();
                    return;
                }
                std::string objectPath;
                std::string service;
                for (const auto& [path, serviceMap] : resp)
                {
                    auto pathChassis =
                        std::filesystem::path(path).filename().string();
                    if (chassisId == pathChassis)
                    {
                        objectPath = path;
                        service = serviceMap[0].first;
                        break;
                    }
                }
                if (objectPath.empty())
                {
                    BMCWEB_LOG_ERROR(
                        "DebugToken interface not implemented for {}",
                        chassisId);
                    tokenUnsupportedHandler();
                    return;
                }
                tokenOperationTimer =
                    std::make_unique<boost::asio::steady_timer>(
                        crow::connections::systemBus->get_io_context());
                tokenOperationTimer->expires_after(
                    std::chrono::seconds(targetedOpTimeoutSeconds));
                tokenOperationTimer->async_wait(
                    [this](const boost::system::error_code& ec) {
                        match.reset(nullptr);
                        if (!ec)
                        {
                            BMCWEB_LOG_ERROR("Debug token operation timeout");
                            generalErrorHandler();
                            return;
                        }
                        if (ec != boost::asio::error::operation_aborted)
                        {
                            BMCWEB_LOG_ERROR("async_wait error {}", ec);
                            generalErrorHandler();
                        }
                    });
                std::string matchRule(
                    "type='signal',"
                    "interface='org.freedesktop.DBus.Properties',"
                    "path='" +
                    objectPath +
                    "',"
                    "member='PropertiesChanged'");
                match = std::make_unique<sdbusplus::bus::match_t>(
                    *crow::connections::systemBus, matchRule,
                    [this, objectPath, service](sdbusplus::message_t& msg) {
                        std::string interface;
                        std::map<std::string, dbus::utility::DbusVariantType>
                            props;
                        msg.read(interface, props);
                        std::string opStatus;
                        if (interface == "xyz.openbmc_project.Common.Progress")
                        {
                            auto it = props.find("Status");
                            if (it != props.end())
                            {
                                auto status =
                                    std::get_if<std::string>(&(it->second));
                                if (status)
                                {
                                    opStatus = status->substr(
                                        status->find_last_of('.') + 1);
                                }
                            }
                        }
                        if (opStatus.empty())
                        {
                            return;
                        }
                        tokenOperationTimer.reset(nullptr);
                        if (opStatus == "Completed")
                        {
                            switch (operation)
                            {
                                case TargetedOperation::GenerateTokenRequest:
                                    requestHandler(objectPath, service);
                                    return;

                                case TargetedOperation::GetTokenStatus:
                                    statusHandler(objectPath, service);
                                    return;

                                default:
                                    successHandler();
                                    return;
                            }
                        }
                        if (opStatus == "Aborted")
                        {
                            errorHandler(objectPath, service);
                            return;
                        }
                        BMCWEB_LOG_ERROR("Status received: {}", opStatus);
                        generalErrorHandler();
                    });
                auto dbusErrorHandler =
                    [this](const boost::system::error_code& ec) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR("DBus error: {}", ec.message());
                            generalErrorHandler();
                        }
                    };
                switch (operation)
                {
                    case TargetedOperation::DisableTokens:
                    {
                        crow::connections::systemBus->async_method_call(
                            dbusErrorHandler, service, objectPath,
                            std::string(debugTokenIntf), "DisableTokens");
                        break;
                    }

                    case TargetedOperation::GenerateTokenRequest:
                    {
                        std::string* tokenOpcode =
                            std::get_if<std::string>(&argument);
                        if (!tokenOpcode)
                        {
                            BMCWEB_LOG_ERROR("Invalid argument");
                            generalErrorHandler();
                            return;
                        }
                        std::string arg =
                            std::string(debugTokenOpcodesEnumPrefix) +
                            *tokenOpcode;
                        crow::connections::systemBus->async_method_call(
                            dbusErrorHandler, service, objectPath,
                            std::string(debugTokenIntf), "GetRequest", arg);
                        break;
                    }

                    case TargetedOperation::GetTokenStatus:
                    {
                        std::string* tokenType =
                            std::get_if<std::string>(&argument);
                        if (!tokenType)
                        {
                            BMCWEB_LOG_ERROR("Invalid argument");
                            generalErrorHandler();
                            return;
                        }
                        std::string arg =
                            std::string(debugTokenTypesEnumPrefix) + *tokenType;
                        crow::connections::systemBus->async_method_call(
                            dbusErrorHandler, service, objectPath,
                            std::string(debugTokenIntf), "GetStatus", arg);
                        break;
                    }

                    case TargetedOperation::InstallToken:
                    {
                        std::vector<uint8_t>* token =
                            std::get_if<std::vector<uint8_t>>(&argument);
                        if (!token)
                        {
                            BMCWEB_LOG_ERROR("Invalid argument");
                            generalErrorHandler();
                            return;
                        }
                        crow::connections::systemBus->async_method_call(
                            dbusErrorHandler, service, objectPath,
                            std::string(debugTokenIntf), "InstallToken",
                            *token);
                        break;
                    }

                    default:
                    {
                        BMCWEB_LOG_ERROR("Invalid token operation");
                        generalErrorHandler();
                        break;
                    }
                }
            });
    }

    bool finished(int& timeout) const
    {
        BMCWEB_LOG_DEBUG("callback: {}, match: {}, timeout: {}",
                         static_cast<bool>(callback), static_cast<bool>(match),
                         static_cast<bool>(tokenOperationTimer));
        bool isFinished = !callback && !match && !tokenOperationTimer;
        timeout = isFinished ? 0 : targetedOpTimeoutSeconds;
        return isFinished;
    }

  private:
    TargetedOperation operation;
    TargetedOperationArgument argument;
    TargetedOperationResultCallback callback;

    std::unique_ptr<boost::asio::steady_timer> tokenOperationTimer;
    std::unique_ptr<sdbusplus::bus::match_t> match;

    void requestHandler(const std::string& objectPath,
                        const std::string& service)
    {
        sdbusplus::asio::getProperty<sdbusplus::message::unix_fd>(
            *crow::connections::systemBus, service, objectPath,
            std::string(debugTokenIntf), "RequestFd",
            [this](const boost::system::error_code ec,
                   const sdbusplus::message::unix_fd& unixfd) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("DBus Get error {}", ec);
                    generalErrorHandler();
                    return;
                }
                BMCWEB_LOG_DEBUG("Received fd: {}", unixfd.fd);
                std::vector<uint8_t> request;
                if (!readNsmTokenRequestFd(unixfd.fd, request))
                {
                    generalErrorHandler();
                    return;
                }
                NsmDebugTokenRequest* nsmReq =
                    reinterpret_cast<NsmDebugTokenRequest*>(request.data());
                switch (nsmReq->status)
                {
                    case NsmDebugTokenChallengeQueryStatus::OK:
                        callback(EndpointState::RequestAcquired, request);
                        break;
                    case NsmDebugTokenChallengeQueryStatus::TokenAlreadyApplied:
                        callback(EndpointState::TokenInstalled,
                                 std::monostate());
                        break;
                    case NsmDebugTokenChallengeQueryStatus::TokenNotSupported:
                        callback(EndpointState::DebugTokenUnsupported,
                                 std::monostate());
                        break;
                    default:
                        BMCWEB_LOG_ERROR("NSM token request - status: {}",
                                         nsmReq->status);
                        callback(EndpointState::Error, std::monostate());
                        break;
                }
                cleanup();
            });
    }

    void statusHandler(const std::string& objectPath,
                       const std::string& service)
    {
        sdbusplus::asio::getProperty<NsmDbusTokenStatus>(
            *crow::connections::systemBus, service, objectPath,
            std::string(debugTokenIntf), "TokenStatus",
            [this](const boost::system::error_code ec,
                   const NsmDbusTokenStatus& dbusStatus) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("DBus Get error {}", ec);
                    generalErrorHandler();
                    return;
                }
                try
                {
                    NsmTokenStatus nsmStatus(dbusStatus);
                    callback(EndpointState::StatusAcquired, nsmStatus);
                    cleanup();
                }
                catch (const std::exception&)
                {
                    generalErrorHandler();
                }
            });
    }

    void errorHandler(const std::string& objectPath, const std::string& service)
    {
        sdbusplus::asio::getProperty<std::tuple<uint16_t, std::string>>(
            *crow::connections::systemBus, service, objectPath,
            std::string(debugTokenIntf), "ErrorCode",
            [this](const boost::system::error_code ec,
                   const std::tuple<uint16_t, std::string>& errorCode) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("DBus Get error {}", ec);
                    generalErrorHandler();
                    return;
                }
                uint16_t code = std::get<uint16_t>(errorCode);
                if (code == debugTokenUnsupportedNsmErrorCode)
                {
                    tokenUnsupportedHandler();
                    return;
                }
                BMCWEB_LOG_ERROR("NSM error code: {}", code);
                callback(EndpointState::Error, code);
                cleanup();
            });
    }

    void generalErrorHandler()
    {
        if (callback)
        {
        callback(EndpointState::Error, std::monostate());
        }
        else
        {
            BMCWEB_LOG_ERROR("Token operation callback is already called.");
        }
        cleanup();
    }

    void successHandler()
    {
        callback(EndpointState::None, std::monostate());
        cleanup();
    }

    void tokenUnsupportedHandler()
    {
        callback(EndpointState::DebugTokenUnsupported, std::monostate());
        cleanup();
    }

    void cleanup()
    {
        boost::asio::post(crow::connections::systemBus->get_io_context(),
                          [this] {
                              callback = nullptr;
                              match.reset(nullptr);
                              tokenOperationTimer.reset(nullptr);
                          });
    }
};

} // namespace redfish::debug_token
