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
#include "nlohmann/json.hpp"
#include "task.hpp"

#include <app.hpp>
#include <dbus_utility.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <utils/json_utils.hpp>
#include <utils/privilege_utils.hpp>

#include <cstdint> //max uint
#include <fstream>
#include <iostream>

namespace redfish
{
namespace profiles
{
const std::string profileService = "xyz.openbmc_project.Profile.Manager";
const std::string profilePath =
    "/xyz/openbmc_project/control/system/Card1/profile/";
const std::string profileManagerPath =
    "/xyz/openbmc_project/control/system/Card1/profile/manager";
const std::string profileManagerInterface =
    "xyz.openbmc_project.Profiles.Manager";
const std::string statusIntrf = "xyz.openbmc_project.Profiles.Statuses";
const std::string pendingListIntrf =
    "xyz.openbmc_project.Profiles.PendingLists";
const std::string managerIntrf = "xyz.openbmc_project.Profiles.Manager";
const std::string configurationIntrf =
    "xyz.openbmc_project.Profiles.Configurations";
const std::string tmpFilePath = "/tmp/profile_tmp.json";
const std::string profileFolder = "/etc/profile-manager/";
const std::string statusPrefix =
    "xyz.openbmc_project.Profiles.Statuses.Status.";
const uint8_t invalidProfileNumber = 0xFF;

/*
Get the latest word after the last dot in the string.
For example, if the input is
"xyz.openbmc_project.Profiles.Statuses.Status.Failed", it will return Failed
*/
inline std::string getLastWordAfterDot(const std::string& input)
{
    size_t lastDotPos = input.find_last_of('.');
    if (lastDotPos != std::string::npos)
    {
        return input.substr(lastDotPos + 1);
    }
    BMCWEB_LOG_ERROR("Parsing error, string {}", input);
    return "";
}

inline void setProfileProperty(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const std::string& profileNumber,
                               const std::string& interface,
                               const std::string& property,
                               const std::string& value)
{
    sdbusplus::asio::setProperty(
        *crow::connections::systemBus, profileService,
        profilePath + profileNumber, interface, property, value,
        [aResp, property, value](const boost::system::error_code ec) {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "DBUS response error on profile setProperty: {}, value: {}, error: {}",
                property, value, ec);
            messages::internalError(aResp->res);
            return;
        }
    });
}

/**
 * @brief Handles GET request for Profile collection
 * @param app - crow application
 * @param req - crow request
 * @param aResp - response object
 * @return None
 */
inline void
    handleGetProfilesList(crow::App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    BMCWEB_LOG_DEBUG("Start get profile list");

    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec,
                const dbus::utility::MapperGetSubTreePathsResponse& objects) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error in get profiles, error: {} ",
                             ec.value());
            messages::internalError(aResp->res);
            return;
        }
        nlohmann::json& members = aResp->res.jsonValue["Members"];
        members = nlohmann::json::array();
        std::vector<std::string> pathNames;
        for (const auto& object : objects)
        {
            sdbusplus::message::object_path path(object);
            std::string profile_number = path.filename();
            if (profile_number.empty())
            {
                continue;
            }
            std::string newPath =
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                "/Oem/Nvidia/SystemConfigurationProfile/List/";
            newPath += profile_number;
            nlohmann::json::object_t member;
            member["@odata.id"] = std::move(newPath);
            members.push_back(std::move(member));
            BMCWEB_LOG_DEBUG("Profile: {}", profile_number);
        }
        aResp->res.jsonValue["Members@odata.count"] = members.size();
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths", profilePath, 0,
        std::array<const char*, 1>{"xyz.openbmc_project.Profiles.Statuses"});
    aResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Oem/Nvidia/SystemConfigurationProfile/List";
    aResp->res.jsonValue["@odata.type"] =
        "#NvidiaSystemProfileCollection.v1_0_0.NvidiaSystemProfileList";
}

/**
 * @brief Handles set property due to PATCH request for Profile status
 * @param aResp - response object
 * @param profileNumber - profile number
 * @param isBios - true if the profile uploaded from the Bios
 * @param property - property name
 * @param status - status value
 * @return None
 */
inline void
    handlePatchSetProfileStatus(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                                const std::string& profileNumber, bool isBios,
                                const std::string& property,
                                const std::string& status)
{
    const std::vector<std::string> allowedUefiValues = {"BiosStarted",
                                                        "BiosFinished"};
    const std::vector<std::string> allowedUserValues = {"Failed"};
    std::vector<std::string> allowedValues = isBios ? allowedUefiValues
                                                    : allowedUserValues;
    std::string user = isBios ? "Bios" : "User";

    auto it = std::find(allowedValues.begin(), allowedValues.end(), status);
    if (it == allowedValues.end())
    {
        messages::operationNotAllowed(
            aResp->res, user + " can not change status to " + status);
        return;
    }
    setProfileProperty(aResp, profileNumber, statusIntrf, property,
                       statusPrefix + status);
}

/**
 * @brief Handles PATCH request for Profile status
 * Can be pathed for
 * ActivateProfile and DeleteProfile - use can only path failed and Bios can
 * only patch BiosStarted and BiosFinished.
 * Description -string that can be patched all users
 * @param app - crow application
 * @param req - crow request
 * @param aResp - response object
 * @param isBios - true if the profile uploaded from the Bios
 * @param property - property name
 * @param status - status value
 * @return None
 */
inline void handlePatchProfile(crow::App& app, const crow::Request& req,
                               const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const std::string& profileNumber)
{
    BMCWEB_LOG_DEBUG("Start handlePatchProfile, profile number {}",
                     profileNumber);
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    std::optional<std::string> activateStatus;
    std::optional<std::string> deleteStatus;
    std::optional<std::string> description;
    if (!json_util::readJsonPatch(req, aResp->res, "Status/ActivateProfile",
                                  activateStatus, "Status/DeleteProfile",
                                  deleteStatus, "Description", description))
    {
        BMCWEB_LOG_ERROR("Not a Valid JSON");
        return;
    }

    if (activateStatus || deleteStatus)
    {
        privilege_utils::isBiosPrivilege(
            req, [req, aResp, activateStatus, deleteStatus, profileNumber](
                     const boost::system::error_code ec, const bool isBios) {
            std::vector<std::string> allowedUefiValues = {"BiosStarted",
                                                          "BiosFinished"};
            std::vector<std::string> allowedUserValues = {"Failed"};
            if (ec)
            {
                messages::internalError(aResp->res);
                return;
            }

            if (activateStatus)
            {
                handlePatchSetProfileStatus(aResp, profileNumber, isBios,
                                            "ActivateProfile", *activateStatus);
            }
            else if (deleteStatus)
            {
                handlePatchSetProfileStatus(aResp, profileNumber, isBios,
                                            "DeleteProfile", *deleteStatus);
            }
        });
    }
    if (description)
    {
        BMCWEB_LOG_DEBUG("Profile description: {} ", *description);
        setProfileProperty(aResp, profileNumber, configurationIntrf,
                           "Description", *description);
    }
    aResp->res.result(boost::beast::http::status::no_content);
}

/**
 * @brief Handles GET request for specific profile
 * @param app - crow application
 * @param req - crow request
 * @param aResp - response object
 * @param profileNumber - profile number
 * @return None
 */
inline void
    handleGetProfileInfo(crow::App& app, const crow::Request& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& profileNumber)
{
    BMCWEB_LOG_DEBUG("Start Handle get profile, profile number: {} ",
                     profileNumber);
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    aResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Oem/Nvidia/SystemConfigurationProfile/List/" + profileNumber;
    aResp->res.jsonValue["@odata.type"] =
        "#NvidiaSystemProfileInfo.v1_0_0.NvidiaSystemProfileInfo";
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, profileService,
        profilePath + profileNumber, statusIntrf,
        [aResp,
         profileNumber](const boost::system::error_code ec,
                        const dbus::utility::DBusPropertiesMap& properties) {
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
            dbus_utils::UnpackErrorPrinter(), properties, "AddProfile",
            addStatus, "ActivateProfile", activationStatus, "DeleteProfile",
            deleteStatus);
        if (!success)
        {
            messages::internalError(aResp->res);
            return;
        }
        aResp->res.jsonValue["Status"]["AddProfile"] =
            getLastWordAfterDot(*addStatus);
        aResp->res.jsonValue["Status"]["ActivateProfile"] =
            getLastWordAfterDot(*activationStatus);
        aResp->res.jsonValue["Status"]["DeleteProfile"] =
            getLastWordAfterDot(*deleteStatus);
    });
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, profileService,
        profilePath + profileNumber, configurationIntrf,
        [aResp,
         profileNumber](const boost::system::error_code ec,
                        const dbus::utility::DBusPropertiesMap& properties) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error: {} ", ec);
            messages::resourceNotFound(aResp->res, "Profile", profileNumber);
            return;
        }
        const std::string* description = nullptr;
        const std::string* owner = nullptr;
        const std::string* uuid = nullptr;
        const uint64_t* version = nullptr;
        const bool* isDefault = nullptr;
        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), properties, "Description",
            description, "Owner", owner, "UUID", uuid, "Version", version,
            "IsDefault", isDefault);

        if (!success)
        {
            messages::internalError(aResp->res);
            return;
        }
        aResp->res.jsonValue["Description"] = *description;
        aResp->res.jsonValue["Owner"] = *owner;
        aResp->res.jsonValue["UUID"] = *uuid;
        aResp->res.jsonValue["Version"] = *version;
        aResp->res.jsonValue["Default"] = *isDefault;
    });
    aResp->res.jsonValue["Profile"]["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Oem/Nvidia/SystemConfigurationProfile/List/" + profileNumber +
        "/ProfileJson";
}

/**
 * @brief Handles GET profile Json
 * @param app - crow application
 * @param req - crow request
 * @param aResp - response object
 * @param profileNumber - profile number
 * @return None
 */
inline void handleGetProfile(crow::App& app, const crow::Request& req,
                             const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                             const std::string& profileNumber)
{
    BMCWEB_LOG_DEBUG("Start handleGetProfile, profile number {} ",
                     profileNumber);
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }

    std::ifstream profileFile(profileFolder + "profile_" + profileNumber +
                              ".json");
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
    aResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Oem/Nvidia/SystemConfigurationProfile/List/" + profileNumber +
        "/ProfileJson";
    aResp->res.jsonValue["@odata.type"] =
        "#NvidiaSystemProfile.v1_0_0.NvidiaSystemProfile";
    aResp->res.jsonValue["Profile"] = jsonProfile;
}

/**
 * @brief Handles GET request profiles status
 * @param app - crow application
 * @param req - crow request
 * @param aResp - response object
 * @return None
 */
inline void
    handleGetProfilesStatus(crow::App& app, const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    BMCWEB_LOG_DEBUG("Start handleGetProfilesStatus ");
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    aResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Oem/Nvidia/SystemConfigurationProfile/Status";
    aResp->res.jsonValue["@odata.type"] =
        "#NvidiaSystemProfileStatus.v1_0_0.NvidiaSystemProfileStatus";
    aResp->res.jsonValue["Name"] = "Profiles status";
    aResp->res.jsonValue["Description"] =
        "Nvidia Profiles management information";
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, profileService, profilePath + "manager",
        pendingListIntrf,
        [aResp](const boost::system::error_code ec,
                const dbus::utility::DBusPropertiesMap& properties) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
            messages::internalError(aResp->res);
            return;
        }

        const std::vector<std::string>* activationPendingList = nullptr;
        const std::vector<std::string>* deletePendingList = nullptr;
        const std::vector<std::string>* addPendingList = nullptr;

        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), properties,
            "ActivationPendingList", activationPendingList, "DeletePendingList",
            deletePendingList, "AddPendingList", addPendingList);

        if (!success)
        {
            messages::internalError(aResp->res);
            return;
        }
        nlohmann::json& pendingListObj = aResp->res.jsonValue["PendingList"];
        pendingListObj = nlohmann::json::object();
        if (!activationPendingList->empty())
        {
            pendingListObj["Activation"] = *activationPendingList;
        }
        if (!deletePendingList->empty())
        {
            pendingListObj["Delete"] = *deletePendingList;
        }
        if (!addPendingList->empty())
        {
            pendingListObj["Add"] = *addPendingList;
        }
    });

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, profileService, profilePath + "manager",
        managerIntrf,
        [aResp](const boost::system::error_code ec,
                const dbus::utility::DBusPropertiesMap& properties) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error: {} ", ec);
            messages::internalError(aResp->res);
            return;
        }

        const uint8_t* activeProfileNumber = nullptr;
        const uint64_t* bmcProfileVersion = nullptr;
        const uint8_t* defaultProfileNumber = nullptr;
        const std::string* factoryResetStatus = nullptr;
        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), properties, "ActiveProfileNumber",
            activeProfileNumber, "BmcVersion", bmcProfileVersion,
            "DefaultProfileNumber", defaultProfileNumber, "FactoryResetStatus",
            factoryResetStatus);

        if (!success)
        {
            messages::internalError(aResp->res);
            return;
        }

        auto assignedIfValid = [aResp](int profileNumber, std::string str) {
            if (profileNumber != invalidProfileNumber)
            {
                aResp->res.jsonValue[str] = profileNumber;
            }
        };
        assignedIfValid(*activeProfileNumber, "ActiveProfileNumber");
        assignedIfValid(*defaultProfileNumber, "DefaultProfileNumber");
        aResp->res.jsonValue["DefaultNvidiaNumber"] = 0;
        aResp->res.jsonValue["BmcProfileVersion"] = *bmcProfileVersion;
        aResp->res.jsonValue["FactoryResetStatus"] =
            getLastWordAfterDot(*factoryResetStatus);
    });
}

/**
 * @brief Handles GET request for profiles URLs information
 * @param app - crow application
 * @param req - crow request
 * @param aResp - response object
 * @return None
 */
inline void handleProfilesUrls(crow::App& app, const crow::Request& req,
                               const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }
    aResp->res.jsonValue["@odata.type"] =
        "#NvidiaSystemConfigurationProfile.v1_0_0.NvidiaSystemConfigurationProfile";
    aResp->res.jsonValue["Name"] = "Nvidia System configuration Profile";
    aResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Oem/Nvidia/SystemConfigurationProfile";
    aResp->res.jsonValue["Actions"]["#Profile.Upload"]["Target"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Oem/Nvidia/SystemConfigurationProfile/Actions/Profile.Update/";
    aResp->res.jsonValue["Status"]["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Oem/Nvidia/SystemConfigurationProfile/Status";
    aResp->res.jsonValue["List"]["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Oem/Nvidia/SystemConfigurationProfile/List";
}

/**
 * @brief Finish the profile task
 * @param taskData - task data object
 * @param  state - profile state
 * @param messages - json messages
 * @param messagesStr - messages string
 * @return bool -always true - task is completed
 */
inline bool finishProfileTask(const std::shared_ptr<task::TaskData>& taskData,
                              std::string_view state, nlohmann::json messages,
                              std::string_view messagesStr)
{
    taskData->timer.cancel();
    taskData->finishTask();
    boost::asio::post(crow::connections::systemBus->get_io_context(),
                      [taskData] { taskData->match.reset(); });
    taskData->state = state;
    taskData->messages.emplace_back(messages);
    taskData->messages.emplace_back(messagesStr);
    return task::completed;
}

/**
 * @brief handle new task status
 * @param taskData - task data object
 * @param action - the profile action that changed the status
 * @param fullStatus - full status string
 * @param  profileNumber - profile number
 * @return bool - is task completed or not
 */
inline bool handleTaskStatus(const std::shared_ptr<task::TaskData>& taskData,
                             std::string action, std::string fullStatus,
                             uint16_t profileNumber)
{
    // {task status, progress percent}
    const std::unordered_map<std::string, int> taskNotCompleted = {
        {"Start", 0},              // Start update of a profile
        {"StartBios", 0},          // Start update of a by Bios
        {"StartVerification", 20}, // Start Verification of profile by BMC
        {"ProfileSaved", 30},      // Profile Saved by BMC
        {"PendingBios", 40},       // Pending Bios to complete update of profile
        {"BiosStarted", 50},       // Bios Started update of profile
        {"BiosFinished", 60},      // Bios Finished update of profile
        {"BmcStarted", 80}};       // Bmc start last stage of update of profile
    const std::vector<std::string> taskCompleted = {"None", "Active"};
    std::string index = std::to_string(taskData->index);

    size_t lastDotPosition = fullStatus.find_last_of('.');
    std::string status;
    if (lastDotPosition != std::string::npos)
    {
        status = fullStatus.substr(lastDotPosition + 1);
        BMCWEB_LOG_DEBUG("status: {}", status);
    }
    else
    {
        BMCWEB_LOG_ERROR("Can not parse the status");
        return finishProfileTask(taskData, "Aborted", messages::internalError(),
                                 action + " Profile manager failed");
    }

    auto statusNotCompleted = taskNotCompleted.find(status);
    if (statusNotCompleted != taskNotCompleted.end())
    {
        taskData->percentComplete = statusNotCompleted->second;
        taskData->state = status;
        taskData->messages.emplace_back(messages::taskProgressChanged(
            index, static_cast<size_t>(statusNotCompleted->second)));
        return !task::completed;
    }
    else if (std::find(taskCompleted.begin(), taskCompleted.end(), status) !=
             taskCompleted.end())
    {
        taskData->percentComplete = 100;
        return finishProfileTask(taskData, "Completed",
                                 messages::taskCompletedOK(index),
                                 "Profile " + std::to_string(profileNumber) +
                                     " " + action + " completed");
    }
    else if (status == "Failed")
    {
        return finishProfileTask(taskData, "Failed",
                                 messages::taskAborted(status),
                                 "Profile " + std::to_string(profileNumber) +
                                     " " + action + " failed");
    }
    BMCWEB_LOG_ERROR("No status is not found");
    return finishProfileTask(taskData, "Aborted", messages::internalError(),
                             "No status is not found");
}

/**
 * @brief save profile file in file system
 * @param aResp - response object
 * @param profile - profile json object
 * @return bool - true if the file is saved successfully
 */
inline bool saveProfileFile(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            std::optional<nlohmann::json> profile)
{
    if (profile == std::nullopt || (*profile)["ProfileFile"].is_null())
    {
        BMCWEB_LOG_ERROR("Missing profile file");
        messages::propertyMissing(aResp->res, "Failed to load profile file");
        return false;
    }
    BMCWEB_LOG_DEBUG("Writing profile file to {}", tmpFilePath);
    std::ofstream out(tmpFilePath, std::ofstream::out | std::ofstream::binary |
                                       std::ofstream::trunc);
    if (!out.is_open())
    {
        messages::internalError(aResp->res);
        return false;
    }

    out << (*profile)["ProfileFile"].dump();
    out.close();
    BMCWEB_LOG_DEBUG("file upload complete!!");
    return true;
}

/**
 * @brief POST request for profile update. This will save the profile on /tmp
 * and trigger the update flow by the profile manager
 * @param app - crow application
 * @param req - crow request
 * @param aResp - response object
 * @return none
 */
inline void handleProfileUpdate(crow::App& app, const crow::Request& req,
                                const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    BMCWEB_LOG_DEBUG("Start handleProfileUpdate");
    if (!redfish::setUpRedfishRoute(app, req, aResp))
    {
        return;
    }

    std::optional<nlohmann::json> profile =
        json_util::readJsonPatchHelper(req, aResp->res);
    if (!saveProfileFile(aResp, profile))
    {
        BMCWEB_LOG_ERROR("Failed to save profile");
        return;
    }

    privilege_utils::isBiosPrivilege(
        req,
        [req, aResp](const boost::system::error_code ec, const bool isBios) {
        if (ec)
        {
            messages::internalError(aResp->res);
            return;
        }
        BMCWEB_LOG_DEBUG("Is bios: {}", std::to_string(isBios));
        crow::connections::systemBus->async_method_call(
            [req, aResp, isBios](const boost::system::error_code ec,
                                 const uint16_t& profileNumber) {
            if (ec)
            {
                messages::internalError(aResp->res);
                BMCWEB_LOG_ERROR("Update profile Dbus error: {}", ec.what());
                return;
            }
            if (profileNumber == UINT16_MAX)
            {
                messages::actionNotSupported(
                    aResp->res,
                    "Invalid action, check error log for more information");
                BMCWEB_LOG_ERROR("Update method called failed ");
                return;
            }
            aResp->res.jsonValue["ProfileNumber"] = profileNumber;
            if (isBios)
            {
                BMCWEB_LOG_DEBUG("Bios requested update, no task is created");
                return;
            }
            BMCWEB_LOG_DEBUG("Update Profile number: {} ",
                             std::to_string(profileNumber));
            std::string matchString =
                "type='signal',interface='org.freedesktop.DBus.Properties',"
                "member='PropertiesChanged',path='" +
                profilePath + std::to_string(profileNumber) + "'";
            std::shared_ptr<task::TaskData> task = task::TaskData::createTask(
                [profileNumber](
                    boost::system::error_code ec, sdbusplus::message_t& msg,
                    const std::shared_ptr<task::TaskData>& taskData) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("Profile dbus error ");
                    return finishProfileTask(taskData, "Aborted",
                                             messages::internalError(),
                                             " More than one changed property");
                }
                std::string iface;
                boost::container::flat_map<std::string,
                                           dbus::utility::DbusVariantType>
                    values;
                msg.read(iface, values);
                BMCWEB_LOG_DEBUG(
                    "Status changed on index: {}, path: {}, interface: {}",
                    std::to_string(taskData->index),
                    std::string(msg.get_path()), iface);
                if (iface != "xyz.openbmc_project.Profiles.Statuses")
                {
                    return !task::completed;
                }
                const auto& action = values.begin()->first;
                const auto* status =
                    std::get_if<std::string>(&values.begin()->second);
                return handleTaskStatus(taskData, action, *status,
                                        profileNumber);
            },
                matchString); // end create task
            task->startTimer(std::chrono::minutes(60));
            task->populateResp(aResp->res);
            task->payload.emplace(req);
            BMCWEB_LOG_DEBUG("Finish create task, profile number {}  ",
                             std::to_string(profileNumber));
        }, // end async_method_call handler
            profileService, profileManagerPath, profileManagerInterface,
            "Update", isBios);
    }); // end  isBiosPrivilege handler
}

} // namespace profiles
inline void requestRoutesProfiles(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/Oem/Nvidia/SystemConfigurationProfile/")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(profiles::handleProfilesUrls, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/Oem/Nvidia/SystemConfigurationProfile/Status/")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(profiles::handleGetProfilesStatus, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/Oem/Nvidia/SystemConfigurationProfile/List/")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(profiles::handleGetProfilesList, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/Oem/Nvidia/SystemConfigurationProfile/List/<str>/")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(profiles::handleGetProfileInfo, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/Oem/Nvidia/SystemConfigurationProfile/List/<str>/")
        .privileges(redfish::privileges::patchComputerSystem)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(profiles::handlePatchProfile, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/Oem/Nvidia/SystemConfigurationProfile/List/<str>/ProfileJson")
        .privileges(redfish::privileges::getComputerSystem)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(profiles::handleGetProfile, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/Oem/Nvidia/SystemConfigurationProfile/Actions/Profile.Update/")
        .privileges(redfish::privileges::postComputerSystem)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(profiles::handleProfileUpdate, std::ref(app)));
}
} // namespace redfish
