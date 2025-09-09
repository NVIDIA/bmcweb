/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION &
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

#include "app.hpp"
#include "dbus_utility.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "error_messages.hpp"
#include "task.hpp"
#include "utils/json_utils.hpp"
#include "logging.hpp"

#include <map>

namespace redfish
{

// task uri for long-run drive operation
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static std::map<std::string, std::string> taskUri;

inline std::optional<std::string> convertDriveFormFactor(
    const std::string& formFactor)
{
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.Drive3_5")
    {
        return "Drive3_5";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.Drive2_5")
    {
        return "Drive2_5";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.EDSFF_1U_Long")
    {
        return "EDSFF_1U_Long";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.EDSFF_1U_Short")
    {
        return "EDSFF_1U_Short";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.EDSFF_E3_Short")
    {
        return "EDSFF_E3_Short";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.EDSFF_E3_Long")
    {
        return "EDSFF_E3_Long";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.M2_2230")
    {
        return "M2_2230";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.M2_2242")
    {
        return "M2_2242";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.M2_2260")
    {
        return "M2_2260";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.M2_2280")
    {
        return "M2_2280";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.M2_22110")
    {
        return "M2_22110";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.U2")
    {
        return "U2";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.PCIeSlotFullLength")
    {
        return "PCIeSlotFullLength";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.PCIeSlotLowProfile")
    {
        return "PCIeSlotLowProfile";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.PCIeHalfLength")
    {
        return "PCIeHalfLength";
    }
    if (formFactor ==
        "xyz.openbmc_project.Inventory.Item.Drive.DriveFormFactor.OEM")
    {
        return "OEM";
    }

    return std::nullopt;
}

inline std::optional<std::string> convertDriveOperation(const std::string& op)
{
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Sanitize")
    {
        return "Sanitize";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Deduplicate")
    {
        return "Deduplicate";
    }
    if (op ==
        "xyz.openbmc_project.Nvme.Operation.OperationType.CheckConsistency")
    {
        return "CheckConsistency";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Initialize")
    {
        return "Initialize";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Replicate")
    {
        return "Replicate";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Delete")
    {
        return "Delete";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.ChangeRAIDType")
    {
        return "ChangeRAIDType";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Rebuild")
    {
        return "Rebuild";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Encrypt")
    {
        return "Encrypt";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Decrypt")
    {
        return "Decrypt";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Resize")
    {
        return "Resize";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Compress")
    {
        return "Compress";
    }
    if (op == "xyz.openbmc_project.Nvme.Operation.OperationType.Format")
    {
        return "Format";
    }
    if (op ==
        "xyz.openbmc_project.Nvme.Operation.OperationType.ChangeStripSize")
    {
        return "ChangeStripSize";
    }
    return "";
}

inline void getDrivePortProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path)
{
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.PortInfo",
        [asyncResp](const boost::system::error_code& ec,
                    const std::vector<
                        std::pair<std::string, dbus::utility::DbusVariantType>> &
                        propertiesList) {
            if (ec)
            {
                // this interface isn't required
                return;
            }
            for (const std::pair<std::string, dbus::utility::DbusVariantType> &
                     property : propertiesList)
            {
                const std::string& propertyName = property.first;
                if (propertyName == "MaxSpeed")
                {
                    const double* maxSpeed =
                        std::get_if<double>(&property.second);
                    if (maxSpeed == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Illegal property: MaxSpeed");
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    asyncResp->res.jsonValue["CapableSpeedGbs"] = *maxSpeed;
                }
                else if (propertyName == "CurrentSpeed")
                {
                    const double* speed = std::get_if<double>(&property.second);
                    if (speed == nullptr)
                    {
                        BMCWEB_LOG_ERROR(
                            "Illegal property: NegotiatedSpeedGbs");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["NegotiatedSpeedGbs"] = *speed;
                }
            }
        });
}

inline void getDriveVersion(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& connectionName,
                            const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Software.Version", "Version",
        [asyncResp, path](const boost::system::error_code& ec,
                          const std::string& version) {
            if (ec)
            {
                return;
            }
            asyncResp->res.jsonValue["Revision"] = version;
        });
}

inline void getDriveFWVersion(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Software.Version", "Version",
        [asyncResp, path](const boost::system::error_code& ec,
                          const std::string& version) {
            if (ec)
            {
                return;
            }

            asyncResp->res.jsonValue["FirmwareVersion"] = version;
        });
}

inline void getDriveLocationContext(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.LocationContext",
        "LocationContext",
        [asyncResp, path](const boost::system::error_code& ec,
                          const std::string& locContext) {
            if (ec)
            {
                return;
            }
            asyncResp->res
                .jsonValue["PhysicalLocation"]["PartLocationContext"] =
                locContext;
        });
}

inline void getDriveLocation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.LocationCode", "LocationCode",
        [asyncResp, path](const boost::system::error_code& ec,
                          const std::string& location) {
            if (ec)
            {
                return;
            }
            asyncResp->res
                .jsonValue["PhysicalLocation"]["PartLocation"]["ServiceLabel"] =
                location;
        });
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.Location", "LocationType",
        [asyncResp,
         path](const boost::system::error_code& ec, const std::string& type) {
            if (ec)
            {
                return;
            }
            asyncResp->res
                .jsonValue["PhysicalLocation"]["PartLocation"]["LocationType"] =
                redfish::dbus_utils::toLocationType(type);
        });
}

inline void getDriveStatus(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& connectionName,
                           const std::string& path, const std::string& sw)
{
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.State.Decorator.OperationalStatus", "Functional",
        [asyncResp, path,
         sw](const boost::system::error_code& ec, const bool functional) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("fail to get drive status");
                return;
            }
            if (!functional)
            {
                asyncResp->res.jsonValue["StatusIndicator"] = "Fail";
            }
            else if (sw != "0" && sw != "2")
            {
                // the temperature(2) is excluded and it is not PFA.
                asyncResp->res.jsonValue["StatusIndicator"] =
                    "PredictiveFailureAnalysis";
                asyncResp->res.jsonValue["FailurePredicted"] = true;
            }
            else
            {
                asyncResp->res.jsonValue["StatusIndicator"] = "OK";
                asyncResp->res.jsonValue["FailurePredicted"] = false;
            }
        });
}

inline void getDriveSmartWarning(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Nvme.Status", "SmartWarnings",
        [asyncResp, connectionName,
         path](const boost::system::error_code& ec4, const std::string& sw) {
            if (ec4)
            {
                BMCWEB_LOG_ERROR("fail to get drive smart");
                return;
            }
            getDriveStatus(asyncResp, connectionName, path, sw);
        });
}

inline void getDriveProgress(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path,
    const std::optional<std::string>& operationName)
{
    sdbusplus::asio::getProperty<uint8_t>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Common.Progress", "Progress",
        [asyncResp, operationName](const boost::system::error_code& ec,
                                   const uint8_t prog) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("fail to get drive progress");
                return;
            }
            nlohmann::json::object_t obj;
            asyncResp->res.jsonValue["Operations"] = nlohmann::json::array_t();

            obj["PercentageComplete"] = prog;
            if (operationName)
            {
                obj["OperationName"] = *operationName;
            }
            obj["AssociatedTask"]["@odata.id"] = taskUri;

            asyncResp->res.jsonValue["Operations"].emplace_back(std::move(obj));
        });
}

inline void getDriveOperation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName, const std::string& path)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Nvme.Operation", "Operation",
        [asyncResp, connectionName,
         path](const boost::system::error_code& ec5, const std::string& op) {
            if (ec5)
            {
                BMCWEB_LOG_ERROR("fail to get drive progress");
                return;
            }

            std::optional<std::string> operationName =
                convertDriveOperation(op);
            getDriveProgress(asyncResp, connectionName, path, operationName);
        });
}

// The getMainChassisId() would get the Main Chassis ID but it's not
// suitable for the case of Drives under sub-chassis Need to ensure this
// Chassis includes the drive endpoints
inline void getChassisID(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& driveId, const std::string& path)
{
    dbus::utility::getAssociationEndPoints(
        path + "/chassis",
        [asyncResp, driveId](const boost::system::error_code& ec3,
                             const dbus::utility::MapperEndPoints& resp) {
            if (ec3)
            {
                BMCWEB_LOG_ERROR("Error in chassis ID association ");
                return;
            }

            if (resp.empty())
            {
                // No ChassisID associated
                return;
            }

            // Find the chassisId that contains this driveId
            sdbusplus::message::object_path chassisPath(resp[0]);
            auto chassisId = std::string(chassisPath.filename());

            asyncResp->res.jsonValue["Links"]["Chassis"]["@odata.id"] =
                "/redfish/v1/Chassis/" + chassisId;

            asyncResp->res.jsonValue["Actions"]["#Drive.SecureErase"]
                                    ["target"] = boost::urls::format(
                "/redfish/v1/Chassis/{}/Drives/{}/Actions/Drive.SecureErase",
                chassisId, driveId);

            asyncResp->res.jsonValue["Actions"]["#Drive.SecureErase"]
                                    ["@Redfish.ActionInfo"] =
                boost::urls::format(
                    "/redfish/v1/Chassis/{}/Drives/{}/SanitizeActionInfo",
                    chassisId, driveId);
        });
}

inline void createSanitizeProgressTask(
    const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& service, const std::string& path,
    const std::string& driveId)
{
    std::shared_ptr<task::TaskData> task = task::TaskData::createTask(
        [service, path,
         driveId](boost::system::error_code ec, sdbusplus::message_t& msg,
                  const std::shared_ptr<task::TaskData>& taskData) {
            if (ec)
            {
                taskData->finishTask();
                taskData->state = "Aborted";
                taskData->messages.emplace_back(
                    messages::resourceErrorsDetectedFormatError(
                        "Drive SecureErase", ec.message()));
                return task::completed;
            }

            std::string iface;
            boost::container::flat_map<std::string,
                                       dbus::utility::DbusVariantType>
                values;

            std::string index = std::to_string(taskData->index);
            msg.read(iface, values);

            if (iface != "xyz.openbmc_project.Common.Progress")
            {
                return !task::completed;
            }
            auto findStatus = values.find("Status");
            if (findStatus != values.end())
            {
                std::string* state =
                    std::get_if<std::string>(&(findStatus->second));
                if (state == nullptr)
                {
                    taskData->messages.emplace_back(messages::internalError());
                    return !task::completed;
                }

                if (state->ends_with("Aborted") || state->ends_with("Failed"))
                {
                    taskData->state = "Exception";
                    taskData->messages.emplace_back(
                        messages::taskAborted(index));
                    return task::completed;
                }

                if (state->ends_with("Completed"))
                {
                    taskData->state = "Completed";
                    taskData->percentComplete = 100;
                    taskData->messages.emplace_back(
                        messages::taskCompletedOK(index));
                    taskData->finishTask();
                    return task::completed;
                }
            }

            auto findProgress = values.find("Progress");
            if (findProgress == values.end())
            {
                return !task::completed;
            }
            uint8_t* progress = std::get_if<uint8_t>(&(findProgress->second));
            if (progress == nullptr)
            {
                taskData->messages.emplace_back(messages::internalError());
                return task::completed;
            }
            taskData->percentComplete = static_cast<int>(*progress);

            BMCWEB_LOG_ERROR("{}", taskData->percentComplete);
            taskData->messages.emplace_back(messages::taskProgressChanged(
                index, static_cast<size_t>(*progress)));

            return !task::completed;
        },
        "type='signal',interface='org.freedesktop.DBus.Properties',"
        "member='PropertiesChanged',path='" +
            path + "'");

    task->startTimer(std::chrono::seconds(60));
    task::Payload payload(req);
    task->payload.emplace(std::move(payload));
    task->populateResp(asyncResp->res);

    taskUri[driveId] =
        "/redfish/v1/TaskService/Tasks/" + std::to_string(task->index);
}

inline void handleDriveSanitizePost(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& /*chassisId*/, const std::string& driveId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::string sanitizeType;
    uint16_t owPass = 0;
    if (!json_util::readJsonAction(req, asyncResp->res, "SanitizationType",
                                   sanitizeType))
    {
        messages::actionParameterValueError(asyncResp->res, "Drive.SecureErase",
                                            "SanitizationType");
        return;
    }
    if (sanitizeType == "Overwrite")
    {
        if (!json_util::readJsonAction(req, asyncResp->res, "OverwritePasses",
                                       owPass))
        {
            messages::actionParameterMissing(
                asyncResp->res, "Drive.SecureErase", "OverwritePasses");
            return;
        }
    }
    else if (sanitizeType == "CryptographicErase")
    {
        sanitizeType = "CryptoErase";
    }

    constexpr std::array<std::string_view, 1> localDriveInterface = {
        "xyz.openbmc_project.Inventory.Item.Drive"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, localDriveInterface,
        [&req, asyncResp, driveId, sanitizeType,
         owPass](const boost::system::error_code& ec4,
                 const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec4)
            {
                BMCWEB_LOG_ERROR("Drive mapper call error");
                messages::internalError(asyncResp->res);
                return;
            }

            auto drive = std::find_if(
                subtree.begin(), subtree.end(),
                [&driveId](
                    const std::pair<std::string,
                                    dbus::utility::MapperServiceMap>& object) {
                    return sdbusplus::message::object_path(object.first)
                               .filename() == driveId;
                });
            if (drive == subtree.end())
            {
                messages::resourceNotFound(asyncResp->res, "Drive", driveId);
                return;
            }
            const std::string& path = drive->first;
            const dbus::utility::MapperServiceMap& connNames = drive->second;

            std::string service;
            std::string interface;
            for (const auto& [connectionName, connInterfaces] : connNames)
            {
                for (const std::string& iface : connInterfaces)
                {
                    if (iface == "xyz.openbmc_project.Nvme.SecureErase")
                    {
                        service = connectionName;
                        interface = iface;
                        break;
                    }
                }
            }
            if (service.empty() || interface.empty())
            {
                BMCWEB_LOG_ERROR("failed to get DriveSanitizetActionInfo");
                messages::internalError(asyncResp->res);
                return;
            }

            auto methodName =
                "xyz.openbmc_project.Nvme.SecureErase.EraseMethod." +
                sanitizeType;
            // execute drive sanitize operation
            crow::connections::systemBus->async_method_call(
                [&req, asyncResp, service, path,
                 driveId](const boost::system::error_code& ec,
                          sdbusplus::message::message& msg) {
                    const sd_bus_error* dbusError = msg.get_error();
                    if (dbusError != nullptr &&
                        strcmp(dbusError->name,
                               "xyz.openbmc_project.Common.Error.NotAllowed") ==
                            0)
                    {
                        std::string resolution =
                            "Drive sanitize in progress. Retry "
                            "the sanitize operation once it is complete.";
                        redfish::messages::updateInProgressMsg(asyncResp->res,
                                                               resolution);
                        BMCWEB_LOG_ERROR(
                            "Sanitize on drive{} already in progress.",
                            driveId);
                    }
                    if (ec)
                    {
                        // other errors return here.
                        return;
                    }
                    createSanitizeProgressTask(req, asyncResp, service, path,
                                               driveId);
                },
                service, path, interface, "Erase", owPass, methodName);
        });
}

inline void handleDriveSanitizetActionInfoGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& /*unused*/, const std::string& driveId)
{
    asyncResp->res.jsonValue["@odata.type"] = "#ActionInfo.v1_1_2.ActionInfo";
    asyncResp->res.jsonValue["Name"] = "Sanitize Action Info";
    asyncResp->res.jsonValue["Id"] = "SanitizeActionInfo";

    constexpr std::array<std::string_view, 1> localDriveInterface = {
        "xyz.openbmc_project.Inventory.Item.Drive"};
    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, localDriveInterface,
        [asyncResp,
         driveId](const boost::system::error_code& ec4,
                  const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec4)
            {
                BMCWEB_LOG_ERROR("Drive mapper call error");
                messages::internalError(asyncResp->res);
                return;
            }

            auto drive = std::find_if(
                subtree.begin(), subtree.end(),
                [&driveId](
                    const std::pair<std::string,
                                    dbus::utility::MapperServiceMap>& object) {
                    return sdbusplus::message::object_path(object.first)
                               .filename() == driveId;
                });
            if (drive == subtree.end())
            {
                messages::resourceNotFound(asyncResp->res, "Drive", driveId);
                return;
            }
            const std::string& path = drive->first;
            const dbus::utility::MapperServiceMap& connNames = drive->second;

            std::string service;
            std::string interface;
            for (const auto& [connectionName, connInterfaces] : connNames)
            {
                for (const std::string& iface : connInterfaces)
                {
                    if (iface == "xyz.openbmc_project.Nvme.SecureErase")
                    {
                        service = connectionName;
                        interface = iface;
                        break;
                    }
                }
            }
            if (service.empty() || interface.empty())
            {
                BMCWEB_LOG_ERROR("failed to get DriveSanitizetActionInfo");
                messages::internalError(asyncResp->res);
                return;
            }
            sdbusplus::asio::getProperty<std::vector<std::string>>(
                *crow::connections::systemBus, service, path, interface,
                "SanitizeCapability",
                [asyncResp](const boost::system::error_code& ec,
                            const std::vector<std::string>& cap) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR("fail to get drive Progress");
                        return;
                    }
                    nlohmann::json::array_t parameters;
                    nlohmann::json::object_t parameter;
                    nlohmann::json::array_t allowed;

                    if (std::find(
                            cap.begin(), cap.end(),
                            "xyz.openbmc_project.Nvme.SecureErase.EraseMethod.Overwrite") !=
                        cap.end())
                    {
                        parameter["Name"] = "OverwritePasses";
                        parameter["DataType"] = "integer";
                        parameters.emplace_back(parameter);

                        allowed.emplace_back("Overwrite");
                    }
                    if (std::find(
                            cap.begin(), cap.end(),
                            "xyz.openbmc_project.Nvme.SecureErase.EraseMethod.BlockErase") !=
                        cap.end())
                    {
                        allowed.emplace_back("BlockErase");
                    }
                    if (std::find(
                            cap.begin(), cap.end(),
                            "xyz.openbmc_project.Nvme.SecureErase.EraseMethod.CryptoErase") !=
                        cap.end())
                    {
                        allowed.emplace_back("CryptographicErase");
                    }
                    parameter["Name"] = "SanitizationType";
                    parameter["DataType"] = "String";

                    parameter["AllowableValues"] = allowed;
                    parameters.emplace_back(parameter);

                    asyncResp->res.jsonValue["Parameters"] = parameters;
                });
        });
}

inline void handleSystemDriveSanitizetActionInfoGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemId, const std::string& driveId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + systemId + "/Drives/" + driveId +
        "/SanitizeActionInfo";

    handleDriveSanitizetActionInfoGet(asyncResp, systemId, driveId);
}

inline void handleChassisDriveSanitizetActionInfoGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& driveId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Chassis/" + chassisId + "/Drives/" + driveId +
        "/SanitizeActionInfo";

    handleDriveSanitizetActionInfoGet(asyncResp, chassisId, driveId);
}

inline void extendSystemsStorageGet(
    const std::pair<std::string, dbus::utility::DbusVariantType>& property,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    const std::string& propertyName = property.first;
    if (propertyName == "FormFactor")
    {
        const std::string* value = std::get_if<std::string>(&property.second);
        if (value == nullptr)
        {
            BMCWEB_LOG_ERROR("Illegal property: FormFactor");
            messages::internalError(asyncResp->res);
            return;
        }
        std::optional<std::string> formFactor = convertDriveFormFactor(*value);
        if (!formFactor)
        {
            BMCWEB_LOG_ERROR("Unsupported Drive FormFactor Interface: {}",
                             *value);
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["DriveFormFactor"] = *formFactor;
    }
}

inline void extendAllDriveInfo(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& connectionName,
    const std::string& path,
    const std::string& interface)
{
    if (interface == "xyz.openbmc_project.Inventory.Decorator.PortInfo")
    {
        getDrivePortProperties(asyncResp, connectionName, path);
    }
    else if (interface == "xyz.openbmc_project.Software.Version")
    {
        getDriveFWVersion(asyncResp, connectionName, path);
    }
    else if (interface == "xyz.openbmc_project.Nvme.Status")
    {
        getDriveSmartWarning(asyncResp, connectionName, path);
    }
    else if (interface ==
             "xyz.openbmc_project.Inventory.Decorator.LocationContext")
    {
        getDriveLocationContext(asyncResp, connectionName, path);
    }
    else if (interface ==
             "xyz.openbmc_project.Inventory.Decorator.LocationCode")
    {
        getDriveLocation(asyncResp, connectionName, path);
    }
    else if (interface == "xyz.openbmc_project.Nvme.Operation")
    {
        getDriveOperation(asyncResp, connectionName, path);
    }
}

/**
 * System drives, this URL will show all the DriveCollection
 * information
 */
inline void driveCollectionGet(
    crow::App& app, const crow::Request& req,
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
    asyncResp->res.jsonValue["@odata.type"] =
        "#DriveCollection.DriveCollection";
    asyncResp->res.jsonValue["Name"] = "Drive Collection";
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/Storage/1/Drives/";

    constexpr std::array<std::string_view, 1> localDriveInterface = {
        "xyz.openbmc_project.Inventory.Item.Drive"};

    dbus::utility::getSubTree(
        "/xyz/openbmc_project/inventory", 0, localDriveInterface,
        [asyncResp, localDriveInterface](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Drive mapper call error");
                messages::internalError(asyncResp->res);
                return;
            }
            nlohmann::json& members = asyncResp->res.jsonValue["Members"];
            // important if array is empty
            members = nlohmann::json::array();
            nlohmann::json::object_t member;
            for (const auto& [path, connNames] : subtree)
            {
                // EM also populate NVMe drives to Dbus
                // We expect to have NVMe resource from nvme-manager so we
                // filter out drive instances from EM by the number of Dbus
                // interface.
                sdbusplus::message::object_path objPath(path);
                auto id = objPath.filename();
                uint32_t num = 0;
                for (const std::string& interface : connNames.begin()->second)
                {
                    if (std::find_if(localDriveInterface.begin(),
                                     localDriveInterface.end(),
                                     [interface](std::string_view possible) {
                                         return interface.starts_with(possible);
                                     }) != localDriveInterface.end())
                    {
                        num++;
                    }
                }
                if (num != localDriveInterface.size())
                {
                    continue;
                }
                member["@odata.id"] =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/Storage/1/Drives/" + id;
                members.emplace_back(member);
            }
            asyncResp->res.jsonValue["Members@odata.count"] = members.size();
        });
}

inline void requestRoutesNvidiaChassisDriveName(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Chassis/<str>/Drives/<str>/Actions/Drive.SecureErase/")
        .privileges(redfish::privileges::postDrive)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleDriveSanitizePost, std::ref(app)));

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Chassis/<str>/Drives/<str>/SanitizeActionInfo/")
        .privileges(redfish::privileges::getDrive)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleChassisDriveSanitizetActionInfoGet, std::ref(app)));
}

inline void requestRoutesNvidiaDrive(App& app)                                                                                                
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/Storage/1/Drives/")
        .privileges(redfish::privileges::getDriveCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(driveCollectionGet, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Storage/1/Drives/<str>/Actions/Drive.SecureErase/")
        .privileges(redfish::privileges::postDrive)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleDriveSanitizePost, std::ref(app)));

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/Storage/1/Drives/<str>/SanitizeActionInfo/")
        .privileges(redfish::privileges::getDrive)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleSystemDriveSanitizetActionInfoGet, std::ref(app)));
}

} // namespace redfish