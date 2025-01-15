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

#include "bmcweb_config.h"

#include "background_copy.hpp"
#include "commit_image.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include <utils/fw_utils.hpp>

#include <memory>

namespace redfish
{

/* holds compute digest operation state to allow one operation at a time */
static bool computeDigestInProgress = false;
const std::string hashComputeInterface = "com.Nvidia.ComputeHash";
constexpr auto retimerHashMaxTimeSec =
    180; // 2 mins for 2 attempts and 1 addional min as buffer

inline void
    extendUpdateServiceGet(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if constexpr (BMCWEB_REDFISH_POST_TO_OLD_UPDATESERVICE)
    {
        // See note about later on in this file about why this is neccesary
        // This is "Wrong" per the standard, but is done temporarily to
        // avoid noise in failing tests as people transition to having this
        // option disabled
        if (!asyncResp->res.getHeaderValue("Allow").empty())
        {
            asyncResp->res.clearHeader(boost::beast::http::field::allow);
        }
        asyncResp->res.addHeader(boost::beast::http::field::allow,
                                 "GET, PATCH, HEAD");
    }
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        asyncResp->res.jsonValue["Oem"]["Nvidia"] = {
            {"@odata.type", "#NvidiaUpdateService.v1_2_0.NvidiaUpdateService"},
            {"PersistentStorage",
             {{"@odata.id",
               "/redfish/v1/UpdateService/Oem/Nvidia/PersistentStorage"}}},
            {"MultipartHttpPushUriOptions",
             {{"UpdateOptionSupport", [&]() {
                   if constexpr (BMCWEB_NVIDIA_OEM_FW_UPDATE_STAGING)
                   {
                       return std::vector<std::string>{"StageAndActivate",
                                                       "StageOnly"};
                   }
                   else
                   {
                       return std::vector<std::string>{"StageAndActivate"};
                   }
               }()}}}};
        debug_token::getErasePolicy(asyncResp);
    }
}

/**
 * @brief update oem action with ComputeDigest for devices which supports hash
 * compute
 *
 * @param[in] asyncResp
 * @param[in] swId
 */
inline void updateOemActionComputeDigest(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& swId)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, swId](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
        if (ec)
        {
            // hash compute interface is not applicable, ignore for the
            // device
            return;
        }
        for (auto& obj : subtree)
        {
            sdbusplus::message::object_path hashPath(obj.first);
            std::string hashId = hashPath.filename();
            if (hashId == swId)
            {
                std::string computeDigestTarget =
                    "/redfish/v1/UpdateService/FirmwareInventory/" + swId +
                    "/Actions/Oem/NvidiaSoftwareInventory.ComputeDigest";
                asyncResp->res
                    .jsonValue["Actions"]["Oem"]
                              ["#NvidiaSoftwareInventory.ComputeDigest"] = {
                    {"target", computeDigestTarget}};
                break;
            }
        }
        return;
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/com/Nvidia/ComputeHash", static_cast<int32_t>(0),
        std::array<const char*, 1>{hashComputeInterface.c_str()});
}

/**
 * @brief compute digest method handler invoke retimer hash computation
 *
 * @param[in] req - http request
 * @param[in] asyncResp - http response
 * @param[in] hashComputeObjPath - hash object path
 * @param[in] swId - software id
 */
inline void computeDigest(const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& hashComputeObjPath,
                          const std::string& swId)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, req, hashComputeObjPath, swId](
            const boost::system::error_code ec,
            const std::vector<std::pair<std::string, std::vector<std::string>>>&
                objInfo) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Failed to GetObject for ComputeDigest: {}",
                                 ec);
                messages::internalError(asyncResp->res);
                return;
            }
            // Ensure we only got one service back
            if (objInfo.size() != 1)
            {
                BMCWEB_LOG_ERROR("Invalid Object Size {}", objInfo.size());
                messages::internalError(asyncResp->res);
                return;
            }
            const std::string hashComputeService = objInfo[0].first;
            unsigned retimerId;
            try
            {
                // TODO this needs moved to from_chars
                retimerId = static_cast<unsigned>(
                    std::stoul(swId.substr(swId.rfind("_") + 1)));
            }
            catch (const std::exception& e)
            {
                BMCWEB_LOG_ERROR("Error while parsing retimer Id: {}",
                                 e.what());
                messages::internalError(asyncResp->res);
                return;
            }
            // callback to reset hash compute state for timeout scenario
            auto timeoutCallback =
                [](const std::string_view state, size_t index) {
                    nlohmann::json message{};
                    if (state == "Started")
                    {
                        message = messages::taskStarted(std::to_string(index));
                    }
                    else if (state == "Aborted")
                    {
                        computeDigestInProgress = false;
                        message = messages::taskAborted(std::to_string(index));
                    }
                    return message;
                };
            // create a task to wait for the hash digest property changed signal
            std::shared_ptr<task::TaskData> task = task::TaskData::createTask(
                [hashComputeObjPath, hashComputeService](
                    boost::system::error_code ec,
                    sdbusplus::message::message& msg,
                    const std::shared_ptr<task::TaskData>& taskData) {
                    if (ec)
                    {
                        if (ec != boost::asio::error::operation_aborted)
                        {
                            taskData->state = "Aborted";
                            taskData->messages.emplace_back(
                                messages::resourceErrorsDetectedFormatError(
                                    "NvidiaSoftwareInventory.ComputeDigest",
                                    ec.message()));
                            taskData->finishTask();
                        }
                        computeDigestInProgress = false;
                        return task::completed;
                    }

                    std::string interface;
                    std::map<std::string, dbus::utility::DbusVariantType> props;

                    msg.read(interface, props);
                    if (interface == hashComputeInterface)
                    {
                        auto it = props.find("Digest");
                        if (it == props.end())
                        {
                            BMCWEB_LOG_ERROR(
                                "Signal doesn't have Digest value");
                            return !task::completed;
                        }
                        auto value = std::get_if<std::string>(&(it->second));
                        if (!value)
                        {
                            BMCWEB_LOG_ERROR("Digest value is not a string");
                            return !task::completed;
                        }

                        if (!(value->empty()))
                        {
                            std::string hashDigestValue = *value;
                            crow::connections::systemBus->async_method_call(
                                [taskData, hashDigestValue](
                                    const boost::system::error_code ec,
                                    const std::variant<std::string>& property) {
                                    if (ec)
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "DBUS response error for Algorithm");
                                        taskData->state = "Exception";
                                        taskData->messages.emplace_back(
                                            messages::taskAborted(
                                                std::to_string(
                                                    taskData->index)));
                                        return;
                                    }
                                    const std::string* hashAlgoValue =
                                        std::get_if<std::string>(&property);
                                    if (hashAlgoValue == nullptr)
                                    {
                                        BMCWEB_LOG_ERROR(
                                            "Null value returned for Algorithm");
                                        taskData->state = "Exception";
                                        taskData->messages.emplace_back(
                                            messages::taskAborted(
                                                std::to_string(
                                                    taskData->index)));
                                        return;
                                    }

                                    nlohmann::json jsonResponse;
                                    jsonResponse["FirmwareDigest"] =
                                        hashDigestValue;
                                    jsonResponse
                                        ["FirmwareDigestHashingAlgorithm"] =
                                            *hashAlgoValue;
                                    taskData->taskResponse.emplace(
                                        jsonResponse);
                                    std::string location =
                                        "Location: /redfish/v1/TaskService/Tasks/" +
                                        std::to_string(taskData->index) +
                                        "/Monitor";
                                    taskData->payload->httpHeaders.emplace_back(
                                        std::move(location));
                                    taskData->state = "Completed";
                                    taskData->percentComplete = 100;
                                    taskData->messages.emplace_back(
                                        messages::taskCompletedOK(
                                            std::to_string(taskData->index)));
                                    taskData->finishTask();
                                },
                                hashComputeService, hashComputeObjPath,
                                "org.freedesktop.DBus.Properties", "Get",
                                hashComputeInterface, "Algorithm");
                            computeDigestInProgress = false;
                            return task::completed;
                        }
                        else
                        {
                            BMCWEB_LOG_ERROR(
                                "GetHash failed. Digest is empty.");
                            taskData->state = "Exception";
                            taskData->messages.emplace_back(
                                messages::resourceErrorsDetectedFormatError(
                                    "NvidiaSoftwareInventory.ComputeDigest",
                                    "Hash Computation Failed"));
                            taskData->finishTask();
                            computeDigestInProgress = false;
                            return task::completed;
                        }
                    }
                    return !task::completed;
                },
                "type='signal',member='PropertiesChanged',"
                "interface='org.freedesktop.DBus.Properties',"
                "path='" +
                    hashComputeObjPath + "',",
                timeoutCallback);
            task->startTimer(std::chrono::seconds(retimerHashMaxTimeSec));
            task->populateResp(asyncResp->res);
            task->payload.emplace(req);
            computeDigestInProgress = true;
            crow::connections::systemBus->async_method_call(
                [task](const boost::system::error_code ec) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("Failed to ComputeDigest: {}", ec);
                        task->state = "Aborted";
                        task->messages.emplace_back(
                            messages::resourceErrorsDetectedFormatError(
                                "NvidiaSoftwareInventory.ComputeDigest",
                                ec.message()));
                        task->finishTask();
                        computeDigestInProgress = false;
                        return;
                    }
                },
                hashComputeService, hashComputeObjPath, hashComputeInterface,
                "GetHash", retimerId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", hashComputeObjPath,
        std::array<const char*, 1>{hashComputeInterface.c_str()});
}

/**
 * @brief post handler for compute digest method
 *
 * @param req
 * @param asyncResp
 * @param swId
 */
inline void
    handlePostComputeDigest(const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& swId)
{
    crow::connections::systemBus->async_method_call(
        [req, asyncResp, swId](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            if (ec)
            {
                messages::resourceNotFound(
                    asyncResp->res, "NvidiaSoftwareInventory.ComputeDigest",
                    swId);
                BMCWEB_LOG_ERROR("Invalid object path: {}", ec);
                return;
            }
            for (auto& obj : subtree)
            {
                sdbusplus::message::object_path hashPath(obj.first);
                std::string hashId = hashPath.filename();
                if (hashId == swId)
                {
                    computeDigest(req, asyncResp, hashPath, swId);
                    return;
                }
            }
            messages::resourceNotFound(
                asyncResp->res, "NvidiaSoftwareInventory.ComputeDigest", swId);
            return;
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/com/Nvidia/ComputeHash", static_cast<int32_t>(0),
        std::array<const char*, 1>{hashComputeInterface.c_str()});
}

/**
 * @brief update oem action with ComputeDigest for devices which supports hash
 * compute
 *
 * @param[in] asyncResp
 * @param[in] swId
 */
inline void updateOemActionComputeDigest(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& swId)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, swId](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            if (ec)
            {
                // hash compute interface is not applicable, ignore for the
                // device
                return;
            }
            for (auto& obj : subtree)
            {
                sdbusplus::message::object_path hashPath(obj.first);
                std::string hashId = hashPath.filename();
                if (hashId == swId)
                {
                    std::string computeDigestTarget =
                        "/redfish/v1/UpdateService/FirmwareInventory/" + swId +
                        "/Actions/Oem/NvidiaSoftwareInventory.ComputeDigest";
                    asyncResp->res
                        .jsonValue["Actions"]["Oem"]["Nvidia"]
                                  ["#NvidiaSoftwareInventory.ComputeDigest"] = {
                        {"target", computeDigestTarget}};
                    break;
                }
            }
            return;
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/com/Nvidia/ComputeHash", static_cast<int32_t>(0),
        std::array<const char*, 1>{hashComputeInterface.c_str()});
}

/**
 * @brief Get allowable value for particular firmware inventory
 * The function gets allowable values from config file
 * /usr/share/bmcweb/fw_mctp_mapping.json.
 * and returns the allowable value if exists in the collection
 *
 * @param[in] inventoryPath - firmware inventory path.
 * @returns Pair of boolean value if the allowable value exists
 * and the object of AllowableValue who contains inventory path
 * and assigned to its MCTP EID.
 */
inline std::pair<bool, CommitImageValueEntry>
    getAllowableValue(const std::string_view inventoryPath)
{
    std::pair<bool, CommitImageValueEntry> result;

    std::vector<CommitImageValueEntry> allowableValues = getAllowableValues();
    std::vector<CommitImageValueEntry>::iterator it =
        find(allowableValues.begin(), allowableValues.end(),
             static_cast<std::string>(inventoryPath));

    if (it != allowableValues.end())
    {
        result.second = *it;
        result.first = true;
    }
    else
    {
        result.first = false;
    }

    return result;
}

/**
 * @brief Check whether firmware inventory is allowable
 * The function gets allowable values from config file
 * /usr/share/bmcweb/fw_mctp_mapping.json.
 * and check if the firmware inventory is in this collection
 *
 * @param[in] inventoryPath - firmware inventory path.
 * @returns boolean value indicates whether firmware inventory
 * is allowable.
 */
inline bool isInventoryAllowableValue(const std::string_view inventoryPath)
{
    bool isAllowable = false;

    std::vector<CommitImageValueEntry> allowableValues = getAllowableValues();
    std::vector<CommitImageValueEntry>::iterator it =
        find(allowableValues.begin(), allowableValues.end(),
             static_cast<std::string>(inventoryPath));

    if (it != allowableValues.end())
    {
        isAllowable = true;
    }
    else
    {
        isAllowable = false;
    }

    return isAllowable;
}

/**
 * @brief Update parameters for GET Method CommitImageInfo
 *
 * @param[in] asyncResp Shared pointer to the response message
 * @param[in] subtree  Collection of objectmappers for
 * "/xyz/openbmc_project/software"
 *
 * @return None
 */
inline void updateParametersForCommitImageInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::vector<std::pair<
        std::string,
        std::vector<std::pair<std::string, std::vector<std::string>>>>>&
        subtree)
{
    asyncResp->res.jsonValue["Parameters"] = nlohmann::json::array();
    nlohmann::json& parameters = asyncResp->res.jsonValue["Parameters"];

    nlohmann::json parameterTargets;
    parameterTargets["Name"] = "Targets";
    parameterTargets["Required"] = false;
    parameterTargets["DataType"] = "StringArray";
    parameterTargets["AllowableValues"] = nlohmann::json::array();

    nlohmann::json& allowableValues = parameterTargets["AllowableValues"];

    for (auto& obj : subtree)
    {
        sdbusplus::message::object_path path(obj.first);
        std::string fwId = path.filename();
        if (fwId.empty())
        {
            messages::internalError(asyncResp->res);
            BMCWEB_LOG_DEBUG("Cannot parse firmware ID");
            return;
        }

        if (isInventoryAllowableValue(obj.first))
        {
            allowableValues.push_back(
                "/redfish/v1/UpdateService/FirmwareInventory/" + fwId);
        }
    }

    parameters.push_back(parameterTargets);

#ifdef BMCWEB_ENABLE_REDFISH_AGGREGATION
    RedfishAggregator::getSatelliteConfigs(
        std::bind_front(forwardCommitImageActionInfo, req, asyncResp));
#endif
}

/**
 * @brief Handles request POST
 * The function triggers Commit Image action
 * for the list of delivered in the body of request
 * firmware inventories
 *
 * @param resp Async HTTP response.
 * @param asyncResp Pointer to object holding response data
 * @param[in] subtree  Collection of objectmappers for
 * "/xyz/openbmc_project/software"
 *
 * @return None
 */
inline void handleCommitImagePost(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::vector<std::pair<
        std::string,
        std::vector<std::pair<std::string, std::vector<std::string>>>>>&
        subtree)
{
    std::optional<std::vector<std::string>> targets;

    if (!json_util::readJsonAction(req, asyncResp->res, "Targets", targets))
    {
        return;
    }

    bool hasTargets = false;

    if (targets && targets.value().empty() == false)
    {
        hasTargets = true;
    }

    if (hasTargets)
    {
        std::vector<std::string> targetsCollection = targets.value();

        for (auto& target : targetsCollection)
        {
            sdbusplus::message::object_path objectPath(target);
            std::string inventoryPath =
                "/xyz/openbmc_project/software/" + objectPath.filename();
            std::pair<bool, CommitImageValueEntry> result =
                getAllowableValue(inventoryPath);
            if (result.first == true)
            {
                uint32_t eid = result.second.mctpEndpointId;

                initBackgroundCopy(req, asyncResp, eid,
                                   result.second.inventoryUri);
            }
            else
            {
                BMCWEB_LOG_DEBUG(
                    "Cannot find firmware inventory in allowable values");
                boost::urls::url_view targetURL(target);
                messages::resourceMissingAtURI(asyncResp->res, targetURL);
            }
        }
    }
    else
    {
        for (auto& obj : subtree)
        {
            std::pair<bool, CommitImageValueEntry> result =
                getAllowableValue(obj.first);

            if (result.first == true)
            {
                uint32_t eid = result.second.mctpEndpointId;

                initBackgroundCopy(req, asyncResp, eid,
                                   result.second.inventoryUri);
            }
        }
    }
}

inline void extendSoftwareInventoryGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& objectPath,
    const std::shared_ptr<std::string>& swId)
{
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        if constexpr (BMCWEB_NVIDIA_OEM_FW_UPDATE_STAGING)
        {
            fw_util::getFWSlotInformation(asyncResp, objectPath);
        }

        updateOemActionComputeDigest(asyncResp, *swId);
    }
}

/**
 * @brief POST handler for SSH public key exchange - user and remote server
 * authentication.
 *
 * @param app
 *
 * @return None
 */
inline nlohmann::json
    extendedInfoSuccessMsg(const std::string& msg, const std::string& arg)
{
    return nlohmann::json{{"@odata.type", "#Message.v1_1_1.Message"},
                          {"Message", msg},
                          {"MessageArgs", {arg}}};
}

inline void requestRoutesUpdateServicePublicKeyExchange(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.PublicKeyExchange/")
        .privileges(redfish::privileges::postUpdateService)
        .methods(
            boost::beast::http::verb::
                post)([&app](
                          const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            std::string remoteServerIP;
            std::string remoteServerKeyString; // "<type> <key>"

            BMCWEB_LOG_DEBUG("Enter UpdateService.PublicKeyExchange doPost");

            if (!json_util::readJsonAction(
                    req, asyncResp->res, "RemoteServerIP", remoteServerIP,
                    "RemoteServerKeyString", remoteServerKeyString) &&
                (remoteServerIP.empty() || remoteServerKeyString.empty()))
            {
                std::string emptyprops;
                if (remoteServerIP.empty())
                {
                    emptyprops += "RemoteServerIP ";
                }
                if (remoteServerKeyString.empty())
                {
                    emptyprops += "RemoteServerKeyString ";
                }
                messages::createFailedMissingReqProperties(asyncResp->res,
                                                           emptyprops);
                BMCWEB_LOG_DEBUG("Missing {}", emptyprops);
                return;
            }

            BMCWEB_LOG_DEBUG("RemoteServerIP: {} RemoteServerKeyString: {}",
                             remoteServerIP, remoteServerKeyString);

            // Verify remoteServerKeyString matches the pattern "<type> <key>"
            std::string remoteServerKeyStringPattern = R"(\S+\s+\S+)";
            std::regex pattern(remoteServerKeyStringPattern);
            if (!std::regex_match(remoteServerKeyString, pattern))
            {
                // Invalid format, return an error message
                messages::actionParameterValueTypeError(
                    asyncResp->res, remoteServerKeyString,
                    "RemoteServerKeyString", "UpdateService.PublicKeyExchange");
                BMCWEB_LOG_DEBUG("Invalid RemoteServerKeyString format");
                return;
            }

            // Call SCP service
            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        BMCWEB_LOG_ERROR("error_code = {} error msg = {}", ec,
                                         ec.message());
                        return;
                    }

                    crow::connections::systemBus->async_method_call(
                        [asyncResp](const boost::system::error_code ec,
                                    const std::string& selfPublicKeyStr) {
                            if (ec || selfPublicKeyStr.empty())
                            {
                                messages::internalError(asyncResp->res);
                                BMCWEB_LOG_ERROR(
                                    "error_code = {} error msg = {}", ec,
                                    ec.message());
                                return;
                            }

                            // Create a JSON object with the additional
                            // information
                            std::string keyMsg =
                                "Please add the following public key info to "
                                "~/.ssh/authorized_keys on the remote server";
                            std::string keyInfo =
                                selfPublicKeyStr + " root@dpu-bmc";

                            asyncResp->res
                                .jsonValue[messages::messageAnnotation] =
                                nlohmann::json::array();
                            asyncResp->res
                                .jsonValue[messages::messageAnnotation]
                                .push_back(
                                    extendedInfoSuccessMsg(keyMsg, keyInfo));
                            messages::success(asyncResp->res);
                            BMCWEB_LOG_DEBUG(
                                "Call to PublicKeyExchange succeeded {}",
                                selfPublicKeyStr);
                        },
                        "xyz.openbmc_project.Software.Download",
                        "/xyz/openbmc_project/software",
                        "xyz.openbmc_project.Common.SCP",
                        "GenerateSelfKeyPair");
                },
                "xyz.openbmc_project.Software.Download",
                "/xyz/openbmc_project/software",
                "xyz.openbmc_project.Common.SCP", "AddRemoteServerPublicKey",
                remoteServerIP, remoteServerKeyString);
        });
}

/**
 * @brief POST handler for adding remote server SSH public key
 *
 * @param app
 *
 * @return None
 */
inline void requestRoutesUpdateServiceRevokeAllRemoteServerPublicKeys(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/UpdateService/Actions/Oem/NvidiaUpdateService.RevokeAllRemoteServerPublicKeys/")
        .privileges(redfish::privileges::postUpdateService)
        .methods(
            boost::beast::http::verb::
                post)([&app](
                          const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            std::string remoteServerIP;

            BMCWEB_LOG_DEBUG(
                "Enter UpdateService.RevokeAllRemoteServerPublicKeys doPost");

            if (!json_util::readJsonAction(req, asyncResp->res,
                                           "RemoteServerIP", remoteServerIP) &&
                remoteServerIP.empty())
            {
                messages::createFailedMissingReqProperties(asyncResp->res,
                                                           "RemoteServerIP");
                BMCWEB_LOG_DEBUG("Missing RemoteServerIP");
                return;
            }

            BMCWEB_LOG_DEBUG("RemoteServerIP: {}", remoteServerIP);

            // Call SCP service
            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec) {
                    if (ec)
                    {
                        messages::internalError(asyncResp->res);
                        BMCWEB_LOG_ERROR("error_code = {} error msg = {}", ec,
                                         ec.message());
                    }
                    else
                    {
                        messages::success(asyncResp->res);
                        BMCWEB_LOG_DEBUG(
                            "Call to RevokeAllRemoteServerPublicKeys succeeded");
                    }
                },
                "xyz.openbmc_project.Software.Download",
                "/xyz/openbmc_project/software",
                "xyz.openbmc_project.Common.SCP",
                "RevokeAllRemoteServerPublicKeys", remoteServerIP);
        });
}

/**
 * @brief forward Commit Image Post Request to satBMC.
 *
 *
 * @param[in] req  HTTP request
 * @param[in] asyncResp Shared pointer to the response message
 * @param[in] ec Error code
 * @param[in] satelliteInfo satellite BMC information
 *
 * @return None
 */
inline void forwardCommitImagePost(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const std::unordered_map<std::string, boost::urls::url>& satelliteInfo)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Dbus query error for satellite BMC.");
        messages::internalError(asyncResp->res);
        return;
    }

    const auto& sat = satelliteInfo.find(redfishAggregationPrefix);
    if (sat == satelliteInfo.end())
    {
        BMCWEB_LOG_ERROR("satBMC is not found");
        return;
    }

    crow::HttpClient client(
        *req.ioService,
        std::make_shared<crow::ConnectionPolicy>(getPostAggregationPolicy()));

    std::function<void(crow::Response&)> cb =
        std::bind_front(handleSatBMCResponse, asyncResp);

    std::string data = req.body();
    boost::urls::url url(sat->second);
    url.set_path(req.url().path());

    client.sendDataWithCallback(std::move(data), url, req.fields(),
                                boost::beast::http::verb::post, cb);
}

/**
 * @brief the response handler of CommitImage Post
 * the function will examine the targets of the request and send out
 * the request to the satellite BMC if the remote targets are present.
 *
 * @param[in] req  HTTP request
 * @param[in] asyncResp Shared pointer to the response message
 *
 * @return return true to pass request to the local. otherwise, don't pass.
 */

inline bool handleSatBMCCommitImagePost(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    std::optional<std::vector<std::string>> targets;

    if (!json_util::readJsonAction(req, asyncResp->res, "Targets", targets))
    {
        messages::createFailedMissingReqProperties(asyncResp->res, "Targets");
        BMCWEB_LOG_ERROR("Missing Targets of OemCommitImage API");
        return false;
    }

    bool hasTargets = false;

    if (targets && targets.value().empty() == false)
    {
        hasTargets = true;
    }

    if (hasTargets)
    {
        std::vector<std::string> targetsCollection = targets.value();

        std::string rfaPrefix = redfishAggregationPrefix;
        rfaPrefix += "_";

        bool prefix = false, noPrefix = false;
        for (auto& target : targetsCollection)
        {
            std::string file = std::filesystem::path(target).filename();
            if (file.starts_with(rfaPrefix))
            {
                prefix = true;
            }
            else
            {
                noPrefix = true;
            }
        }

        if (prefix && !noPrefix)
        {
            // targets with the prefix included only.
            RedfishAggregator::getSatelliteConfigs(
                std::bind_front(forwardCommitImagePost, req, asyncResp));

            // don't pass the request to the local
            return false;
        }
        else if (prefix && noPrefix)
        {
            // drop the request with mixed targets.
            boost::urls::url_view targetURL("Target");
            messages::invalidObject(asyncResp->res, targetURL);
            return false;
        }
    }
    else
    {
        RedfishAggregator::getSatelliteConfigs(
            std::bind_front(forwardCommitImagePost, req, asyncResp));
        // forward the request with empty target.
    }
    return true;
}

/**
 * @brief  callback handler of JSON array object
 * the common function to get the JSON array object, espeically for
 * the response of CommitImageActionInfo from satBMC.
 *
 * @param[in] object JSON object
 * @param[in] name JSON name
 * @param[in] cb  The callback function
 *
 * @return None
 */
inline void getArrayObject(nlohmann::json::object_t* object,
                           const std::string_view name,
                           const std::function<void(nlohmann::json&)>& cb)
{
    for (std::pair<const std::string, nlohmann::json>& item : *object)
    {
        if (item.first != name)
        {
            continue;
        }
        auto* array = item.second.get_ptr<nlohmann::json::array_t*>();
        if (array == nullptr)
        {
            continue;
        }
        for (nlohmann::json& elm : *array)
        {
            cb(elm);
        }
    }
}

/**
 * @brief The response handler of CommitImageActionInfo from satBMC
 * aggregate the allowable values from the response of CommitImageActionInfo
 * if the response is successful.
 *
 * @param[in] asyncResp Shared pointer to the response message
 * @param[in] resp  HTTP response of satBMC
 *
 * @return None
 */
inline void commitImageActionInfoResp(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp, crow::Response& resp)
{
    // Failed to get ActionInfo because of the error response
    // just return without any further processing for the aggregation.
    if ((resp.result() == boost::beast::http::status::too_many_requests) ||
        (resp.result() == boost::beast::http::status::bad_gateway))
    {
        return;
    }

    // The resp will not have a json component
    // We need to create a json from resp's stringResponse
    std::string_view contentType = resp.getHeaderValue("Content-Type");
    if (bmcweb::asciiIEquals(contentType, "application/json") ||
        bmcweb::asciiIEquals(contentType, "application/json; charset=utf-8"))
    {
        nlohmann::json jsonVal = nlohmann::json::parse(*resp.body(), nullptr,
                                                       false);
        if (jsonVal.is_discarded())
        {
            return;
        }
        nlohmann::json::object_t* object =
            jsonVal.get_ptr<nlohmann::json::object_t*>();
        if (object == nullptr)
        {
            BMCWEB_LOG_ERROR("Parsed JSON was not an object?");
            return;
        }

        auto cb = [asyncResp](nlohmann::json& item) mutable {
            auto allowValueCb = [asyncResp](nlohmann::json& item) mutable {
                auto* str = item.get_ptr<std::string*>();
                if (str == nullptr)
                {
                    BMCWEB_LOG_CRITICAL("Item is not a string");
                    return;
                }
                nlohmann::json& allowableValues =
                    asyncResp->res
                        .jsonValue["Parameters"][0]["AllowableValues"];

                allowableValues.push_back(*str);
            };

            auto* nestedObject = item.get_ptr<nlohmann::json::object_t*>();
            if (nestedObject == nullptr)
            {
                BMCWEB_LOG_CRITICAL("Nested object is null");
                return;
            }
            getArrayObject(nestedObject, std::string("AllowableValues"),
                           allowValueCb);
        };
        getArrayObject(object, std::string("Parameters"), cb);
    }
}

/**
 * @brief forward Commit Image Action Info request to satBMC.
 * the function will send the request to satBMC to get the CommitImageActionInfo
 * if the satellie BMC is available.
 *
 * @param[in] req  HTTP request
 * @param[in] asyncResp Shared pointer to the response message
 * @param[in] ec Error code
 * @param[in] satelliteInfo satellite BMC information
 *
 * @return None
 */
inline void forwardCommitImageActionInfo(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const std::unordered_map<std::string, boost::urls::url>& satelliteInfo)
{
    // Something went wrong while querying dbus
    if (ec)
    {
        BMCWEB_LOG_ERROR("Dbus query error for satellite BMC.");
        messages::internalError(asyncResp->res);
        return;
    }

    const auto& sat = satelliteInfo.find(redfishAggregationPrefix);
    if (sat == satelliteInfo.end())
    {
        BMCWEB_LOG_ERROR("satellite BMC is not there.");
        return;
    }

    crow::HttpClient client(
        *req.ioService,
        std::make_shared<crow::ConnectionPolicy>(getPostAggregationPolicy()));

    std::function<void(crow::Response&)> cb =
        std::bind_front(commitImageActionInfoResp, asyncResp);

    std::string data;
    boost::urls::url url(sat->second);
    url.set_path(req.url().path());

    client.sendDataWithCallback(std::move(data), url, req.fields(),
                                boost::beast::http::verb::get, cb);
}


struct SimpleUpdateParams
{
    std::string remoteServerIP;
    std::string fwImagePath;
    std::string transferProtocol;
    bool forceUpdate;
    std::optional<std::string> username;
};

inline void
    downloadViaSCP(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::shared_ptr<const SimpleUpdateParams>& params,
                   const std::string& targetPath)
{
    BMCWEB_LOG_DEBUG("Downloading from {}:{} to {} using {} protocol...",
                     params->remoteServerIP, params->fwImagePath, targetPath,
                     params->transferProtocol);
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
        if (ec)
        {
            messages::internalError(asyncResp->res);
            BMCWEB_LOG_ERROR("error_code = {}, error msg = {}", ec,
                             ec.message());
        }
        else
        {
            BMCWEB_LOG_DEBUG("Call to DownloadViaSCP Success");
        }
    },
        "xyz.openbmc_project.Software.Download",
        "/xyz/openbmc_project/software", "xyz.openbmc_project.Common.SCP",
        "DownloadViaSCP", params->remoteServerIP, (params->username).value(),
        params->fwImagePath, targetPath);
}

inline void
    downloadViaHTTP(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                    const std::shared_ptr<const SimpleUpdateParams>& params,
                    const std::string& targetPath)
{
    BMCWEB_LOG_DEBUG("Downloading from {}:{} to {} using {} protocol...",
                     params->remoteServerIP, params->fwImagePath, targetPath,
                     params->transferProtocol);
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec) {
        if (ec)
        {
            messages::internalError(asyncResp->res);
            BMCWEB_LOG_ERROR("error_code = {}, error msg = {}", ec,
                             ec.message());
        }
        else
        {
            BMCWEB_LOG_DEBUG("Call to DownloadViaHTTP Success");
        }
    },
        "xyz.openbmc_project.Software.Download",
        "/xyz/openbmc_project/software", "xyz.openbmc_project.Common.HTTP",
        "DownloadViaHTTP", params->remoteServerIP,
        (params->transferProtocol == "HTTPS"), params->fwImagePath, targetPath);
}

inline void mountTargetPath(const std::string& serviceName,
                            const std::string& objPath)
{
    // For update target path that needs mouting, proxy interface should be used
    // Here we try to mount the proxy interface
    // For target path that does not need mounting, catch exception and continue
    try
    {
        auto method = crow::connections::systemBus->new_method_call(
            serviceName.data(), objPath.data(),
            "xyz.openbmc_project.VirtualMedia.Proxy", "Mount");
        crow::connections::systemBus->call_noreply(method);
        BMCWEB_LOG_DEBUG("Mounting device");
    }
    catch (const sdbusplus::exception::SdBusError& ex)
    {
        if (std::string_view("org.freedesktop.DBus.Error.UnknownMethod") !=
            std::string_view(ex.name()))
        {
            BMCWEB_LOG_ERROR("Mounting error");
        }
        else
        {
            // This is a normal case for target path that doesn't need
            // any mounting
            BMCWEB_LOG_DEBUG("Continue without mounting");
        }
    }
    catch (...)
    {
        BMCWEB_LOG_ERROR("Mounting error");
    }
}

inline void downloadFirmwareImageToTarget(
    const std::shared_ptr<const crow::Request>& request,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<const SimpleUpdateParams>& params,
    const std::string& service, const std::string& objPath)
{
    mountTargetPath(service, objPath);
    BMCWEB_LOG_DEBUG(
        "Getting value of Path property for service {} and object path {}...",
        service, objPath);
    crow::connections::systemBus->async_method_call(
        [request, asyncResp,
         params](const boost::system::error_code ec,
                 const std::variant<std::string>& property) {
        if (ec)
        {
            messages::actionParameterNotSupported(asyncResp->res, "Targets",
                                                  "UpdateService.SimpleUpdate");
            BMCWEB_LOG_ERROR("Failed to read the path property of Target");
            BMCWEB_LOG_ERROR("error_code = {}, error msg = {}", ec,
                             ec.message());
            return;
        }

        const std::string* targetPath = std::get_if<std::string>(&property);

        if (targetPath == nullptr)
        {
            messages::actionParameterNotSupported(asyncResp->res, "Targets",
                                                  "UpdateService.SimpleUpdate");
            BMCWEB_LOG_ERROR("Null value returned for path");
            return;
        }

        BMCWEB_LOG_DEBUG("Path property: {}", *targetPath);

        // Check if local path exists
        if (!fs::exists(*targetPath))
        {
            messages::resourceNotFound(asyncResp->res, "Targets", *targetPath);
            BMCWEB_LOG_ERROR("Path does not exist");
            return;
        }

        // Setup callback for when new software detected
        // Give SCP 10 minutes to detect new software
        monitorForSoftwareAvailable(asyncResp, *request, 600);

        if (params->transferProtocol == "SCP")
        {
            downloadViaSCP(asyncResp, params, *targetPath);
        }
        else if ((params->transferProtocol == "HTTP") ||
                 (params->transferProtocol == "HTTPS"))
        {
            downloadViaHTTP(asyncResp, params, *targetPath);
        }
    },
        service, objPath, "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Common.FilePath", "Path");
}

inline void setUpdaterForceUpdateProperty(
    const std::shared_ptr<const crow::Request>& request,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<const SimpleUpdateParams>& params,
    const std::string& serviceName, const std::string& serviceObjectPath,
    const std::string& fwItemObjPath)
{
    BMCWEB_LOG_DEBUG(
        "Setting ForceUpdate property for service {} and object path {} to {}",
        serviceName, serviceObjectPath,
        (params->forceUpdate ? "true" : "false"));
    crow::connections::systemBus->async_method_call(
        [request, asyncResp, params, serviceName,
         fwItemObjPath](const boost::system::error_code ec) {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "Failed to set ForceUpdate property. Aborting update as "
                "value of ForceUpdate property can't be guaranteed.");
            BMCWEB_LOG_ERROR("error_code = {}", ec);
            BMCWEB_LOG_ERROR("error_msg = {}", ec.message());
            if (asyncResp)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }
        BMCWEB_LOG_DEBUG("ForceUpdate property successfully set to {}.",
                         (params->forceUpdate ? "true" : "false"));
        // begin downloading the firmware image to the target path
        downloadFirmwareImageToTarget(request, asyncResp, params, serviceName,
                                      fwItemObjPath);
    },
        serviceName, serviceObjectPath, "org.freedesktop.DBus.Properties",
        "Set", "xyz.openbmc_project.Software.UpdatePolicy", "ForceUpdate",
        dbus::utility::DbusVariantType(params->forceUpdate));
}

inline void findObjectPathAssociatedWithService(
    const std::shared_ptr<const crow::Request>& request,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<const SimpleUpdateParams>& params,
    const std::string& serviceName, const std::string& fwItemObjPath,
    const char* rootPath = "/xyz/openbmc_project")
{
    BMCWEB_LOG_DEBUG(
        "Searching for object paths associated with the service {} in the "
        "sub-tree {}...",
        serviceName, rootPath);
    crow::connections::systemBus->async_method_call(
        [request, asyncResp, params, serviceName, fwItemObjPath,
         rootPath](const boost::system::error_code ec,
                   const dbus::utility::MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("error_code = {}", ec);
            BMCWEB_LOG_ERROR("error_msg = {}", ec.message());
            if (asyncResp)
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }
        else if (subtree.empty())
        {
            BMCWEB_LOG_DEBUG(
                "Could not find any services implementing "
                "xyz.openbmc_project.Software.UpdatePolicy associated with the "
                "object path {}. Proceeding with update as though the "
                "ForceUpdate property is set to false.",
                rootPath);
            // Begin downloading the firmware image to the target path
            downloadFirmwareImageToTarget(request, asyncResp, params,
                                          serviceName, fwItemObjPath);
            return;
        }
        // iterate through the object paths in the subtree until one is found
        // with an associated service name matching the input service.
        std::optional<std::string> serviceObjPath{};
        for (const auto& pathServiceMapPair : subtree)
        {
            const auto& currObjPath = pathServiceMapPair.first;
            for (const auto& serviceInterfacesMap : pathServiceMapPair.second)
            {
                const auto& currServiceName = serviceInterfacesMap.first;
                if (currServiceName == serviceName)
                {
                    if (!serviceObjPath.has_value())
                    {
                        serviceObjPath.emplace(currObjPath);
                        break;
                    }
                }
            }
            // break external for-loop if object path is found
            if (serviceObjPath.has_value())
            {
                break;
            }
        }
        if (serviceObjPath.has_value())
        {
            BMCWEB_LOG_DEBUG("Found object path {}.", serviceObjPath.value());
            // use the service and object path found to set the ForceUpdate
            // property
            setUpdaterForceUpdateProperty(request, asyncResp, params,
                                          serviceName, serviceObjPath.value(),
                                          fwItemObjPath);
        }
        else
        {
            // If there is no object implementing
            // xyz.openbmc_project.Software.UpdatePolicy associated with
            // the service under the sub-tree root, then that service does
            // not implement a force-update policy, and the download should
            // continue.
            BMCWEB_LOG_DEBUG(
                "Could not find any a service {} implementing "
                "xyz.openbmc_project.Software.UpdatePolicy and "
                "associated with the object path {}. Proceeding with "
                "update as though the ForceUpdate property is set to "
                "false.",
                serviceName, rootPath);
            // Begin downloading the firmware image to the target path
            downloadFirmwareImageToTarget(request, asyncResp, params,
                                          serviceName, fwItemObjPath);
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", rootPath, 0,
        std::array<const char*, 1>{
            "xyz.openbmc_project.Software.UpdatePolicy"});
}

inline void findAssociatedUpdaterService(
    const std::shared_ptr<const crow::Request>& request,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<const SimpleUpdateParams>& params,
    const std::string& fwItemObjPath)
{
    BMCWEB_LOG_DEBUG("Searching for updater service associated with {}...",
                     fwItemObjPath);
    crow::connections::systemBus->async_method_call(
        [request, asyncResp, params,
         fwItemObjPath](const boost::system::error_code ec,
                        const MapperServiceMap& objInfo) {
        if (ec)
        {
            messages::actionParameterNotSupported(asyncResp->res, "Targets",
                                                  "UpdateService.SimpleUpdate");
            BMCWEB_LOG_ERROR("Request incorrect target URI parameter: {}",
                             fwItemObjPath);
            BMCWEB_LOG_ERROR("error_code = {}, error msg = {}", ec,
                             ec.message());
            return;
        }
        // Ensure we only got one service back
        if (objInfo.size() != 1)
        {
            messages::internalError(asyncResp->res);
            BMCWEB_LOG_ERROR("Invalid Object Size {}", objInfo.size());
            return;
        }
        const std::string& serviceName = objInfo[0].first;
        BMCWEB_LOG_DEBUG("Found service {}", serviceName);
        // The ForceUpdate property of the
        // xyz.openbmc_project.Software.UpdatePolicy dbus interface should
        // be explicitly set to true or false, according to the value of
        // ForceUpdate option in the Redfish command.
        findObjectPathAssociatedWithService(request, asyncResp, params,
                                            serviceName, fwItemObjPath);
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject", fwItemObjPath,
        std::array<const char*, 2>{"xyz.openbmc_project.Software.Version",
                                   "xyz.openbmc_project.Common.FilePath"});
}

inline bool isProtocolScpOrHttp(const std::string& protocol)
{
    return protocol == "SCP" || protocol == "HTTP" || protocol == "HTTPS";
}


} // namespace redfish
