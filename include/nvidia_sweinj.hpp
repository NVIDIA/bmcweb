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

#include "app.hpp"
#include "privileges.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "task.hpp"
#include "utils/json_utils.hpp"
#include "utils/time_utils.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/beast/http/verb.hpp>

#include <functional>

/**
 * Timeout hours for complete injection: Injection without EvendId
 *
 * It has already been observed on some platforms it takes more than 24 hours
 */
constexpr int64_t timeoutHoursForFullInjection = 48;

/**
 * Timeout for a common action Setup;Injection;Cleanup
 */
constexpr int64_t timeoutMinutesForSimpleAction = 30;

namespace redfish
{
namespace nvidia
{
namespace sweinj
{

/**
 * @brief Calls @a CreateRequest() to create a service action request
 * @param app - The App
 */
void requestRoutesSwEinjAction(App& app);

/**
 * @brief Manages the Task return
 */
static bool createTaskCallback(boost::system::error_code ec,
                               sdbusplus::message_t& msg,
                               const std::shared_ptr<task::TaskData>& taskData)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("{}", ec.message());
        taskData->state = "Exception";
        auto msg = messages::taskAborted(ec.message());
        taskData->messages.push_back(msg);
        return task::completed;
    }
    std::string iface;
    dbus::utility::DBusPropertiesMap propertiesMap;
    const std::string* state = nullptr;
    const std::string* errorMmsg = nullptr;
    msg.read(iface, propertiesMap);
    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), propertiesMap, "State", state,
        "ErrorMessage", errorMmsg);
    bool completed = false;
    if (false == success)
    {
        BMCWEB_LOG_ERROR("Failed to read property from Dbus service");
        completed = true;
        auto json = messages::taskAborted("No properties");
        json["Message"] = "Failed to read property from Dbus service";
        taskData->messages.push_back(json);
    }
    else
    {
        if (errorMmsg != nullptr && false == errorMmsg->empty())
        {
            auto json = messages::taskAborted(*errorMmsg);
            json["Message"] = "Task finished with error";
            taskData->messages.push_back(json);
        }
        if (state != nullptr)
        {
            std::string index = std::to_string(taskData->index);
            taskData->state = *state;
            if (*state == "Completed")
            {
                completed = true;
                taskData->messages.emplace_back(
                    messages::taskCompletedOK(index));
            }
            else if (*state == "Exception")
            {
                BMCWEB_LOG_ERROR("*state == Exception");
                completed = true;
                taskData->messages.emplace_back(messages::taskAborted(index));
            }
        }
    }
    // OK or any error
    if (completed)
    {
        taskData->percentComplete = 100;
        taskData->finishTask();
        return task::completed;
    }
    // still running
    return !redfish::task::completed;
}

/**
 * @brief Used as callback from a Request action creation to the SWEINJ
 * service
 * @param req - The request Json
 * @param asyncResp - The response to the user
 * @param requestType - The RequestType (which request) used
 * @param ec - The system error if any
 * @param returnCode - zero (Errors::NoError) indicates success, others
 * errors
 */
static void createRequestCallback(
    task::Payload&& payload,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    bool fullSWEinjInjection, const boost::system::error_code& ec)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Task not created: {}", ec.message());
        auto jsonMsg = redfish::messages::taskAborted(ec.message());
        jsonMsg["Message"] = "Task not created";
        nlohmann::json::array_t messages{jsonMsg};
        asyncResp->res.jsonValue["Messages"] = std::move(messages);
        asyncResp->res.jsonValue["TaskState"] = "Cancelled";
        asyncResp->res.jsonValue["TaskStatus"] = "Critical";
        return;
    }
    // No error, create the task
    auto taskPtr = task::TaskData::createTask(
        std::bind_front(createTaskCallback),
        sdbusplus::bus::match::rules::propertiesChanged(
            "/com/nvidia/software/error/injection",
            "com.Nvidia.Software.Error.Injection.Request"));

    taskPtr->payload.emplace(std::move(payload));
    taskPtr->populateResp(asyncResp->res);
    if (fullSWEinjInjection)
    {
        taskPtr->startTimer(std::chrono::hours(timeoutHoursForFullInjection));
    }
    else
    {
        taskPtr->startTimer(
            std::chrono::minutes(timeoutMinutesForSimpleAction));
    }
}

/**
 * @brief Validates the Action parameters and creates the service request
 * @param req - The request Json
 * @param asyncResp - The response to the user
 * @param bmcId - The Bmcweb manager Id
 */
static void createRequest(App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& bmcId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        BMCWEB_LOG_ERROR("Failed to set up Redfish route.");
        return;
    }

    if (bmcId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        BMCWEB_LOG_ERROR("Invalid BMC ID '{}'", bmcId);
        messages::resourceNotFound(asyncResp->res, "Manager", bmcId);
        return;
    }

    std::string requestType{};
    std::optional<std::string> optionalErrorId{};
    std::optional<std::string> optionalStrDeviceIndex{};
    std::optional<bool> optionalClearData{};
    std::optional<bool> optionalCompleteCycle{};
    std::optional<bool> optionalKeepLogs{};
    std::optional<bool> optionalRecovery{};

    // clang-format off
    if (!redfish::json_util::readJsonAction(
            req, asyncResp->res,
            "RequestType", requestType,
            "ClearData", optionalClearData,
            "ErrorId", optionalErrorId,
            "DeviceIndex", optionalStrDeviceIndex,
            "KeepLogs", optionalKeepLogs,
            "Recovery", optionalRecovery,
            "CompleteCycle", optionalCompleteCycle
            ))
    {
        BMCWEB_LOG_ERROR("Failed to parse JSON body.");
        return;
    }
    // clang-format on

    // check for valid requestType
    if (requestType != "Setup" && requestType != "Injection" &&
        requestType != "Cleanup")
    {
        BMCWEB_LOG_ERROR("Invalid value for 'RequestType'");
        messages::propertyValueNotInList(asyncResp->res, requestType,
                                         "RequestType");
        return;
    }

    // fullSWEinjInjection indicates the all events present for Eventing/AML
    // in that platform will be inject by SW EINJ itself, on Vulcan it is
    // supposed  to last more than 24 hours
    bool fullSWEinjInjection = requestType == "Injection" &&
                               (false == optionalErrorId.has_value() ||
                                true == (*optionalErrorId).empty());

    task::Payload payload(req);
    auto createRequestHandler =
        [payload = std::move(payload), asyncResp,
         fullSWEinjInjection](const boost::system::error_code& ec) mutable {
            createRequestCallback(std::move(payload), asyncResp,
                                  fullSWEinjInjection, ec);
        };
    crow::connections::systemBus->async_method_call(
        std::move(createRequestHandler), "com.Nvidia.Software.Error.Injection",
        "/com/nvidia/software/error/injection",
        "com.Nvidia.Software.Error.Injection.Request", "CreateRequest",
        requestType, optionalErrorId.value_or(""),
        optionalStrDeviceIndex.value_or(""), optionalClearData.value_or(false),
        optionalCompleteCycle.value_or(false), optionalKeepLogs.value_or(false),
        optionalRecovery.value_or(false));
}

/**
 * SWErrorInjection options AllowableValues using NvidiaSWErrorInjection
 * schema.
 *
 * Called from BMCWEB_ROUTE()
 * @param app - The App
 * @param req - The request Json
 * @param asyncResp - The response to the user
 * @param bmcId - The Bmcweb manager Id
 */
static void handleManagersOemNvidiaSwErrorInjectionInfo(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& bmcId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        BMCWEB_LOG_ERROR("Failed to set up Redfish route.");
        return;
    }
    // Process non bmc service manager
    if (bmcId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        BMCWEB_LOG_ERROR("Invalid BMC ID '{}'", bmcId);
        messages::resourceNotFound(asyncResp->res, "Manager", bmcId);
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_1_2.ActionInfo";
    asyncResp->res.jsonValue["@odata.id"] =
        boost::urls::format("/redfish/v1/Managers/{}"
                            "/Oem/Nvidia/SWErrorInjectionActionInfo",
                            bmcId);
    asyncResp->res.jsonValue["Name"] = "SWErrorInjection Action Info";
    asyncResp->res.jsonValue["Id"] = "NvidiaSWErrorInjectionActionInfo";
    asyncResp->res.jsonValue["Parameters"] = {
        {{"Name", "RequestType"},
         {"Required", true},
         {"DataType", "String"},
         {"AllowableValues", {"Setup", "Injection", "Cleanup"}}},
        {{"Name", "ClearData"}, {"Required", false}, {"DataType", "Boolean"}},
        {{"Name", "ErrorId"}, {"Required", false}, {"DataType", "String"}},
        {{"Name", "DeviceIndex"}, {"Required", false}, {"DataType", "String"}},
        {{"Name", "KeepLogs"}, {"Required", false}, {"DataType", "Boolean"}},
        {{"Name", "Recovery"}, {"Required", false}, {"DataType", "Boolean"}},
        {{"Name", "CompleteCycle"},
         {"Required", false},
         {"DataType", "Boolean"}}};
}

void requestRoutesSwEinjAction(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Actions/Oem/NvidiaManager.SWErrorInjection/")
        .privileges(redfish::privileges::postManager)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(createRequest, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/SWErrorInjectionActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleManagersOemNvidiaSwErrorInjectionInfo, std::ref(app)));
}

} // namespace sweinj
} // namespace nvidia
} // namespace redfish
