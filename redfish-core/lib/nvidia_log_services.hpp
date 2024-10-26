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
#include "nvidia_cper_util.hpp"
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

namespace message_registries
{

static void generateMessageRegistry(
    nlohmann::json& logEntry,
    const std::string& odataId, /* e.g. /redfish/v1/Systems/system/LogServices/"
                                  "EventLog/Entries/ */
    const std::string& odataTypeVer /* e.g. v1_13_0 */, const std::string& id,
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
                {"Resolution", res},
                {"Resolved", resolved}};
}

} // namespace message_registries

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

                            if constexpr (BMCWEB_NVIDIA_OEM_LOGSERVICES)
                            {
                                const std::vector<std::pair<
                                    std::string, std::vector<std::string>>>&
                                    connectionNames = object.second;
                                const std::string& connectionName =
                                    connectionNames[0].first;

                                BMCWEB_LOG_DEBUG(
                                    "XID Looking for PrettyName on service {} path {}",
                                    connectionName, path);
                                sdbusplus::asio::getProperty<std::string>(
                                    *crow::connections::systemBus,
                                    connectionName, path,
                                    "xyz.openbmc_project.Inventory.Item",
                                    "PrettyName",
                                    [asyncResp,
                                     chassisId(std::string(chassisId))](
                                        const boost::system::error_code ec,
                                        const std::string& chassisName) {
                                        if (!ec)
                                        {
                                            BMCWEB_LOG_DEBUG(
                                                "XID Looking for Namespace on {}_XID",
                                                chassisName);
                                            crow::connections::systemBus->async_method_call(
                                                [asyncResp,
                                                 chassisId(
                                                     std::string(chassisId))](
                                                    const boost::system::
                                                        error_code ec,
                                                    const std::tuple<
                                                        uint32_t,
                                                        uint64_t>& /*reqData*/) {
                                                    if (!ec)
                                                    {
                                                        nlohmann::json&
                                                            logServiceArray =
                                                                asyncResp->res.jsonValue
                                                                    ["Members"];
                                                        logServiceArray.push_back(
                                                            {{"@odata.id",
                                                              "/redfish/v1/Chassis/" +
                                                                  chassisId +
                                                                  "/LogServices/XID"}});
                                                        asyncResp->res.jsonValue
                                                            ["Members@odata.count"] =
                                                            logServiceArray
                                                                .size();
                                                    }
                                                },
                                                "xyz.openbmc_project.Logging",
                                                "/xyz/openbmc_project/logging",
                                                "xyz.openbmc_project.Logging.Namespace",
                                                "GetStats",
                                                chassisName + "_XID");
                                        }
                                    });
                            } // BMCWEB_NVIDIA_OEM_LOGSERVICES

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
                    {"@odata.id",
                     "/redfish/v1/Systems/" +
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


inline void handleLogServicesDumpServiceComputerSystemPatch(
    crow::App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    [[maybe_unused]] const std::string& systemName)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }


    // Nvidia OEM code 
    std::optional<bool> retimerDebugModeEnabled;
    if (!json_util::readJsonPatch(req, asyncResp->res,
                             "Oem/Nvidia/RetimerDebugModeEnabled",
                             retimerDebugModeEnabled))
    {
        return;
    }

    if (retimerDebugModeEnabled)
    {
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
    }
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
                asyncResp->res.jsonValue["Id"] =
                    "CollectDiagnosticDataActionInfo";

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
                asyncResp->res.jsonValue["Id"] =
                    "CollectDiagnosticDataActionInfo";

                nlohmann::json::object_t parameter_diagnosticDataType;
                parameter_diagnosticDataType["Name"] = "DiagnosticDataType";
                parameter_diagnosticDataType["Required"] = true;
                parameter_diagnosticDataType["DataType"] = "String";

                nlohmann::json::array_t diagnosticDataType_allowableValues;
                diagnosticDataType_allowableValues.push_back("OEM");
                parameter_diagnosticDataType["AllowableValues"] =
                    std::move(diagnosticDataType_allowableValues);

                nlohmann::json::object_t parameter_OEMDiagnosticDataType;
                parameter_OEMDiagnosticDataType["Name"] =
                    "OEMDiagnosticDataType";
                parameter_OEMDiagnosticDataType["Required"] = true;
                parameter_OEMDiagnosticDataType["DataType"] = "String";

                nlohmann::json::array_t OEMDiagnosticDataType_allowableValues;

                // Get the OEMDiagnosticDataType from meson option to push back
                std::string diagTypeStr = "";
                for (const auto& typeStr : OEM_DIAG_DATA_TYPE_ARRAY)
                {
                    diagTypeStr = std::string("DiagnosticType=") +
                                  std::string(typeStr);
                    OEMDiagnosticDataType_allowableValues.emplace_back(
                        diagTypeStr);
                }

                parameter_OEMDiagnosticDataType["AllowableValues"] =
                    std::move(OEMDiagnosticDataType_allowableValues);

                nlohmann::json::array_t parameters;
                parameters.push_back(std::move(parameter_diagnosticDataType));
                parameters.push_back(
                    std::move(parameter_OEMDiagnosticDataType));

                asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
            });
}

inline void extendSystemLogServicesGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
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
            } /* BMCWEB_NVIDIA_OEM_PROPERTIES */
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["LatestEntryID"] =
                std::to_string(std::get<0>(reqData));
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["LatestEntryTimeStamp"] =
                redfish::time_utils::getDateTimeStdtime(lastTimeStamp);
        },
        "xyz.openbmc_project.Logging", "/xyz/openbmc_project/logging",
        "xyz.openbmc_project.Logging.Namespace", "GetStats", "all");

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        if (BMCWEB_NVIDIA_BOOTENTRYID)
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
    } /* BMCWEB_NVIDIA_OEM_PROPERTIES */
}

inline void extendLogServiceOEMGet(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& dumpType)
{
    if (dumpType == "System")
    {
        if constexpr (BMCWEB_NVIDIA_RETIMER_DEBUGMODE)
        {
            sdbusplus::asio::getProperty<bool>(
                *crow::connections::systemBus, "xyz.openbmc_project.Dump.Manager",
                "/xyz/openbmc_project/dump/retimer",
                "xyz.openbmc_project.Dump.DebugMode", "DebugMode",
                [asyncResp](const boost::system::error_code ec,
                            const bool DebugModeEnabled) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error for RetimerDebugModeEnabled {}",
                            ec);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                        "#NvidiaLogService.v1_2_0.NvidiaLogService";
                    asyncResp->res
                        .jsonValue["Oem"]["Nvidia"]["RetimerDebugModeEnabled"] =
                        DebugModeEnabled;
                });
        } // BMCWEB_NVIDIA_RETIMER_DEBUGMODE    
    }
}

inline void dBusEventLogEntryGetAdditionalInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    DbusEventLogEntry& entry, nlohmann::json& objectToFillOut)
{
    // Determine if it's a message registry format or not.
    bool isMessageRegistry = false;
    std::string messageId;
    std::string messageArgs;
    std::string originOfCondition;
    std::string deviceName;
    nlohmann::json::object_t cper;

    if (entry.additionalDataRaw != nullptr)
    {
        AdditionalData additional(*entry.additionalDataRaw);
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
            originOfCondition = additional["REDFISH_ORIGIN_OF_CONDITION"];
        }
        if (additional.count("DEVICE_NAME") > 0)
        {
            deviceName = additional["DEVICE_NAME"];
        }

        // populate CPER section (checks are in the fn)
        nlohmann::json::object_t oem;
        parseAdditionalDataForCPER(cper, oem, additional);
        // add CPER to entry if it is present
        if (!cper.empty())
        {
            objectToFillOut.update(cper);
        }        
    }

    if (isMessageRegistry)
    {
        message_registries::generateMessageRegistry(
            objectToFillOut,
            "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                "/LogServices/"
                "EventLog/Entries/",
            "v1_15_0", std::to_string(entry.id), "System Event Log Entry",
            redfish::time_utils::getDateTimeStdtime(
                redfish::time_utils::getTimestamp(entry.timestamp)),
            messageId, messageArgs, *entry.resolution, entry.resolved,
            (entry.eventId == nullptr) ? "" : *entry.eventId, deviceName, *entry.severity);

        if constexpr (!BMCWEB_DISABLE_HEALTH_ROLLUP)
        {
            origin_utils::convertDbusObjectToOriginOfCondition(
                originOfCondition, std::to_string(entry.id), asyncResp,
                objectToFillOut, deviceName);
        } // BMCWEB_DISABLE_HEALTH_ROLLUP
    }

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
            objectToFillOut.update(oem);
        }
    } // BMCWEB_NVIDIA_OEM_PROPERTIES
}

inline std::vector<std::pair<std::string, std::variant<std::string, uint64_t>>>
    parseOEMAdditionalData(std::string& oemData)
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
                if (subTokens[0] == "DiagnosticType")
                {
                    // Reassign the oemData to stay value only
                    oemData = subTokens[1];
                }
            }
            else
            {
                // Not be a <key,value> pair so it's invalid
                oemData.clear();
            }
        }
    }
    return additionalData;
}

inline void requestRoutesEventLogServicePatch(App& app)
{
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
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

                    if (!json_util::readJsonPatch(req, asyncResp->res, "Oem",
                                                    oemObject))
                    {
                        return;
                    }

                    std::optional<nlohmann::json> oemNvidiaObject;

                    if (!json_util::readJson(*oemObject, asyncResp->res,
                                                "Nvidia", oemNvidiaObject))
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
                                    BMCWEB_LOG_DEBUG("DBUS response error {}",
                                                        ec);
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                            },
                            "xyz.openbmc_project.Logging",
                            "/xyz/openbmc_project/logging",
                            "org.freedesktop.DBus.Properties", "Set",
                            "xyz.openbmc_project.Logging.Namespace",
                            "AutoClearResolvedLogEnabled",
                            dbus::utility::DbusVariantType(
                                *autoClearResolvedLogEnabled));
                    }
                });
    }
}

}