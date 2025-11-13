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
#include "logging.hpp"

#include <sdbusplus/bus/match.hpp>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>

namespace redfish::dot_async
{

// DBus interface name for async status monitoring
constexpr const std::string_view asyncStatusIntf = "com.nvidia.Async.Status";

// Base DBus path for async operation objects
constexpr const std::string_view asyncOperationBasePath =
    "/com/nvidia/nsmd/AsyncOperation";

/**
 * @brief DOT operation result states
 *
 * Enumeration of possible states for DOT async operations.
 */
enum class DotState
{
    None,
    Success,
    Error,
    InProgress,
    InvalidArgument,
    Unavailable,
    UnsupportedRequest
};

/// Result tuple containing operation state and optional error message
using DotResult = std::tuple<DotState, std::string /* error message */>;

/// Callback function type for DOT operation completion
using DotResultCallback = std::function<void(const DotResult&)>;

/**
 * @brief Base class for DOT async operations monitoring infrastructure
 *
 * Provides reusable infrastructure for monitoring DBus async operations:
 * - Match creation/destruction for DBus signals
 * - Status monitoring and handling
 * - Result callback management
 * - Thread-safe completion tracking
 *
 * Derived classes implement specific commands by calling DBus methods
 * and then using the monitoring infrastructure.
 */
class DotAsyncBase : public std::enable_shared_from_this<DotAsyncBase>
{
  public:
    DotAsyncBase(const DotAsyncBase&) = delete;
    DotAsyncBase& operator=(const DotAsyncBase&) = delete;
    DotAsyncBase(DotAsyncBase&&) = delete;
    DotAsyncBase& operator=(DotAsyncBase&&) = delete;

    /**
     * @brief Destructor - invokes callback with final result
     *
     * Ensures the callback is invoked with the operation result when the
     * handler is destroyed, providing guarantee that callback is always called
     * exactly once.
     */
    virtual ~DotAsyncBase()
    {
        if (callback)
        {
            callback(result);
        }
    }

    /**
     * @brief Create DBus signal match for async operation monitoring
     *
     * Sets up a DBus properties changed signal match to monitor the status
     * of the async operation.
     */
    void createMatch()
    {
        std::string matchRule =
            sdbusplus::bus::match::rules::propertiesChangedNamespace(
                asyncOperationBasePath, asyncStatusIntf);
        BMCWEB_LOG_DEBUG("DOT: Creating D-Bus match with rule: {}", matchRule);
        match = std::make_unique<sdbusplus::bus::match_t>(
            *crow::connections::systemBus, matchRule.c_str(),
            [self = shared_from_this()](sdbusplus::message::message& msg) {
                self->matchHandler(msg);
            });
        BMCWEB_LOG_DEBUG("DOT: D-Bus match created successfully");
    }

    /**
     * @brief Destroy DBus signal match
     *
     * Cleans up the DBus signal match when async operation monitoring is
     * complete or no longer needed.
     */
    void destroyMatch()
    {
        match.reset(nullptr);
    }

    /**
     * @brief Monitor async operation status
     *
     * Polls the initial status of the async operation object.
     */
    void monitorAsyncOperation()
    {
        BMCWEB_LOG_DEBUG("DOT: Starting to monitor async path: {}",
                         asyncObjectPath);
        dbus::utility::getProperty<std::string>(
            asyncObjectService, asyncObjectPath, std::string(asyncStatusIntf),
            "Status",
            [self = shared_from_this()](const boost::system::error_code& ec,
                                        const std::string& status) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("Failed to get DOT async status: {}",
                                     ec.message());
                    self->setResultAndComplete(DotState::Error, ec.message());
                    return;
                }
                BMCWEB_LOG_DEBUG("DOT: Initial status poll returned: {}",
                                 status);
                self->handleAsyncStatus(status);
            });
    }

    /// Operation result, updated as operation progresses
    DotResult result{DotState::Error, "Operation not completed"};

    /// DBus object path for the async operation
    std::string asyncObjectPath;

    /// DBus service name for the async operation
    std::string asyncObjectService;

  protected:
    /**
     * @brief Protected constructor for derived classes
     * @param cb Callback to invoke with operation result
     */
    explicit DotAsyncBase(DotResultCallback&& cb) : callback(std::move(cb)) {}

    /**
     * @brief Set result and mark operation as complete (thread-safe)
     *
     * This ensures the result is only set once, even if called from
     * multiple sources (poll and match handler).
     *
     * @param state Operation result state
     * @param message Error message (if any)
     */
    void setResultAndComplete(DotState state, const std::string& message)
    {
        if (completed)
        {
            BMCWEB_LOG_DEBUG(
                "DOT: Ignoring duplicate completion (already completed)");
            return;
        }

        completed = true;
        result = std::make_tuple(state, message);
        destroyMatch();
    }

    /**
     * @brief Handle async operation status updates
     *
     * Processes status updates from the async operation, extracting the status
     * value and mapping it to appropriate DotState.
     *
     * @param dbusStatus Full DBus enum status string
     */
    void handleAsyncStatus(const std::string& dbusStatus)
    {
        if (completed)
        {
            BMCWEB_LOG_DEBUG("DOT: Ignoring status update (already completed)");
            return;
        }

        // Extract status from enum string (e.g.,
        // "com.nvidia.Async.Status.Success" -> "Success")
        std::string status =
            dbusStatus.substr(dbusStatus.find_last_of('.') + 1);

        BMCWEB_LOG_DEBUG("DOT: handleAsyncStatus - raw: '{}', extracted: '{}'",
                         dbusStatus, status);

        if (status.empty() || status == "InProgress")
        {
            BMCWEB_LOG_DEBUG(
                "DOT: Status is InProgress, continuing to monitor");
            return;
        }

        // Terminal states - complete the operation
        if (status == "Success")
        {
            BMCWEB_LOG_DEBUG("DOT: Operation completed successfully");
            setResultAndComplete(DotState::Success, "");
        }
        else if (status == "InvalidArgument")
        {
            BMCWEB_LOG_ERROR("DOT: Operation failed - InvalidArgument");
            setResultAndComplete(DotState::InvalidArgument, status);
        }
        else if (status == "UnsupportedRequest")
        {
            BMCWEB_LOG_ERROR("DOT: Operation failed - UnsupportedRequest");
            setResultAndComplete(DotState::UnsupportedRequest, status);
        }
        else if (status == "Unavailable")
        {
            BMCWEB_LOG_ERROR("DOT: Operation failed - Unavailable");
            setResultAndComplete(DotState::Unavailable, status);
        }
        else
        {
            BMCWEB_LOG_ERROR("DOT: Operation failed with status: {}", status);
            setResultAndComplete(DotState::Error, status);
        }
    }

  private:
    /**
     * @brief Handle DBus properties changed signal
     *
     * Callback for DBus signal matches.
     * @param msg DBus message containing properties changed signal data
     */
    void matchHandler(sdbusplus::message::message& msg)
    {
        BMCWEB_LOG_DEBUG("DOT: matchHandler called for path: {}",
                         msg.get_path());

        if (msg.get_path() != asyncObjectPath)
        {
            BMCWEB_LOG_DEBUG(
                "DOT: Ignoring match - path mismatch (expected: {})",
                asyncObjectPath);
            return;
        }

        if (completed)
        {
            BMCWEB_LOG_DEBUG("DOT: Ignoring match signal (already completed)");
            return;
        }

        std::string interface;
        dbus::utility::DBusPropertiesMap propertiesMap;
        msg.read(interface, propertiesMap);

        BMCWEB_LOG_DEBUG("DOT: Properties changed on interface: {}", interface);

        const std::string* dbusStatus = nullptr;
        for (const auto& [key, value] : propertiesMap)
        {
            if (key == "Status")
            {
                dbusStatus = std::get_if<std::string>(&value);
                break;
            }
        }

        if (dbusStatus == nullptr)
        {
            BMCWEB_LOG_ERROR("Failed to read DOT async Status property");
            return;
        }

        BMCWEB_LOG_DEBUG("DOT: Match handler received status update: {}",
                         *dbusStatus);
        handleAsyncStatus(*dbusStatus);
    }

    /// User callback to invoke with final result
    DotResultCallback callback;

    /// DBus signal match for monitoring operation status changes
    std::unique_ptr<sdbusplus::bus::match_t> match;

    /// Flag to ensure completion happens only once
    bool completed{false};
};

} // namespace redfish::dot_async
