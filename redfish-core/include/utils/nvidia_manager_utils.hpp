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
#include "dbus_utility.hpp"
#include "debug_token/erase_policy.hpp"
#include "error_messages.hpp"
#include "generated/enums/nvidia_manager.hpp"
#include "nsm_cmd_support.hpp"
#include "utils/time_utils.hpp"

#include <async_resp.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <utils/chassis_utils.hpp>

#include <functional>

namespace redfish
{

namespace nvidia_manager_util
{

static constexpr auto restrictionModePath =
    "/xyz/openbmc_project/control/host0/restriction_mode";
static constexpr auto restrictionModeInterface =
    "xyz.openbmc_project.Control.Security.RestrictionMode";
static constexpr auto restrictionModeProperty = "RestrictionMode";
static constexpr std::array<std::string_view, 1> restrictionModeInterfaceArray =
    {restrictionModeInterface};

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
    using namespace std::chrono_literals;
    uint64_t timeout =
        std::chrono::duration_cast<std::chrono::microseconds>(1s).count();
    crow::connections::systemBus->async_method_call_timed(
        [aResp](const boost::system::error_code ec,
                const std::vector<std::pair<
                    std::string, std::variant<std::string>>>& propertiesList) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Error in getting manager service state");
            aResp->res.jsonValue["Status"]["State"] = "Starting";
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
                                aResp->res.jsonValue["Status"]["State"] =
                                    "Enabled";
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
        timeout, "xyz.openbmc_project.State.FeatureReady");
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
            if (property.first == "Description")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_ERROR("Null value returned "
                                     "for Description");
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue["Description"] = *value;
            }
            else if (property.first == "FMState")
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
        connectionName, path, "org.freedesktop.DBus.Properties", "GetAll", "");
}

inline void
    getNSMRawCommandActions(std::shared_ptr<bmcweb::AsyncResp> asyncResp)
{
    auto& oemNsmRawCommand =
        asyncResp->res
            .jsonValue["Actions"]["Oem"]["#NvidiaManager.NSMRawCommand"];
    oemNsmRawCommand["target"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Actions/Oem/NvidiaManager.NSMRawCommand",
        std::string(BMCWEB_REDFISH_MANAGER_URI_NAME));
    oemNsmRawCommand["@Redfish.ActionInfo"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/NSMRawCommandActionInfo",
        std::string(BMCWEB_REDFISH_MANAGER_URI_NAME));
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
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            BMCWEB_LOG_ERROR("Failed to set up Redfish route.");
            return;
        }

        if (bmcId != BMCWEB_REDFISH_MANAGER_URI_NAME)
        {
            messages::resourceNotFound(asyncResp->res,
                                       "#Manager.v1_11_0.Manager", bmcId);
            return;
        }

        uint8_t deviceIdentificationId = 0, deviceInstanceId = 0,
                messageType = 0, commandCode = 0;
        uint16_t dataSizeInBytes = 0;
        bool isLongRunning = false;
        std::vector<uint8_t> data;

        if (nsm_command_support::parseRequestJson(
                req, asyncResp, commandCode, deviceIdentificationId,
                deviceInstanceId, messageType, isLongRunning, dataSizeInBytes,
                data))
        {
            nsm_command_support::callSendRequest(
                asyncResp, deviceIdentificationId, deviceInstanceId,
                isLongRunning, messageType, commandCode, data);
        }
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

        if (bmcId != BMCWEB_REDFISH_MANAGER_URI_NAME)
        {
            messages::resourceNotFound(asyncResp->res,
                                       "#Manager.v1_11_0.Manager", bmcId);
            return;
        }

        nsm_command_support::actionInfoResponse(asyncResp, bmcId);
    });
}

/**
 * @brief Manager Oem/Nvidia/DebugTokenManagement GET handler
 */
inline void debugTokenManagementGetHandler(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "#Manager.v1_11_0.Manager",
                                   managerId);
        return;
    }
    asyncResp->res.jsonValue["@odata.type"] =
        "#NvidiaDebugTokenManagement.v1_0_0.NvidiaDebugTokenManagement";
    asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
        "/redfish/v1/Managers/{}/Oem/Nvidia/DebugTokenManagement", managerId);
    asyncResp->res.jsonValue["Id"] = "DebugTokenManagement";
    asyncResp->res.jsonValue["Name"] = managerId +
                                       " Oem Nvidia DebugTokenManagement";
    debug_token::getErasePolicy([asyncResp](std::optional<bool> erasePolicy) {
        if (!erasePolicy)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["AutomaticTokenErased"] = *erasePolicy;
    });
}

/**
 * @brief Manager Oem/Nvidia/DebugTokenManagement PATCH handler
 */
inline void debugTokenManagementPatchHandler(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& managerId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }
    if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
    {
        messages::resourceNotFound(asyncResp->res, "#Manager.v1_11_0.Manager",
                                   managerId);
        return;
    }
    std::optional<bool> automaticTokenErased;
    if (!json_util::readJsonPatch(req, asyncResp->res, "AutomaticTokenErased",
                                  automaticTokenErased))
    {
        return;
    }
    if (!automaticTokenErased)
    {
        messages::propertyMissing(asyncResp->res, "AutomaticTokenErased");
        return;
    }
    debug_token::setErasePolicy(asyncResp, *automaticTokenErased);
}

/**
 * @brief Manager Oem/Nvidia/DebugTokenManagement route handler
 */
inline void requestRoutesDebugTokenManagement(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Managers/<str>/Oem/Nvidia/DebugTokenManagement/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(debugTokenManagementGetHandler, std::ref(app)));
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Managers/<str>/Oem/Nvidia/DebugTokenManagement/")
        .privileges(redfish::privileges::patchManager)
        .methods(boost::beast::http::verb::patch)(
            std::bind_front(debugTokenManagementPatchHandler, std::ref(app)));
}

/**
 * @brief Converts Redfish restriction mode to D-Bus restriction mode.
 *
 * @param[in] mode The Redfish restriction mode to convert.
 *
 * @return The D-Bus restriction mode.
 */
inline std::string dbusRestrictionModeFromRedfish(const std::string& mode)
{
    if (mode == "None")
    {
        return "xyz.openbmc_project.Control.Security.RestrictionMode.Modes.None";
    }
    if (mode == "Allowlist")
    {
        return "xyz.openbmc_project.Control.Security.RestrictionMode.Modes.Allowlist";
    }
    if (mode == "ProvisionedHostAllowlist")
    {
        return "xyz.openbmc_project.Control.Security.RestrictionMode.Modes.ProvisionedHostAllowlist";
    }
    if (mode == "ProvisionedHostDisabled")
    {
        return "xyz.openbmc_project.Control.Security.RestrictionMode.Modes.ProvisionedHostDisabled";
    }
    return "";
}

/**
 * @brief Converts D-Bus restriction mode to Redfish restriction mode.
 *
 * @param[in] value The D-Bus restriction mode to convert.
 *
 * @return The Redfish restriction mode.
 */
inline nvidia_manager::RestrictionMode
    restrictionModeFromDbus(const std::string& value)
{
    if (value ==
        "xyz.openbmc_project.Control.Security.RestrictionMode.Modes.None")
    {
        return nvidia_manager::RestrictionMode::None;
    }
    if (value ==
        "xyz.openbmc_project.Control.Security.RestrictionMode.Modes.Allowlist")
    {
        return nvidia_manager::RestrictionMode::Allowlist;
    }
    if (value ==
        "xyz.openbmc_project.Control.Security.RestrictionMode.Modes.ProvisionedHostAllowlist")
    {
        return nvidia_manager::RestrictionMode::ProvisionedHostAllowlist;
    }
    if (value ==
        "xyz.openbmc_project.Control.Security.RestrictionMode.Modes.ProvisionedHostDisabled")
    {
        return nvidia_manager::RestrictionMode::ProvisionedHostDisabled;
    }
    return nvidia_manager::RestrictionMode::Invalid;
}

/**
 * @brief Populates the restriction mode data.
 *
 * @param[in] asyncResp   Shared pointer for generating response message.
 * @param[in] ec          The error code.
 * @param[in] modeStr     The D-Bus restriction mode string.
 */
inline void populateRestrictionModeData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec, const std::string& modeStr)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("DBUS getProperty error: {}", ec);
        return;
    }
    nvidia_manager::RestrictionMode mode = restrictionModeFromDbus(modeStr);
    if (mode == nvidia_manager::RestrictionMode::Invalid)
    {
        BMCWEB_LOG_ERROR("Invalid restriction mode: {}", modeStr);
        return;
    }
    nlohmann::json& ipmi = asyncResp->res.jsonValue["Oem"]["Nvidia"]["IPMI"];
    ipmi["RestrictionMode"] = mode;
}

/**
 * @brief Handles getting the restriction mode property from D-Bus.
 *
 * @param[in] asyncResp   Shared pointer for generating response message.
 * @param[in] path        The path of the restriction mode.
 * @param[in] interface   The interface of the restriction mode.
 * @param[in] property    The property of the restriction mode.
 * @param[in] ec          The error code from D-Bus call.
 * @param[in] object      The MapperGetObject result from D-Bus.
 */
inline void getRestrictionModeHandler(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path, const std::string& interface,
    const std::string& property, const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec || object.empty())
    {
        BMCWEB_LOG_ERROR("Error getting service name: {}", ec);
        return;
    }
    const std::string& service = object.begin()->first;
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, service, path, interface, property,
        std::bind_front(populateRestrictionModeData, asyncResp));
}

/**
 * @brief Gets the restriction mode.
 *
 * @param[in] asyncResp   Shared pointer for generating response message.
 */
inline void
    getRestrictionMode(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::getDbusObject(
        restrictionModePath, restrictionModeInterfaceArray,
        std::bind_front(getRestrictionModeHandler, asyncResp,
                        restrictionModePath, restrictionModeInterface,
                        restrictionModeProperty));
}

/**
 * @brief Handles setting the restriction mode property on D-Bus.
 *
 * @param[in] asyncResp   Shared pointer for generating response message.
 * @param[in] path        The path of the restriction mode.
 * @param[in] interface   The interface of the restriction mode.
 * @param[in] property    The property of the restriction mode.
 * @param[in] dbusMode    The D-Bus restriction mode.
 * @param[in] ec          The error code from D-Bus call.
 * @param[in] object      The MapperGetObject result from D-Bus.
 */
inline void setRestrictionModeHandler(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& path, const std::string& interface,
    const std::string& property, const std::string& dbusMode,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec || object.empty())
    {
        BMCWEB_LOG_ERROR("Error getting service name: {}", ec);
        messages::resourceNotFound(asyncResp->res, "RestrictionMode", path);
        return;
    }
    const std::string& service = object.begin()->first;
    setDbusProperty(asyncResp, property, service,
                    sdbusplus::message::object_path(path), interface, property,
                    dbusMode);
}

/**
 * @brief Sets the restriction mode.
 *
 * @param[in] asyncResp   Shared pointer for generating response message.
 * @param[in] mode        The restriction mode to set.
 */
inline void
    setRestrictionMode(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& mode)
{
    std::string dbusMode = dbusRestrictionModeFromRedfish(mode);
    if (dbusMode.empty())
    {
        BMCWEB_LOG_ERROR("Invalid restriction mode: {}", mode);
        messages::propertyValueNotInList(asyncResp->res, mode,
                                         "RestrictionMode");
        return;
    }

    dbus::utility::getDbusObject(
        restrictionModePath, restrictionModeInterfaceArray,
        std::bind_front(setRestrictionModeHandler, asyncResp,
                        restrictionModePath, restrictionModeInterface,
                        restrictionModeProperty, dbusMode));
}

} // namespace nvidia_manager_util
} // namespace redfish
