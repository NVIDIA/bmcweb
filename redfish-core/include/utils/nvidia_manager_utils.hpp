#include "utils/time_utils.hpp"
#include "nsm_cmd_support.hpp"

#include <async_resp.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <utils/chassis_utils.hpp>
namespace redfish
{

namespace nvidia_manager_util
{
/**
 * @brief Retrieves telemetry ready state data over DBus
 *
 * @param[in] aResp Shared pointer for completing asynchronous calls
 * @param[in] connectionName - service name
 * @param[in] path - object path
 * @return none
 */
inline void getOemManagerState(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                               const std::string& connectionName,
                               const std::string& path)
{
    BMCWEB_LOG_DEBUG("Get manager service Telemetry state.");
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec,
                const std::vector<std::pair<
                    std::string, std::variant<std::string>>>& propertiesList) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("Error in getting manager service state");
            return;
        }
        for (const std::pair<std::string, std::variant<std::string>>& property :
             propertiesList)
        {
            if (property.first == "FeatureType")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("nullptr while reading FeatureType");
                    messages::internalError(aResp->res);
                    return;
                }
                if (*value ==
                    "xyz.openbmc_project.State.FeatureReady.FeatureTypes.Manager")
                {
                    for (const std::pair<std::string,
                                         std::variant<std::string>>&
                             propertyItr : propertiesList)
                    {
                        if (propertyItr.first == "State")
                        {
                            const std::string* stateValue =
                                std::get_if<std::string>(&propertyItr.second);
                            if (stateValue == nullptr)
                            {
                                BMCWEB_LOG_DEBUG(
                                    "Null value returned for manager service state");
                                messages::internalError(aResp->res);
                                return;
                            }
                            std::string state = redfish::chassis_utils::
                                getFeatureReadyStateType(*stateValue);
                            aResp->res.jsonValue["Status"]["State"] = state;
                            if (state == "Enabled")
                            {
                                aResp->res.jsonValue["Status"]["Health"] = "OK";
                            }
                            else
                            {
                                aResp->res.jsonValue["Status"]["Health"] =
                                    "Critical";
                            }
                        }
                    }
                }
            }
        }
    },
        connectionName, path, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.State.FeatureReady");
}

inline void
    getOemReadyState(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& bmcId)
{
    // call to get telemtery Ready status
    crow::connections::systemBus->async_method_call(
        [asyncResp, bmcId](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
        if (ec)
        {
            // if platform doesn't support FeatureReady iface then report state
            // based on upstream only no failure reported
            return;
        }
        if (!subtree.empty())
        {
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
                const std::vector<std::string>& interfaces =
                    connectionNames[0].second;
                for (const auto& interfaceName : interfaces)
                {
                    if (interfaceName == "xyz.openbmc_project.State."
                                         "FeatureReady")
                    {
                        getOemManagerState(asyncResp, connectionName, path);
                    }
                }
            }
            return;
        }
        BMCWEB_LOG_ERROR(
            "Could not find interface xyz.openbmc_project.State.FeatureReady");
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", "/", int32_t(0),
        std::array<const char*, 1>{"xyz.openbmc_project.State."
                                   "FeatureReady"});
}

template <typename Callback>
inline void isServiceActive(boost::system::error_code& ec1,
                            std::variant<std::string>& property1,
                            const std::string_view& unit, Callback&& callbackIn)
{
    if (ec1)
    {
        BMCWEB_LOG_WARNING("No OpenOCD service");
        return;
    }
    std::string* loadState = std::get_if<std::string>(&property1);
    if (*loadState == "loaded")
    {
        crow::connections::systemBus->async_method_call(
            [callback{std::forward<Callback>(callbackIn)}](
                boost::system::error_code& ec2,
                std::variant<std::string>& property2) {
            callback(ec2, property2);
        },
            "org.freedesktop.systemd1",
            sdbusplus::message::object_path("/org/freedesktop/systemd1/unit") /=
            unit,
            "org.freedesktop.DBus.Properties", "Get",
            "org.freedesktop.systemd1.Unit", "ActiveState");
    }
}

template <typename Callback>
inline void isLoaded(const std::string_view& unit, Callback&& callbackIn)
{
    crow::connections::systemBus->async_method_call(
        [unit, callback{std::forward<Callback>(callbackIn)}](
            boost::system::error_code& ec,
            std::variant<std::string>& property) {
        isServiceActive(ec, property, unit, callback);
    },
        "org.freedesktop.systemd1",
        sdbusplus::message::object_path("/org/freedesktop/systemd1/unit") /=
        unit,
        "org.freedesktop.DBus.Properties", "Get",
        "org.freedesktop.systemd1.Unit", "LoadState");
}

inline void
    getOemNvidiaOpenOCD(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    isLoaded("openocdon_2eservice",
             [asyncResp](boost::system::error_code& ec,
                         std::variant<std::string>& property) {
        if (ec)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        std::string* serviceStatus = std::get_if<std::string>(&property);
        if (*serviceStatus == "active")
        {
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["OpenOCD"]["Status"]
                                    ["State"] = "Enabled";
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["OpenOCD"]["Enable"] =
                true;
        }
        else
        {
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["OpenOCD"]["Status"]
                                    ["State"] = "Disabled";
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["OpenOCD"]["Enable"] =
                false;
        }
    });
}

inline void setOemNvidiaOpenOCD(const bool value)
{
    if (value)
    {
        dbus::utility::systemdRestartUnit("openocdon_2eservice", "replace");
    }
    else
    {
        dbus::utility::systemdRestartUnit("openocdoff_2etarget", "replace");
    }
}

inline std::string getFMState(const std::string& fmStateType)
{
    if (fmStateType ==
        "com.nvidia.State.FabricManager.FabricManagerState.Offline")
    {
        return "Offline";
    }
    if (fmStateType ==
        "com.nvidia.State.FabricManager.FabricManagerState.Standby")
    {
        return "Standby";
    }
    if (fmStateType ==
        "com.nvidia.State.FabricManager.FabricManagerState.Configured")
    {
        return "Configured";
    }
    if (fmStateType ==
        "com.nvidia.State.FabricManager.FabricManagerState.Error")
    {
        return "Error";
    }
    // Unknown or others
    return "Unknown";
}

inline std::string getFMReportStatus(const std::string& fmReportStatusType)
{
    if (fmReportStatusType ==
        "com.nvidia.State.FabricManager.FabricManagerReportStatus.NotReceived")
    {
        return "Pending";
    }
    if (fmReportStatusType ==
        "com.nvidia.State.FabricManager.FabricManagerReportStatus.Received")
    {
        return "Received";
    }
    if (fmReportStatusType ==
        "com.nvidia.State.FabricManager.FabricManagerReportStatus.Timeout")
    {
        return "Timeout";
    }
    // Unknown or others
    return "Unknown";
}

/**
 * @brief Retrieves fabric manager state info from DBus
 *
 * @param[in] aResp Shared pointer for completing asynchronous calls
 * @param[in] connectionName - service name
 * @param[in] path - object path
 * @return none
 */
inline void
    getFabricManagerInformation(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                                const std::string& connectionName,
                                const std::string& path)
{
    BMCWEB_LOG_DEBUG("Get fabric manager state information.");

    crow::connections::systemBus->async_method_call(
        [aResp](
            const boost::system::error_code ec,
            const std::vector<
                std::pair<std::string, std::variant<std::string, uint64_t>>>&
                propertiesList) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Error in getting fabric manager state info");
            messages::internalError(aResp->res);
            return;
        }

        auto addOemNvidiaOdataType = false;
        for (const std::pair<std::string, std::variant<std::string, uint64_t>>&
                 property : propertiesList)
        {
            if (property.first == "FMState")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for FM state");
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["Oem"]["Nvidia"]["FabricManagerState"] =
                    getFMState(*value);
                addOemNvidiaOdataType = true;
            }
            else if (property.first == "ReportStatus")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for Report Status");
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["Oem"]["Nvidia"]["ReportStatus"] =
                    getFMReportStatus(*value);
                addOemNvidiaOdataType = true;
            }
            else if (property.first == "LastRestartDuration")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for Duration Since LastRestart");
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["Oem"]["Nvidia"]
                                    ["DurationSinceLastRestartSeconds"] =
                    *value;
                addOemNvidiaOdataType = true;
            }
            else if (property.first == "LastRestartTime")
            {
                const uint64_t* value = std::get_if<uint64_t>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for Time Since LastRestart");
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["LastResetTime"] =
                    redfish::time_utils::getDateTimeUint(*value);
            }
        }
        if (addOemNvidiaOdataType)
        {
            aResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
                "#NvidiaManager.v1_4_0.NvidiaFabricManager";
        }
    },
        connectionName, path, "org.freedesktop.DBus.Properties", "GetAll",
        "com.nvidia.State.FabricManager");
}

inline void requestRouteNSMRawCommand(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Actions/Oem/NvidiaManager.NSMRawCommand/")
        .privileges(redfish::privileges::postManager)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& bmcId) {
        uint8_t deviceIdentificationId = 0;
        uint8_t deviceInstanceId = 0;
        bool isLongRunning = false;
        uint8_t messageType = 0;
        uint8_t commandCode = 0;
        uint16_t dataSizeInBytes = 0;
        std::vector<uint8_t> data;
        std::optional<bool> optionalIsLongRunning = false;
        std::optional<std::vector<uint8_t>> optionalData =
            std::vector<uint8_t>();

        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            BMCWEB_LOG_ERROR("Failed to set up Redfish route.");
            return;
        }

        if (bmcId != BMCWEB_REDFISH_MANAGER_URI_NAME)
        {
            BMCWEB_LOG_ERROR("Invalid BMC ID '{}'", bmcId);
            messages::resourceNotFound(
                asyncResp->res, "#NvidiaManager.v1_2_0.NvidiaManager", bmcId);
            return;
        }

        // clang-format off
        if (!redfish::json_util::readJsonAction(
                req, asyncResp->res,
                "CommandCode", commandCode,
                "Data", optionalData,
                "DataSizeBytes", dataSizeInBytes,
                "DeviceIdentificationId", deviceIdentificationId,
                "DeviceInstanceId", deviceInstanceId,
                "IsLongRunning", optionalIsLongRunning,
                "MessageType", messageType
                ))
        {
            BMCWEB_LOG_ERROR("Failed to parse JSON body.");
            return;
        }
        // clang-format on

        isLongRunning = optionalIsLongRunning.value_or(false);
        data = optionalData.value_or(std::vector<uint8_t>());

        getMatchingFruDeviceObjectPath(
            deviceIdentificationId, deviceInstanceId, asyncResp,
            [asyncResp, isLongRunning, messageType, commandCode,
             data](std::string objectPath) mutable {
            if (objectPath.empty())
            {
                BMCWEB_LOG_ERROR("No matching FruDevice found.");
                messages::resourceTypeIncompatible(
                    asyncResp->res, "#NvidiaManager.v1_2_0.NvidiaManager",
                    "deviceIdentificationId");
                return;
            }

            BMCWEB_LOG_DEBUG("Found FruDevice objectPath='{}'", objectPath);

            MemoryFileDescriptor memfd("nsm_command_data");
            if (memfd.fd == -1)
            {
                BMCWEB_LOG_ERROR("Failed to create NSM req memfd");
                messages::internalError(asyncResp->res);
                return;
            }
            if (write(memfd.fd, data.data(), data.size()) !=
                static_cast<ssize_t>(data.size()))
            {
                BMCWEB_LOG_ERROR("Failed to write to NSM req memfd");
                messages::internalError(asyncResp->res);
                return;
            }
            if (!memfd.rewind())
            {
                messages::internalError(asyncResp->res);
                return;
            }

            callSendNSMRawCommand(asyncResp, memfd, objectPath, messageType,
                                  commandCode, isLongRunning);
        });
    });
}

inline void requestRouteNSMRawCommandActionInfo(App& app)
{
    BMCWEB_ROUTE(
        app, "/redfish/v1/Managers/<str>/Oem/Nvidia/NSMRawCommandActionInfo/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& bmcId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        // Process non-BMC service manager
        if (bmcId != BMCWEB_REDFISH_MANAGER_URI_NAME)
        {
            messages::resourceNotFound(asyncResp->res,
                                       "#Manager.v1_11_0.Manager", bmcId);
            return;
        }
        asyncResp->res.jsonValue["@odata.type"] =
            "#ActionInfo.v1_1_2.ActionInfo";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Managers/" + bmcId +
            "/Oem/Nvidia/NSMRawCommandActionInfo";
        asyncResp->res.jsonValue["Name"] = "NSMRawCommand Action Info";
        asyncResp->res.jsonValue["Id"] = "NSMRawCommandActionInfo";

        nlohmann::json parameters = nlohmann::json::array();

        nlohmann::json param1;
        param1["Name"] = "DeviceIdentificationId";
        param1["Required"] = true;
        param1["DataType"] = "Number";
        param1["AllowableValues"] = {0, 1, 2, 3, 4};
        parameters.push_back(param1);

        nlohmann::json param2;
        param2["Name"] = "DeviceInstanceId";
        param2["Required"] = true;
        param2["DataType"] = "Number";
        parameters.push_back(param2);

        nlohmann::json param3;
        param3["Name"] = "IsLongRunning";
        param3["Required"] = false;
        param3["DataType"] = "Boolean";
        parameters.push_back(param3);

        nlohmann::json param4;
        param4["Name"] = "MessageType";
        param4["Required"] = true;
        param4["DataType"] = "Number";
        parameters.push_back(param4);

        nlohmann::json param5;
        param5["Name"] = "CommandCode";
        param5["Required"] = true;
        param5["DataType"] = "Number";
        parameters.push_back(param5);

        nlohmann::json param6;
        param6["Name"] = "DataSizeBytes";
        param6["Required"] = true;
        param6["DataType"] = "Number";
        param6["MinimumValue"] = 0;
        param6["MaximumValue"] = 255;
        parameters.push_back(param6);

        nlohmann::json param7;
        param7["Name"] = "Data";
        param7["Required"] = false;
        param7["DataType"] = "NumberArray";
        param7["MinimumValue"] = 0;
        param7["MaximumValue"] = 255;
        parameters.push_back(param7);

        // Assign the constructed parameters to the response
        asyncResp->res.jsonValue["Parameters"] = parameters;
    });
}

} // namespace nvidia_manager_util
} // namespace redfish
