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
#include "utils/dbus_fd_download_utils.hpp"
#include "utils/dbus_utils.hpp"

#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/exception.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <task.hpp>

namespace redfish
{

namespace nvidia_cpu_ist
{

constexpr std::string_view istServiceName = "com.nvidia.vera.ist";
constexpr std::string_view istObjectPath = "/com/nvidia/vera/ist";
constexpr std::string_view istControlInterface =
    "xyz.openbmc_project.ist.Control";
constexpr std::string_view istRunStateInterface =
    "com.nvidia.vera.ist.run.State";

struct IstTaskContext
{
    std::unique_ptr<sdbusplus::bus::match_t> stageMatch;
};

inline void onStageChange(const std::weak_ptr<task::TaskData>& weakTask,
                          sdbusplus::message_t& message)
{
    std::shared_ptr<task::TaskData> taskData = weakTask.lock();
    if (!taskData)
    {
        return;
    }

    std::string iface;
    dbus::utility::DBusPropertiesMap propertiesChanged;

    try
    {
        message.read(iface, propertiesChanged);
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        BMCWEB_LOG_ERROR("Failed to read stage change message: {}", e.what());
        return;
    }

    const std::string* stage = nullptr;
    if (!sdbusplus::unpackPropertiesNoThrow(
            redfish::dbus_utils::UnpackErrorPrinter(), propertiesChanged,
            "Stage", stage))
    {
        return;
    }

    if (stage == nullptr)
    {
        return;
    }

    BMCWEB_LOG_INFO("IST Stage: {}", *stage);

    if (*stage == "PendingPowerCycle")
    {
        taskData->state = "Suspended";
        // The IST service times out and aborts the run after 10 minutes,
        // so the task timer should never expire, but use 20 minutes as a
        // safety margin since no progress signals arrive until IST is running
        taskData->extendTimer(std::chrono::minutes(20));
    }
    else if (*stage == "RunningIST")
    {
        taskData->state = "Running";
        taskData->extendTimer(std::chrono::minutes(5));
    }
}

inline void onStartIstComplete(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    task::Payload&& payload, const boost::system::error_code& ec,
    const sdbusplus::message::object_path& runPath)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Failed to start IST: {}", ec);
        if (ec == boost::system::errc::device_or_resource_busy)
        {
            messages::resourceInUse(asyncResp->res);
        }
        else
        {
            messages::internalError(asyncResp->res);
        }
        return;
    }

    std::string runPathStr = runPath.str;
    if (runPathStr.empty())
    {
        BMCWEB_LOG_ERROR("StartIST returned empty run object path");
        messages::internalError(asyncResp->res);
        return;
    }
    BMCWEB_LOG_INFO("IST run object: {}", runPathStr);

    std::shared_ptr<IstTaskContext> ctx = std::make_shared<IstTaskContext>();

    // ctx is captured to tie stageMatch lifetime to the task
    std::shared_ptr<task::TaskData> taskData = task::TaskData::createTask(
        [ctx](boost::system::error_code ec2, sdbusplus::message_t& msg,
              const std::shared_ptr<task::TaskData>& td) {
            return dbus_fd_utils::handleTaskMessage(ec2, msg, td);
        },
        std::string(sdbusplus::bus::match::rules::propertiesChanged(
            runPathStr, "xyz.openbmc_project.Common.Progress")));

    taskData->payload.emplace(std::move(payload));
    taskData->populateResp(asyncResp->res);
    taskData->startTimer(std::chrono::minutes(5));

    std::weak_ptr<task::TaskData> weakTask = taskData;
    ctx->stageMatch = std::make_unique<sdbusplus::bus::match_t>(
        static_cast<sdbusplus::bus::bus&>(*crow::connections::systemBus),
        sdbusplus::bus::match::rules::propertiesChanged(
            runPathStr, std::string(istRunStateInterface)),
        std::bind_front(onStageChange, weakTask));
}

inline void handlePostStartIst(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    BMCWEB_LOG_DEBUG("IST: Handling StartIST request");

    std::optional<int32_t> istTimeoutSec;
    std::optional<std::string> continueOnFail;
    std::optional<bool> autoRebootOnComplete;
    std::optional<std::string> testList;
    std::optional<std::string> packageList;
    std::optional<std::string> saveResultsOnPass;
    std::optional<std::string> saveResultsOnFail;

    if (!json_util::readJsonAction(
            req, asyncResp->res, "ISTTimeoutSec", istTimeoutSec,
            "ContinueOnFail", continueOnFail, "AutoRebootOnComplete",
            autoRebootOnComplete, "TestList", testList, "PackageList",
            packageList, "SaveResultsOnPass", saveResultsOnPass,
            "SaveResultsOnFail", saveResultsOnFail))
    {
        return;
    }

    if (continueOnFail && *continueOnFail != "Enable" &&
        *continueOnFail != "Disable" && *continueOnFail != "Default")
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, *continueOnFail, "ContinueOnFail",
            "NvidiaComputerSystem.StartIST");
        return;
    }

    if (saveResultsOnPass && *saveResultsOnPass != "Enable" &&
        *saveResultsOnPass != "Disable" && *saveResultsOnPass != "Default")
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, *saveResultsOnPass, "SaveResultsOnPass",
            "NvidiaComputerSystem.StartIST");
        return;
    }

    if (saveResultsOnFail && *saveResultsOnFail != "Enable" &&
        *saveResultsOnFail != "Disable" && *saveResultsOnFail != "Default")
    {
        messages::actionParameterValueNotInList(
            asyncResp->res, *saveResultsOnFail, "SaveResultsOnFail",
            "NvidiaComputerSystem.StartIST");
        return;
    }

    task::Payload payload(req);

    dbus::utility::async_method_call(
        [asyncResp, payload = std::move(payload)](
            const boost::system::error_code& ec,
            const sdbusplus::message::object_path& runPath) mutable {
            onStartIstComplete(asyncResp, std::move(payload), ec, runPath);
        },
        std::string(istServiceName), std::string(istObjectPath),
        std::string(istControlInterface), "StartIST", istTimeoutSec.value_or(0),
        continueOnFail.value_or(std::string("Default")),
        autoRebootOnComplete.value_or(false), testList.value_or(std::string()),
        packageList.value_or(std::string()),
        saveResultsOnPass.value_or(std::string("Default")),
        saveResultsOnFail.value_or(std::string("Default")));
}

inline void handleGetIstActionInfo(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    nlohmann::json& jsonValue = asyncResp->res.jsonValue;
    jsonValue["@odata.type"] = "#ActionInfo.v1_2_0.ActionInfo";
    jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/StartISTActionInfo", systemName);
    jsonValue["Id"] = "StartISTActionInfo";
    jsonValue["Name"] = "Start IST Action Info";
    jsonValue["Description"] = "Parameters for starting In-System Test (IST)";
    nlohmann::json::array_t parameters;

    nlohmann::json::object_t testList;
    testList["Name"] = "TestList";
    testList["Required"] = false;
    testList["DataType"] = "String";
    parameters.emplace_back(std::move(testList));

    nlohmann::json::object_t packageList;
    packageList["Name"] = "PackageList";
    packageList["Required"] = false;
    packageList["DataType"] = "String";
    parameters.emplace_back(std::move(packageList));

    nlohmann::json::object_t timeout;
    timeout["Name"] = "ISTTimeoutSec";
    timeout["Required"] = false;
    timeout["DataType"] = "Number";
    parameters.emplace_back(std::move(timeout));

    nlohmann::json::object_t contOnFail;
    contOnFail["Name"] = "ContinueOnFail";
    contOnFail["Required"] = false;
    contOnFail["DataType"] = "String";
    contOnFail["AllowableValues"] =
        nlohmann::json::array_t({"Enable", "Disable", "Default"});
    parameters.emplace_back(std::move(contOnFail));

    nlohmann::json::object_t autoReboot;
    autoReboot["Name"] = "AutoRebootOnComplete";
    autoReboot["Required"] = false;
    autoReboot["DataType"] = "Boolean";
    parameters.emplace_back(std::move(autoReboot));

    nlohmann::json::object_t saveOnPass;
    saveOnPass["Name"] = "SaveResultsOnPass";
    saveOnPass["Required"] = false;
    saveOnPass["DataType"] = "String";
    saveOnPass["AllowableValues"] =
        nlohmann::json::array_t({"Enable", "Disable", "Default"});
    parameters.emplace_back(std::move(saveOnPass));

    nlohmann::json::object_t saveOnFail;
    saveOnFail["Name"] = "SaveResultsOnFail";
    saveOnFail["Required"] = false;
    saveOnFail["DataType"] = "String";
    saveOnFail["AllowableValues"] =
        nlohmann::json::array_t({"Enable", "Disable", "Default"});
    parameters.emplace_back(std::move(saveOnFail));

    jsonValue["Parameters"] = std::move(parameters);
}

} // namespace nvidia_cpu_ist

inline void requestRoutesIstActionInfo(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/Oem/Nvidia/StartISTActionInfo/")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            nvidia_cpu_ist::handleGetIstActionInfo, std::ref(app)));
}

inline void requestRoutesIst(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Actions/Oem/NvidiaComputerSystem.StartIST/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(nvidia_cpu_ist::handlePostStartIst, std::ref(app)));
}

} // namespace redfish
