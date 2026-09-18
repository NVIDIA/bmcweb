/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved. SPDX-License-Identifier: Apache-2.0
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
#include "generated/enums/nvidia_system_profile.hpp"
#include "nlohmann/json.hpp"
#include "task.hpp"

#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <dbus_utility.hpp>
#include <nvidia_oem_dpu.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <utils/json_utils.hpp>
#include <utils/memfd_utils.hpp>
#include <utils/privilege_utils.hpp>

#include <cstdint> //max uint
#include <fstream>
#include <iostream>
#include <optional>

namespace redfish
{
namespace profiles
{
const std::string profileService = "xyz.openbmc_project.Profile.Manager";
const std::string profilePath =
    "/xyz/openbmc_project/control/system/Card1/profile/";
const sdbusplus::message::object_path profileManagerPath(
    "/xyz/openbmc_project/control/system/Card1/profile/manager");
const std::string statusIntrf = "xyz.openbmc_project.Profiles.Statuses";
const std::string pendingListIntrf =
    "xyz.openbmc_project.Profiles.PendingLists";
const std::string managerIntrf = "xyz.openbmc_project.Profiles.Manager";
const std::string configurationIntrf =
    "xyz.openbmc_project.Profiles.Configurations";
const std::string profileFolder = "/etc/profile-manager/";
const std::string statusPrefix =
    "xyz.openbmc_project.Profiles.Statuses.Status.";
const uint16_t invalidProfileNumber = 0xFFFF;

// Profile status
const uint8_t nvidiaIndex = 0;
const uint8_t oemIndex = 1;

enum ProfileOwner
{
    User,
    Oem,
    Nvidia,
    Bios,
    Invalid,
};

constexpr std::string_view defaultProfileTruststorePath =
    "/xyz/openbmc_project/certs/authority/profileDefault";
constexpr std::string_view defaultProfileTruststoreService =
    "xyz.openbmc_project.Certs.Manager.Authority.ProfileDefault";
constexpr std::string_view defaultProfileTruststore = "Default";

constexpr std::string_view nvidiaProfileTruststore = "NvidiaCertificates";
constexpr std::string_view nvidiaProfileTruststoreService =
    "xyz.openbmc_project.Certs.Manager.Authority.ProfileNvidia";
constexpr std::string_view nvidiaProfileTruststorePath =
    "/xyz/openbmc_project/certs/authority/profileNvidia";

constexpr std::string_view oemProfileTruststore = "OemCertificates";
constexpr std::string_view oemProfileTruststoreService =
    "xyz.openbmc_project.Certs.Manager.Authority.ProfileOem";
constexpr std::string_view oemProfileTruststorePath =
    "/xyz/openbmc_project/certs/authority/profileOem";

constexpr std::array<std::string_view, 2> profileTruststores = {
    nvidiaProfileTruststore, oemProfileTruststore};

inline nvidia_system_profile::ActionStatus toActionStatus(
    std::string_view status)
{
    if (status == "Start")
    {
        return nvidia_system_profile::ActionStatus::Start;
    }
    if (status == "StartBios")
    {
        return nvidia_system_profile::ActionStatus::StartBios;
    }
    if (status == "StartVerification")
    {
        return nvidia_system_profile::ActionStatus::StartVerification;
    }
    if (status == "ProfileSaved")
    {
        return nvidia_system_profile::ActionStatus::ProfileSaved;
    }
    if (status == "PendingBios")
    {
        return nvidia_system_profile::ActionStatus::PendingBios;
    }
    if (status == "BiosStarted")
    {
        return nvidia_system_profile::ActionStatus::BiosStarted;
    }
    if (status == "BiosFinished")
    {
        return nvidia_system_profile::ActionStatus::BiosFinished;
    }
    if (status == "BiosStarted")
    {
        return nvidia_system_profile::ActionStatus::BiosStarted;
    }
    if (status == "BmcStarted")
    {
        return nvidia_system_profile::ActionStatus::BmcStarted;
    }
    if (status == "None")
    {
        return nvidia_system_profile::ActionStatus::None;
    }
    if (status == "Active")
    {
        return nvidia_system_profile::ActionStatus::Active;
    }
    if (status == "Failed")
    {
        return nvidia_system_profile::ActionStatus::Failed;
    }
    BMCWEB_LOG_ERROR("Invalid status action");
    return nvidia_system_profile::ActionStatus::Invalid;
}

inline std::string actionStatusToString(
    nvidia_system_profile::ActionStatus status)
{
    switch (status)
    {
        case nvidia_system_profile::ActionStatus::Start:
            return "Start";
        case nvidia_system_profile::ActionStatus::StartBios:
            return "StartBios";
        case nvidia_system_profile::ActionStatus::StartVerification:
            return "StartVerification";
        case nvidia_system_profile::ActionStatus::ProfileSaved:
            return "ProfileSaved";
        case nvidia_system_profile::ActionStatus::PendingBios:
            return "PendingBios";
        case nvidia_system_profile::ActionStatus::BiosStarted:
            return "BiosStarted";
        case nvidia_system_profile::ActionStatus::BiosFinished:
            return "BiosFinished";
        case nvidia_system_profile::ActionStatus::BmcStarted:
            return "BmcStarted";
        case nvidia_system_profile::ActionStatus::None:
            return "None";
        case nvidia_system_profile::ActionStatus::Active:
            return "Active";
        case nvidia_system_profile::ActionStatus::Failed:
            return "Failed";
        default:
            BMCWEB_LOG_ERROR("Invalid status to convert to string");
            return "Invalid";
    }
}

inline nvidia_system_profile::ActionStatus getStatusActionFromDbusStatus(
    std::string_view status)
{
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.Start")
    {
        return nvidia_system_profile::ActionStatus::Start;
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.StartBios")
    {
        return nvidia_system_profile::ActionStatus::StartBios;
    }
    if (status ==
        "xyz.openbmc_project.Profiles.Statuses.Status.StartVerification")
    {
        return nvidia_system_profile::ActionStatus::StartVerification;
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.ProfileSaved")
    {
        return nvidia_system_profile::ActionStatus::ProfileSaved;
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.PendingBios")
    {
        return nvidia_system_profile::ActionStatus::PendingBios;
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.BiosStarted")
    {
        return nvidia_system_profile::ActionStatus::BiosStarted;
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.BiosFinished")
    {
        return nvidia_system_profile::ActionStatus::BiosFinished;
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.BiosStarted")
    {
        return nvidia_system_profile::ActionStatus::BiosStarted;
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.BmcStarted")
    {
        return nvidia_system_profile::ActionStatus::BmcStarted;
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.None")
    {
        return nvidia_system_profile::ActionStatus::None;
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.Active")
    {
        return nvidia_system_profile::ActionStatus::Active;
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.Failed")
    {
        return nvidia_system_profile::ActionStatus::Failed;
    }
    BMCWEB_LOG_ERROR("Parsing error, status: {}", status);
    return nvidia_system_profile::ActionStatus::Invalid;
}

inline std::string getStringFromDbusStatus(std::string_view status)
{
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.Start")
    {
        return "Start";
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.StartBios")
    {
        return "StartBios";
    }
    if (status ==
        "xyz.openbmc_project.Profiles.Statuses.Status.StartVerification")
    {
        return "StartVerification";
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.ProfileSaved")
    {
        return "ProfileSaved";
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.PendingBios")
    {
        return "PendingBios";
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.BiosStarted")
    {
        return "BiosStarted";
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.BiosFinished")
    {
        return "BiosFinished";
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.BiosStarted")
    {
        return "BiosStarted";
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.BmcStarted")
    {
        return "BmcStarted";
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.None")
    {
        return "None";
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.Active")
    {
        return "Active";
    }
    if (status == "xyz.openbmc_project.Profiles.Statuses.Status.Failed")
    {
        return "Failed";
    }
    BMCWEB_LOG_ERROR("Parsing error, status: {}", status);
    return "";
}

inline std::string dbusOwnerToString(std::string dbusOwner)
{
    if (dbusOwner == "xyz.openbmc_project.Profiles.Configurations.Owner.Nvidia")
    {
        return "Nvidia";
    }
    if (dbusOwner == "xyz.openbmc_project.Profiles.Configurations.Owner.OEM")
    {
        return "Oem";
    }
    if (dbusOwner == "xyz.openbmc_project.Profiles.Configurations.Owner.User")
    {
        return "User";
    }
    BMCWEB_LOG_ERROR("Owner string error {}", dbusOwner);
    return "";
}

/**
 * @brief Finish the profile task
 * @param taskData - task data object
 * @param state - profile state
 * @param messages - json messages
 * @return bool -if true - task is completed
 */
inline bool finishProfileTask(const std::shared_ptr<task::TaskData>& taskData,
                              std::string_view state,
                              const nlohmann::json& messages)
{
    taskData->state = state;
    taskData->messages.emplace_back(messages);
    return task::completed;
}

/**
 * @brief handle new task status change
 * This function is called when the status profile action changes.
 * if the status is invalid, the task is aborted.
 * if the status is none or active, the task is completed.
 * if the status is failed, the task is aborted.
 * if the status is pending, the task is not completed.
 * @param taskData - task data object
 * @param fullStatus - full status string
 * @return bool - is task completed or not
 */
inline bool handleTaskStatus(const std::shared_ptr<task::TaskData>& taskData,
                             std::string fullStatus)
{
    BMCWEB_LOG_DEBUG("Handle status: {}", fullStatus);
    std::string index = std::to_string(taskData->index);
    nvidia_system_profile::ActionStatus actionStatus =
        getStatusActionFromDbusStatus(fullStatus);
    if (actionStatus == nvidia_system_profile::ActionStatus::Invalid)
    {
        BMCWEB_LOG_ERROR("Invalid action status: {}", fullStatus);
        return finishProfileTask(taskData, "Invalid",
                                 messages::taskAborted(index));
    }
    if (actionStatus == nvidia_system_profile::ActionStatus::None ||
        actionStatus == nvidia_system_profile::ActionStatus::Active)
    {
        taskData->percentComplete = 100;
        return finishProfileTask(taskData, "Completed",
                                 messages::taskCompletedOK(index));
    }
    if (actionStatus == nvidia_system_profile::ActionStatus::Failed)
    {
        return finishProfileTask(taskData, "Exception",
                                 messages::taskAborted(index));
    }
    return !task::completed;
}

/**
 * @brief update task handler
 * This is called in case of match on the profile status interface change
 * @param ec - error code
 * @param msg - dbus message
 * @param taskData - task data object
 * @return bool - is task completed or not
 */
inline bool updateTaskHandler(boost::system::error_code ec,
                              sdbusplus::message_t& msg,
                              const std::shared_ptr<task::TaskData>& taskData)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Profile dbus error ");
        return finishProfileTask(
            taskData, "Aborted",
            messages::taskAborted(std::to_string(taskData->index)));
    }
    std::string iface;
    dbus::utility::DBusPropertiesMap values;
    std::vector<std::string> properties;
    msg.read(iface, values, properties);

    BMCWEB_LOG_DEBUG("Status changed on index: {}, path: {}, interface: {}",
                     std::to_string(taskData->index),
                     std::string(msg.get_path()), iface);
    if (iface != "xyz.openbmc_project.Profiles.Statuses")
    {
        return !task::completed;
    }
    const std::string* activateStatus = nullptr;
    const std::string* addStatus = nullptr;
    const std::string* deleteStatus = nullptr;
    const uint16_t* activateProgress = nullptr;
    const uint16_t* addProgress = nullptr;
    const uint16_t* deleteProgress = nullptr;
    if (!sdbusplus::unpackPropertiesNoThrow(
            redfish::dbus_utils::UnpackErrorPrinter(), values,
            "ActivateProfile", activateStatus, "AddProfile", addStatus,
            "DeleteProfile", deleteStatus, "ActivateProgress", activateProgress,
            "AddProgress", addProgress, "DeleteProgress", deleteProgress))
    {
        taskData->messages.emplace_back(messages::internalError());
        return !task::completed;
    }
    if (activateStatus != nullptr)
    {
        handleTaskStatus(taskData, *activateStatus);
    }
    else if (addStatus != nullptr)
    {
        handleTaskStatus(taskData, *addStatus);
    }
    else if (deleteStatus != nullptr)
    {
        handleTaskStatus(taskData, *deleteStatus);
    }

    if (activateProgress != nullptr)
    {
        taskData->percentComplete = static_cast<int>(*activateProgress);
    }
    else if (addProgress != nullptr)
    {
        taskData->percentComplete = static_cast<int>(*addProgress);
    }
    else if (deleteProgress != nullptr)
    {
        taskData->percentComplete = static_cast<int>(*deleteProgress);
    }
    return !task::completed;
}

inline void startProfileUpdateTask(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& profileNumber, task::Payload&& payload)
{
    std::string matchString = sdbusplus::bus::match::rules::propertiesChanged(
        profilePath + profileNumber, statusIntrf);
    std::shared_ptr<task::TaskData> task =
        task::TaskData::createTask(updateTaskHandler, matchString);
    task->startTimer(std::chrono::minutes(20));
    task->populateResp(aResp->res);
    task->payload.emplace(std::move(payload));
}

inline void setProfileProperty(
    task::Payload&& payload, const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& profileNumber, const std::string& interface,
    const std::string& property, const std::string& value)
{
    dbus::utility::setProperty(
        profileService, profilePath + profileNumber, interface, property, value,
        [aResp, property, value, payload = std::move(payload),
         profileNumber](const boost::system::error_code& ec) mutable {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error on profile setProperty: {}, value: {}, error: {}",
                    property, value, ec);
                messages::internalError(aResp->res);
                return;
            }
            if (value == "xyz.openbmc_project.Profiles.Statuses.Status.Start")
            {
                startProfileUpdateTask(aResp, profileNumber,
                                       std::move(payload));
            }
        });
}

/**
 * @brief Handles GET request for Profile collection
 * @param app - crow application
 * @param req - crow request
 * @param aResp - response object
 * @param systemName - system name
 * @return None
 */
inline void handleGetProfilesCollection(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(aResp->res, "ComputerSystem", systemName);
        return;
    }
    BMCWEB_LOG_DEBUG("Start get profile Collection");

    dbus::utility::getSubTreePaths(
        profilePath, 0,
        std::array<std::string_view, 1>{
            "xyz.openbmc_project.Profiles.Statuses"},
        [aResp](const boost::system::error_code& ec,
                const dbus::utility::MapperGetSubTreePathsResponse& objects) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "DBUS response error in get profiles, error: {} ",
                    ec.value());
                messages::internalError(aResp->res);
                return;
            }
            nlohmann::json& members = aResp->res.jsonValue["Members"];
            members = nlohmann::json::array();
            for (const auto& object : objects)
            {
                sdbusplus::message::object_path path(object);
                std::string profileNumber = path.filename();
                if (profileNumber.empty())
                {
                    continue;
                }
                auto newPath = boost::urls::format(
                    "/redfish/v1/Systems/{}/Oem/Nvidia/SystemConfigProfile/Profiles/{}",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME, profileNumber);
                nlohmann::json::object_t member;
                member["@odata.id"] = std::move(newPath);
                members.push_back(std::move(member));
                BMCWEB_LOG_DEBUG("Profile: {}", profileNumber);
            }
            aResp->res.jsonValue["Members@odata.count"] = members.size();
        });
    aResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/SystemConfigProfile/Profiles",
        BMCWEB_REDFISH_SYSTEM_URI_NAME);
    aResp->res.jsonValue["@odata.type"] =
        "#NvidiaSystemProfileCollection.v1_0_0.NvidiaSystemProfileCollection";
    aResp->res.jsonValue["Name"] = "System Configuration Profiles Collection";
    aResp->res.jsonValue["Id"] = "Profiles";
}

/**
 * @brief Handles set property due to PATCH request for Profile status
 * @param aResp - response object
 * @param profileNumber - profile number
 * @param isBiosUser - true if the profile uploaded from the Bios
 * @param property - property name
 * @param status - status value
 * @return None
 */
inline void handlePatchSetProfileStatus(
    task::Payload&& payload, bool isBiosUser,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& profileNumber, const std::string& property,
    const std::string& status)
{
    const std::array<nvidia_system_profile::ActionStatus, 3> allowedUefiValues =
        {nvidia_system_profile::ActionStatus::BiosStarted,
         nvidia_system_profile::ActionStatus::BiosFinished,
         nvidia_system_profile::ActionStatus::Failed};
    const std::array<nvidia_system_profile::ActionStatus, 2> allowedUserValues =
        {nvidia_system_profile::ActionStatus::Failed};
    std::string user = isBiosUser ? "Bios" : "User";

    nvidia_system_profile::ActionStatus statusEnum = toActionStatus(status);
    if (statusEnum == nvidia_system_profile::ActionStatus::Invalid)
    {
        messages::actionParameterValueError(aResp->res, user, status);
        return;
    }
    BMCWEB_LOG_DEBUG("User: {}, path status: {}", user, status);
    if (isBiosUser)
    {
        if (std::ranges::find(allowedUefiValues, statusEnum) ==
            allowedUefiValues.end())
        {
            messages::actionParameterValueError(aResp->res, user, status);
            return;
        }
    }
    else
    {
        if (std::ranges::find(allowedUserValues, statusEnum) ==
            allowedUserValues.end())
        {
            messages::actionParameterValueError(aResp->res, user, status);
            return;
        }
    }
    setProfileProperty(std::move(payload), aResp, profileNumber, statusIntrf,
                       property, statusPrefix + status);
}

inline void populateProfileStatues(
    task::Payload&& payload, const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const boost::system::error_code& ec, const std::string& profileNumber,
    std::optional<std::string> activateStatus,
    std::optional<std::string> deleteStatus,
    std::optional<std::string> addStatus, bool isBiosUser)
{
    if (ec)
    {
        messages::internalError(aResp->res);
        return;
    }
    if (activateStatus)
    {
        if (deleteStatus)
        {
            messages::actionParameterDuplicate(aResp->res, "ActivateProfile",
                                               "DeleteProfile");
            return;
        }
        handlePatchSetProfileStatus(std::move(payload), isBiosUser, aResp,
                                    profileNumber, "ActivateProfile",
                                    *activateStatus);
        return;
    }
    if (deleteStatus)
    {
        handlePatchSetProfileStatus(std::move(payload), isBiosUser, aResp,
                                    profileNumber, "DeleteProfile",
                                    *deleteStatus);
        return;
    }
    if (addStatus)
    {
        if (addStatus == "Start")
        {
            messages::actionParameterUnknown(aResp->res, "AddProfile", "Start");
            return;
        }
        handlePatchSetProfileStatus(std::move(payload), isBiosUser, aResp,
                                    profileNumber, "AddProfile", *addStatus);
        return;
    }
}

/**
 * @brief Handles PATCH requests for Profile status updates.
 * Supported updates include: ActivateProfile and DeleteProfile.
 * - Users are restricted to patching the "failed" status.
 * - BIOS is limited to patching "BiosStarted" and "BiosFinished" statuses.
 * @param app - crow application
 * @param req - crow request
 * @param aResp - response object
 * @param systemName - system name
 * @param profileNumber - profile number
 * @return None
 */
inline void handlePatchProfile(crow::App& app, const crow::Request& req,
                               const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const std::string& systemName,
                               const std::string& profileNumber)
{
    BMCWEB_LOG_DEBUG("Start handlePatchProfile, profile number {}",
                     profileNumber);
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(aResp->res, "ComputerSystem", systemName);
        return;
    }
    std::optional<std::string> activateStatus;
    std::optional<std::string> deleteStatus;
    std::optional<std::string> addStatus;
    if (!json_util::readJsonPatch(req, aResp->res, "Status/ActivateProfile",
                                  activateStatus, "Status/DeleteProfile",
                                  deleteStatus, "Status/AddProfile", addStatus))
    {
        BMCWEB_LOG_ERROR("Not a Valid JSON");
        return;
    }
    if (req.session == nullptr)
    {
        BMCWEB_LOG_ERROR("Session is null");
        messages::insufficientPrivilege(aResp->res);
        return;
    }
    task::Payload payload(req);
    privilege_utils::isBiosPrivilege(
        req.session->username,
        [aResp, activateStatus, deleteStatus, addStatus, profileNumber,
         payload = std::move(payload)](const boost::system::error_code& ec,
                                       const bool isBiosUser) mutable {
            populateProfileStatues(std::move(payload), aResp, ec, profileNumber,
                                   activateStatus, deleteStatus, addStatus,
                                   isBiosUser);
        });
}

inline void getProfileStatusInfo(
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& profileNumber)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
        messages::resourceNotFound(aResp->res, "Profile", profileNumber);
        return;
    }
    const std::string* addStatus = nullptr;
    const std::string* activationStatus = nullptr;
    const std::string* deleteStatus = nullptr;
    BMCWEB_LOG_DEBUG(" Handle get profile - getAllProperties");
    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties, "AddProfile", addStatus,
        "ActivateProfile", activationStatus, "DeleteProfile", deleteStatus);
    if (!success)
    {
        messages::internalError(aResp->res);
        return;
    }
    auto handleAddStatus =
        [aResp](const std::string& status, const std::string& action) {
            std::string addStatusStr = getStringFromDbusStatus(status);
            if (addStatusStr.empty())
            {
                messages::internalError(aResp->res);
                return;
            }
            aResp->res.jsonValue["Status"][action] = addStatusStr;
        };

    if (addStatus != nullptr)
    {
        handleAddStatus(*addStatus, "AddProfile");
    }
    if (activationStatus != nullptr)
    {
        handleAddStatus(*activationStatus, "ActivateProfile");
    }
    if (deleteStatus != nullptr)
    {
        handleAddStatus(*deleteStatus, "DeleteProfile");
    }
}

inline void getProfileFileInfo(
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& profileNumber)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error: {} ", ec);
        messages::resourceNotFound(aResp->res, "Profile", profileNumber);
        return;
    }
    const std::string* description = nullptr;
    const std::string* owner = nullptr;
    const std::string* uuid = nullptr;
    const std::string* profileName = nullptr;
    const uint64_t* version = nullptr;
    const bool* isDefault = nullptr;
    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties, "Description",
        description, "Owner", owner, "UUID", uuid, "Version", version,
        "IsDefault", isDefault, "Name", profileName);

    if (!success)
    {
        messages::internalError(aResp->res);
        return;
    }
    if (description != nullptr)
    {
        aResp->res.jsonValue["Description"] = *description;
    }
    if (profileName != nullptr)
    {
        aResp->res.jsonValue["ProfileName"] = *profileName;
    }
    if (owner != nullptr)
    {
        std::string ownerStr(dbusOwnerToString(*owner));
        if (ownerStr.empty())
        {
            messages::internalError(aResp->res);
            return;
        }
        aResp->res.jsonValue["Owner"] = ownerStr;
        nlohmann::json::array_t allowableValues;
        allowableValues.emplace_back("Nvidia");
        allowableValues.emplace_back("OEM");
        allowableValues.emplace_back("User");
        aResp->res.jsonValue["Owner@Redfish.AllowableValues"] =
            std::move(allowableValues);
    }
    if (uuid != nullptr)
    {
        aResp->res.jsonValue["UUID"] = *uuid;
    }
    if (version != nullptr)
    {
        aResp->res.jsonValue["Version"] = *version;
    }
    if (isDefault != nullptr)
    {
        aResp->res.jsonValue["Default"] = *isDefault;
    }
}

/**
 * @brief Handles GET request for specific profile
 * @param app - crow application
 * @param req - crow request
 * @param aResp - response object
 * @param systemName - system name
 * @param profileNumber - profile number
 * @return None
 */
inline void handleGetProfileInfo(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& systemName, const std::string& profileNumber)
{
    BMCWEB_LOG_DEBUG("Start Handle get profile, profile number: {} ",
                     profileNumber);
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(aResp->res, "ComputerSystem", systemName);
        return;
    }
    aResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/SystemConfigProfile/Profiles/{}",
        BMCWEB_REDFISH_SYSTEM_URI_NAME, profileNumber);
    aResp->res.jsonValue["ProfileFile"]["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/SystemConfigProfile/Profiles/{}/ProfileFile",
        BMCWEB_REDFISH_SYSTEM_URI_NAME, profileNumber);
    aResp->res.jsonValue["Actions"]["#NvidiaSystemProfile.Activate"]
                        ["target"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/SystemConfigProfile/Profiles/{}/Actions/SystemProfile.Activate",
        BMCWEB_REDFISH_SYSTEM_URI_NAME, profileNumber);
    aResp->res.jsonValue["@odata.type"] =
        "#NvidiaSystemProfile.v1_0_0.NvidiaSystemProfile";
    aResp->res.jsonValue["Name"] = "SystemProfile";
    aResp->res.jsonValue["Id"] = profileNumber;
    dbus::utility::getAllProperties(
        profileService, profilePath + profileNumber, statusIntrf,
        [aResp,
         profileNumber](const boost::system::error_code& ec,
                        const dbus::utility::DBusPropertiesMap& properties) {
            getProfileStatusInfo(ec, properties, aResp, profileNumber);
        });
    dbus::utility::getAllProperties(
        profileService, profilePath + profileNumber, configurationIntrf,
        [aResp,
         profileNumber](const boost::system::error_code& ec,
                        const dbus::utility::DBusPropertiesMap& properties) {
            getProfileFileInfo(ec, properties, aResp, profileNumber);
        });
}

/**
 * @brief Handles GET profile Json
 * @param app - crow application
 * @param req - crow request
 * @param aResp - response object
 * @param systemName - system name
 * @param profileNumber - profile number
 * @return None
 */
inline void handleGetProfile(crow::App& app, const crow::Request& req,
                             const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                             const std::string& systemName,
                             const std::string& profileNumber)
{
    BMCWEB_LOG_DEBUG("Start handleGetProfile, profile number {} ",
                     profileNumber);
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(aResp->res, "ComputerSystem", systemName);
        return;
    }
    std::ifstream profileFile(
        profileFolder + "profile_" + profileNumber + ".json");
    if (!profileFile.good())
    {
        BMCWEB_LOG_ERROR("Profile File not exist: {}profile_{}.json",
                         profileFolder, profileNumber);
        messages::resourceNotFound(aResp->res, "Profile", profileNumber);
        return;
    }
    auto jsonProfile = nlohmann::json::parse(profileFile, nullptr, false);
    if (jsonProfile.is_discarded())
    {
        BMCWEB_LOG_ERROR("Profile file parse error.");
        messages::malformedJSON(aResp->res);
        return;
    }
    aResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/SystemConfigProfile/Profiles/{}/ProfileFile",
        BMCWEB_REDFISH_SYSTEM_URI_NAME, profileNumber);
    aResp->res.jsonValue["@odata.type"] =
        "#NvidiaSystemProfileFile.v1_0_0.NvidiaSystemProfileFile";
    aResp->res.jsonValue["Id"] = "ProfileFile";
    aResp->res.jsonValue["Name"] = "System Configuration Profile File";
    aResp->res.jsonValue["ProfileFile"] = jsonProfile;
}

inline void getProfileInfoIndex(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, int profileIndex,
    const boost::system::error_code& ec, const std::string& property)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("DBUS response error for path: {}",
                         std::to_string(profileIndex));
        messages::internalError(aResp->res);
        return;
    }
    if (property == statusPrefix + "Active")
    {
        aResp->res.jsonValue["ActiveProfileIndex"] = profileIndex;
    }
}

inline void handleGetProfilesStatusPendingList(
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error in get profiles, error: {} ",
                         ec.value());
        messages::internalError(aResp->res);
        return;
    }

    const std::vector<std::string>* activationPendingList = nullptr;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties, "ActivationPendingList",
        activationPendingList);

    if (!success)
    {
        messages::internalError(aResp->res);
        return;
    }
    nlohmann::json& pendingListObj = aResp->res.jsonValue["PendingList"];
    pendingListObj = nlohmann::json::object();
    if (!activationPendingList->empty())
    {
        pendingListObj["Activation"] = (*activationPendingList)[0];
    }
    else
    {
        pendingListObj["Activation"] = nullptr;
    }
}

inline void handleGetProfilesManagerStatus(
    const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error: {} ", ec);
        messages::internalError(aResp->res);
        return;
    }

    const uint64_t* bmcProfileVersion = nullptr;
    const std::string* factoryResetStatus = nullptr;
    const uint16_t* defaultProfileindex = nullptr;
    const uint16_t* activeProfileIndex = nullptr;
    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties, "BmcVersion",
        bmcProfileVersion, "FactoryResetStatus", factoryResetStatus,
        "DefaultProfileindex", defaultProfileindex, "ActiveProfileIndex",
        activeProfileIndex);

    if (!success)
    {
        messages::internalError(aResp->res);
        return;
    }

    // getActiveProfilesInfo(aResp);
    if (defaultProfileindex != nullptr &&
        *defaultProfileindex != invalidProfileNumber)
    {
        aResp->res.jsonValue["DefaultProfileIndex"] = *defaultProfileindex;
    }
    if (activeProfileIndex != nullptr &&
        *activeProfileIndex != invalidProfileNumber)
    {
        aResp->res.jsonValue["ActiveProfileIndex"] = *activeProfileIndex;
    }
    if (bmcProfileVersion != nullptr)
    {
        aResp->res.jsonValue["BmcProfileVersion"] = *bmcProfileVersion;
    }
    if (factoryResetStatus != nullptr)
    {
        std::string factoryResetStatusStr =
            getStringFromDbusStatus(*factoryResetStatus);
        if (factoryResetStatusStr.empty())
        {
            messages::internalError(aResp->res);
            return;
        }
        aResp->res.jsonValue["FactoryResetStatus"] = factoryResetStatusStr;
    }
}
/**
 * @brief Handles GET request profiles status
 * @param app - crow application
 * @param req - crow request
 * @param aResp - response object
 * @param systemName - system name
 * @return None
 */
inline void handleGetProfilesStatus(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& systemName)
{
    BMCWEB_LOG_DEBUG("Start handleGetProfilesStatus ");
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    if (systemName != std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME))
    {
        messages::resourceNotFound(aResp->res,
                                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME),
                                   systemName);
        return;
    }
    aResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/SystemConfigProfile/Status",
        BMCWEB_REDFISH_SYSTEM_URI_NAME);
    aResp->res.jsonValue["@odata.type"] =
        "#NvidiaSystemConfigProfileStatus.v1_0_0.NvidiaSystemConfigProfileStatus";
    aResp->res.jsonValue["Id"] = "Nvidia System Profile Status";
    aResp->res.jsonValue["Name"] = "Profiles status";
    aResp->res.jsonValue["Description"] =
        "Nvidia Profiles management information";
    dbus::utility::getAllProperties(
        profileService, profilePath + "manager", pendingListIntrf,
        [aResp](const boost::system::error_code& ec,
                const dbus::utility::DBusPropertiesMap& properties) {
            handleGetProfilesStatusPendingList(ec, properties, aResp);
        });

    dbus::utility::getAllProperties(
        profileService, profilePath + "manager", managerIntrf,
        [aResp](const boost::system::error_code& ec,
                const dbus::utility::DBusPropertiesMap& properties) {
            handleGetProfilesManagerStatus(ec, properties, aResp);
        });
}

/**
 * @brief Handles GET request for profiles URLs information
 * @param app - crow application
 * @param req - crow request
 * @param aResp - response object
 * @param systemName - system name
 * @return None
 */
inline void handleProfilesUrls(crow::App& app, const crow::Request& req,
                               const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    if (systemName != std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME))
    {
        messages::resourceNotFound(aResp->res,
                                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME),
                                   systemName);
        return;
    }
    aResp->res.jsonValue["@odata.type"] =
        "#NvidiaSystemConfigProfile.v1_0_0.NvidiaSystemConfigProfile";
    aResp->res.jsonValue["Id"] = "SystemConfigProfile";
    aResp->res.jsonValue["Name"] = "Nvidia System configuration Profile";
    aResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/SystemConfigProfile",
        BMCWEB_REDFISH_SYSTEM_URI_NAME);
    aResp->res.jsonValue["Actions"]["#NvidiaSystemConfigProfile.Update"]
                        ["target"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/SystemConfigProfile/Actions/SystemConfigProfile.Update",
        BMCWEB_REDFISH_SYSTEM_URI_NAME);
    aResp->res.jsonValue["Actions"]["#NvidiaSystemConfigProfile.FactoryReset"]
                        ["target"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/SystemConfigProfile/Actions/SystemConfigProfile.FactoryReset",
        BMCWEB_REDFISH_SYSTEM_URI_NAME);
    aResp->res.jsonValue["Status"]["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/SystemConfigProfile/Status",
        BMCWEB_REDFISH_SYSTEM_URI_NAME);
    aResp->res.jsonValue["Profiles"]["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/SystemConfigProfile/Profiles",
        BMCWEB_REDFISH_SYSTEM_URI_NAME);
    aResp->res.jsonValue["Truststore"][nvidiaProfileTruststore]
                        ["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/SystemConfigProfile/Truststore/{}",
        BMCWEB_REDFISH_SYSTEM_URI_NAME, nvidiaProfileTruststore);
    aResp->res.jsonValue["Truststore"][oemProfileTruststore]
                        ["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/SystemConfigProfile/Truststore/{}",
        BMCWEB_REDFISH_SYSTEM_URI_NAME, oemProfileTruststore);
}

inline void callbackProfileUpdate(
    const boost::system::error_code& ec, sdbusplus::message::message& msg,
    task::Payload&& payload, const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    bool isBiosUser, uint16_t profileNumber)
{
    if (ec)
    {
        const sd_bus_error* dbusError = msg.get_error();
        if (dbusError != nullptr && dbusError->name != nullptr)
        {
            if (strcmp(dbusError->name,
                       "xyz.openbmc_project.Common.Error.NotAllowed") == 0)
            {
                messages::actionNotSupported(
                    aResp->res,
                    "Invalid action, check error log for more information");
                BMCWEB_LOG_ERROR(
                    "Update method called failed - Not allowed action {}",
                    ec.what());
                return;
            }
        }

        messages::internalError(aResp->res);
        BMCWEB_LOG_ERROR("Update profile Dbus error: {}", ec.what());
        return;
    }
    BMCWEB_LOG_DEBUG("Start callbackProfileUpdate, profile number {}",
                     std::to_string(profileNumber));
    if (profileNumber == UINT16_MAX)
    {
        messages::actionNotSupported(
            aResp->res, "Invalid action, check error log for more information");
        BMCWEB_LOG_ERROR("Update method called failed ");
        return;
    }
    aResp->res.jsonValue["ProfileNumber"] = profileNumber;
    if (isBiosUser)
    {
        BMCWEB_LOG_DEBUG("Bios requested update, no task is created");
        return;
    }
    BMCWEB_LOG_DEBUG("Update Profile number: {} ",
                     std::to_string(profileNumber));
    startProfileUpdateTask(aResp, std::to_string(profileNumber),
                           std::move(payload));
}

inline void handleProfileUpdateCall(
    task::Payload&& payload, const boost::system::error_code& ec,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, bool isBiosUser,
    const std::shared_ptr<MemoryFD>& memFd)
{
    BMCWEB_LOG_DEBUG("Profile update Is bios: {}", isBiosUser);
    if (ec)
    {
        BMCWEB_LOG_ERROR("iS Bios privilege error: {}", ec.what());
        messages::internalError(aResp->res);
        return;
    }
    dbus::utility::async_method_call(
        [payload = std::move(payload), aResp,
         isBiosUser](const boost::system::error_code& ec2,
                     sdbusplus::message::message& msg,
                     const uint16_t& profileNumber) mutable {
            callbackProfileUpdate(ec2, msg, std::move(payload), aResp,
                                  isBiosUser, profileNumber);
        }, // end async_method_call handler
        profileService, profileManagerPath.str, managerIntrf, "Update",
        isBiosUser, sdbusplus::message::unix_fd(memFd->fd));
}

/**
 * @brief POST request for profile update. This will save the profile on /tmp
 * and trigger the update flow by the profile manager
 * @param app - crow application
 * @param req - crow request
 * @param aResp - response object
 * @param systemName - system name
 * @return none
 */
inline void handleProfileUpdate(crow::App& app, const crow::Request& req,
                                const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                                const std::string& systemName)
{
    BMCWEB_LOG_DEBUG("Start handleProfileUpdate");
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(aResp->res, "ComputerSystem", systemName);
        return;
    }

    auto memFd = std::make_shared<MemoryFD>();
    std::string profileStr = req.body();
    std::vector<uint8_t> profileData;
    profileData.assign(profileStr.begin(), profileStr.end());
    memFd->write(profileData);

    if (req.session == nullptr)
    {
        BMCWEB_LOG_ERROR("Session is null");
        messages::insufficientPrivilege(aResp->res);
        return;
    }
    task::Payload payload(req);
    privilege_utils::isBiosPrivilege(
        req.session->username,
        [aResp, memFd = std::move(memFd), payload = std::move(payload)](
            const boost::system::error_code& ec2, bool isBiosUser) mutable {
            handleProfileUpdateCall(std::move(payload), ec2, aResp, isBiosUser,
                                    memFd);
        }); // end  isBiosPrivilege handler
}

inline bool handleFactoryResetTask(
    boost::system::error_code ec, sdbusplus::message_t& msg,
    const std::shared_ptr<task::TaskData>& taskData)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Profile dbus error ");
        return finishProfileTask(
            taskData, "Aborted",
            messages::taskAborted(std::to_string(taskData->index)));
    }
    std::string iface;

    dbus::utility::DBusPropertiesMap propertiesChanged;
    std::vector<std::string> taskProgress;
    msg.read(iface, propertiesChanged, taskProgress);

    BMCWEB_LOG_DEBUG("Status changed on index: {}, path: {}, interface: {}",
                     std::to_string(taskData->index),
                     std::string(msg.get_path()), iface);
    if (iface != managerIntrf)
    {
        return !task::completed;
    }
    const std::string* status = nullptr;
    const uint16_t* progress = nullptr;
    if (!sdbusplus::unpackPropertiesNoThrow(
            redfish::dbus_utils::UnpackErrorPrinter(), propertiesChanged,
            "FactoryResetStatus", status, "FactoryResetProgress", progress))
    {
        taskData->messages.emplace_back(messages::internalError());
        return !task::completed;
    }

    if (status != nullptr)
    {
        return handleTaskStatus(taskData, *status);
    }
    if (progress != nullptr)
    {
        taskData->percentComplete = static_cast<int>(*progress);
        return !task::completed;
    }
    return !task::completed;
}

inline void callbackSetFactorResetProperty(
    task::Payload&& payload, const boost::system::error_code& ec,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp, bool isBiosUser)
{
    BMCWEB_LOG_DEBUG("Start, isBiosUser:  {}", isBiosUser);
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error to set factory reset: {}", ec);
        messages::internalError(aResp->res);
        return;
    }
    if (!isBiosUser)
    {
        std::string matchString =
            sdbusplus::bus::match::rules::propertiesChanged(
                profileManagerPath.str, managerIntrf);
        std::shared_ptr<task::TaskData> task = task::TaskData::createTask(
            std::bind_front(handleFactoryResetTask), matchString);
        task->startTimer(std::chrono::minutes(20));
        task->populateResp(aResp->res);
        task->payload.emplace(std::move(payload));
        BMCWEB_LOG_DEBUG("Finish create task for system factory reset ");
    }
}

inline void setProfileFactoryResetStatus(
    task::Payload&& payload, const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& factoryResetStatus, bool isBiosUser)
{
    dbus::utility::setProperty(
        profileService, profileManagerPath.str, managerIntrf,
        "FactoryResetStatus", factoryResetStatus,
        [payload = std::move(payload), aResp,
         isBiosUser](const boost::system::error_code& ec) mutable {
            callbackSetFactorResetProperty(std::move(payload), ec, aResp,
                                           isBiosUser);
        });
}

inline void callbackPatchFactoryReset(
    task::Payload&& payload, const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const boost::system::error_code& ec, const std::string& requestedStatus,
    const bool isBiosUser)
{
    BMCWEB_LOG_DEBUG("callbackPatchFactoryReset: requested status {}",
                     requestedStatus);
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error {}", ec);
        messages::internalError(aResp->res);
        return;
    }
    if (!isBiosUser)
    {
        messages::actionNotSupported(aResp->res,
                                     "None Bios User not allow to "
                                     "to patch the factory reset status");
    }
    else // Bios is true
    {
        nvidia_system_profile::ActionStatus factoryResetActionStatus =
            toActionStatus(requestedStatus);
        if ((factoryResetActionStatus ==
             nvidia_system_profile::ActionStatus::BiosStarted) ||
            (factoryResetActionStatus ==
             nvidia_system_profile::ActionStatus::BiosFinished))
        {
            setProfileFactoryResetStatus(std::move(payload), aResp,
                                         statusPrefix + requestedStatus,
                                         isBiosUser);
        }
        else
        {
            messages::actionNotSupported(aResp->res, "Bios invalid action");
        }
    }
}

/**
 * @brief Handles PATCH request for factory reset status
 * @param app - crow application
 * @param req - crow request
 * @param aResp - response object
 * @param systemName - system name
 * @return None
 */
inline void handlePatchProfilesStatus(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    if (systemName != std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME))
    {
        messages::resourceNotFound(aResp->res,
                                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME),
                                   systemName);
        return;
    }
    std::optional<std::string> factoryResetStatus;
    if (!json_util::readJsonPatch(req, aResp->res, "FactoryResetStatus",
                                  factoryResetStatus))
    {
        return;
    }

    if (factoryResetStatus == std::nullopt)
    {
        messages::propertyUnknown(aResp->res, "FactoryResetStatus");
        return;
    }
    std::string requestedStatus = *factoryResetStatus;
    if (req.session == nullptr)
    {
        BMCWEB_LOG_ERROR("Session is null");
        messages::insufficientPrivilege(aResp->res);
        return;
    }
    task::Payload payload(req);
    privilege_utils::isBiosPrivilege(
        req.session->username,
        [payload = std::move(payload), aResp, requestedStatus](
            const boost::system::error_code& ec, bool isBiosUser) mutable {
            callbackPatchFactoryReset(std::move(payload), aResp, ec,
                                      requestedStatus, isBiosUser);
        });
}

/**
 * @brief Handles factory reset request
 * @param app - crow application
 * @param req - crow request
 * @param aResp - response object
 * @param systemName - system name
 * @return None
 */
inline void handleFactoryReset(crow::App& app, const crow::Request& req,
                               const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    if (systemName != std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME))
    {
        messages::resourceNotFound(aResp->res,
                                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME),
                                   systemName);
        return;
    }

    task::Payload payload(req);
    setProfileFactoryResetStatus(std::move(payload), aResp,
                                 statusPrefix + "Start", false);
}

inline std::string getConfigFlashType(const std::string& truststoreName)
{
    if (truststoreName == nvidiaProfileTruststore)
    {
        return "xyz.openbmc_project.Profiles.Configurations.Owner.Nvidia";
    }
    if (truststoreName == oemProfileTruststore)
    {
        return "xyz.openbmc_project.Profiles.Configurations.Owner.OEM";
    }
    BMCWEB_LOG_DEBUG("Error flash type name");
    return "";
}

inline std::string getProfileServiceName(const std::string& truststoreName)
{
    if (truststoreName == nvidiaProfileTruststore)
    {
        return std::string{nvidiaProfileTruststoreService};
    }
    if (truststoreName == oemProfileTruststore)
    {
        return std::string{oemProfileTruststoreService};
    }

    BMCWEB_LOG_ERROR("Get service: Error truststore name");
    return "";
}

inline std::string getProfileTruststorePath(const std::string& truststoreName)
{
    if (truststoreName == nvidiaProfileTruststore)
    {
        return std::string{nvidiaProfileTruststorePath};
    }
    if (truststoreName == oemProfileTruststore)
    {
        return std::string{oemProfileTruststorePath};
    }

    BMCWEB_LOG_ERROR("Get Path: Error truststore name");
    return "";
}

inline bool isTruststoreSupported(const std::string& truststoreName)
{
    const auto* it = std::find(profileTruststores.begin(),
                               profileTruststores.end(), truststoreName);
    return it != profileTruststores.end();
}

inline void handleGetProfileTruststoreCollection(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& truststoreName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (systemName != std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME))
    {
        messages::resourceNotFound(asyncResp->res,
                                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME),
                                   systemName);
        return;
    }
    if (!isTruststoreSupported(truststoreName))
    {
        messages::queryNotSupportedOnResource(asyncResp->res);
        BMCWEB_LOG_ERROR("ERROR: trust store type is not supported: {}.",
                         truststoreName);
        return;
    }
    boost::urls::url url(
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Oem/Nvidia/SystemConfigProfile/Truststore/" + truststoreName);
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/SystemConfigProfile/Truststore/{}",
        BMCWEB_REDFISH_SYSTEM_URI_NAME, truststoreName);
    asyncResp->res.jsonValue["@odata.type"] =
        "#CertificateCollection.CertificateCollection";
    asyncResp->res.jsonValue["Name"] =
        "Profile Truststore Certificate Collection";
    asyncResp->res.jsonValue["@Redfish.SupportedCertificates"] = {"PEM"};

    const std::array<std::string_view, 1> interfaces{
        "xyz.openbmc_project.Certs.Certificate"};
    std::string path = getProfileTruststorePath(truststoreName);
    if (path.empty())
    {
        messages::unrecognizedRequestBody(asyncResp->res);
        return;
    }
    redfish::collection_util::getCollectionMembers(asyncResp, url, interfaces,
                                                   path);
}

inline void handleGetProfileCaCertificate(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& truststoreName,
    const std::string& certId)
{
    BMCWEB_LOG_DEBUG("Start handleGetProfileCaCertificate");
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (systemName != std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME))
    {
        messages::resourceNotFound(asyncResp->res,
                                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME),
                                   systemName);
        return;
    }
    if (!isTruststoreSupported(truststoreName))
    {
        messages::queryNotSupportedOnResource(asyncResp->res);
        BMCWEB_LOG_ERROR("ERROR: trust store type is not supported {}",
                         truststoreName);
        return;
    }

    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/SystemConfigProfile/Truststore/{}/{}",
        BMCWEB_REDFISH_SYSTEM_URI_NAME, truststoreName, certId);
    asyncResp->res.jsonValue["@odata.type"] = "#Certificate.v1_7_0.Certificate";
    asyncResp->res.jsonValue["Id"] = certId;
    asyncResp->res.jsonValue["Name"] = "Profiles Certificate";
    std::string path = getProfileTruststorePath(truststoreName);
    if (path.empty())
    {
        messages::unrecognizedRequestBody(asyncResp->res);
        return;
    }
    path += ("/" + certId);
    std::string service = getProfileServiceName(truststoreName);
    if (service.empty())
    {
        messages::unrecognizedRequestBody(asyncResp->res);
        return;
    }
    BMCWEB_LOG_DEBUG("getCertificateProperties Path={} certId={} certURl={}",
                     path, service, certs::certPropIntf);
    dbus::utility::getAllProperties(
        service, path, certs::certPropIntf,
        [asyncResp,
         certId](const boost::system::error_code& ec,
                 const dbus::utility::DBusPropertiesMap& propertiesList) {
            bluefield::populateTruststoreCertificateInfo(ec, propertiesList,
                                                         asyncResp, certId);
        });
}

static ProfileOwner getOwnerFromIssuer(const std::string& issuer)
{
    BMCWEB_LOG_DEBUG("Issuer: {}", issuer);
    if (issuer.find("NVIDIA") != std::string::npos)
    {
        return ProfileOwner::Nvidia;
    }
    return ProfileOwner::Invalid;
}

inline void installCACerthandler(
    const boost::system::error_code& ec, const std::string& objectPath,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::shared_ptr<CertificateFile>& certFile,
    const std::string& truststoreName, const std::string& service)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Install cert DBUS response error: {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }
    sdbusplus::message::object_path path(objectPath);
    std::string certId = path.filename();
    const boost::urls::url certURL = boost::urls::format(
        "/redfish/v1/Systems/{}/Oem/Nvidia/SystemConfigProfile/Truststore/{}",
        std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME), truststoreName);
    getCertificateProperties(asyncResp, objectPath, service, certId, certURL,
                             "TrustStore Certificate");
    BMCWEB_LOG_DEBUG("Profile TrustStore certificate install file={}",
                     certFile->getCertFilePath());
}

static void installCACert(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& certHttpBody,
                          const std::string& truststoreName)
{
    std::shared_ptr<CertificateFile> certFile =
        std::make_shared<CertificateFile>(certHttpBody);
    std::string service = getProfileServiceName(truststoreName);
    if (service.empty())
    {
        messages::unrecognizedRequestBody(asyncResp->res);
        return;
    }
    std::string path = getProfileTruststorePath(truststoreName);
    if (path.empty())
    {
        messages::unrecognizedRequestBody(asyncResp->res);
        return;
    }
    dbus::utility::async_method_call(
        [asyncResp, certFile, truststoreName,
         service](const boost::system::error_code& ec,
                  const std::string& objectPath) {
            installCACerthandler(ec, objectPath, asyncResp, certFile,
                                 truststoreName, service);
        },
        service, path, "xyz.openbmc_project.Certs.Install", "Install",
        certFile->getCertFilePath());
}

static ProfileOwner getCertOwner(const std::string& certStr)
{
    BIO* bio =
        BIO_new_mem_buf(certStr.data(), static_cast<int>(certStr.size()));
    if (bio == nullptr)
    {
        BMCWEB_LOG_DEBUG("Error creating BIO ");
        return ProfileOwner::Invalid;
    }
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);

    if (cert == nullptr)
    {
        BIO_free(bio);
        BMCWEB_LOG_ERROR("Error reading certificate from string ");
        return ProfileOwner::Invalid;
    }
    BIO_free(bio);
    X509_NAME* issuerName = X509_get_issuer_name(cert);
    if (issuerName == nullptr)
    {
        X509_free(cert);
        BMCWEB_LOG_ERROR("Error getting issuer name from certificate");
        return ProfileOwner::Invalid;
    }
    static const int maxKeySize = 4096;
    std::array<char, maxKeySize> issuerBuffer{};
    BIO* issuerBio = BIO_new(BIO_s_mem());
    X509_NAME_print_ex(issuerBio, issuerName, 0, XN_FLAG_SEP_COMMA_PLUS);
    BIO_read(issuerBio, issuerBuffer.data(), maxKeySize);
    X509_free(cert);
    BIO_free(issuerBio);
    return getOwnerFromIssuer(std::string(issuerBuffer.data()));
}

inline void handlePostCertificateProfile(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& truststoreName, const std::string& certHttpBody,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreePathsResponse& objects)
{
    if (ec == boost::system::errc::io_error)
    {
        BMCWEB_LOG_ERROR("DBUS io error error");
        asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
        asyncResp->res.jsonValue["Members@odata.count"] = 0;
        return;
    }
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error {}", ec.value());
        messages::internalError(asyncResp->res);
        return;
    }
    if (!isTruststoreSupported(truststoreName))
    {
        BMCWEB_LOG_ERROR("ERROR: trust store type is not supported: {}.",
                         truststoreName);
        messages::queryNotSupportedOnResource(asyncResp->res);
        return;
    }

    if ((objects.size() == 1))
    {
        BMCWEB_LOG_ERROR("Max certificate are installed on trust store {}",
                         objects[0]);
        messages::actionNotSupported(
            asyncResp->res, "Max certificate are install on trust store");
        return;
    }
    nlohmann::json& members = asyncResp->res.jsonValue["Members"];
    members = nlohmann::json::array();
    std::string path = getProfileTruststorePath(truststoreName);
    if (path.empty())
    {
        messages::unrecognizedRequestBody(asyncResp->res);
        return;
    }
    std::string service = getProfileServiceName(truststoreName);
    if (service.empty())
    {
        messages::unrecognizedRequestBody(asyncResp->res);
        return;
    }
    if (truststoreName == nvidiaProfileTruststore)
    {
        ProfileOwner owner = getCertOwner(certHttpBody);
        if (owner != ProfileOwner::Nvidia)
        {
            messages::actionNotSupported(
                asyncResp->res, "Issuer should be Nvidia on Nvidia truststore");
            BMCWEB_LOG_ERROR(
                "ERROR: Issuer should be Nvidia on Nvidia truststore.");
            return;
        }
    }
    installCACert(asyncResp, certHttpBody, truststoreName);
}

inline void handleProfileCaCertificatePost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& truststoreName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (systemName != std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME))
    {
        messages::resourceNotFound(asyncResp->res,
                                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME),
                                   systemName);
        return;
    }
    BMCWEB_LOG_DEBUG("start handleProfileCaCertificatePost on {} .",
                     truststoreName);
    std::string certHttpBody = getCertificateFromReqBody(asyncResp, req);

    if (certHttpBody.empty())
    {
        BMCWEB_LOG_ERROR("Cannot get certificate from request body.");
        messages::unrecognizedRequestBody(asyncResp->res);
        return;
    }

    if (!isTruststoreSupported(truststoreName))
    {
        messages::queryNotSupportedOnResource(asyncResp->res);
        BMCWEB_LOG_ERROR("ERROR: trust store type is not supported: {}.",
                         truststoreName);
        return;
    }
    std::array<std::string_view, 1> interfaces = {certs::certPropIntf};
    std::span<const std::string_view> spanInterfaces(interfaces);
    dbus::utility::getSubTreePaths(
        getProfileTruststorePath(truststoreName), 0, spanInterfaces,
        std::bind_front(handlePostCertificateProfile, asyncResp, truststoreName,
                        certHttpBody));
}

/**
 * @brief Handles activate or delete profile request
 * @param app - crow application
 * @param req - crow request
 * @param aResp - response object
 * @param systemName - system name
 * @param profileNumber - profile number
 * @return None
 */
inline void handleProfileActionRequest(
    crow::App& app, const std::string& action, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& systemName, const std::string& profileNumber)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    if (systemName != std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME))
    {
        messages::resourceNotFound(aResp->res,
                                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME),
                                   systemName);
        return;
    }
    task::Payload payload(req);
    setProfileProperty(std::move(payload), aResp, profileNumber, statusIntrf,
                       action, statusPrefix + "Start");
}
} // namespace profiles

inline void requestRoutesProfiles(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/Oem/Nvidia/SystemConfigProfile/")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(profiles::handleProfilesUrls, std::ref(app)));

    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/Oem/Nvidia/SystemConfigProfile/Status/")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(profiles::handleGetProfilesStatus, std::ref(app)));

    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/Oem/Nvidia/SystemConfigProfile/Status/")
        .privileges(redfish::privileges::patchComputerSystem)
        .methods(boost::beast::http::verb::patch)(std::bind_front(
            profiles::handlePatchProfilesStatus, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/SystemConfigProfile/Profiles/")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            profiles::handleGetProfilesCollection, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/SystemConfigProfile/Profiles/<str>/")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(profiles::handleGetProfileInfo, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/SystemConfigProfile/Profiles/<str>/")
        .privileges(redfish::privileges::patchComputerSystem)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(profiles::handlePatchProfile, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/SystemConfigProfile/Profiles/<str>/")
        .privileges(redfish::privileges::deleteComputerSystem)
        .methods(boost::beast::http::verb::delete_)(
            std::bind_front(profiles::handleProfileActionRequest, std::ref(app),
                            "DeleteProfile"));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/SystemConfigProfile/Profiles/<str>/Actions/SystemProfile.Activate/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(profiles::handleProfileActionRequest, std::ref(app),
                            "ActivateProfile"));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/SystemConfigProfile/Profiles/<str>/ProfileFile/")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(profiles::handleGetProfile, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/SystemConfigProfile/Actions/SystemConfigProfile.Update/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(profiles::handleProfileUpdate, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/SystemConfigProfile/Actions/SystemConfigProfile.FactoryReset/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(profiles::handleFactoryReset, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/SystemConfigProfile/Truststore/<str>")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            profiles::handleGetProfileTruststoreCollection, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/SystemConfigProfile/Truststore/<str>/<str>")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            profiles::handleGetProfileCaCertificate, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Oem/Nvidia/SystemConfigProfile/Truststore/<str>")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            profiles::handleProfileCaCertificatePost, std::ref(app)));
}
} // namespace redfish
