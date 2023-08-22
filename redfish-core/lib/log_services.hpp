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

#include "debug_token.hpp"
#include "gzfile.hpp"
#include "http_utility.hpp"
#include "human_sort.hpp"
#include "task.hpp"
#include "utils/time_utils.hpp"

#include <systemd/sd-journal.h>
#include <tinyxml2.h>
#include <unistd.h>

#include <app.hpp>
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
#include <dbus_utility.hpp>
#include <error_messages.hpp>
#include <openbmc_dbus_rest.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/dbus_log_utils.hpp>
#include <utils/dbus_utils.hpp>
#include <utils/origin_utils.hpp>
#include <utils/registry_utils.hpp>
#include <utils/time_utils.hpp>

#include <charconv>
#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace redfish
{

constexpr char const* crashdumpObject = "com.intel.crashdump";
constexpr char const* crashdumpPath = "/com/intel/crashdump";
constexpr char const* crashdumpInterface = "com.intel.crashdump";
constexpr char const* deleteAllInterface =
    "xyz.openbmc_project.Collection.DeleteAll";
constexpr char const* crashdumpOnDemandInterface =
    "com.intel.crashdump.OnDemand";
constexpr char const* crashdumpTelemetryInterface =
    "com.intel.crashdump.Telemetry";

enum class DumpCreationProgress
{
    DUMP_CREATE_SUCCESS,
    DUMP_CREATE_FAILED,
    DUMP_CREATE_INPROGRESS
};

namespace registries
{

static const Message*
    getMessageFromRegistry(const std::string& messageKey,
                           const std::span<const MessageEntry> registry)
{
    std::span<const MessageEntry>::iterator messageIt = std::find_if(
        registry.begin(), registry.end(),
        [&messageKey](const MessageEntry& messageEntry) {
            return std::strcmp(messageEntry.first, messageKey.c_str()) == 0;
        });
    if (messageIt != registry.end())
    {
        return &messageIt->second;
    }

    return nullptr;
}

static const Message* getMessage(const std::string_view& messageID)
{
    // Redfish MessageIds are in the form
    // RegistryName.MajorVersion.MinorVersion.MessageKey, so parse it to find
    // the right Message
    std::vector<std::string> fields;
    fields.reserve(4);
    boost::split(fields, messageID, boost::is_any_of("."));
    const std::string& registryName = fields[0];
    const std::string& messageKey = fields[3];

    // Find the right registry and check it for the MessageKey
    if (std::string(base::header.registryPrefix) == registryName)
    {
        return getMessageFromRegistry(
            messageKey, std::span<const MessageEntry>(base::registry));
    }
    if (std::string(openbmc::header.registryPrefix) == registryName)
    {
        return getMessageFromRegistry(
            messageKey, std::span<const MessageEntry>(openbmc::registry));
    }
    if (std::string(resource_event::header.registryPrefix) == registryName)
    {
        return getMessageFromRegistry(
            messageKey,
            std::span<const MessageEntry>(resource_event::registry));
    }
    if (std::string(update_event::header.registryPrefix) == registryName)
    {
        return getMessageFromRegistry(
            messageKey, std::span<const MessageEntry>(update_event::registry));
    }
    if (std::string(bios::header.registryPrefix) == registryName)
    {
        return getMessageFromRegistry(
            messageKey, std::span<const MessageEntry>(bios::registry));
    }
    if (std::string(task_event::header.registryPrefix) == registryName)
    {
        return getMessageFromRegistry(
            messageKey, std::span<const MessageEntry>(task_event::registry));
    }
    return nullptr;
}

} // namespace registries

namespace message_registries
{

static void generateMessageRegistry(
    nlohmann::json& logEntry,
    const std::string& odataId, /* e.g. /redfish/v1/Systems/system/LogServices/"
                                  "EventLog/Entries/ */
    const std::string& odataTypeVer /* e.g. v1_8_0 */, const std::string& id,
    const std::string& name, const std::string& timestamp,
    const std::string& messageId, const std::string& messageArgs,
    const std::string& resolution, const bool& resolved,
    const std::string& severity = "")
{
    BMCWEB_LOG_DEBUG << "Generating MessageRegitry for [" << messageId << "]";
    const registries::Message* msg = registries::getMessage(messageId);

    if (msg == nullptr)
    {
        BMCWEB_LOG_ERROR << "Failed to lookup the message for MessageId["
                         << messageId << "]";
        return;
    }

    // Severity can be overwritten by caller. Using the one defined in the
    // message registries by default.
    std::string sev;
    if (severity.size() == 0)
    {
        sev = msg->severity;
    }
    else
    {
        sev = translateSeverityDbusToRedfish(severity);
    }

    // Convert messageArgs string for its json format used later.
    std::vector<std::string> fields;
    fields.reserve(msg->numberOfArgs);
    boost::split(fields, messageArgs, boost::is_any_of(","));

    // Trim leading and tailing whitespace of each arg.
    for (auto& f : fields)
    {
        boost::trim(f);
    }
    std::span<std::string> msgArgs;
    msgArgs = {&fields[0], fields.size()};

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
                {"Resolution", resolution},
                {"Resolved", resolved}};
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

inline static int getJournalMetadata(sd_journal* journal,
                                     const std::string_view& field,
                                     std::string_view& contents)
{
    const char* data = nullptr;
    size_t length = 0;
    int ret = 0;
    // Get the metadata from the requested field of the journal entry
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const void** dataVoid = reinterpret_cast<const void**>(&data);

    ret = sd_journal_get_data(journal, field.data(), dataVoid, &length);
    if (ret < 0)
    {
        return ret;
    }
    contents = std::string_view(data, length);
    // Only use the content after the "=" character.
    contents.remove_prefix(std::min(contents.find('=') + 1, contents.size()));
    return ret;
}

inline static int getJournalMetadata(sd_journal* journal,
                                     const std::string_view& field,
                                     const int& base, long int& contents)
{
    int ret = 0;
    std::string_view metadata;
    // Get the metadata from the requested field of the journal entry
    ret = getJournalMetadata(journal, field, metadata);
    if (ret < 0)
    {
        return ret;
    }
    contents = strtol(metadata.data(), nullptr, base);
    return ret;
}

// static bool getSkipParam(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
//                          const crow::Request& req, uint64_t& skip)
// {
//     boost::urls::query_params_view::iterator it =
//     req.urlParams.find("$skip"); if (it != req.urlParams.end())
//     {
//         std::string skipParam = it->value();
//         char* ptr = nullptr;
//         skip = std::strtoul(skipParam.c_str(), &ptr, 10);
//         if (skipParam.empty() || *ptr != '\0')
//         {

//             messages::queryParameterValueTypeError(
//                 asyncResp->res, std::string(skipParam), "$skip");
//             return false;
//         }
//     }
//     return true;
// }

// static constexpr const uint64_t maxEntriesPerPage = 1000;
// static bool getTopParam(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
//                         const crow::Request& req, uint64_t& top)
// {
//     boost::urls::query_params_view::iterator it = req.urlParams.find("$top");
//     if (it != req.urlParams.end())
//     {
//         std::string topParam = it->value();
//         char* ptr = nullptr;
//         top = std::strtoul(topParam.c_str(), &ptr, 10);
//         if (topParam.empty() || *ptr != '\0')
//         {
//             messages::queryParameterValueTypeError(
//                 asyncResp->res, std::string(topParam), "$top");
//             return false;
//         }
//         if (top < 1U || top > maxEntriesPerPage)
//         {

//             messages::queryParameterOutOfRange(
//                 asyncResp->res, std::to_string(top), "$top",
//                 "1-" + std::to_string(maxEntriesPerPage));
//             return false;
//         }
//     }
//     return true;
// }

inline static bool getEntryTimestamp(sd_journal* journal,
                                     std::string& entryTimestamp)
{
    int ret = 0;
    uint64_t timestamp = 0;
    ret = sd_journal_get_realtime_usec(journal, &timestamp);
    if (ret < 0)
    {
        BMCWEB_LOG_ERROR << "Failed to read entry timestamp: "
                         << strerror(-ret);
        return false;
    }
    entryTimestamp =
        redfish::time_utils::getDateTimeUint(timestamp / 1000 / 1000);
    return true;
}

inline static bool getUniqueEntryID(sd_journal* journal, std::string& entryID,
                                    const bool firstEntry = true)
{
    int ret = 0;
    static uint64_t prevTs = 0;
    static int index = 0;
    if (firstEntry)
    {
        prevTs = 0;
    }

    // Get the entry timestamp
    uint64_t curTs = 0;
    ret = sd_journal_get_realtime_usec(journal, &curTs);
    if (ret < 0)
    {
        BMCWEB_LOG_ERROR << "Failed to read entry timestamp: "
                         << strerror(-ret);
        return false;
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

static bool getUniqueEntryID(const std::string& logEntry, std::string& entryID,
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

inline static bool getTimestampFromID(const std::string& entryID,
                                      uint64_t& timestamp, uint64_t& index)
{
    if (entryID.empty())
    {
        return false;
    }
    // Convert the unique ID back to a timestamp to find the entry
    std::string_view tsStr(entryID);

    auto underscorePos = tsStr.find('_');
    if (underscorePos != std::string_view::npos)
    {
        // Timestamp has an index
        tsStr.remove_suffix(tsStr.size() - underscorePos);
        std::string_view indexStr(entryID);
        indexStr.remove_prefix(underscorePos + 1);
        auto [ptr, ec] = std::from_chars(
            indexStr.data(), indexStr.data() + indexStr.size(), index);
        if (ec != std::errc())
        {
            return false;
        }
    }
    // Timestamp has no index
    auto [ptr, ec] =
        std::from_chars(tsStr.data(), tsStr.data() + tsStr.size(), timestamp);
    if (ec != std::errc())
    {
        return false;
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
    std::sort(redfishLogFiles.begin(), redfishLogFiles.end());

    return !redfishLogFiles.empty();
}

std::vector<std::pair<std::string, std::string>>
    parseOEMAdditionalData(const std::string& oemData)
{
    // Parse OEM data for encoded format string
    // oemDiagnosticDataType = "key1=value1;key2=value2;key3=value3"
    std::vector<std::pair<std::string, std::string>> additionalData;
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

void deleteDbusLogEntry(const std::string& entryId,
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

void deleteDbusSELEntry(std::string& entryID,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
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
                BMCWEB_LOG_ERROR << "SELLogEntry (DBus) "
                                    "resp_handler got error "
                                 << ec;
                messages::internalError(asyncResp->res);
                return;
            }
            uint32_t* id = nullptr;
            std::string* message = nullptr;

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
            }
            if (id == nullptr || message == nullptr)
            {
                messages::internalError(asyncResp->res);
                return;
            }
            if (*message == "xyz.openbmc_project.Logging.SEL.Error.Created")
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

inline void parseDumpEntryFromDbusObject(
    const dbus::utility::ManagedObjectType::value_type &object,
    std::string &dumpStatus, uint64_t &size, uint64_t &timestampUs,
    std::string &faultLogDiagnosticDataType,
    std::string &sectionType, std::string &fruid, std::string &severity,
    std::string &nvipSignature, std::string &nvSeverity,
    std::string &nvSocketNumber, std::string &pcieVendorID,
    std::string &pcieDeviceID, std::string &pcieClassCode,
    std::string &pcieFunctionNumber, std::string &pcieDeviceNumber,
    std::string &pcieSegmentNumber, std::string &pcieDeviceBusNumber,
    std::string &pcieSecondaryBusNumber, std::string &pcieSlotNumber,
    const std::shared_ptr<bmcweb::AsyncResp> &asyncResp)
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
                    "xyz.openbmc_project.Dump.Entry.FaultLog.FaultDataType.CPER")
            {
                if (additionalTypeName != nullptr)
                {
                    faultLogDiagnosticDataType = *additionalTypeName;
                }
            }
        }
        else if (interfaceMap.first == "xyz.openbmc_project.Dump.Entry.CPERDecode")
        {
            const std::string *sectionTypePtr = nullptr;
            const std::string *fruidPtr = nullptr;
            const std::string *severityPtr = nullptr;
            const std::string *nvipSignaturePtr = nullptr;
            const std::string *nvSeverityPtr = nullptr;
            const std::string *nvSocketNumberPtr = nullptr;
            const std::string *pcieVendorIDPtr = nullptr;
            const std::string *pcieDeviceIDPtr = nullptr;
            const std::string *pcieClassCodePtr = nullptr;
            const std::string *pcieFunctionNumberPtr = nullptr;
            const std::string *pcieDeviceNumberPtr = nullptr;
            const std::string *pcieSegmentNumberPtr = nullptr;
            const std::string *pcieDeviceBusNumberPtr = nullptr;
            const std::string *pcieSecondaryBusNumberPtr = nullptr;
            const std::string *pcieSlotNumberPtr = nullptr;

            for (auto &propertyMap : interfaceMap.second)
            {
                if (propertyMap.first == "FRU_ID")
                {
                    fruidPtr = std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "NV_IPSignature")
                {
                    nvipSignaturePtr = std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "NV_Severity")
                {
                    nvSeverityPtr = std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "NV_Socket_Number")
                {
                    nvSocketNumberPtr = std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "PCIE_Class_Code")
                {
                    pcieClassCodePtr = std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "PCIE_Device_Bus_Number")
                {
                    pcieDeviceBusNumberPtr = std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "PCIE_Device_ID")
                {
                    pcieDeviceIDPtr = std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "PCIE_Device_Number")
                {
                    pcieDeviceNumberPtr = std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "PCIE_Function_Number")
                {
                    pcieFunctionNumberPtr = std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "PCIE_Secondary_Bus_Number")
                {
                    pcieSecondaryBusNumberPtr = std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "PCIE_Segment_Number")
                {
                    pcieSegmentNumberPtr = std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "PCIE_Slot_Number")
                {
                    pcieSlotNumberPtr = std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "PCIE_Vendor_ID")
                {
                    pcieVendorIDPtr = std::get_if<std::string>(&propertyMap.second);
                }
                else if (propertyMap.first == "Section_Type")
                {
                    sectionTypePtr = std::get_if<std::string>(&propertyMap.second);
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
    }
}

static std::string getDumpEntriesPath(const std::string& dumpType)
{
    std::string entriesPath;

    if (dumpType == "BMC")
    {
        entriesPath =
            "/redfish/v1/Managers/" PLATFORMBMCID "/LogServices/Dump/Entries/";
    }
    else if (dumpType == "System")
    {
        entriesPath = "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/Dump/Entries/";
    }
    else if (dumpType == "FaultLog")
    {
        entriesPath = "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/FaultLog/Entries/";
    }
    else
    {
        BMCWEB_LOG_ERROR << "getDumpEntriesPath() invalid dump type: "
                         << dumpType;
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

    crow::connections::systemBus->async_method_call(
        [asyncResp, entriesPath,
         dumpType](const boost::system::error_code ec,
                   dbus::utility::ManagedObjectType& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "DumpEntry resp_handler got error " << ec;
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
            asyncResp->res.jsonValue["Description"] =
                "Collection of " + dumpType + " Dump Entries";

            nlohmann::json& entriesArray = asyncResp->res.jsonValue["Members"];
            entriesArray = nlohmann::json::array();
            std::string dumpEntryPath =
                "/xyz/openbmc_project/dump/" +
                std::string(boost::algorithm::to_lower_copy(dumpType)) +
                "/entry/";

            std::sort(resp.begin(), resp.end(),
                      [](const auto& l, const auto& r) {
                          return AlphanumLess<std::string>()(
                              l.first.filename(), r.first.filename());
                      });

            for (auto& object : resp)
            {
                if (object.first.str.find(dumpEntryPath) == std::string::npos)
                {
                    continue;
                }
                uint64_t timestampS = 0;
                uint64_t size = 0;
                std::string dumpStatus;
                nlohmann::json::object_t thisEntry;
                std::string faultLogDiagnosticDataType;
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
                    object, dumpStatus, size, timestampS,
                    faultLogDiagnosticDataType,
                    sectionType, fruid, severity, nvipSignature, nvSeverity,
                    nvSocketNumber, pcieVendorID, pcieDeviceID, pcieClassCode,
                    pcieFunctionNumber, pcieDeviceNumber, pcieSegmentNumber,
                    pcieDeviceBusNumber, pcieSecondaryBusNumber, pcieSlotNumber,
                    asyncResp);

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

                if (dumpType == "BMC")
                {
                    thisEntry["Created"] =
                        redfish::time_utils::getDateTimeUint(timestampS);
                    thisEntry["DiagnosticDataType"] = "Manager";
                    thisEntry["AdditionalDataURI"] =
                        entriesPath + entryID + "/attachment";
                    thisEntry["AdditionalDataSizeBytes"] = size;
                }
                else if (dumpType == "System")
                {
                    thisEntry["Created"] =
                        redfish::time_utils::getDateTimeUint(timestampS);
                    thisEntry["DiagnosticDataType"] = "OEM";
                    thisEntry["OEMDiagnosticDataType"] = "System";
                    thisEntry["AdditionalDataURI"] =
                        entriesPath + entryID + "/attachment";
                    thisEntry["AdditionalDataSizeBytes"] = size;
                }
                else if (dumpType == "FaultLog")
                {
                    thisEntry["Created"] =
                        redfish::time_utils::getDateTimeUint(timestampS);
                    thisEntry["DiagnosticDataType"] =
                        faultLogDiagnosticDataType;
                    thisEntry["AdditionalDataURI"] =
                        entriesPath + entryID + "/attachment";
                    thisEntry["AdditionalDataSizeBytes"] = size;
                    thisEntry["Severity"] = severity;
                    // CPER Properties
                    if (sectionType != "NA") {
                    thisEntry["CPER"]["Oem"]["SectionType"] = sectionType;
                    }
                    if (fruid != "NA") {
                    thisEntry["CPER"]["Oem"]["FruID"] = fruid;
                    }
                    if (severity != "NA") {
                    thisEntry["CPER"]["Oem"]["Severity"] = severity;
                    }
                    if (nvipSignature != "NA") {
                    thisEntry["CPER"]["Oem"]["NvIpSignature"] = nvipSignature;
                    }
                    if (nvSeverity != "NA") {
                    thisEntry["CPER"]["Oem"]["NvSeverity"] = nvSeverity;
                    }
                    if (nvSocketNumber != "NA") {
                    thisEntry["CPER"]["Oem"]["NvSocketNumber"] = nvSocketNumber;
                    }
                    if (pcieVendorID != "NA") {
                    thisEntry["CPER"]["Oem"]["PcieVendorId"] = pcieVendorID;
                    }
                    if (pcieDeviceID != "NA") {
                    thisEntry["CPER"]["Oem"]["PcieDeviceId"] = pcieDeviceID;
                    }
                    if (pcieClassCode != "NA") {
                    thisEntry["CPER"]["Oem"]["PcieClassCode"] = pcieClassCode;
                    }
                    if (pcieFunctionNumber != "NA") {
                    thisEntry["CPER"]["Oem"]["PcieFunctionNumber"] = pcieFunctionNumber;
                    }
                    if (pcieDeviceNumber != "NA") {
                    thisEntry["CPER"]["Oem"]["PcieDeviceNumber"] = pcieDeviceNumber;
                    }
                    if (pcieSegmentNumber != "NA") {
                    thisEntry["CPER"]["Oem"]["PcieSegmentNumber"] = pcieSegmentNumber;
                    }
                    if (pcieDeviceBusNumber != "NA") {
                    thisEntry["CPER"]["Oem"]["PcieDeviceBusNumber"] = pcieDeviceBusNumber;
                    }
                    if (pcieSecondaryBusNumber != "NA") {
                    thisEntry["CPER"]["Oem"]["PcieSecondaryBusNumber"] = pcieSecondaryBusNumber;
                    }
                    if (pcieSlotNumber != "NA") {
                    thisEntry["CPER"]["Oem"]["PcieSlotNumber"] = pcieSlotNumber;
                    }

                }
                entriesArray.push_back(std::move(thisEntry));
            }
            asyncResp->res.jsonValue["Members@odata.count"] =
                entriesArray.size();
        },
        "xyz.openbmc_project.Dump.Manager", "/xyz/openbmc_project/dump",
        "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
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

    crow::connections::systemBus->async_method_call(
        [asyncResp, entryID, dumpType,
         entriesPath](const boost::system::error_code ec,
                      const dbus::utility::ManagedObjectType& resp) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "DumpEntry resp_handler got error " << ec;
                messages::internalError(asyncResp->res);
                return;
            }

            bool foundDumpEntry = false;
            std::string dumpEntryPath =
                "/xyz/openbmc_project/dump/" +
                std::string(boost::algorithm::to_lower_copy(dumpType)) +
                "/entry/";

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

                parseDumpEntryFromDbusObject(
                    objectPath, dumpStatus, size, timestampUs,
                    faultLogDiagnosticDataType,
                    sectionType, fruid, severity, nvipSignature, nvSeverity,
                    nvSocketNumber, pcieVendorID, pcieDeviceID, pcieClassCode,
                    pcieFunctionNumber, pcieDeviceNumber, pcieSegmentNumber,
                    pcieDeviceBusNumber, pcieSecondaryBusNumber, pcieSlotNumber,
                    asyncResp);

                if (dumpStatus !=
                        "xyz.openbmc_project.Common.Progress.OperationStatus.Completed" &&
                    !dumpStatus.empty())
                {
                    // Dump status is not Complete
                    // return not found until status is changed to Completed
                    messages::resourceNotFound(asyncResp->res,
                                               dumpType + " dump", entryID);
                    return;
                }

                asyncResp->res.jsonValue["@odata.type"] =
                    "#LogEntry.v1_15_0.LogEntry";
                asyncResp->res.jsonValue["@odata.id"] = entriesPath + entryID;
                asyncResp->res.jsonValue["Id"] = entryID;
                asyncResp->res.jsonValue["EntryType"] = "Event";
                asyncResp->res.jsonValue["Name"] = dumpType + " Dump Entry";

                asyncResp->res.jsonValue["AdditionalDataSizeBytes"] = size;

                if (dumpType == "BMC")
                {
                    asyncResp->res.jsonValue["DiagnosticDataType"] = "Manager";
                    asyncResp->res.jsonValue["AdditionalDataURI"] =
                        "/redfish/v1/Managers/" PLATFORMBMCID
                        "/LogServices/Dump/Entries/" +
                        entryID + "/attachment";
                }
                else if (dumpType == "System")
                {
                    asyncResp->res.jsonValue["DiagnosticDataType"] = "OEM";
                    asyncResp->res.jsonValue["OEMDiagnosticDataType"] =
                        "System";
                    asyncResp->res.jsonValue["AdditionalDataURI"] =
                        "/redfish/v1/Systems/" PLATFORMSYSTEMID
                        "/LogServices/Dump/Entries/" +
                        entryID + "/attachment";
                }
                else if (dumpType == "FaultLog")
                {
                    asyncResp->res.jsonValue["DiagnosticDataType"] =
                        faultLogDiagnosticDataType;
                    asyncResp->res.jsonValue["AdditionalDataURI"] =
                        "/redfish/v1/Systems/" PLATFORMSYSTEMID
                        "/LogServices/FaultLog/Entries/" +
                        entryID + "/attachment";
                    if (sectionType != "NA") {
                        asyncResp->res.jsonValue["CPER"]["Oem"]["SectionType"] = sectionType;
                    }
                    if (fruid != "NA") {
                        asyncResp->res.jsonValue["CPER"]["Oem"]["FruID"] = fruid;
                    }
                    if (severity != "NA") {
                        asyncResp->res.jsonValue["CPER"]["Oem"]["Severity"] = severity;
                    }
                    if (nvipSignature != "NA") {
                        asyncResp->res.jsonValue["CPER"]["Oem"]["NvIpSignature"] = nvipSignature;
                    }
                    if (nvSeverity != "NA") {
                        asyncResp->res.jsonValue["CPER"]["Oem"]["NvSeverity"] = nvSeverity;
                    }
                    if (nvSocketNumber != "NA") {
                        asyncResp->res.jsonValue["CPER"]["Oem"]["NvSocketNumber"] = nvSocketNumber;
                    }
                    if (pcieVendorID != "NA") {
                        asyncResp->res.jsonValue["CPER"]["Oem"]["PcieVendorId"] = pcieVendorID;
                    }
                    if (pcieDeviceID != "NA") {
                        asyncResp->res.jsonValue["CPER"]["Oem"]["PcieDeviceId"] = pcieDeviceID;
                    }
                    if (pcieClassCode != "NA") {
                        asyncResp->res.jsonValue["CPER"]["Oem"]["PcieClassCode"] = pcieClassCode;
                    }
                    if (pcieFunctionNumber != "NA") {
                        asyncResp->res.jsonValue["CPER"]["Oem"]["PcieFunctionNumber"] = pcieFunctionNumber;
                    }
                    if (pcieDeviceNumber != "NA") {
                        asyncResp->res.jsonValue["CPER"]["Oem"]["PcieDeviceNumber"] = pcieDeviceNumber;
                    }
                    if (pcieSegmentNumber != "NA") {
                        asyncResp->res.jsonValue["CPER"]["Oem"]["PcieSegmentNumber"] = pcieSegmentNumber;
                    }
                    if (pcieDeviceBusNumber != "NA") {
                        asyncResp->res.jsonValue["CPER"]["Oem"]["PcieDeviceBusNumber"] = pcieDeviceBusNumber;
                    }
                    if (pcieSecondaryBusNumber != "NA") {
                        asyncResp->res.jsonValue["CPER"]["Oem"]["PcieSecondaryBusNumber"] = pcieSecondaryBusNumber;
                    }
                    if (pcieSlotNumber != "NA") {
                        asyncResp->res.jsonValue["CPER"]["Oem"]["PcieSlotNumber"] = pcieSlotNumber;
                    }

                }
            }

            if (!foundDumpEntry)
            {
                BMCWEB_LOG_ERROR << "Can't find Dump Entry";
                messages::resourceNotFound(asyncResp->res, dumpType + " dump",
                                           entryID);
                return;
            }
        },
        "xyz.openbmc_project.Dump.Manager", "/xyz/openbmc_project/dump",
        "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
}

inline void deleteDumpEntry(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& entryID,
                            const std::string& dumpType)
{
    auto respHandler = [asyncResp,
                        entryID](const boost::system::error_code ec) {
        BMCWEB_LOG_DEBUG << "Dump Entry doDelete callback: Done";
        if (ec)
        {
            if (ec.value() == EBADR)
            {
                messages::resourceNotFound(asyncResp->res, "LogEntry", entryID);
                return;
            }
            BMCWEB_LOG_ERROR << "Dump (DBus) doDelete respHandler got error "
                             << ec << " entryID=" << entryID;
            messages::internalError(asyncResp->res);
            return;
        }
    };
    crow::connections::systemBus->async_method_call(
        respHandler, "xyz.openbmc_project.Dump.Manager",
        "/xyz/openbmc_project/dump/" +
            std::string(boost::algorithm::to_lower_copy(dumpType)) + "/entry/" +
            entryID,
        "xyz.openbmc_project.Object.Delete", "Delete");
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
                BMCWEB_LOG_ERROR << "Status property value is null";
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
        return "/redfish/v1/Managers/" PLATFORMBMCID
               "/LogServices/Dump/Entries/";
    }
    if (dumpPath == "/xyz/openbmc_project/dump/system/entry")
    {
        return "/redfish/v1/Systems/" PLATFORMSYSTEMID
               "/LogServices/Dump/Entries/";
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
        BMCWEB_LOG_ERROR << "Invalid dump type received";
        messages::internalError(asyncResp->res);
        return;
    }

    crow::connections::systemBus->async_method_call(
        [asyncResp, payload, createdObjPath,
         dumpEntryPath{std::move(dumpEntryPath)},
         dumpId](const boost::system::error_code ec,
                 const std::string& introspectXml) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "Introspect call failed with error: "
                                 << ec.message();
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
                BMCWEB_LOG_ERROR << "XML document failed to parse";
                messages::internalError(asyncResp->res);
                return;
            }
            tinyxml2::XMLElement* interfaceNode =
                pRoot->FirstChildElement("interface");

            bool isProgressIntfPresent = false;
            while (interfaceNode != nullptr)
            {
                const char* thisInterfaceName =
                    interfaceNode->Attribute("name");
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
                    boost::system::error_code err,
                    sdbusplus::message::message& msg,
                    const std::shared_ptr<task::TaskData>& taskData) {
                    if (err)
                    {
                        BMCWEB_LOG_ERROR << createdObjPath.str
                                         << ": Error in creating dump";
                        taskData->messages.emplace_back(
                            messages::internalError());
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
                        if (dumpStatus ==
                            DumpCreationProgress::DUMP_CREATE_FAILED)
                        {
                            BMCWEB_LOG_ERROR << createdObjPath.str
                                             << ": Error in creating dump";
                            taskData->state = "Cancelled";
                            nlohmann::json retMessage =
                                messages::operationFailed();
                            taskData->messages.emplace_back(retMessage);
                            return task::completed;
                        }

                        if (dumpStatus ==
                            DumpCreationProgress::DUMP_CREATE_INPROGRESS)
                        {
                            BMCWEB_LOG_DEBUG
                                << createdObjPath.str
                                << ": Dump creation task is in progress";

                            auto valuesIter = std::find_if(
                                values.begin(), values.end(),
                                [](const std::pair<
                                    std::string,
                                    dbus::utility::DbusVariantType>& property)
                                    -> bool {
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

                    std::string headerLoc = "Location: " + dumpEntryPath +
                                            http_helpers::urlEncode(dumpId);
                    taskData->payload->httpHeaders.emplace_back(
                        std::move(headerLoc));

                    BMCWEB_LOG_DEBUG << createdObjPath.str
                                     << ": Dump creation task completed";
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
    std::vector<std::pair<std::string, std::string>> createDumpParamVec;

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
            BMCWEB_LOG_ERROR
                << "CreateDump action parameter 'DiagnosticDataType'/'OEMDiagnosticDataType' value not found!";
            messages::actionParameterMissing(
                asyncResp->res, "CollectDiagnosticData",
                "DiagnosticDataType & OEMDiagnosticDataType");
            return;
        }

        if (*diagnosticDataType != "OEM")
        {
            BMCWEB_LOG_ERROR << "Wrong parameter values passed";
            messages::actionParameterValueError(asyncResp->res, "DiagnosticDataType",
                                                "LogService.CollectDiagnosticData");
            return;
        }
        dumpPath = "/redfish/v1/Systems/" PLATFORMSYSTEMID "/LogServices/Dump/";
    }
    else if (dumpType == "BMC")
    {
        if (!diagnosticDataType)
        {
            BMCWEB_LOG_ERROR
                << "CreateDump action parameter 'DiagnosticDataType' not found!";
            messages::actionParameterMissing(
                asyncResp->res, "CollectDiagnosticData", "DiagnosticDataType");
            return;
        }
        if (*diagnosticDataType != "Manager")
        {
            BMCWEB_LOG_ERROR
                << "Wrong parameter value passed for 'DiagnosticDataType'";
            messages::actionParameterValueError(asyncResp->res, "DiagnosticDataType",
                                                "LogService.CollectDiagnosticData");
            return;
        }
        dumpPath = "/redfish/v1/Managers/" PLATFORMBMCID "/LogServices/Dump/";
    }
    else
    {
        BMCWEB_LOG_ERROR << "CreateDump failed. Unknown dump type";
        messages::internalError(asyncResp->res);
        return;
    }

    // std::vector<std::pair<std::string, std::variant<std::string, uint64_t>>>
    //     createDumpParamVec;

    crow::connections::systemBus->async_method_call(
        [asyncResp, payload(task::Payload(req)), dumpPath,
         oemDiagnosticDataType](
            const boost::system::error_code ec,
            const sdbusplus::message::message& msg,
            const sdbusplus::message::object_path& objPath) mutable {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "CreateDump resp_handler got error " << ec;
                const sd_bus_error* dbusError = msg.get_error();
                if (dbusError == nullptr)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }

                BMCWEB_LOG_ERROR << "CreateDump DBus error: " << dbusError->name
                                 << " and error msg: " << dbusError->message;

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
                    messages::propertyValueIncorrect(asyncResp->res,
                                                     "DiagnosticType",
                                                     *oemDiagnosticDataType);
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
            BMCWEB_LOG_DEBUG << "Dump Created. Path: " << objPath.str;
            createDumpTaskCallback(std::move(payload), asyncResp, objPath);
        },
        "xyz.openbmc_project.Dump.Manager",
        "/xyz/openbmc_project/dump/" +
            std::string(boost::algorithm::to_lower_copy(dumpType)),
        "xyz.openbmc_project.Dump.Create", "CreateDump", createDumpParamVec);
}

inline void clearDump(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& dumpType)
{
    std::string dumpTypeLowerCopy =
        std::string(boost::algorithm::to_lower_copy(dumpType));

    crow::connections::systemBus->async_method_call(
        [asyncResp, dumpType](
            const boost::system::error_code ec,
            const dbus::utility::MapperGetSubTreePathsResponse& subTreePaths) {
            if (ec)
            {
                BMCWEB_LOG_ERROR << "resp_handler got error " << ec;
                messages::internalError(asyncResp->res);
                return;
            }

            for (const std::string& path : subTreePaths)
            {
                sdbusplus::message::object_path objPath(path);
                std::string logID = objPath.filename();
                if (logID.empty())
                {
                    continue;
                }
                deleteDumpEntry(asyncResp, logID, dumpType);
            }
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/dump/" + dumpTypeLowerCopy, 0,
        std::array<std::string, 1>{"xyz.openbmc_project.Dump.Entry." +
                                   dumpType});
}

inline static void
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

constexpr char const* postCodeIface = "xyz.openbmc_project.State.Boot.PostCode";
inline void requestRoutesSystemLogServiceCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID "/LogServices/")
        .privileges(redfish::privileges::getLogServiceCollection)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)

            {
                // Collections don't include the static data added by SubRoute
                // because it has a duplicate entry for members
                asyncResp->res.jsonValue["@odata.type"] =
                    "#LogServiceCollection.LogServiceCollection";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Systems/" PLATFORMSYSTEMID "/LogServices";
                asyncResp->res.jsonValue["Name"] =
                    "System Log Services Collection";
                asyncResp->res.jsonValue["Description"] =
                    "Collection of LogServices for this Computer System";
                nlohmann::json& logServiceArray =
                    asyncResp->res.jsonValue["Members"];
                logServiceArray = nlohmann::json::array();
                nlohmann::json::object_t eventLog;
                eventLog["@odata.id"] = "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                        "/LogServices/EventLog";
                logServiceArray.push_back(std::move(eventLog));
#ifdef BMCWEB_ENABLE_HOST_OS_FEATURE
                nlohmann::json::object_t selLog;
                selLog["@odata.id"] =
                    "/redfish/v1/Systems/" PLATFORMSYSTEMID "/LogServices/SEL";
                logServiceArray.push_back(std::move(selLog));
#endif
#ifdef BMCWEB_ENABLE_REDFISH_DUMP_LOG
                nlohmann::json::object_t dumpLog;
                dumpLog["@odata.id"] =
                    "/redfish/v1/Systems/" PLATFORMSYSTEMID "/LogServices/Dump";
                logServiceArray.push_back(std::move(dumpLog));
#endif

#ifdef BMCWEB_ENABLE_REDFISH_SYSTEM_FAULTLOG_DUMP_LOG
                nlohmann::json::object_t faultLog;
                faultLog["@odata.id"] = "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                        "/LogServices/FaultLog";
                logServiceArray.push_back(std::move(faultLog));
#endif // BMCWEB_ENABLE_REDFISH_SYSTEM_FAULTLOG_DUMP_LOG

#ifdef BMCWEB_ENABLE_REDFISH_CPU_LOG
                nlohmann::json::object_t crashdump;
                crashdump["@odata.id"] = "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                         "/LogServices/Crashdump";
                logServiceArray.push_back(std::move(crashdump));
#endif

#ifdef BMCWEB_ENABLE_REDFISH_HOST_LOGGER
                nlohmann::json::object_t hostlogger;
                hostlogger["@odata.id"] =
                    "/redfish/v1/Systems/" PLATFORMSYSTEMID
                    "/LogServices/HostLogger";
                logServiceArray.push_back(std::move(hostlogger));
#endif
                nlohmann::json::object_t debugToken;
                debugToken["@odata.id"] =
                    "/redfish/v1/Systems/" PLATFORMSYSTEMID
                    "/LogServices/DebugTokenService";
                logServiceArray.push_back(std::move(debugToken));

                asyncResp->res.jsonValue["Members@odata.count"] =
                    logServiceArray.size();

                crow::connections::systemBus->async_method_call(
                    [asyncResp](
                        const boost::system::error_code ec,
                        const dbus::utility::MapperGetSubTreePathsResponse&
                            subtreePath) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR << ec;
                            return;
                        }

                        for (const auto& pathStr : subtreePath)
                        {
                            if (pathStr.find("PostCode") != std::string::npos)
                            {
                                nlohmann::json& logServiceArrayLocal =
                                    asyncResp->res.jsonValue["Members"];
                                logServiceArrayLocal.push_back(
                                    {{"@odata.id",
                                      "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                      "/LogServices/PostCodes"}});
                                asyncResp->res
                                    .jsonValue["Members@odata.count"] =
                                    logServiceArrayLocal.size();
                                return;
                            }
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths", "/",
                    0, std::array<const char*, 1>{postCodeIface});
            });
}

inline void requestRoutesEventLogService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/EventLog/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)([&app](const crow::Request& req,
                                                       const std::shared_ptr<
                                                           bmcweb::AsyncResp>&
                                                           asyncResp) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/Systems/" PLATFORMSYSTEMID "/LogServices/EventLog";
            asyncResp->res.jsonValue["@odata.type"] =
                "#LogService.v1_1_0.LogService";
            asyncResp->res.jsonValue["Name"] = "Event Log Service";
            asyncResp->res.jsonValue["Description"] =
                "System Event Log Service";
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
                        BMCWEB_LOG_ERROR
                            << "Failed to get Data from xyz.openbmc_project.Logging GetStats: "
                            << ec;
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    auto lastTimeStamp =
                        redfish::time_utils::getTimestamp(std::get<1>(reqData));
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                        "#NvidiaLogService.v1_0_0.NvidiaLogService";
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["LatestEntryID"] =
                        std::to_string(std::get<0>(reqData));
                    asyncResp->res
                        .jsonValue["Oem"]["Nvidia"]["LatestEntryTimeStamp"] =
                        redfish::time_utils::getDateTimeStdtime(lastTimeStamp);
                },
                "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
                "xyz.openbmc_project.Logging.Namespace", "GetStats", "all");

            asyncResp->res.jsonValue["Entries"] = {
                {"@odata.id", "/redfish/v1/Systems/" PLATFORMSYSTEMID
                              "/LogServices/EventLog/Entries"}};
            asyncResp->res.jsonValue["Actions"]["#LogService.ClearLog"] = {

                {"target",
                 "/redfish/v1/Systems/" PLATFORMSYSTEMID
                 "/LogServices/EventLog/Actions/LogService.ClearLog"}};
        });
}

inline void requestRoutesSELLogService(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/" PLATFORMSYSTEMID "/LogServices/SEL/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Systems/" PLATFORMSYSTEMID "/LogServices/SEL";
                asyncResp->res.jsonValue["@odata.type"] =
                    "#LogService.v1_1_0.LogService";
                asyncResp->res.jsonValue["Name"] = "SEL Log Service";
                asyncResp->res.jsonValue["Description"] = "IPMI SEL Service";
                asyncResp->res.jsonValue["Id"] = "SEL";
                asyncResp->res.jsonValue["OverWritePolicy"] = "WrapsWhenFull";

                std::pair<std::string, std::string> redfishDateTimeOffset =
                    redfish::time_utils::getDateTimeOffsetNow();

                asyncResp->res.jsonValue["DateTime"] =
                    redfishDateTimeOffset.first;
                asyncResp->res.jsonValue["DateTimeLocalOffset"] =
                    redfishDateTimeOffset.second;

                asyncResp->res.jsonValue["Entries"] = {
                    {"@odata.id", "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                  "/LogServices/SEL/Entries"}};
                asyncResp->res.jsonValue["Actions"]["#LogService.ClearLog"] = {

                    {"target",
                     "/redfish/v1/Systems/" PLATFORMSYSTEMID "/LogServices/SEL/"
                     "Actions/LogService.ClearLog"}};
            });
}

inline void requestRoutesJournalEventLogClear(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/EventLog/Actions/"
                      "LogService.ClearLog/")
        .privileges({{"ConfigureComponents"}})
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
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
                    [asyncResp](const boost::system::error_code ec) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR << "Failed to reload rsyslog: "
                                             << ec;
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        messages::success(asyncResp->res);
                    },
                    "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                    "org.freedesktop.systemd1.Manager", "ReloadUnit",
                    "rsyslog.service", "replace");
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
    boost::split(logEntryFields, entry, boost::is_any_of(","),
                 boost::token_compress_on);
    // We need at least a MessageId to be valid
    if (logEntryFields.empty())
    {
        return LogParseError::parseFailed;
    }
    std::string& messageID = logEntryFields[0];

    // Get the Message from the MessageRegistry
    const registries::Message* message = registries::getMessage(messageID);

    if (message == nullptr)
    {
        BMCWEB_LOG_WARNING << "Log entry not found in registry: " << logEntry;
        return LogParseError::messageIdNotInRegistry;
    }

    std::string msg = message->message;

    // Get the MessageArgs from the log if there are any
    std::span<std::string> messageArgs;
    if (logEntryFields.size() > 1)
    {
        std::string& messageArgsStart = logEntryFields[1];
        // If the first string is empty, assume there are no MessageArgs
        std::size_t messageArgsSize = 0;
        if (!messageArgsStart.empty())
        {
            messageArgsSize = logEntryFields.size() - 1;
        }

        messageArgs = {&messageArgsStart, messageArgsSize};

        // Fill the MessageArgs into the Message
        int i = 0;
        for (const std::string& messageArg : messageArgs)
        {
            std::string argStr = "%" + std::to_string(++i);
            size_t argPos = msg.find(argStr);
            if (argPos != std::string::npos)
            {
                msg.replace(argPos, argStr.length(), messageArg);
            }
        }
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
    logEntryJson["@odata.type"] = "#LogEntry.v1_9_0.LogEntry";
    logEntryJson["@odata.id"] = "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                "/LogServices/EventLog/Entries/" +
                                logEntryID;
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
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/EventLog/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
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

                size_t top =
                    delegatedQuery.top.value_or(query_param::Query::maxTop);
                size_t skip = delegatedQuery.skip.value_or(0);

                // Collections don't include the static data added by SubRoute
                // because it has a duplicate entry for members
                asyncResp->res.jsonValue["@odata.type"] =
                    "#LogEntryCollection.LogEntryCollection";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Systems/" PLATFORMSYSTEMID
                    "/LogServices/EventLog/Entries";
                asyncResp->res.jsonValue["Name"] = "System Event Log Entries";
                asyncResp->res.jsonValue["Description"] =
                    "Collection of System Event Log Entries";

                nlohmann::json& logEntryArray =
                    asyncResp->res.jsonValue["Members"];
                logEntryArray = nlohmann::json::array();
                // Go through the log files and create a unique ID for each
                // entry
                std::vector<std::filesystem::path> redfishLogFiles;
                getRedfishLogFiles(redfishLogFiles);
                uint64_t entryCount = 0;
                std::string logEntry;

                // Oldest logs are in the last file, so start there and loop
                // backwards
                for (auto it = redfishLogFiles.rbegin();
                     it < redfishLogFiles.rend(); it++)
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
                        LogParseError status =
                            fillEventLogEntryJson(idStr, logEntry, bmcLogEntry);
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

                        logEntryArray.push_back(std::move(bmcLogEntry));
                    }
                }
                asyncResp->res.jsonValue["Members@odata.count"] = entryCount;
                if (skip + top < entryCount)
                {
                    asyncResp->res.jsonValue["Members@odata.nextLink"] =
                        "/redfish/v1/Systems/" PLATFORMSYSTEMID
                        "/LogServices/EventLog/Entries?$skip=" +
                        std::to_string(skip + top);
                }
            });
}

inline void requestRoutesJournalEventLogEntry(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/EventLog/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& param) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
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
                for (auto it = redfishLogFiles.rbegin();
                     it < redfishLogFiles.rend(); it++)
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
                            LogParseError status = fillEventLogEntryJson(
                                idStr, logEntry, bmcLogEntry);
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
                    crow::utility::urlFromPieces(
                        "redfish", "v1", "Systems", PLATFORMSYSTEMID,
                        "LogServices", "EventLog", "Entries", targetID));
            });
}

inline void requestRoutesDBusEventLogEntryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/EventLog/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)([&app](const crow::Request& req,
                                                       const std::shared_ptr<
                                                           bmcweb::AsyncResp>&
                                                           asyncResp) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            // Collections don't include the static data added by SubRoute
            // because it has a duplicate entry for members
            asyncResp->res.jsonValue["@odata.type"] =
                "#LogEntryCollection.LogEntryCollection";
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/Systems/" PLATFORMSYSTEMID
                "/LogServices/EventLog/Entries";
            asyncResp->res.jsonValue["Name"] = "System Event Log Entries";
            asyncResp->res.jsonValue["Description"] =
                "Collection of System Event Log Entries";

            // DBus implementation of EventLog/Entries
            // Make call to Logging Service to find all log entry objects
            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec,
                            const dbus::utility::ManagedObjectType& resp) {
                    if (ec)
                    {
                        // TODO Handle for specific error code
                        BMCWEB_LOG_ERROR
                            << "getLogEntriesIfaceData resp_handler got error "
                            << ec;
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    nlohmann::json& entriesArray =
                        asyncResp->res.jsonValue["Members"];
                    entriesArray = nlohmann::json::array();
                    for (auto& objectPath : resp)
                    {
                        const uint32_t* id = nullptr;
                        std::time_t timestamp{};
                        std::time_t updateTimestamp{};
                        const std::string* severity = nullptr;
                        const std::string* message = nullptr;
                        const std::string* filePath = nullptr;
                        bool resolved = false;
                        const std::string* resolution = nullptr;
                        const std::vector<std::string>* additionalDataRaw =
                            nullptr;
                        for (auto& interfaceMap : objectPath.second)
                        {
                            if (interfaceMap.first ==
                                "xyz.openbmc_project.Logging.Entry")
                            {
                                for (auto& propertyMap : interfaceMap.second)
                                {
                                    if (propertyMap.first == "Id")
                                    {
                                        id = std::get_if<uint32_t>(
                                            &propertyMap.second);
                                    }
                                    else if (propertyMap.first == "Timestamp")
                                    {
                                        const uint64_t* millisTimeStamp =
                                            std::get_if<uint64_t>(
                                                &propertyMap.second);
                                        if (millisTimeStamp != nullptr)
                                        {
                                            timestamp = redfish::time_utils::
                                                getTimestamp(*millisTimeStamp);
                                        }
                                    }
                                    else if (propertyMap.first ==
                                             "UpdateTimestamp")
                                    {
                                        const uint64_t* millisTimeStamp =
                                            std::get_if<uint64_t>(
                                                &propertyMap.second);
                                        if (millisTimeStamp != nullptr)
                                        {
                                            updateTimestamp = redfish::
                                                time_utils::getTimestamp(
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
                                    else if (propertyMap.first == "Resolution")
                                    {
                                        resolution = std::get_if<std::string>(
                                            &propertyMap.second);
                                    }
                                    else if (propertyMap.first ==
                                             "AdditionalData")
                                    {
                                        additionalDataRaw = std::get_if<
                                            std::vector<std::string>>(
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
                        if (id == nullptr || message == nullptr ||
                            severity == nullptr)
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
                        if (additionalDataRaw != nullptr)
                        {
                            AdditionalData additional(*additionalDataRaw);
                            if (additional.count("REDFISH_MESSAGE_ID") > 0)
                            {
                                isMessageRegistry = true;
                                messageId = additional["REDFISH_MESSAGE_ID"];
                                BMCWEB_LOG_DEBUG << "MessageId: [" << messageId
                                                 << "]";

                                if (additional.count("REDFISH_MESSAGE_ARGS") >
                                    0)
                                {
                                    messageArgs =
                                        additional["REDFISH_MESSAGE_ARGS"];
                                }
                            }
                            if (additional.count(
                                    "REDFISH_ORIGIN_OF_CONDITION") > 0)
                            {
                                originOfCondition =
                                    additional["REDFISH_ORIGIN_OF_CONDITION"];
                            }
                            if (additional.count("DEVICE_NAME") > 0)
                            {
                                deviceName = additional["DEVICE_NAME"];
                            }
                        }
                        if (isMessageRegistry)
                        {

                            message_registries::generateMessageRegistry(
                                thisEntry,
                                "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                "/LogServices/"
                                "EventLog/Entries/",
                                "v1_9_0", std::to_string(*id),
                                "System Event Log Entry",
                                redfish::time_utils::getDateTimeStdtime(
                                    timestamp),
                                messageId, messageArgs, *resolution, resolved,
                                *severity);

                            origin_utils::convertDbusObjectToOriginOfCondition(
                                originOfCondition, std::to_string(*id),
                                asyncResp, thisEntry, deviceName);
                        }

                        // generateMessageRegistry will not create the entry if
                        // the messageId can't be found in message registries.
                        // So check the entry 'Id' anyway to cover that case.
                        if (thisEntry["Id"].size() == 0)
                        {
                            thisEntry["@odata.type"] =
                                "#LogEntry.v1_9_0.LogEntry";
                            thisEntry["@odata.id"] =
                                "/redfish/v1/Systems/" PLATFORMSYSTEMID "/"
                                "LogServices/EventLog/Entries/" +
                                std::to_string(*id);
                            thisEntry["Name"] = "System Event Log Entry";
                            thisEntry["Id"] = std::to_string(*id);
                            thisEntry["Message"] = *message;
                            thisEntry["Resolved"] = resolved;
                            thisEntry["EntryType"] = "Event";
                            thisEntry["Severity"] =
                                translateSeverityDbusToRedfish(*severity);
                            thisEntry["Created"] =
                                redfish::time_utils::getDateTimeStdtime(
                                    timestamp);
                            thisEntry["Modified"] =
                                redfish::time_utils::getDateTimeStdtime(
                                    updateTimestamp);
                        }
                        if (filePath != nullptr)
                        {
                            thisEntry["AdditionalDataURI"] =
                                "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                "/LogServices/"
                                "EventLog/"
                                "Entries/" +
                                std::to_string(*id) + "/attachment";
                        }
                        entriesArray.push_back(thisEntry);
                    }
                    std::sort(entriesArray.begin(), entriesArray.end(),
                              [](const nlohmann::json& left,
                                 const nlohmann::json& right) {
                                  return (left["Id"] <= right["Id"]);
                              });

                    asyncResp->res.jsonValue["Members@odata.count"] =
                        entriesArray.size();
                },
                "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
                "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
        });
}

inline void requestRoutesDBusEventLogEntry(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/EventLog/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& param) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            std::string entryID = param;
            dbus::utility::escapePathForDbus(entryID);

            // DBus implementation of EventLog/Entries
            // Make call to Logging Service to find all log entry objects
            crow::connections::systemBus->async_method_call(
                [asyncResp, entryID](const boost::system::error_code ec,
                                     const GetManagedPropertyType& resp) {
                    if (ec.value() == EBADR)
                    {
                        messages::resourceNotFound(asyncResp->res,
                                                   "EventLogEntry", entryID);
                        return;
                    }
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR
                            << "EventLogEntry (DBus) resp_handler got error "
                            << ec;
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    const uint32_t* id = nullptr;
                    std::time_t timestamp{};
                    std::time_t updateTimestamp{};
                    const std::string* severity = nullptr;
                    const std::string* message = nullptr;
                    const std::string* filePath = nullptr;
                    bool resolved = false;
                    const std::string* resolution = nullptr;
                    const std::vector<std::string>* additionalDataRaw = nullptr;

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
                                timestamp = redfish::time_utils::getTimestamp(
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
                            severity =
                                std::get_if<std::string>(&propertyMap.second);
                        }
                        else if (propertyMap.first == "Message")
                        {
                            message =
                                std::get_if<std::string>(&propertyMap.second);
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
                            resolution =
                                std::get_if<std::string>(&propertyMap.second);
                        }
                        else if (propertyMap.first == "AdditionalData")
                        {
                            additionalDataRaw =
                                std::get_if<std::vector<std::string>>(
                                    &propertyMap.second);
                        }
                        else if (propertyMap.first == "Path")
                        {
                            filePath =
                                std::get_if<std::string>(&propertyMap.second);
                        }
                    }
                    if (id == nullptr || message == nullptr ||
                        severity == nullptr)
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
                    if (additionalDataRaw != nullptr)
                    {
                        AdditionalData additional(*additionalDataRaw);
                        if (additional.count("REDFISH_MESSAGE_ID") > 0)
                        {
                            isMessageRegistry = true;
                            messageId = additional["REDFISH_MESSAGE_ID"];
                            BMCWEB_LOG_DEBUG << "MessageId: [" << messageId
                                             << "]";

                            if (additional.count("REDFISH_MESSAGE_ARGS") > 0)
                            {
                                messageArgs =
                                    additional["REDFISH_MESSAGE_ARGS"];
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
                    }

                    if (isMessageRegistry)
                    {
                        message_registries::generateMessageRegistry(
                            asyncResp->res.jsonValue,
                            "/redfish/v1/Systems/" PLATFORMSYSTEMID
                            "/LogServices/"
                            "EventLog/Entries/",
                            "v1_9_0", std::to_string(*id),
                            "System Event Log Entry",
                            redfish::time_utils::getDateTimeStdtime(timestamp),
                            messageId, messageArgs, *resolution, resolved,
                            *severity);

                        origin_utils::convertDbusObjectToOriginOfCondition(
                            originOfCondition, std::to_string(*id), asyncResp,
                            asyncResp->res.jsonValue, deviceName);
                    }

                    // generateMessageRegistry will not create the entry if
                    // the messageId can't be found in message registries.
                    // So check the entry 'Id' anyway to cover that case.
                    if (asyncResp->res.jsonValue["Id"].size() == 0)
                    {
                        asyncResp->res.jsonValue["@odata.type"] =
                            "#LogEntry.v1_9_0.LogEntry";
                        asyncResp->res.jsonValue["@odata.id"] =
                            "/redfish/v1/Systems/" PLATFORMSYSTEMID
                            "/LogServices/"
                            "EventLog/"
                            "Entries/" +
                            std::to_string(*id);
                        asyncResp->res.jsonValue["Name"] =
                            "System Event Log Entry";
                        asyncResp->res.jsonValue["Id"] = std::to_string(*id);
                        asyncResp->res.jsonValue["Message"] = *message;
                        asyncResp->res.jsonValue["Resolved"] = resolved;
                        asyncResp->res.jsonValue["EntryType"] = "Event";
                        asyncResp->res.jsonValue["Severity"] =
                            translateSeverityDbusToRedfish(*severity);
                        asyncResp->res.jsonValue["Created"] =
                            redfish::time_utils::getDateTimeStdtime(timestamp);
                        asyncResp->res.jsonValue["Modified"] =
                            redfish::time_utils::getDateTimeStdtime(
                                updateTimestamp);
                    }
                    if (filePath != nullptr)
                    {
                        asyncResp->res.jsonValue["AdditionalDataURI"] =
                            "/redfish/v1/Systems/" PLATFORMSYSTEMID
                            "/LogServices/"
                            "EventLog/"
                            "Entries/" +
                            std::to_string(*id) + "/attachment";
                    }
                },
                "xyz.openbmc_project.Logging",
                "/xyz/openbmc_project/logging/entry/" + entryID,
                "org.freedesktop.DBus.Properties", "GetAll", "");
        });

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/EventLog/Entries/<str>/")
        .privileges(redfish::privileges::patchLogEntry)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& entryId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                std::optional<bool> resolved;

                if (!json_util::readJsonPatch(req, asyncResp->res, "Resolved",
                                              resolved))
                {
                    return;
                }
                BMCWEB_LOG_DEBUG << "Set Resolved";

                crow::connections::systemBus->async_method_call(
                    [asyncResp, entryId](const boost::system::error_code ec) {
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG << "DBUS response error " << ec;
                            messages::internalError(asyncResp->res);
                            return;
                        }
                    },
                    "xyz.openbmc_project.Logging",
                    "/xyz/openbmc_project/logging/entry/" + entryId,
                    "org.freedesktop.DBus.Properties", "Set",
                    "xyz.openbmc_project.Logging.Entry", "Resolved",
                    dbus::utility::DbusVariantType(*resolved));
            });

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/EventLog/Entries/<str>/")
        .privileges(redfish::privileges::deleteLogEntry)

        .methods(boost::beast::http::verb::
                     delete_)([&app](const crow::Request& req,
                                     const std::shared_ptr<bmcweb::AsyncResp>&
                                         asyncResp,
                                     const std::string& param) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            BMCWEB_LOG_DEBUG << "Do delete single event entries.";

            std::string entryID = param;

            dbus::utility::escapePathForDbus(entryID);

            // Process response from Logging service.
            auto respHandler = [asyncResp,
                                entryID](const boost::system::error_code ec) {
                BMCWEB_LOG_DEBUG
                    << "EventLogEntry (DBus) doDelete callback: Done";
                if (ec)
                {
                    if (ec.value() == EBADR)
                    {
                        messages::resourceNotFound(asyncResp->res, "LogEntry",
                                                   entryID);
                        return;
                    }
                    // TODO Handle for specific error code
                    BMCWEB_LOG_ERROR
                        << "EventLogEntry (DBus) doDelete respHandler got error "
                        << ec;
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

inline void requestRoutesDBusEventLogEntryDownload(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/EventLog/Entries/<str>/attachment")
        .privileges(redfish::privileges::getLogEntry)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& param) {
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

            std::string entryID = param;
            dbus::utility::escapePathForDbus(entryID);

            crow::connections::systemBus->async_method_call(
                [asyncResp,
                 entryID](const boost::system::error_code ec,
                          const sdbusplus::message::unix_fd& unixfd) {
                    if (ec.value() == EBADR)
                    {
                        messages::resourceNotFound(
                            asyncResp->res, "EventLogAttachment", entryID);
                        return;
                    }
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG << "DBUS response error " << ec;
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    int fd = -1;
                    fd = dup(unixfd);
                    if (fd == -1)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    long long int size = lseek(fd, 0, SEEK_END);
                    if (size == -1)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    // Arbitrary max size of 64kb
                    constexpr int maxFileSize = 65536;
                    if (size > maxFileSize)
                    {
                        BMCWEB_LOG_ERROR
                            << "File size exceeds maximum allowed size of "
                            << maxFileSize;
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    std::vector<char> data(static_cast<size_t>(size));
                    long long int rc = lseek(fd, 0, SEEK_SET);
                    if (rc == -1)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    rc = read(fd, data.data(), data.size());
                    if ((rc == -1) || (rc != size))
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    close(fd);

                    std::string_view strData(data.data(), data.size());
                    std::string output = crow::utility::base64encode(strData);

                    asyncResp->res.addHeader(
                        boost::beast::http::field::content_type,
                        "application/octet-stream");
                    asyncResp->res.addHeader(
                        boost::beast::http::field::content_transfer_encoding,
                        "Base64");
                    asyncResp->res.body() = std::move(output);
                },
                "xyz.openbmc_project.Logging",
                "/xyz/openbmc_project/logging/entry/" + entryID,
                "xyz.openbmc_project.Logging.Entry", "GetEntry");
        });
}

void populateRedfishSELEntry(GetManagedPropertyType& resp,
                             nlohmann::json& thisEntry)
{
    uint32_t* id = nullptr;
    std::time_t timestamp{};
    std::time_t updateTimestamp{};
    std::string* severity = nullptr;
    std::string* message = nullptr;
    std::vector<std::string>* additionalDataVectorString = nullptr;
    std::string generatorId;
    std::string messageId;
    bool resolved = false;

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
        else if (propertyMap.first == "AdditionalData")
        {
            std::string eventDir;
            std::string recordType;
            std::string sensorData;
            std::ostringstream hexCodeEventDir;
            additionalDataVectorString =
                std::get_if<std::vector<std::string>>(&propertyMap.second);
            for (std::string& i : *additionalDataVectorString)
            {
                std::vector<std::string> additionalDataKVpair;
                boost::split(additionalDataKVpair, i, boost::is_any_of("="));
                if (additionalDataKVpair.size() == 2)
                {
                    if (additionalDataKVpair[0] == "EVENT_DIR")
                    {
                        hexCodeEventDir << "0x" << std::setfill('0')
                                        << std::setw(2) << std::hex
                                        << std::stoi(additionalDataKVpair[1]);
                    }
                    else if (additionalDataKVpair[0] == "GENERATOR_ID")
                    {
                        std::ostringstream hexCodeGeneratorId;
                        if (!additionalDataKVpair[1].empty())
                        {
                            hexCodeGeneratorId
                                << "0x" << std::setfill('0') << std::setw(4)
                                << std::hex
                                << std::stoi(additionalDataKVpair[1]);
                            generatorId = hexCodeGeneratorId.str();
                        }
                    }
                    else if (additionalDataKVpair[0] == "RECORD_TYPE")
                    {
                        recordType = additionalDataKVpair[1];
                    }
                    else if (additionalDataKVpair[0] == "SENSOR_DATA")
                    {
                        sensorData = additionalDataKVpair[1];
                        boost::algorithm::to_lower(sensorData);
                    }
                }
            }
            // MessageId for SEL is of the form 0xNNaabbcc
            // where 'NN' is the EventDir/EventType byte, aa is first byte
            // sensor data, bb is second byte sensor data, cc is third byte
            // sensor data
            messageId = hexCodeEventDir.str() + sensorData;
        }
    }
    if (id == nullptr || message == nullptr || severity == nullptr)
    {
        throw std::runtime_error("Invalid SEL Entry");
        return;
    }
    if (*message != "xyz.openbmc_project.Logging.SEL.Error.Created")
    {
        return;
    }
    thisEntry["@odata.type"] = "#LogEntry.v1_8_0.LogEntry";
    thisEntry["@odata.id"] = "/redfish/v1/Systems/" PLATFORMSYSTEMID
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
    thisEntry["Created"] = redfish::time_utils::getDateTimeStdtime(timestamp);
    thisEntry["Modified"] =
        redfish::time_utils::getDateTimeStdtime(updateTimestamp);
}

inline void requestRoutesDBusSELLogEntryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/SEL/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(
            boost::beast::http::verb::
                get)([](const crow::Request&,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            // Collections don't include the static data added by SubRoute
            // because it has a duplicate entry for members
            asyncResp->res.jsonValue["@odata.type"] =
                "#LogEntryCollection.LogEntryCollection";
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/Systems/" PLATFORMSYSTEMID
                "/LogServices/SEL/Entries";
            asyncResp->res.jsonValue["Name"] = "System Event Log Entries";
            asyncResp->res.jsonValue["Description"] =
                "Collection of System Event Log Entries";

            // DBus implementation of SEL/Entries
            // Make call to Logging Service to find all log entry objects
            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec,
                            GetManagedObjectsType& resp) {
                    if (ec)
                    {
                        // TODO Handle for specific error code
                        BMCWEB_LOG_ERROR
                            << "getLogEntriesIfaceData resp_handler got error "
                            << ec;
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    nlohmann::json& entriesArray =
                        asyncResp->res.jsonValue["Members"];
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
                                catch (const std::runtime_error& e)
                                {
                                    messages::internalError(asyncResp->res);
                                    continue;
                                }
                            }
                        }
                    }
                    std::sort(entriesArray.begin(), entriesArray.end(),
                              [](const nlohmann::json& left,
                                 const nlohmann::json& right) {
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
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/SEL/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& param)

            {
                std::string entryID = param;
                dbus::utility::escapePathForDbus(entryID);

                // DBus implementation of EventLog/Entries
                // Make call to Logging Service to find all log entry objects
                crow::connections::systemBus->async_method_call(
                    [asyncResp, entryID](const boost::system::error_code ec,
                                         GetManagedPropertyType& resp) {
                        if (ec.value() == EBADR)
                        {
                            messages::resourceNotFound(asyncResp->res,
                                                       "SELLogEntry", entryID);
                            return;
                        }
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR << "SELLogEntry (DBus) "
                                                "resp_handler got error "
                                             << ec;
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        nlohmann::json& thisEntry = asyncResp->res.jsonValue;
                        thisEntry = nlohmann::json::object();
                        try
                        {
                            populateRedfishSELEntry(resp, thisEntry);
                        }
                        catch (const std::runtime_error& e)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                    },
                    "xyz.openbmc_project.Logging",
                    "/xyz/openbmc_project/logging/entry/" + entryID,
                    "org.freedesktop.DBus.Properties", "GetAll", "");
            });

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/SEL/Entries/<str>/")
        .privileges(redfish::privileges::deleteLogEntry)
        .methods(boost::beast::http::verb::delete_)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& param) {
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

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/SEL/Actions/"
                      "LogService.ClearLog/")
        .privileges(redfish::privileges::postLogService)
        .methods(
            boost::beast::http::verb::
                post)([](const crow::Request&,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            crow::connections::systemBus->async_method_call(
                [asyncResp](const boost::system::error_code ec,
                            GetManagedObjectsType& resp) {
                    if (ec)
                    {
                        // TODO Handle for specific error code
                        BMCWEB_LOG_ERROR
                            << "getLogEntriesIfaceData resp_handler got error "
                            << ec;
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    for (auto& objectPath : resp)
                    {
                        uint32_t* id = nullptr;
                        std::string* message = nullptr;
                        for (auto& interfaceMap : objectPath.second)
                        {
                            if (interfaceMap.first ==
                                "xyz.openbmc_project.Logging.Entry")
                            {
                                for (auto& propertyMap : interfaceMap.second)
                                {
                                    if (propertyMap.first == "Id")
                                    {
                                        id = std::get_if<uint32_t>(
                                            &propertyMap.second);
                                    }
                                    else if (propertyMap.first == "Message")
                                    {
                                        message = std::get_if<std::string>(
                                            &propertyMap.second);
                                    }
                                }
                                if (id == nullptr || message == nullptr)
                                {
                                    messages::internalError(asyncResp->res);
                                    continue;
                                }
                                if (*message ==
                                    "xyz.openbmc_project.Logging.SEL.Error.Created")
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
        BMCWEB_LOG_ERROR << ec.message();
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
            BMCWEB_LOG_ERROR << "fail to expose host logs";
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
    logEntryJson["@odata.type"] = "#LogEntry.v1_9_0.LogEntry";
    logEntryJson["@odata.id"] = "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                "/LogServices/HostLogger/Entries/" +
                                logEntryID;
    logEntryJson["Name"] = "Host Logger Entry";
    logEntryJson["Id"] = logEntryID;
    logEntryJson["Message"] = msg;
    logEntryJson["EntryType"] = "Oem";
    logEntryJson["Severity"] = "OK";
    logEntryJson["OemRecordFormat"] = "Host Logger Entry";
}

inline void requestRoutesSystemHostLogger(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/HostLogger/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Systems/" PLATFORMSYSTEMID
                    "/LogServices/HostLogger";
                asyncResp->res.jsonValue["@odata.type"] =
                    "#LogService.v1_1_0.LogService";
                asyncResp->res.jsonValue["Name"] = "Host Logger Service";
                asyncResp->res.jsonValue["Description"] = "Host Logger Service";
                asyncResp->res.jsonValue["Id"] = "HostLogger";
                asyncResp->res.jsonValue["Entries"] = {
                    {"@odata.id", "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                  "/LogServices/HostLogger/Entries"}};
            });
}

inline void requestRoutesSystemHostLoggerCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/HostLogger/Entries/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
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
                    "/redfish/v1/Systems/" PLATFORMSYSTEMID
                    "/LogServices/HostLogger/Entries";
                asyncResp->res.jsonValue["@odata.type"] =
                    "#LogEntryCollection.LogEntryCollection";
                asyncResp->res.jsonValue["Name"] = "HostLogger Entries";
                asyncResp->res.jsonValue["Description"] =
                    "Collection of HostLogger Entries";
                nlohmann::json& logEntryArray =
                    asyncResp->res.jsonValue["Members"];
                logEntryArray = nlohmann::json::array();
                asyncResp->res.jsonValue["Members@odata.count"] = 0;

                std::vector<std::filesystem::path> hostLoggerFiles;
                if (!getHostLoggerFiles(hostLoggerFolderPath, hostLoggerFiles))
                {
                    BMCWEB_LOG_ERROR << "fail to get host log file path";
                    return;
                }

                // If we weren't provided top and skip limits, use the defaults.
                size_t skip = delegatedQuery.skip.value_or(0);
                size_t top =
                    delegatedQuery.top.value_or(query_param::Query::maxTop);
                size_t logCount = 0;
                // This vector only store the entries we want to expose that
                // control by skip and top.
                std::vector<std::string> logEntries;
                if (!getHostLoggerEntries(hostLoggerFiles, skip, top,
                                          logEntries, logCount))
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                // If vector is empty, that means skip value larger than total
                // log count
                if (logEntries.size() == 0)
                {
                    asyncResp->res.jsonValue["Members@odata.count"] = logCount;
                    return;
                }
                if (logEntries.size() > 0)
                {
                    for (size_t i = 0; i < logEntries.size(); i++)
                    {
                        nlohmann::json::object_t hostLogEntry;
                        fillHostLoggerEntryJson(std::to_string(skip + i),
                                                logEntries[i], hostLogEntry);
                        logEntryArray.push_back(std::move(hostLogEntry));
                    }

                    asyncResp->res.jsonValue["Members@odata.count"] = logCount;
                    if (skip + top < logCount)
                    {
                        asyncResp->res.jsonValue["Members@odata.nextLink"] =
                            "/redfish/v1/Systems/" PLATFORMSYSTEMID
                            "/LogServices/HostLogger/Entries?$skip=" +
                            std::to_string(skip + top);
                    }
                }
            });
}

inline void requestRoutesSystemHostLoggerLogEntry(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/HostLogger/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& param) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                const std::string& targetID = param;

                uint64_t idInt = 0;

                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
                const char* end = targetID.data() + targetID.size();

                auto [ptr, ec] = std::from_chars(targetID.data(), end, idInt);
                if (ec == std::errc::invalid_argument ||
                    ec == std::errc::result_out_of_range)
                {
                    messages::resourceMissingAtURI(
                        asyncResp->res,
                        crow::utility::urlFromPieces(
                            "redfish", "v1", "Systems", PLATFORMSYSTEMID,
                            "LogServices", "HostLogger", "Entries", targetID));
                    return;
                }

                std::vector<std::filesystem::path> hostLoggerFiles;
                if (!getHostLoggerFiles(hostLoggerFolderPath, hostLoggerFiles))
                {
                    BMCWEB_LOG_ERROR << "fail to get host log file path";
                    return;
                }

                size_t logCount = 0;
                size_t top = 1;
                std::vector<std::string> logEntries;
                // We can get specific entry by skip and top. For example, if we
                // want to get nth entry, we can set skip = n-1 and top = 1 to
                // get that entry
                if (!getHostLoggerEntries(hostLoggerFiles, idInt, top,
                                          logEntries, logCount))
                {
                    messages::internalError(asyncResp->res);
                    return;
                }

                if (!logEntries.empty())
                {
                    nlohmann::json::object_t hostLogEntry;
                    fillHostLoggerEntryJson(targetID, logEntries[0],
                                            hostLogEntry);
                    asyncResp->res.jsonValue.update(hostLogEntry);
                    return;
                }

                // Requested ID was not found
                messages::resourceMissingAtURI(
                    asyncResp->res,
                    crow::utility::urlFromPieces(
                        "redfish", "v1", "Systems", PLATFORMSYSTEMID,
                        "LogServices", "HostLogger", "Entries", targetID));
            });
}

constexpr char const* dumpManagerIface =
    "xyz.openbmc_project.Collection.DeleteAll";
inline void handleBMCLogServicesCollectionGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
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
        "/redfish/v1/Managers/" PLATFORMBMCID "/LogServices";
    asyncResp->res.jsonValue["Name"] = "Open BMC Log Services Collection";
    asyncResp->res.jsonValue["Description"] =
        "Collection of LogServices for this Manager";
    nlohmann::json& logServiceArray = asyncResp->res.jsonValue["Members"];
    logServiceArray = nlohmann::json::array();

#ifdef BMCWEB_ENABLE_REDFISH_BMC_JOURNAL
    logServiceArray.push_back(
        {{"@odata.id",
          "/redfish/v1/Managers/" PLATFORMBMCID "/LogServices/Journal"}});
#endif

    asyncResp->res.jsonValue["Members@odata.count"] = logServiceArray.size();

#ifdef BMCWEB_ENABLE_REDFISH_DUMP_LOG
    auto respHandler = [asyncResp](
                           const boost::system::error_code ec,
                           const dbus::utility::MapperGetSubTreePathsResponse&
                               subTreePaths) {
        if (ec)
        {
            BMCWEB_LOG_ERROR
                << "handleBMCLogServicesCollectionGet respHandler got error "
                << ec;
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
                    {{"@odata.id", "/redfish/v1/Managers/" PLATFORMBMCID
                                   "/LogServices/Dump"}});
            }
        }

        asyncResp->res.jsonValue["Members@odata.count"] =
            logServiceArrayLocal.size();
    };

    crow::connections::systemBus->async_method_call(
        respHandler, "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
        "/xyz/openbmc_project/dump", 0,
        std::array<const char*, 1>{dumpManagerIface});
#endif
}

inline void requestRoutesBMCLogServiceCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/" PLATFORMBMCID "/LogServices/")
        .privileges(redfish::privileges::getLogServiceCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleBMCLogServicesCollectionGet, std::ref(app)));
}

inline void requestRoutesBMCJournalLogService(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Managers/" PLATFORMBMCID "/LogServices/Journal/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)

            {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                asyncResp->res.jsonValue["@odata.type"] =
                    "#LogService.v1_1_0.LogService";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Managers/" PLATFORMBMCID
                    "/LogServices/Journal";
                asyncResp->res.jsonValue["Name"] =
                    "Open BMC Journal Log Service";
                asyncResp->res.jsonValue["Description"] =
                    "BMC Journal Log Service";
                asyncResp->res.jsonValue["Id"] = "Journal";
                asyncResp->res.jsonValue["OverWritePolicy"] = "WrapsWhenFull";

                std::pair<std::string, std::string> redfishDateTimeOffset =
                    redfish::time_utils::getDateTimeOffsetNow();
                asyncResp->res.jsonValue["DateTime"] =
                    redfishDateTimeOffset.first;
                asyncResp->res.jsonValue["DateTimeLocalOffset"] =
                    redfishDateTimeOffset.second;

                asyncResp->res.jsonValue["Entries"] = {
                    {"@odata.id", "/redfish/v1/Managers/" PLATFORMBMCID
                                  "/LogServices/Journal/Entries"}};
            });
}

static int
    fillBMCJournalLogEntryJson(const std::string& bmcJournalLogEntryID,
                               sd_journal* journal,
                               nlohmann::json::object_t& bmcJournalLogEntryJson)
{
    // Get the Log Entry contents
    int ret = 0;

    std::string message;
    std::string_view syslogID;
    ret = getJournalMetadata(journal, "SYSLOG_IDENTIFIER", syslogID);
    if (ret < 0)
    {
        BMCWEB_LOG_ERROR << "Failed to read SYSLOG_IDENTIFIER field: "
                         << strerror(-ret);
    }
    if (!syslogID.empty())
    {
        message += std::string(syslogID) + ": ";
    }

    std::string_view msg;
    ret = getJournalMetadata(journal, "MESSAGE", msg);
    if (ret < 0)
    {
        BMCWEB_LOG_ERROR << "Failed to read MESSAGE field: " << strerror(-ret);
        return 1;
    }
    message += std::string(msg);

    // Get the severity from the PRIORITY field
    long int severity = 8; // Default to an invalid priority
    ret = getJournalMetadata(journal, "PRIORITY", 10, severity);
    if (ret < 0)
    {
        BMCWEB_LOG_ERROR << "Failed to read PRIORITY field: " << strerror(-ret);
    }

    // Get the Created time from the timestamp
    std::string entryTimeStr;
    if (!getEntryTimestamp(journal, entryTimeStr))
    {
        return 1;
    }

    // Fill in the log entry with the gathered data
    bmcJournalLogEntryJson["@odata.type"] = "#LogEntry.v1_9_0.LogEntry";
    bmcJournalLogEntryJson["@odata.id"] = "/redfish/v1/Managers/" PLATFORMBMCID
                                          "/LogServices/Journal/Entries/" +
                                          bmcJournalLogEntryID;
    bmcJournalLogEntryJson["Name"] = "BMC Journal Entry";
    bmcJournalLogEntryJson["Id"] = bmcJournalLogEntryID;
    bmcJournalLogEntryJson["Message"] = std::move(message);
    bmcJournalLogEntryJson["EntryType"] = "Oem";
    bmcJournalLogEntryJson["Severity"] =
        severity <= 2 ? "Critical" : severity <= 4 ? "Warning" : "OK";
    bmcJournalLogEntryJson["OemRecordFormat"] = "BMC Journal Entry";
    bmcJournalLogEntryJson["Created"] = std::move(entryTimeStr);
    return 0;
}

inline void requestRoutesBMCJournalLogEntryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/" PLATFORMBMCID
                      "/LogServices/Journal/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
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

                size_t skip = delegatedQuery.skip.value_or(0);
                size_t top =
                    delegatedQuery.top.value_or(query_param::Query::maxTop);

                // Collections don't include the static data added by SubRoute
                // because it has a duplicate entry for members
                asyncResp->res.jsonValue["@odata.type"] =
                    "#LogEntryCollection.LogEntryCollection";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Managers/" PLATFORMBMCID
                    "/LogServices/Journal/Entries";
                asyncResp->res.jsonValue["Name"] = "Open BMC Journal Entries";
                asyncResp->res.jsonValue["Description"] =
                    "Collection of BMC Journal Entries";
                nlohmann::json& logEntryArray =
                    asyncResp->res.jsonValue["Members"];
                logEntryArray = nlohmann::json::array();

                // Go through the journal and use the timestamp to create a
                // unique ID for each entry
                sd_journal* journalTmp = nullptr;
                int ret = sd_journal_open(&journalTmp, SD_JOURNAL_LOCAL_ONLY);
                if (ret < 0)
                {
                    BMCWEB_LOG_ERROR << "failed to open journal: "
                                     << strerror(-ret);
                    messages::internalError(asyncResp->res);
                    return;
                }
                std::unique_ptr<sd_journal, decltype(&sd_journal_close)>
                    journal(journalTmp, sd_journal_close);
                journalTmp = nullptr;
                uint64_t entryCount = 0;
                // Reset the unique ID on the first entry
                bool firstEntry = true;
                SD_JOURNAL_FOREACH(journal.get())
                {
                    entryCount++;
                    // Handle paging using skip (number of entries to skip from
                    // the start) and top (number of entries to display)
                    if (entryCount <= skip || entryCount > skip + top)
                    {
                        continue;
                    }

                    std::string idStr;
                    if (!getUniqueEntryID(journal.get(), idStr, firstEntry))
                    {
                        continue;
                    }
                    firstEntry = false;

                    nlohmann::json::object_t bmcJournalLogEntry;
                    if (fillBMCJournalLogEntryJson(idStr, journal.get(),
                                                   bmcJournalLogEntry) != 0)
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    logEntryArray.push_back(std::move(bmcJournalLogEntry));
                }
                asyncResp->res.jsonValue["Members@odata.count"] = entryCount;
                if (skip + top < entryCount)
                {
                    asyncResp->res.jsonValue["Members@odata.nextLink"] =
                        "/redfish/v1/Managers/" PLATFORMBMCID
                        "/LogServices/Journal/Entries?$skip=" +
                        std::to_string(skip + top);
                }
            });
}

inline void requestRoutesBMCJournalLogEntry(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/" PLATFORMBMCID
                      "/LogServices/Journal/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& entryID) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                // Convert the unique ID back to a timestamp to find the entry
                uint64_t ts = 0;
                uint64_t index = 0;
                if (!getTimestampFromID(entryID, ts, index))
                {
                    messages::resourceMissingAtURI(
                        asyncResp->res,
                        crow::utility::urlFromPieces(
                            "redfish", "v1", "Managers", PLATFORMBMCID,
                            "LogServices", "Journal", "Entries", entryID));
                    return;
                }

                sd_journal* journalTmp = nullptr;
                int ret = sd_journal_open(&journalTmp, SD_JOURNAL_LOCAL_ONLY);
                if (ret < 0)
                {
                    BMCWEB_LOG_ERROR << "failed to open journal: "
                                     << strerror(-ret);
                    messages::internalError(asyncResp->res);
                    return;
                }
                std::unique_ptr<sd_journal, decltype(&sd_journal_close)>
                    journal(journalTmp, sd_journal_close);
                journalTmp = nullptr;
                // Go to the timestamp in the log and move to the entry at the
                // index tracking the unique ID
                std::string idStr;
                bool firstEntry = true;
                ret = sd_journal_seek_realtime_usec(journal.get(), ts);
                if (ret < 0)
                {
                    BMCWEB_LOG_ERROR << "failed to seek to an entry in journal"
                                     << strerror(-ret);
                    messages::internalError(asyncResp->res);
                    return;
                }
                for (uint64_t i = 0; i <= index; i++)
                {
                    sd_journal_next(journal.get());
                    if (!getUniqueEntryID(journal.get(), idStr, firstEntry))
                    {
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    firstEntry = false;
                }
                // Confirm that the entry ID matches what was requested
                if (idStr != entryID)
                {
                    messages::resourceMissingAtURI(
                        asyncResp->res,
                        crow::utility::urlFromPieces(
                            "redfish", "v1", "Managers", PLATFORMBMCID,
                            "LogServices", "Journal", "Entries", entryID));
                    return;
                }

                nlohmann::json::object_t bmcJournalLogEntry;
                if (fillBMCJournalLogEntryJson(entryID, journal.get(),
                                               bmcJournalLogEntry) != 0)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                asyncResp->res.jsonValue.update(bmcJournalLogEntry);
            });
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
        dumpPath = "/redfish/v1/Managers/" PLATFORMBMCID "/LogServices/Dump";
        overWritePolicy = "WrapsWhenFull";
        collectDiagnosticDataSupported = true;
    }
    else if (dumpType == "FaultLog")
    {
        dumpPath =
            "/redfish/v1/Managers/" PLATFORMBMCID "/LogServices/FaultLog";
        overWritePolicy = "Unknown";
        collectDiagnosticDataSupported = false;
    }
    else if (dumpType == "System")
    {
        dumpPath = "/redfish/v1/Systems/" PLATFORMSYSTEMID "/LogServices/Dump";
        overWritePolicy = "WrapsWhenFull";
        collectDiagnosticDataSupported = true;
    }
    else
    {
        BMCWEB_LOG_ERROR << "getDumpServiceInfo() invalid dump type: "
                         << dumpType;
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
    asyncResp->res.jsonValue["Actions"]["#LogService.ClearLog"]["target"] =
        dumpPath + "/Actions/LogService.ClearLog";

    if (collectDiagnosticDataSupported)
    {
        asyncResp->res.jsonValue["Actions"]["#LogService.CollectDiagnosticData"]
                                ["target"] =
            dumpPath + "/Actions/LogService.CollectDiagnosticData";
    }
}

inline void handleLogServicesDumpServiceGet(
    crow::App& app, const std::string& dumpType, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    getDumpServiceInfo(asyncResp, dumpType);
}

inline void handleLogServicesDumpServiceComputerSystemGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    getDumpServiceInfo(asyncResp, "System");
}

inline void handleLogServicesDumpEntriesCollectionGet(
    crow::App& app, const std::string& dumpType, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    getDumpEntryCollection(asyncResp, dumpType);
}

inline void handleLogServicesDumpEntriesCollectionComputerSystemGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    getDumpEntryCollection(asyncResp, "System");
}

inline void handleLogServicesDumpEntryGet(
    crow::App& app, const std::string& dumpType, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& dumpId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    getDumpEntryById(asyncResp, dumpId, dumpType);
}
inline void handleLogServicesDumpEntryComputerSystemGet(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& dumpId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    getDumpEntryById(asyncResp, dumpId, "System");
}

inline void handleLogServicesDumpEntryDelete(
    crow::App& app, const std::string& dumpType, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& dumpId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    deleteDumpEntry(asyncResp, dumpId, dumpType);
}

inline void handleLogServicesDumpEntryComputerSystemDelete(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& dumpId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    deleteDumpEntry(asyncResp, dumpId, "System");
}

inline void handleLogServicesDumpCollectDiagnosticDataPost(
    crow::App& app, const std::string& dumpType, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    createDump(asyncResp, req, dumpType);
}

inline void handleLogServicesDumpCollectDiagnosticDataComputerSystemPost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    createDump(asyncResp, req, "System");
}

inline void handleLogServicesDumpClearLogPost(
    crow::App& app, const std::string& dumpType, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    clearDump(asyncResp, dumpType);
}

inline void handleLogServicesDumpClearLogComputerSystemPost(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    clearDump(asyncResp, "System");
}

inline void requestRoutesBMCDumpService(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Managers/" PLATFORMBMCID "/LogServices/Dump/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleLogServicesDumpServiceGet, std::ref(app), "BMC"));
}

inline void requestRoutesBMCDumpEntryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/" PLATFORMBMCID
                      "/LogServices/Dump/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleLogServicesDumpEntriesCollectionGet, std::ref(app), "BMC"));
}

inline void requestRoutesBMCDumpEntry(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/" PLATFORMBMCID
                      "/LogServices/Dump/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleLogServicesDumpEntryGet, std::ref(app), "BMC"));

    BMCWEB_ROUTE(app, "/redfish/v1/Managers/" PLATFORMBMCID
                      "/LogServices/Dump/Entries/<str>/")
        .privileges(redfish::privileges::deleteLogEntry)
        .methods(boost::beast::http::verb::delete_)(std::bind_front(
            handleLogServicesDumpEntryDelete, std::ref(app), "BMC"));
}

inline void requestRoutesBMCDumpCreate(App& app)
{

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Managers/" PLATFORMBMCID
                 "/LogServices/Dump/Actions/LogService.CollectDiagnosticData/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(
            std::bind_front(handleLogServicesDumpCollectDiagnosticDataPost,
                            std::ref(app), "BMC"));
}

inline void requestRoutesBMCDumpClear(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/" PLATFORMBMCID
                      "/LogServices/Dump/Actions/LogService.ClearLog/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleLogServicesDumpClearLogPost, std::ref(app), "BMC"));
}

inline void requestRoutesFaultLogDumpService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/" PLATFORMSYSTEMID
                      "/LogServices/FaultLog/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleLogServicesDumpServiceGet, std::ref(app), "FaultLog"));
}

inline void requestRoutesFaultLogDumpEntryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/" PLATFORMSYSTEMID
                      "/LogServices/FaultLog/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleLogServicesDumpEntriesCollectionGet,
                            std::ref(app), "FaultLog"));
}

inline void requestRoutesFaultLogDumpEntry(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/" PLATFORMBMCID
                      "/LogServices/FaultLog/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleLogServicesDumpEntryGet, std::ref(app), "FaultLog"));

    BMCWEB_ROUTE(app, "/redfish/v1/Managers/" PLATFORMBMCID
                      "/LogServices/FaultLog/Entries/<str>/")
        .privileges(redfish::privileges::deleteLogEntry)
        .methods(boost::beast::http::verb::delete_)(std::bind_front(
            handleLogServicesDumpEntryDelete, std::ref(app), "FaultLog"));
}

inline void requestRoutesFaultLogDumpClear(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/" PLATFORMBMCID
                      "/LogServices/FaultLog/Actions/LogService.ClearLog/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleLogServicesDumpClearLogPost, std::ref(app), "FaultLog"));
}

inline void requestRoutesSystemDumpService(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/" PLATFORMSYSTEMID "/LogServices/Dump/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleLogServicesDumpServiceComputerSystemGet, std::ref(app)));
}

inline void requestRoutesSystemDumpEntryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/Dump/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleLogServicesDumpEntriesCollectionComputerSystemGet,
            std::ref(app)));
}

inline void requestRoutesSystemDumpEntry(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/Dump/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleLogServicesDumpEntryComputerSystemGet, std::ref(app)));

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/Dump/Entries/<str>/")
        .privileges(redfish::privileges::deleteLogEntry)
        .methods(boost::beast::http::verb::delete_)(std::bind_front(
            handleLogServicesDumpEntryComputerSystemDelete, std::ref(app)));
}

inline void requestRoutesSystemDumpCreate(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Systems/" PLATFORMSYSTEMID
                 "/LogServices/Dump/Actions/LogService.CollectDiagnosticData/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleLogServicesDumpCollectDiagnosticDataComputerSystemPost,
            std::ref(app)));
}

inline void requestRoutesSystemDumpClear(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/Dump/Actions/LogService.ClearLog/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(std::bind_front(
            handleLogServicesDumpClearLogComputerSystemPost, std::ref(app)));
}

inline void requestRoutesSystemFaultLogService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/FaultLog/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)

            {
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Systems/" PLATFORMSYSTEMID
                    "/LogServices/FaultLog";
                asyncResp->res.jsonValue["@odata.type"] =
                    "#LogService.v1_2_0.LogService";
                asyncResp->res.jsonValue["Name"] = "FaultLog LogService";
                asyncResp->res.jsonValue["Description"] =
                    "System FaultLog LogService";
                asyncResp->res.jsonValue["Id"] = "FaultLog";
                asyncResp->res.jsonValue["OverWritePolicy"] = "WrapsWhenFull";

                std::pair<std::string, std::string> redfishDateTimeOffset =
                    redfish::time_utils::getDateTimeOffsetNow();
                asyncResp->res.jsonValue["DateTime"] =
                    redfishDateTimeOffset.first;
                asyncResp->res.jsonValue["DateTimeLocalOffset"] =
                    redfishDateTimeOffset.second;

                asyncResp->res.jsonValue["Entries"] = {
                    {"@odata.id", "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                  "/LogServices/FaultLog/Entries"}};
                asyncResp->res.jsonValue["Actions"] = {
                    {"#LogService.ClearLog",
                     {{"target",
                       "/redfish/v1/Systems/" PLATFORMSYSTEMID
                       "/LogServices/FaultLog/Actions/LogService.ClearLog"}}}};
            });
}

inline void requestRoutesSystemFaultLogEntryCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/FaultLog/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                asyncResp->res.jsonValue["@odata.type"] =
                    "#LogEntryCollection.LogEntryCollection";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Systems/" PLATFORMSYSTEMID
                    "/LogServices/FaultLog/Entries";
                asyncResp->res.jsonValue["Name"] = "System FaultLog Entries";
                asyncResp->res.jsonValue["Description"] =
                    "Collection of System FaultLog Entries";

                getDumpEntryCollection(asyncResp, "FaultLog");
            });
}

inline void requestRoutesSystemFaultLogEntry(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/FaultLog/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)

        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& param) {
                getDumpEntryById(asyncResp, param, "FaultLog");
            });

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/FaultLog/Entries/<str>/")
        .privileges(redfish::privileges::deleteLogEntry)
        .methods(boost::beast::http::verb::delete_)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& param) {
                deleteDumpEntry(asyncResp, param, "FaultLog");
            });
}

inline void requestRoutesSystemFaultLogClear(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/FaultLog/Actions/LogService.ClearLog/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)

            { clearDump(asyncResp, "FaultLog"); });
}

inline void requestRoutesCrashdumpService(App& app)
{
    // Note: Deviated from redfish privilege registry for GET & HEAD
    // method for security reasons.
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/Crashdump/")
        // This is incorrect, should be:
        //.privileges(redfish::privileges::getLogService)
        .privileges({{"ConfigureManager"}})
        .methods(boost::beast::http::verb::get)([&app](const crow::Request& req,
                                                       const std::shared_ptr<
                                                           bmcweb::AsyncResp>&
                                                           asyncResp) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            // Copy over the static data to include the entries added by
            // SubRoute
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/Systems/" PLATFORMSYSTEMID
                "/LogServices/Crashdump";
            asyncResp->res.jsonValue["@odata.type"] =
                "#LogService.v1_2_0.LogService";
            asyncResp->res.jsonValue["Name"] = "Open BMC Oem Crashdump Service";
            asyncResp->res.jsonValue["Description"] = "Oem Crashdump Service";
            asyncResp->res.jsonValue["Id"] = "Oem Crashdump";
            asyncResp->res.jsonValue["OverWritePolicy"] = "WrapsWhenFull";
            asyncResp->res.jsonValue["MaxNumberOfRecords"] = 3;

            std::pair<std::string, std::string> redfishDateTimeOffset =
                redfish::time_utils::getDateTimeOffsetNow();
            asyncResp->res.jsonValue["DateTime"] = redfishDateTimeOffset.first;
            asyncResp->res.jsonValue["DateTimeLocalOffset"] =
                redfishDateTimeOffset.second;

            asyncResp->res.jsonValue["Entries"] = {
                {"@odata.id", "/redfish/v1/Systems/" PLATFORMSYSTEMID
                              "/LogServices/Crashdump/Entries"}};
            asyncResp->res.jsonValue["Actions"] = {
                {"#LogService.ClearLog",
                 {{"target",
                   "/redfish/v1/Systems/" PLATFORMSYSTEMID
                   "/LogServices/Crashdump/Actions/LogService.ClearLog"}}},
                {"#LogService.CollectDiagnosticData",
                 {{"target",
                   "/redfish/v1/Systems/" PLATFORMSYSTEMID
                   "/LogServices/Crashdump/Actions/LogService.CollectDiagnosticData"}}}};
        });
}

void inline requestRoutesCrashdumpClear(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/Crashdump/Actions/LogService.ClearLog/")
        // This is incorrect, should be:
        //.privileges(redfish::privileges::postLogService)
        .privileges({{"ConfigureComponents"}})
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                crow::connections::systemBus->async_method_call(
                    [asyncResp](const boost::system::error_code ec,
                                const std::string&) {
                        if (ec)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        messages::success(asyncResp->res);
                    },
                    crashdumpObject, crashdumpPath, deleteAllInterface,
                    "DeleteAll");
            });
}

static void
    logCrashdumpEntry(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& logID, nlohmann::json& logEntryJson)
{
    auto getStoredLogCallback =
        [asyncResp, logID,
         &logEntryJson](const boost::system::error_code ec,
                        const dbus::utility::DBusPropertiesMap& params) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "failed to get log ec: " << ec.message();
                if (ec.value() ==
                    boost::system::linux_error::bad_request_descriptor)
                {
                    messages::resourceNotFound(asyncResp->res, "LogEntry",
                                               logID);
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
                    crow::utility::urlFromPieces("redfish", "v1", "Systems",
                                                 PLATFORMSYSTEMID,
                                                 "LogServices", "Crashdump",
                                                 "Entries", logID, filename));
                return;
            }

            std::string crashdumpURI = "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                       "/LogServices/Crashdump/Entries/" +
                                       logID + "/" + filename;
            nlohmann::json::object_t logEntry;
            logEntry["@odata.type"] = "#LogEntry.v1_9_0.LogEntry";
            logEntry["@odata.id"] = "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                    "/LogServices/Crashdump/Entries/" +
                                    logID;
            logEntry["Name"] = "CPU Crashdump";
            logEntry["Id"] = logID;
            logEntry["EntryType"] = "Oem";
            logEntry["AdditionalDataURI"] = std::move(crashdumpURI);
            logEntry["DiagnosticDataType"] = "OEM";
            logEntry["OEMDiagnosticDataType"] = "PECICrashdump";
            logEntry["Created"] = std::move(timestamp);

            // If logEntryJson references an array of LogEntry resources
            // ('Members' list), then push this as a new entry, otherwise set it
            // directly
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
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/Crashdump/Entries/")
        // This is incorrect, should be.
        //.privileges(redfish::privileges::postLogEntryCollection)
        .privileges({{"ConfigureComponents"}})
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                crow::connections::systemBus->async_method_call(
                    [asyncResp](const boost::system::error_code ec,
                                const std::vector<std::string>& resp) {
                        if (ec)
                        {
                            if (ec.value() !=
                                boost::system::errc::no_such_file_or_directory)
                            {
                                BMCWEB_LOG_DEBUG << "failed to get entries ec: "
                                                 << ec.message();
                                messages::internalError(asyncResp->res);
                                return;
                            }
                        }
                        asyncResp->res.jsonValue["@odata.type"] =
                            "#LogEntryCollection.LogEntryCollection";
                        asyncResp->res.jsonValue["@odata.id"] =
                            "/redfish/v1/Systems/" PLATFORMSYSTEMID
                            "/LogServices/Crashdump/Entries";
                        asyncResp->res.jsonValue["Name"] =
                            "Open BMC Crashdump Entries";
                        asyncResp->res.jsonValue["Description"] =
                            "Collection of Crashdump Entries";
                        asyncResp->res.jsonValue["Members"] =
                            nlohmann::json::array();
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
                            logCrashdumpEntry(
                                asyncResp, logID,
                                asyncResp->res.jsonValue["Members"]);
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths", "",
                    0, std::array<const char*, 1>{crashdumpInterface});
            });
}

inline void requestRoutesCrashdumpEntry(App& app)
{
    // Note: Deviated from redfish privilege registry for GET & HEAD
    // method for security reasons.

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/Crashdump/Entries/<str>/")
        // this is incorrect, should be
        // .privileges(redfish::privileges::getLogEntry)
        .privileges({{"ConfigureComponents"}})
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& param) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
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
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/Crashdump/Entries/<str>/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& logID, const std::string& fileName) {
                // Do not call getRedfishRoute here since the crashdump file is
                // not a Redfish resource.

                auto getStoredLogCallback =
                    [asyncResp, logID, fileName,
                     url(boost::urls::url(req.urlView))](
                        const boost::system::error_code ec,
                        const std::vector<std::pair<
                            std::string, dbus::utility::DbusVariantType>>&
                            resp) {
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG << "failed to get log ec: "
                                             << ec.message();
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        std::string dbusFilename{};
                        std::string dbusTimestamp{};
                        std::string dbusFilepath{};

                        parseCrashdumpParameters(resp, dbusFilename,
                                                 dbusTimestamp, dbusFilepath);

                        if (dbusFilename.empty() || dbusTimestamp.empty() ||
                            dbusFilepath.empty())
                        {
                            messages::resourceMissingAtURI(
                                asyncResp->res,
                                crow::utility::urlFromPieces(
                                    "redfish", "v1", "Systems",
                                    PLATFORMSYSTEMID, "LogServices",
                                    "Crashdump", "Entries", logID, fileName));

                            return;
                        }

                        // Verify the file name parameter is correct
                        if (fileName != dbusFilename)
                        {
                            messages::resourceMissingAtURI(
                                asyncResp->res,
                                crow::utility::urlFromPieces(
                                    "redfish", "v1", "Systems",
                                    PLATFORMSYSTEMID, "LogServices",
                                    "Crashdump", "Entries", logID, fileName));

                            return;
                        }

                        if (!std::filesystem::exists(dbusFilepath))
                        {
                            messages::resourceMissingAtURI(
                                asyncResp->res,
                                crow::utility::urlFromPieces(
                                    "redfish", "v1", "Systems",
                                    PLATFORMSYSTEMID, "LogServices",
                                    "Crashdump", "Entries", logID, fileName));

                            return;
                        }
                        std::ifstream ifs(dbusFilepath,
                                          std::ios::in | std::ios::binary);
                        asyncResp->res.body() = std::string(
                            std::istreambuf_iterator<char>{ifs}, {});

                        // Configure this to be a file download when accessed
                        // from a browser
                        asyncResp->res.addHeader(
                            boost::beast::http::field::content_disposition,
                            "attachment");
                    };
                sdbusplus::asio::getAllProperties(
                    *crow::connections::systemBus, crashdumpObject,
                    crashdumpPath + std::string("/") + logID,
                    crashdumpInterface, std::move(getStoredLogCallback));
            });
}

enum class OEMDiagnosticType
{
    onDemand,
    telemetry,
    invalid,
};

inline OEMDiagnosticType
    getOEMDiagnosticType(const std::string_view& oemDiagStr)
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
        app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
             "/LogServices/Crashdump/Actions/LogService.CollectDiagnosticData/")
        // The below is incorrect;  Should be ConfigureManager
        //.privileges(redfish::privileges::postLogService)
        .privileges({{"ConfigureComponents"}})
        .methods(
            boost::beast::http::verb::
                post)([&app](
                          const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }

            std::string diagnosticDataType;
            std::string oemDiagnosticDataType;
            if (!redfish::json_util::readJsonAction(
                    req, asyncResp->res, "DiagnosticDataType",
                    diagnosticDataType, "OEMDiagnosticDataType",
                    oemDiagnosticDataType))
            {
                return;
            }

            if (diagnosticDataType != "OEM")
            {
                BMCWEB_LOG_ERROR
                    << "Only OEM DiagnosticDataType supported for Crashdump";
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
                BMCWEB_LOG_ERROR << "Unsupported OEMDiagnosticDataType: "
                                 << oemDiagnosticDataType;
                messages::actionParameterValueFormatError(
                    asyncResp->res, oemDiagnosticDataType,
                    "OEMDiagnosticDataType", "CollectDiagnosticData");
                return;
            }

            auto collectCrashdumpCallback = [asyncResp,
                                             payload(task::Payload(req)),
                                             taskMatchStr](
                                                const boost::system::error_code
                                                    ec,
                                                const std::string&) mutable {
                if (ec)
                {
                    if (ec.value() ==
                        boost::system::errc::operation_not_supported)
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
                std::shared_ptr<task::TaskData> task =
                    task::TaskData::createTask(
                        [](boost::system::error_code err, sdbusplus::message_t&,
                           const std::shared_ptr<task::TaskData>& taskData) {
                            if (!err)
                            {
                                taskData->messages.emplace_back(
                                    messages::taskCompletedOK(
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
                std::move(collectCrashdumpCallback), crashdumpObject,
                crashdumpPath, iface, method);
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

    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/EventLog/Actions/LogService.ClearLog/")
        .privileges(redfish::privileges::postLogService)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                BMCWEB_LOG_DEBUG << "Do delete all entries.";

                // Process response from Logging service.
                auto respHandler = [asyncResp](
                                       const boost::system::error_code ec) {
                    BMCWEB_LOG_DEBUG
                        << "doClearLog resp_handler callback: Done";
                    if (ec)
                    {
                        // TODO Handle for specific error code
                        BMCWEB_LOG_ERROR << "doClearLog resp_handler got error "
                                         << ec;
                        asyncResp->res.result(
                            boost::beast::http::status::internal_server_error);
                        return;
                    }

                    asyncResp->res.result(
                        boost::beast::http::status::no_content);
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
    static std::string const redfishLogDir = "/var/log/";
    static std::string const mfgTestPrefix = "mfgtest-";
    std::error_code ec;

    bool fileExists = std::filesystem::exists(path, ec);
    if (ec)
    {
        BMCWEB_LOG_ERROR << "File access error: " << ec;
    }
    else if (!fileExists)
    {
        BMCWEB_LOG_ERROR << path << " does not exist";
    }
    else
    {
        std::string filename =
            mfgTestPrefix + std::to_string(scriptExecOutputFiles.size());
        std::string targetPath = redfishLogDir + filename;
        BMCWEB_LOG_DEBUG << "Copying output to " << targetPath;
        std::filesystem::copy(path, targetPath, ec);
        if (ec)
        {
            BMCWEB_LOG_ERROR << "File copy error: " << ec;
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
        BMCWEB_LOG_ERROR << "Error executing script: " << ec;
        t->state = "Aborted";
        t->messages.emplace_back(messages::internalError());
    }
    else
    {
        BMCWEB_LOG_DEBUG << "Script exit code: " << exitCode;
        if (exitCode == 0)
        {
            std::string output(mfgTestProcOutput.data());
            int id = copyMfgTestOutputFile(output);
            if (id != -1)
            {
                std::string path = "/redfish/v1/Systems/" PLATFORMSYSTEMID
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
                BMCWEB_LOG_ERROR
                    << "CopyMfgTestOutputFile failed with Output file error";
                t->messages.emplace_back(
                    messages::taskAborted(std::to_string(t->index)));
            }
        }
        else
        {
            t->state = "Exception";
            BMCWEB_LOG_ERROR << "Mfg Script failed with exit code: "
                             << exitCode;
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
        app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
             "/LogServices/EventLog/Actions/LogService.CollectDiagnosticData/")
        .privileges(redfish::privileges::postLogService)
        .methods(
            boost::beast::http::verb::
                post)([&](const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            std::string diagnosticDataType;
            std::string oemDiagnosticDataType;
            if (!redfish::json_util::readJsonAction(
                    req, asyncResp->res, "DiagnosticDataType",
                    diagnosticDataType, "OEMDiagnosticDataType",
                    oemDiagnosticDataType))
            {
                return;
            }

            if (diagnosticDataType != "OEM")
            {
                BMCWEB_LOG_ERROR
                    << "Only OEM DiagnosticDataType supported for EventLog";
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
                        [](boost::system::error_code,
                           sdbusplus::message::message&,
                           const std::shared_ptr<task::TaskData>& taskData) {
                            mfgTestProc = nullptr;
                            mfgTestTask = nullptr;
                            if (taskData->percentComplete != 100)
                            {
                                taskData->state = "Exception";
                                taskData->messages.emplace_back(
                                    messages::taskAborted(
                                        std::to_string(taskData->index)));
                            }
                            return task::completed;
                        },
                        "0");
                    mfgTestTask->payload.emplace(req);
                    mfgTestTask->startTimer(
                        std::chrono::seconds(mfgTestTimeout));
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
                        BMCWEB_LOG_ERROR
                            << "Manufacturing script failed with error: "
                            << e.what();
                        mfgTestTask->messages.emplace_back(
                            messages::taskAborted(
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
                BMCWEB_LOG_ERROR << "Unsupported OEMDiagnosticDataType: "
                                 << oemDiagnosticDataType;
                messages::actionParameterValueFormatError(
                    asyncResp->res, oemDiagnosticDataType,
                    "OEMDiagnosticDataType", "CollectDiagnosticData");
                return;
            }
        });
}

inline void requestRoutesEventLogDiagnosticDataEntry(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/EventLog/DiagnosticData/<uint>/attachment")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const uint32_t id) {
                auto files = scriptExecOutputFiles.size();
                if (files == 0 || id > files - 1)
                {
                    messages::resourceMissingAtURI(asyncResp->res,
                                                   crow::utility::urlFromPieces(
                                                        "redfish", "v1", "Systems", PLATFORMSYSTEMID,
                                                        "LogServices", "EventLog", "DiagnosticData", id, "attachment");
                    return;
                }
                std::ifstream file(scriptExecOutputFiles[id]);
                if (!file.good())
                {
                    messages::resourceMissingAtURI(
                        asyncResp->res,
                        crow::utility::urlFromPieces(
                            "redfish", "v1", "Systems", PLATFORMSYSTEMID,
                            "LogServices", "EventLog", "DiagnosticData", id,
                            "attachment"));
                    return;
                }
                std::stringstream ss;
                ss << file.rdbuf();
                auto output = ss.str();

                asyncResp->res.addHeader("Content-Type",
                                         "application/octet-stream");
                asyncResp->res.addHeader("Content-Transfer-Encoding", "Binary");
                asyncResp->res.body() = std::move(output);
            });
}
#endif /* BMCWEB_ENABLE_MFG_TEST_API */

/****************************************************
 * Redfish PostCode interfaces
 * using DBUS interface: getPostCodesTS
 ******************************************************/
inline void requestRoutesPostCodesLogService(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/PostCodes/")
        .privileges(redfish::privileges::getLogService)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                asyncResp->res.jsonValue = {
                    {"@odata.id", "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                  "/LogServices/PostCodes"},
                    {"@odata.type", "#LogService.v1_1_0.LogService"},
                    {"Name", "POST Code Log Service"},
                    {"Description", "POST Code Log Service"},
                    {"Id", "PostCodes"},
                    {"OverWritePolicy", "WrapsWhenFull"},
                    {"Entries",
                     {{"@odata.id", "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                    "/LogServices/PostCodes/Entries"}}}};

                std::pair<std::string, std::string> redfishDateTimeOffset =
                    redfish::time_utils::getDateTimeOffsetNow();
                asyncResp->res.jsonValue["DateTime"] =
                    redfishDateTimeOffset.first;
                asyncResp->res.jsonValue["DateTimeLocalOffset"] =
                    redfishDateTimeOffset.second;

                asyncResp->res.jsonValue["Actions"]["#LogService.ClearLog"] = {
                    {"target",
                     "/redfish/v1/Systems/" PLATFORMSYSTEMID
                     "/LogServices/PostCodes/Actions/LogService.ClearLog"}};
            });
}

inline void requestRoutesPostCodesClear(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/PostCodes/Actions/LogService.ClearLog/")
        // The following privilege is incorrect;  It should be ConfigureManager
        //.privileges(redfish::privileges::postLogService)
        .privileges({{"ConfigureComponents"}})
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                BMCWEB_LOG_DEBUG << "Do delete all postcodes entries.";

                // Make call to post-code service to request clear all
                crow::connections::systemBus->async_method_call(
                    [asyncResp](const boost::system::error_code ec) {
                        if (ec)
                        {
                            // TODO Handle for specific error code
                            BMCWEB_LOG_ERROR
                                << "doClearPostCodes resp_handler got error "
                                << ec;
                            asyncResp->res.result(boost::beast::http::status::
                                                      internal_server_error);
                            messages::internalError(asyncResp->res);
                            return;
                        }
                    },
                    "xyz.openbmc_project.State.Boot.PostCode0",
                    "/xyz/openbmc_project/State/Boot/PostCode0",
                    "xyz.openbmc_project.Collection.DeleteAll", "DeleteAll");
            });
}

static void fillPostCodeEntry(
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
    nlohmann::json& logEntryArray = aResp->res.jsonValue["Members"];

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
        entryTimeStr =
            redfish::time_utils::getDateTimeUint(usecSinceEpoch / 1000 / 1000);

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
        std::vector<std::string> messageArgs = {
            std::to_string(bootIndex), timeOffsetStr.str(), hexCode.str()};

        // Get MessageArgs template from message registry
        std::string msg;
        if (message != nullptr)
        {
            msg = message->message;

            // fill in this post code value
            int i = 0;
            for (const std::string& messageArg : messageArgs)
            {
                std::string argStr = "%" + std::to_string(++i);
                size_t argPos = msg.find(argStr);
                if (argPos != std::string::npos)
                {
                    msg.replace(argPos, argStr.length(), messageArg);
                }
            }
        }

        // Get Severity template from message registry
        std::string severity;
        if (message != nullptr)
        {
            severity = message->messageSeverity;
        }

        // add to AsyncResp
        logEntryArray.push_back({});
        nlohmann::json& bmcLogEntry = logEntryArray.back();
        bmcLogEntry["@odata.type"] = "#LogEntry.v1_9_0.LogEntry";
        bmcLogEntry["@odata.id"] = "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                   "/LogServices/PostCodes/Entries/" +
                                   postcodeEntryID;
        bmcLogEntry["Name"] = "POST Code Log Entry";
        bmcLogEntry["Id"] = postcodeEntryID;
        bmcLogEntry["Message"] = std::move(msg);
        bmcLogEntry["MessageId"] = "OpenBMC.0.2.BIOSPOSTCode";
        bmcLogEntry["MessageArgs"] = std::move(messageArgs);
        bmcLogEntry["EntryType"] = "Event";
        bmcLogEntry["Severity"] = std::move(severity);
        bmcLogEntry["Created"] = entryTimeStr;
        if (!std::get<std::vector<uint8_t>>(code.second).empty())
        {
            bmcLogEntry["AdditionalDataURI"] =
                "/redfish/v1/Systems/" PLATFORMSYSTEMID
                "/LogServices/PostCodes/Entries/" +
                postcodeEntryID + "/attachment";
        }
    }
}

static void getPostCodeForEntry(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                                const uint16_t bootIndex,
                                const uint64_t codeIndex)
{
    crow::connections::systemBus->async_method_call(
        [aResp, bootIndex,
         codeIndex](const boost::system::error_code ec,
                    const boost::container::flat_map<
                        uint64_t, std::tuple<uint64_t, std::vector<uint8_t>>>&
                        postcode) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS POST CODE PostCode response error";
                messages::internalError(aResp->res);
                return;
            }

            // skip the empty postcode boots
            if (postcode.empty())
            {
                return;
            }

            fillPostCodeEntry(aResp, postcode, bootIndex, codeIndex);

            aResp->res.jsonValue["Members@odata.count"] =
                aResp->res.jsonValue["Members"].size();
        },
        "xyz.openbmc_project.State.Boot.PostCode0",
        "/xyz/openbmc_project/State/Boot/PostCode0",
        "xyz.openbmc_project.State.Boot.PostCode", "GetPostCodesWithTimeStamp",
        bootIndex);
}

static void getPostCodeForBoot(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const uint16_t bootIndex,
                               const uint16_t bootCount,
                               const uint64_t entryCount, const uint64_t skip,
                               const uint64_t top)
{
    crow::connections::systemBus->async_method_call(
        [aResp, bootIndex, bootCount, entryCount, skip,
         top](const boost::system::error_code ec,
              const boost::container::flat_map<
                  uint64_t, std::tuple<uint64_t, std::vector<uint8_t>>>&
                  postcode) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS POST CODE PostCode response error";
                messages::internalError(aResp->res);
                return;
            }

            uint64_t endCount = entryCount;
            if (!postcode.empty())
            {
                endCount = entryCount + postcode.size();

                if ((skip < endCount) && ((top + skip) > entryCount))
                {
                    uint64_t thisBootSkip =
                        std::max(skip, entryCount) - entryCount;
                    uint64_t thisBootTop =
                        std::min(top + skip, endCount) - entryCount;

                    fillPostCodeEntry(aResp, postcode, bootIndex, 0,
                                      thisBootSkip, thisBootTop);
                }
                aResp->res.jsonValue["Members@odata.count"] = endCount;
            }

            // continue to previous bootIndex
            if (bootIndex < bootCount)
            {
                getPostCodeForBoot(aResp, static_cast<uint16_t>(bootIndex + 1),
                                   bootCount, endCount, skip, top);
            }
            else
            {
                aResp->res.jsonValue["Members@odata.nextLink"] =
                    "/redfish/v1/Systems/" PLATFORMSYSTEMID
                    "/LogServices/PostCodes/Entries?$skip=" +
                    std::to_string(skip + top);
            }
        },
        "xyz.openbmc_project.State.Boot.PostCode0",
        "/xyz/openbmc_project/State/Boot/PostCode0",
        "xyz.openbmc_project.State.Boot.PostCode", "GetPostCodesWithTimeStamp",
        bootIndex);
}

static void
    getCurrentBootNumber(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         size_t skip, size_t top)
{
    uint64_t entryCount = 0;
    sdbusplus::asio::getProperty<uint16_t>(
        *crow::connections::systemBus,
        "xyz.openbmc_project.State.Boot.PostCode0",
        "/xyz/openbmc_project/State/Boot/PostCode0",
        "xyz.openbmc_project.State.Boot.PostCode", "CurrentBootCycleCount",
        [aResp, entryCount, skip, top](const boost::system::error_code ec,
                                       const uint16_t bootCount) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG << "DBUS response error " << ec;
                messages::internalError(aResp->res);
                return;
            }
            getPostCodeForBoot(aResp, 1, bootCount, entryCount, skip, top);
        });
}

inline void requestRoutesPostCodesEntryCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/PostCodes/Entries/")
        .privileges(redfish::privileges::getLogEntryCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
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
                asyncResp->res.jsonValue["@odata.type"] =
                    "#LogEntryCollection.LogEntryCollection";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Systems/" PLATFORMSYSTEMID
                    "/LogServices/PostCodes/Entries";
                asyncResp->res.jsonValue["Name"] = "BIOS POST Code Log Entries";
                asyncResp->res.jsonValue["Description"] =
                    "Collection of POST Code Log Entries";
                asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
                asyncResp->res.jsonValue["Members@odata.count"] = 0;

                size_t skip = delegatedQuery.skip.value_or(0);
                size_t top =
                    delegatedQuery.top.value_or(query_param::Query::maxTop);
                getCurrentBootNumber(asyncResp, skip, top);
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
inline static bool parsePostCode(const std::string& postCodeID,
                                 uint64_t& currentValue, uint16_t& index)
{
    std::vector<std::string> split;
    boost::algorithm::split(split, postCodeID, boost::is_any_of("-"));
    if (split.size() != 2 || split[0].length() < 2 || split[0].front() != 'B')
    {
        return false;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const char* start = split[0].data() + 1;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const char* end = split[0].data() + split[0].size();
    auto [ptrIndex, ecIndex] = std::from_chars(start, end, index);

    if (ptrIndex != end || ecIndex != std::errc())
    {
        return false;
    }

    start = split[1].data();

    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    end = split[1].data() + split[1].size();
    auto [ptrValue, ecValue] = std::from_chars(start, end, currentValue);
    if (ptrValue != end || ecValue != std::errc())
    {
        return false;
    }

    return true;
}

inline void requestRoutesPostCodesEntryAdditionalData(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/PostCodes/Entries/<str>/attachment/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& postCodeID) {
                if (!http_helpers::isContentTypeAllowed(
                        req.getHeaderValue("Accept"),
                        http_helpers::ContentType::OctetStream, true))
                {
                    asyncResp->res.result(
                        boost::beast::http::status::bad_request);
                    return;
                }

                uint64_t currentValue = 0;
                uint16_t index = 0;
                if (!parsePostCode(postCodeID, currentValue, index))
                {
                    messages::resourceNotFound(asyncResp->res, "LogEntry",
                                               postCodeID);
                    return;
                }

                crow::connections::systemBus->async_method_call(
                    [asyncResp, postCodeID, currentValue](
                        const boost::system::error_code ec,
                        const std::vector<std::tuple<
                            uint64_t, std::vector<uint8_t>>>& postcodes) {
                        if (ec.value() == EBADR)
                        {
                            messages::resourceNotFound(asyncResp->res,
                                                       "LogEntry", postCodeID);
                            return;
                        }
                        if (ec)
                        {
                            BMCWEB_LOG_DEBUG << "DBUS response error " << ec;
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        size_t value = static_cast<size_t>(currentValue) - 1;
                        if (value == std::string::npos ||
                            postcodes.size() < currentValue)
                        {
                            BMCWEB_LOG_ERROR << "Wrong currentValue value";
                            messages::resourceNotFound(asyncResp->res,
                                                       "LogEntry", postCodeID);
                            return;
                        }

                        auto& [tID, c] = postcodes[value];
                        if (c.empty())
                        {
                            BMCWEB_LOG_INFO << "No found post code data";
                            messages::resourceNotFound(asyncResp->res,
                                                       "LogEntry", postCodeID);
                            return;
                        }
                        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                        const char* d = reinterpret_cast<const char*>(c.data());
                        std::string_view strData(d, c.size());

                        asyncResp->res.addHeader("Content-Type",
                                                 "application/octet-stream");
                        asyncResp->res.addHeader("Content-Transfer-Encoding",
                                                 "Base64");
                        asyncResp->res.body() =
                            crow::utility::base64encode(strData);
                    },
                    "xyz.openbmc_project.State.Boot.PostCode0",
                    "/xyz/openbmc_project/State/Boot/PostCode0",
                    "xyz.openbmc_project.State.Boot.PostCode", "GetPostCodes",
                    index);
            });
}

inline void requestRoutesPostCodesEntry(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/PostCodes/Entries/<str>/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& targetID) {
                uint16_t bootIndex = 0;
                uint64_t codeIndex = 0;
                if (!parsePostCode(targetID, codeIndex, bootIndex))
                {
                    // Requested ID was not found
                    messages::resourceMissingAtURI(
                        asyncResp->res,
                        crow::utility::urlFromPieces(
                            "redfish", "v1", "Systems", PLATFORMSYSTEMID,
                            "LogServices", "PostCodes", "Entries", targetID));
                    return;
                }
                if (bootIndex == 0 || codeIndex == 0)
                {
                    BMCWEB_LOG_DEBUG << "Get Post Code invalid entry string "
                                     << targetID;
                }

                asyncResp->res.jsonValue["@odata.type"] =
                    "#LogEntry.v1_4_0.LogEntry";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Systems/" PLATFORMSYSTEMID
                    "/LogServices/PostCodes/Entries";
                asyncResp->res.jsonValue["Name"] = "BIOS POST Code Log Entries";
                asyncResp->res.jsonValue["Description"] =
                    "Collection of POST Code Log Entries";
                asyncResp->res.jsonValue["Members"] = nlohmann::json::array();
                asyncResp->res.jsonValue["Members@odata.count"] = 0;

                getPostCodeForEntry(asyncResp, bootIndex, codeIndex);
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
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const std::string& chassisId)

            {
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
                        for (const std::pair<
                                 std::string,
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
                                "/redfish/v1/Chassis/" + chassisId +
                                "/LogServices";
                            asyncResp->res.jsonValue["Name"] =
                                "System Log Services Collection";
                            asyncResp->res.jsonValue["Description"] =
                                "Collection of LogServices for this Computer System";
                            nlohmann::json& logServiceArray =
                                asyncResp->res.jsonValue["Members"];
                            logServiceArray = nlohmann::json::array();
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_LOGSERVICES
                            if (chassisId.find("GPU") != std::string::npos ||
                                chassisId.find("NVSwitch") != std::string::npos)
                            {
                                logServiceArray.push_back(
                                    {{"@odata.id", "/redfish/v1/Chassis/" +
                                                       chassisId +
                                                       "/LogServices/XID"}});
                            }
#endif // BMCWEB_ENABLE_NVIDIA_OEM_LOGSERVICES
                            asyncResp->res.jsonValue["Members@odata.count"] =
                                logServiceArray.size();
                            return;
                        }
                        // Couldn't find an object with that name.  return an
                        // error
                        messages::resourceNotFound(asyncResp->res,
                                                   "#Chassis.v1_17_0.Chassis",
                                                   chassisId);
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
        .methods(
            boost::beast::http::verb::get)([](const crow::Request&,
                                              const std::shared_ptr<
                                                  bmcweb::AsyncResp>& asyncResp,
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
                    for (const std::pair<
                             std::string,
                             std::vector<std::pair<std::string,
                                                   std::vector<std::string>>>>&
                             object : subtree)
                    {
                        const std::string& path = object.first;
                        const std::vector<
                            std::pair<std::string, std::vector<std::string>>>&
                            connectionNames = object.second;

                        const std::string& connectionName =
                            connectionNames[0].first;

                        sdbusplus::message::object_path objPath(path);
                        if (objPath.filename() != chassisId)
                        {
                            continue;
                        }
                        asyncResp->res.jsonValue["@odata.id"] =
                            "/redfish/v1/Chassis/" + chassisId +
                            "/LogServices/XID";
                        asyncResp->res.jsonValue["@odata.type"] =
                            "#LogService.v1_1_0.LogService";
                        asyncResp->res.jsonValue["Name"] = "XID Log Service";
                        asyncResp->res.jsonValue["Description"] =
                            "XID Log Service";
                        asyncResp->res.jsonValue["Id"] = "XID";
                        asyncResp->res.jsonValue["OverWritePolicy"] =
                            "WrapsWhenFull";

                        std::pair<std::string, std::string>
                            redfishDateTimeOffset =
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
                                    BMCWEB_LOG_DEBUG
                                        << "DBus response error for PrettyName";
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                // Call Phosphor-logging GetStats method to get
                                // LatestEntryTimestamp and LatestEntryID
                                crow::connections::systemBus->async_method_call(
                                    [asyncResp](
                                        const boost::system::error_code ec,
                                        const std::tuple<uint32_t, uint64_t>&
                                            reqData) {
                                        if (ec)
                                        {
                                            BMCWEB_LOG_ERROR
                                                << "Failed to get Data from xyz.openbmc_project.Logging GetStats: "
                                                << ec;
                                            messages::internalError(
                                                asyncResp->res);
                                            return;
                                        }
                                        auto lastTimeStamp =
                                            redfish::time_utils::getTimestamp(
                                                std::get<1>(reqData));
                                        asyncResp->res
                                            .jsonValue["Oem"]["Nvidia"]
                                                      ["@odata.type"] =
                                            "#NvidiaLogService.v1_0_0.NvidiaLogService";
                                        asyncResp->res
                                            .jsonValue["Oem"]["Nvidia"]
                                                      ["LatestEntryID"] =
                                            std::to_string(
                                                std::get<0>(reqData));
                                        asyncResp->res
                                            .jsonValue["Oem"]["Nvidia"]
                                                      ["LatestEntryTimeStamp"] =
                                            redfish::time_utils::
                                                getDateTimeStdtime(
                                                    lastTimeStamp);
                                    },
                                    "xyz.openbmc_project.Logging",
                                    "/xyz/openbmc_project/logging",
                                    "xyz.openbmc_project.Logging.Namespace",
                                    "GetStats", chassisName + "_XID");
                            });

                        asyncResp->res.jsonValue["Entries"] = {
                            {"@odata.id", "/redfish/v1/Chassis/" + chassisId +
                                              "/LogServices/XID/Entries"}};
                        return;
                    }
                    // Couldn't find an object with that name.  return an
                    // error
                    messages::resourceNotFound(
                        asyncResp->res, "#Chassis.v1_17_0.Chassis", chassisId);
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
        .methods(
            boost::beast::http::verb::get)([](const crow::Request&,
                                              const std::shared_ptr<
                                                  bmcweb::AsyncResp>& asyncResp,
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
                    for (const std::pair<
                             std::string,
                             std::vector<std::pair<std::string,
                                                   std::vector<std::string>>>>&
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

                        const std::string& connectionName =
                            connectionNames[0].first;
                        const std::vector<std::string>& interfaces2 =
                            connectionNames[0].second;

                        const std::string inventoryItemInterface =
                            "xyz.openbmc_project.Inventory.Item";
                        if (std::find(interfaces2.begin(), interfaces2.end(),
                                      inventoryItemInterface) !=
                            interfaces2.end())
                        {
                            sdbusplus::asio::getProperty<std::string>(
                                *crow::connections::systemBus, connectionName,
                                path, inventoryItemInterface, "PrettyName",
                                [asyncResp, chassisId(std::string(chassisId))](
                                    const boost::system::error_code ec,
                                    const std::string& chassisName) {
                                    if (ec)
                                    {
                                        BMCWEB_LOG_DEBUG
                                            << "DBus response error for PrettyName";
                                        messages::internalError(asyncResp->res);
                                        return;
                                    }

                                    BMCWEB_LOG_DEBUG << "PrettyName: "
                                                     << chassisName;
                                    // Collections don't include the static data
                                    // added by SubRoute because it has a
                                    // duplicate entry for members
                                    asyncResp->res.jsonValue["@odata.type"] =
                                        "#LogEntryCollection.LogEntryCollection";
                                    asyncResp->res.jsonValue["@odata.id"] =
                                        "/redfish/v1/Chassis/" + chassisId +
                                        "/LogServices/XID/Entries";
                                    asyncResp->res.jsonValue["Name"] =
                                        "XID Log Entries";
                                    asyncResp->res.jsonValue["Description"] =
                                        "Collection of XID Log Entries";
                                    asyncResp->res
                                        .jsonValue["Members@odata.count"] = 0;

                                    BMCWEB_LOG_DEBUG
                                        << "Namespace: " << chassisName
                                        << "_XID";

                                    // DBus implementation of EventLog/Entries
                                    // Make call to Logging Service to find all
                                    // log entry objects
                                    crow::connections::
                                        systemBus
                                            ->async_method_call(
                                                [asyncResp,
                                                 chassisId(std::
                                                               string(
                                                                   chassisId))](const boost::
                                                                                    system::error_code
                                                                                        ec,
                                                                                const GetManagedObjectsType& resp) {
                                                    if (ec)
                                                    {
                                                        // TODO Handle for
                                                        // specific error code
                                                        BMCWEB_LOG_ERROR
                                                            << "getLogEntriesIfaceData resp_handler got error "
                                                            << ec.message();
                                                        messages::internalError(
                                                            asyncResp->res);
                                                        return;
                                                    }

                                                    const uint32_t* id =
                                                        nullptr;
                                                    std::time_t timestamp{};
                                                    std::time_t
                                                        updateTimestamp{};
                                                    const std::string*
                                                        severity = nullptr;
                                                    const std::string* message =
                                                        nullptr;
                                                    const std::string*
                                                        filePath = nullptr;
                                                    bool resolved = false;
                                                    const std::string*
                                                        resolution = nullptr;
                                                    const std::vector<
                                                        std::string>*
                                                        additionalDataRaw =
                                                            nullptr;

                                                    nlohmann::json&
                                                        entriesArray =
                                                            asyncResp->res
                                                                .jsonValue
                                                                    ["Members"];
                                                    entriesArray =
                                                        nlohmann::json::array();

                                                    for (auto& objectPath :
                                                         resp)
                                                    {
                                                        nlohmann::json
                                                            thisEntry =
                                                                nlohmann::json::
                                                                    object();

                                                        for (auto&
                                                                 interfaceMap :
                                                             objectPath.second)
                                                        {
                                                            if (interfaceMap
                                                                    .first ==
                                                                "xyz.openbmc_project.Logging.Entry")
                                                            {
                                                                nlohmann::json thisEntry =
                                                                    nlohmann::json::
                                                                        object();

                                                                for (
                                                                    auto&
                                                                        propertyMap :
                                                                    interfaceMap
                                                                        .second)
                                                                {
                                                                    if (propertyMap
                                                                            .first ==
                                                                        "Id")
                                                                    {
                                                                        id = std::get_if<
                                                                            uint32_t>(
                                                                            &propertyMap
                                                                                 .second);
                                                                    }
                                                                    else if (
                                                                        propertyMap
                                                                            .first ==
                                                                        "Timestamp")
                                                                    {
                                                                        const uint64_t* millisTimeStamp =
                                                                            std::get_if<
                                                                                uint64_t>(
                                                                                &propertyMap
                                                                                     .second);
                                                                        if (millisTimeStamp !=
                                                                            nullptr)
                                                                        {
                                                                            timestamp = redfish::
                                                                                time_utils::getTimestamp(
                                                                                    *millisTimeStamp);
                                                                        }
                                                                    }
                                                                    else if (
                                                                        propertyMap
                                                                            .first ==
                                                                        "UpdateTimestamp")
                                                                    {
                                                                        const uint64_t* millisTimeStamp =
                                                                            std::get_if<
                                                                                uint64_t>(
                                                                                &propertyMap
                                                                                     .second);
                                                                        if (millisTimeStamp !=
                                                                            nullptr)
                                                                        {
                                                                            updateTimestamp =
                                                                                redfish::time_utils::
                                                                                    getTimestamp(
                                                                                        *millisTimeStamp);
                                                                        }
                                                                    }
                                                                    else if (
                                                                        propertyMap
                                                                            .first ==
                                                                        "Severity")
                                                                    {
                                                                        severity = std::get_if<
                                                                            std::
                                                                                string>(
                                                                            &propertyMap
                                                                                 .second);
                                                                    }
                                                                    else if (
                                                                        propertyMap
                                                                            .first ==
                                                                        "Message")
                                                                    {
                                                                        message = std::get_if<
                                                                            std::
                                                                                string>(
                                                                            &propertyMap
                                                                                 .second);
                                                                    }
                                                                    else if (
                                                                        propertyMap
                                                                            .first ==
                                                                        "Resolved")
                                                                    {
                                                                        const bool* resolveptr =
                                                                            std::get_if<
                                                                                bool>(
                                                                                &propertyMap
                                                                                     .second);
                                                                        if (resolveptr ==
                                                                            nullptr)
                                                                        {
                                                                            messages::internalError(
                                                                                asyncResp
                                                                                    ->res);
                                                                            return;
                                                                        }
                                                                        resolved =
                                                                            *resolveptr;
                                                                    }
                                                                    else if (
                                                                        propertyMap
                                                                            .first ==
                                                                        "Resolution")
                                                                    {
                                                                        resolution = std::get_if<
                                                                            std::
                                                                                string>(
                                                                            &propertyMap
                                                                                 .second);
                                                                    }
                                                                    else if (
                                                                        propertyMap
                                                                            .first ==
                                                                        "AdditionalData")
                                                                    {
                                                                        additionalDataRaw =
                                                                            std::get_if<
                                                                                std::vector<
                                                                                    std::
                                                                                        string>>(
                                                                                &propertyMap
                                                                                     .second);
                                                                    }
                                                                    else if (
                                                                        propertyMap
                                                                            .first ==
                                                                        "Path")
                                                                    {
                                                                        filePath = std::get_if<
                                                                            std::
                                                                                string>(
                                                                            &propertyMap
                                                                                 .second);
                                                                    }
                                                                }
                                                                if (id ==
                                                                        nullptr ||
                                                                    message ==
                                                                        nullptr ||
                                                                    severity ==
                                                                        nullptr)
                                                                {
                                                                    messages::internalError(
                                                                        asyncResp
                                                                            ->res);
                                                                    return;
                                                                }

                                                                // Determine if
                                                                // it's a
                                                                // message
                                                                // registry
                                                                // format or
                                                                // not.
                                                                bool
                                                                    isMessageRegistry =
                                                                        false;
                                                                std::string
                                                                    messageId;
                                                                std::string
                                                                    messageArgs;
                                                                std::string
                                                                    originOfCondition;
                                                                std::string
                                                                    deviceName;
                                                                if (additionalDataRaw !=
                                                                    nullptr)
                                                                {
                                                                    AdditionalData
                                                                        additional(
                                                                            *additionalDataRaw);
                                                                    if (additional
                                                                            .count(
                                                                                "REDFISH_MESSAGE_ID") >
                                                                        0)
                                                                    {
                                                                        isMessageRegistry =
                                                                            true;
                                                                        messageId = additional
                                                                            ["REDFISH_MESSAGE_ID"];
                                                                        BMCWEB_LOG_DEBUG
                                                                            << "MessageId: ["
                                                                            << messageId
                                                                            << "]";

                                                                        if (additional
                                                                                .count(
                                                                                    "REDFISH_MESSAGE_ARGS") >
                                                                            0)
                                                                        {
                                                                            messageArgs = additional
                                                                                ["REDFISH_MESSAGE_ARGS"];
                                                                        }
                                                                    }
                                                                    if (additional
                                                                            .count(
                                                                                "REDFISH_ORIGIN_OF_CONDITION") >
                                                                        0)
                                                                    {
                                                                        originOfCondition =
                                                                            additional
                                                                                ["REDFISH_ORIGIN_OF_CONDITION"];
                                                                    }
                                                                    if (additional
                                                                            .count(
                                                                                "DEVICE_NAME") >
                                                                        0)
                                                                    {
                                                                        deviceName = additional
                                                                            ["DEVICE_NAME"];
                                                                    }
                                                                }
                                                                if (isMessageRegistry)
                                                                {
                                                                    message_registries::generateMessageRegistry(
                                                                        thisEntry,
                                                                        "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                                                        "/LogServices/"
                                                                        "EventLog/Entries/",
                                                                        "v1_9_0",
                                                                        std::to_string(
                                                                            *id),
                                                                        "System Event Log Entry",
                                                                        redfish::time_utils::
                                                                            getDateTimeStdtime(
                                                                                timestamp),
                                                                        messageId,
                                                                        messageArgs,
                                                                        *resolution,
                                                                        resolved,
                                                                        *severity);

                                                                    origin_utils::convertDbusObjectToOriginOfCondition(
                                                                        originOfCondition,
                                                                        std::to_string(
                                                                            *id),
                                                                        asyncResp,
                                                                        thisEntry,
                                                                        deviceName);
                                                                }

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
                                                                if (thisEntry["Id"]
                                                                        .size() ==
                                                                    0)
                                                                {
                                                                    thisEntry
                                                                        ["@odata.type"] =
                                                                            "#LogEntry.v1_9_0.LogEntry";
                                                                    thisEntry["@odata.id"] =
                                                                        "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                                                        "/LogServices/"
                                                                        "EventLog/"
                                                                        "Entries/" +
                                                                        std::to_string(
                                                                            *id);
                                                                    thisEntry
                                                                        ["Name"] =
                                                                            "System Event Log Entry";
                                                                    thisEntry["Id"] =
                                                                        std::to_string(
                                                                            *id);
                                                                    thisEntry
                                                                        ["Message"] =
                                                                            *message;
                                                                    thisEntry
                                                                        ["Resolved"] =
                                                                            resolved;
                                                                    thisEntry
                                                                        ["EntryType"] =
                                                                            "Event";
                                                                    thisEntry["Severity"] =
                                                                        translateSeverityDbusToRedfish(
                                                                            *severity);
                                                                    thisEntry["Created"] =
                                                                        redfish::time_utils::
                                                                            getDateTimeStdtime(
                                                                                timestamp);
                                                                    thisEntry["Modified"] =
                                                                        redfish::time_utils::
                                                                            getDateTimeStdtime(
                                                                                updateTimestamp);
                                                                }
                                                                if (filePath !=
                                                                    nullptr)
                                                                {
                                                                    thisEntry["AdditionalDataURI"] =
                                                                        "/redfish/v1/Systems/" PLATFORMSYSTEMID
                                                                        "/LogServices/"
                                                                        "EventLog/"
                                                                        "Entries/" +
                                                                        std::to_string(
                                                                            *id) +
                                                                        "/attachment";
                                                                }
                                                                entriesArray
                                                                    .push_back(
                                                                        thisEntry);
                                                                asyncResp->res.jsonValue
                                                                    ["Members@odata.count"] =
                                                                    entriesArray
                                                                        .size();
                                                            }
                                                        }
                                                    }
                                                    std::sort(
                                                        entriesArray.begin(),
                                                        entriesArray.end(),
                                                        [](const nlohmann::json&
                                                               left,
                                                           const nlohmann::json&
                                                               right) {
                                                            return (
                                                                left["Id"] <=
                                                                right["Id"]);
                                                        });
                                                },
                                                "xyz.openbmc_project.Logging",
                                                "/xyz/openbmc_project/logging",
                                                "xyz.openbmc_project.Logging.Namespace",
                                                "GetAll", chassisName + "_XID",
                                                "xyz.openbmc_project.Logging.Namespace.ResolvedFilterType.Both");
                                    return;
                                });
                            return;
                        }
                    }
                    messages::resourceNotFound(
                        asyncResp->res, "#Chassis.v1_15_0.Chassis", chassisId);
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
static std::vector<std::string> debugTokenData;

inline void requestRoutesDebugToken(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/DebugTokenService")
        .privileges(redfish::privileges::getLogEntry)
        .methods(
            boost::beast::http::verb::
                get)([](const crow::Request&,
                        const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
            asyncResp->res.jsonValue["@odata.id"] =
                "/redfish/v1/Systems/" PLATFORMSYSTEMID
                "/LogServices/DebugTokenService";
            asyncResp->res.jsonValue["@odata.type"] =
                "#LogService.v1_2_0.LogService";
            asyncResp->res.jsonValue["Name"] = "Debug token service";
            asyncResp->res.jsonValue["Description"] = "Debug token service";
            asyncResp->res.jsonValue["Id"] = "DebugTokenService";

            std::pair<std::string, std::string> redfishDateTimeOffset =
                redfish::time_utils::getDateTimeOffsetNow();
            asyncResp->res.jsonValue["DateTime"] = redfishDateTimeOffset.first;
            asyncResp->res.jsonValue["DateTimeLocalOffset"] =
                redfishDateTimeOffset.second;
            asyncResp->res.jsonValue["Actions"] = {
                {"#LogService.CollectDiagnosticData",
                 {{"target",
                   "/redfish/v1/Systems/" PLATFORMSYSTEMID
                   "/LogServices/DebugTokenService/CollectDiagnosticData"}}}};
        });
}

inline void requestRoutesDebugTokenServiceDiagnosticDataCollect(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/DebugTokenService/CollectDiagnosticData")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::post)(
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                std::string diagnosticDataType;
                std::string oemDiagnosticDataType;
                if (!redfish::json_util::readJsonAction(
                        req, asyncResp->res, "DiagnosticDataType",
                        diagnosticDataType, "OEMDiagnosticDataType",
                        oemDiagnosticDataType))
                {
                    return;
                }

                if (diagnosticDataType != "OEM")
                {
                    BMCWEB_LOG_ERROR << "Only OEM DiagnosticDataType supported "
                                        "for DebugTokenService";
                    messages::actionParameterValueFormatError(
                        asyncResp->res, diagnosticDataType,
                        "DiagnosticDataType", "CollectDiagnosticData");
                    return;
                }

                auto callback = [](const std::shared_ptr<task::TaskData>& task,
                                   const std::string& output) {
                    debugTokenData.push_back(output);
                    std::string path =
                        "/redfish/v1/Systems/" PLATFORMSYSTEMID
                        "/LogServices/DebugTokenService/"
                        "DiagnosticData/" +
                        std::to_string(debugTokenData.size() - 1) +
                        "/attachment";
                    std::string location = "Location: " + path;
                    task->payload->httpHeaders.emplace_back(
                        std::move(location));
                };

                if (oemDiagnosticDataType == "DebugTokenStatus")
                {
                    static std::shared_ptr<debug_token::StatusQuery> query;
                    if (!query || !query->isRunning())
                    {
                        query = std::make_shared<debug_token::StatusQuery>(
                            std::move(callback));
                        query->run(req, asyncResp);
                    }
                    else
                    {
                        messages::serviceTemporarilyUnavailable(asyncResp->res,
                                                                "60");
                    }
                }
                else
                {
                    auto index = redfish::debug_token::getMeasurementIndex(
                        oemDiagnosticDataType);
                    if (index >= 0)
                    {
                        static std::shared_ptr<debug_token::Request> request;
                        if (!request || !request->isRunning())
                        {
                            request = std::make_shared<debug_token::Request>(
                                index, std::move(callback));
                            request->run(req, asyncResp);
                        }
                        else
                        {
                            messages::serviceTemporarilyUnavailable(
                                asyncResp->res, "60");
                        }
                    }
                    else
                    {

                        BMCWEB_LOG_ERROR
                            << "Unsupported OEMDiagnosticDataType: "
                            << oemDiagnosticDataType;
                        messages::actionParameterValueFormatError(
                            asyncResp->res, oemDiagnosticDataType,
                            "OEMDiagnosticDataType", "CollectDiagnosticData");
                    }
                }
            });
}

inline void requestRoutesDebugTokenServiceDiagnosticDataEntryDownload(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Systems/" PLATFORMSYSTEMID
                      "/LogServices/DebugTokenService"
                      "/DiagnosticData/<uint>/attachment")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               const uint32_t& id) {
                std::string_view accept = req.getHeaderValue("Accept");
                if (!accept.empty() &&
                    !http_helpers::isContentTypeAllowed(
                        req.getHeaderValue("Accept"),
                        http_helpers::ContentType::OctetStream, true))
                {
                    asyncResp->res.result(
                        boost::beast::http::status::bad_request);
                    return;
                }
                auto dataCount = debugTokenData.size();
                if (dataCount == 0 || id > dataCount - 1)
                {
                    messages::resourceMissingAtURI(
                        asyncResp->res,
                        crow::utility::urlFromPieces(
                            "redfish", "v1", "Systems", PLATFORMSYSTEMID,
                            "LogServices", "DebugTokenService",
                            "DiagnosticData", std::to_string(id),
                            "attachment"));
                    asyncResp->res.result(
                        boost::beast::http::status::not_found);
                    return;
                }

                asyncResp->res.addHeader("Content-Type",
                                         "application/octet-stream");
                asyncResp->res.addHeader("Content-Transfer-Encoding", "Binary");
                asyncResp->res.body() = debugTokenData[id];
            });
}

} // namespace redfish
