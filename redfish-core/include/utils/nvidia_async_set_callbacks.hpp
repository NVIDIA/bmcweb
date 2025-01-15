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
#include "utils/nvidia_async_set_utils.hpp"

namespace redfish
{
namespace nvidia_async_operation_utils
{

class PatchGenericCallback
{
  public:
    explicit PatchGenericCallback(std::shared_ptr<bmcweb::AsyncResp> resp) :
        resp(std::move(resp))
    {}

    void operator()(const std::string& status) const
    {
        if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
        {
            return;
        }

        if (status ==
            nvidia_async_operation_utils::asyncStatusValueWriteFailure)
        {
            // Service failed to change the config
            messages::operationFailed(resp->res);
        }
        else if (status ==
                 nvidia_async_operation_utils::asyncStatusValueUnavailable)
        {
            std::string errBusy = "0x50A";
            std::string errBusyResolution =
                "Command failed with error busy, please try after 60 seconds";

            // busy error
            messages::asyncError(resp->res, errBusy, errBusyResolution);
        }
        else if (status ==
                 nvidia_async_operation_utils::asyncStatusValueTimeout)
        {
            std::string errTimeout = "0x600";
            std::string errTimeoutResolution =
                "Settings may/maynot have applied, please check get response before patching";

            // timeout error
            messages::asyncError(resp->res, errTimeout, errTimeoutResolution);
        }
        else
        {
            messages::internalError(resp->res);
        }
    }

  private:
    std::shared_ptr<bmcweb::AsyncResp> resp;
};

using PatchMigModeCallback = PatchGenericCallback;
using PatchEccModeCallback = PatchGenericCallback;
using PatchPortDisableCallback = PatchGenericCallback;
using PatchPowerModeCallback = PatchGenericCallback;
using PatchIsolationModeCallback = PatchGenericCallback;
using PatchEdppSetPointCallback = PatchGenericCallback;
using PatchCCModeCallback = PatchGenericCallback;
using PatchEgmModeCallback = PatchGenericCallback;

class PatchSpeedConfigCallback
{
  public:
    explicit PatchSpeedConfigCallback(std::shared_ptr<bmcweb::AsyncResp> resp,
                                      uint32_t speedLimit) :
        resp(std::move(resp)), speedLimit(speedLimit)
    {}

    void operator()(const std::string& status) const
    {
        if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
        {
            return;
        }

        if (status ==
            nvidia_async_operation_utils::asyncStatusValueWriteFailure)
        {
            // Service failed to change the config
            messages::operationFailed(resp->res);
        }
        else if (status ==
                 nvidia_async_operation_utils::asyncStatusValueUnavailable)
        {
            std::string errBusy = "0x50A";
            std::string errBusyResolution =
                "Command failed with error busy, please try after 60 seconds";

            // busy error
            messages::asyncError(resp->res, errBusy, errBusyResolution);
        }
        else if (status ==
                 nvidia_async_operation_utils::asyncStatusValueTimeout)
        {
            std::string errTimeout = "0x600";
            std::string errTimeoutResolution =
                "Settings may/maynot have applied, please check get response before patching";

            // timeout error
            messages::asyncError(resp->res, errTimeout, errTimeoutResolution);
        }
        else if (status ==
                 nvidia_async_operation_utils::asyncStatusValueInvalidArgument)
        {
            // Invalid value
            messages::propertyValueIncorrect(resp->res, "SpeedLimitMHz",
                                             std::to_string(speedLimit));
        }
        else
        {
            messages::internalError(resp->res);
        }
    }

  private:
    std::shared_ptr<bmcweb::AsyncResp> resp;
    uint32_t speedLimit;
};

class PatchPowerCapCallback
{
  public:
    explicit PatchPowerCapCallback(std::shared_ptr<bmcweb::AsyncResp> resp,
                                   int64_t setpoint) :
        resp(std::move(resp)), setpoint(setpoint)
    {}

    void operator()(const std::string& status) const
    {
        if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
        {
            return;
        }

        if (status ==
            nvidia_async_operation_utils::asyncStatusValueWriteFailure)
        {
            // Service failed to change the config
            messages::operationFailed(resp->res);
        }
        else if (status ==
                 nvidia_async_operation_utils::asyncStatusValueUnavailable)
        {
            std::string errBusy = "0x50A";
            std::string errBusyResolution =
                "Command failed with error busy, please try after 60 seconds";

            // busy error
            messages::asyncError(resp->res, errBusy, errBusyResolution);
        }
        else if (status ==
                 nvidia_async_operation_utils::asyncStatusValueTimeout)
        {
            std::string errTimeout = "0x600";
            std::string errTimeoutResolution =
                "Settings may/maynot have applied, please check get response before patching";

            // timeout error
            messages::asyncError(resp->res, errTimeout, errTimeoutResolution);
        }
        else if (status ==
                 nvidia_async_operation_utils::asyncStatusValueInvalidArgument)
        {
            // Invalid value
            messages::propertyValueIncorrect(resp->res, "setpoint",
                                             std::to_string(setpoint));
        }
        else
        {
            messages::internalError(resp->res);
        }
    }

  private:
    std::shared_ptr<bmcweb::AsyncResp> resp;
    int64_t setpoint;
};

class PatchClockLimitControlCallback
{
  public:
    explicit PatchClockLimitControlCallback(
        std::shared_ptr<bmcweb::AsyncResp> resp) : resp(std::move(resp))
    {}

    void operator()(const std::string& status) const
    {
        if (status == nvidia_async_operation_utils::asyncStatusValueSuccess)
        {
            messages::success(resp->res);
            return;
        }

        if (status ==
            nvidia_async_operation_utils::asyncStatusValueWriteFailure)
        {
            // Service failed to change the config
            messages::operationFailed(resp->res);
        }
        else if (status ==
                 nvidia_async_operation_utils::asyncStatusValueUnavailable)
        {
            std::string errBusy = "0x50A";
            std::string errBusyResolution =
                "NSM Command failed with error busy, please try after 60 seconds";

            // busy error
            messages::asyncError(resp->res, errBusy, errBusyResolution);
        }
        else if (status ==
                 nvidia_async_operation_utils::asyncStatusValueTimeout)
        {
            std::string errTimeout = "0x600";
            std::string errTimeoutResolution =
                "Settings may/maynot have applied, please check get response before patching";

            // timeout error

            messages::asyncError(resp->res, errTimeout, errTimeoutResolution);
        }
        else
        {
            messages::internalError(resp->res);
        }
    }

  private:
    std::shared_ptr<bmcweb::AsyncResp> resp;
};

/**
 * @brief Async patch method
 *
 * @tparam Callback Async Patch callback type
 * @tparam Value Type of property to set
 * @param aResp Pointer to object holding response data
 * @param service DBus object service for property set call
 * @param path DBus object path for property set call
 * @param interface DBus property interface for property set call
 * @param property DBus property name
 * @param value DBus property value
 * @param showError Show error and return if async interface doesn't exists in
 * DBus path
 */
template <typename Callback = PatchGenericCallback, typename Value>
inline void patch(std::shared_ptr<bmcweb::AsyncResp> aResp,
                  const std::string& service, const std::string& path,
                  const std::string& interface, const std::string& property,
                  const Value& value, bool showError = true)
{
    BMCWEB_LOG_DEBUG("PATCH {} {} {} {} {}", service, path, property, interface,
                     value);
    dbus::utility::getDbusObject(
        path, std::array<std::string_view, 1>{setAsyncInterfaceName},
        [aResp, path, service, property, interface, value,
         showError](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetObject& object) {
            if (!ec)
            {
                for (const auto& [serv, _] : object)
                {
                    if (serv != service)
                    {
                        continue;
                    }
                    BMCWEB_LOG_DEBUG(
                        "Performing Patch using Set Async Method Call");
                    doGenericSetAsyncAndGatherResult(
                        aResp, std::chrono::seconds(60), service, path,
                        interface, property, std::variant<Value>(value),
                        Callback{aResp});

                    return;
                }
            }
            else if (showError)
            {
                BMCWEB_LOG_ERROR("Missing setAsyncInterface object for {}",
                                 path);
                messages::internalError(aResp->res);
                return;
            }

            BMCWEB_LOG_DEBUG("Performing Patch using set-property Call");

            // Set the property, with handler to check error responses
            crow::connections::systemBus->async_method_call(
                [aResp, property, interface,
                 service](boost::system::error_code ec,
                          sdbusplus::message::message& msg) {
                    if (!ec)
                    {
                        BMCWEB_LOG_DEBUG("Set {} property for {} succeeded",
                                         property, interface);
                        return;
                    }
                    BMCWEB_LOG_WARNING(
                        "Set {} property for {} failed: {}. Serv: {}", property,
                        interface, ec, service);

                    // Read and convert dbus error message to redfish error
                    const sd_bus_error* dbusError = msg.get_error();
                    if (dbusError == nullptr)
                    {
                        messages::internalError(aResp->res);
                        return;
                    }

                    if (strcmp(dbusError->name,
                               "xyz.openbmc_project.Common."
                               "Device.Error.WriteFailure") == 0)
                    {
                        // Service failed to change the config
                        messages::operationFailed(aResp->res);
                    }
                    else if (
                        strcmp(
                            dbusError->name,
                            "xyz.openbmc_project.Common.Error.Unavailable") ==
                        0)
                    {
                        std::string errBusy = "0x50A";
                        std::string errBusyResolution =
                            "SMBPBI Command failed with error busy, please try after 60 seconds";

                        // busy error
                        messages::asyncError(aResp->res, errBusy,
                                             errBusyResolution);
                    }
                    else if (strcmp(
                                 dbusError->name,
                                 "xyz.openbmc_project.Common.Error.Timeout") ==
                             0)
                    {
                        std::string errTimeout = "0x600";
                        std::string errTimeoutResolution =
                            "Settings may/maynot have applied, please check get response before patching";

                        // timeout error
                        messages::asyncError(aResp->res, errTimeout,
                                             errTimeoutResolution);
                    }
                    else if (strcmp(
                                 dbusError->name,
                                 "org.freedesktop.DBus.Error.UnknownObject") ==
                             0)
                    {
                        BMCWEB_LOG_WARNING("Serv {} didn't support this object",
                                           service);
                    }
                    else
                    {
                        messages::internalError(aResp->res);
                    }
                },
                service, path, "org.freedesktop.DBus.Properties", "Set",
                interface, property, std::variant<Value>(value));
        });
}

} // namespace nvidia_async_operation_utils
} // namespace redfish
