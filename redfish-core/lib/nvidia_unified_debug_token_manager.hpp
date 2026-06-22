/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
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

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_singleton.hpp"
#include "dbus_utility.hpp"
#include "debug_token/erase_policy.hpp"
#include "debug_token/task_utils.hpp"
#include "debug_token/unified/action.hpp"
#include "debug_token/unified/utils.hpp"
#include "error_messages.hpp"
#include "multipart_parser.hpp"
#include "nvidia_messages.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "task.hpp"
#include "utils/chassis_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/memfd_utils.hpp"
#include "utils/nvidia_async_call_utils.hpp"

#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace redfish
{

namespace debug_token_manager_util
{

constexpr std::string_view debugTokenAggregationInterface =
    "com.nvidia.DebugToken.Aggregation";

/**
 * @brief Finds the parent chassis for a component using the inventory_chassis
 * endpoint.
 *
 * This function uses the forward association endpoint (inventory_chassis)
 * to locate the parent chassis that owns the specified component.
 *
 * @param[in] asyncResp Shared pointer to the AsyncResp object for sending
 * asynchronous responses.
 * @param[in] inventoryPath Object path of the component in the inventory.
 * @param[in] componentID The ID of the component whose parent chassis is to be
 * found.
 *
 * @return void Returns nothing. The result (if found) is added to asyncResp's
 * JSON response.
 */
inline void findParentChassisForComponent(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& inventoryPath, const std::string& componentID)
{
    std::string inventoryChassisPath = inventoryPath + "/inventory_chassis";

    chassis_utils::getAssociationEndpoint(
        inventoryChassisPath,
        [asyncResp, componentID,
         inventoryPath](const bool& status, const std::string& chassisPath) {
            if (!status)
            {
                BMCWEB_LOG_DEBUG(
                    "No inventory_chassis found for inventory object: {}",
                    inventoryPath);
                return;
            }

            std::string chassisId =
                std::filesystem::path(chassisPath).filename().string();

            if (!chassisId.empty())
            {
                nlohmann::json tcLink;
                tcLink["@odata.id"] = boost::urls::format(
                    "/redfish/v1/Chassis/{}/TrustedComponents/{}", chassisId,
                    componentID);
                asyncResp->res.jsonValue["Links"]["TrustedComponents"]
                    .push_back(std::move(tcLink));
            }
        });
}

/**
 * @brief Handle associations property response for SPDM component processing
 *
 * @param[in] asyncResp Shared pointer to the AsyncResp object.
 * @param[in] spdmPath Object path of the SPDM component.
 * @param[in] ec Error code from D-Bus operation.
 * @param[in] associations The Associations property value.
 */
inline void handleSpdmAssociationsResponse(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& spdmPath, const boost::system::error_code& ec,
    const dbus::utility::AssociationsType& associations)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("Failed to get Associations property for {}: {}",
                         spdmPath, ec.message());
        return;
    }

    for (const auto& [forward, reverse, target] : associations)
    {
        if (forward == "inventory_object")
        {
            std::string inventoryPath = target;
            std::string componentID =
                std::filesystem::path(inventoryPath).filename().string();

            if (componentID.starts_with(PLATFORMDEVICEPREFIX))
            {
                componentID = componentID.substr(
                    std::string(PLATFORMDEVICEPREFIX).size());
            }

            if (componentID.empty())
            {
                BMCWEB_LOG_DEBUG("Empty component ID from inventory path: {}",
                                 inventoryPath);
                return;
            }

            findParentChassisForComponent(asyncResp, inventoryPath,
                                          componentID);
            return;
        }
    }

    BMCWEB_LOG_DEBUG("No inventory_object association found for {}", spdmPath);
}

/**
 * @brief Populates TrustedComponents links for DebugTokenManagement.
 *
 * This function enumerates all SPDM Responder objects and maps them to their
 * parent chassis using D-Bus associations, then adds corresponding links to the
 * Redfish response.
 *
 * @param[in] asyncResp Shared pointer to the Redfish response.
 */
inline void populateTrustedComponentsLinks(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    nlohmann::json& trustedComponentsArray =
        asyncResp->res.jsonValue["Links"]["TrustedComponents"];
    trustedComponentsArray = nlohmann::json::array();

    const std::array<std::string_view, 1> spdmInterfaces = {
        "xyz.openbmc_project.SPDM.Responder"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project", 0, spdmInterfaces,
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("No SPDM components found");
                return;
            }

            for (const auto& [spdmPath, services] : subtree)
            {
                if (!services.empty())
                {
                    const std::string& service = services[0].first;
                    dbus::utility::getProperty<dbus::utility::AssociationsType>(
                        service, spdmPath,
                        "xyz.openbmc_project.Association.Definitions",
                        "Associations",
                        std::bind_front(&handleSpdmAssociationsResponse,
                                        asyncResp, spdmPath));
                }
            }
        });
}

/**
 * @brief Populate aggregated token management properties and actions
 *
 * Adds InstallTokenPushURI property and EraseToken action with ActionInfo link
 * for aggregated debug token operations.
 *
 * @param[in] asyncResp Shared pointer to the Redfish response
 * @param[in] managerId ID of the manager object
 */
inline void updateAggregateAPIProps(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    asyncResp->res.jsonValue["InstallTokenPushURI"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/DebugTokenManagement/install-token",
        managerId);
    nlohmann::json& eraseTokenAction =
        asyncResp->res
            .jsonValue["Actions"]["#NvidiaDebugTokenManagement.EraseToken"];
    eraseTokenAction["target"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/DebugTokenManagement/Actions/NvidiaDebugTokenManagement.EraseToken",
        managerId);
    eraseTokenAction["@Redfish.ActionInfo"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/DebugTokenManagement/EraseTokenActionInfo",
        managerId);
}

/**
 * @brief Structure to track aggregated erase token results using task
 * infrastructure.
 *
 * This class manages the progress and results of an aggregated erase token
 * operation across multiple devices. It tracks the total number of devices,
 * completed devices, and failed devices, and updates the
 * task status accordingly.
 *
 * @param[in] taskData Shared pointer to the task data structure.
 */
struct EraseTokenAggregator
{
    std::shared_ptr<task::TaskData> task;
    size_t totalDevices = 0;
    size_t completedDevices = 0;
    size_t failedDevices = 0;

    /**
     * @brief Constructor for EraseTokenAggregator.
     *
     * @param[in] taskData Shared pointer to the task data structure.
     */
    explicit EraseTokenAggregator(std::shared_ptr<task::TaskData> taskData) :
        task(std::move(taskData))
    {}

    /**
     * @brief Checks if erase token operation for all devices is complete,
     * updates task state accordingly.
     */
    void checkCompletion()
    {
        if (completedDevices < totalDevices)
        {
            return;
        }
        if (failedDevices > 0)
        {
            task->state = "Exception";
            task->messages.emplace_back(
                messages::resourceErrorsDetectedFormatError(
                    "NvidiaDebugTokenManagement.EraseToken",
                    "Token operation failed for one or more devices"));
        }
        else
        {
            task->state = "Completed";
        }

        task->percentComplete = 100;
        debug_token::finishTask(task);
    }

    /**
     * @brief Adds result of an individual device erase operation.
     *
     * @param[in] deviceId   The device identifier.
     * @param[in] success    Operation success flag.
     * @param[in] errorMsg   Optional error message.
     */
    void addResult(const std::string& deviceId, bool success,
                   const std::string& errorMsg = "")
    {
        completedDevices++;
        if (success)
        {
            BMCWEB_LOG_DEBUG("Erase succeeded for {}", deviceId);
            task->messages.emplace_back(
                messages::debugTokenEraseSuccess(deviceId));
        }
        else
        {
            failedDevices++;
            BMCWEB_LOG_ERROR("Erase failed for {}: {}", deviceId, errorMsg);
            task->messages.emplace_back(
                messages::debugTokenEraseFailed(deviceId, errorMsg));
        }

        if (totalDevices > 0)
        {
            task->percentComplete =
                static_cast<int>((completedDevices * 100) / totalDevices);
        }

        checkCompletion();
    }
};

/**
 * @brief Callback for individual device erase operations
 *
 * @param[in] aggregator Shared pointer to EraseTokenAggregator.
 * @param[in] deviceId Device identifier.
 * @param[in] result Operation result.
 */
inline void deviceEraseCallback(
    const std::shared_ptr<EraseTokenAggregator>& aggregator,
    const std::string& deviceId,
    const debug_token::unified::action::Result& result)
{
    if (std::holds_alternative<debug_token::NsmResult>(result))
    {
        const auto& [code, message] = std::get<debug_token::NsmResult>(result);
        if (code == debug_token::debugTokenSuccessNsmErrorCode)
        {
            aggregator->addResult(deviceId, true);
        }
        else if (code == debug_token::debugTokenNotInstalledNsmErrorCode)
        {
            aggregator->completedDevices++;
            aggregator->task->messages.emplace_back(
                messages::debugTokenNotInstalled(deviceId));
            if (aggregator->totalDevices > 0)
            {
                aggregator->task->percentComplete = static_cast<int>(
                    (aggregator->completedDevices * 100) /
                    aggregator->totalDevices);
            }
            aggregator->checkCompletion();
        }
        else if (code == debug_token::debugTokenUnsupportedNsmErrorCode)
        {
            aggregator->addResult(deviceId, false, "Debug token unsupported");
        }
        else
        {
            aggregator->addResult(deviceId, false, message);
        }
    }
    else
    {
        aggregator->addResult(deviceId, false, "Unexpected result type");
    }
}

/**
 * @brief Erase token for a single NSM device
 *
 * @param[in] aggregator Shared pointer to EraseTokenAggregator.
 * @param[in] chassisId Chassis identifier for the NSM device.
 * @param[in] eraseType Erase type enum string.
 * @param[in] tokenType Token type enum string.
 */
inline void eraseTokenForDevice(
    const std::shared_ptr<EraseTokenAggregator>& aggregator,
    const std::string& chassisId, const std::string& eraseType,
    const std::string& tokenType)
{
    debug_token::unified::action::Handler::startOperation(
        chassisId, debug_token::unified::action::Operation::EraseToken,
        std::make_pair(eraseType, tokenType),
        std::bind_front(&deviceEraseCallback, aggregator, chassisId));
}

/**
 * @brief Handle subtree response for aggregated erase token operation
 *
 * @param[in] aggregator Shared pointer to EraseTokenAggregator.
 * @param[in] eraseType Erase type enum string.
 * @param[in] tokenType Token type enum string.
 * @param[in] ec Error code from D-Bus operation.
 * @param[in] subtree Response from getSubTree containing debug token objects.
 */
inline void handleEraseTokenSubtreeResponse(
    const std::shared_ptr<EraseTokenAggregator>& aggregator,
    const std::string& eraseType, const std::string& tokenType,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Failed to get debug token objects: {}", ec);
        aggregator->task->state = "Exception";
        aggregator->task->percentComplete = 100;
        aggregator->task->messages.emplace_back(messages::internalError());
        debug_token::finishTask(aggregator->task);
        return;
    }

    if (subtree.empty())
    {
        BMCWEB_LOG_ERROR("No NSM debug token objects found");
        aggregator->task->state = "Completed";
        aggregator->task->percentComplete = 100;
        aggregator->task->messages.emplace_back(messages::success());
        debug_token::finishTask(aggregator->task);
        return;
    }

    aggregator->totalDevices = subtree.size();
    for (const auto& [objectPath, serviceMap] : subtree)
    {
        std::string chassisId =
            std::filesystem::path(objectPath).filename().string();
        if (chassisId.empty())
        {
            BMCWEB_LOG_WARNING("Empty chassis ID for path: {}", objectPath);
            aggregator->addResult(objectPath, false, "Invalid chassis ID");
            continue;
        }

        eraseTokenForDevice(aggregator, chassisId, eraseType, tokenType);
    }
}

/**
 * @brief Initiate aggregated erase token operation across all NSM devices
 *
 * @param[in] task Shared pointer to Redfish TaskData.
 * @param[in] eraseType D-Bus erase type enum string (fully qualified).
 * @param[in] tokenType D-Bus token type enum string (fully qualified).
 */
inline void startAggregatedEraseToken(
    const std::shared_ptr<task::TaskData>& task, const std::string& eraseType,
    const std::string& tokenType)
{
    auto aggregator = std::make_shared<EraseTokenAggregator>(task);

    constexpr std::array<std::string_view, 1> debugTokenInterfaces = {
        debug_token::debugTokenActionIntf};

    dbus::utility::getSubTree(
        std::string(debug_token::debugTokenBasePath), 0, debugTokenInterfaces,
        std::bind_front(&handleEraseTokenSubtreeResponse, aggregator, eraseType,
                        tokenType));
}

/**
 * @brief Handler for EraseToken action
 *
 * @param[in] app Reference to the Redfish application.
 * @param[in] req The HTTP request object.
 * @param[in] asyncResp Shared pointer to AsyncResp for sending a response.
 * @param[in] managerId The manager identifier from the route.
 */
inline void debugTokenManagementEraseTokenHandler(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "#Manager.v1_15_0.Manager",
                                   managerId);
        return;
    }

    std::string eraseType;
    std::optional<std::string> tokenType;

    if (!json_util::readJsonAction(req, asyncResp->res, "EraseType", eraseType,
                                   "TokenType", tokenType))
    {
        return;
    }

    std::optional<std::string> eraseTypeOpt =
        debug_token::unified::mapEraseTypeToDbusEnum(eraseType);
    if (!eraseTypeOpt.has_value())
    {
        BMCWEB_LOG_ERROR("Failed to map erase type: {}", eraseType);
        messages::actionParameterValueNotInList(
            asyncResp->res, eraseType, "EraseType",
            "NvidiaDebugTokenManagement.EraseToken");
        return;
    }

    std::string tokenTypeEnum = "com.nvidia.DebugToken.Common.Types.None";
    if (eraseType == "TokenType")
    {
        if (!tokenType.has_value())
        {
            messages::actionParameterMissing(
                asyncResp->res, "NvidiaDebugTokenManagement.EraseToken",
                "TokenType");
            return;
        }

        std::optional<std::string> tokenTypeOpt =
            debug_token::unified::mapTokenTypeToDbusEnum(*tokenType);
        if (!tokenTypeOpt.has_value())
        {
            BMCWEB_LOG_ERROR("Invalid token type: {}", *tokenType);
            messages::actionParameterValueNotInList(
                asyncResp->res, *tokenType, "TokenType",
                "NvidiaDebugTokenManagement.EraseToken");
            return;
        }
        tokenTypeEnum = tokenTypeOpt.value();
    }

    constexpr uint32_t debugTokenTaskTimeoutSec = 300;
    std::shared_ptr<task::TaskData> task =
        debug_token::createTask(req, asyncResp, debugTokenTaskTimeoutSec);

    startAggregatedEraseToken(task, eraseTypeOpt.value(), tokenTypeEnum);
}

/**
 * @brief Aggregator structure for managing install token task
 *
 * Tracks the progress of aggregated install token operation and updates
 * the Redfish task with per-device results. Unlike erase token which makes
 * per-device calls, install token uses the NSM aggregation interface which
 * handles all devices in a single call and returns aggregated results as
 * array[struct[uint16, string, string]] via the async operation value.
 */
struct InstallTokenAggregator
{
    std::shared_ptr<task::TaskData> task;
    std::string asyncOpPath;
    std::unique_ptr<sdbusplus::bus::match_t> propertiesChangedMatch;
    bool completed{false};

    /**
     * @brief Constructor for InstallTokenAggregator.
     *
     * @param[in] taskData Shared pointer to the task data structure.
     */
    explicit InstallTokenAggregator(std::shared_ptr<task::TaskData> taskData) :
        task(std::move(taskData))
    {
        task->state = "Running";
        task->percentComplete = 0;
    }

    /**
     * @brief Process asynchronous install token results and update task
     * state/messages.
     *
     * @param[in] results Vector of tuples containing error_code, message,
     * device_name per device.
     */
    void processAsyncResults(
        const std::vector<std::tuple<uint16_t, std::string, std::string>>&
            results)
    {
        if (completed)
        {
            return;
        }
        completed = true;
        size_t failedDevices = 0;

        for (const auto& [errorCode, errorMessage, deviceName] : results)
        {
            if (errorCode == 0)
            {
                BMCWEB_LOG_DEBUG("Install succeeded for {}", deviceName);
                task->messages.emplace_back(
                    messages::debugTokenInstallationSuccess(deviceName));
            }
            else if (errorCode ==
                     debug_token::debugTokenAlreadyInstalledNsmErrorCode)
            {
                BMCWEB_LOG_DEBUG("Token already installed on {}", deviceName);
                task->messages.emplace_back(
                    messages::debugTokenAlreadyInstalled(deviceName));
            }
            else
            {
                failedDevices++;
                BMCWEB_LOG_ERROR("Install failed for {}: {} (code: {})",
                                 deviceName, errorMessage, errorCode);
                task->messages.emplace_back(
                    messages::debugTokenInstallationFailed(deviceName,
                                                           errorMessage));
            }
        }
        if (failedDevices > 0)
        {
            task->state = "Exception";
        }
        else
        {
            task->state = "Completed";
        }

        task->percentComplete = 100;
        debug_token::finishTask(task);
    }

    /**
     * @brief Handle async value property response
     *
     * @param[in] self Shared pointer to this InstallTokenAggregator.
     * @param[in] ec Error code from D-Bus operation.
     * @param[in] value The async operation Value property.
     */
    static void handleAsyncValueResponse(
        const std::shared_ptr<InstallTokenAggregator>& self,
        const boost::system::error_code& ec,
        const std::variant<
            std::vector<std::tuple<uint16_t, std::string, std::string>>>& value)
    {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to get async operation value: {}",
                             ec.message());
            self->task->state = "Exception";
            self->task->messages.emplace_back(messages::internalError());
            self->task->percentComplete = 100;
            debug_token::finishTask(self->task);
            self->completed = true;
            return;
        }

        using ResultsType =
            std::vector<std::tuple<uint16_t, std::string, std::string>>;

        const ResultsType* results = std::get_if<ResultsType>(&value);

        if ((results != nullptr) && !results->empty())
        {
            self->processAsyncResults(*results);
        }
        else
        {
            BMCWEB_LOG_ERROR("Failed to parse async operation results{}",
                             (results != nullptr) ? " (empty array)" : "");
            self->task->state = "Exception";
            self->task->messages.emplace_back(messages::internalError());
            self->task->percentComplete = 100;
            debug_token::finishTask(self->task);
            self->completed = true;
        }
        if (self->propertiesChangedMatch)
        {
            self->propertiesChangedMatch.reset();
        }
    }

    /**
     * @brief Handles async operation status property updates.
     *
     * If completed, fetches final results and updates the task.
     *
     * @param[in] service The D-Bus service.
     * @param[in] objectPath The async object path.
     * @param[in] status The current status string.
     * @param[in] self Shared pointer to this InstallTokenAggregator.
     */
    void handleStatusUpdate(
        const std::string& service, const std::string& objectPath,
        const std::string& status,
        const std::shared_ptr<InstallTokenAggregator>& self) const
    {
        if (completed)
        {
            return;
        }

        BMCWEB_LOG_DEBUG("Async operation status: {}", status);
        if (status.find("InProgress") != std::string::npos)
        {
            return;
        }

        using ValueType = std::variant<
            std::vector<std::tuple<uint16_t, std::string, std::string>>>;

        dbus::utility::getProperty<ValueType>(
            service, objectPath, "com.nvidia.Async.Value", "Value",
            std::bind_front(&InstallTokenAggregator::handleAsyncValueResponse,
                            self));
    }

    /**
     * @brief Handle properties changed signal for async status updates
     *
     * @param[in] self Shared pointer to this InstallTokenAggregator.
     * @param[in] service The D-Bus service.
     * @param[in] objectPath The async operation object path.
     * @param[in,out] msg D-Bus message containing property changes.
     */
    static void handlePropertiesChanged(
        const std::shared_ptr<InstallTokenAggregator>& self,
        const std::string& service, const std::string& objectPath,
        sdbusplus::message_t& msg)
    {
        if (self->completed)
        {
            return;
        }

        std::string interface;
        std::map<std::string, std::variant<std::string>> changedProperties;
        msg.read(interface, changedProperties);

        auto statusIt = changedProperties.find("Status");
        if (statusIt == changedProperties.end())
        {
            return;
        }

        const std::string* status = std::get_if<std::string>(&statusIt->second);
        if (status == nullptr)
        {
            return;
        }

        self->handleStatusUpdate(service, objectPath, *status, self);
    }

    /**
     * @brief Handle initial status property response
     *
     * @param[in] self Shared pointer to this InstallTokenAggregator.
     * @param[in] service The D-Bus service.
     * @param[in] objectPath The async operation object path.
     * @param[in] ec Error code from D-Bus operation.
     * @param[in] status The current status string.
     */
    static void handleInitialStatusResponse(
        const std::shared_ptr<InstallTokenAggregator>& self,
        const std::string& service, const std::string& objectPath,
        const boost::system::error_code& ec, const std::string& status)
    {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to get initial async operation status: {}",
                             ec.message());
            self->task->state = "Exception";
            self->task->messages.emplace_back(messages::internalError());
            self->task->percentComplete = 100;
            debug_token::finishTask(self->task);
            self->completed = true;
            return;
        }

        BMCWEB_LOG_DEBUG("Initial async operation status: {}", status);
        self->handleStatusUpdate(service, objectPath, status, self);
    }

    /**
     * @brief Monitor async operation status property, set up callback for
     * completion.
     *
     * Sets up a property changed match for monitoring Status and fetches
     * current status.
     *
     * @param[in] service The D-Bus service.
     * @param[in] objectPath The async operation object path.
     * @param[in] self Shared pointer to this InstallTokenAggregator.
     */
    void monitorAsyncOperation(
        const std::string& service, const std::string& objectPath,
        const std::shared_ptr<InstallTokenAggregator>& self)
    {
        asyncOpPath = objectPath;
        propertiesChangedMatch = std::make_unique<sdbusplus::bus::match_t>(
            *crow::connections::systemBus,
            sdbusplus::bus::match::rules::propertiesChanged(
                objectPath, "com.nvidia.Async.Status"),
            std::bind_front(&InstallTokenAggregator::handlePropertiesChanged,
                            self, service, objectPath));

        // Check the current status in case operation already completed
        // This prevents race condition where status changed before we set up
        // the match
        dbus::utility::getProperty<std::string>(
            service, objectPath, "com.nvidia.Async.Status", "Status",
            std::bind_front(
                &InstallTokenAggregator::handleInitialStatusResponse, self,
                service, objectPath));
    }
};

/**
 * @brief Validate TokenFile exists in multipart form data
 *
 * @param[in] asyncResp Shared pointer to Redfish async response.
 * @param[in] parser MultipartParser object holding parsed form parts.
 *
 * @return True if TokenFile exists, false otherwise (and responds with error).
 */
inline std::optional<std::string> extractTokenFile(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::span<FormPart> multipart)
{
    for (const FormPart& formpart : multipart)
    {
        boost::beast::http::fields::const_iterator it =
            formpart.fields.find("Content-Disposition");
        if (it == formpart.fields.end())
        {
            BMCWEB_LOG_DEBUG("Couldn't find Content-Disposition");
            continue;
        }

        std::string contentDisposition(it->value());
        if (contentDisposition.find("name=\"TokenFile\"") != std::string::npos)
        {
            return formpart.content;
        }
    }

    BMCWEB_LOG_ERROR("TokenFile form part is missing");
    messages::propertyMissing(asyncResp->res, "TokenFile");
    return std::nullopt;
}

/**
 * @brief Handle subtree response for aggregated install token operation
 *
 * @param[in] aggregator Shared pointer to InstallTokenAggregator.
 * @param[in] memFd Shared pointer to MemoryFD holding the token file.
 * @param[in] ec Error code from D-Bus operation.
 * @param[in] subtree Response from getSubTree.
 */
inline void handleInstallTokenSubtreeResponse(
    const std::shared_ptr<InstallTokenAggregator>& aggregator,
    const std::shared_ptr<MemoryFD>& memFd, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec || subtree.empty())
    {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "Failed to find DebugToken.Aggregation interface: {}",
                ec.message());
        }
        else
        {
            BMCWEB_LOG_ERROR(
                "No objects implementing DebugToken.Aggregation found");
        }
        aggregator->task->messages.emplace_back(messages::internalError());
        aggregator->task->state = "Exception";
        aggregator->task->percentComplete = 100;
        debug_token::finishTask(aggregator->task);
        return;
    }

    const std::string& objectPath = subtree[0].first;
    const std::string& service = subtree[0].second[0].first;

    BMCWEB_LOG_DEBUG("Calling InstallToken on {} (service: {})", objectPath,
                     service);

    dbus::utility::async_method_call(
        [aggregator, service](const boost::system::error_code& ec2,
                              const sdbusplus::object_path& asyncOpPath) {
            if (ec2)
            {
                BMCWEB_LOG_ERROR(
                    "InstallToken D-Bus call failed: {} (error code: {})",
                    ec2.message(), ec2.value());
                aggregator->task->state = "Exception";
                if (ec2.value() == boost::system::errc::io_error ||
                    ec2.value() == boost::system::errc::invalid_argument)
                {
                    aggregator->task->messages.emplace_back(
                        messages::resourceErrorsDetectedFormatError(
                            "NvidiaDebugTokenManagement.InstallToken",
                            "Invalid Token File"));
                }
                else
                {
                    aggregator->task->messages.emplace_back(
                        messages::internalError());
                }

                aggregator->task->percentComplete = 100;
                debug_token::finishTask(aggregator->task);
                return;
            }

            BMCWEB_LOG_DEBUG("InstallToken async operation path: {}",
                             asyncOpPath.str);
            aggregator->monitorAsyncOperation(service, asyncOpPath.str,
                                              aggregator);
        },
        service, objectPath, std::string(debugTokenAggregationInterface),
        "InstallToken", sdbusplus::message::unix_fd(memFd->fd));
}

/**
 * @brief Start aggregated install token operation
 *
 * Calls the NSM aggregation InstallToken method, monitors for completion,
 * and updates the Redfish task with per-device results.
 *
 * @param[in] task Shared pointer to Redfish TaskData.
 * @param[in] memFd Shared pointer to MemoryFD holding the token file.
 */
inline void startAggregatedInstallToken(
    const std::shared_ptr<task::TaskData>& task, std::string_view tokenFile)
{
    std::vector<uint8_t> fileData(tokenFile.begin(), tokenFile.end());
    auto memFd = std::make_shared<MemoryFD>();
    memFd->write(fileData);
    memFd->rewind();
    auto aggregator = std::make_shared<InstallTokenAggregator>(task);
    constexpr std::array<std::string_view, 1> interfaces = {
        debugTokenAggregationInterface};

    dbus::utility::getSubTree(
        "/", 0, interfaces,
        std::bind_front(&handleInstallTokenSubtreeResponse, aggregator, memFd));
}

/**
 * @brief Handler for install-token POST endpoint
 *
 * @param[in] app Reference to the Redfish application.
 * @param[in] req The HTTP request object.
 * @param[in] asyncResp Shared pointer to AsyncResp for sending a response.
 * @param[in] managerId The route manager identifier.
 */
inline void debugTokenManagementInstallTokenHandler(
    App& app, crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "#Manager.v1_15_0.Manager",
                                   managerId);
        return;
    }
    std::optional<std::string> tokenFile =
        extractTokenFile(asyncResp, req.multipart());
    if (!tokenFile)
    {
        return;
    }

    // Check if the token file is empty
    if (tokenFile->empty())
    {
        return;
    }

    constexpr uint32_t debugTokenTaskTimeoutSec = 300;
    std::shared_ptr<task::TaskData> task =
        debug_token::createTask(req, asyncResp, debugTokenTaskTimeoutSec);

    if (!task)
    {
        BMCWEB_LOG_ERROR("Failed to create task");
        messages::internalError(asyncResp->res);
        return;
    }

    startAggregatedInstallToken(task, std::move(*tokenFile));
}

/**
 * @brief Handler for EraseTokenActionInfo endpoint
 *
 * @param[in] app Reference to the Redfish application.
 * @param[in] req The HTTP request object.
 * @param[in] asyncResp Shared pointer to AsyncResp for sending a response.
 * @param[in] managerId The route manager identifier.
 */
inline void debugTokenManagementEraseTokenActionInfoHandler(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "#Manager.v1_15_0.Manager",
                                   managerId);
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_2_0.ActionInfo";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/DebugTokenManagement/EraseTokenActionInfo",
        managerId);
    asyncResp->res.jsonValue["Id"] = "EraseTokenActionInfo";
    asyncResp->res.jsonValue["Name"] = "Erase Token Action Info";

    nlohmann::json::array_t parameters;

    nlohmann::json::object_t eraseTypeParam;
    eraseTypeParam["Name"] = "EraseType";
    eraseTypeParam["Required"] = true;
    eraseTypeParam["DataType"] = "String";
    eraseTypeParam["AllowableValues"] = nlohmann::json::array(
        {"EraseAll", "EraseAllAndRatchetCounterIncreased", "TokenType"});
    parameters.emplace_back(std::move(eraseTypeParam));

    nlohmann::json::object_t tokenTypeParam;
    tokenTypeParam["Name"] = "TokenType";
    tokenTypeParam["Required"] = false;
    tokenTypeParam["DataType"] = "String";
    auto allowableTypes = debug_token::unified::getAllowableTokenTypes();
    tokenTypeParam["AllowableValues"] =
        nlohmann::json::array_t(allowableTypes.begin(), allowableTypes.end());
    parameters.emplace_back(std::move(tokenTypeParam));

    asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
}

/**
 * @brief Register routes for DebugTokenManagement actions (new functionality)
 *
 * @param[in] app Reference to the Redfish application.
 */
inline void requestRoutesDebugTokenManagementActions(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/DebugTokenManagement/install-token")
        .privileges(redfish::privileges::postManager)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            debugTokenManagementInstallTokenHandler, std::ref(app)));
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/DebugTokenManagement/Actions/NvidiaDebugTokenManagement.EraseToken")
        .privileges(redfish::privileges::postManager)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            debugTokenManagementEraseTokenHandler, std::ref(app)));
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/DebugTokenManagement/EraseTokenActionInfo")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            debugTokenManagementEraseTokenActionInfoHandler, std::ref(app)));
}

} // namespace debug_token_manager_util
} // namespace redfish
