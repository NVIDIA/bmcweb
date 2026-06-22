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
#include "nvidia_async_set_utils.hpp"

#include <boost/asio/steady_timer.hpp>
#include <sdbusplus/bus/match.hpp>

namespace redfish
{
namespace nvidia_async_operation_utils
{
static constexpr auto callAsyncValueInterfaceName = "com.nvidia.Async.Value";
static constexpr auto callAsyncValuePropertyName = "Value";

template <typename Callback, typename ValueType>
struct CallAsyncStatusHandlerInfo
{
    std::shared_ptr<bmcweb::AsyncResp> aresp;
    const Callback callback;
    std::unique_ptr<sdbusplus::bus::match_t> match;
    const std::string service;
    std::string object;
    const std::string statusInterface;
    const std::string statusProperty;
    const std::string valueInterface;
    const std::string valueProperty;
    boost::asio::steady_timer timeoutTimer;
    bool completed{};
    using Value = ValueType;
};

template <typename CallAsyncStatusInfo>
// NOLINTNEXTLINE(performance-unnecessary-value-param,cppcoreguidelines-avoid-non-const-global-variables)
void callAsyncGetValue(std::shared_ptr<CallAsyncStatusInfo> statusInfo,
                       const std::string& status)
{
    if constexpr (std::is_same_v<typename CallAsyncStatusInfo::Value, void>)
    {
        if (status != asyncStatusValueInProgress)
        {
            statusInfo->completed = true;
            statusInfo->callback(status);
            statusInfo->timeoutTimer.cancel();
        }
    }
    else
    {
        if (status != asyncStatusValueInProgress)
        {
            std::weak_ptr<CallAsyncStatusInfo> weakStatusInfo{statusInfo};
            crow::connections::systemBus->async_method_call(
                [weakStatusInfo, status](
                    const boost::system::error_code ec,
                    const std::variant<typename CallAsyncStatusInfo::Value>&
                        value) {
                    auto statInfo = weakStatusInfo.lock();
                    if (!statInfo || statInfo->completed)
                    {
                        BMCWEB_LOG_INFO(
                            "Call Async : Redundant Response for GetValue or Response arrived after the timeout.");
                        return;
                    }

                    if (ec)
                    {
                        BMCWEB_LOG_INFO(
                            "Call Async : GetValue failed with error {}", ec);
                        reportErrorAndCancel(statInfo);
                        return;
                    }
                    const auto valuePtr =
                        std::get_if<typename CallAsyncStatusInfo::Value>(
                            &value);
                    if (valuePtr == nullptr &&
                        status != asyncStatusValueSuccess)
                    {
                        BMCWEB_LOG_INFO(
                            "Call Async: Failed to get value from variant for non-success status");
                        reportErrorAndCancel(statInfo);
                        return;
                    }
                    statInfo->completed = true;
                    statInfo->callback(status, valuePtr);
                    statInfo->timeoutTimer.cancel();
                },
                statusInfo->service, statusInfo->object,
                "org.freedesktop.DBus.Properties", "Get",
                statusInfo->valueInterface, statusInfo->valueProperty);
        }
    }
}

template <typename CallAsyncStatusInfo>
class CallAsyncGetStatus
{
  public:
    explicit CallAsyncGetStatus(
        std::weak_ptr<CallAsyncStatusInfo> statusInfoPtr) :
        // NOLINTNEXTLINE(performance-unnecessary-value-param)
        weakStatusInfo(statusInfoPtr)
    {}

    void operator()(const boost::system::error_code ec,
                    const std::variant<std::string>& status)
    {
        auto statusInfo = weakStatusInfo.lock();
        if (!statusInfo || statusInfo->completed)
        {
            BMCWEB_LOG_INFO(
                "Call Async : Redudent Response for GetStatus or Response arrived after the timeout.");
            return;
        }

        if (ec)
        {
            BMCWEB_LOG_INFO("Call Async : GetStatus failed with error {}", ec);
            reportErrorAndCancel(statusInfo);
        }
        else
        {
            const std::string* statusString = std::get_if<std::string>(&status);

            if (statusString == nullptr)
            {
                BMCWEB_LOG_INFO("Call Async : Error in GetStatus Call");
                reportErrorAndCancel(statusInfo);
            }
            else
            {
                BMCWEB_LOG_INFO("Call Async : Status from Get Status Call : {}",
                                *statusString);

                callAsyncGetValue(statusInfo, *statusString);
            }
        }
    }

  private:
    std::weak_ptr<CallAsyncStatusInfo> weakStatusInfo;
};

template <typename CallAsyncStatusInfo>
class CallAsyncStatusChanged
{
  public:
    explicit CallAsyncStatusChanged(
        std::weak_ptr<CallAsyncStatusInfo> statusInfoPtr) :
        // NOLINTNEXTLINE(performance-unnecessary-value-param)
        weakStatusInfo(statusInfoPtr)
    {}

    void operator()(sdbusplus::message::message& msg)
    {
        auto statusInfo = weakStatusInfo.lock();
        if (!statusInfo || statusInfo->completed)
        {
            BMCWEB_LOG_INFO(
                "Call Async : Status PropertiesChanged signal arrived after the timeout.");
            return;
        }

        BMCWEB_LOG_DEBUG(
            "Call Async : Status PropertiesChanged signal Object Path : {}",
            msg.get_path());

        std::string interface;
        std::map<std::string, dbus::utility::DbusVariantType> properties;
        msg.read(interface, properties);

        BMCWEB_LOG_DEBUG(
            "Call Async : Status PropertiesChanged signal Interface : {}",
            interface);

        if (interface == statusInfo->statusInterface)
        {
            for (const auto& [property, value] : properties)
            {
                BMCWEB_LOG_DEBUG(
                    "Call Async : Status PropertiesChanged signal Property : {}",
                    property);

                if (property == statusInfo->statusProperty)
                {
                    const std::string* status =
                        std::get_if<std::string>(&value);

                    if (status == nullptr)
                    {
                        BMCWEB_LOG_INFO(
                            "Call Async : Error while obtaining Status from PropertiesChanged signal");

                        reportErrorAndCancel(statusInfo);
                    }
                    else
                    {
                        BMCWEB_LOG_INFO(
                            "Call Async : Status from PropertiesChanged signal : {}",
                            *status);

                        callAsyncGetValue(statusInfo, *status);
                    }

                    return;
                }
            }
        }
    }

  private:
    std::weak_ptr<CallAsyncStatusInfo> weakStatusInfo;
};

template <typename CallAsyncStatusInfo>
class CallAsyncMethodCall
{
  public:
    explicit CallAsyncMethodCall(
        std::weak_ptr<CallAsyncStatusInfo> statusInfoPtr) :
        // NOLINTNEXTLINE(performance-unnecessary-value-param)
        weakStatusInfo(statusInfoPtr)
    {}

    void operator()(boost::system::error_code ec,
                    sdbusplus::message::message& msg)
    {
        auto statusInfo = weakStatusInfo.lock();
        if (!statusInfo)
        {
            BMCWEB_LOG_INFO(
                "Call Async : DBus Response arrived after the timeout.");
            return;
        }

        if (ec)
        {
            BMCWEB_LOG_INFO("Call Async : Set failed with unexpected error {}",
                            ec);
            std::optional<std::string> statusToSend;
            const sd_bus_error* dbusError = msg.get_error();
            if (dbusError != nullptr && dbusError->name != nullptr)
            {
                statusToSend = mapDbusErrorNameToAsyncStatus(dbusError->name);
                if (statusToSend.has_value())
                {
                    statusInfo->completed = true;
                    if constexpr (std::is_same_v<
                                      typename CallAsyncStatusInfo::Value,
                                      void>)
                    {
                        statusInfo->callback(*statusToSend);
                    }
                    else
                    {
                        statusInfo->callback(*statusToSend, nullptr);
                    }
                    statusInfo->timeoutTimer.cancel();
                    return;
                }
            }

            reportErrorAndCancel(statusInfo);

            return;
        }

        sdbusplus::object_path objectPath;
        msg.read(objectPath);
        statusInfo->object = objectPath;

        BMCWEB_LOG_DEBUG("Call Async : Status Object Path : {}",
                         statusInfo->object);

        statusInfo->match = std::make_unique<sdbusplus::bus::match_t>(
            *crow::connections::systemBus,
            sdbusplus::bus::match::rules::propertiesChanged(
                statusInfo->object, statusInfo->statusInterface),
            CallAsyncStatusChanged<CallAsyncStatusInfo>{statusInfo});

        dbus::utility::async_method_call(
            CallAsyncGetStatus<CallAsyncStatusInfo>{statusInfo},
            statusInfo->service, statusInfo->object,
            "org.freedesktop.DBus.Properties", "Get",
            statusInfo->statusInterface, statusInfo->statusProperty);
    }

  private:
    std::weak_ptr<CallAsyncStatusInfo> weakStatusInfo;
};

template <typename Value, typename Callback, typename... Params>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
void doCallAsyncAndGatherResult(
    std::shared_ptr<bmcweb::AsyncResp> resp,
    const std::chrono::milliseconds timeout, const std::string& service,
    const std::string& object, const std::string& interface,
    const std::string& method, const std::string& statusInterface,
    const std::string& statusProperty, const std::string& valueInterface,
    const std::string& valueProperty, Callback&& callback, Params&&... params)
{
    using CallAsyncStatusInfo =
        CallAsyncStatusHandlerInfo<typename std::decay_t<Callback>, Value>;

    std::shared_ptr<CallAsyncStatusInfo> statusInfo(new CallAsyncStatusInfo{
        .aresp{resp},
        .callback{std::forward<Callback>(callback)},
        .match{},
        .service{service},
        .object{},
        .statusInterface{statusInterface},
        .statusProperty{statusProperty},
        .valueInterface{valueInterface},
        .valueProperty{valueProperty},
        .timeoutTimer = boost::asio::steady_timer(
            crow::connections::systemBus->get_io_context()),
        .completed{}});

    dbus::utility::async_method_call(
        CallAsyncMethodCall<CallAsyncStatusInfo>{statusInfo},
        statusInfo->service, object, interface, method,
        std::forward<Params>(params)...);

    statusInfo->timeoutTimer.expires_after(timeout);
    statusInfo->timeoutTimer.async_wait(
        [statusInfo](boost::system::error_code ec) {
            if (ec != boost::asio::error::operation_aborted)
            {
                BMCWEB_LOG_INFO("Call Async : Operation timed out.");
                messages::operationTimeout(statusInfo->aresp->res);
            }
        });
}

template <typename Value, typename Callback, typename... Params>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
void doGenericCallAsyncAndGatherResult(
    std::shared_ptr<bmcweb::AsyncResp> resp,
    const std::chrono::milliseconds timeout, const std::string& service,
    const std::string& object, const std::string& interface,
    const std::string& method, Callback&& callback, Params&&... params)
{
    doCallAsyncAndGatherResult<Value>(
        std::move(resp), timeout, service, object, interface, method,
        asyncStatusInterfaceName, asyncStatusPropertyName,
        callAsyncValueInterfaceName, callAsyncValuePropertyName,
        std::forward<Callback>(callback), std::forward<Params>(params)...);
}

} // namespace nvidia_async_operation_utils
} // namespace redfish
