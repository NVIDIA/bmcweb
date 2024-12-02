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

#include <boost/asio/steady_timer.hpp>
#include <sdbusplus/bus/match.hpp>

namespace redfish
{
namespace nvidia_async_operation_utils
{
static constexpr std::string_view setAsyncInterfaceName =
    "com.nvidia.Async.Set";
static constexpr auto setAsyncMethodName = "Set";
static constexpr auto asyncStatusInterfaceName = "com.nvidia.Async.Status";
static constexpr auto asyncStatusPropertyName = "Status";
static constexpr auto asyncStatusValueInProgress =
    "com.nvidia.Async.Status.AsyncOperationStatus.InProgress";
static constexpr auto asyncStatusValueSuccess =
    "com.nvidia.Async.Status.AsyncOperationStatus.Success";
static constexpr auto asyncStatusValueTimeout =
    "com.nvidia.Async.Status.AsyncOperationStatus.Timeout";
static constexpr auto asyncStatusValueInternalFailure =
    "com.nvidia.Async.Status.AsyncOperationStatus.InternalFailure";
static constexpr auto asyncStatusValueResourceNotFound =
    "com.nvidia.Async.Status.AsyncOperationStatus.ResourceNotFound";
static constexpr auto asyncStatusValueUnavailable =
    "com.nvidia.Async.Status.AsyncOperationStatus.Unavailable";
static constexpr auto asyncStatusValueUnsupportedRequest =
    "com.nvidia.Async.Status.AsyncOperationStatus.UnsupportedRequest";
static constexpr auto asyncStatusValueWriteFailure =
    "com.nvidia.Async.Status.AsyncOperationStatus.WriteFailure";
static constexpr auto asyncStatusValueInvalidArgument =
    "com.nvidia.Async.Status.AsyncOperationStatus.InvalidArgument";

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
    bool completed;
};

template <typename SetAsyncStatusInfo>
void reportErrorAndCancel(std::shared_ptr<SetAsyncStatusInfo> statusInfo)
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
        std::weak_ptr<SetAsyncStatusInfo> weakStatusInfo) :
        weakStatusInfo(weakStatusInfo)
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
        std::weak_ptr<SetAsyncStatusInfo> weakStatusInfo) :
        weakStatusInfo(weakStatusInfo)
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
        std::weak_ptr<SetAsyncStatusInfo> weakStatusInfo) :
        weakStatusInfo(weakStatusInfo)
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
            BMCWEB_LOG_ERROR("Set Async : Set failed with unexptected error {}",
                             ec);

            const sd_bus_error* dbusError = msg.get_error();

            if (dbusError != nullptr)
            {
                BMCWEB_LOG_ERROR("Set Async : Set failed with DBus error {}",
                                 dbusError->name);
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

        crow::connections::systemBus->async_method_call(
            SetAsyncGetStatus<SetAsyncStatusInfo>{statusInfo},
            statusInfo->service, statusInfo->object,
            "org.freedesktop.DBus.Properties", "Get", statusInfo->interface,
            statusInfo->property);
    }

  private:
    std::weak_ptr<SetAsyncStatusInfo> weakStatusInfo;
};

template <typename Callback, typename Value>
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

    crow::connections::systemBus->async_method_call(
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
void doGenericSetAsyncAndGatherResult(
    std::shared_ptr<bmcweb::AsyncResp> resp,
    const std::chrono::milliseconds timeout, const std::string& service,
    const std::string& object, const std::string& interface,
    const std::string& property, Value&& value, Callback&& callback)
{
    doSetAsyncAndGatherResult(
        resp, timeout, service, object, interface, property,
        std::string{setAsyncInterfaceName}, setAsyncMethodName,
        asyncStatusInterfaceName, asyncStatusPropertyName,
        std::forward<Value>(value), std::forward<Callback>(callback));
}

} // namespace nvidia_async_operation_utils
} // namespace redfish
