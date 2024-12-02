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
             {{"UpdateOptionSupport",
#ifdef BMCWEB_NVIDIA_OEM_FW_UPDATE_STAGING
               {"StageAndActivate", "StageOnly"}}}
#else
               {"StageAndActivate"}}}
#endif
            }
        };
    }
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
#ifdef BMCWEB_NVIDIA_OEM_FW_UPDATE_STAGING
        fw_util::getFWSlotInformation(asyncResp, objectPath);
#endif

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

} // namespace redfish
