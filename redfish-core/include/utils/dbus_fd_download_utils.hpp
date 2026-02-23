/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2026 NVIDIA CORPORATION &
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
#include "task.hpp"

#include <unistd.h>

#include <boost/system/error_code.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/exception.hpp>
#include <sdbusplus/message/native_types.hpp>
#include <xyz/openbmc_project/Common/Progress/common.hpp>

namespace redfish
{
namespace dbus_fd_utils
{

inline void streamFdResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, const sdbusplus::message::unix_fd& fd)
{
    if (ec)
    {
        if (ec == boost::system::errc::host_unreachable)
        {
            BMCWEB_LOG_DEBUG("Backend wasn't reachable");
            asyncResp->res.result(boost::beast::http::status::not_found);
            return;
        }
        messages::internalError(asyncResp->res);
        return;
    }

    asyncResp->res.addHeader("Content-Type", "application/octet-stream");

    lseek(fd, 0, SEEK_SET);
    asyncResp->res.openFd(dup(fd));
}

inline void getDbusResultFd(const std::string& serviceName,
                            const sdbusplus::message::object_path& objectPath,
                            const std::string& interface,
                            const std::string& property,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    BMCWEB_LOG_DEBUG("Reading result FD from {} path={} iface={} prop={}",
                     serviceName, objectPath.str, interface, property);
    dbus::utility::getProperty<sdbusplus::message::unix_fd>(
        serviceName, objectPath, interface, property,
        [asyncResp](const boost::system::error_code& ec,
                    const sdbusplus::message::unix_fd& fd) {
            streamFdResponse(asyncResp, ec, fd);
        });
}

inline bool processProgressProperties(
    const dbus::utility::DBusPropertiesMap& values,
    const std::shared_ptr<task::TaskData>& taskData)
{
    using Progress = sdbusplus::common::xyz::openbmc_project::common::Progress;
    using OpStatus = Progress::OperationStatus;

    std::string index = std::to_string(taskData->index);

    const std::string* status = nullptr;
    const uint8_t* progress = nullptr;
    if (!sdbusplus::unpackPropertiesNoThrow(
            redfish::dbus_utils::UnpackErrorPrinter(), values, "Status", status,
            "Progress", progress))
    {
        BMCWEB_LOG_ERROR("Failed to unpack properties.  Wrong type?");
        taskData->messages.emplace_back(messages::internalError());
        return !task::completed;
    }

    if (progress != nullptr)
    {
        BMCWEB_LOG_DEBUG("Progress changed to {}", *progress);
        taskData->percentComplete = *progress;
        taskData->extendTimer(std::chrono::minutes(5));
    }

    if (status != nullptr)
    {
        BMCWEB_LOG_DEBUG("Status changed to {}", *status);

        std::optional<OpStatus> opStatus =
            Progress::convertStringToOperationStatus(*status);
        if (!opStatus)
        {
            BMCWEB_LOG_WARNING("Unexpected status: {}", *status);
            taskData->state = "Exception";
            taskData->percentComplete = 100;
            return task::completed;
        }

        switch (*opStatus)
        {
            case OpStatus::InProgress:
                return !task::completed;
            case OpStatus::Completed:
                taskData->messages.emplace_back(
                    messages::taskCompletedOK(index));
                taskData->state = "Completed";
                break;
            case OpStatus::Failed:
            case OpStatus::Aborted:
                taskData->state = "Exception";
                taskData->messages.emplace_back(messages::taskAborted(index));
                break;
            default:
                BMCWEB_LOG_WARNING("Unhandled status: {}", *status);
                taskData->state = "Exception";
                break;
        }

        taskData->percentComplete = 100;
        return task::completed;
    }

    return !task::completed;
}

inline bool handleTaskMessage(const boost::system::error_code& ec,
                              sdbusplus::message_t& msg,
                              const std::shared_ptr<task::TaskData>& taskData)
{
    if (ec)
    {
        taskData->messages.emplace_back(messages::internalError());
        return task::completed;
    }

    std::string iface;
    dbus::utility::DBusPropertiesMap values;

    try
    {
        msg.read(iface, values);
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        BMCWEB_LOG_ERROR("Failed to read D-Bus message: {}", e.what());
        return !task::completed;
    }

    return processProgressProperties(values, taskData);
}

} // namespace dbus_fd_utils
} // namespace redfish
