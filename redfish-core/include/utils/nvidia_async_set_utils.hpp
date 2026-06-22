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

#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"

#include <systemd/sd-bus.h>

#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/message.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace redfish
{
namespace nvidia_async_operation_utils
{
static const std::string setAsyncInterfaceName = "com.nvidia.Async.Set";
static const std::string setAsyncMethodName = "Set";

// Canonical timeout for an async (com.nvidia.Async.Set) property set. This is
// the single source of truth for the value the stock single-GPU setters pass to
// doGenericSetAsyncAndGatherResult (std::chrono::seconds(60)) and for the bulk
// fan-out timeout. Both the chrono duration AND any operator-facing "retry
// after N seconds" message text must be derived from this constant so the two
// never drift apart.
static constexpr unsigned int asyncSetTimeoutSeconds = 60;
static const std::string asyncStatusInterfaceName = "com.nvidia.Async.Status";
static const std::string asyncStatusPropertyName = "Status";

static constexpr std::string_view asyncStatusValueInProgress =
    "com.nvidia.Async.Status.AsyncOperationStatus.InProgress";
static constexpr std::string_view asyncStatusValueSuccess =
    "com.nvidia.Async.Status.AsyncOperationStatus.Success";
static constexpr std::string_view asyncStatusValueTimeout =
    "com.nvidia.Async.Status.AsyncOperationStatus.Timeout";
static constexpr std::string_view asyncStatusValueInternalFailure =
    "com.nvidia.Async.Status.AsyncOperationStatus.InternalFailure";
static constexpr std::string_view asyncStatusValueResourceNotFound =
    "com.nvidia.Async.Status.AsyncOperationStatus.ResourceNotFound";
static constexpr std::string_view asyncStatusValueUnavailable =
    "com.nvidia.Async.Status.AsyncOperationStatus.Unavailable";
static constexpr std::string_view asyncStatusValueUnsupportedRequest =
    "com.nvidia.Async.Status.AsyncOperationStatus.UnsupportedRequest";
static constexpr std::string_view asyncStatusValueWriteFailure =
    "com.nvidia.Async.Status.AsyncOperationStatus.WriteFailure";
static constexpr std::string_view asyncStatusValueInvalidArgument =
    "com.nvidia.Async.Status.AsyncOperationStatus.InvalidArgument";
static constexpr std::string_view asyncStatusValueConflictingOperation =
    "com.nvidia.Async.Status.AsyncOperationStatus.ConflictingOperation";

/**
 * Maps D-Bus error name to async status. Use when msg.get_error() is available
 * for precise mapping (avoids EIO fallback for unknown D-Bus errors).
 */
inline std::optional<std::string> mapDbusErrorNameToAsyncStatus(
    std::string_view dbusErrorName)
{
    if (dbusErrorName.empty())
    {
        return std::nullopt;
    }
    if (dbusErrorName == "xyz.openbmc_project.Common.Error.InvalidArgument")
    {
        return std::string(asyncStatusValueInvalidArgument);
    }
    if (dbusErrorName == "xyz.openbmc_project.Common.Error.Unavailable")
    {
        return std::string(asyncStatusValueUnavailable);
    }
    if (dbusErrorName == "xyz.openbmc_project.Common.Error.InternalFailure")
    {
        return std::string(asyncStatusValueInternalFailure);
    }
    if (dbusErrorName == "xyz.openbmc_project.Common.Error.Timeout")
    {
        return std::string(asyncStatusValueTimeout);
    }
    if (dbusErrorName == "xyz.openbmc_project.Common.Error.NotAllowed")
    {
        return std::string(asyncStatusValueUnavailable);
    }
    if (dbusErrorName == "xyz.openbmc_project.Common.Device.Error.WriteFailure")
    {
        return std::string(asyncStatusValueWriteFailure);
    }
    if (dbusErrorName == "org.freedesktop.DBus.Error.UnknownObject" ||
        dbusErrorName == "org.freedesktop.DBus.Error.UnknownMethod")
    {
        return std::string(asyncStatusValueResourceNotFound);
    }
    return std::nullopt;
}

template <typename Callback>
struct SetAsyncStatusHandlerInfo
{
    std::shared_ptr<bmcweb::AsyncResp> aresp;
    const Callback callback;
    std::unique_ptr<sdbusplus::bus::match_t> match;
    const std::string service;
    std::string object;
    const std::string interface;
    const std::string property;
    boost::asio::steady_timer timeoutTimer;
    bool completed{};
};

template <typename SetAsyncStatusInfo>
void reportErrorAndCancel(const std::shared_ptr<SetAsyncStatusInfo>& statusInfo)
{
    statusInfo->completed = true;
    messages::internalError(statusInfo->aresp->res);
    statusInfo->timeoutTimer.cancel();
}

template <typename SetAsyncStatusInfo>
class SetAsyncGetStatus
{
  public:
    explicit SetAsyncGetStatus(
        std::weak_ptr<SetAsyncStatusInfo> inWeakStatusInfo) :
        weakStatusInfo(std::move(inWeakStatusInfo))
    {}

    void operator()(const boost::system::error_code ec,
                    const std::variant<std::string>& status)
    {
        auto statusInfo = weakStatusInfo.lock();
        if (!statusInfo || statusInfo->completed)
        {
            BMCWEB_LOG_INFO(
                "Set Async : Redudent Response for GetStatus or Response arrived after the timeout.");
            return;
        }

        if (ec)
        {
            BMCWEB_LOG_ERROR("Set Async : GetStatus failed with error {}", ec);
            reportErrorAndCancel(statusInfo);
        }
        else
        {
            const std::string* statusString = std::get_if<std::string>(&status);

            if (statusString == nullptr)
            {
                BMCWEB_LOG_ERROR("Set Async : Error in GetStatus Call");
                reportErrorAndCancel(statusInfo);
            }
            else
            {
                BMCWEB_LOG_INFO("Set Async : Status from Get Status Call : {}",
                                *statusString);

                if (*statusString != asyncStatusValueInProgress)
                {
                    statusInfo->completed = true;
                    statusInfo->callback(*statusString);
                    statusInfo->timeoutTimer.cancel();
                }
            }
        }
    }

  private:
    std::weak_ptr<SetAsyncStatusInfo> weakStatusInfo;
};

template <typename SetAsyncStatusInfo>
class SetAsyncStatusChanged
{
  public:
    explicit SetAsyncStatusChanged(
        std::weak_ptr<SetAsyncStatusInfo> inWeakStatusInfo) :
        weakStatusInfo(std::move(inWeakStatusInfo))
    {}

    void operator()(sdbusplus::message::message& msg)
    {
        auto statusInfo = weakStatusInfo.lock();
        if (!statusInfo || statusInfo->completed)
        {
            BMCWEB_LOG_INFO(
                "Set Async : Redudent Status PropertiesChanged signal or signal arrived after the timeout.");
            return;
        }

        BMCWEB_LOG_DEBUG(
            "Set Async : Status PropertiesChanged signal Object Path : {}",
            msg.get_path());

        std::string interface;
        std::map<std::string, dbus::utility::DbusVariantType> properties;
        msg.read(interface, properties);

        BMCWEB_LOG_DEBUG(
            "Set Async : Status PropertiesChanged signal Interface : {}",
            interface);

        if (interface == statusInfo->interface)
        {
            for (const auto& [property, value] : properties)
            {
                BMCWEB_LOG_DEBUG(
                    "Set Async : Status PropertiesChanged signal Property : {}",
                    property);

                if (property == statusInfo->property)
                {
                    const std::string* status =
                        std::get_if<std::string>(&value);

                    if (status == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Set Async : Error while obtaining Status from PropertiesChanged signal");

                        reportErrorAndCancel(statusInfo);
                    }
                    else
                    {
                        BMCWEB_LOG_INFO(
                            "Set Async : Status from PropertiesChanged signal : {}",
                            *status);

                        if (*status != asyncStatusValueInProgress)
                        {
                            statusInfo->completed = true;
                            statusInfo->callback(*status);
                            statusInfo->timeoutTimer.cancel();
                        }
                    }

                    return;
                }
            }
        }
    }

  private:
    std::weak_ptr<SetAsyncStatusInfo> weakStatusInfo;
};

template <typename SetAsyncStatusInfo>
class SetAsyncMethodCall
{
  public:
    explicit SetAsyncMethodCall(
        std::weak_ptr<SetAsyncStatusInfo> inWeakStatusInfo) :
        weakStatusInfo(std::move(inWeakStatusInfo))
    {}

    void operator()(boost::system::error_code ec,
                    sdbusplus::message::message& msg)
    {
        auto statusInfo = weakStatusInfo.lock();
        if (!statusInfo)
        {
            BMCWEB_LOG_INFO(
                "Set Async : DBus Response arrived after the timeout.");
            return;
        }

        if (ec)
        {
            BMCWEB_LOG_ERROR("Set Async : Set failed with unexpected error {}",
                             ec);

            std::optional<std::string> statusToSend;
            const sd_bus_error* dbusError = msg.get_error();
            if (dbusError != nullptr && dbusError->name != nullptr)
            {
                statusToSend = mapDbusErrorNameToAsyncStatus(dbusError->name);
                if (statusToSend.has_value())
                {
                    statusInfo->completed = true;
                    statusInfo->callback(*statusToSend);
                    statusInfo->timeoutTimer.cancel();
                    return;
                }
            }

            reportErrorAndCancel(statusInfo);

            return;
        }

        sdbusplus::message::object_path objectPath;
        msg.read(objectPath);
        statusInfo->object = objectPath;

        BMCWEB_LOG_DEBUG("Set Async : Status Object Path : {}",
                         statusInfo->object);

        statusInfo->match = std::make_unique<sdbusplus::bus::match_t>(
            *crow::connections::systemBus,
            sdbusplus::bus::match::rules::propertiesChanged(
                statusInfo->object, statusInfo->interface),
            SetAsyncStatusChanged<SetAsyncStatusInfo>{statusInfo});

        dbus::utility::async_method_call(
            SetAsyncGetStatus<SetAsyncStatusInfo>{statusInfo},
            statusInfo->service, statusInfo->object,
            "org.freedesktop.DBus.Properties", "Get", statusInfo->interface,
            statusInfo->property);
    }

  private:
    std::weak_ptr<SetAsyncStatusInfo> weakStatusInfo;
};

template <typename Callback, typename Value>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
void doSetAsyncAndGatherResult(
    std::shared_ptr<bmcweb::AsyncResp> resp,
    const std::chrono::milliseconds timeout, const std::string& service,
    const std::string& object, const std::string& interface,
    const std::string& property, const std::string& setAsyncInterface,
    const std::string& setAsyncMethod, const std::string& statusInterface,
    const std::string& statusProperty, Value&& value, Callback&& callback)
{
    using SetAsyncStatusInfo =
        SetAsyncStatusHandlerInfo<typename std::decay_t<Callback>>;

    std::shared_ptr<SetAsyncStatusInfo> statusInfo(new SetAsyncStatusInfo{
        .aresp{resp},
        .callback{std::forward<Callback>(callback)},
        .match{},
        .service{service},
        .object{},
        .interface{statusInterface},
        .property{statusProperty},
        .timeoutTimer = boost::asio::steady_timer(
            crow::connections::systemBus->get_io_context()),
        .completed{}});

    dbus::utility::async_method_call(
        SetAsyncMethodCall<SetAsyncStatusInfo>{statusInfo}, statusInfo->service,
        object, setAsyncInterface, setAsyncMethod, interface, property,
        std::forward<Value>(value));

    statusInfo->timeoutTimer.expires_after(timeout);
    statusInfo->timeoutTimer.async_wait(
        [statusInfo](boost::system::error_code ec) {
            if (ec != boost::asio::error::operation_aborted)
            {
                BMCWEB_LOG_INFO("Set Async : Operation timed out.");
                messages::operationTimeout(statusInfo->aresp->res);
            }
        });
}

template <typename Callback, typename Value>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
void doGenericSetAsyncAndGatherResult(
    std::shared_ptr<bmcweb::AsyncResp> resp,
    const std::chrono::milliseconds timeout, const std::string& service,
    const std::string& object, const std::string& interface,
    const std::string& property, Value&& value, Callback&& callback)
{
    doSetAsyncAndGatherResult(
        std::move(resp), timeout, service, object, interface, property,
        std::string{setAsyncInterfaceName}, setAsyncMethodName,
        asyncStatusInterfaceName, asyncStatusPropertyName,
        std::forward<Value>(value), std::forward<Callback>(callback));
}

// ----------------------------------------------------------------------------
// Bulk-aware async setter
//
// doGenericSetAsyncForBulk is a sibling of doGenericSetAsyncAndGatherResult
// whose ONLY behavioral difference is that the two terminal paths that the
// stock setter writes directly to the shared asyncResp->res
// (internal/unmapped D-Bus error, and per-set timeout) instead deliver a
// status string to the supplied callback. The success and mapped-failure
// paths are identical (they already route through the callback). This lets a
// bulk fan-out aggregate N×2 outcomes without any per-set write corrupting the
// single shared response — the bulk handler's assembleResponse remains the
// sole writer of res.
//
// The stock doGenericSetAsyncAndGatherResult is intentionally left byte-for-
// byte unchanged so existing single-property callers are unaffected.
// ----------------------------------------------------------------------------

// Routes the internal/unmapped-error terminal path to the callback as a status
// string instead of messages::internalError(res).
template <typename SetAsyncStatusInfo>
void reportErrorToCallback(
    const std::shared_ptr<SetAsyncStatusInfo>& statusInfo)
{
    statusInfo->completed = true;
    statusInfo->callback(std::string(asyncStatusValueInternalFailure));
    statusInfo->timeoutTimer.cancel();
}

template <typename SetAsyncStatusInfo>
class BulkSetAsyncGetStatus
{
  public:
    explicit BulkSetAsyncGetStatus(
        std::weak_ptr<SetAsyncStatusInfo> inWeakStatusInfo) :
        weakStatusInfo(std::move(inWeakStatusInfo))
    {}

    void operator()(const boost::system::error_code ec,
                    const std::variant<std::string>& status)
    {
        auto statusInfo = weakStatusInfo.lock();
        if (!statusInfo || statusInfo->completed)
        {
            return;
        }

        if (ec)
        {
            reportErrorToCallback(statusInfo);
            return;
        }

        const std::string* statusString = std::get_if<std::string>(&status);
        if (statusString == nullptr)
        {
            reportErrorToCallback(statusInfo);
            return;
        }

        if (*statusString != asyncStatusValueInProgress)
        {
            statusInfo->completed = true;
            statusInfo->callback(*statusString);
            statusInfo->timeoutTimer.cancel();
        }
    }

  private:
    std::weak_ptr<SetAsyncStatusInfo> weakStatusInfo;
};

template <typename SetAsyncStatusInfo>
class BulkSetAsyncStatusChanged
{
  public:
    explicit BulkSetAsyncStatusChanged(
        std::weak_ptr<SetAsyncStatusInfo> inWeakStatusInfo) :
        weakStatusInfo(std::move(inWeakStatusInfo))
    {}

    void operator()(sdbusplus::message::message& msg)
    {
        auto statusInfo = weakStatusInfo.lock();
        if (!statusInfo || statusInfo->completed)
        {
            return;
        }

        std::string interface;
        std::map<std::string, dbus::utility::DbusVariantType> properties;
        msg.read(interface, properties);

        if (interface != statusInfo->interface)
        {
            return;
        }

        for (const auto& [property, value] : properties)
        {
            if (property != statusInfo->property)
            {
                continue;
            }

            const std::string* status = std::get_if<std::string>(&value);
            if (status == nullptr)
            {
                reportErrorToCallback(statusInfo);
                return;
            }

            if (*status != asyncStatusValueInProgress)
            {
                statusInfo->completed = true;
                statusInfo->callback(*status);
                statusInfo->timeoutTimer.cancel();
            }
            return;
        }
    }

  private:
    std::weak_ptr<SetAsyncStatusInfo> weakStatusInfo;
};

template <typename SetAsyncStatusInfo>
class BulkSetAsyncMethodCall
{
  public:
    explicit BulkSetAsyncMethodCall(
        std::weak_ptr<SetAsyncStatusInfo> inWeakStatusInfo) :
        weakStatusInfo(std::move(inWeakStatusInfo))
    {}

    void operator()(boost::system::error_code ec,
                    sdbusplus::message::message& msg)
    {
        auto statusInfo = weakStatusInfo.lock();
        if (!statusInfo || statusInfo->completed)
        {
            return;
        }

        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "Bulk Set Async : Set failed with unexpected error {}", ec);

            const sd_bus_error* dbusError = msg.get_error();
            if (dbusError != nullptr && dbusError->name != nullptr)
            {
                std::optional<std::string> statusToSend =
                    mapDbusErrorNameToAsyncStatus(dbusError->name);
                if (statusToSend.has_value())
                {
                    statusInfo->completed = true;
                    statusInfo->callback(*statusToSend);
                    statusInfo->timeoutTimer.cancel();
                    return;
                }
            }

            reportErrorToCallback(statusInfo);
            return;
        }

        sdbusplus::message::object_path objectPath;
        msg.read(objectPath);
        statusInfo->object = objectPath;

        statusInfo->match = std::make_unique<sdbusplus::bus::match_t>(
            *crow::connections::systemBus,
            sdbusplus::bus::match::rules::propertiesChanged(
                statusInfo->object, statusInfo->interface),
            BulkSetAsyncStatusChanged<SetAsyncStatusInfo>{statusInfo});

        dbus::utility::async_method_call(
            BulkSetAsyncGetStatus<SetAsyncStatusInfo>{statusInfo},
            statusInfo->service, statusInfo->object,
            "org.freedesktop.DBus.Properties", "Get", statusInfo->interface,
            statusInfo->property);
    }

  private:
    std::weak_ptr<SetAsyncStatusInfo> weakStatusInfo;
};

template <typename Callback, typename Value>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
void doSetAsyncForBulk(
    std::shared_ptr<bmcweb::AsyncResp> resp,
    const std::chrono::milliseconds timeout, const std::string& service,
    const std::string& object, const std::string& interface,
    const std::string& property, const std::string& setAsyncInterface,
    const std::string& setAsyncMethod, const std::string& statusInterface,
    const std::string& statusProperty, Value&& value, Callback&& callback)
{
    using SetAsyncStatusInfo =
        SetAsyncStatusHandlerInfo<typename std::decay_t<Callback>>;

    std::shared_ptr<SetAsyncStatusInfo> statusInfo(new SetAsyncStatusInfo{
        .aresp{resp},
        .callback{std::forward<Callback>(callback)},
        .match{},
        .service{service},
        .object{},
        .interface{statusInterface},
        .property{statusProperty},
        .timeoutTimer = boost::asio::steady_timer(
            crow::connections::systemBus->get_io_context()),
        .completed{}});

    dbus::utility::async_method_call(
        BulkSetAsyncMethodCall<SetAsyncStatusInfo>{statusInfo},
        statusInfo->service, object, setAsyncInterface, setAsyncMethod,
        interface, property, std::forward<Value>(value));

    statusInfo->timeoutTimer.expires_after(timeout);
    statusInfo->timeoutTimer.async_wait(
        [statusInfo](boost::system::error_code ec) {
            if (ec == boost::asio::error::operation_aborted)
            {
                return;
            }
            if (statusInfo->completed)
            {
                return;
            }
            BMCWEB_LOG_INFO("Bulk Set Async : Operation timed out.");
            statusInfo->completed = true;
            // Route timeout into the callback, never to asyncResp->res.
            statusInfo->callback(std::string(asyncStatusValueTimeout));
        });
}

template <typename Callback, typename Value>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
void doGenericSetAsyncForBulk(
    std::shared_ptr<bmcweb::AsyncResp> resp,
    const std::chrono::milliseconds timeout, const std::string& service,
    const std::string& object, const std::string& interface,
    const std::string& property, Value&& value, Callback&& callback)
{
    doSetAsyncForBulk(std::move(resp), timeout, service, object, interface,
                      property, std::string{setAsyncInterfaceName},
                      setAsyncMethodName, asyncStatusInterfaceName,
                      asyncStatusPropertyName, std::forward<Value>(value),
                      std::forward<Callback>(callback));
}

} // namespace nvidia_async_operation_utils
} // namespace redfish
