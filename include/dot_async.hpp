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

#include "dot/base.hpp"
#include "dot/dot_async_base.hpp"

#include <string>
#include <string_view>

namespace redfish::dot_async
{
/**
 * @brief Handler for DOT command operations
 *
 * Implements specific DOT commands (CAKInstall, Bypass) using the
 * reusable async monitoring infrastructure.
 */
class DotCommandHandler : public DotAsyncBase
{
  public:
    /**
     * @brief Start DOT CAK Install operation
     *
     * Factory method to create a handler instance and initiate Component
     * Authentication Key (CAK) installation operation asynchronously.
     * Provisions both CAK and optionally Lock Authentication Key (LAK) on the
     * DOT-capable component.
     *
     * @param service DBus service name providing the DOT interface
     * @param objectPath DBus object path for the DOT component
     * @param cakAuthScheme CAK authentication scheme (Ecdsa or Hybrid)
     * @param cakEcdsaKey CAK ECDSA key data in base64 format
     * @param cakLmsKey CAK LMS key data in base64 format (required for Hybrid)
     * @param lakAuthScheme LAK authentication scheme (Ecdsa or Hybrid)
     * @param lakEcdsaKey LAK ECDSA key data in base64 format
     * @param lakLmsKey LAK LMS key data in base64 format (required for Hybrid)
     * @param lockDisable Flag to disable lock requirement after installation
     * @param minSvn Minimum security version number for the component
     * @param callback Result callback invoked upon operation completion
     */
    static void startCAKInstall(
        const std::string& service, const std::string& objectPath,
        const std::string& cakAuthScheme, const std::string& cakEcdsaKey,
        const std::string& cakLmsKey, const std::string& lakAuthScheme,
        const std::string& lakEcdsaKey, const std::string& lakLmsKey,
        bool lockDisable, uint32_t minSvn, DotResultCallback&& callback)
    {
        struct MakeSharedHelper : public DotCommandHandler
        {
            MakeSharedHelper(DotResultCallback&& cb) :
                DotCommandHandler(std::move(cb))
            {}
        };
        auto handler = std::make_shared<MakeSharedHelper>(std::move(callback));
        handler->runCAKInstall(service, objectPath, cakAuthScheme, cakEcdsaKey,
                               cakLmsKey, lakAuthScheme, lakEcdsaKey, lakLmsKey,
                               lockDisable, minSvn);
    }

    /**
     * @brief Start DOT Bypass operation
     *
     * Factory method to create a handler instance and initiate DOT bypass
     * operation asynchronously. Bypasses Component Authentication Key (CAK)
     * requirements on the DOT-capable component.
     *
     * @param service DBus service name providing the DOT interface
     * @param objectPath DBus object path for the DOT component
     * @param callback Result callback invoked upon operation completion
     */
    static void startBypass(const std::string& service,
                            const std::string& objectPath,
                            DotResultCallback&& callback)
    {
        struct MakeSharedHelper : public DotCommandHandler
        {
            MakeSharedHelper(DotResultCallback&& cb) :
                DotCommandHandler(std::move(cb))
            {}
        };
        auto handler = std::make_shared<MakeSharedHelper>(std::move(callback));
        handler->runBypass(service, objectPath);
    }

  private:
    /**
     * @brief Private constructor
     * @param cb Callback to invoke with operation result
     */
    explicit DotCommandHandler(DotResultCallback&& cb) :
        DotAsyncBase(std::move(cb))
    {}

    /**
     * @brief Parse DBus error and map to DotState
     *
     * Analyzes DBus error messages to determine the appropriate DotState error
     * category. Recognizes specific error strings (InvalidArgument,
     * Unavailable) and maps them to corresponding states, defaulting to generic
     * Error state.
     *
     * @param errorMsg The error message from DBus error_code
     * @return Appropriate DotState based on error type
     */
    static DotState parseDbusError(const std::string& errorMsg)
    {
        if (errorMsg.find("InvalidArgument") != std::string::npos)
        {
            return DotState::InvalidArgument;
        }
        if (errorMsg.find("Unavailable") != std::string::npos)
        {
            return DotState::Unavailable;
        }
        return DotState::Error;
    }

    /**
     * @brief Convert auth scheme string to DBus enum format
     *
     * Converts user-friendly authentication scheme names ("Ecdsa" or "Hybrid")
     * into their full DBus enumeration paths required by the DOT DBus interface
     * (e.g., "com.nvidia.Dot.Action.KeyAuthScheme.Ecdsa").
     *
     * @param scheme Authentication scheme: "Ecdsa" or "Hybrid"
     * @return Full DBus enum path or empty string on error
     */
    static std::string convertAuthSchemeToDbusEnum(const std::string& scheme)
    {
        if (scheme == "Ecdsa")
        {
            return std::string(dot::dotActionIntf) + ".KeyAuthScheme.Ecdsa";
        }
        if (scheme == "Hybrid")
        {
            return std::string(dot::dotActionIntf) + ".KeyAuthScheme.Hybrid";
        }
        BMCWEB_LOG_ERROR("Invalid authentication scheme: {}", scheme);
        return "";
    }

    /**
     * @brief Execute DOT CAK Install operation via DBus
     *
     * Initiates the CAK installation and sets up monitoring.
     */
    void runCAKInstall(
        const std::string& service, const std::string& objectPath,
        const std::string& cakAuthScheme, const std::string& cakEcdsaKey,
        const std::string& cakLmsKey, const std::string& lakAuthScheme,
        const std::string& lakEcdsaKey, const std::string& lakLmsKey,
        bool lockDisable, uint32_t minSvn)
    {
        std::string cakAuthEnum = convertAuthSchemeToDbusEnum(cakAuthScheme);
        std::string lakAuthEnum = convertAuthSchemeToDbusEnum(lakAuthScheme);

        if (cakAuthEnum.empty() || lakAuthEnum.empty())
        {
            result = std::make_tuple(DotState::Error,
                                     "Invalid authentication scheme");
            return;
        }

        asyncObjectService = service;

        dbus::utility::async_method_call(
            [self = shared_from_this()](
                const boost::system::error_code& ec,
                const sdbusplus::message::object_path& asyncPath) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("DotCAKInstall DBus error: {}",
                                     ec.message());
                    DotState errorState = parseDbusError(ec.message());
                    self->result = std::make_tuple(errorState, ec.message());
                    return;
                }
                self->asyncObjectPath = asyncPath.str;
                self->createMatch();
                self->monitorAsyncOperation();
            },
            service, objectPath, std::string(dot::dotActionIntf),
            "DotCAKInstall", cakAuthEnum, cakEcdsaKey, cakLmsKey, lakAuthEnum,
            lakEcdsaKey, lakLmsKey, lockDisable, minSvn);
    }

    /**
     * @brief Execute DOT Bypass operation via DBus
     *
     * Initiates the bypass operation and sets up monitoring.
     */
    void runBypass(const std::string& service, const std::string& objectPath)
    {
        BMCWEB_LOG_DEBUG("DOT Bypass: Calling service={} objectPath={}",
                         service, objectPath);
        asyncObjectService = service;

        dbus::utility::async_method_call(
            [self = shared_from_this()](
                const boost::system::error_code& ec,
                const sdbusplus::message::object_path& asyncPath) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("DOT Bypass DBus error: {}", ec.message());
                    DotState errorState = parseDbusError(ec.message());
                    self->result = std::make_tuple(errorState, ec.message());
                    return;
                }
                BMCWEB_LOG_DEBUG("DOT Bypass: Received async path: {}",
                                 asyncPath.str);
                self->asyncObjectPath = asyncPath.str;
                self->createMatch();
                self->monitorAsyncOperation();
            },
            service, objectPath, std::string(dot::dotActionIntf), "Bypass");
    }
};

} // namespace redfish::dot_async
