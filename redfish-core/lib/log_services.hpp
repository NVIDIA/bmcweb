/*
// Copyright (c) 2018 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
#pragma once

#include "app.hpp"
#include "dbus_utility.hpp"
#include "debug_token.hpp"
#include "error_messages.hpp"
#include "generated/enums/log_entry.hpp"
#include "gzfile.hpp"
#include "http_utility.hpp"
#include "human_sort.hpp"
#include "nvidia_messages.hpp"
#include "query.hpp"
#include "registries.hpp"
#include "registries/base_message_registry.hpp"
#include "registries/openbmc_message_registry.hpp"
#include "registries/privilege_registry.hpp"
#include "task.hpp"
#include "task_messages.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/time_utils.hpp"

#include <systemd/sd-id128.h>
#include <tinyxml2.h>
#include <unistd.h>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/process.hpp>
#include <boost/process/async.hpp>
#include <boost/process/child.hpp>
#include <boost/system/linux_error.hpp>
#include <boost/url/format.hpp>
#include <dbus_utility.hpp>
#include <error_messages.hpp>
#include <openbmc_dbus_rest.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/exception.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/message/native_types.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/dbus_log_utils.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/log_services_util.hpp>
#include <utils/origin_utils.hpp>
#include <utils/time_utils.hpp>

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace redfish
{

constexpr const char* crashdumpObject = "com.intel.crashdump";
constexpr const char* crashdumpPath = "/com/intel/crashdump";
constexpr const char* crashdumpInterface = "com.intel.crashdump";
constexpr const char* deleteAllInterface =
    "xyz.openbmc_project.Collection.DeleteAll";
constexpr const char* crashdumpOnDemandInterface =
    "com.intel.crashdump.OnDemand";
constexpr const char* crashdumpTelemetryInterface =
    "com.intel.crashdump.Telemetry";
constexpr const char* logEntryVersion = "#LogEntry.v1_15_0.LogEntry";

enum class DumpCreationProgress
{
    DUMP_CREATE_SUCCESS,
    DUMP_CREATE_FAILED,
    DUMP_CREATE_INPROGRESS
};

namespace message_registries
{

static void generateMessageRegistry(
    nlohmann::json& logEntry,
    const std::string& odataId, /* e.g. /redfish/v1/Systems/system/LogServices/"
                                  "EventLog/Entries/ */
    const std::string& odataTypeVer /* e.g. v1_15_0 */, const std::string& id,
    const std::string& name, const std::string& timestamp,
    const std::string& messageId, const std::string& messageArgs,
    const std::string& resolution, const bool& resolved,
    const std::string& eventId, const std::string& deviceName,
    const std::string& severity = "")
{
    BMCWEB_LOG_DEBUG(
        "Generating MessageRegitry for [{}] For Device {} For EventId {} ",
        messageId, deviceName, eventId);
    const registries::Message* msg = registries::getMessage(messageId);

    if (msg == nullptr)
    {
        BMCWEB_LOG_ERROR("Failed to lookup the message for MessageId[{}]",
                         messageId);
        return;
    }

    // Severity & Resolution can be overwritten by caller. Using the one defined
    // in the message registries by default.
    std::string sev;
    if (severity.size() == 0)
    {
        sev = msg->messageSeverity;
    }
    else
    {
        sev = translateSeverityDbusToRedfish(severity);
    }

    std::string res(resolution);
    if (res.size() == 0)
    {
        res = msg->resolution;
    }

    // Convert messageArgs string for its json format used later.
    std::vector<std::string> fields;
    std::vector<std::string> field_proc;
    fields.reserve(msg->numberOfArgs);
    boost::split(fields, messageArgs, boost::is_any_of(","));

    // Trim leading and tailing whitespace of each arg.
    for (auto& f : fields)
    {
        boost::trim(f);
        if (f.length())
        {
            field_proc.push_back(f);
        }
    }

    std::span<std::string> msgArgs;
    msgArgs = {&field_proc[0], field_proc.size()};

    std::string message = msg->message;
    int i = 0;
    for (auto& arg : msgArgs)
    {
        // Substituion
        std::string argStr = "%" + std::to_string(++i);
        size_t argPos = message.find(argStr);
        if (argPos != std::string::npos)
        {
            message.replace(argPos, argStr.length(), arg);
        }
    }

    logEntry = {{"@odata.id", odataId + id},
                {"@odata.type", "#LogEntry." + odataTypeVer + ".LogEntry"},
                {"Id", id},
                {"Name", name},
                {"EntryType", "Event"},
                {"Severity", sev},
                {"Created", timestamp},
                {"Message", message},
                {"MessageId", messageId},
                {"MessageArgs", msgArgs},
                {"Resolution", res},
                {"Resolved", resolved}};
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        if (!eventId.empty() || !deviceName.empty())
        {
            nlohmann::json oem = {
                {"Oem",
                 {{"Nvidia",
                   {{"@odata.type",
                     "#NvidiaLogEntry.v1_1_0.NvidiaLogEntry"}}}}}};
            if (!deviceName.empty())
            {
                oem["Oem"]["Nvidia"]["Device"] = deviceName;
            }
            if (!eventId.empty())
            {
                oem["Oem"]["Nvidia"]["ErrorId"] = eventId;
            }
            logEntry.update(oem);
        }
    }
}

} // namespace message_registries

namespace fs = std::filesystem;

using GetManagedPropertyType =
    boost::container::flat_map<std::string, dbus::utility::DbusVariantType>;

using GetManagedObjectsType = boost::container::flat_map<
    sdbusplus::message::object_path,
    boost::container::flat_map<std::string, GetManagedPropertyType>>;

namespace fs = std::filesystem;

inline std::string translateSeverityDbusToRedfish(const std::string& s)
{
    if ((s == "xyz.openbmc_project.Logging.Entry.Level.Alert") ||
        (s == "xyz.openbmc_project.Logging.Entry.Level.Critical") ||
        (s == "xyz.openbmc_project.Logging.Entry.Level.Emergency") ||
        (s == "xyz.openbmc_project.Logging.Entry.Level.Error"))
    {
        return "Critical";
    }
    if ((s == "xyz.openbmc_project.Logging.Entry.Level.Debug") ||
        (s == "xyz.openbmc_project.Logging.Entry.Level.Informational") ||
        (s == "xyz.openbmc_project.Logging.Entry.Level.Notice"))
    {
        return "OK";
    }
    if (s == "xyz.openbmc_project.Logging.Entry.Level.Warning")
    {
        return "Warning";
    }
    return "";
}

inline std::optional<bool> getProviderNotifyAction(const std::string& notify)
{
    std::optional<bool> notifyAction;
    if (notify == "xyz.openbmc_project.Logging.Entry.Notify.Notify")
    {
        notifyAction = true;
    }
    else if (notify == "xyz.openbmc_project.Logging.Entry.Notify.Inhibit")
    {
        notifyAction = false;
    }

    return notifyAction;
}

inline std::string getDumpPath(std::string_view dumpType)
{
    std::string dbusDumpPath = "/xyz/openbmc_project/dump/";
    std::ranges::transform(dumpType, std::back_inserter(dbusDumpPath),
                           bmcweb::asciiToLower);

    return dbusDumpPath;
}

inline bool getUniqueEntryID(const std::string& logEntry, std::string& entryID,
                             const bool firstEntry = true)
{
    static time_t prevTs = 0;
    static int index = 0;
    if (firstEntry)
    {
        prevTs = 0;
    }

    // Get the entry timestamp
    std::time_t curTs = 0;
    std::tm timeStruct = {};
    std::istringstream entryStream(logEntry);
    if (entryStream >> std::get_time(&timeStruct, "%Y-%m-%dT%H:%M:%S"))
    {
        curTs = std::mktime(&timeStruct);
    }
    // If the timestamp isn't unique, increment the index
    if (curTs == prevTs)
    {
        index++;
    }
    else
    {
        // Otherwise, reset it
        index = 0;
    }
    // Save the timestamp
    prevTs = curTs;

    entryID = std::to_string(curTs);
    if (index > 0)
    {
        entryID += "_" + std::to_string(index);
    }
    return true;
}

static bool
    getRedfishLogFiles(std::vector<std::filesystem::path>& redfishLogFiles)
{
    static const std::filesystem::path redfishLogDir = "/var/log";
    static const std::string redfishLogFilename = "redfish";

    // Loop through the directory looking for redfish log files
    for (const std::filesystem::directory_entry& dirEnt :
         std::filesystem::directory_iterator(redfishLogDir))
    {
        // If we find a redfish log file, save the path
        std::string filename = dirEnt.path().filename();
        if (filename.starts_with(redfishLogFilename))
        {
            redfishLogFiles.emplace_back(redfishLogDir / filename);
        }
    }
    // As the log files rotate, they are appended with a ".#" that is higher for
    // the older logs. Since we don't expect more than 10 log files, we
    // can just sort the list to get them in order from newest to oldest
    std::ranges::sort(redfishLogFiles);

    return !redfishLogFiles.empty();
}

inline std::vector<std::pair<std::string, std::variant<std::string, uint64_t>>>
    parseOEMAdditionalData(const std::string_view& oemData)
{
    // Parse OEM data for encoded format string
    // oemDiagnosticDataType = "key1=value1;key2=value2;key3=value3"
    std::vector<std::pair<std::string, std::variant<std::string, uint64_t>>>
        additionalData;
    std::vector<std::string> tokens;
    boost::split(tokens, oemData, boost::is_any_of(";"));
    if (!tokens.empty())
    {
        std::vector<std::string> subTokens;
        for (auto& token : tokens)
        {
            boost::split(subTokens, token, boost::is_any_of("="));
            // Include only <key,value> pair with '=' delimiter
            if (subTokens.size() == 2)
            {
                additionalData.emplace_back(
                    std::make_pair(subTokens[0], subTokens[1]));
            }
        }
    }
    return additionalData;
}

inline void
    deleteDbusLogEntry(const std::string& entryId,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    auto respHandler = [asyncResp](const boost::system::error_code ec) {
        if (ec)
        {
            asyncResp->res.result(
                boost::beast::http::status::internal_server_error);
            return;
        }
        asyncResp->res.result(boost::beast::http::status::no_content);
    };
    crow::connections::systemBus->async_method_call(
        respHandler, "xyz.openbmc_project.Logging",
        "/xyz/openbmc_project/logging/entry/" + entryId,
        "xyz.openbmc_project.Object.Delete", "Delete");
}

inline bool isSelEntry(const std::string* message,
                       const std::vector<std::string>* additionalData)
{
    if ((message != nullptr) &&
        (*message == "xyz.openbmc_project.Logging.SEL.Error.Created"))
    {
        return true;
    }
    if (additionalData != nullptr)
    {
        AdditionalData additional(*additionalData);
        if (additional.count("namespace") > 0 &&
            additional["namespace"] == "SEL")
        {
            return true;
        }
    }
    return false;
}

inline void
    deleteDbusSELEntry(std::string& entryID,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, entryID](const boost::system::error_code ec,
                             GetManagedPropertyType& resp) {
        if (ec.value() == EBADR)
        {
            messages::resourceNotFound(asyncResp->res, "SELLogEntry", entryID);

            return;
        }
        if (ec)
        {
            BMCWEB_LOG_ERROR("SELLogEntry (DBus) resp_handler got error {}",
                             ec);
            messages::internalError(asyncResp->res);
            return;
        }
        uint32_t* id = nullptr;
        std::string* message = nullptr;
        const std::vector<std::string>* additionalData = nullptr;

        for (auto& propertyMap : resp)
        {
            if (propertyMap.first == "Id")
            {
                id = std::get_if<uint32_t>(&propertyMap.second);
            }

            else if (propertyMap.first == "Message")
            {
                message = std::get_if<std::string>(&propertyMap.second);
            }
            else if (propertyMap.first == "AdditionalData")
            {
                additionalData =
                    std::get_if<std::vector<std::string>>(&propertyMap.second);
            }
        }
        if (id == nullptr || message == nullptr)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        if (isSelEntry(message, additionalData))
        {
            deleteDbusLogEntry(entryID, asyncResp);
            return;
        }
        messages::resourceNotFound(asyncResp->res, "SELLogEntry", entryID);
        return;
    },
        "xyz.openbmc_project.Logging",
        "/xyz/openbmc_project/logging/entry/" + entryID,
        "org.freedesktop.DBus.Properties", "GetAll", "");
}

inline log_entry::OriginatorTypes
    mapDbusOriginatorTypeToRedfish(const std::string& originatorType)
{
    if (originatorType ==
        "xyz.openbmc_project.Common.OriginatedBy.OriginatorTypes.Client")
    {
        return log_entry::OriginatorTypes::Client;
    }
    if (originatorType ==
        "xyz.openbmc_project.Common.OriginatedBy.OriginatorTypes.Internal")
    {
        return log_entry::OriginatorTypes::Internal;
    }
    if (originatorType ==
        "xyz.openbmc_project.Common.OriginatedBy.OriginatorTypes.SupportingService")
    {
        return log_entry::OriginatorTypes::SupportingService;
    }
    return log_entry::OriginatorTypes::Invalid;
}

inline void parseDumpEntryFromDbusObject(
    const dbus::utility::ManagedObjectType::value_type& object,
    std::string& dumpStatus, uint64_t& size, uint64_t& timestampUs,
    std::string& faultLogDiagnosticDataType, std::string& notificationType,
    std::string& sectionType, std::string& fruid, std::string& severity,
    std::string& nvipSignature, std::string& nvSeverity,
    std::string& nvSocketNumber, std::string& pcieVendorID,
    std::string& pcieDeviceID, std::string& pcieClassCode,
    std::string& pcieFunctionNumber, std::string& pcieDeviceNumber,
    std::string& pcieSegmentNumber, std::string& pcieDeviceBusNumber,
    std::string& pcieSecondaryBusNumber, std::string& pcieSlotNumber,
    std::string& originatorId, log_entry::OriginatorTypes& originatorType,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    for (const auto& interfaceMap : object.second)
    {
        if (interfaceMap.first == "xyz.openbmc_project.Common.Progress")
        {
            for (const auto& propertyMap : interfaceMap.second)
            {
                if (propertyMap.first == "Status")
                {
                    const auto* status =
                        std::get_if<std::string>(&propertyMap.second);
                    if (status == nullptr)
                    {
                        messages::internalError(asyncResp->res);
                        break;
                    }
                    dumpStatus = *status;
                }
            }
        }
        else if (interfaceMap.first == "xyz.openbmc_project.Dump.Entry")
        {
            for (const auto& propertyMap : interfaceMap.second)
            {
                if (propertyMap.first == "Size")
                {
                    const auto* sizePtr =
                        std::get_if<uint64_t>(&propertyMap.second);
                    if (sizePtr == nullptr)
                    {
                        messages::internalError(asyncResp->res);
                        break;
                    }
                    size = *sizePtr;
                    break;
                }
            }
        }
        else if (interfaceMap.first == "xyz.openbmc_project.FDR.Entry")
        {
            for (const auto& propertyMap : interfaceMap.second)
            {
                if (propertyMap.first == "Size")
                {
                    const auto* sizePtr =
                        std::get_if<uint64_t>(&propertyMap.second);
                    if (sizePtr == nullptr)
                    {
                        messages::internalError(asyncResp->res);
                        break;
                    }
                    size = *sizePtr;
                    break;
                }
            }
        }
        else if (interfaceMap.first == "xyz.openbmc_project.Time.EpochTime")
        {
            for (const auto& propertyMap : interfaceMap.second)
            {
                if (propertyMap.first == "Elapsed")
                {
                    const uint64_t* usecsTimeStamp =
                        std::get_if<uint64_t>(&propertyMap.second);
                    if (usecsTimeStamp == nullptr)
                    {
                        messages::internalError(asyncResp->res);
                        break;
                    }
                    timestampUs = *usecsTimeStamp;
                    break;
                }
            }
        }
        else if (interfaceMap.first ==
                 "xyz.openbmc_project.Dump.Entry.FaultLog")
        {
            const std::string* type = nullptr;
            const std::string* additionalTypeName = nullptr;
            for (auto& propertyMap : interfaceMap.second)
            {
                if (propertyMap.first == "Type")
                {
                    type = std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "AdditionalTypeName")
                {
                    additionalTypeName =
                        std::get_if<std::string>(&propertyMap.second);
                }
            }
            if (type != nullptr &&
                *type ==
                    "xyz.openbmc_project.Common.FaultLogType.FaultLogTypes.CPER")
            {
                if (additionalTypeName != nullptr)
                {
                    faultLogDiagnosticDataType = *additionalTypeName;
                }
            }
        }
        else if (interfaceMap.first ==
                 "xyz.openbmc_project.Dump.Entry.CPERDecode")
        {
            const std::string* notificationTypePtr = nullptr;
            const std::string* sectionTypePtr = nullptr;
            const std::string* fruidPtr = nullptr;
            const std::string* severityPtr = nullptr;
            const std::string* nvipSignaturePtr = nullptr;
            const std::string* nvSeverityPtr = nullptr;
            const std::string* nvSocketNumberPtr = nullptr;
            const std::string* pcieVendorIDPtr = nullptr;
            const std::string* pcieDeviceIDPtr = nullptr;
            const std::string* pcieClassCodePtr = nullptr;
            const std::string* pcieFunctionNumberPtr = nullptr;
            const std::string* pcieDeviceNumberPtr = nullptr;
            const std::string* pcieSegmentNumberPtr = nullptr;
            const std::string* pcieDeviceBusNumberPtr = nullptr;
            const std::string* pcieSecondaryBusNumberPtr = nullptr;
            const std::string* pcieSlotNumberPtr = nullptr;

            for (auto& propertyMap : interfaceMap.second)
            {
                if (propertyMap.first == "FRU_ID")
                {
                    fruidPtr = std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "NV_IPSignature")
                {
                    nvipSignaturePtr =
                        std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "NV_Severity")
                {
                    nvSeverityPtr =
                        std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "NV_Socket_Number")
                {
                    nvSocketNumberPtr =
                        std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "PCIE_Class_Code")
                {
                    pcieClassCodePtr =
                        std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "PCIE_Device_Bus_Number")
                {
                    pcieDeviceBusNumberPtr =
                        std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "PCIE_Device_ID")
                {
                    pcieDeviceIDPtr =
                        std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "PCIE_Device_Number")
                {
                    pcieDeviceNumberPtr =
                        std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "PCIE_Function_Number")
                {
                    pcieFunctionNumberPtr =
                        std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "PCIE_Secondary_Bus_Number")
                {
                    pcieSecondaryBusNumberPtr =
                        std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "PCIE_Segment_Number")
                {
                    pcieSegmentNumberPtr =
                        std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "PCIE_Slot_Number")
                {
                    pcieSlotNumberPtr =
                        std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "PCIE_Vendor_ID")
                {
                    pcieVendorIDPtr =
                        std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "Section_Type")
                {
                    sectionTypePtr =
                        std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "Notification_Type")
                {
                    notificationTypePtr =
                        std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "Severity")
                {
                    severityPtr = std::get_if<std::string>(&propertyMap.second);
                }
            }

            if (fruidPtr != nullptr)
            {
                fruid = *fruidPtr;
            }

            if (notificationTypePtr != nullptr)
            {
                notificationType = *notificationTypePtr;
            }

            if (sectionTypePtr != nullptr)
            {
                sectionType = *sectionTypePtr;
            }

            if (severityPtr != nullptr)
            {
                severity = *severityPtr;
            }

            if (nvipSignaturePtr != nullptr)
            {
                nvipSignature = *nvipSignaturePtr;
            }

            if (nvSeverityPtr != nullptr)
            {
                nvSeverity = *nvSeverityPtr;
            }

            if (nvSocketNumberPtr != nullptr)
            {
                nvSocketNumber = *nvSocketNumberPtr;
            }

            if (pcieVendorIDPtr != nullptr)
            {
                pcieVendorID = *pcieVendorIDPtr;
            }

            if (pcieDeviceIDPtr != nullptr)
            {
                pcieDeviceID = *pcieDeviceIDPtr;
            }

            if (pcieClassCodePtr != nullptr)
            {
                pcieClassCode = *pcieClassCodePtr;
            }

            if (pcieFunctionNumberPtr != nullptr)
            {
                pcieFunctionNumber = *pcieFunctionNumberPtr;
            }

            if (pcieDeviceNumberPtr != nullptr)
            {
                pcieDeviceNumber = *pcieDeviceNumberPtr;
            }

            if (pcieSegmentNumberPtr != nullptr)
            {
                pcieSegmentNumber = *pcieSegmentNumberPtr;
            }

            if (pcieDeviceBusNumberPtr != nullptr)
            {
                pcieDeviceBusNumber = *pcieDeviceBusNumberPtr;
            }

            if (pcieSecondaryBusNumberPtr != nullptr)
            {
                pcieSecondaryBusNumber = *pcieSecondaryBusNumberPtr;
            }

            if (pcieSlotNumberPtr != nullptr)
            {
                pcieSlotNumber = *pcieSlotNumberPtr;
            }
        }
        else if (interfaceMap.first ==
                 "xyz.openbmc_project.Common.OriginatedBy")
        {
            for (const auto& propertyMap : interfaceMap.second)
            {
                if (propertyMap.first == "OriginatorId")
                {
                    const std::string* id =
                        std::get_if<std::string>(&propertyMap.second);
                    if (id == nullptr)
                    {
                        messages::internalError(asyncResp->res);
                        break;
                    }
                    originatorId = *id;
                }

                if (propertyMap.first == "OriginatorType")
                {
                    const std::string* type =
                        std::get_if<std::string>(&propertyMap.second);
                    if (type == nullptr)
                    {
                        messages::internalError(asyncResp->res);
                        break;
                    }

                    originatorType = mapDbusOriginatorTypeToRedfish(*type);
                    if (originatorType == log_entry::OriginatorTypes::Invalid)
                    {
                        messages::internalError(asyncResp->res);
                        break;
                    }
                }
            }
        }
    }
}

static std::string getDumpEntriesPath(const std::string& dumpType)
{
    std::string entriesPath;

    if (dumpType == "BMC")
    {
        entriesPath =
            std::format("/redfish/v1/Managers/{}/LogServices/Dump/Entries/",
                        BMCWEB_REDFISH_MANAGER_URI_NAME);
    }
    else if (dumpType == "System")
    {
        entriesPath =
            std::format("/redfish/v1/Systems/{}/LogServices/Dump/Entries/",
                        BMCWEB_REDFISH_SYSTEM_URI_NAME);
    }
    else if (dumpType == "FDR")
    {
        entriesPath = "/redfish/v1/Systems/" +
                      std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                      "/LogServices/FDR/Entries/";
    }
    else if (dumpType == "FaultLog")
    {
        entriesPath =
            std::format("/redfish/v1/Systems/{}/LogServices/FaultLog/Entries/",
                        BMCWEB_REDFISH_SYSTEM_URI_NAME);
    }
    else
    {
        BMCWEB_LOG_ERROR("getDumpEntriesPath() invalid dump type: {}",
                         dumpType);
    }

    // Returns empty string on error
    return entriesPath;
}

inline void
    getDumpEntryCollection(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& dumpType)
{
    std::string entriesPath = getDumpEntriesPath(dumpType);
    if (entriesPath.empty())
    {
        messages::internalError(asyncResp->res);
        return;
    }

    sdbusplus::message::object_path path("/xyz/openbmc_project/dump");
    dbus::utility::getManagedObjects(
        "xyz.openbmc_project.Dump.Manager", path,
        [asyncResp, entriesPath,
         dumpType](const boost::system::error_code& ec,
                   const dbus::utility::ManagedObjectType& objects) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DumpEntry resp_handler got error {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }

        // Remove ending slash
        std::string odataIdStr = entriesPath;
        if (!odataIdStr.empty())
        {
            odataIdStr.pop_back();
        }

        asyncResp->res.jsonValue["@odata.type"] =
            "#LogEntryCollection.LogEntryCollection";
        asyncResp->res.jsonValue["@odata.id"] = std::move(odataIdStr);
        asyncResp->res.jsonValue["Name"] = dumpType + " Dump Entries";
        asyncResp->res.jsonValue["Description"] = "Collection of " + dumpType +
                                                  " Dump Entries";

        nlohmann::json::array_t entriesArray;
        std::string dumpEntryPath = getDumpPath(dumpType) + "/entry/";

        dbus::utility::ManagedObjectType resp(objects);
        std::ranges::sort(resp, [](const auto& l, const auto& r) {
            return AlphanumLess<std::string>()(l.first.filename(),
                                               r.first.filename());
        });

        for (auto& object : resp)
        {
            if (object.first.str.find(dumpEntryPath) == std::string::npos)
            {
                continue;
            }
            uint64_t timestampUs = 0;
            uint64_t size = 0;
            std::string dumpStatus;
            std::string originatorId;
            log_entry::OriginatorTypes originatorType =
                log_entry::OriginatorTypes::Internal;
            nlohmann::json::object_t thisEntry;
            std::string faultLogDiagnosticDataType;
            std::string notificationType;
            std::string sectionType;
            std::string fruid;
            std::string severity;
            std::string nvipSignature;
            std::string nvSeverity;
            std::string nvSocketNumber;
            std::string pcieVendorID;
            std::string pcieDeviceID;
            std::string pcieClassCode;
            std::string pcieFunctionNumber;
            std::string pcieDeviceNumber;
            std::string pcieSegmentNumber;
            std::string pcieDeviceBusNumber;
            std::string pcieSecondaryBusNumber;
            std::string pcieSlotNumber;

            std::string entryID = object.first.filename();
            if (entryID.empty())
            {
                continue;
            }

            parseDumpEntryFromDbusObject(
                object, dumpStatus, size, timestampUs,
                faultLogDiagnosticDataType, notificationType, sectionType,
                fruid, severity, nvipSignature, nvSeverity, nvSocketNumber,
                pcieVendorID, pcieDeviceID, pcieClassCode, pcieFunctionNumber,
                pcieDeviceNumber, pcieSegmentNumber, pcieDeviceBusNumber,
                pcieSecondaryBusNumber, pcieSlotNumber, originatorId,
                originatorType, asyncResp);

            if (dumpStatus !=
                    "xyz.openbmc_project.Common.Progress.OperationStatus.Completed" &&
                !dumpStatus.empty())
            {
                // Dump status is not Complete, no need to enumerate
                continue;
            }

            thisEntry["@odata.type"] = "#LogEntry.v1_15_0.LogEntry";
            thisEntry["@odata.id"] = entriesPath + entryID;
            thisEntry["Id"] = entryID;
            thisEntry["EntryType"] = "Event";
            thisEntry["Name"] = dumpType + " Dump Entry";
            thisEntry["Created"] =
                redfish::time_utils::getDateTimeUintUs(timestampUs);

            if (!originatorId.empty())
            {
                thisEntry["Originator"] = originatorId;
                thisEntry["OriginatorType"] = originatorType;
            }

            if (dumpType == "BMC")
            {
                thisEntry["DiagnosticDataType"] = "Manager";
                thisEntry["AdditionalDataURI"] = entriesPath + entryID +
                                                 "/attachment";
                thisEntry["AdditionalDataSizeBytes"] = size;
            }
            else if (dumpType == "System")
            {
                thisEntry["DiagnosticDataType"] = "OEM";
                thisEntry["OEMDiagnosticDataType"] = "System";
                thisEntry["AdditionalDataURI"] = entriesPath + entryID +
                                                 "/attachment";
                thisEntry["AdditionalDataSizeBytes"] = size;
            }
            else if (dumpType == "FDR")
            {
                thisEntry["DiagnosticDataType"] = "OEM";
                thisEntry["OEMDiagnosticDataType"] = "FDR";
                thisEntry["AdditionalDataURI"] = boost::urls::format(
                    "{}{}/attachment", entriesPath, entryID);
                thisEntry["AdditionalDataSizeBytes"] = size;
            }
            else if (dumpType == "FaultLog")
            {
                thisEntry["DiagnosticDataType"] = faultLogDiagnosticDataType;
                thisEntry["AdditionalDataURI"] = entriesPath + entryID +
                                                 "/attachment";
                thisEntry["AdditionalDataSizeBytes"] = size;

                std::string messageId = "Platform.1.0.PlatformError";
                thisEntry["MessageId"] = messageId;

                const registries::Message* msg =
                    registries::getMessage(messageId);
                if (msg != nullptr)
                {
                    thisEntry["Message"] = msg->message;
                    thisEntry["Severity"] = msg->messageSeverity;
                    thisEntry["Resolution"] = msg->resolution;
                }

                // CPER Properties
                if (notificationType != "NA")
                {
                    thisEntry["CPER"]["NotificationType"] = notificationType;
                }
                // CPER Oem properties
                thisEntry["CPER"]["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaLogEntry.v1_0_0.CPER";
                if (sectionType != "NA")
                {
                    thisEntry["CPER"]["Oem"]["Nvidia"]["SectionType"] =
                        sectionType;
                }
                if (fruid != "NA")
                {
                    thisEntry["CPER"]["Oem"]["Nvidia"]["FruID"] = fruid;
                }
                if (severity != "NA")
                {
                    thisEntry["CPER"]["Oem"]["Nvidia"]["Severity"] = severity;
                }
                if (nvipSignature != "NA")
                {
                    thisEntry["CPER"]["Oem"]["Nvidia"]["NvIpSignature"] =
                        nvipSignature;
                }
                if (nvSeverity != "NA")
                {
                    thisEntry["CPER"]["Oem"]["Nvidia"]["NvSeverity"] =
                        nvSeverity;
                }
                if (nvSocketNumber != "NA")
                {
                    thisEntry["CPER"]["Oem"]["Nvidia"]["NvSocketNumber"] =
                        nvSocketNumber;
                }
                if (pcieVendorID != "NA")
                {
                    thisEntry["CPER"]["Oem"]["Nvidia"]["PCIeVendorId"] =
                        pcieVendorID;
                }
                if (pcieDeviceID != "NA")
                {
                    thisEntry["CPER"]["Oem"]["Nvidia"]["PCIeDeviceId"] =
                        pcieDeviceID;
                }
                if (pcieClassCode != "NA")
                {
                    thisEntry["CPER"]["Oem"]["Nvidia"]["PCIeClassCode"] =
                        pcieClassCode;
                }
                if (pcieFunctionNumber != "NA")
                {
                    thisEntry["CPER"]["Oem"]["Nvidia"]["PCIeFunctionNumber"] =
                        pcieFunctionNumber;
                }
                if (pcieDeviceNumber != "NA")
                {
                    thisEntry["CPER"]["Oem"]["Nvidia"]["PCIeDeviceNumber"] =
                        pcieDeviceNumber;
                }
                if (pcieSegmentNumber != "NA")
                {
                    thisEntry["CPER"]["Oem"]["Nvidia"]["PCIeSegmentNumber"] =
                        pcieSegmentNumber;
                }
                if (pcieDeviceBusNumber != "NA")
                {
                    thisEntry["CPER"]["Oem"]["Nvidia"]["PCIeDeviceBusNumber"] =
                        pcieDeviceBusNumber;
                }
                if (pcieSecondaryBusNumber != "NA")
                {
                    thisEntry["CPER"]["Oem"]["Nvidia"]
                             ["PCIeSecondaryBusNumber"] =
                                 pcieSecondaryBusNumber;
                }
                if (pcieSlotNumber != "NA")
                {
                    thisEntry["CPER"]["Oem"]["Nvidia"]["PCIeSlotNumber"] =
                        pcieSlotNumber;
                }
            }
            entriesArray.emplace_back(std::move(thisEntry));
        }
        asyncResp->res.jsonValue["Members@odata.count"] = entriesArray.size();
        asyncResp->res.jsonValue["Members"] = std::move(entriesArray);
    });
}

inline void
    getDumpEntryById(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& entryID, const std::string& dumpType)
{
    std::string entriesPath = getDumpEntriesPath(dumpType);
    if (entriesPath.empty())
    {
        messages::internalError(asyncResp->res);
        return;
    }

    sdbusplus::message::object_path path("/xyz/openbmc_project/dump");
    dbus::utility::getManagedObjects(
        "xyz.openbmc_project.Dump.Manager", path,
        [asyncResp, entryID, dumpType,
         entriesPath](const boost::system::error_code& ec,
                      const dbus::utility::ManagedObjectType& resp) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DumpEntry resp_handler got error {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }

        bool foundDumpEntry = false;
        std::string dumpEntryPath = getDumpPath(dumpType) + "/entry/";

        for (const auto& objectPath : resp)
        {
            if (objectPath.first.str != dumpEntryPath + entryID)
            {
                continue;
            }

            foundDumpEntry = true;
            uint64_t timestampUs = 0;
            uint64_t size = 0;
            std::string dumpStatus;
            std::string faultLogDiagnosticDataType;
            std::string notificationType;
            std::string sectionType;
            std::string fruid;
            std::string severity;
            std::string nvipSignature;
            std::string nvSeverity;
            std::string nvSocketNumber;
            std::string pcieVendorID;
            std::string pcieDeviceID;
            std::string pcieClassCode;
            std::string pcieFunctionNumber;
            std::string pcieDeviceNumber;
            std::string pcieSegmentNumber;
            std::string pcieDeviceBusNumber;
            std::string pcieSecondaryBusNumber;
            std::string pcieSlotNumber;
            std::string originatorId;
            log_entry::OriginatorTypes originatorType =
                log_entry::OriginatorTypes::Internal;

            parseDumpEntryFromDbusObject(
                objectPath, dumpStatus, size, timestampUs,
                faultLogDiagnosticDataType, notificationType, sectionType,
                fruid, severity, nvipSignature, nvSeverity, nvSocketNumber,
                pcieVendorID, pcieDeviceID, pcieClassCode, pcieFunctionNumber,
                pcieDeviceNumber, pcieSegmentNumber, pcieDeviceBusNumber,
                pcieSecondaryBusNumber, pcieSlotNumber, originatorId,
                originatorType, asyncResp);

            if (dumpStatus !=
                    "xyz.openbmc_project.Common.Progress.OperationStatus.Completed" &&
                !dumpStatus.empty())
            {
                // Dump status is not Complete
                // return not found until status is changed to Completed
                messages::resourceNotFound(asyncResp->res, dumpType + " dump",
                                           entryID);
                return;
            }

            asyncResp->res.jsonValue["@odata.type"] =
                "#LogEntry.v1_15_0.LogEntry";
            asyncResp->res.jsonValue["@odata.id"] = entriesPath + entryID;
            asyncResp->res.jsonValue["Id"] = entryID;
            asyncResp->res.jsonValue["EntryType"] = "Event";
            asyncResp->res.jsonValue["Name"] = dumpType + " Dump Entry";
            asyncResp->res.jsonValue["Created"] =
                redfish::time_utils::getDateTimeUintUs(timestampUs);

            if (!originatorId.empty())
            {
                asyncResp->res.jsonValue["Originator"] = originatorId;
                asyncResp->res.jsonValue["OriginatorType"] = originatorType;
            }

            asyncResp->res.jsonValue["AdditionalDataSizeBytes"] = size;
            asyncResp->res.jsonValue["Created"] =
                redfish::time_utils::getDateTimeUintUs(timestampUs);

            // Set schema defaults
            asyncResp->res.jsonValue["Message"] = "";
            asyncResp->res.jsonValue["Severity"] = "OK";

            if (dumpType == "BMC")
            {
                asyncResp->res.jsonValue["DiagnosticDataType"] = "Manager";
                asyncResp->res.jsonValue["AdditionalDataURI"] =
                    "/redfish/v1/Managers/" +
                    std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                    "/LogServices/Dump/Entries/" + entryID + "/attachment";
                asyncResp->res.jsonValue["AdditionalDataSizeBytes"] = size;
            }
            else if (dumpType == "System")
            {
                asyncResp->res.jsonValue["DiagnosticDataType"] = "OEM";
                asyncResp->res.jsonValue["OEMDiagnosticDataType"] = "System";
                asyncResp->res.jsonValue["AdditionalDataURI"] =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/LogServices/Dump/Entries/" + entryID + "/attachment";
                asyncResp->res.jsonValue["AdditionalDataSizeBytes"] = size;
            }
            else if (dumpType == "FDR")
            {
                asyncResp->res.jsonValue["DiagnosticDataType"] = "OEM";
                asyncResp->res.jsonValue["OEMDiagnosticDataType"] = "FDR";
                asyncResp->res.jsonValue["AdditionalDataURI"] =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/LogServices/FDR/Entries/" + entryID + "/attachment";
                asyncResp->res.jsonValue["AdditionalDataSizeBytes"] = size;
            }
            else if (dumpType == "FaultLog")
            {
                asyncResp->res.jsonValue["DiagnosticDataType"] =
                    faultLogDiagnosticDataType;
                asyncResp->res.jsonValue["AdditionalDataURI"] =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/LogServices/FaultLog/Entries/" + entryID + "/attachment";

                std::string messageId = "Platform.1.0.PlatformError";
                asyncResp->res.jsonValue["MessageId"] = messageId;

                const registries::Message* msg =
                    registries::getMessage(messageId);
                if (msg != nullptr)
                {
                    asyncResp->res.jsonValue["Message"] = msg->message;
                    asyncResp->res.jsonValue["Severity"] = msg->messageSeverity;
                    asyncResp->res.jsonValue["Resolution"] = msg->resolution;
                }

                // CPER Properties
                if (notificationType != "NA")
                {
                    asyncResp->res.jsonValue["CPER"]["NotificationType"] =
                        notificationType;
                }
                // CPER Oem properties
                asyncResp->res
                    .jsonValue["CPER"]["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaLogEntry.v1_0_0.CPER";
                if (sectionType != "NA")
                {
                    asyncResp->res.jsonValue["CPER"]["Oem"]["Nvidia"]
                                            ["SectionType"] = sectionType;
                }
                if (fruid != "NA")
                {
                    asyncResp->res.jsonValue["CPER"]["Oem"]["Nvidia"]["FruID"] =
                        fruid;
                }
                if (severity != "NA")
                {
                    asyncResp->res.jsonValue["CPER"]["Oem"]["Nvidia"]
                                            ["Severity"] = severity;
                }
                if (nvipSignature != "NA")
                {
                    asyncResp->res.jsonValue["CPER"]["Oem"]["Nvidia"]
                                            ["NvIpSignature"] = nvipSignature;
                }
                if (nvSeverity != "NA")
                {
                    asyncResp->res.jsonValue["CPER"]["Oem"]["Nvidia"]
                                            ["NvSeverity"] = nvSeverity;
                }
                if (nvSocketNumber != "NA")
                {
                    asyncResp->res.jsonValue["CPER"]["Oem"]["Nvidia"]
                                            ["NvSocketNumber"] = nvSocketNumber;
                }
                if (pcieVendorID != "NA")
                {
                    asyncResp->res.jsonValue["CPER"]["Oem"]["Nvidia"]
                                            ["PCIeVendorId"] = pcieVendorID;
                }
                if (pcieDeviceID != "NA")
                {
                    asyncResp->res.jsonValue["CPER"]["Oem"]["Nvidia"]
                                            ["PCIeDeviceId"] = pcieDeviceID;
                }
                if (pcieClassCode != "NA")
                {
                    asyncResp->res.jsonValue["CPER"]["Oem"]["Nvidia"]
                                            ["PCIeClassCode"] = pcieClassCode;
                }
                if (pcieFunctionNumber != "NA")
                {
                    asyncResp->res.jsonValue["CPER"]["Oem"]["Nvidia"]
                                            ["PCIeFunctionNumber"] =
                        pcieFunctionNumber;
                }
                if (pcieDeviceNumber != "NA")
                {
                    asyncResp->res.jsonValue["CPER"]["Oem"]["Nvidia"]
                                            ["PCIeDeviceNumber"] =
                        pcieDeviceNumber;
                }
                if (pcieSegmentNumber != "NA")
                {
                    asyncResp->res.jsonValue["CPER"]["Oem"]["Nvidia"]
                                            ["PCIeSegmentNumber"] =
                        pcieSegmentNumber;
                }
                if (pcieDeviceBusNumber != "NA")
                {
                    asyncResp->res.jsonValue["CPER"]["Oem"]["Nvidia"]
                                            ["PCIeDeviceBusNumber"] =
                        pcieDeviceBusNumber;
                }
                if (pcieSecondaryBusNumber != "NA")
                {
                    asyncResp->res.jsonValue["CPER"]["Oem"]["Nvidia"]
                                            ["PCIeSecondaryBusNumber"] =
                        pcieSecondaryBusNumber;
                }
                if (pcieSlotNumber != "NA")
                {
                    asyncResp->res.jsonValue["CPER"]["Oem"]["Nvidia"]
                                            ["PCIeSlotNumber"] = pcieSlotNumber;
                }
                asyncResp->res.jsonValue["AdditionalDataSizeBytes"] = size;
            }
        }

        if (!foundDumpEntry)
        {
            BMCWEB_LOG_ERROR("Can't find Dump Entry {}", entryID);
            messages::resourceNotFound(asyncResp->res, dumpType + " dump",
                                       entryID);
            return;
        }
    });
}

inline void deleteDumpEntry(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& entryID,
                            const std::string& dumpType)
{
    auto respHandler = [asyncResp,
                        entryID](const boost::system::error_code& ec) {
        BMCWEB_LOG_DEBUG("Dump Entry doDelete callback: Done");
        if (ec)
        {
            if (ec.value() == EBADR)
            {
                messages::resourceNotFound(asyncResp->res, "LogEntry", entryID);
                return;
            }
            BMCWEB_LOG_ERROR(
                "Dump (DBus) doDelete respHandler got error {} entryID={}", ec,
                entryID);
            messages::internalError(asyncResp->res);
            return;
        }
    };

    crow::connections::systemBus->async_method_call(
        respHandler, "xyz.openbmc_project.Dump.Manager",
        std::format("{}/entry/{}", getDumpPath(dumpType), entryID),
        "xyz.openbmc_project.Object.Delete", "Delete");
}
inline bool checkSizeLimit(int fd, crow::Response& res)
{
    long long int size = lseek(fd, 0, SEEK_END);
    if (size <= 0)
    {
        BMCWEB_LOG_ERROR("Failed to get size of file, lseek() returned {}",
                         size);
        messages::internalError(res);
        return false;
    }

#ifdef BMCWEB_ENABLE_REDFISH_FDR_DUMP_LOG
    // "The maximum size of FDR dump is 1.5GB
    constexpr long long int maxFileSize = 1500 * 1024LL * 1024LL;
#else
    // Arbitrary max size of 20MB to accommodate BMC dumps
    constexpr long long int maxFileSize = 20LL * 1024LL * 1024LL;
#endif // #ifdef BMCWEB_ENABLE_REDFISH_FDR_DUMP_LOG
    if (size > maxFileSize)
    {
        BMCWEB_LOG_ERROR("File size {} exceeds maximum allowed size of {}",
                         size, maxFileSize);
        messages::internalError(res);
        return false;
    }
    off_t rc = lseek(fd, 0, SEEK_SET);
    if (rc < 0)
    {
        BMCWEB_LOG_ERROR("Failed to reset file offset to 0");
        messages::internalError(res);
        return false;
    }
    return true;
}
inline void
    downloadEntryCallback(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& entryID,
                          const std::string& downloadEntryType,
                          const boost::system::error_code& ec,
                          const sdbusplus::message::unix_fd& unixfd)
{
    if (ec.value() == EBADR)
    {
        messages::resourceNotFound(asyncResp->res, "EntryAttachment", entryID);
        return;
    }
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    // Make sure we know how to process the retrieved entry attachment
    if ((downloadEntryType != "BMC") && (downloadEntryType != "System") &&
        (downloadEntryType != "FDR"))
    {
        BMCWEB_LOG_ERROR("downloadEntryCallback() invalid entry type: {}",
                         downloadEntryType);
        messages::internalError(asyncResp->res);
        return;
    }

    int fd = -1;
    fd = dup(unixfd);
    if (fd < 0)
    {
        BMCWEB_LOG_ERROR("Failed to open file");
        messages::internalError(asyncResp->res);
        return;
    }
    if (!checkSizeLimit(fd, asyncResp->res))
    {
        close(fd);
        return;
    }
    if (downloadEntryType == "System")
    {
        if (!asyncResp->res.openFd(fd, bmcweb::EncodingType::Base64))
        {
            messages::internalError(asyncResp->res);
            close(fd);
            return;
        }
        asyncResp->res.addHeader(
            boost::beast::http::field::content_transfer_encoding, "Base64");
        return;
    }
    else if (downloadEntryType == "FDR")
    {
        if (!asyncResp->res.openFd(fd, bmcweb::EncodingType::Raw))
        {
            messages::internalError(asyncResp->res);
            close(fd);
            return;
        }

        return;
    }
    if (!asyncResp->res.openFd(fd))
    {
        messages::internalError(asyncResp->res);
        close(fd);
        return;
    }
    asyncResp->res.addHeader(boost::beast::http::field::content_type,
                             "application/octet-stream");
}

inline void
    downloadDumpEntry(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& entryID, const std::string& dumpType)
{
    if (dumpType != "BMC")
    {
        BMCWEB_LOG_WARNING("Can't find Dump Entry {}", entryID);
        messages::resourceNotFound(asyncResp->res, dumpType + " dump", entryID);
        return;
    }

    std::string dumpEntryPath = std::format("{}/entry/{}",
                                            getDumpPath(dumpType), entryID);

    auto downloadDumpEntryHandler =
        [asyncResp, entryID,
         dumpType](const boost::system::error_code& ec,
                   const sdbusplus::message::unix_fd& unixfd) {
        downloadEntryCallback(asyncResp, entryID, dumpType, ec, unixfd);
    };

    crow::connections::systemBus->async_method_call(
        std::move(downloadDumpEntryHandler), "xyz.openbmc_project.Dump.Manager",
        dumpEntryPath, "xyz.openbmc_project.Dump.Entry", "GetFileHandle");
}

inline void
    downloadEventLogEntry(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& systemName,
                          const std::string& entryID,
                          const std::string& dumpType)
{
    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }

    std::string entryPath =
        sdbusplus::message::object_path("/xyz/openbmc_project/logging/entry") /
        entryID;

    auto downloadEventLogEntryHandler =
        [asyncResp, entryID,
         dumpType](const boost::system::error_code& ec,
                   const sdbusplus::message::unix_fd& unixfd) {
        downloadEntryCallback(asyncResp, entryID, dumpType, ec, unixfd);
    };

    crow::connections::systemBus->async_method_call(
        std::move(downloadEventLogEntryHandler), "xyz.openbmc_project.Logging",
        entryPath, "xyz.openbmc_project.Logging.Entry", "GetEntry");
}

inline DumpCreationProgress
    mapDbusStatusToDumpProgress(const std::string& status)
{
    if (status ==
            "xyz.openbmc_project.Common.Progress.OperationStatus.Failed" ||
        status == "xyz.openbmc_project.Common.Progress.OperationStatus.Aborted")
    {
        return DumpCreationProgress::DUMP_CREATE_FAILED;
    }
    if (status ==
        "xyz.openbmc_project.Common.Progress.OperationStatus.Completed")
    {
        return DumpCreationProgress::DUMP_CREATE_SUCCESS;
    }
    return DumpCreationProgress::DUMP_CREATE_INPROGRESS;
}

inline DumpCreationProgress
    getDumpCompletionStatus(const dbus::utility::DBusPropertiesMap& values)
{
    for (const auto& [key, val] : values)
    {
        if (key == "Status")
        {
            const std::string* value = std::get_if<std::string>(&val);
            if (value == nullptr)
            {
                BMCWEB_LOG_ERROR("Status property value is null");
                return DumpCreationProgress::DUMP_CREATE_FAILED;
            }
            return mapDbusStatusToDumpProgress(*value);
        }
    }
    return DumpCreationProgress::DUMP_CREATE_INPROGRESS;
}

inline std::string getDumpEntryPath(const std::string& dumpPath)
{
    if (dumpPath == "/xyz/openbmc_project/dump/bmc/entry")
    {
        return std::format("/redfish/v1/Managers/{}/LogServices/Dump/Entries/",
                           BMCWEB_REDFISH_MANAGER_URI_NAME);
    }
    if (dumpPath == "/xyz/openbmc_project/dump/system/entry")
    {
        return std::format("/redfish/v1/Systems/{}/LogServices/Dump/Entries/",
                           BMCWEB_REDFISH_SYSTEM_URI_NAME);
    }
    if (dumpPath == "/xyz/openbmc_project/dump/fdr/entry")
    {
        return "/redfish/v1/Systems/" +
               std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
               "/LogServices/FDR/Entries/";
    }
    return "";
}

inline void createDumpTaskCallback(
    task::Payload&& payload,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const sdbusplus::message::object_path& createdObjPath)
{
    const std::string dumpPath = createdObjPath.parent_path().str;
    const std::string dumpId = createdObjPath.filename();

    std::string dumpEntryPath = getDumpEntryPath(dumpPath);

    if (dumpEntryPath.empty())
    {
        BMCWEB_LOG_ERROR("Invalid dump type received");
        messages::internalError(asyncResp->res);
        return;
    }

    crow::connections::systemBus->async_method_call(
        [asyncResp, payload = std::move(payload), createdObjPath,
         dumpEntryPath{std::move(dumpEntryPath)},
         dumpId](const boost::system::error_code& ec,
                 const std::string& introspectXml) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Introspect call failed with error: {}",
                             ec.message());
            messages::internalError(asyncResp->res);
            return;
        }

        // Check if the created dump object has implemented Progress
        // interface to track dump completion. If yes, fetch the "Status"
        // property of the interface, modify the task state accordingly.
        // Else, return task completed.
        tinyxml2::XMLDocument doc;

        doc.Parse(introspectXml.data(), introspectXml.size());
        tinyxml2::XMLNode* pRoot = doc.FirstChildElement("node");
        if (pRoot == nullptr)
        {
            BMCWEB_LOG_ERROR("XML document failed to parse");
            messages::internalError(asyncResp->res);
            return;
        }
        tinyxml2::XMLElement* interfaceNode =
            pRoot->FirstChildElement("interface");

        bool isProgressIntfPresent = false;
        while (interfaceNode != nullptr)
        {
            const char* thisInterfaceName = interfaceNode->Attribute("name");
            if (thisInterfaceName != nullptr)
            {
                if (thisInterfaceName ==
                    std::string_view("xyz.openbmc_project.Common.Progress"))
                {
                    interfaceNode =
                        interfaceNode->NextSiblingElement("interface");
                    continue;
                }
                isProgressIntfPresent = true;
                break;
            }
            interfaceNode = interfaceNode->NextSiblingElement("interface");
        }

        std::shared_ptr<task::TaskData> task = task::TaskData::createTask(
            [createdObjPath, dumpEntryPath, dumpId, isProgressIntfPresent](
                const boost::system::error_code& ec2, sdbusplus::message_t& msg,
                const std::shared_ptr<task::TaskData>& taskData) {
            if (ec2)
            {
                BMCWEB_LOG_ERROR("{}: Error in creating dump",
                                 createdObjPath.str);
                taskData->messages.emplace_back(messages::internalError());
                taskData->state = "Cancelled";
                return task::completed;
            }

            if (isProgressIntfPresent)
            {
                dbus::utility::DBusPropertiesMap values;
                std::string prop;
                msg.read(prop, values);

                DumpCreationProgress dumpStatus =
                    getDumpCompletionStatus(values);
                if (dumpStatus == DumpCreationProgress::DUMP_CREATE_FAILED)
                {
                    BMCWEB_LOG_ERROR("{}: Error in creating dump",
                                     createdObjPath.str);
                    taskData->state = "Cancelled";
                    nlohmann::json retMessage = messages::operationFailed();
                    taskData->messages.emplace_back(retMessage);
                    return task::completed;
                }

                if (dumpStatus == DumpCreationProgress::DUMP_CREATE_INPROGRESS)
                {
                    BMCWEB_LOG_DEBUG("{}: Dump creation task is in progress",
                                     createdObjPath.str);

                    auto valuesIter = std::find_if(
                        values.begin(), values.end(),
                        [](const std::pair<std::string,
                                           dbus::utility::DbusVariantType>&
                               property) -> bool {
                        return property.first == "Progress";
                    });

                    if (valuesIter != values.end())
                    {
                        const uint8_t* progress =
                            std::get_if<uint8_t>(&(valuesIter->second));
                        if (progress != nullptr)
                        {
                            taskData->percentComplete =
                                static_cast<int>(*progress);
                            taskData->messages.emplace_back(
                                messages::taskProgressChanged(
                                    std::to_string(taskData->index),
                                    static_cast<size_t>(*progress)));
                        }
                    }

                    return !task::completed;
                }
            }

            nlohmann::json retMessage = messages::success();
            taskData->messages.emplace_back(retMessage);

            boost::urls::url url = boost::urls::format("{}{}", dumpEntryPath,
                                                       dumpId);

            std::string headerLoc = "Location: ";
            headerLoc += url.buffer();

            taskData->payload->httpHeaders.emplace_back(std::move(headerLoc));

            BMCWEB_LOG_DEBUG("{}: Dump creation task completed",
                             createdObjPath.str);
            taskData->state = "Completed";
            taskData->percentComplete = 100;
            return task::completed;
        },
            "type='signal',interface='org.freedesktop.DBus.Properties',"
            "member='PropertiesChanged',path='" +
                createdObjPath.str + "'");

        // The task timer is set to max time limit within which the
        // requested dump will be collected.
        task->startTimer(std::chrono::minutes(45));
        task->populateResp(asyncResp->res);
        task->payload.emplace(payload);
    },
        "xyz.openbmc_project.Dump.Manager", createdObjPath,
        "org.freedesktop.DBus.Introspectable", "Introspect");
}

inline void createDump(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const crow::Request& req, const std::string& dumpType)
{
    std::string dumpPath = getDumpEntriesPath(dumpType);
    if (dumpPath.empty())
    {
        messages::internalError(asyncResp->res);
        return;
    }

    std::optional<std::string> diagnosticDataType;
    std::optional<std::string> oemDiagnosticDataType;
    std::vector<std::pair<std::string, std::variant<std::string, uint64_t>>>
        createDumpParamVec;

    if (!redfish::json_util::readJsonAction(
            req, asyncResp->res, "DiagnosticDataType", diagnosticDataType,
            "OEMDiagnosticDataType", oemDiagnosticDataType))
    {
        return;
    }

    if (dumpType == "System")
    {
        // Decode oemDiagnosticDataType string format
        createDumpParamVec = parseOEMAdditionalData(*oemDiagnosticDataType);

        if (!oemDiagnosticDataType || !diagnosticDataType)
        {
            BMCWEB_LOG_ERROR(
                "CreateDump action parameter 'DiagnosticDataType'/'OEMDiagnosticDataType' value not found!");
            messages::actionParameterMissing(
                asyncResp->res, "CollectDiagnosticData",
                "DiagnosticDataType & OEMDiagnosticDataType");
            return;
        }

        if (*diagnosticDataType != "OEM")
        {
            BMCWEB_LOG_ERROR("Wrong parameter values passed");
            messages::actionParameterValueError(
                asyncResp->res, "DiagnosticDataType",
                "LogService.CollectDiagnosticData");
            return;
        }

        bool isValidParam = false;
        for (const auto& dumpPara : createDumpParamVec)
        {
            if (dumpPara.first == "DiagnosticType")
            {
                const std::string* oemDiagType =
                    std::get_if<std::string>(&dumpPara.second);
                if (oemDiagType == nullptr)
                {
                    continue;
                }
                auto it = std::ranges::find(
                    BMCWEB_OEM_DIAGNOSTIC_ALLOWABLE_TYPE, *oemDiagType);
                if (it !=
                    std::ranges::end(BMCWEB_OEM_DIAGNOSTIC_ALLOWABLE_TYPE))
                {
                    isValidParam = true;
                    break;
                }
            }
        }

        if (isValidParam == false)
        {
            BMCWEB_LOG_ERROR("Wrong parameter values passed");
            messages::actionParameterValueError(
                asyncResp->res, "OEMDiagnosticDataType",
                "LogService.CollectDiagnosticData");
            return;
        }

        dumpPath = "/redfish/v1/Systems/" +
                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                   "/LogServices/Dump/";
    }
    else if (dumpType == "FDR")
    {
        // Decode oemDiagnosticDataType string format
        createDumpParamVec = parseOEMAdditionalData(*oemDiagnosticDataType);

        if (!oemDiagnosticDataType || !diagnosticDataType)
        {
            BMCWEB_LOG_ERROR(
                "CreateDump action parameter 'DiagnosticDataType'/'OEMDiagnosticDataType' value not found!");
            messages::actionParameterMissing(
                asyncResp->res, "CollectDiagnosticData",
                "DiagnosticDataType & OEMDiagnosticDataType");
            return;
        }

        if (*diagnosticDataType != "OEM")
        {
            BMCWEB_LOG_ERROR("Wrong parameter values passed");
            messages::actionParameterValueError(
                asyncResp->res, "DiagnosticDataType",
                "LogService.CollectDiagnosticData");
            return;
        }

        bool isValidParam = false;
        for (const auto& dumpPara : createDumpParamVec)
        {
            if (dumpPara.first == "DiagnosticType")
            {
                const std::string* oemDiagType =
                    std::get_if<std::string>(&dumpPara.second);
                if (*oemDiagType == "FDR")
                {
                    isValidParam = true;
                    break;
                }
            }
        }

        if (isValidParam == false)
        {
            BMCWEB_LOG_ERROR("Wrong parameter values passed");
            messages::actionParameterValueError(
                asyncResp->res, "OEMDiagnosticDataType",
                "LogService.CollectDiagnosticData");
            return;
        }

        dumpPath = "/redfish/v1/Systems/" +
                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                   "/LogServices/FDR/";
    }
    else if (dumpType == "BMC")
    {
        if (!diagnosticDataType)
        {
            BMCWEB_LOG_ERROR(
                "CreateDump action parameter 'DiagnosticDataType' not found!");
            messages::actionParameterMissing(
                asyncResp->res, "CollectDiagnosticData", "DiagnosticDataType");
            return;
        }
        if (*diagnosticDataType != "Manager")
        {
            BMCWEB_LOG_ERROR(
                "Wrong parameter value passed for 'DiagnosticDataType'");
            messages::actionParameterValueError(
                asyncResp->res, "DiagnosticDataType",
                "LogService.CollectDiagnosticData");
            return;
        }
        dumpPath = std::format("/redfish/v1/Managers/{}/LogServices/Dump/",
                               BMCWEB_REDFISH_MANAGER_URI_NAME);
    }
    else
    {
        BMCWEB_LOG_ERROR("CreateDump failed. Unknown dump type");
        messages::internalError(asyncResp->res);
        return;
    }

    if (req.session != nullptr)
    {
        createDumpParamVec.emplace_back(
            "xyz.openbmc_project.Dump.Create.CreateParameters.OriginatorId",
            req.session->clientIp);
        createDumpParamVec.emplace_back(
            "xyz.openbmc_project.Dump.Create.CreateParameters.OriginatorType",
            "xyz.openbmc_project.Common.OriginatedBy.OriginatorTypes.Client");
    }

    crow::connections::systemBus->async_method_call(
        [asyncResp, payload(task::Payload(req)), dumpPath,
         oemDiagnosticDataType](
            const boost::system::error_code ec,
            const sdbusplus::message::message& msg,
            const sdbusplus::message::object_path& objPath) mutable {
        if (ec)
        {
            BMCWEB_LOG_ERROR("CreateDump resp_handler got error {}", ec);
            const sd_bus_error* dbusError = msg.get_error();
            if (dbusError == nullptr)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            BMCWEB_LOG_ERROR("CreateDump DBus error: {} and error msg: {}",
                             dbusError->name, dbusError->message);
            if (std::string_view(
                    "xyz.openbmc_project.Common.Error.NotAllowed") ==
                dbusError->name)
            {
                messages::resourceInStandby(asyncResp->res);
                return;
            }
            if (std::string_view(
                    "xyz.openbmc_project.Dump.Create.Error.Disabled") ==
                dbusError->name)
            {
                messages::serviceDisabled(asyncResp->res, dumpPath);
                return;
            }
            if (std::string_view(
                    "xyz.openbmc_project.Common.Error.Unavailable") ==
                dbusError->name)
            {
                messages::resourceInUse(asyncResp->res);
                return;
            }
            if (std::string_view(
                    "xyz.openbmc_project.Dump.Create.Error.QuotaExceeded") ==
                dbusError->name)
            {
                messages::createLimitReachedForResource(asyncResp->res);
                return;
            }
            if (std::string_view(
                    "xyz.openbmc_project.Common.Error.InvalidArgument") ==
                dbusError->name)
            {
                messages::propertyValueIncorrect(
                    asyncResp->res, "DiagnosticType", *oemDiagnosticDataType);
                return;
            }
            // Other Dbus errors such as:
            // xyz.openbmc_project.Common.Error.InvalidArgument &
            // org.freedesktop.DBus.Error.InvalidArgs are all related to
            // the dbus call that is made here in the bmcweb
            // implementation and has nothing to do with the client's
            // input in the request. Hence, returning internal error
            // back to the client.
            messages::internalError(asyncResp->res);
            return;
        }
        BMCWEB_LOG_DEBUG("Dump Created. Path: {}", objPath.str);
        createDumpTaskCallback(std::move(payload), asyncResp, objPath);
    },
        "xyz.openbmc_project.Dump.Manager", getDumpPath(dumpType),
        "xyz.openbmc_project.Dump.Create", "CreateDump", createDumpParamVec);
}

inline void clearDump(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& dumpType)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code& ec) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("clearDump resp_handler got error {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }
    },
        "xyz.openbmc_project.Dump.Manager", getDumpPath(dumpType),
        "xyz.openbmc_project.Collection.DeleteAll", "DeleteAll");
}

inline void
    parseCrashdumpParameters(const dbus::utility::DBusPropertiesMap& params,
                             std::string& filename, std::string& timestamp,
                             std::string& logfile)
{
    const std::string* filenamePtr = nullptr;
    const std::string* timestampPtr = nullptr;
    const std::string* logfilePtr = nullptr;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), params, "Timestamp", timestampPtr,
        "Filename", filenamePtr, "Log", logfilePtr);

    if (!success)
    {
        return;
    }

    if (filenamePtr != nullptr)
    {
        filename = *filenamePtr;
    }

    if (timestampPtr != nullptr)
    {
        timestamp = *timestampPtr;
    }

    if (logfilePtr != nullptr)
    {
        logfile = *logfilePtr;
    }
}

inline void requestRoutesSystemLogServiceCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/")
        .privileges(redfish::privileges::getLogServiceCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       systemName);
            return;
        }
        if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
        {
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       systemName);
            return;
        }

        // Collections don't include the static data added by SubRoute
        // because it has a duplicate entry for members
        asyncResp->res.jsonValue["@odata.type"] =
            "#LogServiceCollection.LogServiceCollection";
        asyncResp->res.jsonValue["@odata.id"] =
            std::format("/redfish/v1/Systems/{}/LogServices",
                        BMCWEB_REDFISH_SYSTEM_URI_NAME);
        asyncResp->res.jsonValue["Name"] = "System Log Services Collection";
        asyncResp->res.jsonValue["Description"] =
            "Collection of LogServices for this Computer System";
        nlohmann::json& logServiceArray = asyncResp->res.jsonValue["Members"];
        logServiceArray = nlohmann::json::array();
        nlohmann::json::object_t eventLog;
#ifdef BMCWEB_ENABLE_HOST_OS_FEATURE
        nlohmann::json::object_t selLog;
        selLog["@odata.id"] = "/redfish/v1/Systems/" +
                              std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                              "/LogServices/SEL";
        logServiceArray.push_back(std::move(selLog));
#endif

#ifdef BMCWEB_ENABLE_REDFISH_FDR_DUMP_LOG
        nlohmann::json::object_t fdrLog;
        fdrLog["@odata.id"] = "/redfish/v1/Systems/" +
                              std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                              "/LogServices/FDR";
        logServiceArray.push_back(std::move(fdrLog));
#endif // BMCWEB_ENABLE_REDFISH_FDR_DUMP_LOG

#ifdef BMCWEB_ENABLE_REDFISH_SYSTEM_FAULTLOG_DUMP_LOG
        nlohmann::json::object_t faultLog;
        faultLog["@odata.id"] = "/redfish/v1/Systems/" +
                                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                "/LogServices/FaultLog";
        logServiceArray.push_back(std::move(faultLog));
#endif // BMCWEB_ENABLE_REDFISH_SYSTEM_FAULTLOG_DUMP_LOG
        eventLog["@odata.id"] =
            std::format("/redfish/v1/Systems/{}/LogServices/EventLog",
                        BMCWEB_REDFISH_SYSTEM_URI_NAME);
        logServiceArray.emplace_back(std::move(eventLog));
        if constexpr (BMCWEB_REDFISH_DUMP_LOG)
        {
            nlohmann::json::object_t dumpLog;
            dumpLog["@odata.id"] =
                std::format("/redfish/v1/Systems/{}/LogServices/Dump",
                            BMCWEB_REDFISH_SYSTEM_URI_NAME);
            logServiceArray.emplace_back(std::move(dumpLog));
        }

        if constexpr (BMCWEB_REDFISH_CPU_LOG)
        {
            nlohmann::json::object_t crashdump;
            crashdump["@odata.id"] =
                std::format("/redfish/v1/Systems/{}/LogServices/Crashdump",
                            BMCWEB_REDFISH_SYSTEM_URI_NAME);
            logServiceArray.emplace_back(std::move(crashdump));
        }

        if constexpr (BMCWEB_REDFISH_HOST_LOGGER)
        {
            nlohmann::json::object_t hostlogger;
            hostlogger["@odata.id"] =
                std::format("/redfish/v1/Systems/{}/LogServices/HostLogger",
                            BMCWEB_REDFISH_SYSTEM_URI_NAME);
            logServiceArray.emplace_back(std::move(hostlogger));
        }

        nlohmann::json::object_t debugToken;
        debugToken["@odata.id"] = "/redfish/v1/Systems/" +
                                  std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                  "/LogServices/DebugTokenService";
        logServiceArray.push_back(std::move(debugToken));

        asyncResp->res.jsonValue["Members@odata.count"] =
            logServiceArray.size();

        constexpr std::array<std::string_view, 1> interfaces = {
            "xyz.openbmc_project.State.Boot.PostCode"};
        dbus::utility::getSubTreePaths(
            "/", 0, interfaces,
            [asyncResp](const boost::system::error_code& ec,
                        const dbus::utility::MapperGetSubTreePathsResponse&
                            subtreePath) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("{}", ec);
                return;
            }

            for (const auto& pathStr : subtreePath)
            {
                if (pathStr.find("PostCode") != std::string::npos)
                {
                    nlohmann::json& logServiceArrayLocal =
                        asyncResp->res.jsonValue["Members"];
                    nlohmann::json::object_t member;
                    member["@odata.id"] = std::format(
                        "/redfish/v1/Systems/{}/LogServices/PostCodes",
                        BMCWEB_REDFISH_SYSTEM_URI_NAME);

                    logServiceArrayLocal.emplace_back(std::move(member));

                    asyncResp->res.jsonValue["Members@odata.count"] =
                        logServiceArrayLocal.size();
                    return;
                }
            }
        });
    });
}

inline void requestRoutesEventLogService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/EventLog/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& systemName) {
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
        asyncResp->res.jsonValue["@odata.id"] =
            std::format("/redfish/v1/Systems/{}/LogServices/EventLog",
                        BMCWEB_REDFISH_SYSTEM_URI_NAME);
        asyncResp->res.jsonValue["@odata.type"] =
            "#LogService.v1_2_0.LogService";
        asyncResp->res.jsonValue["Name"] = "Event Log Service";
        asyncResp->res.jsonValue["Description"] = "System Event Log Service";
        asyncResp->res.jsonValue["Id"] = "EventLog";
        asyncResp->res.jsonValue["OverWritePolicy"] = "WrapsWhenFull";

        std::pair<std::string, std::string> redfishDateTimeOffset =
            redfish::time_utils::getDateTimeOffsetNow();

        asyncResp->res.jsonValue["DateTime"] = redfishDateTimeOffset.first;
        asyncResp->res.jsonValue["DateTimeLocalOffset"] =
            redfishDateTimeOffset.second;

        // Call Phosphor-logging GetStats method to get
        // LatestEntryTimestamp and LatestEntryID
        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code ec,
                        const std::tuple<uint32_t, uint64_t>& reqData) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "Failed to get Data from xyz.openbmc_project.Logging GetStats: {}",
                    ec);
                messages::internalError(asyncResp->res);
                return;
            }
            auto lastTimeStamp =
                redfish::time_utils::getTimestamp(std::get<1>(reqData));
            if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
            {
                asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaLogService.v1_3_0.NvidiaLogService";
            }
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["LatestEntryID"] =
                std::to_string(std::get<0>(reqData));
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["LatestEntryTimeStamp"] =
                redfish::time_utils::getDateTimeStdtime(lastTimeStamp);
        },
            "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
            "xyz.openbmc_project.Logging.Namespace", "GetStats", "all");

        if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
        {
            if (bmcwebEnableNvidiaBootEntryId)
            {
                populateBootEntryId(asyncResp->res);
            }

            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec,
                            std::variant<bool>& resp) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "Failed to get Data from xyz.openbmc_project.Logging: {}",
                        ec);
                    messages::internalError(asyncResp->res);
                    return;
                }
                const bool* state = std::get_if<bool>(&resp);
                asyncResp->res
                    .jsonValue["Oem"]["Nvidia"]["AutoClearResolvedLogEnabled"] =
                    *state;
            },
                "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
                "org.freedesktop.DBus.Properties", "Get",
                "xyz.openbmc_project.Logging.Namespace",
                "AutoClearResolvedLogEnabled");
        }

        asyncResp->res.jsonValue["Entries"] = {
            {"@odata.id", "/redfish/v1/Systems/" +
                              std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                              "/LogServices/EventLog/Entries"}};
        asyncResp->res.jsonValue["Actions"]["#LogService.ClearLog"] = {

            {"target",
             "/redfish/v1/Systems/" +
                 std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                 "/LogServices/EventLog/Actions/LogService.ClearLog"}};
    });

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/LogServices/EventLog/")
        .privileges(redfish::privileges::patchLogService)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::optional<nlohmann::json> oemObject;

        if (!json_util::readJsonPatch(req, asyncResp->res, "Oem", oemObject))
        {
            return;
        }

        std::optional<nlohmann::json> oemNvidiaObject;

        if (!json_util::readJson(*oemObject, asyncResp->res, "Nvidia",
                                 oemNvidiaObject))
        {
            return;
        }

        std::optional<bool> autoClearResolvedLogEnabled;

        if (!json_util::readJson(*oemNvidiaObject, asyncResp->res,
                                 "AutoClearResolvedLogEnabled",
                                 autoClearResolvedLogEnabled))
        {
            return;
        }
        BMCWEB_LOG_DEBUG("Set Log Purge Policy");

        if (autoClearResolvedLogEnabled)
        {
            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                    messages::internalError(asyncResp->res);
                    return;
                }
            },
                "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
                "org.freedesktop.DBus.Properties", "Set",
                "xyz.openbmc_project.Logging.Namespace",
                "AutoClearResolvedLogEnabled",
                dbus::utility::DbusVariantType(*autoClearResolvedLogEnabled));
        }
    });
}

inline void requestRoutesSELLogService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/SEL/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) + "/LogServices/SEL";
        asyncResp->res.jsonValue["@odata.type"] =
            "#LogService.v1_1_0.LogService";
        asyncResp->res.jsonValue["Name"] = "SEL Log Service";
        asyncResp->res.jsonValue["Description"] = "IPMI SEL Service";
        asyncResp->res.jsonValue["Id"] = "SEL";
        asyncResp->res.jsonValue["OverWritePolicy"] = "WrapsWhenFull";

        std::pair<std::string, std::string> redfishDateTimeOffset =
            redfish::time_utils::getDateTimeOffsetNow();

        asyncResp->res.jsonValue["DateTime"] = redfishDateTimeOffset.first;
        asyncResp->res.jsonValue["DateTimeLocalOffset"] =
            redfishDateTimeOffset.second;

        asyncResp->res.jsonValue["Entries"] = {
            {"@odata.id", "/redfish/v1/Systems/" +
                              std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                              "/LogServices/SEL/Entries"}};
        asyncResp->res.jsonValue["Actions"]["#LogService.ClearLog"] = {

            {"target", "/redfish/v1/Systems/" +
                           std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                           "/LogServices/SEL/"
                           "Actions/LogService.ClearLog"}};
    });
}

inline void requestRoutesJournalEventLogClear(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/EventLog/Actions/"
                      "LogService.ClearLog/")
        .privileges({{"ConfigureComponents"}})
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        // Clear the EventLog by deleting the log files
        std::vector<std::filesystem::path> redfishLogFiles;
        if (getRedfishLogFiles(redfishLogFiles))
        {
            for (const std::filesystem::path& file : redfishLogFiles)
            {
                std::error_code ec;
                std::filesystem::remove(file, ec);
            }
        }

        // Reload rsyslog so it knows to start new log files
        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Failed to reload rsyslog: {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }

            messages::success(asyncResp->res);
        },
            "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
            "org.freedesktop.systemd1.Manager", "ReloadUnit", "rsyslog.service",
            "replace");
    });
}

enum class LogParseError
{
    success,
    parseFailed,
    messageIdNotInRegistry,
};

static LogParseError
    fillEventLogEntryJson(const std::string& logEntryID,
                          const std::string& logEntry,
                          nlohmann::json::object_t& logEntryJson)
{
    // The redfish log format is "<Timestamp> <MessageId>,<MessageArgs>"
    // First get the Timestamp
    size_t space = logEntry.find_first_of(' ');
    if (space == std::string::npos)
    {
        return LogParseError::parseFailed;
    }
    std::string timestamp = logEntry.substr(0, space);
    // Then get the log contents
    size_t entryStart = logEntry.find_first_not_of(' ', space);
    if (entryStart == std::string::npos)
    {
        return LogParseError::parseFailed;
    }
    std::string_view entry(logEntry);
    entry.remove_prefix(entryStart);
    // Use split to separate the entry into its fields
    std::vector<std::string> logEntryFields;
    bmcweb::split(logEntryFields, entry, ',');
    // We need at least a MessageId to be valid
    auto logEntryIter = logEntryFields.begin();
    if (logEntryIter == logEntryFields.end())
    {
        return LogParseError::parseFailed;
    }
    std::string& messageID = *logEntryIter;
    // Get the Message from the MessageRegistry
    const registries::Message* message = registries::getMessage(messageID);

    logEntryIter++;
    if (message == nullptr)
    {
        BMCWEB_LOG_WARNING("Log entry not found in registry: {}", logEntry);
        return LogParseError::messageIdNotInRegistry;
    }

    std::vector<std::string_view> messageArgs(logEntryIter,
                                              logEntryFields.end());
    messageArgs.resize(message->numberOfArgs);

    std::string msg = redfish::registries::fillMessageArgs(messageArgs,
                                                           message->message);
    if (msg.empty())
    {
        return LogParseError::parseFailed;
    }

    // Get the Created time from the timestamp. The log timestamp is in RFC3339
    // format which matches the Redfish format except for the fractional seconds
    // between the '.' and the '+', so just remove them.
    std::size_t dot = timestamp.find_first_of('.');
    std::size_t plus = timestamp.find_first_of('+');
    if (dot != std::string::npos && plus != std::string::npos)
    {
        timestamp.erase(dot, plus - dot);
    }

    // Fill in the log entry with the gathered data
    logEntryJson["@odata.type"] = logEntryVersion;
    logEntryJson["@odata.id"] = getLogEntryDataId(logEntryID);
    logEntryJson["Name"] = "System Event Log Entry";
    logEntryJson["Id"] = logEntryID;
    logEntryJson["Message"] = std::move(msg);
    logEntryJson["MessageId"] = std::move(messageID);
    logEntryJson["MessageArgs"] = messageArgs;
    logEntryJson["EntryType"] = "Event";
    logEntryJson["Severity"] = message->messageSeverity;
    logEntryJson["Created"] = std::move(timestamp);
    return LogParseError::success;
}

inline void requestRoutesJournalEventLogEntryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/EventLog/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& systemName) {
        query_param::QueryCapabilities capabilities = {
            .canDelegateTop = true,
            .canDelegateSkip = true,
        };
        query_param::Query delegatedQuery;
        if (!redfish::setUpRedfishRouteWithDelegation(
                app, req, asyncResp, delegatedQuery, capabilities))
        {
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       systemName);
            return;
        }
        if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
        {
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       systemName);
            return;
        }

        size_t top = delegatedQuery.top.value_or(query_param::Query::maxTop);
        size_t skip = delegatedQuery.skip.value_or(0);

        // Collections don't include the static data added by SubRoute
        // because it has a duplicate entry for members
        asyncResp->res.jsonValue["@odata.type"] =
            "#LogEntryCollection.LogEntryCollection";
        asyncResp->res.jsonValue["@odata.id"] =
            std::format("/redfish/v1/Systems/{}/LogServices/EventLog/Entries",
                        BMCWEB_REDFISH_SYSTEM_URI_NAME);
        asyncResp->res.jsonValue["Name"] = "System Event Log Entries";
        asyncResp->res.jsonValue["Description"] =
            "Collection of System Event Log Entries";

        nlohmann::json& logEntryArray = asyncResp->res.jsonValue["Members"];
        logEntryArray = nlohmann::json::array();
        // Go through the log files and create a unique ID for each
        // entry
        std::vector<std::filesystem::path> redfishLogFiles;
        getRedfishLogFiles(redfishLogFiles);
        uint64_t entryCount = 0;
        std::string logEntry;

        // Oldest logs are in the last file, so start there and loop
        // backwards
        for (auto it = redfishLogFiles.rbegin(); it < redfishLogFiles.rend();
             it++)
        {
            std::ifstream logStream(*it);
            if (!logStream.is_open())
            {
                continue;
            }

            // Reset the unique ID on the first entry
            bool firstEntry = true;
            while (std::getline(logStream, logEntry))
            {
                std::string idStr;
                if (!getUniqueEntryID(logEntry, idStr, firstEntry))
                {
                    continue;
                }
                firstEntry = false;

                nlohmann::json::object_t bmcLogEntry;
                LogParseError status = fillEventLogEntryJson(idStr, logEntry,
                                                             bmcLogEntry);
                if (status == LogParseError::messageIdNotInRegistry)
                {
                    continue;
                }
                if (status != LogParseError::success)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }

                entryCount++;
                // Handle paging using skip (number of entries to skip
                // from the start) and top (number of entries to
                // display)
                if (entryCount <= skip || entryCount > skip + top)
                {
                    continue;
                }

                logEntryArray.emplace_back(std::move(bmcLogEntry));
            }
        }
        asyncResp->res.jsonValue["Members@odata.count"] = entryCount;
        if (skip + top < entryCount)
        {
            asyncResp->res.jsonValue["Members@odata.nextLink"] =
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                "/LogServices/EventLog/Entries?$skip=" +
                std::to_string(skip + top);
        }
    });
}

inline void requestRoutesJournalEventLogEntry(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/LogServices/EventLog/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& param) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       BMCWEB_REDFISH_SYSTEM_URI_NAME);
            return;
        }

        const std::string& targetID = param;

        // Go through the log files and check the unique ID for each
        // entry to find the target entry
        std::vector<std::filesystem::path> redfishLogFiles;
        getRedfishLogFiles(redfishLogFiles);
        std::string logEntry;

        // Oldest logs are in the last file, so start there and loop
        // backwards
        for (auto it = redfishLogFiles.rbegin(); it < redfishLogFiles.rend();
             it++)
        {
            std::ifstream logStream(*it);
            if (!logStream.is_open())
            {
                continue;
            }

            // Reset the unique ID on the first entry
            bool firstEntry = true;
            while (std::getline(logStream, logEntry))
            {
                std::string idStr;
                if (!getUniqueEntryID(logEntry, idStr, firstEntry))
                {
                    continue;
                }
                firstEntry = false;

                if (idStr == targetID)
                {
                    nlohmann::json::object_t bmcLogEntry;
                    LogParseError status =
                        fillEventLogEntryJson(idStr, logEntry, bmcLogEntry);
                    if (status != LogParseError::success)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue.update(bmcLogEntry);
                    return;
                }
            }
        }
        // Requested ID was not found
        messages::resourceMissingAtURI(
            asyncResp->res,
            boost::urls::format(
                "/redfish/v1/Systems/{}/LogServices/EventLog/Entries/{}",
                BMCWEB_REDFISH_SYSTEM_URI_NAME, targetID));
    });
}

inline std::string capitalizeProp(const std::string& key)
{
    std::string ret = std::string(key);
    if (ret.length() && isalpha(ret[0]))
    {
        ret[0] = static_cast<char>(toupper(ret[0]));
    }
    return ret;
}

template <class UnaryFunction>
inline void jsonIterate(nlohmann::json& jOut, const nlohmann::json& jIn,
                        UnaryFunction changeProp, bool debug = 0)
{
    if (!jIn.is_structured())
    {
        jOut = jIn;
        return;
    }

    for (auto it = jIn.begin(); it != jIn.end(); ++it)
    {
        std::string kstr;

        kstr = changeProp(it.key());

        // Filter unwanted properties
        if (kstr == "ErrorInformation")
        {
            jsonIterate(jOut, it.value(), changeProp);
            continue;
        }

        if (it->is_structured())
        {
            if (it.value().is_object())
            {
                jsonIterate(jOut[kstr], it.value(), changeProp);
            }
            else if (it.value().is_array())
            {
                jOut[kstr] = nlohmann::json::array();
                for (auto& arr_i : *it)
                {
                    jOut[kstr].push_back(nlohmann::json());
                    jsonIterate(jOut[kstr].back(), arr_i, changeProp);
                }
            }
            else
            {
                BMCWEB_LOG_WARNING("Unknown JSON structured type");
            }
        }
        else
        {
            jOut[kstr] = it.value();
        }
    }
    if (debug)
    {
        BMCWEB_LOG_DEBUG("jIn:\n", jIn.dump());
        BMCWEB_LOG_DEBUG("jOut:\n", jOut.dump());
        BMCWEB_LOG_DEBUG(
            "---------------------------------------------------------------------\n\n\n\n");
    }
}

inline int severityToStr(const std::string& code, std::string& out)
{
    const std::map<std::string, std::string> code_map = {
        {"0", "Warning"}, {"1", "Critical"}, {"2", "OK"}, {"3", "Warning"}};
    const auto it = code_map.find(code);
    if (it != code_map.end())
    {
        out = it->second;
        return 0;
    }
    return 1;
}

inline void parseAdditionalDataForCPER(
    nlohmann::json::object_t& entry,
    [[maybe_unused]] const nlohmann::json::object_t& oem,
    const AdditionalData& additional)
{
    const auto& type = additional.find("diagnosticDataType");
    if (additional.end() == type ||
        ("CPER" != type->second && "CPERSection" != type->second))
    {
        return;
    }

    BMCWEB_LOG_DEBUG("Got {}", type->second);

    nlohmann::json jOut;

    const auto& notifT = additional.find("notificationType");
    if (additional.end() == notifT)
    {
        BMCWEB_LOG_ERROR("notificationType property not found in CPER log");
        return;
    }
    BMCWEB_LOG_DEBUG("Adding notificationType");
    jOut["CPER"]["NotificationType"] = notifT->second;

    const auto& sevCode = additional.find("cperSeverityCode");
    if (additional.end() == sevCode)
    {
        BMCWEB_LOG_ERROR("severity code property not found in CPER log");
        return;
    }

    std::string code_val;
    if (!severityToStr(sevCode->second, code_val))
    {
        BMCWEB_LOG_DEBUG("Adding severity code");
        jOut["Severity"] = code_val;
    }

    const auto& diagData = additional.find("diagnosticData");
    if (additional.end() == diagData)
    {
        BMCWEB_LOG_ERROR("diagnosticData property not found in CPER log");
        return;
    }

    BMCWEB_LOG_DEBUG("Adding diagnosticData");
    jOut["DiagnosticData"] = diagData->second;

    const auto& secT = additional.find("sectionType");
    if (additional.end() == secT)
    {
        BMCWEB_LOG_WARNING("sectionType property not found in CPER log");
    }
    else
    {
        jOut["CPER"]["SectionType"] = secT->second;
    }

    nlohmann::json cperData;
    const auto& jDiag = additional.find("jsonDiagnosticData");
    if (additional.end() == jDiag)
    {
        BMCWEB_LOG_ERROR("jsonDiagnosticData property not found in CPER log");
    }
    else
    {
        cperData = nlohmann::json::parse(jDiag->second, nullptr, false);
        if (cperData.is_discarded())
        {
            BMCWEB_LOG_ERROR("Could not parse CPER jsonDiagnosticData");
            return;
        }
    }

    if (cperData.find("sections") == cperData.end())
    {
        BMCWEB_LOG_ERROR("Sections property not found in CPER log");
        return;
    }

    const nlohmann::json::array_t* sections =
        cperData["sections"].get_ptr<const nlohmann::json::array_t*>();
    if (sections == nullptr)
    {
        BMCWEB_LOG_ERROR("sections property in CPER is not an array");
        return;
    }

    // Iterate over Sections:
    for (const auto& section : *sections)
    {
        jsonIterate(jOut["CPER"]["Oem"]["Nvidia"], section, capitalizeProp);
        // We only care about the first section
        break;
    }

    // Root
    jOut["DiagnosticDataType"] = "CPERSection";
    jOut["MessageId"] = "Platform.1.0.PlatformError";

    // NVIDIA
    jOut["CPER"]["Oem"]["Nvidia"]["@odata.type"] =
        "#NvidiaCPER.v1_0_0.NvidiaCPER";

    entry = jOut;

    BMCWEB_LOG_DEBUG("Done {}", type->second);
}

inline void requestRoutesDBusEventLogEntryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/EventLog/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       BMCWEB_REDFISH_SYSTEM_URI_NAME);
            return;
        }

        // Collections don't include the static data added by SubRoute
        // because it has a duplicate entry for members
        asyncResp->res.jsonValue["@odata.type"] =
            "#LogEntryCollection.LogEntryCollection";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/LogServices/EventLog/Entries";
        asyncResp->res.jsonValue["Name"] = "System Event Log Entries";
        asyncResp->res.jsonValue["Description"] =
            "Collection of System Event Log Entries";

        // DBus implementation of EventLog/Entries
        // Make call to Logging Service to find all log entry objects
        sdbusplus::message::object_path path("/xyz/openbmc_project/logging");
        dbus::utility::getManagedObjects(
            "xyz.openbmc_project.Logging", path,
            [asyncResp](const boost::system::error_code& ec,
                        const dbus::utility::ManagedObjectType& resp) {
            if (ec)
            {
                // TODO Handle for specific error code
                BMCWEB_LOG_ERROR(
                    "getLogEntriesIfaceData resp_handler got error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            nlohmann::json& entriesArray = asyncResp->res.jsonValue["Members"];
            entriesArray = nlohmann::json::array();
            for (auto& objectPath : resp)
            {
                const uint32_t* id = nullptr;
                std::time_t timestamp{};
                std::time_t updateTimestamp{};
                const std::string* severity = nullptr;
                const std::string* message = nullptr;
                const std::string* filePath = nullptr;
                const std::string* eventId = nullptr;
                bool resolved = false;
                const std::string* notify = nullptr;
                const std::string* resolution = nullptr;
                const std::vector<std::string>* additionalDataRaw = nullptr;
                for (auto& interfaceMap : objectPath.second)
                {
                    if (interfaceMap.first ==
                        "xyz.openbmc_project.Logging.Entry")
                    {
                        for (auto& propertyMap : interfaceMap.second)
                        {
                            if (propertyMap.first == "Id")
                            {
                                id = std::get_if<uint32_t>(&propertyMap.second);
                            }
                            else if (propertyMap.first == "Timestamp")
                            {
                                const uint64_t* millisTimeStamp =
                                    std::get_if<uint64_t>(&propertyMap.second);
                                if (millisTimeStamp != nullptr)
                                {
                                    timestamp =
                                        redfish::time_utils::getTimestamp(
                                            *millisTimeStamp);
                                }
                            }
                            else if (propertyMap.first == "UpdateTimestamp")
                            {
                                const uint64_t* millisTimeStamp =
                                    std::get_if<uint64_t>(&propertyMap.second);
                                if (millisTimeStamp != nullptr)
                                {
                                    updateTimestamp =
                                        redfish::time_utils::getTimestamp(
                                            *millisTimeStamp);
                                }
                            }
                            else if (propertyMap.first == "Severity")
                            {
                                severity = std::get_if<std::string>(
                                    &propertyMap.second);
                            }
                            else if (propertyMap.first == "Message")
                            {
                                message = std::get_if<std::string>(
                                    &propertyMap.second);
                            }
                            else if (propertyMap.first == "Resolved")
                            {
                                const bool* resolveptr =
                                    std::get_if<bool>(&propertyMap.second);
                                if (resolveptr == nullptr)
                                {
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                resolved = *resolveptr;
                            }
                            else if (propertyMap.first == "Resolution")
                            {
                                resolution = std::get_if<std::string>(
                                    &propertyMap.second);
                            }
                            else if (propertyMap.first == "AdditionalData")
                            {
                                additionalDataRaw =
                                    std::get_if<std::vector<std::string>>(
                                        &propertyMap.second);
                            }
                            else if (propertyMap.first ==
                                     "ServiceProviderNotify")
                            {
                                notify = std::get_if<std::string>(
                                    &propertyMap.second);
                                if (notify == nullptr)
                                {
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                            }
                            else if (propertyMap.first == "EventId")
                            {
                                eventId = std::get_if<std::string>(
                                    &propertyMap.second);
                            }
                        }
                        if (id == nullptr || message == nullptr ||
                            severity == nullptr)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                    }
                    else if (interfaceMap.first ==
                             "xyz.openbmc_project.Common.FilePath")
                    {
                        for (auto& propertyMap : interfaceMap.second)
                        {
                            if (propertyMap.first == "Path")
                            {
                                filePath = std::get_if<std::string>(
                                    &propertyMap.second);
                            }
                        }
                    }
                }
                // Object path without the
                // xyz.openbmc_project.Logging.Entry interface, ignore
                // and continue.
                if (id == nullptr || message == nullptr || severity == nullptr)
                {
                    continue;
                }
                nlohmann::json thisEntry = nlohmann::json::object();

                // Determine if it's a message registry format or not.
                bool isMessageRegistry = false;
                std::string messageId;
                std::string messageArgs;
                std::string originOfCondition;
                std::string deviceName;
                nlohmann::json::object_t cper;
                if (additionalDataRaw != nullptr)
                {
                    AdditionalData additional(*additionalDataRaw);
                    if (additional.count("REDFISH_MESSAGE_ID") > 0)
                    {
                        isMessageRegistry = true;
                        messageId = additional["REDFISH_MESSAGE_ID"];
                        BMCWEB_LOG_DEBUG("MessageId: [{}]", messageId);

                        if (additional.count("REDFISH_MESSAGE_ARGS") > 0)
                        {
                            messageArgs = additional["REDFISH_MESSAGE_ARGS"];
                        }
                    }
                    if (additional.count("REDFISH_ORIGIN_OF_CONDITION") > 0)
                    {
                        originOfCondition =
                            additional["REDFISH_ORIGIN_OF_CONDITION"];
                    }
                    if (additional.count("DEVICE_NAME") > 0)
                    {
                        deviceName = additional["DEVICE_NAME"];
                    }

                    // populate CPER section (checks are in the fn)
                    nlohmann::json::object_t oem;
                    parseAdditionalDataForCPER(cper, oem, additional);
                }
                if (isMessageRegistry)
                {
                    message_registries::generateMessageRegistry(
                        thisEntry,
                        "/redfish/v1/Systems/" +
                            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                            "/LogServices/"
                            "EventLog/Entries/",
                        "v1_15_0", std::to_string(*id),
                        "System Event Log Entry",
                        redfish::time_utils::getDateTimeStdtime(timestamp),
                        messageId, messageArgs, *resolution, resolved,
                        (eventId == nullptr) ? "" : *eventId, deviceName,
                        *severity);
#ifndef BMCWEB_DISABLE_HEALTH_ROLLUP
                    origin_utils::convertDbusObjectToOriginOfCondition(
                        originOfCondition, std::to_string(*id), asyncResp,
                        thisEntry, deviceName);
#endif // BMCWEB_DISABLE_HEALTH_ROLLUP
                }

                // generateMessageRegistry will not create the entry if
                // the messageId can't be found in message registries.
                // So check the entry 'Id' anyway to cover that case.
                if (thisEntry["Id"].size() == 0)
                {
                    thisEntry["@odata.type"] = "#LogEntry.v1_15_0.LogEntry";
                    thisEntry["@odata.id"] =
                        getLogEntryDataId(std::to_string(*id));
                    thisEntry["Name"] = "System Event Log Entry";
                    thisEntry["Id"] = std::to_string(*id);
                    thisEntry["Message"] = *message;
                    thisEntry["Resolved"] = resolved;
                    thisEntry["EntryType"] = "Event";
                    thisEntry["Severity"] =
                        translateSeverityDbusToRedfish(*severity);
                    thisEntry["Created"] =
                        redfish::time_utils::getDateTimeStdtime(timestamp);
                    thisEntry["Modified"] =
                        redfish::time_utils::getDateTimeStdtime(
                            updateTimestamp);
                    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                    {
                        if ((eventId != nullptr && !eventId->empty()) ||
                            !deviceName.empty())
                        {
                            nlohmann::json oem = {
                                {"Oem",
                                 {{"Nvidia",
                                   {{"@odata.type",
                                     "#NvidiaLogEntry.v1_1_0.NvidiaLogEntry"}}}}}};
                            if (!deviceName.empty())
                            {
                                oem["Oem"]["Nvidia"]["Device"] = deviceName;
                            }
                            if (eventId != nullptr && !eventId->empty())
                            {
                                oem["Oem"]["Nvidia"]["ErrorId"] =
                                    std::string(*eventId);
                            }
                            thisEntry.update(oem);
                        }
                    }
                    std::optional<bool> notifyAction =
                        getProviderNotifyAction(*notify);
                    if (notifyAction)
                    {
                        thisEntry["ServiceProviderNotified"] = *notifyAction;
                    }
                }
                // add CPER to entry if it is present
                if (!cper.empty())
                {
                    thisEntry.update(cper);
                }
                if (filePath != nullptr)
                {
                    thisEntry["AdditionalDataURI"] =
                        getLogEntryAdditionalDataURI(std::to_string(*id));
                }
                entriesArray.push_back(thisEntry);
            }
            std::sort(
                entriesArray.begin(), entriesArray.end(),
                [](const nlohmann::json& left, const nlohmann::json& right) {
                return (left["Id"] <= right["Id"]);
            });
            asyncResp->res.jsonValue["Members@odata.count"] =
                entriesArray.size();
            asyncResp->res.jsonValue["Members"] = std::move(entriesArray);
        });
    });
}

inline void requestRoutesDBusEventLogEntry(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/LogServices/EventLog/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& param) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       BMCWEB_REDFISH_SYSTEM_URI_NAME);
            return;
        }

        std::string entryID = param;
        dbus::utility::escapePathForDbus(entryID);

        // DBus implementation of EventLog/Entries
        // Make call to Logging Service to find all log entry
        // objects
        sdbusplus::asio::getAllProperties(
            *crow::connections::systemBus, "xyz.openbmc_project.Logging",
            "/xyz/openbmc_project/logging/entry/" + entryID, "",
            [asyncResp, entryID](const boost::system::error_code& ec,
                                 const dbus::utility::DBusPropertiesMap& resp) {
            if (ec.value() == EBADR)
            {
                messages::resourceNotFound(asyncResp->res, "EventLogEntry",
                                           entryID);
                return;
            }
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "EventLogEntry (DBus) resp_handler got error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }

            const uint32_t* id = nullptr;
            const uint64_t* timestamp = nullptr;
            const uint64_t* updateTimestamp = nullptr;
            const std::string* severity = nullptr;
            const std::string* message = nullptr;
            const std::string* filePath = nullptr;
            const std::string* eventId = nullptr;
            bool resolved = false;
            const std::string* resolution = nullptr;
            const std::vector<std::string>* additionalDataRaw = nullptr;
            const std::string* notify = nullptr;

            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), resp, "Id", id, "Timestamp",
                timestamp, "UpdateTimestamp", updateTimestamp, "Severity",
                severity, "Message", message, "Resolved", resolved,
                "Resolution", resolution, "AdditionalData", additionalDataRaw,
                "Path", filePath, "ServiceProviderNotify", notify, "EventId",
                eventId);

            if (!success)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            // Determine if it's a message registry format or not.
            bool isMessageRegistry = false;
            std::string messageId;
            std::string messageArgs;
            std::string originOfCondition;
            std::string deviceName;
            nlohmann::json::object_t cper;
            if (additionalDataRaw != nullptr)
            {
                AdditionalData additional(*additionalDataRaw);
                if (additional.count("REDFISH_MESSAGE_ID") > 0)
                {
                    isMessageRegistry = true;
                    messageId = additional["REDFISH_MESSAGE_ID"];
                    BMCWEB_LOG_DEBUG("MessageId: [{}]", messageId);

                    if (additional.count("REDFISH_MESSAGE_ARGS") > 0)
                    {
                        messageArgs = additional["REDFISH_MESSAGE_ARGS"];
                    }
                }
                if (additional.count("REDFISH_ORIGIN_OF_CONDITION") > 0)
                {
                    originOfCondition =
                        additional["REDFISH_ORIGIN_OF_CONDITION"];
                }
                if (additional.count("DEVICE_NAME") > 0)
                {
                    deviceName = additional["DEVICE_NAME"];
                }

                // populate CPER section (checks are in the fn)
                nlohmann::json::object_t oem;
                parseAdditionalDataForCPER(cper, oem, additional);
            }

            if (isMessageRegistry)
            {
                message_registries::generateMessageRegistry(
                    asyncResp->res.jsonValue,
                    "/redfish/v1/Systems/" +
                        std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                        "/LogServices/"
                        "EventLog/Entries/",
                    "v1_15_0", std::to_string(*id), "System Event Log Entry",
                    redfish::time_utils::getDateTimeStdtime(
                        redfish::time_utils::getTimestamp(*timestamp)),
                    messageId, messageArgs, *resolution, resolved,
                    (eventId == nullptr) ? "" : *eventId, deviceName,
                    *severity);
#ifndef BMCWEB_DISABLE_HEALTH_ROLLUP
                origin_utils::convertDbusObjectToOriginOfCondition(
                    originOfCondition, std::to_string(*id), asyncResp,
                    asyncResp->res.jsonValue, deviceName);
#endif // BMCWEB_DISABLE_HEALTH_ROLLUP
            }

            // generateMessageRegistry will not create the entry if
            // the messageId can't be found in message registries.
            // So check the entry 'Id' anyway to cover that case.
            if (asyncResp->res.jsonValue["Id"].size() == 0)
            {
                if (id == nullptr || message == nullptr ||
                    severity == nullptr || timestamp == nullptr ||
                    updateTimestamp == nullptr || notify == nullptr)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }

                asyncResp->res.jsonValue["@odata.type"] =
                    "#LogEntry.v1_15_0.LogEntry";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/LogServices/"
                    "EventLog/"
                    "Entries/" +
                    std::to_string(*id);
                asyncResp->res.jsonValue["Name"] = "System Event Log Entry";
                asyncResp->res.jsonValue["Id"] = std::to_string(*id);
                asyncResp->res.jsonValue["Message"] = *message;
                asyncResp->res.jsonValue["Resolved"] = resolved;
                std::optional<bool> notifyAction =
                    getProviderNotifyAction(*notify);
                if (notifyAction)
                {
                    asyncResp->res.jsonValue["ServiceProviderNotified"] =
                        *notifyAction;
                }
                if ((resolution != nullptr) && (!(*resolution).empty()))
                {
                    asyncResp->res.jsonValue["Resolution"] = *resolution;
                }
                asyncResp->res.jsonValue["EntryType"] = "Event";
                asyncResp->res.jsonValue["Severity"] =
                    translateSeverityDbusToRedfish(*severity);
                asyncResp->res.jsonValue["Created"] =
                    redfish::time_utils::getDateTimeUintMs(*timestamp);
                asyncResp->res.jsonValue["Modified"] =
                    redfish::time_utils::getDateTimeUintMs(*updateTimestamp);
                if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                {
                    if ((eventId != nullptr && !eventId->empty()) ||
                        !deviceName.empty())
                    {
                        nlohmann::json oem = {
                            {"Oem",
                             {{"Nvidia",
                               {{"@odata.type",
                                 "#NvidiaLogEntry.v1_1_0.NvidiaLogEntry"}}}}}};
                        if (!deviceName.empty())
                        {
                            oem["Oem"]["Nvidia"]["Device"] = deviceName;
                        }
                        if (eventId != nullptr && !eventId->empty())
                        {
                            oem["Oem"]["Nvidia"]["ErrorId"] =
                                std::string(*eventId);
                        }
                        asyncResp->res.jsonValue.update(oem);
                    }
                }
                if (filePath != nullptr)
                {
                    getLogEntryAdditionalDataURI(std::to_string(*id));
                }
            }
            // add CPER to entry if it is present
            if (!cper.empty())
            {
                asyncResp->res.jsonValue.update(cper);
            }
        });
    });

    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/LogServices/EventLog/Entries/<str>/")
        .privileges(redfish::privileges::patchLogEntry)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& entryId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       BMCWEB_REDFISH_SYSTEM_URI_NAME);
            return;
        }
        std::optional<bool> resolved;

        if (!json_util::readJsonPatch(req, asyncResp->res, "Resolved",
                                      resolved))
        {
            return;
        }
        BMCWEB_LOG_DEBUG("Set Resolved");

        setDbusProperty(asyncResp, "Resolved", "xyz.openbmc_project.Logging",
                        "/xyz/openbmc_project/logging/entry/" + entryId,
                        "xyz.openbmc_project.Logging.Entry", "Resolved",
                        *resolved);
    });

    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/LogServices/EventLog/Entries/<str>/")
        .privileges(redfish::privileges::deleteLogEntry)
        .methods(boost::beast::http::verb::delete_)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& param) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       BMCWEB_REDFISH_SYSTEM_URI_NAME);
            return;
        }
        BMCWEB_LOG_DEBUG("Do delete single event entries.");

        std::string entryID = param;

        dbus::utility::escapePathForDbus(entryID);

        // Process response from Logging service.
        auto respHandler = [asyncResp,
                            entryID](const boost::system::error_code& ec) {
            BMCWEB_LOG_DEBUG("EventLogEntry (DBus) doDelete callback: Done");
            if (ec)
            {
                if (ec.value() == EBADR)
                {
                    messages::resourceNotFound(asyncResp->res, "LogEntry",
                                               entryID);
                    return;
                }
                // TODO Handle for specific error code
                BMCWEB_LOG_ERROR(
                    "EventLogEntry (DBus) doDelete respHandler got error {}",
                    ec);
                asyncResp->res.result(
                    boost::beast::http::status::internal_server_error);
                return;
            }

            asyncResp->res.result(boost::beast::http::status::ok);
        };

        // Make call to Logging service to request Delete Log
        crow::connections::systemBus->async_method_call(
            respHandler, "xyz.openbmc_project.Logging",
            "/xyz/openbmc_project/logging/entry/" + entryID,
            "xyz.openbmc_project.Object.Delete", "Delete");
    });
}

inline void populateRedfishSELEntry(GetManagedPropertyType& resp,
                                    nlohmann::json& thisEntry)
{
    uint32_t* id = nullptr;
    std::time_t timestamp{};
    std::time_t updateTimestamp{};
    std::string* severity = nullptr;
    std::string* eventId = nullptr;
    std::string* message = nullptr;
    std::vector<std::string>* additionalDataVectorString = nullptr;
    std::string generatorId;
    std::string messageId;
    bool resolved = false;
    bool isMessageRegistry = false;
    std::string sensorData;
    std::string deviceName;
    std::ostringstream hexCodeEventDir;
    std::string messageArgs;
    std::string originOfCondition;
    std::string sensorNumber;
    log_entry::SensorType sensorType = log_entry::SensorType::Invalid;
    log_entry::LogEntryCode entryCode = log_entry::LogEntryCode::Invalid;
    const std::string* resolution = nullptr;

    for (auto& propertyMap : resp)
    {
        if (propertyMap.first == "Id")
        {
            id = std::get_if<uint32_t>(&propertyMap.second);
        }
        else if (propertyMap.first == "Timestamp")
        {
            const uint64_t* millisTimeStamp =
                std::get_if<uint64_t>(&propertyMap.second);
            if (millisTimeStamp != nullptr)
            {
                timestamp = redfish::time_utils::getTimestamp(*millisTimeStamp);
            }
        }
        else if (propertyMap.first == "UpdateTimestamp")
        {
            const uint64_t* millisTimeStamp =
                std::get_if<uint64_t>(&propertyMap.second);
            if (millisTimeStamp != nullptr)
            {
                updateTimestamp =
                    redfish::time_utils::getTimestamp(*millisTimeStamp);
            }
        }
        else if (propertyMap.first == "Severity")
        {
            severity = std::get_if<std::string>(&propertyMap.second);
        }
        else if (propertyMap.first == "EventId")
        {
            eventId = std::get_if<std::string>(&propertyMap.second);
        }
        else if (propertyMap.first == "Message")
        {
            message = std::get_if<std::string>(&propertyMap.second);
        }
        else if (propertyMap.first == "Resolved")
        {
            bool* resolveptr = std::get_if<bool>(&propertyMap.second);
            if (resolveptr == nullptr)
            {
                throw std::runtime_error("Invalid SEL Entry");
                return;
            }
            resolved = *resolveptr;
        }
        else if (propertyMap.first == "Resolution")
        {
            resolution = std::get_if<std::string>(&propertyMap.second);
        }
        else if (propertyMap.first == "AdditionalData")
        {
            std::string eventDir;
            std::string recordType;
            additionalDataVectorString =
                std::get_if<std::vector<std::string>>(&propertyMap.second);
            if (additionalDataVectorString != nullptr)
            {
                AdditionalData additional(*additionalDataVectorString);
                if (additional.count("REDFISH_MESSAGE_ID") > 0)
                {
                    isMessageRegistry = true;
                    messageId = additional["REDFISH_MESSAGE_ID"];
                    BMCWEB_LOG_DEBUG("RedFish MessageId: [{}]", messageId);

                    if (additional.count("REDFISH_MESSAGE_ARGS") > 0)
                    {
                        messageArgs = additional["REDFISH_MESSAGE_ARGS"];
                    }
                }
                else
                {
                    if (additional.count("EVENT_DIR") > 0)
                    {
                        hexCodeEventDir << "0x" << std::setfill('0')
                                        << std::setw(2) << std::hex
                                        << std::stoi(additional["EVENT_DIR"]);
                    }
                    if (additional.count("GENERATOR_ID") > 0)
                    {
                        std::ostringstream hexCodeGeneratorId;
                        if (!additional["GENERATOR_ID"].empty())
                        {
                            hexCodeGeneratorId
                                << "0x" << std::setfill('0') << std::setw(4)
                                << std::hex
                                << std::stoi(additional["GENERATOR_ID"]);
                            generatorId = hexCodeGeneratorId.str();
                        }
                    }
                    if (additional.count("RECORD_TYPE") > 0)
                    {
                        recordType = additional["RECORD_TYPE"];
                    }
                    if (additional.count("SENSOR_DATA") > 0)
                    {
                        sensorData = additional["SENSOR_DATA"];
                        boost::algorithm::to_lower(sensorData);
                    }
                    // MessageId for SEL is of the form 0xNNaabbcc
                    // where 'NN' is the EventDir/EventType byte, aa is first
                    // byte sensor data, bb is second byte sensor data, cc is
                    // third byte sensor data
                    messageId = hexCodeEventDir.str() + sensorData;
                    BMCWEB_LOG_DEBUG("SEL MessageId: [{}]", messageId);
                }
                if (additional.end() != additional.find("EVENT_DIR"))
                {
                    uint8_t eventDir = 0;
                    std::string_view eventDirView = additional["EVENT_DIR"];
                    std::from_chars_result result = std::from_chars(
                        eventDirView.begin(), eventDirView.end(), eventDir);
                    if (result.ec != std::errc{} ||
                        result.ptr != eventDirView.end())
                    {
                        BMCWEB_LOG_DEBUG("SEL EVENT_DIR value not found");
                    }
                    else
                    {
                        if (eventDir)
                        {
                            entryCode = log_entry::LogEntryCode::Assert;
                        }
                        else
                        {
                            entryCode = log_entry::LogEntryCode::Deassert;
                        }
                    }
                }
                if (additional.end() != additional.find("SENSOR_TYPE"))
                {
                    nlohmann::json sensorTypeJson = additional["SENSOR_TYPE"];
                    sensorType = sensorTypeJson.get<log_entry::SensorType>();
                }
                if (additional.end() != additional.find("SENSOR_NUMBER"))
                {
                    sensorNumber = additional["SENSOR_NUMBER"];
                }
                if (additional.count("REDFISH_ORIGIN_OF_CONDITION") > 0)
                {
                    originOfCondition =
                        additional["REDFISH_ORIGIN_OF_CONDITION"];
                }
                if (additional.count("DEVICE_NAME") > 0)
                {
                    deviceName = additional["DEVICE_NAME"];
                }
            }
        }
    }
    if (id == nullptr || message == nullptr || severity == nullptr)
    {
        throw std::runtime_error("Invalid SEL Entry");
        return;
    }
    if (!isSelEntry(message, additionalDataVectorString))
    {
        return;
    }
    if (isMessageRegistry)
    {
        message_registries::generateMessageRegistry(
            thisEntry,
            "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                "/LogServices/"
                "SEL/Entries/",
            "v1_15_0", std::to_string(*id), "System Event Log Entry",
            redfish::time_utils::getDateTimeStdtime(timestamp), messageId,
            messageArgs, *resolution, resolved,
            (eventId == nullptr) ? "" : *eventId, deviceName, *severity);
        thisEntry["EntryType"] = "SEL";
    }

    // generateMessageRegistry will not create the entry if
    // the messageId can't be found in message registries.
    // So check the entry 'Id' anyway to cover that case.
    if (thisEntry["Id"].size() == 0)
    {
        thisEntry["@odata.type"] = logEntryVersion;
        thisEntry["@odata.id"] = "/redfish/v1/Systems/" +
                                 std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                 "/LogServices/SEL/"
                                 "Entries/" +
                                 std::to_string(*id);
        thisEntry["Name"] = "System Event Log Entry";
        thisEntry["Id"] = std::to_string(*id);
        if (!generatorId.empty())
        {
            thisEntry["GeneratorId"] = generatorId;
        }
        if (!messageId.empty())
        {
            thisEntry["MessageId"] = messageId;
        }
        thisEntry["Message"] = *message;
        thisEntry["Resolved"] = resolved;
        thisEntry["EntryType"] = "SEL";
        thisEntry["Severity"] = translateSeverityDbusToRedfish(*severity);
        thisEntry["Created"] =
            redfish::time_utils::getDateTimeStdtime(timestamp);
        thisEntry["Modified"] =
            redfish::time_utils::getDateTimeStdtime(updateTimestamp);
        if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
        {
            if ((eventId != nullptr && !eventId->empty()) ||
                !deviceName.empty())
            {
                nlohmann::json oem = {
                    {"Oem",
                     {{"Nvidia",
                       {{"@odata.type",
                         "#NvidiaLogEntry.v1_1_0.NvidiaLogEntry"}}}}}};
                if (!deviceName.empty())
                {
                    oem["Oem"]["Nvidia"]["Device"] = deviceName;
                }
                if (eventId != nullptr && !eventId->empty())
                {
                    oem["Oem"]["Nvidia"]["ErrorId"] = std::string(*eventId);
                }
                thisEntry.update(oem);
            }
        }
    }
    if (log_entry::SensorType::Invalid != sensorType)
    {
        thisEntry["SensorType"] = sensorType;
    }
    if (!sensorNumber.empty())
    {
        uint8_t number = 0;
        std::string_view sensorNumberView = sensorNumber;
        std::from_chars_result result = std::from_chars(
            sensorNumberView.begin(), sensorNumberView.end(), number);
        if (result.ec != std::errc{} || result.ptr != sensorNumberView.end())
        {
            BMCWEB_LOG_DEBUG("SEL sensor number value not found");
        }
        else
        {
            thisEntry["SensorNumber"] = number;
        }
    }
    if (log_entry::LogEntryCode::Invalid != entryCode)
    {
        thisEntry["EntryCode"] = entryCode;
    }
}

inline void requestRoutesDBusSELLogEntryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/SEL/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        // Collections don't include the static data added by
        // SubRoute because it has a duplicate entry for members
        asyncResp->res.jsonValue["@odata.type"] =
            "#LogEntryCollection.LogEntryCollection";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/LogServices/SEL/Entries";
        asyncResp->res.jsonValue["Name"] = "System Event Log Entries";
        asyncResp->res.jsonValue["Description"] =
            "Collection of System Event Log Entries";

        // DBus implementation of SEL/Entries
        // Make call to Logging Service to find all log entry
        // objects
        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code ec,
                        GetManagedObjectsType& resp) {
            if (ec)
            {
                // TODO Handle for specific error code
                BMCWEB_LOG_ERROR(
                    "getLogEntriesIfaceData resp_handler got error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
            nlohmann::json& entriesArray = asyncResp->res.jsonValue["Members"];
            entriesArray = nlohmann::json::array();
            for (auto& objectPath : resp)
            {
                nlohmann::json thisEntry = nlohmann::json::object();
                for (auto& interfaceMap : objectPath.second)
                {
                    if (interfaceMap.first ==
                        "xyz.openbmc_project.Logging.Entry")
                    {
                        try
                        {
                            populateRedfishSELEntry(interfaceMap.second,
                                                    thisEntry);
                            if (!thisEntry.empty())
                            {
                                entriesArray.push_back(thisEntry);
                            }
                        }
                        catch ([[maybe_unused]] const std::runtime_error& e)
                        {
                            messages::internalError(asyncResp->res);
                            continue;
                        }
                    }
                }
            }
            std::sort(
                entriesArray.begin(), entriesArray.end(),
                [](const nlohmann::json& left, const nlohmann::json& right) {
                return (left["Id"] <= right["Id"]);
            });
            asyncResp->res.jsonValue["Members@odata.count"] =
                entriesArray.size();
        },
            "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
            "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    });
}

inline void requestRoutesDBusSELLogEntry(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/LogServices/SEL/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& param)

    {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            return;
        }
        std::string entryID = param;
        dbus::utility::escapePathForDbus(entryID);

        // DBus implementation of EventLog/Entries
        // Make call to Logging Service to find all log entry
        // objects
        crow::connections::systemBus->async_method_call(
            [asyncResp, entryID](const boost::system::error_code ec,
                                 GetManagedPropertyType& resp) {
            if (ec.value() == EBADR)
            {
                messages::resourceNotFound(asyncResp->res, "SELLogEntry",
                                           entryID);
                return;
            }
            if (ec)
            {
                BMCWEB_LOG_ERROR("SELLogEntry (DBus) resp_handler got error {}",
                                 ec);
                messages::internalError(asyncResp->res);
                return;
            }
            nlohmann::json& thisEntry = asyncResp->res.jsonValue;
            thisEntry = nlohmann::json::object();
            try
            {
                populateRedfishSELEntry(resp, thisEntry);
            }
            catch ([[maybe_unused]] const std::runtime_error& e)
            {
                messages::internalError(asyncResp->res);
                return;
            }
        },
            "xyz.openbmc_project.Logging",
            "/xyz/openbmc_project/logging/entry/" + entryID,
            "org.freedesktop.DBus.Properties", "GetAll", "");
    });

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/LogServices/SEL/Entries/<str>/")
        .privileges(redfish::privileges::deleteLogEntry)
        .methods(boost::beast::http::verb::delete_)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& param) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::string entryID = param;

        dbus::utility::escapePathForDbus(entryID);
        deleteDbusSELEntry(entryID, asyncResp);
    });
}

inline void requestRoutesDBusSELLogServiceActionsClear(App& app)
{
    /**
     * Function handles POST method request.
     * The Clear Log actions does not require any parameter.The action deletes
     * all entries found in the Entries collection for this Log Service.
     */

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/SEL/Actions/"
                      "LogService.ClearLog/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code ec,
                        GetManagedObjectsType& resp) {
            if (ec)
            {
                // TODO Handle for specific error code
                BMCWEB_LOG_ERROR(
                    "getLogEntriesIfaceData resp_handler got error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }

            for (auto& objectPath : resp)
            {
                uint32_t* id = nullptr;
                std::string* message = nullptr;
                const std::vector<std::string>* additionalData = nullptr;

                for (auto& interfaceMap : objectPath.second)
                {
                    if (interfaceMap.first ==
                        "xyz.openbmc_project.Logging.Entry")
                    {
                        for (auto& propertyMap : interfaceMap.second)
                        {
                            if (propertyMap.first == "Id")
                            {
                                id = std::get_if<uint32_t>(&propertyMap.second);
                            }
                            else if (propertyMap.first == "Message")
                            {
                                message = std::get_if<std::string>(
                                    &propertyMap.second);
                            }
                            else if (propertyMap.first == "AdditionalData")
                            {
                                additionalData =
                                    std::get_if<std::vector<std::string>>(
                                        &propertyMap.second);
                            }
                        }
                        if (id == nullptr || message == nullptr)
                        {
                            messages::internalError(asyncResp->res);
                            continue;
                        }
                        if (isSelEntry(message, additionalData))
                        {
                            std::string entryId = std::to_string(*id);
                            deleteDbusLogEntry(entryId, asyncResp);
                        }
                    }
                }
            }
        },
            "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
            "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    });
}

constexpr const char* hostLoggerFolderPath = "/var/log/console";

inline bool
    getHostLoggerFiles(const std::string& hostLoggerFilePath,
                       std::vector<std::filesystem::path>& hostLoggerFiles)
{
    std::error_code ec;
    std::filesystem::directory_iterator logPath(hostLoggerFilePath, ec);
    if (ec)
    {
        BMCWEB_LOG_ERROR("{}", ec.message());
        return false;
    }
    for (const std::filesystem::directory_entry& it : logPath)
    {
        std::string filename = it.path().filename();
        // Prefix of each log files is "log". Find the file and save the
        // path
        if (boost::starts_with(filename, "log"))
        {
            hostLoggerFiles.emplace_back(it.path());
        }
    }
    // As the log files rotate, they are appended with a ".#" that is higher for
    // the older logs. Since we start from oldest logs, sort the name in
    // descending order.
    std::sort(hostLoggerFiles.rbegin(), hostLoggerFiles.rend(),
              AlphanumLess<std::string>());

    return true;
}

inline bool getHostLoggerEntries(
    const std::vector<std::filesystem::path>& hostLoggerFiles, uint64_t skip,
    uint64_t top, std::vector<std::string>& logEntries, size_t& logCount)
{
    GzFileReader logFile;

    // Go though all log files and expose host logs.
    for (const std::filesystem::path& it : hostLoggerFiles)
    {
        if (!logFile.gzGetLines(it.string(), skip, top, logEntries, logCount))
        {
            BMCWEB_LOG_ERROR("fail to expose host logs");
            return false;
        }
    }
    // Get lastMessage from constructor by getter
    std::string lastMessage = logFile.getLastMessage();
    if (!lastMessage.empty())
    {
        logCount++;
        if (logCount > skip && logCount <= (skip + top))
        {
            logEntries.push_back(lastMessage);
        }
    }
    return true;
}

inline void fillHostLoggerEntryJson(const std::string& logEntryID,
                                    const std::string& msg,
                                    nlohmann::json::object_t& logEntryJson)
{
    // Fill in the log entry with the gathered data.
    logEntryJson["@odata.type"] = logEntryVersion;
    logEntryJson["@odata.id"] = "/redfish/v1/Systems/" +
                                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                "/LogServices/HostLogger/Entries/" + logEntryID;
    logEntryJson["Name"] = "Host Logger Entry";
    logEntryJson["Id"] = logEntryID;
    logEntryJson["Message"] = msg;
    logEntryJson["EntryType"] = "Oem";
    logEntryJson["Severity"] = "OK";
    logEntryJson["OemRecordFormat"] = "Host Logger Entry";
}

inline void requestRoutesSystemHostLogger(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/HostLogger/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/LogServices/HostLogger";
        asyncResp->res.jsonValue["@odata.type"] =
            "#LogService.v1_1_0.LogService";
        asyncResp->res.jsonValue["Name"] = "Host Logger Service";
        asyncResp->res.jsonValue["Description"] = "Host Logger Service";
        asyncResp->res.jsonValue["Id"] = "HostLogger";
        asyncResp->res.jsonValue["Entries"] = {
            {"@odata.id", "/redfish/v1/Systems/" +
                              std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                              "/LogServices/HostLogger/Entries"}};
    });
}

inline void requestRoutesSystemHostLoggerCollection(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/LogServices/HostLogger/Entries/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        query_param::QueryCapabilities capabilities = {
            .canDelegateTop = true,
            .canDelegateSkip = true,
        };
        query_param::Query delegatedQuery;
        if (!redfish::setUpRedfishRouteWithDelegation(
                app, req, asyncResp, delegatedQuery, capabilities))
        {
            return;
        }
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/LogServices/HostLogger/Entries";
        asyncResp->res.jsonValue["@odata.type"] =
            "#LogEntryCollection.LogEntryCollection";
        asyncResp->res.jsonValue["Name"] = "HostLogger Entries";
        asyncResp->res.jsonValue["Description"] =
            "Collection of HostLogger Entries";
        nlohmann::json& logEntryArray = asyncResp->res.jsonValue["Members"];
        logEntryArray = nlohmann::json::array();
        asyncResp->res.jsonValue["Members@odata.count"] = 0;

        std::vector<std::filesystem::path> hostLoggerFiles;
        if (!getHostLoggerFiles(hostLoggerFolderPath, hostLoggerFiles))
        {
            BMCWEB_LOG_ERROR("fail to get host log file path");
            return;
        }

        // If we weren't provided top and skip limits, use the
        // defaults.
        size_t skip = delegatedQuery.skip.value_or(0);
        size_t top = delegatedQuery.top.value_or(query_param::Query::maxTop);
        size_t logCount = 0;
        // This vector only store the entries we want to expose that
        // control by skip and top.
        std::vector<std::string> logEntries;
        if (!getHostLoggerEntries(hostLoggerFiles, skip, top, logEntries,
                                  logCount))
        {
            messages::internalError(asyncResp->res);
            return;
        }
        // If vector is empty, that means skip value larger than
        // total log count
        if (logEntries.empty())
        {
            asyncResp->res.jsonValue["Members@odata.count"] = logCount;
            return;
        }
        if (!logEntries.empty())
        {
            for (size_t i = 0; i < logEntries.size(); i++)
            {
                nlohmann::json::object_t hostLogEntry;
                fillHostLoggerEntryJson(std::to_string(skip + i), logEntries[i],
                                        hostLogEntry);
                logEntryArray.emplace_back(std::move(hostLogEntry));
            }

            asyncResp->res.jsonValue["Members@odata.count"] = logCount;
            if (skip + top < logCount)
            {
                asyncResp->res.jsonValue["Members@odata.nextLink"] =
                    "/redfish/v1/Systems/" +
                    std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                    "/LogServices/HostLogger/Entries?$skip=" +
                    std::to_string(skip + top);
            }
        }
    });
}

inline void requestRoutesSystemHostLoggerLogEntry(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/LogServices/HostLogger/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& param) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       BMCWEB_REDFISH_SYSTEM_URI_NAME);
            return;
        }
        const std::string& targetID = param;

        uint64_t idInt = 0;

        auto [ptr, ec] = std::from_chars(&*targetID.begin(), &*targetID.end(),
                                         idInt);
        if (ec == std::errc::invalid_argument ||
            ec == std::errc::result_out_of_range)
        {
            messages::resourceMissingAtURI(
                asyncResp->res,
                boost::urls::format(
                    "/redfish/v1/Systems/{}/LogServices/HostLogger/Entries/{}",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME, targetID));
        }

        std::vector<std::filesystem::path> hostLoggerFiles;
        if (!getHostLoggerFiles(hostLoggerFolderPath, hostLoggerFiles))
        {
            BMCWEB_LOG_DEBUG("Failed to get host log file path");
            return;
        }

        size_t logCount = 0;
        size_t top = 1;
        std::vector<std::string> logEntries;
        // We can get specific entry by skip and top. For example,
        // if we want to get nth entry, we can set skip = n-1 and
        // top = 1 to get that entry
        if (!getHostLoggerEntries(hostLoggerFiles, idInt, top, logEntries,
                                  logCount))
        {
            messages::internalError(asyncResp->res);
            return;
        }

        if (!logEntries.empty())
        {
            nlohmann::json::object_t hostLogEntry;
            fillHostLoggerEntryJson(targetID, logEntries[0], hostLogEntry);
            asyncResp->res.jsonValue.update(hostLogEntry);
            return;
        }

        // Requested ID was not found
        messages::resourceMissingAtURI(
            asyncResp->res,
            boost::urls::format(
                "/redfish/v1/Systems/{}/LogServices/HostLogger/Entries/{}",
                BMCWEB_REDFISH_SYSTEM_URI_NAME, targetID));
    });
}

inline void handleBMCLogServicesCollectionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& managerName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    // Collections don't include the static data added by SubRoute
    // because it has a duplicate entry for members
    asyncResp->res.jsonValue["@odata.type"] =
        "#LogServiceCollection.LogServiceCollection";
    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Managers/" + std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
        "/LogServices";
    asyncResp->res.jsonValue["Name"] = "Open BMC Log Services Collection";
    asyncResp->res.jsonValue["Description"] =
        "Collection of LogServices for this Manager";
    nlohmann::json& logServiceArray = asyncResp->res.jsonValue["Members"];
    logServiceArray = nlohmann::json::array();

#ifdef BMCWEB_ENABLE_REDFISH_BMC_JOURNAL
    logServiceArray.push_back(
        {{"@odata.id", "/redfish/v1/Managers/" +
                           std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                           "/LogServices/Journal"}});
#endif

    asyncResp->res.jsonValue["Members@odata.count"] = logServiceArray.size();

    if constexpr (BMCWEB_REDFISH_DUMP_LOG)
    {
        constexpr std::array<std::string_view, 1> interfaces = {
            "xyz.openbmc_project.Collection.DeleteAll"};
        dbus::utility::getSubTreePaths(
            "/xyz/openbmc_project/dump", 0, interfaces,
            [asyncResp](const boost::system::error_code& ec,
                        const dbus::utility::MapperGetSubTreePathsResponse&
                            subTreePaths) {
            if (ec)
            {
                BMCWEB_LOG_ERROR(
                    "handleBMCLogServicesCollectionGet respHandler got error {}",
                    ec);
                // Assume that getting an error simply means there are no dump
                // LogServices. Return without adding any error response.
                return;
            }

            nlohmann::json& logServiceArrayLocal =
                asyncResp->res.jsonValue["Members"];

            for (const std::string& path : subTreePaths)
            {
                if (path == "/xyz/openbmc_project/dump/bmc")
                {
                    logServiceArrayLocal.push_back(
                        {{"@odata.id",
                          "/redfish/v1/Managers/" +
                              std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                              "/LogServices/Dump"}});
                }
                else if (path == "/xyz/openbmc_project/dump/faultlog")
                {
#ifndef BMCWEB_ENABLE_REDFISH_SYSTEM_FAULTLOG_DUMP_LOG
                    nlohmann::json::object_t member;
                    member["@odata.id"] =
                        "/redfish/v1/Managers/" +
                        std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                        "/LogServices/FaultLog";
                    logServiceArrayLocal.emplace_back(std::move(member));
#endif
                }
            }

            asyncResp->res.jsonValue["Members@odata.count"] =
                logServiceArrayLocal.size();
        });
    }
}

inline void requestRoutesBMCLogServiceCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/LogServices/")
        .privileges(redfish::privileges::getLogServiceCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleBMCLogServicesCollectionGet, std::ref(app)));
}

inline void
    getDumpServiceInfo(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& dumpType)
{
    std::string dumpPath;
    std::string overWritePolicy;
    bool collectDiagnosticDataSupported = false;

    if (dumpType == "BMC")
    {
        dumpPath = "/redfish/v1/Managers/" +
                   std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                   "/LogServices/Dump";
        overWritePolicy = "WrapsWhenFull";
        collectDiagnosticDataSupported = true;
    }
    else if (dumpType == "FaultLog")
    {
        dumpPath = "/redfish/v1/Managers/" +
                   std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                   "/LogServices/FaultLog";
        overWritePolicy = "Unknown";
        collectDiagnosticDataSupported = false;
    }
    else if (dumpType == "System")
    {
        dumpPath = "/redfish/v1/Systems/" +
                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                   "/LogServices/Dump";
        overWritePolicy = "WrapsWhenFull";
        collectDiagnosticDataSupported = true;
    }
    else
    {
        BMCWEB_LOG_ERROR("getDumpServiceInfo() invalid dump type: {}",
                         dumpType);
        messages::internalError(asyncResp->res);
        return;
    }

    asyncResp->res.jsonValue["@odata.id"] = dumpPath;
    asyncResp->res.jsonValue["@odata.type"] = "#LogService.v1_2_0.LogService";
    asyncResp->res.jsonValue["Name"] = "Dump LogService";
    asyncResp->res.jsonValue["Description"] = dumpType + " Dump LogService";
    asyncResp->res.jsonValue["Id"] = std::filesystem::path(dumpPath).filename();
    asyncResp->res.jsonValue["OverWritePolicy"] = std::move(overWritePolicy);

    std::pair<std::string, std::string> redfishDateTimeOffset =
        redfish::time_utils::getDateTimeOffsetNow();
    asyncResp->res.jsonValue["DateTime"] = redfishDateTimeOffset.first;
    asyncResp->res.jsonValue["DateTimeLocalOffset"] =
        redfishDateTimeOffset.second;

    asyncResp->res.jsonValue["Entries"]["@odata.id"] = dumpPath + "/Entries";

#ifdef BMCWEB_ENABLE_NVIDIA_RETIMER_DEBUGMODE
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, "xyz.openbmc_project.Dump.Manager",
        "/xyz/openbmc_project/dump/retimer",
        "xyz.openbmc_project.Dump.DebugMode", "DebugMode",
        [asyncResp](const boost::system::error_code ec,
                    const bool DebugModeEnabled) {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "DBUS response error for RetimerDebugModeEnabled {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
            "#NvidiaLogService.v1_2_0.NvidiaLogService";
        asyncResp->res.jsonValue["Oem"]["Nvidia"]["RetimerDebugModeEnabled"] =
            DebugModeEnabled;
    });
#endif // BMCWEB_ENABLE_NVIDIA_RETIMER_DEBUGMODE
    if (collectDiagnosticDataSupported)
    {
        asyncResp->res.jsonValue["Actions"]["#LogService.CollectDiagnosticData"]
                                ["target"] =
            dumpPath + "/Actions/LogService.CollectDiagnosticData";
        asyncResp->res.jsonValue["Actions"]["#LogService.CollectDiagnosticData"]
                                ["@Redfish.ActionInfo"] =
            dumpPath + "/CollectDiagnosticDataActionInfo";
    }

    constexpr std::array<std::string_view, 1> interfaces = {deleteAllInterface};
    dbus::utility::getSubTreePaths(
        "/xyz/openbmc_project/dump", 0, interfaces,
        [asyncResp, dumpType, dumpPath](
            const boost::system::error_code& ec,
            const dbus::utility::MapperGetSubTreePathsResponse& subTreePaths) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("getDumpServiceInfo respHandler got error {}", ec);
            // Assume that getting an error simply means there are
            // no dump LogServices. Return without adding any error
            // response.
            return;
        }
        std::string dbusDumpPath = getDumpPath(dumpType);
        for (const std::string& path : subTreePaths)
        {
            if (path == dbusDumpPath)
            {
                asyncResp->res
                    .jsonValue["Actions"]["#LogService.ClearLog"]["target"] =
                    dumpPath + "/Actions/LogService.ClearLog";
                break;
            }
        }
    });
}

inline void handleLogServicesDumpServiceGet(
    crow::App& app, const std::string& dumpType, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }

    getDumpServiceInfo(asyncResp, dumpType);
}

inline void handleLogServicesDumpServiceComputerSystemGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (chassisId != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem", chassisId);
        return;
    }
    getDumpServiceInfo(asyncResp, "System");
}

inline void handleLogServicesDumpServiceComputerSystemPatch(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::optional<nlohmann::json> oemObject;
    if (!json_util::readJsonPatch(req, asyncResp->res, "Oem", oemObject))
    {
        return;
    }

    std::optional<nlohmann::json> oemNvidiaObject;
    if (!json_util::readJson(*oemObject, asyncResp->res, "Nvidia",
                             oemNvidiaObject))
    {
        return;
    }

    std::optional<bool> retimerDebugModeEnabled;
    if (!json_util::readJson(*oemNvidiaObject, asyncResp->res,
                             "RetimerDebugModeEnabled",
                             retimerDebugModeEnabled))
    {
        return;
    }

    sdbusplus::asio::setProperty(
        *crow::connections::systemBus, "xyz.openbmc_project.Dump.Manager",
        "/xyz/openbmc_project/dump/retimer",
        "xyz.openbmc_project.Dump.DebugMode", "DebugMode",
        *retimerDebugModeEnabled,
        [asyncResp](const boost::system::error_code ec) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error DebugMode setProperty {}",
                             ec);
            messages::internalError(asyncResp->res);
            return;
        }
    });
    messages::success(asyncResp->res);
    return;
}

inline void handleLogServicesDumpEntriesCollectionGet(
    crow::App& app, const std::string& dumpType, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }
    getDumpEntryCollection(asyncResp, dumpType);
}

inline void handleLogServicesDumpEntriesCollectionComputerSystemGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (chassisId != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem", chassisId);
        return;
    }
    getDumpEntryCollection(asyncResp, "System");
}

inline void handleLogServicesDumpEntryGet(
    crow::App& app, const std::string& dumpType, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& dumpId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }
    getDumpEntryById(asyncResp, dumpId, dumpType);
}

inline void handleLogServicesDumpEntryComputerSystemGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& dumpId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (chassisId != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem", chassisId);
        return;
    }
    getDumpEntryById(asyncResp, dumpId, "System");
}

inline void handleLogServicesDumpEntryDelete(
    crow::App& app, const std::string& dumpType, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId, const std::string& dumpId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }
    deleteDumpEntry(asyncResp, dumpId, dumpType);
}

inline void handleLogServicesDumpEntryComputerSystemDelete(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& chassisId, const std::string& dumpId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (chassisId != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem", chassisId);
        return;
    }
    deleteDumpEntry(asyncResp, dumpId, "System");
}

inline void handleLogServicesDumpEntryDownloadGet(
    crow::App& app, const std::string& dumpType, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& chassisId, const std::string& dumpId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    downloadDumpEntry(asyncResp, dumpId, dumpType);
}

inline void handleDBusEventLogEntryDownloadGet(
    crow::App& app, const std::string& dumpType, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName, const std::string& entryID)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (!http_helpers::isContentTypeAllowed(
            req.getHeaderValue("Accept"),
            http_helpers::ContentType::OctetStream, true))
    {
        asyncResp->res.result(boost::beast::http::status::bad_request);
        return;
    }
    downloadEventLogEntry(asyncResp, systemName, entryID, dumpType);
}

inline void handleLogServicesDumpCollectDiagnosticDataPost(
    crow::App& app, const std::string& dumpType, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }

    createDump(asyncResp, req, dumpType);
}

inline void handleLogServicesDumpCollectDiagnosticDataComputerSystemPost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    createDump(asyncResp, req, "System");
}

inline void handleLogServicesDumpClearLogPost(
    crow::App& app, const std::string& dumpType, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "Manager", managerId);
        return;
    }
    clearDump(asyncResp, dumpType);
}
inline void handleLogServicesDumpClearLogComputerSystemPost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
    {
        // Option currently returns no systems.  TBD
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                   systemName);
        return;
    }
    clearDump(asyncResp, "System");
}

inline void
    handleSetUpRedfishRoute(crow::App& app,
                            [[maybe_unused]] const std::string& dumpType,
                            const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            [[maybe_unused]] const std::string& managerId,
                            [[maybe_unused]] const std::string& dumpId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        BMCWEB_LOG_DEBUG("Aggregation is enabled. Forwarding URI");
        return;
    }

    // Do nothing for the Host BMC because it will not invoke this function
    return;
}

inline void requestRoutesBMCDumpServiceActionInfo(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/LogServices/Dump/CollectDiagnosticDataActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& managerName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        asyncResp->res.jsonValue["@odata.type"] =
            "#ActionInfo.v1_2_0.ActionInfo";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Managers/" +
            std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
            "/LogServices/Dump/CollectDiagnosticDataActionInfo";
        asyncResp->res.jsonValue["Name"] =
            "CollectDiagnosticDataActionInfo Action Info";
        asyncResp->res.jsonValue["Id"] = "CollectDiagnosticDataActionInfo";

        nlohmann::json::object_t parameter_diagnosticDataType;
        parameter_diagnosticDataType["Name"] = "DiagnosticDataType";
        parameter_diagnosticDataType["Required"] = true;
        parameter_diagnosticDataType["DataType"] = "String";

        nlohmann::json::array_t diagnosticDataType_allowableValues;
        diagnosticDataType_allowableValues.push_back("Manager");
        parameter_diagnosticDataType["AllowableValues"] =
            std::move(diagnosticDataType_allowableValues);

        nlohmann::json::array_t parameters;
        parameters.push_back(std::move(parameter_diagnosticDataType));

        asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
    });
}

inline void requestRoutesBMCDumpService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/LogServices/Dump/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleLogServicesDumpServiceGet, std::ref(app), "BMC"));
}

inline void requestRoutesBMCDumpEntryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/LogServices/Dump/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleLogServicesDumpEntriesCollectionGet, std::ref(app), "BMC"));
}

inline void requestRoutesBMCDumpEntry(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Managers/<str>/LogServices/Dump/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleLogServicesDumpEntryGet, std::ref(app), "BMC"));

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Managers/<str>/LogServices/Dump/Entries/<str>/")
        .privileges(redfish::privileges::deleteLogEntry)
        .methods(boost::beast::http::verb::delete_)(std::bind_front(
            handleLogServicesDumpEntryDelete, std::ref(app), "BMC"));
}

inline void requestRoutesBMCDumpEntryDownload(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/LogServices/Dump/Entries/<str>/attachment/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleLogServicesDumpEntryDownloadGet, std::ref(app), "BMC"));
}

inline void requestRoutesBMCDumpCreate(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/LogServices/Dump/Actions/LogService.CollectDiagnosticData/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleLogServicesDumpCollectDiagnosticDataPost,
                            std::ref(app), "BMC"));
}

inline void requestRoutesBMCDumpClear(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/LogServices/Dump/Actions/LogService.ClearLog/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleLogServicesDumpClearLogPost, std::ref(app), "BMC"));
}

inline void requestRoutesDBusEventLogEntryDownload(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/EventLog/Entries/<str>/attachment/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleDBusEventLogEntryDownloadGet, std::ref(app), "System"));
}

inline void requestRoutesFaultLogDumpService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/LogServices/FaultLog/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleLogServicesDumpServiceGet, std::ref(app), "FaultLog"));
}

inline void requestRoutesFaultLogDumpEntryCollection(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Managers/<str>/LogServices/FaultLog/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleLogServicesDumpEntriesCollectionGet,
                            std::ref(app), "FaultLog"));
}

inline void requestRoutesFaultLogDumpEntry(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/Managers/<str>/LogServices/FaultLog/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleLogServicesDumpEntryGet, std::ref(app), "FaultLog"));

    BMCWEB_ROUTE(
        app, "/redfish/v1/Managers/<str>/LogServices/FaultLog/Entries/<str>/")
        .privileges(redfish::privileges::deleteLogEntry)
        .methods(boost::beast::http::verb::delete_)(std::bind_front(
            handleLogServicesDumpEntryDelete, std::ref(app), "FaultLog"));
}

inline void requestRoutesFaultLogDumpClear(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/LogServices/FaultLog/Actions/LogService.ClearLog/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleLogServicesDumpClearLogPost, std::ref(app), "FaultLog"));
}

inline void requestRoutesSystemDumpEntryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/Dump/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleLogServicesDumpEntriesCollectionComputerSystemGet,
            std::ref(app)));
}

inline void requestRoutesSystemDumpService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/Dump/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleLogServicesDumpServiceComputerSystemGet, std::ref(app)));
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/Dump/")
        .privileges(redfish::privileges::patchLogService)
        .methods(boost::beast::http::verb::patch)(std::bind_front(
            handleLogServicesDumpServiceComputerSystemPatch, std::ref(app)));
}

inline void requestRoutesSystemDumpServiceActionInfo(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/Dump/CollectDiagnosticDataActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& managerName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        asyncResp->res.jsonValue["@odata.type"] =
            "#ActionInfo.v1_2_0.ActionInfo";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/LogServices/Dump/CollectDiagnosticDataActionInfo";
        asyncResp->res.jsonValue["Name"] =
            "CollectDiagnosticDataActionInfo Action Info";
        asyncResp->res.jsonValue["Id"] = "CollectDiagnosticDataActionInfo";

        nlohmann::json::object_t parameter_diagnosticDataType;
        parameter_diagnosticDataType["Name"] = "DiagnosticDataType";
        parameter_diagnosticDataType["Required"] = true;
        parameter_diagnosticDataType["DataType"] = "String";

        nlohmann::json::array_t diagnosticDataType_allowableValues;
        diagnosticDataType_allowableValues.push_back("OEM");
        parameter_diagnosticDataType["AllowableValues"] =
            std::move(diagnosticDataType_allowableValues);

        nlohmann::json::object_t parameter_OEMDiagnosticDataType;
        parameter_OEMDiagnosticDataType["Name"] = "OEMDiagnosticDataType";
        parameter_OEMDiagnosticDataType["Required"] = true;
        parameter_OEMDiagnosticDataType["DataType"] = "String";

        nlohmann::json::array_t OEMDiagnosticDataType_allowableValues;

        // Get the OEMDiagnosticDataType from meson option to push back
        std::string diagTypeStr = "";
        for (const auto& typeStr : BMCWEB_OEM_DIAGNOSTIC_ALLOWABLE_TYPE)
        {
            diagTypeStr = std::string("DiagnosticType=") + std::string(typeStr);
            OEMDiagnosticDataType_allowableValues.emplace_back(diagTypeStr);
        }

        parameter_OEMDiagnosticDataType["AllowableValues"] =
            std::move(OEMDiagnosticDataType_allowableValues);

        nlohmann::json::array_t parameters;
        parameters.push_back(std::move(parameter_diagnosticDataType));
        parameters.push_back(std::move(parameter_OEMDiagnosticDataType));

        asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
    });
}

inline void requestRoutesSystemDumpEntry(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/LogServices/Dump/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleLogServicesDumpEntryComputerSystemGet, std::ref(app)));

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/LogServices/Dump/Entries/<str>/")
        .privileges(redfish::privileges::deleteLogEntry)
        .methods(boost::beast::http::verb::delete_)(std::bind_front(
            handleLogServicesDumpEntryComputerSystemDelete, std::ref(app)));
}

inline void requestRoutesSystemDumpCreate(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/Dump/Actions/LogService.CollectDiagnosticData/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleLogServicesDumpCollectDiagnosticDataComputerSystemPost,
            std::ref(app)));
}

inline void requestRoutesSystemDumpClear(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/Dump/Actions/LogService.ClearLog/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleLogServicesDumpClearLogComputerSystemPost, std::ref(app)));
}

inline void requestRoutesSystemFaultLogService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/FaultLog/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName)

    {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/LogServices/FaultLog";
        asyncResp->res.jsonValue["@odata.type"] =
            "#LogService.v1_2_0.LogService";
        asyncResp->res.jsonValue["Name"] = "FaultLog LogService";
        asyncResp->res.jsonValue["Description"] = "System FaultLog LogService";
        asyncResp->res.jsonValue["Id"] = "FaultLog";
        asyncResp->res.jsonValue["OverWritePolicy"] = "WrapsWhenFull";

        std::pair<std::string, std::string> redfishDateTimeOffset =
            redfish::time_utils::getDateTimeOffsetNow();
        asyncResp->res.jsonValue["DateTime"] = redfishDateTimeOffset.first;
        asyncResp->res.jsonValue["DateTimeLocalOffset"] =
            redfishDateTimeOffset.second;

        asyncResp->res.jsonValue["Entries"] = {
            {"@odata.id", "/redfish/v1/Systems/" +
                              std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                              "/LogServices/FaultLog/Entries"}};
        asyncResp->res.jsonValue["Actions"] = {
            {"#LogService.ClearLog",
             {{"target",
               "/redfish/v1/Systems/" +
                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                   "/LogServices/FaultLog/Actions/LogService.ClearLog"}}}};
    });
}

inline void requestRoutesSystemFaultLogEntryCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/FaultLog/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        asyncResp->res.jsonValue["@odata.type"] =
            "#LogEntryCollection.LogEntryCollection";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/LogServices/FaultLog/Entries";
        asyncResp->res.jsonValue["Name"] = "System FaultLog Entries";
        asyncResp->res.jsonValue["Description"] =
            "Collection of System FaultLog Entries";

        getDumpEntryCollection(asyncResp, "FaultLog");
    });
}

inline void requestRoutesSystemFaultLogEntry(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/LogServices/FaultLog/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)

        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& param) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        getDumpEntryById(asyncResp, param, "FaultLog");
    });

    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/LogServices/FaultLog/Entries/<str>/")
        .privileges(redfish::privileges::deleteLogEntry)
        .methods(boost::beast::http::verb::delete_)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               [[maybe_unused]] const std::string& systemName,
               const std::string& param) {
        deleteDumpEntry(asyncResp, param, "FaultLog");
    });
}

inline void requestRoutesSystemFaultLogClear(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/FaultLog/Actions/LogService.ClearLog/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName)

    {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        clearDump(asyncResp, "FaultLog");
    });
}

inline void getFDRServiceState(const std::shared_ptr<bmcweb::AsyncResp>& aResp)
{
    constexpr const char* serviceName = "org.freedesktop.systemd1";
    // change fdrServiceObjectPath accoridng to FDR service name
    constexpr const char* fdrServiceObjectPath =
        "/org/freedesktop/systemd1/unit/nvidia_2dfdr_2eservice";
    constexpr const char* interfaceName = "org.freedesktop.systemd1.Unit";
    constexpr const char* property = "SubState";

    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, serviceName, fdrServiceObjectPath,
        interfaceName, property,
        [aResp](const boost::system::error_code ec,
                const std::string& serviceState) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("DBUS response error {}", ec);
            messages::internalError(aResp->res);
            return;
        }

        BMCWEB_LOG_DEBUG("serviceState : {}", serviceState);

        if (serviceState == "running")
        {
            aResp->res.jsonValue["ServiceEnabled"] = true;
        }
        else
        {
            aResp->res.jsonValue["ServiceEnabled"] = false;
        }
    });
}

inline void
    handleFDRServiceGet(crow::App& app, const crow::Request& req,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        [[maybe_unused]] const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    asyncResp->res.jsonValue["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
        "/LogServices/FDR";
    asyncResp->res.jsonValue["@odata.type"] = "#LogService.v1_2_0.LogService";
    asyncResp->res.jsonValue["Name"] = "FDR LogService";
    asyncResp->res.jsonValue["Description"] = "System FDR LogService";
    asyncResp->res.jsonValue["Id"] = "FDR";
    asyncResp->res.jsonValue["OverWritePolicy"] = "Unknown";

    std::pair<std::string, std::string> redfishDateTimeOffset =
        redfish::time_utils::getDateTimeOffsetNow();
    asyncResp->res.jsonValue["DateTime"] = redfishDateTimeOffset.first;
    asyncResp->res.jsonValue["DateTimeLocalOffset"] =
        redfishDateTimeOffset.second;
    asyncResp->res.jsonValue["Entries"] = {
        {"@odata.id", "/redfish/v1/Systems/" +
                          std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                          "/LogServices/FDR/Entries"}};
    asyncResp->res.jsonValue["Actions"]["#LogService.ClearLog"] = {
        {"target", "/redfish/v1/Systems/" +
                       std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                       "/LogServices/FDR/Actions/LogService.ClearLog"}};
    asyncResp->res
        .jsonValue["Actions"]["Oem"]["#NvidiaLogService.GenerateBirthCert"] = {
        {"target",
         "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
             "/LogServices/FDR/Actions/Oem/NvidiaLogService.GenerateBirthCert"}};
    asyncResp->res.jsonValue["Actions"]["#LogService.CollectDiagnosticData"] = {
        {"target",
         "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
             "/LogServices/FDR/Actions/LogService.CollectDiagnosticData"}};

    getFDRServiceState(asyncResp);
}

inline void
    handleFDRServicePatch(crow::App& app, const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          [[maybe_unused]] const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    std::optional<bool> enabled;

    if (!json_util::readJsonPatch(req, asyncResp->res, "ServiceEnabled",
                                  enabled))
    {
        BMCWEB_LOG_ERROR("Failed to get ServiceEnabled property");
        return;
    }

    if (!enabled.has_value())
    {
        BMCWEB_LOG_ERROR("No value for ServiceEnabled property");
        return;
    }

    BMCWEB_LOG_DEBUG("enabled = {}", *enabled);

    constexpr const char* serviceName = "org.freedesktop.systemd1";
    constexpr const char* objectPath = "/org/freedesktop/systemd1";
    constexpr const char* interfaceName = "org.freedesktop.systemd1.Manager";
    constexpr const char* startService = "StartUnit";
    constexpr const char* stopService = "StopUnit";
    constexpr const char* enableService = "EnableUnitFiles";
    constexpr const char* disableService = "DisableUnitFiles";
    // change fdrServiceName accoridng to FDR service name
    constexpr const char* fdrServiceName = "nvidia-fdr.service";

    if (*enabled)
    {
        // Try to enable service persistently
        constexpr bool runtime = false;
        constexpr bool force = false;

        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
        },
            serviceName, objectPath, interfaceName, enableService,
            std::array<std::string, 1>{fdrServiceName}, runtime, force);

        // Try to start service
        constexpr const char* mode = "replace";

        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
        },
            serviceName, objectPath, interfaceName, startService,
            fdrServiceName, mode);
    }
    else
    {
        // Try to stop service
        constexpr const char* mode = "replace";

        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
        },
            serviceName, objectPath, interfaceName, stopService, fdrServiceName,
            mode);

        // Try to disable service persistently
        constexpr bool runtime = false;

        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code ec) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }
        },
            serviceName, objectPath, interfaceName, disableService,
            std::array<std::string, 1>{fdrServiceName}, runtime);
    }
}

inline void requestRoutesSystemFDRService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/FDR/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleFDRServiceGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/FDR/")
        .privileges(redfish::privileges::patchLogService)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(handleFDRServicePatch, std::ref(app)));
}

inline void requestRoutesSystemFDREntryCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/FDR/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        asyncResp->res.jsonValue["@odata.type"] =
            "#LogEntryCollection.LogEntryCollection";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/LogServices/FDR/Entries";
        asyncResp->res.jsonValue["Name"] = "System FDR Entries";
        asyncResp->res.jsonValue["Description"] =
            "Collection of System FDR Entries";

        getDumpEntryCollection(asyncResp, "FDR");
    });
}

inline void requestRoutesSystemFDREntry(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/LogServices/FDR/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& param) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        getDumpEntryById(asyncResp, param, "FDR");
    });

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/LogServices/FDR/Entries/<str>/")
        .privileges(redfish::privileges::deleteLogEntry)
        .methods(boost::beast::http::verb::delete_)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& param) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        deleteDumpEntry(asyncResp, param, "FDR");
    });
}

inline void requestRoutesSystemFDREntryDownload(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/FDR/Entries/<str>/attachment/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& entryID) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        auto downloadDumpEntryHandler =
            [asyncResp, entryID](const boost::system::error_code& ec,
                                 const sdbusplus::message::unix_fd& unixfd) {
            downloadEntryCallback(asyncResp, entryID, "FDR", ec, unixfd);
        };

        sdbusplus::message::object_path entry(
            "/xyz/openbmc_project/dump/fdr/entry");
        entry /= entryID;
        crow::connections::systemBus->async_method_call(
            std::move(downloadDumpEntryHandler),
            "xyz.openbmc_project.Dump.Manager", entry,
            "xyz.openbmc_project.Dump.Entry", "GetFileHandle");
    });
}

inline void handleLogServicesFDRDumpCollectDiagnosticDataPost(
    crow::App& app, const std::string& dumpType, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& /*managerId*/)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    createDump(asyncResp, req, dumpType);
}

inline void requestRoutesSystemFDRCreate(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/FDR/Actions/LogService.CollectDiagnosticData/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleLogServicesFDRDumpCollectDiagnosticDataPost,
                            std::ref(app), "FDR"));
}

void inline requestRoutesSystemFDRClear(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/FDR/Actions/LogService.ClearLog/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        std::vector<std::pair<std::string, std::variant<std::string, uint64_t>>>
            createDumpParamVec;

        createDumpParamVec.emplace_back("DiagnosticType", "FDR");
        createDumpParamVec.emplace_back("Action", "Clean");

        crow::connections::systemBus->async_method_call(
            [asyncResp](
                const boost::system::error_code ec,
                const sdbusplus::message::message& msg,
                const sdbusplus::message::object_path& objPath) mutable {
            (void)msg;
            (void)objPath;

            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            messages::success(asyncResp->res);
        },
            "xyz.openbmc_project.Dump.Manager", "/xyz/openbmc_project/dump/fdr",
            "xyz.openbmc_project.Dump.Create", "CreateDump",
            createDumpParamVec);
    });
}

void inline requestRoutesSystemFDRGenBirthCert(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/FDR/Actions/Oem/NvidiaLogService.GenerateBirthCert/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        std::vector<std::pair<std::string, std::variant<std::string, uint64_t>>>
            createDumpParamVec;

        createDumpParamVec.emplace_back("DiagnosticType", "FDR");
        createDumpParamVec.emplace_back("Action", "GenBirthCert");

        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code ec,
                        [[maybe_unused]] const sdbusplus::message::message& msg,
                        [[maybe_unused]] const sdbusplus::message::object_path&
                            objPath) mutable {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            messages::success(asyncResp->res);
        },
            "xyz.openbmc_project.Dump.Manager", "/xyz/openbmc_project/dump/fdr",
            "xyz.openbmc_project.Dump.Create", "CreateDump",
            createDumpParamVec);
    });
}

inline void requestRoutesCrashdumpService(App& app)
{
    // Note: Deviated from redfish privilege registry for GET & HEAD
    // method for security reasons.
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/Crashdump/")
        // This is incorrect, should be:
        //.privileges(redfish::privileges::getLogService)
        .privileges({{"ConfigureManager"}})
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       BMCWEB_REDFISH_SYSTEM_URI_NAME);
            return;
        }

        // Copy over the static data to include the entries added by
        // SubRoute
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/LogServices/Crashdump";
        asyncResp->res.jsonValue["@odata.type"] =
            "#LogService.v1_2_0.LogService";
        asyncResp->res.jsonValue["Name"] = "Open BMC Oem Crashdump Service";
        asyncResp->res.jsonValue["Description"] = "Oem Crashdump Service";
        asyncResp->res.jsonValue["Id"] = "Crashdump";
        asyncResp->res.jsonValue["OverWritePolicy"] = "WrapsWhenFull";
        asyncResp->res.jsonValue["MaxNumberOfRecords"] = 3;

        std::pair<std::string, std::string> redfishDateTimeOffset =
            redfish::time_utils::getDateTimeOffsetNow();
        asyncResp->res.jsonValue["DateTime"] = redfishDateTimeOffset.first;
        asyncResp->res.jsonValue["DateTimeLocalOffset"] =
            redfishDateTimeOffset.second;

        asyncResp->res.jsonValue["Entries"] = {
            {"@odata.id", "/redfish/v1/Systems/" +
                              std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                              "/LogServices/Crashdump/Entries"}};
        asyncResp->res.jsonValue["Actions"] = {
            {"#LogService.ClearLog",
             {{"target",
               "/redfish/v1/Systems/" +
                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                   "/LogServices/Crashdump/Actions/LogService.ClearLog"}}},
            {"#LogService.CollectDiagnosticData",
             {{"target",
               "/redfish/v1/Systems/" +
                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                   "/LogServices/Crashdump/Actions/LogService.CollectDiagnosticData"}}}};
    });
}

void inline requestRoutesCrashdumpClear(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/Crashdump/Actions/LogService.ClearLog/")
        // This is incorrect, should be:
        //.privileges(redfish::privileges::postLogService)
        .privileges({{"ConfigureComponents"}})
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       BMCWEB_REDFISH_SYSTEM_URI_NAME);
            return;
        }
        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code& ec,
                        const std::string&) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            messages::success(asyncResp->res);
        },
            crashdumpObject, crashdumpPath, deleteAllInterface, "DeleteAll");
    });
}

static void
    logCrashdumpEntry(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& logID, nlohmann::json& logEntryJson)
{
    auto getStoredLogCallback =
        [asyncResp, logID,
         &logEntryJson](const boost::system::error_code& ec,
                        const dbus::utility::DBusPropertiesMap& params) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("failed to get log ec: {}", ec.message());
            if (ec.value() ==
                boost::system::linux_error::bad_request_descriptor)
            {
                messages::resourceNotFound(asyncResp->res, "LogEntry", logID);
            }
            else
            {
                messages::internalError(asyncResp->res);
            }
            return;
        }

        std::string timestamp{};
        std::string filename{};
        std::string logfile{};
        parseCrashdumpParameters(params, filename, timestamp, logfile);

        if (filename.empty() || timestamp.empty())
        {
            messages::resourceMissingAtURI(
                asyncResp->res,
                boost::urls::format(
                    "/redfish/v1/Systems/{}/LogServices/Crashdump/Entries/{}/{}",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME, logID, filename));
            return;
        }

        std::string crashdumpURI = "/redfish/v1/Systems/" +
                                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                   "/LogServices/Crashdump/Entries/" + logID +
                                   "/" + filename;
        nlohmann::json::object_t logEntry;
        logEntry["@odata.type"] = "#LogEntry.v1_15_0.LogEntry";
        logEntry["@odata.id"] = "/redfish/v1/Systems/" +
                                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                "/LogServices/Crashdump/Entries/" + logID;
        logEntry["Name"] = "CPU Crashdump";
        logEntry["Id"] = logID;
        logEntry["EntryType"] = "Oem";
        logEntry["AdditionalDataURI"] = std::move(crashdumpURI);
        logEntry["DiagnosticDataType"] = "OEM";
        logEntry["OEMDiagnosticDataType"] = "PECICrashdump";
        logEntry["Created"] = std::move(timestamp);

        // If logEntryJson references an array of LogEntry resources
        // ('Members' list), then push this as a new entry,
        // otherwise set it directly
        if (logEntryJson.is_array())
        {
            logEntryJson.push_back(logEntry);
            asyncResp->res.jsonValue["Members@odata.count"] =
                logEntryJson.size();
        }
        else
        {
            logEntryJson.update(logEntry);
        }
    };
    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, crashdumpObject,
        crashdumpPath + std::string("/") + logID, crashdumpInterface,
        std::move(getStoredLogCallback));
}

inline void requestRoutesCrashdumpEntryCollection(App& app)
{
    // Note: Deviated from redfish privilege registry for GET & HEAD
    // method for security reasons.
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/LogServices/Crashdump/Entries/")
        // This is incorrect, should be.
        //.privileges(redfish::privileges::postLogEntryCollection)
        .privileges({{"ConfigureComponents"}})
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       BMCWEB_REDFISH_SYSTEM_URI_NAME);
            return;
        }

        constexpr std::array<std::string_view, 1> interfaces = {
            crashdumpInterface};
        dbus::utility::getSubTreePaths(
            "/", 0, interfaces,
            [asyncResp](const boost::system::error_code& ec,
                        const std::vector<std::string>& resp) {
            if (ec)
            {
                if (ec.value() !=
                    boost::system::errc::no_such_file_or_directory)
                {
                    BMCWEB_LOG_DEBUG("failed to get entries ec: {}",
                                     ec.message());
                    messages::internalError(asyncResp->res);
                    return;
                }
            }
            asyncResp->res.jsonValue["@odata.type"] =
                "#LogEntryCollection.LogEntryCollection";
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                "/LogServices/Crashdump/Entries";
            asyncResp->res.jsonValue["Name"] = "Open BMC Crashdump Entries";
            asyncResp->res.jsonValue["Description"] =
                "Collection of Crashdump Entries";
            asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
            asyncResp->res.jsonValue["Members@odata.count"] = 0;

            for (const std::string& path : resp)
            {
                const sdbusplus::message::object_path objPath(path);
                // Get the log ID
                std::string logID = objPath.filename();
                if (logID.empty())
                {
                    continue;
                }
                // Add the log entry to the array
                logCrashdumpEntry(asyncResp, logID,
                                  asyncResp->res.jsonValue["Members"]);
            }
        });
    });
}

inline void requestRoutesCrashdumpEntry(App& app)
{
    // Note: Deviated from redfish privilege registry for GET & HEAD
    // method for security reasons.

    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/LogServices/Crashdump/Entries/<str>/")
        // this is incorrect, should be
        // .privileges(redfish::privileges::getLogEntry)
        .privileges({{"ConfigureComponents"}})
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& param) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       BMCWEB_REDFISH_SYSTEM_URI_NAME);
            return;
        }
        const std::string& logID = param;
        logCrashdumpEntry(asyncResp, logID, asyncResp->res.jsonValue);
    });
}

inline void requestRoutesCrashdumpFile(App& app)
{
    // Note: Deviated from redfish privilege registry for GET & HEAD
    // method for security reasons.
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/Crashdump/Entries/<str>/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& logID, const std::string& fileName) {
        // Do not call getRedfishRoute here since the crashdump file
        // is not a Redfish resource.
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       BMCWEB_REDFISH_SYSTEM_URI_NAME);
            return;
        }

        auto getStoredLogCallback =
            [asyncResp, logID, fileName, url(boost::urls::url(req.url()))](
                const boost::system::error_code& ec,
                const std::vector<
                    std::pair<std::string, dbus::utility::DbusVariantType>>&
                    resp) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("failed to get log ec: {}", ec.message());
                messages::internalError(asyncResp->res);
                return;
            }

            std::string dbusFilename{};
            std::string dbusTimestamp{};
            std::string dbusFilepath{};

            parseCrashdumpParameters(resp, dbusFilename, dbusTimestamp,
                                     dbusFilepath);

            if (dbusFilename.empty() || dbusTimestamp.empty() ||
                dbusFilepath.empty())
            {
                messages::resourceMissingAtURI(
                    asyncResp->res,
                    boost::urls::format(
                        "/redfish/v1/Systems/{}/LogServices/Crashdump/Entries/{}/{}",
                        BMCWEB_REDFISH_SYSTEM_URI_NAME, logID, fileName));
                return;
            }

            // Verify the file name parameter is correct
            if (fileName != dbusFilename)
            {
                messages::resourceMissingAtURI(
                    asyncResp->res,
                    boost::urls::format(
                        "/redfish/v1/Systems/{}/LogServices/Crashdump/Entries/{}/{}",
                        BMCWEB_REDFISH_SYSTEM_URI_NAME, logID, fileName));

                return;
            }

            if (!asyncResp->res.openFile(dbusFilepath))
            {
                messages::resourceMissingAtURI(
                    asyncResp->res,
                    boost::urls::format(
                        "/redfish/v1/Systems/{}/LogServices/Crashdump/Entries/{}/{}",
                        BMCWEB_REDFISH_SYSTEM_URI_NAME, logID, fileName));

                return;
            }

            // Configure this to be a file download when accessed
            // from a browser
            asyncResp->res.addHeader(
                boost::beast::http::field::content_disposition, "attachment");
        };
        sdbusplus::asio::getAllProperties(
            *crow::connections::systemBus, crashdumpObject,
            crashdumpPath + std::string("/") + logID, crashdumpInterface,
            std::move(getStoredLogCallback));
    });
}

enum class OEMDiagnosticType
{
    onDemand,
    telemetry,
    invalid,
};

inline OEMDiagnosticType getOEMDiagnosticType(std::string_view oemDiagStr)
{
    if (oemDiagStr == "OnDemand")
    {
        return OEMDiagnosticType::onDemand;
    }
    if (oemDiagStr == "Telemetry")
    {
        return OEMDiagnosticType::telemetry;
    }

    return OEMDiagnosticType::invalid;
}

inline void requestRoutesCrashdumpCollect(App& app)
{
    // Note: Deviated from redfish privilege registry for GET & HEAD
    // method for security reasons.
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/Crashdump/Actions/LogService.CollectDiagnosticData/")
        // The below is incorrect;  Should be ConfigureManager
        //.privileges(redfish::privileges::postLogService)
        .privileges({{"ConfigureComponents"}})
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       BMCWEB_REDFISH_SYSTEM_URI_NAME);
            return;
        }

        std::string diagnosticDataType;
        std::string oemDiagnosticDataType;
        if (!redfish::json_util::readJsonAction(
                req, asyncResp->res, "DiagnosticDataType", diagnosticDataType,
                "OEMDiagnosticDataType", oemDiagnosticDataType))
        {
            return;
        }

        if (diagnosticDataType != "OEM")
        {
            BMCWEB_LOG_ERROR(
                "Only OEM DiagnosticDataType supported for Crashdump");
            messages::actionParameterValueFormatError(
                asyncResp->res, diagnosticDataType, "DiagnosticDataType",
                "CollectDiagnosticData");
            return;
        }

        OEMDiagnosticType oemDiagType =
            getOEMDiagnosticType(oemDiagnosticDataType);

        std::string iface;
        std::string method;
        std::string taskMatchStr;
        if (oemDiagType == OEMDiagnosticType::onDemand)
        {
            iface = crashdumpOnDemandInterface;
            method = "GenerateOnDemandLog";
            taskMatchStr = "type='signal',"
                           "interface='org.freedesktop.DBus.Properties',"
                           "member='PropertiesChanged',"
                           "arg0namespace='com.intel.crashdump'";
        }
        else if (oemDiagType == OEMDiagnosticType::telemetry)
        {
            iface = crashdumpTelemetryInterface;
            method = "GenerateTelemetryLog";
            taskMatchStr = "type='signal',"
                           "interface='org.freedesktop.DBus.Properties',"
                           "member='PropertiesChanged',"
                           "arg0namespace='com.intel.crashdump'";
        }
        else
        {
            BMCWEB_LOG_ERROR("Unsupported OEMDiagnosticDataType: {}",
                             oemDiagnosticDataType);
            messages::actionParameterValueFormatError(
                asyncResp->res, oemDiagnosticDataType, "OEMDiagnosticDataType",
                "CollectDiagnosticData");
            return;
        }

        auto collectCrashdumpCallback =
            [asyncResp, payload(task::Payload(req)),
             taskMatchStr](const boost::system::error_code& ec,
                           const std::string&) mutable {
            if (ec)
            {
                if (ec.value() == boost::system::errc::operation_not_supported)
                {
                    messages::resourceInStandby(asyncResp->res);
                }
                else if (ec.value() ==
                         boost::system::errc::device_or_resource_busy)
                {
                    messages::serviceTemporarilyUnavailable(asyncResp->res,
                                                            "60");
                }
                else
                {
                    messages::internalError(asyncResp->res);
                }
                return;
            }
            std::shared_ptr<task::TaskData> task = task::TaskData::createTask(
                [](const boost::system::error_code& ec2, sdbusplus::message_t&,
                   const std::shared_ptr<task::TaskData>& taskData) {
                if (!ec2)
                {
                    taskData->messages.emplace_back(messages::taskCompletedOK(
                        std::to_string(taskData->index)));
                    taskData->state = "Completed";
                }
                return task::completed;
            },
                taskMatchStr);

            task->startTimer(std::chrono::minutes(5));
            task->populateResp(asyncResp->res);
            task->payload.emplace(std::move(payload));
        };

        crow::connections::systemBus->async_method_call(
            std::move(collectCrashdumpCallback), crashdumpObject, crashdumpPath,
            iface, method);
    });
}

/**
 * DBusLogServiceActionsClear class supports POST method for ClearLog action.
 */
inline void requestRoutesDBusLogServiceActionsClear(App& app)
{
    /**
     * Function handles POST method request.
     * The Clear Log actions does not require any parameter.The action deletes
     * all entries found in the Entries collection for this Log Service.
     */

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/EventLog/Actions/LogService.ClearLog/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       BMCWEB_REDFISH_SYSTEM_URI_NAME);
            return;
        }
        BMCWEB_LOG_DEBUG("Do delete all entries.");

        // Process response from Logging service.
        auto respHandler = [asyncResp](const boost::system::error_code& ec) {
            BMCWEB_LOG_DEBUG("doClearLog resp_handler callback: Done");
            if (ec)
            {
                // TODO Handle for specific error code
                BMCWEB_LOG_ERROR("doClearLog resp_handler got error {}", ec);
                asyncResp->res.result(
                    boost::beast::http::status::internal_server_error);
                return;
            }

            asyncResp->res.result(boost::beast::http::status::no_content);
        };

        // Make call to Logging service to request Clear Log
        crow::connections::systemBus->async_method_call(
            respHandler, "xyz.openbmc_project.Logging",
            "/xyz/openbmc_project/logging",
            "xyz.openbmc_project.Collection.DeleteAll", "DeleteAll");
    });
}

#ifdef BMCWEB_ENABLE_MFG_TEST_API
static std::shared_ptr<task::TaskData> mfgTestTask;
static std::shared_ptr<boost::process::child> mfgTestProc;
static std::vector<char> mfgTestProcOutput(128, 0);
static std::vector<std::string> scriptExecOutputFiles;

/**
 * @brief Copy script output file to the predefined location.
 *
 * @param[in]  postCodeID     Post Code ID
 * @param[out] currentValue   Current value
 * @param[out] index          Index value
 *
 * @return int -1 if an error occurred, filename index in
 * scriptExecOutputFiles vector otherwise.
 */
static int copyMfgTestOutputFile(std::string& path)
{
    static const std::string redfishLogDir = "/var/log/";
    static const std::string mfgTestPrefix = "mfgtest-";
    std::error_code ec;

    bool fileExists = std::filesystem::exists(path, ec);
    if (ec)
    {
        BMCWEB_LOG_ERROR("File access error: {}", ec.message());
    }
    else if (!fileExists)
    {
        BMCWEB_LOG_ERROR("{} does not exist", path);
    }
    else
    {
        std::string filename = mfgTestPrefix +
                               std::to_string(scriptExecOutputFiles.size());
        std::string targetPath = redfishLogDir + filename;
        BMCWEB_LOG_DEBUG("Copying output to {}", targetPath);
        std::filesystem::copy(path, targetPath, ec);
        if (ec)
        {
            BMCWEB_LOG_ERROR("File copy error: {}", ec.message());
        }
        else
        {
            scriptExecOutputFiles.push_back(targetPath);
            return static_cast<int>(scriptExecOutputFiles.size()) - 1;
        }
    }

    return -1;
}

/**
 * @brief On-exit callback for the manufacturing script subprocess
 *
 * @param[in]  exitCode     Exit code of the script subprocess
 * @param[in]  ec           Optional system error code
 *
 */
static void mfgTestProcExitHandler(int exitCode, const std::error_code& ec)
{
    auto& t = mfgTestTask;
    if (ec)
    {
        BMCWEB_LOG_ERROR("Error executing script: {}", ec.message());
        t->state = "Aborted";
        t->messages.emplace_back(messages::internalError());
    }
    else
    {
        BMCWEB_LOG_DEBUG("Script exit code: {}", exitCode);
        if (exitCode == 0)
        {
            std::string output(mfgTestProcOutput.data());
            int id = copyMfgTestOutputFile(output);
            if (id != -1)
            {
                std::string path = "/redfish/v1/Systems/" +
                                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                   "/LogServices/EventLog/DiagnosticData/" +
                                   std::to_string(id);
                std::string location = "Location: " + path + "/attachment";
                t->payload->httpHeaders.emplace_back(std::move(location));
                t->state = "Completed";
                t->percentComplete = 100;
                t->messages.emplace_back(
                    messages::taskCompletedOK(std::to_string(t->index)));
            }
            else
            {
                t->state = "Exception";
                BMCWEB_LOG_ERROR(
                    "CopyMfgTestOutputFile failed with Output file error");
                t->messages.emplace_back(
                    messages::taskAborted(std::to_string(t->index)));
            }
        }
        else
        {
            t->state = "Exception";
            BMCWEB_LOG_ERROR("Mfg Script failed with exit code: {}", exitCode);
            t->messages.emplace_back(
                messages::taskAborted(std::to_string(t->index)));
        }
    }
    mfgTestProc = nullptr;
    mfgTestTask = nullptr;
    std::fill(mfgTestProcOutput.begin(), mfgTestProcOutput.end(), 0);
};

inline void requestRoutesEventLogDiagnosticDataCollect(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/EventLog/Actions/LogService.CollectDiagnosticData/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::string diagnosticDataType;
        std::string oemDiagnosticDataType;
        if (!redfish::json_util::readJsonAction(
                req, asyncResp->res, "DiagnosticDataType", diagnosticDataType,
                "OEMDiagnosticDataType", oemDiagnosticDataType))
        {
            return;
        }

        if (diagnosticDataType != "OEM")
        {
            BMCWEB_LOG_ERROR(
                "Only OEM DiagnosticDataType supported for EventLog");
            messages::actionParameterValueFormatError(
                asyncResp->res, diagnosticDataType, "DiagnosticDataType",
                "CollectDiagnosticData");
            return;
        }

        if (oemDiagnosticDataType == "Manufacturing")
        {
            if (mfgTestTask == nullptr)
            {
                mfgTestTask = task::TaskData::createTask(
                    [](boost::system::error_code, sdbusplus::message::message&,
                       const std::shared_ptr<task::TaskData>& taskData) {
                    mfgTestProc = nullptr;
                    mfgTestTask = nullptr;
                    if (taskData->percentComplete != 100)
                    {
                        taskData->state = "Exception";
                        taskData->messages.emplace_back(messages::taskAborted(
                            std::to_string(taskData->index)));
                    }
                    return task::completed;
                },
                    "0");
                mfgTestTask->payload.emplace(req);
                mfgTestTask->startTimer(std::chrono::seconds(mfgTestTimeout));
                try
                {
                    mfgTestProc = std::make_shared<boost::process::child>(
                        "/usr/bin/mfg-script-exec.sh",
                        "/usr/share/mfg-script-exec/config.yml",
                        boost::process::std_out >
                            boost::asio::buffer(mfgTestProcOutput),
                        crow::connections::systemBus->get_io_context(),
                        boost::process::on_exit = mfgTestProcExitHandler);
                }
                catch (const std::runtime_error& e)
                {
                    mfgTestTask->state = "Exception";
                    BMCWEB_LOG_ERROR(
                        "Manufacturing script failed with error: {}", e.what());
                    mfgTestTask->messages.emplace_back(messages::taskAborted(
                        std::to_string(mfgTestTask->index)));
                    mfgTestProc = nullptr;
                }
                mfgTestTask->populateResp(asyncResp->res);
                if (mfgTestProc == nullptr)
                {
                    mfgTestTask = nullptr;
                }
            }
            else
            {
                mfgTestTask->populateResp(asyncResp->res);
            }
        }
        else
        {
            BMCWEB_LOG_ERROR("Unsupported OEMDiagnosticDataType: {}",
                             oemDiagnosticDataType);
            messages::actionParameterValueFormatError(
                asyncResp->res, oemDiagnosticDataType, "OEMDiagnosticDataType",
                "CollectDiagnosticData");
            return;
        }
    });
}

inline void requestRoutesEventLogDiagnosticDataEntry(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/EventLog/DiagnosticData/<str>/attachment")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               [[maybe_unused]] const std::string& systemName,
               const std::string& idParam) {
        uint32_t id = 0;
        std::string_view paramSV(idParam);
        auto it = std::from_chars(paramSV.begin(), paramSV.end(), id);
        if (it.ec != std::errc())
        {
            messages::internalError(asyncResp->res);
            return;
        }

        auto files = scriptExecOutputFiles.size();
        if (files == 0 || id > files - 1)
        {
            messages::resourceMissingAtURI(
                asyncResp->res,
                boost::urls::format(
                    "/redfish/v1/Systems/{}/LogServices/EventLog/DiagnosticData/{}/attachment",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME, std::to_string(id)));
            return;
        }
        if (!asyncResp->res.openFile(scriptExecOutputFiles[id]))
        {
            messages::resourceMissingAtURI(
                asyncResp->res,
                boost::urls::format(
                    "/redfish/v1/Systems/{}/LogServices/EventLog/DiagnosticData/{}/attachment",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME, std::to_string(id)));
            return;
        }

        asyncResp->res.addHeader("Content-Type", "application/octet-stream");
        asyncResp->res.addHeader("Content-Transfer-Encoding", "Binary");
    });
}
#endif /* BMCWEB_ENABLE_MFG_TEST_API */

/****************************************************
 * Redfish PostCode interfaces
 * using DBUS interface: getPostCodesTS
 ******************************************************/
inline void requestRoutesPostCodesLogService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/PostCodes/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       BMCWEB_REDFISH_SYSTEM_URI_NAME);
            return;
        }

        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/LogServices/PostCodes";
        asyncResp->res.jsonValue["@odata.type"] =
            "#LogService.v1_2_0.LogService";
        asyncResp->res.jsonValue["Name"] = "POST Code Log Service";
        asyncResp->res.jsonValue["Description"] = "POST Code Log Service";
        asyncResp->res.jsonValue["Id"] = "PostCodes";
        asyncResp->res.jsonValue["OverWritePolicy"] = "WrapsWhenFull";
        asyncResp->res.jsonValue["Entries"]["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/LogServices/PostCodes/Entries";

        std::pair<std::string, std::string> redfishDateTimeOffset =
            redfish::time_utils::getDateTimeOffsetNow();
        asyncResp->res.jsonValue["DateTime"] = redfishDateTimeOffset.first;
        asyncResp->res.jsonValue["DateTimeLocalOffset"] =
            redfishDateTimeOffset.second;

        asyncResp->res.jsonValue["Actions"]["#LogService.ClearLog"] = {
            {"target",
             "/redfish/v1/Systems/" +
                 std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                 "/LogServices/PostCodes/Actions/LogService.ClearLog"}};
    });
}

inline void requestRoutesPostCodesClear(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/PostCodes/Actions/LogService.ClearLog/")
        // The following privilege is incorrect;  It should be ConfigureManager
        //.privileges(redfish::privileges::postLogService)
        .privileges({{"ConfigureComponents"}})
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       BMCWEB_REDFISH_SYSTEM_URI_NAME);
            return;
        }
        BMCWEB_LOG_DEBUG("Do delete all postcodes entries.");

        // Make call to post-code service to request clear all
        crow::connections::systemBus->async_method_call(
            [asyncResp](const boost::system::error_code& ec) {
            if (ec)
            {
                // TODO Handle for specific error code
                BMCWEB_LOG_ERROR("doClearPostCodes resp_handler got error {}",
                                 ec);
                asyncResp->res.result(
                    boost::beast::http::status::internal_server_error);
                messages::internalError(asyncResp->res);
                return;
            }
            messages::success(asyncResp->res);
        },
            "xyz.openbmc_project.State.Boot.PostCode0",
            "/xyz/openbmc_project/State/Boot/PostCode0",
            "xyz.openbmc_project.Collection.DeleteAll", "DeleteAll");
    });
}

/**
 * @brief Parse post code ID and get the current value and index value
 *        eg: postCodeID=B1-2, currentValue=1, index=2
 *
 * @param[in]  postCodeID     Post Code ID
 * @param[out] currentValue   Current value
 * @param[out] index          Index value
 *
 * @return bool true if the parsing is successful, false the parsing fails
 */
inline bool parsePostCode(const std::string& postCodeID, uint64_t& currentValue,
                          uint16_t& index)
{
    std::vector<std::string> split;
    bmcweb::split(split, postCodeID, '-');
    if (split.size() != 2 || split[0].length() < 2 || split[0].front() != 'B')
    {
        return false;
    }

    auto start = std::next(split[0].begin());
    auto end = split[0].end();
    auto [ptrIndex, ecIndex] = std::from_chars(&*start, &*end, index);

    if (ptrIndex != &*end || ecIndex != std::errc())
    {
        return false;
    }

    start = split[1].begin();
    end = split[1].end();

    auto [ptrValue, ecValue] = std::from_chars(&*start, &*end, currentValue);

    return ptrValue == &*end && ecValue == std::errc();
}

static bool fillPostCodeEntry(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const boost::container::flat_map<
        uint64_t, std::tuple<uint64_t, std::vector<uint8_t>>>& postcode,
    const uint16_t bootIndex, const uint64_t codeIndex = 0,
    const uint64_t skip = 0, const uint64_t top = 0)
{
    // Get the Message from the MessageRegistry
    const registries::Message* message =
        registries::getMessage("OpenBMC.0.2.BIOSPOSTCode");

    uint64_t currentCodeIndex = 0;
    uint64_t firstCodeTimeUs = 0;
    for (const std::pair<uint64_t, std::tuple<uint64_t, std::vector<uint8_t>>>&
             code : postcode)
    {
        currentCodeIndex++;
        std::string postcodeEntryID =
            "B" + std::to_string(bootIndex) + "-" +
            std::to_string(currentCodeIndex); // 1 based index in EntryID string

        uint64_t usecSinceEpoch = code.first;
        uint64_t usTimeOffset = 0;

        if (1 == currentCodeIndex)
        { // already incremented
            firstCodeTimeUs = code.first;
        }
        else
        {
            usTimeOffset = code.first - firstCodeTimeUs;
        }

        // skip if no specific codeIndex is specified and currentCodeIndex does
        // not fall between top and skip
        if ((codeIndex == 0) &&
            (currentCodeIndex <= skip || currentCodeIndex > top))
        {
            continue;
        }

        // skip if a specific codeIndex is specified and does not match the
        // currentIndex
        if ((codeIndex > 0) && (currentCodeIndex != codeIndex))
        {
            // This is done for simplicity. 1st entry is needed to calculate
            // time offset. To improve efficiency, one can get to the entry
            // directly (possibly with flatmap's nth method)
            continue;
        }

        // currentCodeIndex is within top and skip or equal to specified code
        // index

        // Get the Created time from the timestamp
        std::string entryTimeStr;
        entryTimeStr = redfish::time_utils::getDateTimeUintUs(usecSinceEpoch);

        // assemble messageArgs: BootIndex, TimeOffset(100us), PostCode(hex)
        std::ostringstream hexCode;
        hexCode << "0x";
#ifdef BMCWEB_ARRAY_BOOT_PROGRESS
        for (auto itr : std::get<1>(code.second))
        {
            hexCode << std::setfill('0') << std::setw(2) << std::hex
                    << static_cast<int>(itr);
        }
#else
        hexCode << std::setfill('0') << std::setw(2) << std::hex
                << std::get<0>(code.second);
#endif
        std::ostringstream timeOffsetStr;
        // Set Fixed -Point Notation
        timeOffsetStr << std::fixed;
        // Set precision to 4 digits
        timeOffsetStr << std::setprecision(4);
        // Add double to stream
        timeOffsetStr << static_cast<double>(usTimeOffset) / 1000 / 1000;

        std::string bootIndexStr = std::to_string(bootIndex);
        std::string timeOffsetString = timeOffsetStr.str();
        std::string hexCodeStr = hexCode.str();

        std::array<std::string_view, 3> messageArgs = {
            bootIndexStr, timeOffsetString, hexCodeStr};

        std::string msg =
            redfish::registries::fillMessageArgs(messageArgs, message->message);
        if (msg.empty())
        {
            messages::internalError(aResp->res);
            return false;
        }

        // Get Severity template from message registry
        std::string severity;
        if (message != nullptr)
        {
            severity = message->messageSeverity;
        }

        // Format entry
        nlohmann::json::object_t bmcLogEntry;
        bmcLogEntry["@odata.type"] = logEntryVersion;
        bmcLogEntry["@odata.id"] = "/redfish/v1/Systems/" +
                                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                   "/LogServices/PostCodes/Entries/" +
                                   postcodeEntryID;
        bmcLogEntry["Name"] = "POST Code Log Entry";
        bmcLogEntry["Id"] = postcodeEntryID;
        bmcLogEntry["Message"] = std::move(msg);
        bmcLogEntry["MessageId"] = "OpenBMC.0.2.BIOSPOSTCode";
        bmcLogEntry["MessageArgs"] = messageArgs;
        bmcLogEntry["EntryType"] = "Event";
        bmcLogEntry["Severity"] = std::move(severity);
        bmcLogEntry["Created"] = entryTimeStr;
        if (!std::get<std::vector<uint8_t>>(code.second).empty())
        {
            bmcLogEntry["AdditionalDataURI"] =
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                "/LogServices/PostCodes/Entries/" + postcodeEntryID +
                "/attachment";
        }

        // codeIndex is only specified when querying single entry, return only
        // that entry in this case
        if (codeIndex != 0)
        {
            aResp->res.jsonValue.update(bmcLogEntry);
            return true;
        }

        nlohmann::json& logEntryArray = aResp->res.jsonValue["Members"];
        logEntryArray.push_back(std::move(bmcLogEntry));
    }

    // Return value is always false when querying multiple entries
    return false;
}

static void getPostCodeForEntry(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                                const std::string& entryId)
{
    uint16_t bootIndex = 0;
    uint64_t codeIndex = 0;

    if (!parsePostCode(entryId, codeIndex, bootIndex))
    {
        // Requested ID was not found
        messages::resourceNotFound(aResp->res, "LogEntry", entryId);
        return;
    }

    if (bootIndex == 0 || codeIndex == 0)
    {
        // 0 is an invalid index
        messages::resourceNotFound(aResp->res, "LogEntry", entryId);
        return;
    }
    crow::connections::systemBus->async_method_call(
        [aResp, entryId, bootIndex,
         codeIndex](const boost::system::error_code ec,
                    const boost::container::flat_map<
                        uint64_t, std::tuple<uint64_t, std::vector<uint8_t>>>&
                        postcode) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS POST CODE PostCode response error");
            messages::internalError(aResp->res);
            return;
        }

        // skip the empty postcode boots
        if (postcode.empty())
        {
            messages::resourceNotFound(aResp->res, "LogEntry", entryId);
            return;
        }

        aResp->res.jsonValue["Members@odata.count"] =
            aResp->res.jsonValue["Members"].size();
        if (!fillPostCodeEntry(aResp, postcode, bootIndex, codeIndex))
        {
            messages::resourceNotFound(aResp->res, "LogEntry", entryId);
            return;
        }
    },
        "xyz.openbmc_project.State.Boot.PostCode0",
        "/xyz/openbmc_project/State/Boot/PostCode0",
        "xyz.openbmc_project.State.Boot.PostCode", "GetPostCodesWithTimeStamp",
        bootIndex);
}

static void
    getPostCodeForBoot(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const uint16_t bootIndex, const uint16_t bootCount,
                       const uint64_t entryCount, size_t skip, size_t top)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, bootIndex, bootCount, entryCount, skip,
         top](const boost::system::error_code& ec,
              const boost::container::flat_map<
                  uint64_t, std::tuple<uint64_t, std::vector<uint8_t>>>&
                  postcode) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS POST CODE PostCode response error");
            messages::internalError(asyncResp->res);
            return;
        }

        uint64_t endCount = entryCount;
        if (!postcode.empty())
        {
            endCount = entryCount + postcode.size();
            if (skip < endCount && (top + skip) > entryCount)
            {
                uint64_t thisBootSkip = std::max(static_cast<uint64_t>(skip),
                                                 entryCount) -
                                        entryCount;
                uint64_t thisBootTop =
                    std::min(static_cast<uint64_t>(top + skip), endCount) -
                    entryCount;

                fillPostCodeEntry(asyncResp, postcode, bootIndex, 0,
                                  thisBootSkip, thisBootTop);
            }
            asyncResp->res.jsonValue["Members@odata.count"] = endCount;
        }

        // continue to previous bootIndex
        if (bootIndex < bootCount)
        {
            getPostCodeForBoot(asyncResp, static_cast<uint16_t>(bootIndex + 1),
                               bootCount, endCount, skip, top);
        }
        else if (skip + top < endCount)
        {
            asyncResp->res.jsonValue["Members@odata.nextLink"] =
                std::format(
                    "/redfish/v1/Systems/{}/LogServices/PostCodes/Entries?$skip=",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                std::to_string(skip + top);
        }
    },
        "xyz.openbmc_project.State.Boot.PostCode0",
        "/xyz/openbmc_project/State/Boot/PostCode0",
        "xyz.openbmc_project.State.Boot.PostCode", "GetPostCodesWithTimeStamp",
        bootIndex);
}

static void
    getCurrentBootNumber(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         size_t skip, size_t top)
{
    uint64_t entryCount = 0;
    sdbusplus::asio::getProperty<uint16_t>(
        *crow::connections::systemBus,
        "xyz.openbmc_project.State.Boot.PostCode0",
        "/xyz/openbmc_project/State/Boot/PostCode0",
        "xyz.openbmc_project.State.Boot.PostCode", "CurrentBootCycleCount",
        [asyncResp, entryCount, skip, top](const boost::system::error_code& ec,
                                           const uint16_t bootCount) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }
        getPostCodeForBoot(asyncResp, 1, bootCount, entryCount, skip, top);
    });
}

inline void requestRoutesPostCodesEntryCollection(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/<str>/LogServices/PostCodes/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& systemName) {
        query_param::QueryCapabilities capabilities = {
            .canDelegateTop = true,
            .canDelegateSkip = true,
        };
        query_param::Query delegatedQuery;
        if (!redfish::setUpRedfishRouteWithDelegation(
                app, req, asyncResp, delegatedQuery, capabilities))
        {
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       systemName);
            return;
        }

        if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
        {
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       systemName);
            return;
        }
        asyncResp->res.jsonValue["@odata.type"] =
            "#LogEntryCollection.LogEntryCollection";
        asyncResp->res.jsonValue["@odata.id"] =
            std::format("/redfish/v1/Systems/{}/LogServices/PostCodes/Entries",
                        BMCWEB_REDFISH_SYSTEM_URI_NAME);
        asyncResp->res.jsonValue["Name"] = "BIOS POST Code Log Entries";
        asyncResp->res.jsonValue["Description"] =
            "Collection of POST Code Log Entries";
        asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
        asyncResp->res.jsonValue["Members@odata.count"] = 0;
        size_t skip = delegatedQuery.skip.value_or(0);
        size_t top = delegatedQuery.top.value_or(query_param::Query::maxTop);
        getCurrentBootNumber(asyncResp, skip, top);
    });
}

inline void requestRoutesPostCodesEntryAdditionalData(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/PostCodes/Entries/<str>/attachment/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& systemName,
                   const std::string& postCodeID) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if (!http_helpers::isContentTypeAllowed(
                req.getHeaderValue("Accept"),
                http_helpers::ContentType::OctetStream, true))
        {
            asyncResp->res.result(boost::beast::http::status::bad_request);
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       systemName);
            return;
        }
        if (systemName != BMCWEB_REDFISH_SYSTEM_URI_NAME)
        {
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       systemName);
            return;
        }

        uint64_t currentValue = 0;
        uint16_t index = 0;
        if (!parsePostCode(postCodeID, currentValue, index))
        {
            messages::resourceNotFound(asyncResp->res, "LogEntry", postCodeID);
            return;
        }

        crow::connections::systemBus->async_method_call(
            [asyncResp, postCodeID, currentValue](
                const boost::system::error_code& ec,
                const std::vector<std::tuple<uint64_t, std::vector<uint8_t>>>&
                    postcodes) {
            if (ec.value() == EBADR)
            {
                messages::resourceNotFound(asyncResp->res, "LogEntry",
                                           postCodeID);
                return;
            }
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error {}", ec);
                messages::internalError(asyncResp->res);
                return;
            }

            size_t value = static_cast<size_t>(currentValue) - 1;
            if (value == std::string::npos || postcodes.size() < currentValue)
            {
                BMCWEB_LOG_WARNING("Wrong currentValue value");
                messages::resourceNotFound(asyncResp->res, "LogEntry",
                                           postCodeID);
                return;
            }

            const auto& [tID, c] = postcodes[value];
            if (c.empty())
            {
                BMCWEB_LOG_WARNING("No found post code data");
                messages::resourceNotFound(asyncResp->res, "LogEntry",
                                           postCodeID);
                return;
            }
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            const char* d = reinterpret_cast<const char*>(c.data());
            std::string_view strData(d, c.size());

            asyncResp->res.addHeader(boost::beast::http::field::content_type,
                                     "application/octet-stream");
            asyncResp->res.addHeader(
                boost::beast::http::field::content_transfer_encoding, "Base64");
            asyncResp->res.write(crow::utility::base64encode(strData));
        },
            "xyz.openbmc_project.State.Boot.PostCode0",
            "/xyz/openbmc_project/State/Boot/PostCode0",
            "xyz.openbmc_project.State.Boot.PostCode", "GetPostCodes", index);
    });
}

inline void requestRoutesPostCodesEntry(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/LogServices/PostCodes/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& targetID) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if constexpr (BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            // Option currently returns no systems.  TBD
            messages::resourceNotFound(asyncResp->res, "ComputerSystem",
                                       BMCWEB_REDFISH_SYSTEM_URI_NAME);
            return;
        }
        getPostCodeForEntry(asyncResp, targetID);
    });
}

inline void requestRoutesChassisLogServiceCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/LogServices/")
        .privileges(redfish::privileges::getLogServiceCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& chassisId)

    {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        const std::array<const char*, 2> interfaces = {
            "xyz.openbmc_project.Inventory.Item.Board",
            "xyz.openbmc_project.Inventory.Item.Chassis"};

        crow::connections::systemBus->async_method_call(
            [asyncResp, chassisId(std::string(chassisId))](
                const boost::system::error_code ec,
                const crow::openbmc_mapper::GetSubTreeType& subtree) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            // Iterate over all retrieved ObjectPaths.
            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                     object : subtree)
            {
                const std::string& path = object.first;

                sdbusplus::message::object_path objPath(path);
                if (objPath.filename() != chassisId)
                {
                    continue;
                }
                // Collections don't include the static data added
                // by SubRoute because it has a duplicate entry for
                // members
                asyncResp->res.jsonValue["@odata.type"] =
                    "#LogServiceCollection.LogServiceCollection";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Chassis/" + chassisId + "/LogServices";
                asyncResp->res.jsonValue["Name"] =
                    "System Log Services Collection";
                asyncResp->res.jsonValue["Description"] =
                    "Collection of LogServices for this Computer System";
                nlohmann::json& logServiceArray =
                    asyncResp->res.jsonValue["Members"];
                logServiceArray = nlohmann::json::array();

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_LOGSERVICES
                const std::vector<
                    std::pair<std::string, std::vector<std::string>>>&
                    connectionNames = object.second;
                const std::string& connectionName = connectionNames[0].first;

                BMCWEB_LOG_DEBUG(
                    "XID Looking for PrettyName on service {} path {}",
                    connectionName, path);
                sdbusplus::asio::getProperty<std::string>(
                    *crow::connections::systemBus, connectionName, path,
                    "xyz.openbmc_project.Inventory.Item", "PrettyName",
                    [asyncResp, chassisId(std::string(chassisId))](
                        const boost::system::error_code ec,
                        const std::string& chassisName) {
                    if (!ec)
                    {
                        BMCWEB_LOG_DEBUG("XID Looking for Namespace on {}_XID",
                                         chassisName);
                        crow::connections::systemBus->async_method_call(
                            [asyncResp, chassisId(std::string(chassisId))](
                                const boost::system::error_code ec,
                                const std::tuple<uint32_t,
                                                 uint64_t>& /*reqData*/) {
                            if (!ec)
                            {
                                nlohmann::json& logServiceArray =
                                    asyncResp->res.jsonValue["Members"];
                                logServiceArray.push_back(
                                    {{"@odata.id", "/redfish/v1/Chassis/" +
                                                       chassisId +
                                                       "/LogServices/XID"}});
                                asyncResp->res
                                    .jsonValue["Members@odata.count"] =
                                    logServiceArray.size();
                            }
                        },
                            "xyz.openbmc_project.Logging",
                            "/xyz/openbmc_project/logging",
                            "xyz.openbmc_project.Logging.Namespace", "GetStats",
                            chassisName + "_XID");
                    }
                });
#endif // BMCWEB_ENABLE_NVIDIA_OEM_LOGSERVICES

                asyncResp->res.jsonValue["Members@odata.count"] =
                    logServiceArray.size();
                return;
            }
            // Couldn't find an object with that name.  return an
            // error
            messages::resourceNotFound(asyncResp->res,
                                       "#Chassis.v1_17_0.Chassis", chassisId);
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/inventory", 0, interfaces);
    });
}

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_LOGSERVICES
inline void requestRoutesChassisXIDLogService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/LogServices/XID/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& chassisId) {
        const std::array<const char*, 2> interfaces = {
            "xyz.openbmc_project.Inventory.Item.Board",
            "xyz.openbmc_project.Inventory.Item.Chassis"};
        crow::connections::systemBus->async_method_call(
            [asyncResp, chassisId(std::string(chassisId))](
                const boost::system::error_code ec,
                const crow::openbmc_mapper::GetSubTreeType& subtree) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            // Iterate over all retrieved ObjectPaths.
            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                     object : subtree)
            {
                const std::string& path = object.first;
                const std::vector<
                    std::pair<std::string, std::vector<std::string>>>&
                    connectionNames = object.second;

                const std::string& connectionName = connectionNames[0].first;

                sdbusplus::message::object_path objPath(path);
                if (objPath.filename() != chassisId)
                {
                    continue;
                }
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Chassis/" + chassisId + "/LogServices/XID";
                asyncResp->res.jsonValue["@odata.type"] =
                    "#LogService.v1_1_0.LogService";
                asyncResp->res.jsonValue["Name"] = "XID Log Service";
                asyncResp->res.jsonValue["Description"] = "XID Log Service";
                asyncResp->res.jsonValue["Id"] = "XID";
                asyncResp->res.jsonValue["OverWritePolicy"] = "WrapsWhenFull";

                std::pair<std::string, std::string> redfishDateTimeOffset =
                    redfish::time_utils::getDateTimeOffsetNow();

                asyncResp->res.jsonValue["DateTime"] =
                    redfishDateTimeOffset.first;
                asyncResp->res.jsonValue["DateTimeLocalOffset"] =
                    redfishDateTimeOffset.second;

                const std::string inventoryItemInterface =
                    "xyz.openbmc_project.Inventory.Item";

                sdbusplus::asio::getProperty<std::string>(
                    *crow::connections::systemBus, connectionName, path,
                    inventoryItemInterface, "PrettyName",
                    [asyncResp, chassisId(std::string(chassisId))](
                        const boost::system::error_code ec,
                        const std::string& chassisName) {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG("DBus response error for PrettyName");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    // Call Phosphor-logging GetStats method to get
                    // LatestEntryTimestamp and LatestEntryID
                    crow::connections::systemBus->async_method_call(
                        [asyncResp](
                            const boost::system::error_code ec,
                            const std::tuple<uint32_t, uint64_t>& reqData) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR(
                                "Failed to get Data from xyz.openbmc_project.Logging GetStats: {}",
                                ec);
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        auto lastTimeStamp = redfish::time_utils::getTimestamp(
                            std::get<1>(reqData));
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                            "#NvidiaLogService.v1_3_0.NvidiaLogService";
                        asyncResp->res
                            .jsonValue["Oem"]["Nvidia"]["LatestEntryID"] =
                            std::to_string(std::get<0>(reqData));
                        asyncResp->res.jsonValue["Oem"]["Nvidia"]
                                                ["LatestEntryTimeStamp"] =
                            redfish::time_utils::getDateTimeStdtime(
                                lastTimeStamp);
                    },
                        "xyz.openbmc_project.Logging",
                        "/xyz/openbmc_project/logging",
                        "xyz.openbmc_project.Logging.Namespace", "GetStats",
                        chassisName + "_XID");
                });
                if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
                {
                    if (bmcwebEnableNvidiaBootEntryId)
                    {
                        populateBootEntryId(asyncResp->res);
                    }
                }
                asyncResp->res.jsonValue["Entries"] = {
                    {"@odata.id", "/redfish/v1/Chassis/" + chassisId +
                                      "/LogServices/XID/Entries"}};
                return;
            }
            // Couldn't find an object with that name.  return an
            // error
            messages::resourceNotFound(asyncResp->res,
                                       "#Chassis.v1_17_0.Chassis", chassisId);
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/inventory", 0, interfaces);
    });
}

inline void requestRoutesChassisXIDLogEntryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/LogServices/XID/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& chassisId) {
        const std::array<const char*, 2> interfaces = {
            "xyz.openbmc_project.Inventory.Item.Board",
            "xyz.openbmc_project.Inventory.Item.Chassis"};

        crow::connections::systemBus->async_method_call(
            [asyncResp, chassisId(std::string(chassisId))](
                const boost::system::error_code ec,
                const crow::openbmc_mapper::GetSubTreeType& subtree) {
            if (ec)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            // Iterate over all retrieved ObjectPaths.
            for (const std::pair<std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
                     object : subtree)
            {
                const std::string& path = object.first;
                const std::vector<
                    std::pair<std::string, std::vector<std::string>>>&
                    connectionNames = object.second;

                sdbusplus::message::object_path objPath(path);
                if (objPath.filename() != chassisId)
                {
                    continue;
                }

                const std::string& connectionName = connectionNames[0].first;
                const std::vector<std::string>& interfaces2 =
                    connectionNames[0].second;

                const std::string inventoryItemInterface =
                    "xyz.openbmc_project.Inventory.Item";
                if (std::find(interfaces2.begin(), interfaces2.end(),
                              inventoryItemInterface) != interfaces2.end())
                {
                    sdbusplus::asio::getProperty<std::string>(
                        *crow::connections::systemBus, connectionName, path,
                        inventoryItemInterface, "PrettyName",
                        [asyncResp, chassisId(std::string(chassisId))](
                            const boost::system::error_code ec,
                            const std::string& chassisName) {
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG(
                                "DBus response error for PrettyName");
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        BMCWEB_LOG_DEBUG("PrettyName: {}", chassisName);
                        // Collections don't include the static data
                        // added by SubRoute because it has a
                        // duplicate entry for members
                        asyncResp->res.jsonValue["@odata.type"] =
                            "#LogEntryCollection.LogEntryCollection";
                        asyncResp->res.jsonValue["@odata.id"] =
                            "/redfish/v1/Chassis/" + chassisId +
                            "/LogServices/XID/Entries";
                        asyncResp->res.jsonValue["Name"] = "XID Log Entries";
                        asyncResp->res.jsonValue["Description"] =
                            "Collection of XID Log Entries";
                        asyncResp->res.jsonValue["Members@odata.count"] = 0;

                        BMCWEB_LOG_DEBUG("Namespace: {}_XID", chassisName);

                        // DBus implementation of EventLog/Entries
                        // Make call to Logging Service to find all
                        // log entry objects
                        crow::connections::systemBus->async_method_call(
                            [asyncResp, chassisId(std::string(chassisId))](
                                const boost::system::error_code ec,
                                const GetManagedObjectsType& resp) {
                            if (ec)
                            {
                                // TODO Handle for
                                // specific error code
                                BMCWEB_LOG_ERROR(
                                    "getLogEntriesIfaceData resp_handler got error {}",
                                    ec.message());
                                messages::internalError(asyncResp->res);
                                return;
                            }

                            const uint32_t* id = nullptr;
                            std::time_t timestamp{};
                            std::time_t updateTimestamp{};
                            const std::string* severity = nullptr;
                            const std::string* eventId = nullptr;
                            const std::string* message = nullptr;
                            const std::string* filePath = nullptr;
                            bool resolved = false;
                            const std::string* resolution = nullptr;
                            const std::vector<std::string>* additionalDataRaw =
                                nullptr;

                            nlohmann::json& entriesArray =
                                asyncResp->res.jsonValue["Members"];
                            entriesArray = nlohmann::json::array();

                            for (auto& objectPath : resp)
                            {
                                nlohmann::json thisEntry =
                                    nlohmann::json::object();

                                for (auto& interfaceMap : objectPath.second)
                                {
                                    if (interfaceMap.first ==
                                        "xyz.openbmc_project.Logging.Entry")
                                    {
                                        nlohmann::json thisEntry =
                                            nlohmann::json::object();

                                        for (auto& propertyMap :
                                             interfaceMap.second)
                                        {
                                            if (propertyMap.first == "Id")
                                            {
                                                id = std::get_if<uint32_t>(
                                                    &propertyMap.second);
                                            }
                                            else if (propertyMap.first ==
                                                     "Timestamp")
                                            {
                                                const uint64_t*
                                                    millisTimeStamp =
                                                        std::get_if<uint64_t>(
                                                            &propertyMap
                                                                 .second);
                                                if (millisTimeStamp != nullptr)
                                                {
                                                    timestamp = redfish::
                                                        time_utils::getTimestamp(
                                                            *millisTimeStamp);
                                                }
                                            }
                                            else if (propertyMap.first ==
                                                     "UpdateTimestamp")
                                            {
                                                const uint64_t*
                                                    millisTimeStamp =
                                                        std::get_if<uint64_t>(
                                                            &propertyMap
                                                                 .second);
                                                if (millisTimeStamp != nullptr)
                                                {
                                                    updateTimestamp = redfish::
                                                        time_utils::getTimestamp(
                                                            *millisTimeStamp);
                                                }
                                            }
                                            else if (propertyMap.first ==
                                                     "Severity")
                                            {
                                                severity =
                                                    std::get_if<std::string>(
                                                        &propertyMap.second);
                                            }
                                            else if (propertyMap.first ==
                                                     "Message")
                                            {
                                                message =
                                                    std::get_if<std::string>(
                                                        &propertyMap.second);
                                            }
                                            else if (propertyMap.first ==
                                                     "Resolved")
                                            {
                                                const bool* resolveptr =
                                                    std::get_if<bool>(
                                                        &propertyMap.second);
                                                if (resolveptr == nullptr)
                                                {
                                                    messages::internalError(
                                                        asyncResp->res);
                                                    return;
                                                }
                                                resolved = *resolveptr;
                                            }
                                            else if (propertyMap.first ==
                                                     "Resolution")
                                            {
                                                resolution =
                                                    std::get_if<std::string>(
                                                        &propertyMap.second);
                                            }
                                            else if (propertyMap.first ==
                                                     "AdditionalData")
                                            {
                                                additionalDataRaw = std::get_if<
                                                    std::vector<std::string>>(
                                                    &propertyMap.second);
                                            }
                                            else if (propertyMap.first ==
                                                     "Path")
                                            {
                                                filePath =
                                                    std::get_if<std::string>(
                                                        &propertyMap.second);
                                            }
                                            else if (propertyMap.first ==
                                                     "EventId")
                                            {
                                                eventId =
                                                    std::get_if<std::string>(
                                                        &propertyMap.second);
                                            }
                                        }
                                        if (id == nullptr ||
                                            message == nullptr ||
                                            severity == nullptr)
                                        {
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }

                                        // Determine if
                                        // it's a
                                        // message
                                        // registry
                                        // format or
                                        // not.
                                        bool isMessageRegistry = false;
                                        std::string messageId;
                                        std::string messageArgs;
                                        std::string originOfCondition;
                                        std::string deviceName;
                                        if (additionalDataRaw != nullptr)
                                        {
                                            AdditionalData additional(
                                                *additionalDataRaw);
                                            if (additional.count(
                                                    "REDFISH_MESSAGE_ID") > 0)
                                            {
                                                isMessageRegistry = true;
                                                messageId = additional
                                                    ["REDFISH_MESSAGE_ID"];
                                                BMCWEB_LOG_DEBUG(
                                                    "MessageId: [{}]",
                                                    messageId);

                                                if (additional.count(
                                                        "REDFISH_MESSAGE_ARGS") >
                                                    0)
                                                {
                                                    messageArgs = additional
                                                        ["REDFISH_MESSAGE_ARGS"];
                                                }
                                            }
                                            if (additional.count(
                                                    "REDFISH_ORIGIN_OF_CONDITION") >
                                                0)
                                            {
                                                originOfCondition = additional
                                                    ["REDFISH_ORIGIN_OF_CONDITION"];
                                            }
                                            if (additional.count(
                                                    "DEVICE_NAME") > 0)
                                            {
                                                deviceName =
                                                    additional["DEVICE_NAME"];
                                            }
                                        }
                                        if (isMessageRegistry)
                                        {
                                            message_registries::
                                                generateMessageRegistry(
                                                    thisEntry,
                                                    "/redfish/v1/Systems/" +
                                                        std::string(
                                                            BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                                        "/LogServices/"
                                                        "EventLog/Entries/",
                                                    "v1_15_0",
                                                    std::to_string(*id),
                                                    "System Event Log Entry",
                                                    redfish::time_utils::
                                                        getDateTimeStdtime(
                                                            timestamp),
                                                    messageId, messageArgs,
                                                    *resolution, resolved,
                                                    (eventId == nullptr)
                                                        ? ""
                                                        : *eventId,
                                                    deviceName, *severity);

#ifndef BMCWEB_DISABLE_HEALTH_ROLLUP
                                            origin_utils::
                                                convertDbusObjectToOriginOfCondition(
                                                    originOfCondition,
                                                    std::to_string(*id),
                                                    asyncResp, thisEntry,
                                                    deviceName);
                                        }
#endif // BMCWEB_DISABLE_HEALTH_ROLLUP

                                        // generateMessageRegistry
                                        // will not
                                        // create the
                                        // entry if the
                                        // messageId
                                        // can't be
                                        // found in
                                        // message
                                        // registries.
                                        // So check the
                                        // entry 'Id'
                                        // anyway to
                                        // cover that
                                        // case.
                                        if (thisEntry["Id"].size() == 0)
                                        {
                                            thisEntry["@odata.type"] =
                                                "#LogEntry.v1_15_0.LogEntry";
                                            thisEntry["@odata.id"] =
                                                "/redfish/v1/Systems/" +
                                                std::string(
                                                    BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                                "/LogServices/"
                                                "EventLog/"
                                                "Entries/" +
                                                std::to_string(*id);
                                            thisEntry["Name"] =
                                                "System Event Log Entry";
                                            thisEntry["Id"] =
                                                std::to_string(*id);
                                            thisEntry["Message"] = *message;
                                            thisEntry["Resolved"] = resolved;
                                            thisEntry["EntryType"] = "Event";
                                            thisEntry["Severity"] =
                                                translateSeverityDbusToRedfish(
                                                    *severity);
                                            thisEntry["Created"] = redfish::
                                                time_utils::getDateTimeStdtime(
                                                    timestamp);
                                            thisEntry["Modified"] = redfish::
                                                time_utils::getDateTimeStdtime(
                                                    updateTimestamp);
                                        }
                                        if constexpr (
                                            BMCWEB_NVIDIA_OEM_PROPERTIES)
                                        {
                                            if ((eventId != nullptr &&
                                                 !eventId->empty()) ||
                                                !deviceName.empty())
                                            {
                                                nlohmann::json oem = {
                                                    {"Oem",
                                                     {{"Nvidia",
                                                       {{"@odata.type",
                                                         "#NvidiaLogEntry.v1_1_0.NvidiaLogEntry"}}}}}};
                                                if (!deviceName.empty())
                                                {
                                                    oem["Oem"]["Nvidia"]
                                                       ["Device"] = deviceName;
                                                }
                                                if (eventId != nullptr &&
                                                    !eventId->empty())
                                                {
                                                    oem["Oem"]["Nvidia"]
                                                       ["ErrorId"] =
                                                           std::string(
                                                               *eventId);
                                                }
                                                thisEntry.update(oem);
                                            }
                                        }
                                        if (filePath != nullptr)
                                        {
                                            thisEntry["AdditionalDataURI"] =
                                                getLogEntryAdditionalDataURI(
                                                    std::to_string(*id));
                                        }
                                        entriesArray.push_back(thisEntry);
                                        asyncResp->res
                                            .jsonValue["Members@odata.count"] =
                                            entriesArray.size();
                                    }
                                }
                            }
                            std::sort(entriesArray.begin(), entriesArray.end(),
                                      [](const nlohmann::json& left,
                                         const nlohmann::json& right) {
                                return (left["Id"] <= right["Id"]);
                            });
                        },
                            "xyz.openbmc_project.Logging",
                            "/xyz/openbmc_project/logging",
                            "xyz.openbmc_project.Logging.Namespace", "GetAll",
                            chassisName + "_XID",
                            "xyz.openbmc_project.Logging.Namespace.ResolvedFilterType.Both");
                        return;
                    });
                    return;
                }
            }
            messages::resourceNotFound(asyncResp->res,
                                       "#Chassis.v1_15_0.Chassis", chassisId);
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/inventory", 0, interfaces);
    });
}
#endif // BMCWEB_ENABLE_NVIDIA_OEM_LOGSERVICES

// vector containing debug token-related functionalities'
// (GetDebugTokenRequest, GetDebugTokenStatus) output data
static std::vector<std::tuple<std::string, std::string>> debugTokenData;
static constexpr const uint32_t debugTokenTaskTimeoutSec{300};

inline void requestRoutesDebugToken(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/DebugTokenService")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/LogServices/DebugTokenService";
        asyncResp->res.jsonValue["@odata.type"] =
            "#LogService.v1_2_0.LogService";
        asyncResp->res.jsonValue["Name"] = "Debug Token Service";
        asyncResp->res.jsonValue["Description"] = "Debug Token Service";
        asyncResp->res.jsonValue["Id"] = "DebugTokenService";

        std::pair<std::string, std::string> redfishDateTimeOffset =
            redfish::time_utils::getDateTimeOffsetNow();
        asyncResp->res.jsonValue["DateTime"] = redfishDateTimeOffset.first;
        asyncResp->res.jsonValue["DateTimeLocalOffset"] =
            redfishDateTimeOffset.second;
        asyncResp->res.jsonValue["Entries"] = {
            {"@odata.id", "/redfish/v1/Systems/" +
                              std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                              "/LogServices/DebugTokenService/Entries"}};
        asyncResp->res.jsonValue["Actions"] = {
            {"#LogService.CollectDiagnosticData",
             {{"target",
               "/redfish/v1/Systems/" +
                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                   "/LogServices/DebugTokenService/LogService.CollectDiagnosticData"}}}};
    });
}

inline void requestRoutesDebugTokenServiceEntryCollection(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/Systems/<str>/LogServices/DebugTokenService/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        asyncResp->res.jsonValue["@odata.type"] =
            "#LogEntryCollection.LogEntryCollection";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/LogServices/DebugTokenService/Entries";
        asyncResp->res.jsonValue["Name"] = "Debug Token Service Entries";
        asyncResp->res.jsonValue["Description"] =
            "Collection of Debug Token Service Entries";
        asyncResp->res.jsonValue["Members@odata.count"] = debugTokenData.size();

        nlohmann::json& entriesArray = asyncResp->res.jsonValue["Members"];
        entriesArray = nlohmann::json::array();
        auto entryID = 0;
        for (auto& objects : debugTokenData)
        {
            nlohmann::json::object_t thisEntry;

            thisEntry["@odata.type"] = "#LogEntry.v1_15_0.LogEntry";
            thisEntry["@odata.id"] =
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                "/LogServices/DebugTokenService/Entries/" +
                std::to_string(entryID);
            thisEntry["Id"] = std::to_string(entryID);
            thisEntry["EntryType"] = "Oem";
            thisEntry["Name"] = "Debug Token Entry";
            thisEntry["DiagnosticDataType"] = "OEM";
            thisEntry["OEMDiagnosticDataType"] = std::get<0>(objects);
            thisEntry["AdditionalDataSizeBytes"] =
                std::get<1>(objects).length();
            thisEntry["AdditionalDataURI"] =
                "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                "/LogServices/DebugTokenService/Entries/" +
                std::to_string(entryID) + "/attachment";
            entriesArray.push_back(std::move(thisEntry));
            entryID++;
        }
    });
}

inline void requestRoutesDebugTokenServiceEntry(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/DebugTokenService/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& idstr) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::string_view accept = req.getHeaderValue("Accept");
        if (!accept.empty() &&
            !http_helpers::isContentTypeAllowed(
                req.getHeaderValue("Accept"),
                http_helpers::ContentType::OctetStream, true))
        {
            asyncResp->res.result(boost::beast::http::status::bad_request);
            return;
        }

        uint32_t id = static_cast<uint32_t>(stoi(idstr));
        auto dataCount = debugTokenData.size();
        if (dataCount == 0 || id > dataCount - 1)
        {
            messages::resourceMissingAtURI(
                asyncResp->res,
                boost::urls::format(
                    "/redfish/v1/Systems/{}/LogServices/DebugTokenService/Entries/{}",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME, std::to_string(id)));
            asyncResp->res.result(boost::beast::http::status::not_found);
            return;
        }
        asyncResp->res.jsonValue["@odata.type"] = "#LogEntry.v1_15_0.LogEntry";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/LogServices/DebugTokenService/Entries/" + std::to_string(id);
        asyncResp->res.jsonValue["Id"] = std::to_string(id);
        asyncResp->res.jsonValue["EntryType"] = "Oem";
        asyncResp->res.jsonValue["Name"] = "Debug Token Entry";
        asyncResp->res.jsonValue["DiagnosticDataType"] = "OEM";
        asyncResp->res.jsonValue["OEMDiagnosticDataType"] =
            std::get<0>(debugTokenData.at(id));
        asyncResp->res.jsonValue["AdditionalDataSizeBytes"] =
            std::get<1>(debugTokenData.at(id)).length();
        asyncResp->res.jsonValue["AdditionalDataURI"] =
            "/redfish/v1/Systems/" +
            std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
            "/LogServices/DebugTokenService/Entries/" + std::to_string(id) +
            "/attachment";
    });
}

inline void requestRoutesDebugTokenServiceDiagnosticDataCollect(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/DebugTokenService/LogService.CollectDiagnosticData")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::string diagnosticDataType;
        std::string oemDiagnosticDataType;
        if (!redfish::json_util::readJsonAction(
                req, asyncResp->res, "DiagnosticDataType", diagnosticDataType,
                "OEMDiagnosticDataType", oemDiagnosticDataType))
        {
            return;
        }
        if (diagnosticDataType != "OEM")
        {
            BMCWEB_LOG_ERROR("Only OEM DiagnosticDataType supported "
                             "for DebugTokenService");
            messages::actionParameterValueFormatError(
                asyncResp->res, diagnosticDataType, "DiagnosticDataType",
                "CollectDiagnosticData");
            return;
        }

        debug_token::RequestType type;
        if (oemDiagnosticDataType != "DebugTokenStatus")
        {
            if (oemDiagnosticDataType == "GetDebugTokenRequest")
            {
                type = debug_token::RequestType::DebugTokenRequest;
            }
#ifdef BMCWEB_ENABLE_DOT
            else if (oemDiagnosticDataType == "GetDOTCAKUnlockTokenRequest")
            {
                type = debug_token::RequestType::DOTCAKUnlockTokenRequest;
            }
            else if (oemDiagnosticDataType == "GetDOTEnableTokenRequest")
            {
                type = debug_token::RequestType::DOTEnableTokenRequest;
            }
            else if (oemDiagnosticDataType == "GetDOTSignTestToken")
            {
                type = debug_token::RequestType::DOTSignTestToken;
            }
            else if (oemDiagnosticDataType == "GetDOTOverrideTokenRequest")
            {
                type = debug_token::RequestType::DOTOverrideTokenRequest;
            }
#endif
            else
            {
                BMCWEB_LOG_ERROR("Unsupported OEMDiagnosticDataType: {}",
                                 oemDiagnosticDataType);
                messages::actionParameterValueFormatError(
                    asyncResp->res, oemDiagnosticDataType,
                    "OEMDiagnosticDataType", "CollectDiagnosticData");
            }
        }

        static std::unique_ptr<debug_token::OperationHandler> op;
        if (op)
        {
            messages::serviceTemporarilyUnavailable(
                asyncResp->res, std::to_string(debugTokenTaskTimeoutSec));
            return;
        }

        std::shared_ptr<task::TaskData> task = task::TaskData::createTask(
            [](boost::system::error_code ec, sdbusplus::message_t&,
               const std::shared_ptr<task::TaskData>& taskData) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Debug token operation task error: {}",
                                 ec.message());
                if (ec != boost::asio::error::operation_aborted)
                {
                    taskData->messages.emplace_back(
                        messages::resourceErrorsDetectedFormatError(
                            "Debug token task", ec.message()));
                }
                op.reset();
            }
            return op == nullptr ? task::completed : !task::completed;
        },
            "0");

        auto resultHandler =
            [oemDiagnosticDataType,
             task](const std::shared_ptr<std::vector<std::unique_ptr<
                       debug_token::DebugTokenEndpoint>>>& endpoints) {
            std::string result;
            int totalEpCount = 0;
            int validEpCount = 0;
            if (op && endpoints)
            {
                op->getResult(result);
                totalEpCount = static_cast<int>(endpoints->size());
                for (const auto& ep : *endpoints)
                {
                    const auto& state = ep->getState();
                    BMCWEB_LOG_DEBUG(
                        "endpoint object:{} ",
                        sdbusplus::message::object_path(ep->getObject())
                            .filename());
                    BMCWEB_LOG_DEBUG("oemDiagnosticDataType:{}",
                                     oemDiagnosticDataType);
                    if (state == debug_token::EndpointState::RequestAcquired ||
                        state == debug_token::EndpointState::TokenInstalled ||
                        state ==
                            debug_token::EndpointState::DebugTokenUnsupported ||
                        state == debug_token::EndpointState::StatusAcquired)
                    {
                        ++validEpCount;
                    }
                    const auto& objectName = ep->getObject();
                    const auto deviceName =
                        sdbusplus::message::object_path(objectName).filename();
                    std::string stateDesc;
                    switch (state)
                    {
                        case debug_token::EndpointState::DebugTokenUnsupported:
                            task->messages.emplace_back(
                                messages::debugTokenUnsupported(deviceName));
                            break;
                        case debug_token::EndpointState::StatusAcquired:
                            task->messages.emplace_back(
                                messages::debugTokenStatusSuccess(deviceName));
                            break;
                        case debug_token::EndpointState::TokenInstalled:
                            task->messages.emplace_back(
                                messages::debugTokenAlreadyInstalled(
                                    deviceName));
                            break;
                        case debug_token::EndpointState::RequestAcquired:
                            task->messages.emplace_back(
                                messages::debugTokenRequestSuccess(deviceName));
                            break;
                        case debug_token::EndpointState::Error:
                            stateDesc = "Error";
                            task->messages.emplace_back(
                                messages::resourceErrorsDetectedFormatError(
                                    deviceName, stateDesc));
                            break;
                        default:
                            stateDesc = "Invalid state";
                            task->messages.emplace_back(
                                messages::resourceErrorsDetectedFormatError(
                                    objectName, stateDesc));
                            break;
                    }
                }
            }
            if (result.size() != 0)
            {
                debugTokenData.emplace_back(
                    make_tuple(oemDiagnosticDataType, result));
                std::string path = "/redfish/v1/Systems/" +
                                   std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                                   "/LogServices/DebugTokenService/"
                                   "Entries/" +
                                   std::to_string(debugTokenData.size() - 1) +
                                   "/attachment";
                std::string location = "Location: " + path;
                task->payload->httpHeaders.emplace_back(std::move(location));
            }
            if (validEpCount == 0 || totalEpCount == 0)
            {
                task->state = "Stopping";
                task->messages.emplace_back(
                    messages::taskAborted(std::to_string(task->index)));
            }
            else
            {
                if (validEpCount == totalEpCount)
                {
                    task->state = "Completed";
                    task->messages.emplace_back(
                        messages::taskCompletedOK(std::to_string(task->index)));
                }
                else
                {
                    task->state = "Exception";
                    task->messages.emplace_back(messages::taskCompletedWarning(
                        std::to_string(task->index)));
                }
                task->percentComplete = 100 * validEpCount / totalEpCount;
            }
            task->timer.cancel();
            task->finishTask();
            task->sendTaskEvent(task->state, task->index);
            boost::asio::post(crow::connections::systemBus->get_io_context(),
                              [task] { op.reset(); });
        };
        auto errorHandler = [task](bool critical, const std::string& desc,
                                   const std::string& error) {
            task->messages.emplace_back(
                messages::resourceErrorsDetectedFormatError(desc, error));
            if (critical)
            {
                task->state = "Stopping";
                task->messages.emplace_back(
                    messages::taskAborted(std::to_string(task->index)));
                task->timer.cancel();
                task->finishTask();
                task->sendTaskEvent(task->state, task->index);
                boost::asio::post(
                    crow::connections::systemBus->get_io_context(),
                    [] { op.reset(); });
            }
        };

        if (oemDiagnosticDataType == "DebugTokenStatus")
        {
            op = std::make_unique<debug_token::StatusQueryHandler>(
                resultHandler, errorHandler);
        }
        else
        {
            op = std::make_unique<debug_token::RequestHandler>(
                resultHandler, errorHandler, type);
        }
        task->payload.emplace(req);
        task->populateResp(asyncResp->res);
        task->startTimer(std::chrono::seconds(debugTokenTaskTimeoutSec));
    });
}

inline void requestRoutesDebugTokenServiceDiagnosticDataEntryDownload(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/<str>/LogServices/DebugTokenService"
                      "/Entries/<str>/attachment/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName,
                   const std::string& idstr) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        std::string_view accept = req.getHeaderValue("Accept");
        if (!accept.empty() &&
            !http_helpers::isContentTypeAllowed(
                req.getHeaderValue("Accept"),
                http_helpers::ContentType::OctetStream, true))
        {
            asyncResp->res.result(boost::beast::http::status::bad_request);
            return;
        }

        uint32_t id = static_cast<uint32_t>(stoi(idstr));

        auto dataCount = debugTokenData.size();
        if (dataCount == 0 || id > dataCount - 1)
        {
            messages::resourceMissingAtURI(
                asyncResp->res,
                boost::urls::format(
                    "/redfish/v1/Systems/{}/LogServices/DebugTokenService/Entries/{}/attachment",
                    BMCWEB_REDFISH_SYSTEM_URI_NAME, std::to_string(id)));
            asyncResp->res.result(boost::beast::http::status::not_found);
            return;
        }

        asyncResp->res.addHeader("Content-Type", "application/octet-stream");
        asyncResp->res.addHeader("Content-Transfer-Encoding", "Binary");
        std::string data = std::get<1>(debugTokenData[id]);
        asyncResp->res.write(std::move(data));
    });
}

} // namespace redfish
