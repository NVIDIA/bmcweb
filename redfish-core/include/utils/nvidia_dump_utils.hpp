/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
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

#include "bmcweb_config.h"

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"
#include "nvidia_log_services.hpp"
#include "task.hpp"
#include "utils/dbus_fd_download_utils.hpp"

#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace redfish
{
namespace nvidia_dump_utils
{

// D-Bus identifiers are const char* rather than constexpr std::string_view:
// dbus::utility::async_method_call takes const std::string& for the
// service/object/interface/method arguments, and std::string is not implicitly
// constructible from std::string_view.
constexpr const char* dumpManagerService = "xyz.openbmc_project.Dump.Manager";
constexpr const char* systemDumpObjectPath = "/xyz/openbmc_project/dump/system";
constexpr const char* dumpCreateInterface = "xyz.openbmc_project.Dump.Create";
constexpr const char* progressInterface = "xyz.openbmc_project.Common.Progress";

// Matches the published signature of Dump.Create.CreateDump, a{sv} with
// variant[string,uint64]. Callers build the vector; nothing here interprets it.
using DumpCreateParams =
    std::vector<std::pair<std::string, std::variant<std::string, uint64_t>>>;

/**
 * @brief Map a CreateDump D-Bus error name onto a Redfish error.
 *
 * Deliberately not the generic mapping in log_services.hpp: that one renders
 * InvalidArgument as propertyValueIncorrect("DiagnosticType", ...), which
 * dereferences an OEMDiagnosticDataType optional that an OEM action route
 * never populates.
 *
 * @param[in] actionName    Fully-qualified action name, for the error body.
 * @param[in] parameterName Request parameter blamed for an InvalidArgument.
 *                          The dump manager raises one error name for every
 *                          rejected parameter, so the caller nominates the one
 *                          the two sides can legitimately disagree about.
 */
inline void mapCreateDumpDbusError(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    std::string_view dbusErrorName, const std::string& actionName,
    const std::string& parameterName)
{
    if (dbusErrorName == "xyz.openbmc_project.Dump.Create.Error.QuotaExceeded")
    {
        messages::createLimitReachedForResource(asyncResp->res);
        return;
    }
    if (dbusErrorName == "xyz.openbmc_project.Common.Error.Unavailable")
    {
        // This tree maps ResourceInUse to 503, which is what a collection
        // already in progress must read as.
        messages::resourceInUse(asyncResp->res);
        return;
    }
    if (dbusErrorName == "xyz.openbmc_project.Common.Error.InvalidArgument")
    {
        messages::actionParameterValueError(asyncResp->res, parameterName,
                                            actionName);
        return;
    }
    messages::internalError(asyncResp->res);
}

/**
 * @brief Attach the dump entry link to a task that completed successfully.
 *
 * Only called on success: a failed collection must leave the task monitor
 * without an entry link.
 */
inline void decorateCompletedDumpTask(
    const std::shared_ptr<task::TaskData>& taskData,
    const std::string& entryUri)
{
    if (taskData->payload)
    {
        taskData->payload->httpHeaders.emplace_back("Location: " + entryUri);
    }
    setDumpEntryTaskResponse(taskData, entryUri);
}

/**
 * @brief Task callback for the dump entry's Progress interface.
 *
 * Delegates the Status/Progress transition table to the shared helper and adds
 * only the dump-specific completion decoration.
 */
inline bool handleDumpProgress(
    const std::string& entryUri, boost::system::error_code ec,
    sdbusplus::message_t& msg, const std::shared_ptr<task::TaskData>& taskData)
{
    bool done = dbus_fd_utils::handleTaskMessage(ec, msg, taskData);
    if (done && taskData->state == "Completed")
    {
        decorateCompletedDumpTask(taskData, entryUri);
    }
    return done;
}

/**
 * @brief Create the Redfish task that tracks a dump entry to completion.
 *
 * Unlike createDumpTaskCallback() in log_services.hpp this does no Introspect
 * round-trip before installing the PropertiesChanged match. That probe exists
 * in the legacy path to discover whether the entry implements Common.Progress
 * -- the dump manager's System entries always do -- and it is what makes the
 * legacy path lossy: the match is only constructed inside startTimer(), which
 * runs in the Introspect completion handler, so an entry that reaches
 * Completed inside that window emits its signal with no subscriber and the
 * task then sits Running until the timer aborts it. Installing the match
 * synchronously from the CreateDump reply closes that window.
 */
inline void installDumpTask(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            task::Payload&& payload,
                            const sdbusplus::message::object_path& objPath)
{
    const std::string dumpId = objPath.filename();
    if (dumpId.empty())
    {
        BMCWEB_LOG_ERROR("CreateDump returned an unusable entry path: {}",
                         objPath.str);
        messages::internalError(asyncResp->res);
        return;
    }

    std::string entryUri =
        boost::urls::format(
            "/redfish/v1/Systems/{}/LogServices/Dump/Entries/{}",
            BMCWEB_REDFISH_SYSTEM_URI_NAME, dumpId)
            .buffer();

    std::string match = sdbusplus::bus::match::rules::propertiesChanged(
        objPath.str, progressInterface);

    // Copy out of the registry deque: createTask returns a reference into it
    // and the deque evicts entries once maxTaskCount is reached.
    std::shared_ptr<task::TaskData> task = task::TaskData::createTask(
        std::bind_front(handleDumpProgress, entryUri), match);

    // payload must be set before populateResp(), which reads it for
    // HidePayload and the Payload object.
    task->payload.emplace(std::move(payload));
    // The shared progress helper re-extends this by 5 minutes on every tick,
    // so this is an outer bound on a collection that stops reporting rather
    // than the expected duration. It matches the dump manager's own entry
    // progress cap, so neither side gives up before the other.
    task->startTimer(std::chrono::minutes(45));
    task->populateResp(asyncResp->res);
}

inline void afterCreateSystemDump(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, task::Payload payload,
    const std::string& actionName, const std::string& parameterName,
    const boost::system::error_code& ec, const sdbusplus::message_t& msg,
    const sdbusplus::message::object_path& objPath)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("{} CreateDump failed: {}", actionName, ec.message());
        const sd_bus_error* dbusError = msg.get_error();
        if (dbusError == nullptr || dbusError->name == nullptr)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        BMCWEB_LOG_ERROR("{} CreateDump D-Bus error: {} - {}", actionName,
                         dbusError->name, dbusError->message);
        mapCreateDumpDbusError(asyncResp, dbusError->name, actionName,
                               parameterName);
        return;
    }

    installDumpTask(asyncResp, std::move(payload), objPath);
}

/**
 * @brief Call Dump.Create.CreateDump on the System dump manager and hand the
 *        resulting entry to a Redfish task.
 *
 * @param[in] params        AdditionalData for the dump, consumed as published.
 * @param[in] actionName    Fully-qualified action name, for error bodies.
 * @param[in] parameterName Request parameter blamed for an InvalidArgument.
 */
inline void createSystemDump(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, task::Payload payload,
    const DumpCreateParams& params, std::string actionName,
    std::string parameterName)
{
    // The handler must be a lambda with concrete parameter types, not a
    // std::bind_front: sdbusplus::asio::connection unpacks the reply using
    // boost::callable_traits::args_t, which cannot expand the templated
    // operator() of a bind expression.
    dbus::utility::async_method_call(
        asyncResp,
        [asyncResp, payload = std::move(payload),
         actionName = std::move(actionName),
         parameterName = std::move(parameterName)](
            const boost::system::error_code& ec,
            const sdbusplus::message_t& msg,
            const sdbusplus::message::object_path& objPath) mutable {
            afterCreateSystemDump(asyncResp, std::move(payload), actionName,
                                  parameterName, ec, msg, objPath);
        },
        dumpManagerService, systemDumpObjectPath, dumpCreateInterface,
        "CreateDump", params);
}

} // namespace nvidia_dump_utils
} // namespace redfish
