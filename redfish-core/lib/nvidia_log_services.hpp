#pragma once

#include "app.hpp"
#include "cper_utils.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "generated/enums/log_entry.hpp"
#include "gzfile.hpp"
#include "http_utility.hpp"
#include "human_sort.hpp"
#include "nvidia_dbus_utility.hpp"
#include "nvidia_error_messages.hpp"
#include "nvidia_event_service_manager.hpp"
#include "nvidia_messages.hpp"
#include "query.hpp"
#include "registries.hpp"
#include "registries/base_message_registry.hpp"
#include "registries/openbmc_message_registry.hpp"
#include "registries/privilege_registry.hpp"
#include "task.hpp"
#include "task_messages.hpp"
#include "utils/dbus_event_log_entry.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_time_utils.hpp"
#include "utils/nvidia_utils.hpp"
#include "utils/time_utils.hpp"

#include <systemd/sd-id128.h>
#include <tinyxml2.h>
#include <unistd.h>

#include <boost/beast/http/verb.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/process.hpp>
#include <boost/system/linux_error.hpp>
#include <boost/url/format.hpp>
#include <openbmc_dbus_rest.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/exception.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/message/native_types.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/dbus_log_utils.hpp>
#include <utils/log_services_util.hpp>
#include <utils/origin_utils.hpp>

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
    if (severity.empty())
    {
        sev = msg->messageSeverity;
    }
    else
    {
        sev = translateSeverityDbusToRedfish(severity);
    }

    std::string res(resolution);
    if (res.empty())
    {
        res = msg->resolution;
    }

    // Convert messageArgs string for its json format used later.
    std::vector<std::string> fields;
    fields.reserve(msg->numberOfArgs);
    bmcweb::split(fields, messageArgs, ',');

    // Trim leading and tailing whitespace of each arg.
    for (auto& f : fields)
    {
        redfish::trim(f);
    }
    std::span<std::string> msgArgs;
    msgArgs = {fields.data(), fields.size()};

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

    // Create the new JSON object with message registry format
    nlohmann::json newLogEntry = {
        {"@odata.id", odataId + id},
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

    // Update the existing logEntry with new fields, preserving existing ones
    logEntry.update(newLogEntry);
}

} // namespace message_registries

namespace api_metrics
{
constexpr const char* service = "xyz.openbmc_project.Settings";
constexpr const char* objpath = "/xyz/openbmc_project/logging/bmc_cmd_metrics";
constexpr const char* interface = "xyz.openbmc_project.Object.Enable";
constexpr const char* property = "Enabled";

inline bool& getEnabled()
{
    static bool enabled = true;
    return enabled;
}

inline void handleInitProperty(const boost::system::error_code& ec,
                               bool enabled)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR(
            "Failed to get API Metrics setting, using default=enabled: {}",
            ec.message());
        return;
    }
    getEnabled() = enabled;
}

inline void handleGetProperty(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, bool enabled)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Failed to get API Metrics setting via D-Bus: {}",
                         ec.message());
        messages::resourceNotFound(asyncResp->res, "Property",
                                   "ApiMetricsEnabled");
        return;
    }
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
        "#NvidiaLogService.v1_4_0.NvidiaLogService";
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["ApiMetricsEnabled"] = enabled;
}

inline void handleSetProperty(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Failed to set API Metrics setting via D-Bus: {}",
                         ec.message());
        messages::resourceNotFound(asyncResp->res, "Property",
                                   "ApiMetricsEnabled");
        return;
    }
    asyncResp->res.result(boost::beast::http::status::no_content);
}

inline void onPropertyChanged(sdbusplus::message_t& msg)
{
    std::string iface;
    dbus::utility::DBusPropertiesMap propertiesMap;
    msg.read(iface, propertiesMap);

    auto it = std::ranges::find_if(propertiesMap, [](const auto& x) {
        return x.first == property;
    });

    if (it != propertiesMap.end())
    {
        const bool* enabled = std::get_if<bool>(&it->second);
        if (enabled == nullptr)
        {
            BMCWEB_LOG_ERROR("API Metrics D-Bus property '{}' is not a bool",
                             property);
            return;
        }
        getEnabled() = *enabled;
        BMCWEB_LOG_DEBUG("API Metrics: {}", *enabled ? "enabled" : "disabled");
    }
}

inline void onServiceStarted(sdbusplus::message_t& msg)
{
    std::string name;
    std::string oldOwner;
    std::string newOwner;
    msg.read(name, oldOwner, newOwner);

    if (!newOwner.empty())
    {
        sdbusplus::asio::getProperty<bool>(*crow::connections::systemBus,
                                           service, objpath, interface,
                                           property, handleInitProperty);
    }
}

inline void registerApiMetricsSignal()
{
    BMCWEB_LOG_INFO("Register API Metrics PropertiesChanged Signal");

    // Monitor property changes
    static std::unique_ptr<sdbusplus::bus::match_t> apiMetricsMatch =
        std::make_unique<sdbusplus::bus::match_t>(
            *crow::connections::systemBus,
            sdbusplus::bus::match::rules::propertiesChanged(objpath, interface),
            onPropertyChanged);

    // Monitor service start/restart
    static std::unique_ptr<sdbusplus::bus::match_t> serviceMonitor =
        std::make_unique<sdbusplus::bus::match_t>(
            *crow::connections::systemBus,
            sdbusplus::bus::match::rules::nameOwnerChanged(service),
            onServiceStarted);

    // Get initial value after match is set up
    sdbusplus::asio::getProperty<bool>(*crow::connections::systemBus, service,
                                       objpath, interface, property,
                                       handleInitProperty);
}

} // namespace api_metrics

inline void requestRoutesChassisLogServiceCollection(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */
    BMCWEB_ROUTE(app, "/redfish/v1/Chassis/<str>/LogServices/")
        .privileges(redfish::privileges::getLogServiceCollection)
        .methods(
            boost::beast::http::verb::
                get)([&app](const crow::Request& req,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& chassisId) {
            if (!redfish::setUpRedfishRoute(app, req, asyncResp))
            {
                return;
            }
            const std::array<const char*, 2> interfaces = {
                "xyz.openbmc_project.Inventory.Item.Board",
                "xyz.openbmc_project.Inventory.Item.Chassis"};

            crow::connections::systemBus->async_method_call(
                [asyncResp, chassisId(std::string(chassisId))](
                    const boost::system::error_code& ec,
                    const dbus::utility::GetSubTreeType& subtree) {
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
                                *crow::connections::systemBus, connectionName,
                                path, "xyz.openbmc_project.Inventory.Item",
                                "PrettyName",
                                [asyncResp, chassisId(std::string(chassisId))](
                                    const boost::system::error_code& ec2,
                                    const std::string& chassisName) {
                                    if (!ec2)
                                    {
                                        BMCWEB_LOG_DEBUG(
                                            "XID Looking for Namespace on {}_XID",
                                            chassisName);
                                        crow::connections::systemBus->async_method_call(
                                            [asyncResp,
                                             chassisId(std::string(chassisId))](
                                                const boost::system::error_code&
                                                    ec3,
                                                const std::tuple<
                                                    uint32_t,
                                                    uint64_t>& /*reqData*/) {
                                                if (!ec3)
                                                {
                                                    nlohmann::json& logArray =
                                                        asyncResp->res.jsonValue
                                                            ["Members"];
                                                    logArray.push_back(
                                                        {{"@odata.id",
                                                          "/redfish/v1/Chassis/" +
                                                              chassisId +
                                                              "/LogServices/XID"}});
                                                    asyncResp->res.jsonValue
                                                        ["Members@odata.count"] =
                                                        logArray.size();
                                                }
                                            },
                                            "xyz.openbmc_project.Logging",
                                            "/xyz/openbmc_project/logging",
                                            "xyz.openbmc_project.Logging.Namespace",
                                            "GetStats", chassisName + "_XID");
                                    }
                                });
                        } // BMCWEB_NVIDIA_OEM_LOGSERVICES

                        asyncResp->res.jsonValue["Members@odata.count"] =
                            logServiceArray.size();
                        return;
                    }
                    // Couldn't find an object with that name. Return an error
                    messages::resourceNotFound(
                        asyncResp->res, "#Chassis.v1_17_0.Chassis", chassisId);
                },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/inventory", 0, interfaces);
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
                    BMCWEB_LOG_ERROR(
                        "DBUS response error DebugMode setProperty {}", ec);
                    messages::internalError(asyncResp->res);
                    return;
                }
            });
        messages::success(asyncResp->res);
    }
}

inline void handleLogServicesDumpServicePatch(
    crow::App& app, const crow::Request& req,
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

    std::optional<bool> apiMetricsEnabled;
    if (!json_util::readJsonPatch(req, asyncResp->res,
                                  "Oem/Nvidia/ApiMetricsEnabled",
                                  apiMetricsEnabled))
    {
        return;
    }

    if (apiMetricsEnabled)
    {
        sdbusplus::asio::setProperty(
            *crow::connections::systemBus, api_metrics::service,
            api_metrics::objpath, api_metrics::interface, api_metrics::property,
            *apiMetricsEnabled,
            [asyncResp](const boost::system::error_code& ec) {
                api_metrics::handleSetProperty(asyncResp, ec);
            });
    }
}

// Extension function to parse NVIDIA-specific dump entry properties from DBus
inline void parseNvidiaDumpEntryFromDbusObject(
    const dbus::utility::ManagedObjectType::value_type& object, uint64_t& size,
    std::string& faultLogDiagnosticDataType, std::string& notificationType,
    std::string& sectionType, std::string& fruid, std::string& severity,
    std::string& nvipSignature, std::string& nvSeverity,
    std::string& nvSocketNumber, std::string& pcieVendorID,
    std::string& pcieDeviceID, std::string& pcieClassCode,
    std::string& pcieFunctionNumber, std::string& pcieDeviceNumber,
    std::string& pcieSegmentNumber, std::string& pcieDeviceBusNumber,
    std::string& pcieSecondaryBusNumber, std::string& pcieSlotNumber)
{
    for (const auto& interfaceMap : object.second)
    {
        if (interfaceMap.first == "xyz.openbmc_project.FDR.Entry")
        {
            for (const auto& propertyMap : interfaceMap.second)
            {
                if (propertyMap.first == "Size")
                {
                    const auto* sizePtr =
                        std::get_if<uint64_t>(&propertyMap.second);
                    if (sizePtr == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Failed to get FDR Size property");
                        break;
                    }
                    size = *sizePtr;
                    break;
                }
            }
        }
        else if (interfaceMap.first ==
                 "xyz.openbmc_project.Dump.Entry.FaultLog")
        {
            const std::string* type = nullptr;
            const std::string* additionalTypeName = nullptr;
            for (const auto& propertyMap : interfaceMap.second)
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

            for (const auto& propertyMap : interfaceMap.second)
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
    }
}

// Extension function for dump entry NVIDIA CPER properties
inline void extendDumpEntryWithCPERProperties(
    nlohmann::json& jsonEntry, const std::string& dumpType,
    const std::string& sectionType, const std::string& fruid,
    const std::string& severity, const std::string& nvipSignature,
    const std::string& nvSeverity, const std::string& nvSocketNumber,
    const std::string& pcieVendorID, const std::string& pcieDeviceID,
    const std::string& pcieClassCode, const std::string& pcieFunctionNumber,
    const std::string& pcieDeviceNumber, const std::string& pcieSegmentNumber,
    const std::string& pcieDeviceBusNumber,
    const std::string& pcieSecondaryBusNumber,
    const std::string& pcieSlotNumber)
{
    if (dumpType == "FaultLog")
    {
        // CPER Oem properties
        jsonEntry["CPER"]["Oem"]["Nvidia"]["@odata.type"] =
            "#NvidiaLogEntry.v1_0_0.CPER";
        if (sectionType != "NA")
        {
            jsonEntry["CPER"]["Oem"]["Nvidia"]["SectionType"] = sectionType;
        }
        if (fruid != "NA")
        {
            jsonEntry["CPER"]["Oem"]["Nvidia"]["FruID"] = fruid;
        }
        if (severity != "NA")
        {
            jsonEntry["CPER"]["Oem"]["Nvidia"]["Severity"] = severity;
        }
        if (nvipSignature != "NA")
        {
            jsonEntry["CPER"]["Oem"]["Nvidia"]["NvIpSignature"] = nvipSignature;
        }
        if (nvSeverity != "NA")
        {
            jsonEntry["CPER"]["Oem"]["Nvidia"]["NvSeverity"] = nvSeverity;
        }
        if (nvSocketNumber != "NA")
        {
            jsonEntry["CPER"]["Oem"]["Nvidia"]["NvSocketNumber"] =
                nvSocketNumber;
        }
        if (pcieVendorID != "NA")
        {
            jsonEntry["CPER"]["Oem"]["Nvidia"]["PCIeVendorId"] = pcieVendorID;
        }
        if (pcieDeviceID != "NA")
        {
            jsonEntry["CPER"]["Oem"]["Nvidia"]["PCIeDeviceId"] = pcieDeviceID;
        }
        if (pcieClassCode != "NA")
        {
            jsonEntry["CPER"]["Oem"]["Nvidia"]["PCIeClassCode"] = pcieClassCode;
        }
        if (pcieFunctionNumber != "NA")
        {
            jsonEntry["CPER"]["Oem"]["Nvidia"]["PCIeFunctionNumber"] =
                pcieFunctionNumber;
        }
        if (pcieDeviceNumber != "NA")
        {
            jsonEntry["CPER"]["Oem"]["Nvidia"]["PCIeDeviceNumber"] =
                pcieDeviceNumber;
        }
        if (pcieSegmentNumber != "NA")
        {
            jsonEntry["CPER"]["Oem"]["Nvidia"]["PCIeSegmentNumber"] =
                pcieSegmentNumber;
        }
        if (pcieDeviceBusNumber != "NA")
        {
            jsonEntry["CPER"]["Oem"]["Nvidia"]["PCIeDeviceBusNumber"] =
                pcieDeviceBusNumber;
        }
        if (pcieSecondaryBusNumber != "NA")
        {
            jsonEntry["CPER"]["Oem"]["Nvidia"]["PCIeSecondaryBusNumber"] =
                pcieSecondaryBusNumber;
        }
        if (pcieSlotNumber != "NA")
        {
            jsonEntry["CPER"]["Oem"]["Nvidia"]["PCIeSlotNumber"] =
                pcieSlotNumber;
        }
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

                nlohmann::json::object_t parameterDiagnosticDataType;
                parameterDiagnosticDataType["Name"] = "DiagnosticDataType";
                parameterDiagnosticDataType["Required"] = true;
                parameterDiagnosticDataType["DataType"] = "String";

                nlohmann::json::array_t diagnosticDataTypeAllowableValues;
                diagnosticDataTypeAllowableValues.emplace_back("Manager");
                parameterDiagnosticDataType["AllowableValues"] =
                    std::move(diagnosticDataTypeAllowableValues);

                nlohmann::json::array_t parameters;
                parameters.emplace_back(std::move(parameterDiagnosticDataType));

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

                // Get the OEM AllowableValues from D-Bus
                redfish::getOEMDiagnosticAllowableValues(
                    "System",
                    [asyncResp](
                        const std::vector<std::string>& oemAllowableValues) {
                        nlohmann::json::object_t parameterDiagnosticDataType;
                        parameterDiagnosticDataType["Name"] =
                            "DiagnosticDataType";
                        parameterDiagnosticDataType["Required"] = true;
                        parameterDiagnosticDataType["DataType"] = "String";

                        nlohmann::json::array_t
                            diagnosticDataTypeAllowableValues;
                        diagnosticDataTypeAllowableValues.emplace_back("OEM");
                        parameterDiagnosticDataType["AllowableValues"] =
                            std::move(diagnosticDataTypeAllowableValues);

                        nlohmann::json::object_t parameterOemDiagnosticDataType;
                        parameterOemDiagnosticDataType["Name"] =
                            "OEMDiagnosticDataType";
                        parameterOemDiagnosticDataType["Required"] = true;
                        parameterOemDiagnosticDataType["DataType"] = "String";
                        parameterOemDiagnosticDataType["AllowableValues"] =
                            oemAllowableValues;

                        nlohmann::json::array_t parameters;
                        parameters.emplace_back(
                            std::move(parameterDiagnosticDataType));
                        parameters.emplace_back(
                            std::move(parameterOemDiagnosticDataType));

                        asyncResp->res.jsonValue["Parameters"] =
                            std::move(parameters);
                    });
            });
}

inline void extendSystemLogServicesGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    // Call Phosphor-logging GetStats method to get
    // LatestEntryTimestamp and LatestEntryID
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code& outerEc,
                    const std::tuple<uint32_t, uint64_t>& reqData) {
            if (outerEc)
            {
                BMCWEB_LOG_ERROR(
                    "Failed to get Data from xyz.openbmc_project.Logging GetStats: {}",
                    outerEc);
                messages::internalError(asyncResp->res);
                return;
            }
            auto lastTimeStamp =
                redfish::time_utils::getTimestamp(std::get<1>(reqData));
            if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
            {
                asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                    "#NvidiaLogService.v1_4_0.NvidiaLogService";
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
            [asyncResp](const boost::system::error_code& innerEc,
                        std::variant<bool>& resp) {
                if (innerEc)
                {
                    BMCWEB_LOG_ERROR(
                        "Failed to get Data from xyz.openbmc_project.Logging: {}",
                        innerEc);
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

inline void extendLogServiceOEMGet(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& dumpType)
{
    if (dumpType == "BMC")
    {
        if constexpr (BMCWEB_NVIDIA_API_METRICS)
        {
            sdbusplus::asio::getProperty<bool>(
                *crow::connections::systemBus, api_metrics::service,
                api_metrics::objpath, api_metrics::interface,
                api_metrics::property,
                std::bind_front(api_metrics::handleGetProperty, asyncResp));
        } // BMCWEB_NVIDIA_API_METRICS
    }
    else if (dumpType == "System")
    {
        if constexpr (BMCWEB_NVIDIA_RETIMER_DEBUGMODE)
        {
            sdbusplus::asio::getProperty<bool>(
                *crow::connections::systemBus,
                "xyz.openbmc_project.Dump.Manager",
                "/xyz/openbmc_project/dump/retimer",
                "xyz.openbmc_project.Dump.DebugMode", "DebugMode",
                [asyncResp](const boost::system::error_code ec,
                            const bool debugModeEnabled) {
                    if (ec)
                    {
                        BMCWEB_LOG_ERROR(
                            "DBUS response error for RetimerDebugModeEnabled {}",
                            ec);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                        "#NvidiaLogService.v1_4_0.NvidiaLogService";
                    asyncResp->res
                        .jsonValue["Oem"]["Nvidia"]["RetimerDebugModeEnabled"] =
                        debugModeEnabled;
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
    bool deviceEventData = false;

    if (!entry.AdditionalData.empty())
    {
        AdditionalData additional(entry.AdditionalData);
        if (additional.contains("REDFISH_MESSAGE_ID"))
        {
            isMessageRegistry = true;
            messageId = additional["REDFISH_MESSAGE_ID"];
            BMCWEB_LOG_DEBUG("MessageId: [{}]", messageId);

            if (additional.contains("REDFISH_MESSAGE_ARGS"))
            {
                messageArgs = additional["REDFISH_MESSAGE_ARGS"];
            }
        }
        if (additional.contains("REDFISH_ORIGIN_OF_CONDITION"))
        {
            originOfCondition = additional["REDFISH_ORIGIN_OF_CONDITION"];
        }
        if (additional.contains("DEVICE_NAME"))
        {
            deviceName = additional["DEVICE_NAME"];
        }
        if (additional.contains("DEVICE_EVENT_DATA"))
        {
            deviceEventData = true;
        }
        // populate CPER section (checks are in the fn)
        nlohmann::json::object_t oem;
        parseAdditionalDataForCPER(cper, oem, additional, originOfCondition);
        // add CPER to entry if it is present
        if (!cper.empty())
        {
            objectToFillOut.update(cper);
        }
    }

    if (deviceEventData && (entry.Path != nullptr) && (entry.Id != 0U))
    {
        objectToFillOut["AdditionalDataURI"] =
            getLogEntryAdditionalDataURI(std::to_string(entry.Id));
    }

    if (isMessageRegistry)
    {
        message_registries::generateMessageRegistry(
            objectToFillOut,
            "/redfish/v1/Systems/" +
                std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                "/LogServices/"
                "EventLog/Entries/",
            "v1_15_0", std::to_string(entry.Id), "System Event Log Entry",
            redfish::time_utils::getDateTimeStdtime(
                redfish::time_utils::getTimestamp(entry.Timestamp)),
            messageId, messageArgs, *entry.Resolution, entry.Resolved,
            std::to_string(entry.Id), deviceName, entry.Severity);

        if constexpr (!BMCWEB_DISABLE_HEALTH_ROLLUP)
        {
            origin_utils::convertDbusObjectToOriginOfCondition(
                originOfCondition, std::to_string(entry.Id), asyncResp,
                objectToFillOut, deviceName);
        } // BMCWEB_DISABLE_HEALTH_ROLLUP
    }

    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        if ((entry.Id != 0U) || !deviceName.empty())
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
            if (entry.Id != 0U)
            {
                oem["Oem"]["Nvidia"]["ErrorId"] = std::to_string(entry.Id);
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
    bmcweb::split(tokens, oemData, ';');
    if (!tokens.empty())
    {
        for (auto& token : tokens)
        {
            std::vector<std::string> subTokens;
            bmcweb::split(subTokens, token, '=');
            // Include only <key,value> pair with '=' delimiter
            if (subTokens.size() == 2)
            {
                additionalData.emplace_back(subTokens[0], subTokens[1]);
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

// Forward declarations - functions defined in log_services.hpp
void createDump(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                const crow::Request& req, const std::string& dumpType);
void downloadDumpEntry(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& entryID, const std::string& dumpType);

inline void precheckOemDiagDataTypeAndCreateDump(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const crow::Request& req, const std::string& dumpType)
{
    std::optional<std::string> diagnosticDataType;
    std::optional<std::string> oemDiagnosticDataType;

    if (!redfish::json_util::readJsonAction(
            req, asyncResp->res, "DiagnosticDataType", diagnosticDataType,
            "OEMDiagnosticDataType", oemDiagnosticDataType))
    {
        return;
    }

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
        messages::actionParameterValueError(asyncResp->res,
                                            "DiagnosticDataType",
                                            "LogService.CollectDiagnosticData");
        return;
    }

    redfish::getOEMDiagnosticAllowableValues(
        dumpType, [asyncResp, &req, dumpType, oemDiagnosticDataType](
                      const std::vector<std::string>& oemAllowableValues) {
            // Check the OEMDiagnosticDataType AllowableValues should be the
            // same as our definition
            bool isValid = false;
            if (dumpType == "System")
            {
                isValid = std::find(oemAllowableValues.begin(),
                                    oemAllowableValues.end(),
                                    *oemDiagnosticDataType) !=
                          oemAllowableValues.end();
            }
            else if (dumpType == "FDR")
            {
                std::string oemDataCopy = *oemDiagnosticDataType;
                std::vector<
                    std::pair<std::string, std::variant<std::string, uint64_t>>>
                    createDumpParamVec = parseOEMAdditionalData(oemDataCopy);
                for (const auto& dumpPara : createDumpParamVec)
                {
                    if (dumpPara.first == "DiagnosticType")
                    {
                        const std::string* oemDiagType =
                            std::get_if<std::string>(&dumpPara.second);
                        if (*oemDiagType == "FDR")
                        {
                            isValid = true;
                            break;
                        }
                    }
                }
            }

            if (!isValid)
            {
                BMCWEB_LOG_ERROR("Wrong parameter values passed");
                messages::actionParameterValueError(
                    asyncResp->res, "OEMDiagnosticDataType",
                    "LogService.CollectDiagnosticData");
                return;
            }

            createDump(asyncResp, req, dumpType);
        });
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
                            [asyncResp](const boost::system::error_code& ec) {
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

// NVIDIA-specific file size limits
constexpr long long int maxFileSize()
{
    if constexpr (BMCWEB_REDFISH_FDR_LOG)
    {
        // "The maximum size of FDR dump is 1.5GB
        return 1500 * 1024LL * 1024LL;
    }
    else
    {
        // Arbitrary max size of 20MB to accommodate BMC dumps
        return 20LL * 1024LL * 1024LL;
    }
}

// NVIDIA-specific system dump entry download handler
inline void handleLogServicesSystemDumpEntryDownloadGet(
    crow::App& app, const std::string& dumpType, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& systemId, const std::string& dumpId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    if (systemId != BMCWEB_REDFISH_SYSTEM_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "System", systemId);
        return;
    }
    downloadDumpEntry(asyncResp, dumpId, dumpType);
}

// NVIDIA-specific system dump entry download route registration
inline void requestRoutesSystemDumpEntryDownload(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Systems/<str>/LogServices/Dump/Entries/<str>/attachment/")
        .privileges(redfish::privileges::getLogEntry)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleLogServicesSystemDumpEntryDownloadGet,
                            std::ref(app), "System"));
}

} // namespace redfish
