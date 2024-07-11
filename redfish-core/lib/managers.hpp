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

// #include "error_messages.hpp"
#include "bmcweb_config.h"

#include "app.hpp"
#include "dbus_utility.hpp"
#include "health.hpp"
#include "persistentstorage_util.hpp"
#include "query.hpp"
#include "redfish_util.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_manager_utils.hpp"
#include "utils/sw_utils.hpp"
#include "utils/time_utils.hpp"

#include <app.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/date_time.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <query.hpp>
#include <registries/privilege_registry.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>
#include <utils/chassis_utils.hpp>
#include <utils/conditions_utils.hpp>
#include <utils/fw_utils.hpp>
#include <utils/hex_utils.hpp>
#include <utils/systemd_utils.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>

namespace redfish
{

// Map of service name to list of interfaces
using MapperServiceMap =
    std::vector<std::pair<std::string, std::vector<std::string>>>;

// Map of object paths to MapperServiceMaps
using MapperGetSubTreeResponse =
    std::vector<std::pair<std::string, MapperServiceMap>>;

using GetSubTreeType = std::vector<
    std::pair<std::string,
              std::vector<std::pair<std::string, std::vector<std::string>>>>>;

const std::string hexPrefix = "0x";

const int invalidDataOutSizeErr = 0x116;

#ifdef BMCWEB_ENABLE_TLS_AUTH_OPT_IN

/**
 * Helper to enable the AuthenticationTLSRequired
 */
inline void enableTLSAuth()
{
    BMCWEB_LOG_DEBUG("Processing AuthenticationTLSRequired Enable.");
    std::filesystem::path dropinConf = "bmcweb-socket-tls.conf";
    std::filesystem::path dropinDir = "/etc/systemd/system/bmcweb.socket.d";
    std::filesystem::path confPath = dropinDir / dropinConf;

    try
    {
        if (!std::filesystem::exists(dropinDir))
        {
            std::filesystem::create_directory(dropinDir);
        }
        std::ofstream out(confPath.string());
        out << "[Socket]" << std::endl;
        out << "ListenStream=" << std::endl;    // disable port 80
        out << "ListenStream=443" << std::endl; // enable port 443
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("TLSAuthEnable drop-in socket configuration file: {}",
                         e.what());
    }
    persistent_data::getConfig().enableTLSAuth();

    // restart procedure
    // TODO find nicer and faster way?
    try
    {
        dbus::utility::systemdReload();
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("TLSAuthEnable systemd Reload failed with: {}",
                         e.what());
    }

    try
    {
        dbus::utility::systemdRestartUnit("bmcweb_2esocket", "replace");
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("TLSAuthEnable bmcweb.socket Restart failed with: {}",
                         e.what());
    }

    try
    {
        dbus::utility::systemdRestartUnit("bmcweb_2eservice", "replace");
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("TLSAuthEnable bmcweb.service Restart failed with: {}",
                         e.what());
    }
}

#endif // BMCWEB_ENABLE_TLS_AUTH_OPT_IN

/**
 * Function reboots the BMC.
 *
 * @param[in] asyncResp - Shared pointer for completing asynchronous calls
 */
inline void
    doBMCGracefulRestart(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    const char* processName = "xyz.openbmc_project.State.BMC";
    const char* objectPath = "/xyz/openbmc_project/state/bmc0";
    const char* interfaceName = "xyz.openbmc_project.State.BMC";
    const std::string& propertyValue =
        "xyz.openbmc_project.State.BMC.Transition.Reboot";
    const char* destProperty = "RequestedBMCTransition";

    // Create the D-Bus variant for D-Bus call.
    sdbusplus::asio::setProperty(
        *crow::connections::systemBus, processName, objectPath, interfaceName,
        destProperty, propertyValue,
        [asyncResp](const boost::system::error_code& ec) {
        // Use "Set" method to set the property value.
        if (ec)
        {
            BMCWEB_LOG_DEBUG("[Set] Bad D-Bus request error: {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }

        messages::success(asyncResp->res);
    });
}

inline void
    doBMCForceRestart(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    const char* processName = "xyz.openbmc_project.State.BMC";
    const char* objectPath = "/xyz/openbmc_project/state/bmc0";
    const char* interfaceName = "xyz.openbmc_project.State.BMC";
    const std::string& propertyValue =
        "xyz.openbmc_project.State.BMC.Transition.HardReboot";
    const char* destProperty = "RequestedBMCTransition";

    // Create the D-Bus variant for D-Bus call.
    sdbusplus::asio::setProperty(
        *crow::connections::systemBus, processName, objectPath, interfaceName,
        destProperty, propertyValue,
        [asyncResp](const boost::system::error_code& ec) {
        // Use "Set" method to set the property value.
        if (ec)
        {
            BMCWEB_LOG_DEBUG("[Set] Bad D-Bus request error: {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }

        messages::success(asyncResp->res);
    });
}

/**
 * Function shutdowns the BMC.
 *
 * @param[in] asyncResp - Shared pointer for completing asynchronous calls
 */
inline void
    doBMCGracefulShutdown(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    const char* processName = "xyz.openbmc_project.State.BMC";
    const char* objectPath = "/xyz/openbmc_project/state/bmc0";
    const char* interfaceName = "xyz.openbmc_project.State.BMC";
    const std::string& propertyValue =
        "xyz.openbmc_project.State.BMC.Transition.PowerOff";
    const char* destProperty = "RequestedBMCTransition";

    // Create the D-Bus variant for D-Bus call.
    sdbusplus::asio::setProperty(
        *crow::connections::systemBus, processName, objectPath, interfaceName,
        destProperty, propertyValue,
        [asyncResp](const boost::system::error_code& ec) {
        // Use "Set" method to set the property value.
        if (ec)
        {
            BMCWEB_LOG_DEBUG("[Set] Bad D-Bus request error: {}", ec);
            messages::internalError(asyncResp->res);
            return;
        }

        messages::success(asyncResp->res);
    });
}

/**
 * ManagerResetAction class supports the POST method for the Reset (reboot)
 * action.
 */
inline void requestRoutesManagerResetAction(App& app)
{
    /**
     * Function handles POST method request.
     * Analyzes POST body before sending Reset (Reboot) request data to D-Bus.
     * OpenBMC supports ResetType "GracefulRestart" and "ForceRestart".
     */

    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/Actions/Manager.Reset/")
        .privileges(redfish::privileges::postManager)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& managerId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
        {
            messages::resourceNotFound(asyncResp->res, "Manager", managerId);
            return;
        }

        BMCWEB_LOG_DEBUG("Post Manager Reset.");

        std::string resetType;

        if (!redfish::json_util::readJsonAction(req, asyncResp->res,
                                                "ResetType", resetType))
        {
            return;
        }

#ifdef BMCWEB_ENABLE_REDFISH_DBUS_EVENT_PUSH
        if (resetType == "GracefulRestart" || resetType == "ForceRestart" ||
            resetType == "GracefulShutdown")
        {
            // Send an event for Manager Reset
            Event event =
                redfish::EventUtil::getInstance().createEventRebootReason(
                    "ManagerReset", "Managers");
            redfish::EventServiceManager::getInstance().sendEventWithOOC(
                std::string(req.target()), event);
        }
#endif

        if (resetType == "GracefulRestart")
        {
            BMCWEB_LOG_DEBUG("Proceeding with {}", resetType);
            doBMCGracefulRestart(asyncResp);
            return;
        }
        if (resetType == "ForceRestart")
        {
            BMCWEB_LOG_DEBUG("Proceeding with {}", resetType);
            doBMCForceRestart(asyncResp);
            return;
        }
        if (resetType == "GracefulShutdown")
        {
            BMCWEB_LOG_DEBUG("Proceeding with {}", resetType);
            doBMCGracefulShutdown(asyncResp);
            return;
        }
        BMCWEB_LOG_DEBUG("Invalid property value for ResetType: {}", resetType);
        messages::actionParameterNotSupported(asyncResp->res, resetType,
                                              "ResetType");

        return;
    });
}

/**
 * ManagerResetToDefaultsAction class supports POST method for factory reset
 * action.
 */
inline void requestRoutesManagerResetToDefaultsAction(App& app)
{
    /**
     * Function handles ResetToDefaults POST method request.
     *
     * Analyzes POST body message and factory resets BMC by calling
     * BMC code updater factory reset followed by a BMC reboot.
     *
     * BMC code updater factory reset wipes the whole BMC read-write
     * filesystem which includes things like the network settings.
     *
     * OpenBMC only supports ResetToDefaultsType "ResetAll".
     */

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Managers/<str>/Actions/Manager.ResetToDefaults/")
        .privileges(redfish::privileges::postManager)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& managerId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
        {
            messages::resourceNotFound(asyncResp->res, "Manager", managerId);
            return;
        }

        BMCWEB_LOG_DEBUG("Post ResetToDefaults.");

        std::string ifnameFactoryReset =
            "xyz.openbmc_project.Common.FactoryReset";
        std::optional<std::string> resetType;
        std::optional<std::string> resetToDefaultsType;

        if (!json_util::readJsonAction(req, asyncResp->res, "ResetType",
                                       resetType, "ResetToDefaultsType",
                                       resetToDefaultsType))
        {
            BMCWEB_LOG_DEBUG("Missing property ResetType.");

            messages::actionParameterMissing(asyncResp->res, "ResetToDefaults",
                                             "ResetType");
            return;
        }

        if (resetToDefaultsType && !resetType)
        {
            BMCWEB_LOG_WARNING(
                "Using deprecated ResetToDefaultsType, should be ResetType."
                "Support for the ResetToDefaultsType will be dropped in 2Q24");
            resetType = resetToDefaultsType;
        }

        if (resetType != "ResetAll")
        {
            BMCWEB_LOG_DEBUG("Invalid property value for ResetType: {}",
                             *resetType);
            messages::actionParameterNotSupported(asyncResp->res, *resetType,
                                                  "ResetType");
            return;
        }

#ifdef BMCWEB_ENABLE_REDFISH_DBUS_EVENT_PUSH
        // Send an event for reset to defaults
        Event event = redfish::EventUtil::getInstance().createEventRebootReason(
            "FactoryReset", "Managers");
        redfish::EventServiceManager::getInstance().sendEventWithOOC(
            std::string(req.target()), event);
#endif

        crow::connections::systemBus->async_method_call(
            [asyncResp, ifnameFactoryReset](
                const boost::system::error_code ec,
                const std::vector<std::pair<
                    std::string, std::vector<std::string>>>& interfaceNames) {
            if (ec || interfaceNames.size() <= 0)
            {
                BMCWEB_LOG_ERROR("Can't find object");
                messages::internalError(asyncResp->res);
                return;
            }

            for (const std::pair<std::string, std::vector<std::string>>&
                     object : interfaceNames)
            {
                crow::connections::systemBus->async_method_call(
                    [asyncResp, object](const boost::system::error_code ec) {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG("Failed to ResetToDefaults: {}", ec);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    // Factory Reset doesn't actually happen
                    // until a reboot Can't erase what the BMC
                    // is running on
                    doBMCGracefulRestart(asyncResp);
                },
                    object.first, "/xyz/openbmc_project/software",
                    ifnameFactoryReset, "Reset");
            }
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetObject",
            "/xyz/openbmc_project/software",
            std::array<const char*, 1>{ifnameFactoryReset.c_str()});
    });
}

/**
 * NvidiaManagerResetToDefaultsAction class supports POST method for complete
 * reset action.
 */
inline void requestRoutesNvidiaManagerResetToDefaultsAction(App& app)
{
    /**
     * Function handles ResetToDefaults POST method request.
     *
     */

    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Actions/Oem/NvidiaManager.ResetToDefaults")
        .privileges(redfish::privileges::postManager)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& managerName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        BMCWEB_LOG_DEBUG("Post ResetToDefaults.");
        (void)req;
        std::string ifnameCompleteReset = "com.nvidia.Common.CompleteReset";

        crow::connections::systemBus->async_method_call(
            [asyncResp, ifnameCompleteReset](
                const boost::system::error_code ec,
                const std::vector<std::pair<
                    std::string, std::vector<std::string>>>& interfaceNames) {
            if (ec || interfaceNames.size() <= 0)
            {
                BMCWEB_LOG_ERROR("Can't find object");
                messages::internalError(asyncResp->res);
                return;
            }

            for (const std::pair<std::string, std::vector<std::string>>&
                     object : interfaceNames)
            {
                crow::connections::systemBus->async_method_call(
                    [asyncResp, object](const boost::system::error_code ec) {
                    if (ec)
                    {
                        BMCWEB_LOG_DEBUG("Failed to ResetToDefaults: {}", ec);
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    // Factory Reset doesn't actually happen
                    // until a reboot Can't erase what the BMC
                    // is running on
                    doBMCGracefulRestart(asyncResp);
                },
                    object.first, "/xyz/openbmc_project/software",
                    ifnameCompleteReset, "CompleteReset");
            }
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetObject",
            "/xyz/openbmc_project/software",
            std::array<const char*, 1>{ifnameCompleteReset.c_str()});
    });
}

/**
 * eMMCSecureErase class supports POST method for eMMC Secure Erase
 */
inline void requestRoutesNvidiaManagerEmmcSecureErase(App& app)
{
    /**
     * Function handles ResetToDefaults POST method request.
     *
     */

    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/Actions/Oem/eMMC.SecureErase")
        .privileges(redfish::privileges::postManager)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        std::string setCommand = "/sbin/fw_setenv emmc_secure_erase yes";
        PersistentStorageUtil persistentStorageUtil;
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        BMCWEB_LOG_DEBUG("Post eMMC Secure Erase.");
        auto resetEMMCSecureEraseCallback =
            []([[maybe_unused]] const crow::Request& req,
               [[maybe_unused]] const std::shared_ptr<bmcweb::AsyncResp>&
                   asyncResp,
               [[maybe_unused]] const std::string& stdOut,
               [[maybe_unused]] const std::string& stdErr,
               [[maybe_unused]] const boost::system::error_code& ec,
               [[maybe_unused]] int errorCode) -> void {
            BMCWEB_LOG_INFO("Setting eMMC Secure Erase uboot env");
            return;
        };
        persistentStorageUtil.executeEnvCommand(
            req, asyncResp, setCommand,
            std::move(resetEMMCSecureEraseCallback));
        doBMCGracefulRestart(asyncResp);
    });
}

inline void requestRoutesManagerEmmcSecureEraseActionInfo(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */

    BMCWEB_ROUTE(app,
                 "/redfish/v1/Managers/<str>/Oem/EmmcSecureEraseActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& systemName) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        asyncResp->res.jsonValue["@odata.type"] =
            "#ActionInfo.v1_1_2.ActionInfo";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Managers/" +
            std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
            "/Oem/EmmcSecureEraseActionInfo";
        asyncResp->res.jsonValue["Name"] = "Emmc Secure Erase Action Info";
        asyncResp->res.jsonValue["Id"] = "EmmcSecureEraseActionInfo";
    });
}

/**
 * ManagerResetActionInfo derived class for delivering Manager
 * ResetType AllowableValues using ResetInfo schema.
 */
inline void requestRoutesManagerResetActionInfo(App& app)
{
    /**
     * Functions triggers appropriate requests on DBus
     */

    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/ResetActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& managerId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
        {
            messages::resourceNotFound(asyncResp->res, "Manager", managerId);
            return;
        }

        asyncResp->res.jsonValue["@odata.type"] =
            "#ActionInfo.v1_1_2.ActionInfo";
        asyncResp->res.jsonValue["@odata.id"] =
            "/redfish/v1/Managers/" +
            std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) + "/ResetActionInfo";
        asyncResp->res.jsonValue["Name"] = "Reset Action Info";
        asyncResp->res.jsonValue["Id"] = "ResetActionInfo";
        nlohmann::json::object_t parameter;
        parameter["Name"] = "ResetType";
        parameter["Required"] = true;
        parameter["DataType"] = "String";

        nlohmann::json::array_t allowableValues;
        allowableValues.push_back("GracefulRestart");
        allowableValues.push_back("ForceRestart");
        allowableValues.push_back("GracefulShutdown");
        parameter["AllowableValues"] = std::move(allowableValues);

        nlohmann::json::array_t parameters;
        parameters.emplace_back(std::move(parameter));

        asyncResp->res.jsonValue["Parameters"] = std::move(parameters);
    });
}

inline void
    setDbusSelCapacity(size_t capacity,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    auto respHandler = [asyncResp](const boost::system::error_code ec) {
        if (ec.value() == EBADR)
        {
            messages::resourceNotFound(asyncResp->res,
                                       "#Manager.v1_11_0.Manager", "Capacity");
            return;
        }
        if (ec)
        {
            messages::internalError(asyncResp->res);
            return;
        }

        asyncResp->res.result(boost::beast::http::status::no_content);
    };
    crow::connections::systemBus->async_method_call(
        respHandler, "xyz.openbmc_project.Logging",
        "/xyz/openbmc_project/logging", "xyz.openbmc_project.Logging.Capacity",
        "SetInfoLogCapacity", capacity);
}

/**
 * NvidiaManagerSetSelCapacityAction class supports POST method for
 * configuration of SEL capacity.
 */
inline void requestRoutesNvidiaManagerSetSelCapacityAction(App& app)
{
    BMCWEB_ROUTE(app,
                 "/redfish/v1/Managers/<str>/Actions/Oem/Nvidia/SelCapacity/")
        .privileges(redfish::privileges::postManager)
        .methods(boost::beast::http::verb::post)(
            [](const crow::Request& req,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               [[maybe_unused]] const std::string& systemName) {
        size_t capacity = 0;

        if (!redfish::json_util::readJsonAction(req, asyncResp->res,
                                                "ErrorInfoCap", capacity))
        {
            BMCWEB_LOG_ERROR("Unable to set ErrorInfoCap");
            return;
        }

        setDbusSelCapacity(capacity, asyncResp);
    });
}

inline void
    getDbusSelCapacity(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    auto respHandler = [asyncResp](const boost::system::error_code ec,
                                   const std::variant<size_t>& capacity) {
        if (ec.value() == EBADR)
        {
            messages::resourceNotFound(asyncResp->res,
                                       "#Manager.v1_11_0.Manager", "Capacity");
            return;
        }
        if (ec)
        {
            asyncResp->res.result(
                boost::beast::http::status::internal_server_error);
            return;
        }
        const size_t* capacityValue = std::get_if<size_t>(&capacity);
        if (capacityValue == nullptr)
        {
            BMCWEB_LOG_ERROR("dbuserror nullptr error");
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["ErrorInfoCap"] = *capacityValue;
    };
    crow::connections::systemBus->async_method_call(
        respHandler, "xyz.openbmc_project.Logging",
        "/xyz/openbmc_project/logging", "org.freedesktop.DBus.Properties",
        "Get", "xyz.openbmc_project.Logging.Capacity", "InfoLogCapacity");
}

/**
 * NvidiaManagerGetSelCapacity class supports GET method for getting
 * the SEL capacity.
 */
inline void requestRoutesNvidiaManagerGetSelCapacity(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/Oem/Nvidia/SelCapacity/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            [](const crow::Request&,
               const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
               [[maybe_unused]] const std::string& systemName) {
        getDbusSelCapacity(asyncResp);
    });
}

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES

/**
 * SyncOOBRawCommandActionInfo derived class for delivering Managers
 * RawOOBCommands AllowableValues using NvidiaSyncOOBRawCommandAction schema.
 */
inline void requestRoutesNvidiaSyncOOBRawCommandActionInfo(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/SyncOOBRawCommandActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& bmcId)

    {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        // Process non bmc service manager
        if (bmcId != BMCWEB_REDFISH_MANAGER_URI_NAME)
        {
            messages::resourceNotFound(asyncResp->res,
                                       "#Manager.v1_11_0.Manager", bmcId);
            return;
        }
        asyncResp->res.jsonValue = {
            {"@odata.type", "#ActionInfo.v1_1_2.ActionInfo"},
            {"@odata.id", "/redfish/v1/Managers/" + bmcId +
                              "/Oem/Nvidia/SyncOOBRawCommandActionInfo"},
            {"Name", "SyncOOBRawCommand Action Info"},
            {"Id", "NvidiaSyncOOBRawCommandActionInfo"},
            {"Parameters",
             {{{"Name", "TargetType"},
               {"Required", true},
               {"DataType", "String"},
               {"AllowableValues", {"GPU", "NVSwitch", "Baseboard"}}},
              {{"Name", "TartgetInstanceId"},
               {"Required", false},
               {"DataType", "Number"}},
              {{"Name", "Opcode"}, {"Required", true}, {"DataType", "String"}},
              {{"Name", "Arg1"}, {"Required", false}, {"DataType", "String"}},
              {{"Name", "Arg2"}, {"Required", false}, {"DataType", "String"}},
              {{"Name", "DataIn"},
               {"Required", false},
               {"DataType", "StringArray"},
               {"ArraySizeMaximum", 4},
               {"ArraySizeMinimum", 4}},
              {{"Name", "ExtDataIn"},
               {"Required", false},
               {"DataType", "StringArray"},
               {"ArraySizeMaximum", 4},
               {"ArraySizeMinimum", 4}}}}};
    });
}

/**
 * AsyncOOBRawCommandActionInfo derived class for delivering Managers
 * RawOOBCommands AllowableValues using NvidiaAsyncOOBRawCommandAction schema.
 */
inline void requestRoutesNvidiaAsyncOOBRawCommandActionInfo(App& app)
{
    BMCWEB_ROUTE(
        app,
        "/redfish/v1/Managers/<str>/Oem/Nvidia/AsyncOOBRawCommandActionInfo/")
        .privileges(redfish::privileges::getActionInfo)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& bmcId)

    {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        // Process non bmc service manager
        if (bmcId != BMCWEB_REDFISH_MANAGER_URI_NAME)
        {
            messages::resourceNotFound(asyncResp->res,
                                       "#Manager.v1_11_0.Manager", bmcId);
            return;
        }

        asyncResp->res.jsonValue = {
            {"@odata.type", "#ActionInfo.v1_1_2.ActionInfo"},
            {"@odata.id", "/redfish/v1/Managers/" + bmcId +
                              "/Oem/Nvidia/AsyncOOBRawCommandActionInfo"},
            {"Name", "AsyncOOBRawCommand Action Info"},
            {"Id", "NvidiaAsyncOOBRawCommandActionInfo"},
            {"Parameters",
             {{{"Name", "TargetType"},
               {"Required", true},
               {"DataType", "String"},
               {"AllowableValues", {"GPU", "NVSwitch"}}},
              {{"Name", "TartgetInstanceId"},
               {"Required", true},
               {"DataType", "Number"}},
              {{"Name", "AsyncArg1"},
               {"Required", true},
               {"DataType", "String"}},
              {{"Name", "AsyncDataIn"},
               {"Required", false},
               {"DataType", "StringArray"}},
              {{"Name", "RequestedDataOutBytes"},
               {"Required", true},
               {"DataType", "Number"},
               {"MinimumValue", 0},
               {"MaximumValue", 1024}}}}};
    });
}

// convert sync command input request data to raw datain
inline uint32_t formatSyncDataIn(std::vector<std::string>& data)
{
    size_t found;
    std::string dataStr;
    uint32_t dataIn = 0;
    for (auto it = data.rbegin(); it != data.rend(); ++it)
    {
        found = (*it).find(hexPrefix);
        if (found != std::string::npos)
        {
            (*it).erase(found, hexPrefix.length());
        }
        dataStr.append(*it);
    }

    try
    {
        dataIn = static_cast<uint32_t>(std::stoul(dataStr, nullptr, 16));
    }
    catch (const std::invalid_argument& ia)
    {
        BMCWEB_LOG_ERROR("stoul conversion exception Invalid argument {}",
                         ia.what());
        throw std::runtime_error("Invalid Argument");
    }
    catch (const std::out_of_range& oor)
    {
        BMCWEB_LOG_ERROR("stoul conversion exception out fo range {}",
                         oor.what());
        throw std::runtime_error("Invalid Argument");
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("stoul conversion undefined exception{}", e.what());
        throw std::runtime_error("Invalid Argument");
    }
    return dataIn;
}

void executeRawSynCommand(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                          const std::string& serviceName,
                          const std::string& objPath, const std::string& Type,
                          uint8_t id, uint8_t opCode, uint8_t arg1,
                          uint8_t arg2, uint32_t dataIn, uint32_t extDataIn)
{
    BMCWEB_LOG_DEBUG("executeRawSynCommand fn");
    crow::connections::systemBus->async_method_call(
        [resp, Type,
         id](boost::system::error_code ec, sdbusplus::message::message& msg,
             const std::tuple<int, uint32_t, uint32_t, uint32_t>& res) {
        if (!ec)
        {
            int rc = get<0>(res);
            if (rc != 0)
            {
                BMCWEB_LOG_ERROR("synccommand failed with rc:{}", rc);
                messages::operationFailed(resp->res);
                return;
            }

            resp->res.jsonValue["@odata.type"] =
                "#NvidiaManager.v1_2_0.NvidiaManager";
            resp->res.jsonValue["StatusRegister"] =
                intToHexByteArray(get<1>(res));
            resp->res.jsonValue["DataOut"] = intToHexByteArray(get<2>(res));
            resp->res.jsonValue["ExtDataOut"] = intToHexByteArray(get<3>(res));
            return;
        }
        // Read and convert dbus error message to redfish error
        const sd_bus_error* dbusError = msg.get_error();
        if (dbusError == nullptr)
        {
            BMCWEB_LOG_DEBUG("dbuserror nullptr error");
            messages::internalError(resp->res);
            return;
        }
        if (strcmp(dbusError->name,
                   "xyz.openbmc_project.Common.Error.InvalidArgument") == 0)
        {
            BMCWEB_LOG_ERROR(
                "xyz.openbmc_project.Common.Error.InvalidArgument error");
            messages::propertyValueIncorrect(resp->res, "TargetInstanceId",
                                             std::to_string(id));
        }
        else
        {
            BMCWEB_LOG_ERROR("form executeRawSynCommand failed with error ");
            BMCWEB_LOG_ERROR("{}", ec);
            messages::internalError(resp->res);
        }
        return;
    },
        serviceName, objPath, "com.nvidia.Protocol.SMBPBI.Raw", "SyncCommand",
        Type, id, opCode, arg1, arg2, dataIn, extDataIn);
}

inline void requestRouteSyncRawOobCommand(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/" +
                          std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                          "/Actions/Oem/NvidiaManager.SyncOOBRawCommand")
        .privileges(redfish::privileges::postManager)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        uint8_t targetId;
        std::string targetType;
        std::string opCode;
        std::string arg1;
        std::string arg2;
        uint8_t opCodeRaw;
        uint8_t arg1Raw;
        uint8_t arg2Raw;
        uint32_t dataInRaw = 0;
        uint32_t extDataInRaw = 0;
        std::optional<std::vector<std::string>> dataIn;
        std::optional<std::vector<std::string>> extDataIn;

        if (!redfish::json_util::readJsonAction(
                req, asyncResp->res, "TargetType", targetType,
                "TargetInstanceId", targetId, "Opcode", opCode, "Arg1", arg1,
                "Arg2", arg2, "DataIn", dataIn, "ExtDataIn", extDataIn))
        {
            BMCWEB_LOG_ERROR("Missing property");
            return;
        }

        if ((dataIn) && ((*dataIn).size()))
        {
            try
            {
                dataInRaw = formatSyncDataIn(*dataIn);
            }
            catch (const std::runtime_error& e)
            {
                BMCWEB_LOG_ERROR("formatSyncDataIn failed with runtime error ");
                messages::internalError(asyncResp->res);
                return;
            }
        }

        if ((extDataIn) && ((*extDataIn).size()))
        {
            try
            {
                extDataInRaw = formatSyncDataIn(*extDataIn);
            }
            catch (const std::runtime_error& e)
            {
                BMCWEB_LOG_ERROR("formatSyncDataIn failed with runtime error ");
                messages::internalError(asyncResp->res);
                return;
            }
        }

        try
        {
            opCodeRaw = static_cast<uint8_t>(std::stoul(opCode, nullptr, 16));
            arg1Raw = static_cast<uint8_t>(std::stoul(arg1, nullptr, 16));
            arg2Raw = static_cast<uint8_t>(std::stoul(arg2, nullptr, 16));
        }
        catch (...)
        {
            BMCWEB_LOG_ERROR("raw Sync command failed : stoul exception ");
            messages::internalError(asyncResp->res);
            return;
        }

        crow::connections::systemBus->async_method_call(
            [asyncResp, targetType, targetId, opCodeRaw, arg1Raw, arg2Raw,
             dataInRaw, extDataInRaw](const boost::system::error_code ec,
                                      const MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("unable to find SMBPBI raw interface");
                messages::internalError(asyncResp->res);
                return;
            }
            for (const auto& [objectPath, serviceMap] : subtree)
            {
                if (serviceMap.size() < 1)
                {
                    BMCWEB_LOG_ERROR("No service Present");
                    messages::internalError(asyncResp->res);
                    return;
                }
                const std::string& serviceName = serviceMap[0].first;
                executeRawSynCommand(asyncResp, serviceName, objectPath,
                                     targetType, targetId, opCodeRaw, arg1Raw,
                                     arg2Raw, dataInRaw, extDataInRaw);
            }
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/inventory", int32_t(0),
            std::array<const char*, 1>{"com.nvidia.Protocol.SMBPBI.Raw"});
    });
}

// function to convert dataInbyte array to dataIn uint32 vector
std::vector<std::uint32_t>
    formatAsyncDataIn(std::vector<std::string>& asynDataInBytes)
{
    size_t j;
    size_t found;
    std::string temp;
    std::vector<std::uint32_t> asyncDataIn;
    auto dataInSize = asynDataInBytes.size();
    try
    {
        if (dataInSize)
        {
            for (size_t i = 0; i < dataInSize; i++)
            {
                j = i + 1;
                // handle "0x" hex prefix
                found = (asynDataInBytes[i]).find(hexPrefix);
                if (found != std::string::npos)
                {
                    (asynDataInBytes[i]).erase(found, hexPrefix.length());
                }
                temp.insert(0, asynDataInBytes[i]);

                if ((j == dataInSize) || ((j % 4) == 0))
                {
                    // covert string to unit32_t
                    asyncDataIn.push_back(
                        static_cast<uint32_t>(std::stoul(temp, nullptr, 16)));
                    temp.erase();
                }
            }
        }
    }
    catch (const std::invalid_argument& ia)
    {
        BMCWEB_LOG_ERROR(
            "formatAsyncDataIn: stoul conversion exception Invalid argument {}",
            ia.what());
        throw std::runtime_error("Invalid Argument");
    }
    catch (const std::out_of_range& oor)
    {
        BMCWEB_LOG_ERROR(
            "formatAsyncDataIn: stoul conversion exception out fo range {}",
            oor.what());
        throw std::runtime_error("Argument out of range");
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR(
            "formatAsyncDataIn: stoul conversion undefined exception{}",
            e.what());
        throw std::runtime_error("undefined exception");
    }

    return asyncDataIn;
}

void executeRawAsynCommand(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                           const std::string& serviceName,
                           const std::string& objPath, const std::string& Type,
                           uint8_t id, uint8_t argRaw,
                           const std::vector<uint32_t>& asyncDataInRaw,
                           uint32_t requestedDataOutBytes)
{
    BMCWEB_LOG_DEBUG("executeRawAsynCommand fn");
    crow::connections::systemBus->async_method_call(
        [resp, Type, requestedDataOutBytes,
         id](boost::system::error_code ec, sdbusplus::message::message& msg,
             const std::tuple<int, uint32_t, uint32_t, std::vector<uint32_t>>&
                 res) {
        if (!ec)
        {
            int rc = get<0>(res);

            if (rc == invalidDataOutSizeErr)
            {
                BMCWEB_LOG_ERROR("asynccommand failed with rc:{}", rc);
                messages::propertyValueIncorrect(
                    resp->res, "RequestedDataOutBytes",
                    std::to_string(requestedDataOutBytes));
                return;
            }

            if (rc != 0)
            {
                BMCWEB_LOG_ERROR("asynccommand failed with rc:{}", rc);
                messages::operationFailed(resp->res);
                return;
            }
            resp->res.jsonValue["@odata.type"] =
                "#NvidiaManager.v1_2_0.NvidiaManager";

            resp->res.jsonValue["StatusRegister"] =
                intToHexByteArray(get<1>(res));
            resp->res.jsonValue["DataOut"] = intToHexByteArray(get<2>(res));
            std::vector<std::uint32_t> asyncDataOut = get<3>(res);
            std::vector<std::string> asyncDataOutBytes;
            for (auto val : asyncDataOut)
            {
                auto dataOutHex = intToHexByteArray(val);
                asyncDataOutBytes.insert(asyncDataOutBytes.end(),
                                         dataOutHex.begin(), dataOutHex.end());
            }
            resp->res.jsonValue["AsyncDataOut"] = asyncDataOutBytes;

            return;
        }
        // Read and convert dbus error message to redfish error
        const sd_bus_error* dbusError = msg.get_error();
        if (dbusError == nullptr)
        {
            BMCWEB_LOG_ERROR("dbus error nullptr error");
            messages::internalError(resp->res);
            return;
        }
        if (strcmp(dbusError->name,
                   "xyz.openbmc_project.Common.Error.InvalidArgument") == 0)
        {
            BMCWEB_LOG_ERROR(
                "xyz.openbmc_project.Common.Error.InvalidArgument error");
            messages::propertyValueIncorrect(resp->res, "TargetInstanceId",
                                             std::to_string(id));
        }
        else
        {
            BMCWEB_LOG_ERROR("form executeRawAsynCommand failed with error ");
            BMCWEB_LOG_ERROR("{}", ec);
            messages::internalError(resp->res);
        }
        return;
    },
        serviceName, objPath, "com.nvidia.Protocol.SMBPBI.Raw", "AsyncCommand",
        Type, id, argRaw, asyncDataInRaw, requestedDataOutBytes);
}

inline void requestRouteAsyncRawOobCommand(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/" +
                          std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                          "/Actions/Oem/NvidiaManager.AsyncOOBRawCommand")
        .privileges(redfish::privileges::postManager)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        uint8_t targetId;
        uint8_t argRaw;
        uint32_t requestedDataOutBytes;
        std::string targetType;
        std::string arg;
        std::optional<std::vector<std::string>> asynDataIn;
        std::vector<uint32_t> asyncDataInRaw;

        if (!redfish::json_util::readJsonAction(
                req, asyncResp->res, "TargetType", targetType,
                "TargetInstanceId", targetId, "AsyncArg1", arg,
                "RequestedDataOutBytes", requestedDataOutBytes, "AsyncDataIn",
                asynDataIn))
        {
            BMCWEB_LOG_ERROR("Missing property");
            return;
        }

        if ((asynDataIn) && ((*asynDataIn).size()))
        {
            try
            {
                asyncDataInRaw = formatAsyncDataIn(*asynDataIn);
            }
            catch (const std::runtime_error& e)
            {
                BMCWEB_LOG_ERROR(
                    "formatAsyncDataIn failed with runtime error ");
                messages::internalError(asyncResp->res);
                return;
            }
        }
        try
        {
            argRaw = static_cast<uint8_t>(std::stoul(arg, nullptr, 16));
        }
        catch (...)
        {
            BMCWEB_LOG_ERROR("raw Async command failed : stoul exception ");
            messages::internalError(asyncResp->res);
            return;
        }

        crow::connections::systemBus->async_method_call(
            [asyncResp, targetType, targetId, argRaw, asyncDataInRaw,
             requestedDataOutBytes](const boost::system::error_code ec,
                                    const MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("unable to find SMBPBI raw interface");
                messages::internalError(asyncResp->res);
                return;
            }
            for (const auto& [objectPath, serviceMap] : subtree)
            {
                if (serviceMap.size() < 1)
                {
                    BMCWEB_LOG_ERROR("No service Present");
                    messages::internalError(asyncResp->res);
                    return;
                }
                const std::string& serviceName = serviceMap[0].first;
                executeRawAsynCommand(asyncResp, serviceName, objectPath,
                                      targetType, targetId, argRaw,
                                      asyncDataInRaw, requestedDataOutBytes);
            }
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/inventory", int32_t(0),
            std::array<const char*, 1>{"com.nvidia.Protocol.SMBPBI.Raw"});
    });
}
#endif // BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES

static constexpr const char* objectManagerIface =
    "org.freedesktop.DBus.ObjectManager";
static constexpr const char* pidConfigurationIface =
    "xyz.openbmc_project.Configuration.Pid";
static constexpr const char* pidZoneConfigurationIface =
    "xyz.openbmc_project.Configuration.Pid.Zone";
static constexpr const char* stepwiseConfigurationIface =
    "xyz.openbmc_project.Configuration.Stepwise";
static constexpr const char* thermalModeIface =
    "xyz.openbmc_project.Control.ThermalMode";

inline void
    asyncPopulatePid(const std::string& connection, const std::string& path,
                     const std::string& currentProfile,
                     const std::vector<std::string>& supportedProfiles,
                     const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    sdbusplus::message::object_path objPath(path);
    dbus::utility::getManagedObjects(
        connection, objPath,
        [asyncResp, currentProfile, supportedProfiles](
            const boost::system::error_code& ec,
            const dbus::utility::ManagedObjectType& managedObj) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("{}", ec);
            messages::internalError(asyncResp->res);
            return;
        }
        nlohmann::json& configRoot =
            asyncResp->res.jsonValue["Oem"]["OpenBmc"]["Fan"];
        nlohmann::json& fans = configRoot["FanControllers"];
        fans["@odata.type"] = "#OpenBMCManager.FanControllers";
        fans["@odata.id"] = boost::urls::format(
            "/redfish/v1/Managers/{}#/Oem/OpenBmc/Fan/FanControllers",
            BMCWEB_REDFISH_MANAGER_URI_NAME);

        nlohmann::json& pids = configRoot["PidControllers"];
        pids["@odata.type"] = "#OpenBMCManager.PidControllers";
        pids["@odata.id"] = boost::urls::format(
            "/redfish/v1/Managers/{}#/Oem/OpenBmc/Fan/PidControllers",
            BMCWEB_REDFISH_MANAGER_URI_NAME);

        nlohmann::json& stepwise = configRoot["StepwiseControllers"];
        stepwise["@odata.type"] = "#OpenBMCManager.StepwiseControllers";
        stepwise["@odata.id"] = boost::urls::format(
            "/redfish/v1/Managers/{}#/Oem/OpenBmc/Fan/StepwiseControllers",
            BMCWEB_REDFISH_MANAGER_URI_NAME);

        nlohmann::json& zones = configRoot["FanZones"];
        zones["@odata.id"] = boost::urls::format(
            "/redfish/v1/Managers/{}#/Oem/OpenBmc/Fan/FanZones",
            BMCWEB_REDFISH_MANAGER_URI_NAME);
        zones["@odata.type"] = "#OpenBMCManager.FanZones";
        configRoot["@odata.id"] =
            boost::urls::format("/redfish/v1/Managers/{}#/Oem/OpenBmc/Fan",
                                BMCWEB_REDFISH_MANAGER_URI_NAME);
        configRoot["@odata.type"] = "#OpenBMCManager.Fan";
        configRoot["Profile@Redfish.AllowableValues"] = supportedProfiles;

        if (!currentProfile.empty())
        {
            configRoot["Profile"] = currentProfile;
        }
        BMCWEB_LOG_DEBUG("profile = {} !", currentProfile);

        for (const auto& pathPair : managedObj)
        {
            for (const auto& intfPair : pathPair.second)
            {
                if (intfPair.first != pidConfigurationIface &&
                    intfPair.first != pidZoneConfigurationIface &&
                    intfPair.first != stepwiseConfigurationIface)
                {
                    continue;
                }

                std::string name;

                for (const std::pair<std::string,
                                     dbus::utility::DbusVariantType>& propPair :
                     intfPair.second)
                {
                    if (propPair.first == "Name")
                    {
                        const std::string* namePtr =
                            std::get_if<std::string>(&propPair.second);
                        if (namePtr == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Pid Name Field illegal");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        name = *namePtr;
                        dbus::utility::escapePathForDbus(name);
                    }
                    else if (propPair.first == "Profiles")
                    {
                        const std::vector<std::string>* profiles =
                            std::get_if<std::vector<std::string>>(
                                &propPair.second);
                        if (profiles == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Pid Profiles Field illegal");
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        if (std::find(profiles->begin(), profiles->end(),
                                      currentProfile) == profiles->end())
                        {
                            BMCWEB_LOG_INFO(
                                "{} not supported in current profile", name);
                            continue;
                        }
                    }
                }
                nlohmann::json* config = nullptr;
                const std::string* classPtr = nullptr;

                for (const std::pair<std::string,
                                     dbus::utility::DbusVariantType>& propPair :
                     intfPair.second)
                {
                    if (propPair.first == "Class")
                    {
                        classPtr = std::get_if<std::string>(&propPair.second);
                    }
                }

                boost::urls::url url(
                    boost::urls::format("/redfish/v1/Managers/{}",
                                        BMCWEB_REDFISH_MANAGER_URI_NAME));
                if (intfPair.first == pidZoneConfigurationIface)
                {
                    std::string chassis;
                    if (!dbus::utility::getNthStringFromPath(pathPair.first.str,
                                                             5, chassis))
                    {
                        chassis = "#IllegalValue";
                    }
                    nlohmann::json& zone = zones[name];
                    zone["Chassis"] = {
                        {"@odata.id", "/redfish/v1/Chassis/" + chassis}};
                    zone["@odata.id"] =
                        "/redfish/v1/Managers/" +
                        std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                        "#/Oem/OpenBmc/Fan/FanZones/" + name;
                    zone["@odata.type"] = "#OpenBMCManager.FanZone";
                    config = &zone;
                }

                else if (intfPair.first == stepwiseConfigurationIface)
                {
                    if (classPtr == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Pid Class Field illegal");
                        messages::internalError(asyncResp->res);
                        return;
                    }

                    nlohmann::json& controller = stepwise[name];
                    config = &controller;

                    controller["@odata.id"] =
                        "/redfish/v1/Managers/" +
                        std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                        "#/Oem/OpenBmc/Fan/StepwiseControllers/" + name;
                    controller["@odata.type"] =
                        "#OpenBMCManager.StepwiseController";

                    controller["Direction"] = *classPtr;
                }

                // pid and fans are off the same configuration
                else if (intfPair.first == pidConfigurationIface)
                {
                    if (classPtr == nullptr)
                    {
                        BMCWEB_LOG_ERROR("Pid Class Field illegal");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    bool isFan = *classPtr == "fan";
                    nlohmann::json& element = isFan ? fans[name] : pids[name];
                    config = &element;
                    if (isFan)
                    {
                        element["@odata.id"] =
                            "/redfish/v1/Managers/" +
                            std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                            "#/Oem/OpenBmc/Fan/FanControllers/" + name;
                        element["@odata.type"] =
                            "#OpenBMCManager.FanController";
                    }
                    else
                    {
                        element["@odata.id"] =
                            "/redfish/v1/Managers/" +
                            std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                            "#/Oem/OpenBmc/Fan/PidControllers/" + name;
                        element["@odata.type"] =
                            "#OpenBMCManager.PidController";
                    }
                }
                else
                {
                    BMCWEB_LOG_ERROR("Unexpected configuration");
                    messages::internalError(asyncResp->res);
                    return;
                }

                // used for making maps out of 2 vectors
                const std::vector<double>* keys = nullptr;
                const std::vector<double>* values = nullptr;

                for (const auto& propertyPair : intfPair.second)
                {
                    if (propertyPair.first == "Type" ||
                        propertyPair.first == "Class" ||
                        propertyPair.first == "Name")
                    {
                        continue;
                    }

                    // zones
                    if (intfPair.first == pidZoneConfigurationIface)
                    {
                        const double* ptr =
                            std::get_if<double>(&propertyPair.second);
                        if (ptr == nullptr)
                        {
                            BMCWEB_LOG_ERROR("Field Illegal {}",
                                             propertyPair.first);
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        (*config)[propertyPair.first] = *ptr;
                    }

                    if (intfPair.first == stepwiseConfigurationIface)
                    {
                        if (propertyPair.first == "Reading" ||
                            propertyPair.first == "Output")
                        {
                            const std::vector<double>* ptr =
                                std::get_if<std::vector<double>>(
                                    &propertyPair.second);

                            if (ptr == nullptr)
                            {
                                BMCWEB_LOG_ERROR("Field Illegal {}",
                                                 propertyPair.first);
                                messages::internalError(asyncResp->res);
                                return;
                            }

                            if (propertyPair.first == "Reading")
                            {
                                keys = ptr;
                            }
                            else
                            {
                                values = ptr;
                            }
                            if (keys != nullptr && values != nullptr)
                            {
                                if (keys->size() != values->size())
                                {
                                    BMCWEB_LOG_ERROR(
                                        "Reading and Output size don't match ");
                                    messages::internalError(asyncResp->res);
                                    return;
                                }
                                nlohmann::json& steps = (*config)["Steps"];
                                steps = nlohmann::json::array();
                                for (size_t ii = 0; ii < keys->size(); ii++)
                                {
                                    nlohmann::json::object_t step;
                                    step["Target"] = (*keys)[ii];
                                    step["Output"] = (*values)[ii];
                                    steps.emplace_back(std::move(step));
                                }
                            }
                        }
                        if (propertyPair.first == "NegativeHysteresis" ||
                            propertyPair.first == "PositiveHysteresis")
                        {
                            const double* ptr =
                                std::get_if<double>(&propertyPair.second);
                            if (ptr == nullptr)
                            {
                                BMCWEB_LOG_ERROR("Field Illegal {}",
                                                 propertyPair.first);
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            (*config)[propertyPair.first] = *ptr;
                        }
                    }

                    // pid and fans are off the same configuration
                    if (intfPair.first == pidConfigurationIface ||
                        intfPair.first == stepwiseConfigurationIface)
                    {
                        if (propertyPair.first == "Zones")
                        {
                            const std::vector<std::string>* inputs =
                                std::get_if<std::vector<std::string>>(
                                    &propertyPair.second);

                            if (inputs == nullptr)
                            {
                                BMCWEB_LOG_ERROR("Zones Pid Field Illegal");
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            auto& data = (*config)[propertyPair.first];
                            data = nlohmann::json::array();
                            for (std::string itemCopy : *inputs)
                            {
                                dbus::utility::escapePathForDbus(itemCopy);
                                nlohmann::json::object_t input;
                                boost::urls::url managerUrl = boost::urls::format(
                                    "/redfish/v1/Managers/{}#{}",
                                    BMCWEB_REDFISH_MANAGER_URI_NAME,
                                    ("/Oem/OpenBmc/Fan/FanZones"_json_pointer /
                                     itemCopy)
                                        .to_string());
                                input["@odata.id"] = std::move(managerUrl);
                                data.emplace_back(std::move(input));
                            }
                        }
                        // todo(james): may never happen, but this
                        // assumes configuration data referenced in the
                        // PID config is provided by the same daemon, we
                        // could add another loop to cover all cases,
                        // but I'm okay kicking this can down the road a
                        // bit

                        else if (propertyPair.first == "Inputs" ||
                                 propertyPair.first == "Outputs")
                        {
                            auto& data = (*config)[propertyPair.first];
                            const std::vector<std::string>* inputs =
                                std::get_if<std::vector<std::string>>(
                                    &propertyPair.second);

                            if (inputs == nullptr)
                            {
                                BMCWEB_LOG_ERROR("Field Illegal {}",
                                                 propertyPair.first);
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            data = *inputs;
                        }
                        else if (propertyPair.first == "SetPointOffset")
                        {
                            const std::string* ptr =
                                std::get_if<std::string>(&propertyPair.second);

                            if (ptr == nullptr)
                            {
                                BMCWEB_LOG_ERROR("Field Illegal {}",
                                                 propertyPair.first);
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            // translate from dbus to redfish
                            if (*ptr == "WarningHigh")
                            {
                                (*config)["SetPointOffset"] =
                                    "UpperThresholdNonCritical";
                            }
                            else if (*ptr == "WarningLow")
                            {
                                (*config)["SetPointOffset"] =
                                    "LowerThresholdNonCritical";
                            }
                            else if (*ptr == "CriticalHigh")
                            {
                                (*config)["SetPointOffset"] =
                                    "UpperThresholdCritical";
                            }
                            else if (*ptr == "CriticalLow")
                            {
                                (*config)["SetPointOffset"] =
                                    "LowerThresholdCritical";
                            }
                            else
                            {
                                BMCWEB_LOG_ERROR("Value Illegal {}", *ptr);
                                messages::internalError(asyncResp->res);
                                return;
                            }
                        }
                        // doubles
                        else if (propertyPair.first == "FFGainCoefficient" ||
                                 propertyPair.first == "FFOffCoefficient" ||
                                 propertyPair.first == "ICoefficient" ||
                                 propertyPair.first == "ILimitMax" ||
                                 propertyPair.first == "ILimitMin" ||
                                 propertyPair.first == "PositiveHysteresis" ||
                                 propertyPair.first == "NegativeHysteresis" ||
                                 propertyPair.first == "OutLimitMax" ||
                                 propertyPair.first == "OutLimitMin" ||
                                 propertyPair.first == "PCoefficient" ||
                                 propertyPair.first == "SetPoint" ||
                                 propertyPair.first == "SlewNeg" ||
                                 propertyPair.first == "SlewPos")
                        {
                            const double* ptr =
                                std::get_if<double>(&propertyPair.second);
                            if (ptr == nullptr)
                            {
                                BMCWEB_LOG_ERROR("Field Illegal {}",
                                                 propertyPair.first);
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            (*config)[propertyPair.first] = *ptr;
                        }
                    }
                }
            }
        }
    });
}

enum class CreatePIDRet
{
    fail,
    del,
    patch
};

inline bool
    getZonesFromJsonReq(const std::shared_ptr<bmcweb::AsyncResp>& response,
                        std::vector<nlohmann::json::object_t>& config,
                        std::vector<std::string>& zones)
{
    if (config.empty())
    {
        BMCWEB_LOG_ERROR("Empty Zones");
        messages::propertyValueFormatError(response->res, config, "Zones");
        return false;
    }
    for (auto& odata : config)
    {
        std::string path;
        if (!redfish::json_util::readJsonObject(odata, response->res,
                                                "@odata.id", path))
        {
            return false;
        }
        std::string input;

        // 8 below comes from
        // /redfish/v1/Managers/BMCWEB_REDFISH_MANAGER_URI_NAME/Oem/OpenBmc/Fan/FanZones/Left
        //     0    1     2      3    4    5      6     7      8
        if (!dbus::utility::getNthStringFromPath(path, 8, input))
        {
            BMCWEB_LOG_ERROR("Got invalid path {}", path);
            BMCWEB_LOG_ERROR("Illegal Type Zones");
            messages::propertyValueFormatError(response->res, odata, "Zones");
            return false;
        }
        std::replace(input.begin(), input.end(), '_', ' ');
        zones.emplace_back(std::move(input));
    }
    return true;
}

inline const dbus::utility::ManagedObjectType::value_type*
    findChassis(const dbus::utility::ManagedObjectType& managedObj,
                std::string_view value, std::string& chassis)
{
    BMCWEB_LOG_DEBUG("Find Chassis: {}", value);

    std::string escaped(value);
    std::replace(escaped.begin(), escaped.end(), ' ', '_');
    escaped = "/" + escaped;
    auto it = std::ranges::find_if(managedObj, [&escaped](const auto& obj) {
        if (obj.first.str.ends_with(escaped))
        {
            BMCWEB_LOG_DEBUG("Matched {}", obj.first.str);
            return true;
        }
        return false;
    });

    if (it == managedObj.end())
    {
        return nullptr;
    }
    // 5 comes from <chassis-name> being the 5th element
    // /xyz/openbmc_project/inventory/system/chassis/<chassis-name>
    if (dbus::utility::getNthStringFromPath(it->first.str, 5, chassis))
    {
        return &(*it);
    }

    return nullptr;
}

inline CreatePIDRet createPidInterface(
    const std::shared_ptr<bmcweb::AsyncResp>& response, const std::string& type,
    std::string_view name, nlohmann::json& jsonValue, const std::string& path,
    const dbus::utility::ManagedObjectType& managedObj, bool createNewObject,
    dbus::utility::DBusPropertiesMap& output, std::string& chassis,
    const std::string& profile)
{
    // common deleter
    if (jsonValue == nullptr)
    {
        std::string iface;
        if (type == "PidControllers" || type == "FanControllers")
        {
            iface = pidConfigurationIface;
        }
        else if (type == "FanZones")
        {
            iface = pidZoneConfigurationIface;
        }
        else if (type == "StepwiseControllers")
        {
            iface = stepwiseConfigurationIface;
        }
        else
        {
            BMCWEB_LOG_ERROR("Illegal Type {}", type);
            messages::propertyUnknown(response->res, type);
            return CreatePIDRet::fail;
        }

        BMCWEB_LOG_DEBUG("del {} {}", path, iface);
        // delete interface
        crow::connections::systemBus->async_method_call(
            [response, path](const boost::system::error_code& ec) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Error patching {}: {}", path, ec);
                messages::internalError(response->res);
                return;
            }
            messages::success(response->res);
        },
            "xyz.openbmc_project.EntityManager", path, iface, "Delete");
        return CreatePIDRet::del;
    }

    const dbus::utility::ManagedObjectType::value_type* managedItem = nullptr;
    if (!createNewObject)
    {
        // if we aren't creating a new object, we should be able to find it on
        // d-bus
        managedItem = findChassis(managedObj, name, chassis);
        if (managedItem == nullptr)
        {
            BMCWEB_LOG_ERROR("Failed to get chassis from config patch");
            messages::invalidObject(
                response->res,
                boost::urls::format("/redfish/v1/Chassis/{}", chassis));
            return CreatePIDRet::fail;
        }
    }

    if (!profile.empty() &&
        (type == "PidControllers" || type == "FanControllers" ||
         type == "StepwiseControllers"))
    {
        if (managedItem == nullptr)
        {
            output.emplace_back("Profiles", std::vector<std::string>{profile});
        }
        else
        {
            std::string interface;
            if (type == "StepwiseControllers")
            {
                interface = stepwiseConfigurationIface;
            }
            else
            {
                interface = pidConfigurationIface;
            }
            bool ifaceFound = false;
            for (const auto& iface : managedItem->second)
            {
                if (iface.first == interface)
                {
                    ifaceFound = true;
                    for (const auto& prop : iface.second)
                    {
                        if (prop.first == "Profiles")
                        {
                            const std::vector<std::string>* curProfiles =
                                std::get_if<std::vector<std::string>>(
                                    &(prop.second));
                            if (curProfiles == nullptr)
                            {
                                BMCWEB_LOG_ERROR(
                                    "Illegal profiles in managed object");
                                messages::internalError(response->res);
                                return CreatePIDRet::fail;
                            }
                            if (std::find(curProfiles->begin(),
                                          curProfiles->end(),
                                          profile) == curProfiles->end())
                            {
                                std::vector<std::string> newProfiles =
                                    *curProfiles;
                                newProfiles.push_back(profile);
                                output.emplace_back("Profiles", newProfiles);
                            }
                        }
                    }
                }
            }

            if (!ifaceFound)
            {
                BMCWEB_LOG_ERROR("Failed to find interface in managed object");
                messages::internalError(response->res);
                return CreatePIDRet::fail;
            }
        }
    }

    if (type == "PidControllers" || type == "FanControllers")
    {
        if (createNewObject)
        {
            output.emplace_back("Class",
                                type == "PidControllers" ? "temp" : "fan");
            output.emplace_back("Type", "Pid");
        }

        std::optional<std::vector<nlohmann::json::object_t>> zones;
        std::optional<std::vector<std::string>> inputs;
        std::optional<std::vector<std::string>> outputs;
        std::map<std::string, std::optional<double>> doubles;
        std::optional<std::string> setpointOffset;
        if (!redfish::json_util::readJson(
                jsonValue, response->res, "Inputs", inputs, "Outputs", outputs,
                "Zones", zones, "FFGainCoefficient",
                doubles["FFGainCoefficient"], "FFOffCoefficient",
                doubles["FFOffCoefficient"], "ICoefficient",
                doubles["ICoefficient"], "ILimitMax", doubles["ILimitMax"],
                "ILimitMin", doubles["ILimitMin"], "OutLimitMax",
                doubles["OutLimitMax"], "OutLimitMin", doubles["OutLimitMin"],
                "PCoefficient", doubles["PCoefficient"], "SetPoint",
                doubles["SetPoint"], "SetPointOffset", setpointOffset,
                "SlewNeg", doubles["SlewNeg"], "SlewPos", doubles["SlewPos"],
                "PositiveHysteresis", doubles["PositiveHysteresis"],
                "NegativeHysteresis", doubles["NegativeHysteresis"]))
        {
            return CreatePIDRet::fail;
        }
        if (zones)
        {
            std::vector<std::string> zonesStr;
            if (!getZonesFromJsonReq(response, *zones, zonesStr))
            {
                BMCWEB_LOG_ERROR("Illegal Zones");
                return CreatePIDRet::fail;
            }
            if (chassis.empty() &&
                findChassis(managedObj, zonesStr[0], chassis) == nullptr)
            {
                BMCWEB_LOG_ERROR("Failed to get chassis from config patch");
                messages::invalidObject(
                    response->res,
                    boost::urls::format("/redfish/v1/Chassis/{}", chassis));
                return CreatePIDRet::fail;
            }
            output.emplace_back("Zones", std::move(zonesStr));
        }

        if (inputs)
        {
            for (std::string& value : *inputs)
            {
                std::replace(value.begin(), value.end(), '_', ' ');
            }
            output.emplace_back("Inputs", *inputs);
        }

        if (outputs)
        {
            for (std::string& value : *outputs)
            {
                std::replace(value.begin(), value.end(), '_', ' ');
            }
            output.emplace_back("Outputs", *outputs);
        }

        if (setpointOffset)
        {
            // translate between redfish and dbus names
            if (*setpointOffset == "UpperThresholdNonCritical")
            {
                output.emplace_back("SetPointOffset", "WarningLow");
            }
            else if (*setpointOffset == "LowerThresholdNonCritical")
            {
                output.emplace_back("SetPointOffset", "WarningHigh");
            }
            else if (*setpointOffset == "LowerThresholdCritical")
            {
                output.emplace_back("SetPointOffset", "CriticalLow");
            }
            else if (*setpointOffset == "UpperThresholdCritical")
            {
                output.emplace_back("SetPointOffset", "CriticalHigh");
            }
            else
            {
                BMCWEB_LOG_ERROR("Invalid setpointoffset {}", *setpointOffset);
                messages::propertyValueNotInList(response->res, name,
                                                 "SetPointOffset");
                return CreatePIDRet::fail;
            }
        }

        // doubles
        for (const auto& pairs : doubles)
        {
            if (!pairs.second)
            {
                continue;
            }
            BMCWEB_LOG_DEBUG("{} = {}", pairs.first, *pairs.second);
            output.emplace_back(pairs.first, *pairs.second);
        }
    }

    else if (type == "FanZones")
    {
        output.emplace_back("Type", "Pid.Zone");

        std::optional<std::string> chassisId;
        std::optional<double> failSafePercent;
        std::optional<double> minThermalOutput;
        if (!redfish::json_util::readJson(jsonValue, response->res,
                                          "Chassis/@odata.id", chassisId,
                                          "FailSafePercent", failSafePercent,
                                          "MinThermalOutput", minThermalOutput))
        {
            return CreatePIDRet::fail;
        }

        if (chassisId)
        {
            // /redfish/v1/chassis/chassis_name/
            if (!dbus::utility::getNthStringFromPath(*chassisId, 3, chassis))
            {
                BMCWEB_LOG_ERROR("Got invalid path {}", *chassisId);
                messages::invalidObject(
                    response->res,
                    boost::urls::format("/redfish/v1/Chassis/{}", *chassisId));
                return CreatePIDRet::fail;
            }
        }
        if (minThermalOutput)
        {
            output.emplace_back("MinThermalOutput", *minThermalOutput);
        }
        if (failSafePercent)
        {
            output.emplace_back("FailSafePercent", *failSafePercent);
        }
    }
    else if (type == "StepwiseControllers")
    {
        output.emplace_back("Type", "Stepwise");

        std::optional<std::vector<nlohmann::json::object_t>> zones;
        std::optional<std::vector<nlohmann::json::object_t>> steps;
        std::optional<std::vector<std::string>> inputs;
        std::optional<double> positiveHysteresis;
        std::optional<double> negativeHysteresis;
        std::optional<std::string> direction; // upper clipping curve vs lower
        if (!redfish::json_util::readJson(
                jsonValue, response->res, "Zones", zones, "Steps", steps,
                "Inputs", inputs, "PositiveHysteresis", positiveHysteresis,
                "NegativeHysteresis", negativeHysteresis, "Direction",
                direction))
        {
            return CreatePIDRet::fail;
        }

        if (zones)
        {
            std::vector<std::string> zonesStrs;
            if (!getZonesFromJsonReq(response, *zones, zonesStrs))
            {
                BMCWEB_LOG_ERROR("Illegal Zones");
                return CreatePIDRet::fail;
            }
            if (chassis.empty() &&
                findChassis(managedObj, zonesStrs[0], chassis) == nullptr)
            {
                BMCWEB_LOG_ERROR("Failed to get chassis from config patch");
                messages::invalidObject(
                    response->res,
                    boost::urls::format("/redfish/v1/Chassis/{}", chassis));
                return CreatePIDRet::fail;
            }
            output.emplace_back("Zones", std::move(zonesStrs));
        }
        if (steps)
        {
            std::vector<double> readings;
            std::vector<double> outputs;
            for (auto& step : *steps)
            {
                double target = 0.0;
                double out = 0.0;

                if (!redfish::json_util::readJsonObject(
                        step, response->res, "Target", target, "Output", out))
                {
                    return CreatePIDRet::fail;
                }
                readings.emplace_back(target);
                outputs.emplace_back(out);
            }
            output.emplace_back("Reading", std::move(readings));
            output.emplace_back("Output", std::move(outputs));
        }
        if (inputs)
        {
            for (std::string& value : *inputs)
            {
                std::replace(value.begin(), value.end(), '_', ' ');
            }
            output.emplace_back("Inputs", std::move(*inputs));
        }
        if (negativeHysteresis)
        {
            output.emplace_back("NegativeHysteresis", *negativeHysteresis);
        }
        if (positiveHysteresis)
        {
            output.emplace_back("PositiveHysteresis", *positiveHysteresis);
        }
        if (direction)
        {
            constexpr const std::array<const char*, 2> allowedDirections = {
                "Ceiling", "Floor"};
            if (std::ranges::find(allowedDirections, *direction) ==
                allowedDirections.end())
            {
                messages::propertyValueTypeError(response->res, "Direction",
                                                 *direction);
                return CreatePIDRet::fail;
            }
            output.emplace_back("Class", *direction);
        }
    }
    else
    {
        BMCWEB_LOG_ERROR("Illegal Type {}", type);
        messages::propertyUnknown(response->res, type);
        return CreatePIDRet::fail;
    }
    return CreatePIDRet::patch;
}
struct GetPIDValues : std::enable_shared_from_this<GetPIDValues>
{
    struct CompletionValues
    {
        std::vector<std::string> supportedProfiles;
        std::string currentProfile;
        dbus::utility::MapperGetSubTreeResponse subtree;
    };

    explicit GetPIDValues(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncRespIn) :
        asyncResp(asyncRespIn)

    {}

    void run()
    {
        std::shared_ptr<GetPIDValues> self = shared_from_this();

        // get all configurations
        constexpr std::array<std::string_view, 4> interfaces = {
            pidConfigurationIface, pidZoneConfigurationIface,
            objectManagerIface, stepwiseConfigurationIface};
        dbus::utility::getSubTree(
            "/", 0, interfaces,
            [self](
                const boost::system::error_code& ec,
                const dbus::utility::MapperGetSubTreeResponse& subtreeLocal) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("{}", ec);
                messages::internalError(self->asyncResp->res);
                return;
            }
            self->complete.subtree = subtreeLocal;
        });

        // at the same time get the selected profile
        constexpr std::array<std::string_view, 1> thermalModeIfaces = {
            thermalModeIface};
        dbus::utility::getSubTree(
            "/", 0, thermalModeIfaces,
            [self](
                const boost::system::error_code& ec,
                const dbus::utility::MapperGetSubTreeResponse& subtreeLocal) {
            if (ec || subtreeLocal.empty())
            {
                return;
            }
            if (subtreeLocal[0].second.size() != 1)
            {
                // invalid mapper response, should never happen
                BMCWEB_LOG_ERROR("GetPIDValues: Mapper Error");
                messages::internalError(self->asyncResp->res);
                return;
            }

            const std::string& path = subtreeLocal[0].first;
            const std::string& owner = subtreeLocal[0].second[0].first;

            sdbusplus::asio::getAllProperties(
                *crow::connections::systemBus, owner, path, thermalModeIface,
                [path, owner,
                 self](const boost::system::error_code& ec2,
                       const dbus::utility::DBusPropertiesMap& resp) {
                if (ec2)
                {
                    BMCWEB_LOG_ERROR(
                        "GetPIDValues: Can't get thermalModeIface {}", path);
                    messages::internalError(self->asyncResp->res);
                    return;
                }

                const std::string* current = nullptr;
                const std::vector<std::string>* supported = nullptr;

                const bool success = sdbusplus::unpackPropertiesNoThrow(
                    dbus_utils::UnpackErrorPrinter(), resp, "Current", current,
                    "Supported", supported);

                if (!success)
                {
                    messages::internalError(self->asyncResp->res);
                    return;
                }

                if (current == nullptr || supported == nullptr)
                {
                    BMCWEB_LOG_ERROR(
                        "GetPIDValues: thermal mode iface invalid {}", path);
                    messages::internalError(self->asyncResp->res);
                    return;
                }
                self->complete.currentProfile = *current;
                self->complete.supportedProfiles = *supported;
            });
        });
    }

    static void
        processingComplete(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const CompletionValues& completion)
    {
        if (asyncResp->res.result() != boost::beast::http::status::ok)
        {
            return;
        }
        // create map of <connection, path to objMgr>>
        boost::container::flat_map<
            std::string, std::string, std::less<>,
            std::vector<std::pair<std::string, std::string>>>
            objectMgrPaths;
        boost::container::flat_set<std::string, std::less<>,
                                   std::vector<std::string>>
            calledConnections;
        for (const auto& pathGroup : completion.subtree)
        {
            for (const auto& connectionGroup : pathGroup.second)
            {
                auto findConnection =
                    calledConnections.find(connectionGroup.first);
                if (findConnection != calledConnections.end())
                {
                    break;
                }
                for (const std::string& interface : connectionGroup.second)
                {
                    if (interface == objectManagerIface)
                    {
                        objectMgrPaths[connectionGroup.first] = pathGroup.first;
                    }
                    // this list is alphabetical, so we
                    // should have found the objMgr by now
                    if (interface == pidConfigurationIface ||
                        interface == pidZoneConfigurationIface ||
                        interface == stepwiseConfigurationIface)
                    {
                        auto findObjMgr =
                            objectMgrPaths.find(connectionGroup.first);
                        if (findObjMgr == objectMgrPaths.end())
                        {
                            BMCWEB_LOG_DEBUG("{}Has no Object Manager",
                                             connectionGroup.first);
                            continue;
                        }

                        calledConnections.insert(connectionGroup.first);

                        asyncPopulatePid(findObjMgr->first, findObjMgr->second,
                                         completion.currentProfile,
                                         completion.supportedProfiles,
                                         asyncResp);
                        break;
                    }
                }
            }
        }
    }

    ~GetPIDValues()
    {
        boost::asio::post(crow::connections::systemBus->get_io_context(),
                          std::bind_front(&processingComplete, asyncResp,
                                          std::move(complete)));
    }

    GetPIDValues(const GetPIDValues&) = delete;
    GetPIDValues(GetPIDValues&&) = delete;
    GetPIDValues& operator=(const GetPIDValues&) = delete;
    GetPIDValues& operator=(GetPIDValues&&) = delete;

    std::shared_ptr<bmcweb::AsyncResp> asyncResp;
    CompletionValues complete;
};

struct SetPIDValues : std::enable_shared_from_this<SetPIDValues>
{
    SetPIDValues(
        const std::shared_ptr<bmcweb::AsyncResp>& asyncRespIn,
        std::vector<
            std::pair<std::string, std::optional<nlohmann::json::object_t>>>&&
            configurationsIn,
        std::optional<std::string>& profileIn) :
        asyncResp(asyncRespIn),
        configuration(std::move(configurationsIn)),
        profile(std::move(profileIn))
    {}

    SetPIDValues(const SetPIDValues&) = delete;
    SetPIDValues(SetPIDValues&&) = delete;
    SetPIDValues& operator=(const SetPIDValues&) = delete;
    SetPIDValues& operator=(SetPIDValues&&) = delete;

    void run()
    {
        if (asyncResp->res.result() != boost::beast::http::status::ok)
        {
            return;
        }

        std::shared_ptr<SetPIDValues> self = shared_from_this();

        // todo(james): might make sense to do a mapper call here if this
        // interface gets more traction
        sdbusplus::message::object_path objPath(
            "/xyz/openbmc_project/inventory");
        dbus::utility::getManagedObjects(
            "xyz.openbmc_project.EntityManager", objPath,
            [self](const boost::system::error_code& ec,
                   const dbus::utility::ManagedObjectType& mObj) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Error communicating to Entity Manager");
                messages::internalError(self->asyncResp->res);
                return;
            }
            const std::array<const char*, 3> configurations = {
                pidConfigurationIface, pidZoneConfigurationIface,
                stepwiseConfigurationIface};

            for (const auto& [path, object] : mObj)
            {
                for (const auto& [interface, _] : object)
                {
                    if (std::ranges::find(configurations, interface) !=
                        configurations.end())
                    {
                        self->objectCount++;
                        break;
                    }
                }
            }
            self->managedObj = mObj;
        });

        // at the same time get the profile information
        constexpr std::array<std::string_view, 1> thermalModeIfaces = {
            thermalModeIface};
        dbus::utility::getSubTree(
            "/", 0, thermalModeIfaces,
            [self](const boost::system::error_code& ec,
                   const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec || subtree.empty())
            {
                return;
            }
            if (subtree[0].second.empty())
            {
                // invalid mapper response, should never happen
                BMCWEB_LOG_ERROR("SetPIDValues: Mapper Error");
                messages::internalError(self->asyncResp->res);
                return;
            }

            const std::string& path = subtree[0].first;
            const std::string& owner = subtree[0].second[0].first;
            sdbusplus::asio::getAllProperties(
                *crow::connections::systemBus, owner, path, thermalModeIface,
                [self, path, owner](const boost::system::error_code& ec2,
                                    const dbus::utility::DBusPropertiesMap& r) {
                if (ec2)
                {
                    BMCWEB_LOG_ERROR(
                        "SetPIDValues: Can't get thermalModeIface {}", path);
                    messages::internalError(self->asyncResp->res);
                    return;
                }
                const std::string* current = nullptr;
                const std::vector<std::string>* supported = nullptr;

                const bool success = sdbusplus::unpackPropertiesNoThrow(
                    dbus_utils::UnpackErrorPrinter(), r, "Current", current,
                    "Supported", supported);

                if (!success)
                {
                    messages::internalError(self->asyncResp->res);
                    return;
                }

                if (current == nullptr || supported == nullptr)
                {
                    BMCWEB_LOG_ERROR(
                        "SetPIDValues: thermal mode iface invalid {}", path);
                    messages::internalError(self->asyncResp->res);
                    return;
                }
                self->currentProfile = *current;
                self->supportedProfiles = *supported;
                self->profileConnection = owner;
                self->profilePath = path;
            });
        });
    }
    void pidSetDone()
    {
        if (asyncResp->res.result() != boost::beast::http::status::ok)
        {
            return;
        }
        std::shared_ptr<bmcweb::AsyncResp> response = asyncResp;
        if (profile)
        {
            if (std::ranges::find(supportedProfiles, *profile) ==
                supportedProfiles.end())
            {
                messages::actionParameterUnknown(response->res, "Profile",
                                                 *profile);
                return;
            }
            currentProfile = *profile;
            sdbusplus::asio::setProperty(
                *crow::connections::systemBus, profileConnection, profilePath,
                thermalModeIface, "Current", *profile,
                [response](const boost::system::error_code& ec) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR("Error patching profile{}", ec);
                    messages::internalError(response->res);
                }
            });
        }

        for (auto& containerPair : configuration)
        {
            auto& container = containerPair.second;
            if (!container)
            {
                continue;
            }

            const std::string& type = containerPair.first;

            for (auto& [name, value] : *container)
            {
                std::string dbusObjName = name;
                std::replace(dbusObjName.begin(), dbusObjName.end(), ' ', '_');
                BMCWEB_LOG_DEBUG("looking for {}", name);

                auto pathItr = std::ranges::find_if(
                    managedObj, [&dbusObjName](const auto& obj) {
                    return obj.first.filename() == dbusObjName;
                });
                dbus::utility::DBusPropertiesMap output;

                output.reserve(16); // The pid interface length

                // determines if we're patching entity-manager or
                // creating a new object
                bool createNewObject = (pathItr == managedObj.end());
                BMCWEB_LOG_DEBUG("Found = {}", !createNewObject);

                std::string iface;
                if (!createNewObject)
                {
                    bool findInterface = false;
                    for (const auto& interface : pathItr->second)
                    {
                        if (interface.first == pidConfigurationIface)
                        {
                            if (type == "PidControllers" ||
                                type == "FanControllers")
                            {
                                iface = pidConfigurationIface;
                                findInterface = true;
                                break;
                            }
                        }
                        else if (interface.first == pidZoneConfigurationIface)
                        {
                            if (type == "FanZones")
                            {
                                iface = pidZoneConfigurationIface;
                                findInterface = true;
                                break;
                            }
                        }
                        else if (interface.first == stepwiseConfigurationIface)
                        {
                            if (type == "StepwiseControllers")
                            {
                                iface = stepwiseConfigurationIface;
                                findInterface = true;
                                break;
                            }
                        }
                    }

                    // create new object if interface not found
                    if (!findInterface)
                    {
                        createNewObject = true;
                    }
                }

                if (createNewObject && value == nullptr)
                {
                    // can't delete a non-existent object
                    messages::propertyValueNotInList(response->res, value,
                                                     name);
                    continue;
                }

                std::string path;
                if (pathItr != managedObj.end())
                {
                    path = pathItr->first.str;
                }

                BMCWEB_LOG_DEBUG("Create new = {}", createNewObject);

                // arbitrary limit to avoid attacks
                constexpr const size_t controllerLimit = 500;
                if (createNewObject && objectCount >= controllerLimit)
                {
                    messages::resourceExhaustion(response->res, type);
                    continue;
                }
                std::string escaped = name;
                std::replace(escaped.begin(), escaped.end(), '_', ' ');
                output.emplace_back("Name", escaped);

                std::string chassis;
                CreatePIDRet ret = createPidInterface(
                    response, type, name, value, path, managedObj,
                    createNewObject, output, chassis, currentProfile);
                if (ret == CreatePIDRet::fail)
                {
                    return;
                }
                if (ret == CreatePIDRet::del)
                {
                    continue;
                }

                if (!createNewObject)
                {
                    for (const auto& property : output)
                    {
                        crow::connections::systemBus->async_method_call(
                            [response,
                             propertyName{std::string(property.first)}](
                                const boost::system::error_code& ec) {
                            if (ec)
                            {
                                BMCWEB_LOG_ERROR("Error patching {}: {}",
                                                 propertyName, ec);
                                messages::internalError(response->res);
                                return;
                            }
                            messages::success(response->res);
                        },
                            "xyz.openbmc_project.EntityManager", path,
                            "org.freedesktop.DBus.Properties", "Set", iface,
                            property.first, property.second);
                    }
                }
                else
                {
                    if (chassis.empty())
                    {
                        BMCWEB_LOG_ERROR("Failed to get chassis from config");
                        messages::internalError(response->res);
                        return;
                    }

                    bool foundChassis = false;
                    for (const auto& obj : managedObj)
                    {
                        if (obj.first.filename() == chassis)
                        {
                            chassis = obj.first.str;
                            foundChassis = true;
                            break;
                        }
                    }
                    if (!foundChassis)
                    {
                        BMCWEB_LOG_ERROR("Failed to find chassis on dbus");
                        messages::resourceMissingAtURI(
                            response->res,
                            boost::urls::format("/redfish/v1/Chassis/{}",
                                                chassis));
                        return;
                    }

                    crow::connections::systemBus->async_method_call(
                        [response](const boost::system::error_code& ec) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR("Error Adding Pid Object {}", ec);
                            messages::internalError(response->res);
                            return;
                        }
                        messages::success(response->res);
                    },
                        "xyz.openbmc_project.EntityManager", chassis,
                        "xyz.openbmc_project.AddObject", "AddObject", output);
                }
            }
        }
    }

    ~SetPIDValues()
    {
        try
        {
            pidSetDone();
        }
        catch (...)
        {
            BMCWEB_LOG_CRITICAL("pidSetDone threw exception");
        }
    }

    std::shared_ptr<bmcweb::AsyncResp> asyncResp;
    std::vector<std::pair<std::string, std::optional<nlohmann::json::object_t>>>
        configuration;
    std::optional<std::string> profile;
    dbus::utility::ManagedObjectType managedObj;
    std::vector<std::string> supportedProfiles;
    std::string currentProfile;
    std::string profileConnection;
    std::string profilePath;
    size_t objectCount = 0;
};

/**
 * @brief Retrieves manager service state data over DBus
 *
 * @param[in] aResp Shared pointer for completing asynchronous calls
 * @param[in] connectionName - service name
 * @param[in] path - object path
 * @return none
 */
inline void getManagerState(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            const std::string& connectionName,
                            const std::string& path)
{
    BMCWEB_LOG_DEBUG("Get manager service state.");

    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec,
                const std::vector<std::pair<
                    std::string, std::variant<std::string>>>& propertiesList) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("Error in getting manager service state");
            messages::internalError(aResp->res);
            return;
        }
        for (const std::pair<std::string, std::variant<std::string>>& property :
             propertiesList)
        {
            if (property.first == "State")
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value == nullptr)
                {
                    BMCWEB_LOG_DEBUG("Null value returned "
                                     "for manager service state");
                    messages::internalError(aResp->res);
                    return;
                }
                std::string state =
                    redfish::chassis_utils::getPowerStateType(*value);
                aResp->res.jsonValue["Status"]["State"] = state;
                if (state == "Enabled")
                {
                    aResp->res.jsonValue["Status"]["Health"] = "OK";
                }
                else
                {
                    aResp->res.jsonValue["Status"]["Health"] = "Critical";
                }
            }
        }
    },
        connectionName, path, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.State.Decorator.OperationalStatus");
}

/**
 * @brief Retrieves BMC asset properties data over DBus
 *
 * @param[in] aResp Shared pointer for completing asynchronous calls
 * @param[in] connectionName - service name
 * @param[in] path - object path
 * @return none
 */
inline void getBMCAssetData(const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                            const std::string& connectionName,
                            const std::string& path)
{
    BMCWEB_LOG_DEBUG("Get BMC manager asset data.");
    crow::connections::systemBus->async_method_call(
        [aResp](const boost::system::error_code ec,
                const std::vector<std::pair<
                    std::string, std::variant<std::string>>>& propertiesList) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("Can't get bmc asset!");
            messages::internalError(aResp->res);
            return;
        }
        for (const std::pair<std::string, std::variant<std::string>>& property :
             propertiesList)
        {
            const std::string& propertyName = property.first;

            if ((propertyName == "PartNumber") ||
                (propertyName == "SerialNumber") ||
                (propertyName == "Manufacturer") || (propertyName == "Model") ||
                (propertyName == "SparePartNumber") || (propertyName == "Name"))
            {
                const std::string* value =
                    std::get_if<std::string>(&property.second);
                if (value == nullptr)
                {
                    // illegal property
                    messages::internalError(aResp->res);
                    return;
                }
                aResp->res.jsonValue[propertyName] = *value;
            }
        }
    },
        connectionName, path, "org.freedesktop.DBus.Properties", "GetAll",
        "xyz.openbmc_project.Inventory.Decorator."
        "Asset");
}

/**
 * @brief Retrieves BMC manager location data over DBus
 *
 * @param[in] asyncResp Shared pointer for completing asynchronous calls
 * @param[in] connectionName - service name
 * @param[in] path - object path
 * @return none
 */
inline void getLocation(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& connectionName,
                        const std::string& path)
{
    BMCWEB_LOG_DEBUG("Get BMC manager Location data.");

    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, connectionName, path,
        "xyz.openbmc_project.Inventory.Decorator.LocationCode", "LocationCode",
        [asyncResp](const boost::system::error_code& ec,
                    const std::string& property) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error for "
                             "Location");
            messages::internalError(asyncResp->res);
            return;
        }

        asyncResp->res.jsonValue["Location"]["PartLocation"]["ServiceLabel"] =
            property;
    });
}
// avoid name collision systems.hpp
inline void
    managerGetLastResetTime(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    BMCWEB_LOG_DEBUG("Getting Manager Last Reset Time");

    sdbusplus::asio::getProperty<uint64_t>(
        *crow::connections::systemBus, "xyz.openbmc_project.State.BMC",
        "/xyz/openbmc_project/state/bmc0", "xyz.openbmc_project.State.BMC",
        "LastRebootTime",
        [asyncResp](const boost::system::error_code& ec,
                    const uint64_t lastResetTime) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("D-BUS response error {}", ec);
            return;
        }

        // LastRebootTime is epoch time, in milliseconds
        // https://github.com/openbmc/phosphor-dbus-interfaces/blob/7f9a128eb9296e926422ddc312c148b625890bb6/xyz/openbmc_project/State/BMC.interface.yaml#L19
        uint64_t lastResetTimeStamp = lastResetTime / 1000;

        // Convert to ISO 8601 standard
        asyncResp->res.jsonValue["LastResetTime"] =
            redfish::time_utils::getDateTimeUint(lastResetTimeStamp);
    });
}

/**
 * @brief Set the running firmware image
 *
 * @param[i,o] asyncResp - Async response object
 * @param[i] runningFirmwareTarget - Image to make the running image
 *
 * @return void
 */
inline void
    setActiveFirmwareImage(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                           const std::string& runningFirmwareTarget)
{
    // Get the Id from /redfish/v1/UpdateService/FirmwareInventory/<Id>
    std::string::size_type idPos = runningFirmwareTarget.rfind('/');
    if (idPos == std::string::npos)
    {
        messages::propertyValueNotInList(asyncResp->res, runningFirmwareTarget,
                                         "@odata.id");
        BMCWEB_LOG_DEBUG("Can't parse firmware ID!");
        return;
    }
    idPos++;
    if (idPos >= runningFirmwareTarget.size())
    {
        messages::propertyValueNotInList(asyncResp->res, runningFirmwareTarget,
                                         "@odata.id");
        BMCWEB_LOG_DEBUG("Invalid firmware ID.");
        return;
    }
    std::string firmwareId = runningFirmwareTarget.substr(idPos);

    // Make sure the image is valid before setting priority
    sdbusplus::message::object_path objPath("/xyz/openbmc_project/software");
    dbus::utility::getManagedObjects(
        "xyz.openbmc_project.Software.BMC.Updater", objPath,
        [asyncResp, firmwareId, runningFirmwareTarget](
            const boost::system::error_code& ec,
            const dbus::utility::ManagedObjectType& subtree) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("D-Bus response error getting objects.");
            messages::internalError(asyncResp->res);
            return;
        }

        if (subtree.empty())
        {
            BMCWEB_LOG_DEBUG("Can't find image!");
            messages::internalError(asyncResp->res);
            return;
        }

        bool foundImage = false;
        for (const auto& object : subtree)
        {
            const std::string& path =
                static_cast<const std::string&>(object.first);
            std::size_t idPos2 = path.rfind('/');

            if (idPos2 == std::string::npos)
            {
                continue;
            }

            idPos2++;
            if (idPos2 >= path.size())
            {
                continue;
            }

            if (path.substr(idPos2) == firmwareId)
            {
                foundImage = true;
                break;
            }
        }

        if (!foundImage)
        {
            messages::propertyValueNotInList(
                asyncResp->res, runningFirmwareTarget, "@odata.id");
            BMCWEB_LOG_DEBUG("Invalid firmware ID.");
            return;
        }

        BMCWEB_LOG_DEBUG("Setting firmware version {} to priority 0.",
                         firmwareId);

        // Only support Immediate
        // An addition could be a Redfish Setting like
        // ActiveSoftwareImageApplyTime and support OnReset
        sdbusplus::asio::setProperty(
            *crow::connections::systemBus,
            "xyz.openbmc_project.Software.BMC.Updater",
            "/xyz/openbmc_project/software/" + firmwareId,
            "xyz.openbmc_project.Software.RedundancyPriority", "Priority",
            static_cast<uint8_t>(0),
            [asyncResp](const boost::system::error_code& ec2) {
            if (ec2)
            {
                BMCWEB_LOG_DEBUG("D-Bus response error setting.");
                messages::internalError(asyncResp->res);
                return;
            }
            doBMCGracefulRestart(asyncResp);
        });
    });
}

inline void setServiceIdentification(std::shared_ptr<bmcweb::AsyncResp> aResp,
                                     std::string sysId)
{
    sdbusplus::asio::setProperty(
        *crow::connections::systemBus, "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/Software/Settings/ServiceIdentification",
        "xyz.openbmc_project.Inventory.Decorator.AssetTag", "AssetTag", sysId,
        [aResp{std::move(aResp)}](const boost::system::error_code ec) {
        if (ec)
        {
            BMCWEB_LOG_ERROR(
                "DBUS response error on ServiceIdentification setProperty: {}",
                ec);
            return;
        }
    });
}

inline void getServiceIdentification(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/Software/Settings/ServiceIdentification",
        "xyz.openbmc_project.Inventory.Decorator.AssetTag", "AssetTag",
        [asyncResp](const boost::system::error_code ec,
                    const std::string& sysId) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error for ServiceIdentification {}",
                             ec);
            return;
        }
        asyncResp->res.jsonValue["ServiceIdentification"] = sysId;
    });
}

inline void
    afterSetDateTime(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const boost::system::error_code& ec,
                     const sdbusplus::message_t& msg)
{
    if (ec)
    {
        BMCWEB_LOG_DEBUG("Failed to set elapsed time. DBUS response error {}",
                         ec);
        const sd_bus_error* dbusError = msg.get_error();
        if (dbusError != nullptr)
        {
            std::string_view errorName(dbusError->name);
            if (errorName ==
                "org.freedesktop.timedate1.AutomaticTimeSyncEnabled")
            {
                BMCWEB_LOG_DEBUG("Setting conflict");
                messages::propertyValueConflict(
                    asyncResp->res, "DateTime",
                    "Managers/NetworkProtocol/NTPProcotolEnabled");
                return;
            }
        }
        messages::internalError(asyncResp->res);
        return;
    }
    asyncResp->res.result(boost::beast::http::status::no_content);
}

inline void setDateTime(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& datetime)
{
    BMCWEB_LOG_DEBUG("Set date time: {}", datetime);

    std::optional<redfish::time_utils::usSinceEpoch> us =
        redfish::time_utils::dateStringToEpoch(datetime);
    if (!us)
    {
        messages::propertyValueFormatError(asyncResp->res, datetime,
                                           "DateTime");
        return;
    }
    // Set the absolute datetime
    bool relative = false;
    bool interactive = false;
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code& ec,
                    const sdbusplus::message_t& msg) {
        afterSetDateTime(asyncResp, ec, msg);
    },
        "org.freedesktop.timedate1", "/org/freedesktop/timedate1",
        "org.freedesktop.timedate1", "SetTime", us->count(), relative,
        interactive);
}

inline void getLinkManagerForSwitches(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objPath)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    std::variant<std::vector<std::string>>& resp) {
        if (ec)
        {
            return; // no fabric = no failures
        }
        std::vector<std::string>* objects =
            std::get_if<std::vector<std::string>>(&resp);
        if (objects == nullptr)
        {
            return;
        }
        asyncResp->res.jsonValue["Links"]["ManagerForSwitches"] =
            nlohmann::json::array();
        for (const std::string& fabric : *objects)
        {
            sdbusplus::message::object_path path(fabric);
            std::string fabricId = path.filename();
            crow::connections::systemBus->async_method_call(
                [asyncResp, fabric,
                 fabricId](const boost::system::error_code ec,
                           const GetSubTreeType& subtree) {
                if (ec)
                {
                    messages::internalError(asyncResp->res);
                }
                nlohmann::json& tempArray =
                    asyncResp->res.jsonValue["Links"]["ManagerForSwitches"];
                for (const std::pair<
                         std::string,
                         std::vector<
                             std::pair<std::string, std::vector<std::string>>>>&
                         object : subtree)
                {
                    const std::string& path = object.first;
                    sdbusplus::message::object_path objPath(path);
                    std::string switchId = objPath.filename();
                    std::string managerUri = "/redfish/v1/Fabrics/";
                    managerUri += fabricId + "/Switches/";
                    managerUri += switchId;

                    tempArray.push_back({{"@odata.id", managerUri}});
                }
            },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree", fabric, 0,
                std::array<const char*, 1>{
                    "xyz.openbmc_project.Inventory.Item.Switch"});
        }
    },
        "xyz.openbmc_project.ObjectMapper", objPath + "/fabric",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
inline void
    getFencingPrivilege(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp](const boost::system::error_code ec,
                    const MapperGetSubTreeResponse& subtree) {
        if (ec)
        {
            messages::internalError(asyncResp->res);
            return;
        }
        for (const auto& [objectPath, serviceMap] : subtree)
        {
            if (serviceMap.size() < 1)
            {
                BMCWEB_LOG_ERROR("Got 0 service "
                                 "names");
                messages::internalError(asyncResp->res);
                return;
            }
            const std::string& serviceName = serviceMap[0].first;
            // Get SMBPBI Fencing Privilege
            crow::connections::systemBus->async_method_call(
                [asyncResp](
                    const boost::system::error_code ec,
                    const std::vector<std::pair<
                        std::string, std::variant<std::string, uint8_t>>>&
                        propertiesList) {
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "DBUS response error: Unable to get the smbpbi fencing privilege {}",
                        ec);
                    messages::internalError(asyncResp->res);
                }

                for (const std::pair<std::string,
                                     std::variant<std::string, uint8_t>>&
                         property : propertiesList)
                {
                    if (property.first == "SMBPBIFencingState")
                    {
                        const uint8_t* fencingPrivilege =
                            std::get_if<uint8_t>(&property.second);
                        if (fencingPrivilege == nullptr)
                        {
                            BMCWEB_LOG_DEBUG("Null value returned "
                                             "for SMBPBI privilege");
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        nlohmann::json& json = asyncResp->res.jsonValue;
                        json["Oem"]["Nvidia"]["@odata.type"] =
                            "#NvidiaManager.v1_2_0.NvidiaManager";
                        json["Oem"]["Nvidia"]["SMBPBIFencingPrivilege"] =
                            redfish::dbus_utils::toSMPBIPrivilegeString(
                                *fencingPrivilege);
                    }
                }
            },
                serviceName, objectPath, "org.freedesktop.DBus.Properties",
                "GetAll", "xyz.openbmc_project.GpuOobRecovery.Server");
        }
    },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree", "/", int32_t(0),
        std::array<const char*, 1>{
            "xyz.openbmc_project.GpuOobRecovery.Server"});
}

inline void
    patchFencingPrivilege(const std::shared_ptr<bmcweb::AsyncResp>& resp,
                          const std::string& privilegeType,
                          const std::string& serviceName,
                          const std::string& objPath)
{
    uint8_t privilege =
        redfish::dbus_utils::toSMPBIPrivilegeType(privilegeType);

    // Validate privilege type
    if (privilege == 0)
    {
        messages::invalidObject(resp->res,
                                boost::urls::format("{}", privilegeType));
        return;
    }

    // Set the property, with handler to check error responses
    crow::connections::systemBus->async_method_call(
        [resp, privilege, privilegeType](boost::system::error_code ec,
                                         sdbusplus::message::message& msg) {
        if (!ec)
        {
            BMCWEB_LOG_DEBUG("Set SMBPBI privilege  property succeeded");
            return;
        }
        BMCWEB_LOG_DEBUG(" set SMBPBI privilege  property failed: {}", ec);
        // Read and convert dbus error message to redfish error
        const sd_bus_error* dbusError = msg.get_error();
        if (dbusError == nullptr)
        {
            messages::internalError(resp->res);
            return;
        }
        if (strcmp(dbusError->name,
                   "xyz.openbmc_project.Common.Error.InvalidArgument") == 0)
        {
            // Invalid value
            messages::propertyValueIncorrect(
                resp->res, "SMBPBIFencingPrivilege", privilegeType);
        }

        if (strcmp(dbusError->name, "xyz.openbmc_project.Common."
                                    "Device.Error.WriteFailure") == 0)
        {
            // Service failed to change the config
            messages::operationFailed(resp->res);
        }
        else
        {
            messages::internalError(resp->res);
        }
    },
        serviceName, objPath, "org.freedesktop.DBus.Properties", "Set",
        "xyz.openbmc_project.GpuOobRecovery.Server", "SMBPBIFencingState",
        std::variant<uint8_t>(privilege));
}
#endif // BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES

inline void
    getIsCommandShellEnable(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    sdbusplus::asio::getProperty<bool>(
        *crow::connections::systemBus, "xyz.openbmc_project.Settings",
        "/xyz/openbmc_project/ipmi/sol/eth0", "xyz.openbmc_project.Ipmi.SOL",
        "Enable",
        [asyncResp](const boost::system::error_code ec, const bool& isEnable) {
        if (ec)
        {
            BMCWEB_LOG_DEBUG("DBUS response error for getIsCommandShellEnable");
            messages::internalError(asyncResp->res);
            return;
        }
        asyncResp->res.jsonValue["CommandShell"]["ServiceEnabled"] = isEnable;
    });
}

inline void
    checkForQuiesced(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& bmcId)
{
    sdbusplus::asio::getProperty<std::string>(
        *crow::connections::systemBus, "org.freedesktop.systemd1",
        "/org/freedesktop/systemd1/unit/obmc-bmc-service-quiesce@0.target",
        "org.freedesktop.systemd1.Unit", "ActiveState",
        [asyncResp, bmcId](const boost::system::error_code& ec,
                           const std::string& val) {
        if (!ec)
        {
            if (val == "active")
            {
                asyncResp->res.jsonValue["Status"]["Health"] = "Critical";
                asyncResp->res.jsonValue["Status"]["State"] = "Quiesced";
                return;
            }
        }
        asyncResp->res.jsonValue["Status"]["Health"] = "OK";
        asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
        nvidia_manager_util::getOemReadyState(asyncResp, bmcId);
    });
}

inline void requestRoutesManager(App& app)
{
    std::string uuid = persistent_data::getConfig().systemUuid;

    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/")
        .privileges(redfish::privileges::getManager)
        .methods(boost::beast::http::verb::get)(
            [&app, uuid](const crow::Request& req,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& managerId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        // Process non bmc service manager
        if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
        {
            crow::connections::systemBus->async_method_call(
                [asyncResp, managerId](
                    const boost::system::error_code ec,
                    const std::vector<
                        std::pair<std::string,
                                  std::vector<std::pair<
                                      std::string, std::vector<std::string>>>>>&
                        subtree) {
                if (ec)
                {
                    BMCWEB_LOG_DEBUG("D-Bus response error on GetSubTree {}",
                                     ec);
                    return;
                }
                // Iterate over all retrieved ObjectPaths.
                for (const std::pair<
                         std::string,
                         std::vector<
                             std::pair<std::string, std::vector<std::string>>>>&
                         object : subtree)
                {
                    const std::string& path = object.first;
                    const std::vector<
                        std::pair<std::string, std::vector<std::string>>>&
                        connectionNames = object.second;

                    if (!boost::ends_with(path, managerId))
                    {
                        continue;
                    }
                    if (connectionNames.size() < 1)
                    {
                        BMCWEB_LOG_ERROR("Got 0 Connection names");
                        continue;
                    }

                    asyncResp->res.jsonValue["@odata.id"] =
                        "/redfish/v1/Managers/" + managerId;
                    asyncResp->res.jsonValue["@odata.type"] =
                        "#Manager.v1_11_0.Manager";
                    asyncResp->res.jsonValue["Id"] = managerId;
                    asyncResp->res.jsonValue["Name"] =
                        "OpenBmc Manager Service";
                    asyncResp->res.jsonValue["Description"] =
                        "Software Service for Baseboard Management "
                        "Functions";
                    asyncResp->res.jsonValue["ManagerType"] = "Service";
                    const std::string& connectionName =
                        connectionNames[0].first;
                    const std::vector<std::string>& interfaces =
                        connectionNames[0].second;

                    for (const auto& interfaceName : interfaces)
                    {
                        if (interfaceName ==
                            "xyz.openbmc_project.Inventory.Decorator."
                            "Asset")
                        {
                            getBMCAssetData(asyncResp, connectionName, path);
                        }
                        else if (interfaceName == "xyz.openbmc_project.State."
                                                  "Decorator.OperationalStatus")
                        {
                            getManagerState(asyncResp, connectionName, path);
                        }
                    }
                    getLinkManagerForSwitches(asyncResp, path);

#ifndef BMCWEB_DISABLE_CONDITIONS_ARRAY
                    redfish::conditions_utils::populateServiceConditions(
                        asyncResp, managerId);
#endif // BMCWEB_DISABLE_CONDITIONS_ARRAY

#ifdef BMCWEB_ENABLE_HEALTH_ROLLUP_ALTERNATIVE
                    auto health = std::make_shared<HealthRollup>(
                        path, [asyncResp](const std::string& rootHealth,
                                          const std::string& healthRollup) {
                        asyncResp->res.jsonValue["Status"]["Health"] =
                            rootHealth;
#ifndef BMCWEB_DISABLE_HEALTH_ROLLUP
                        asyncResp->res.jsonValue["Status"]["HealthRollup"] =
                            healthRollup;
#endif // BMCWEB_DISABLE_HEALTH_ROLLUP
                    });
                    health->start();
#endif

                    return;
                }
                messages::resourceNotFound(
                    asyncResp->res, "#Manager.v1_11_0.Manager", managerId);
            },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                "/xyz/openbmc_project/inventory", int32_t(0),
                std::array<const char*, 1>{"xyz.openbmc_project.Inventory."
                                           "Item.ManagementService"});
            return;
        }
        // Process BMC manager

        asyncResp->res.jsonValue["@odata.id"] = boost::urls::format(
            "/redfish/v1/Managers/{}", BMCWEB_REDFISH_MANAGER_URI_NAME);
        asyncResp->res.jsonValue["@odata.type"] = "#Manager.v1_14_0.Manager";
        asyncResp->res.jsonValue["Id"] = BMCWEB_REDFISH_MANAGER_URI_NAME;
        asyncResp->res.jsonValue["Name"] = "OpenBmc Manager";
        asyncResp->res.jsonValue["Description"] =
            "Baseboard Management Controller";
        asyncResp->res.jsonValue["PowerState"] = "On";
        asyncResp->res.jsonValue["Status"]["State"] = "Starting";
        asyncResp->res.jsonValue["Status"]["Health"] = "OK";

        asyncResp->res.jsonValue["ManagerType"] = "BMC";
        asyncResp->res.jsonValue["UUID"] = systemd_utils::getUuid();
        asyncResp->res.jsonValue["ServiceEntryPointUUID"] = uuid;
        asyncResp->res.jsonValue["Model"] = "OpenBmc"; // TODO(ed), get model

        asyncResp->res.jsonValue["LogServices"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Managers/{}/LogServices",
                                BMCWEB_REDFISH_MANAGER_URI_NAME);
#ifdef BMCWEB_ENABLE_LLDP_DEDICATED_PORTS
        asyncResp->res.jsonValue["DedicatedNetworkPorts"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Managers/{}/DedicatedNetworkPorts",
                                BMCWEB_REDFISH_MANAGER_URI_NAME);
#endif
#ifndef BMCWEB_DISABLE_CONDITIONS_ARRAY
        redfish::conditions_utils::populateServiceConditions(
            asyncResp, std::string(BMCWEB_REDFISH_MANAGER_URI_NAME));
#endif // BMCWEB_DISABLE_CONDITIONS_ARRAY

#ifdef BMCWEB_ENABLE_HOST_IFACE
        asyncResp->res.jsonValue["HostInterfaces"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Managers/{}/HostInterfaces",
                                BMCWEB_REDFISH_MANAGER_URI_NAME);
#endif

#ifdef BMCWEB_ENABLE_RMEDIA
        asyncResp->res.jsonValue["VirtualMedia"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Managers/{}/VirtualMedia",
                                BMCWEB_REDFISH_MANAGER_URI_NAME);
#endif
        asyncResp->res.jsonValue["NetworkProtocol"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Managers/{}/NetworkProtocol",
                                BMCWEB_REDFISH_MANAGER_URI_NAME);
        asyncResp->res.jsonValue["EthernetInterfaces"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Managers/{}/EthernetInterfaces",
                                BMCWEB_REDFISH_MANAGER_URI_NAME);

        if constexpr (BMCWEB_VM_NBDPROXY)
        {
            asyncResp->res.jsonValue["VirtualMedia"]["@odata.id"] =
                boost::urls::format("/redfish/v1/Managers/{}/VirtualMedia",
                                    BMCWEB_REDFISH_MANAGER_URI_NAME);
        }

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
        // default oem data
        nlohmann::json& oem = asyncResp->res.jsonValue["Oem"];
#ifdef BMCWEB_ENABLE_HOST_OS_FEATURE
        nlohmann::json& oemOpenbmc = oem["OpenBmc"];

        oemOpenbmc["@odata.type"] = "#OpenBMCManager.OpenBmc";
        oemOpenbmc["@odata.id"] = "/redfish/v1/Managers/" +
                                  std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                                  "#/Oem/OpenBmc";

        oemOpenbmc["Certificates"] = {
            {"@odata.id", "/redfish/v1/Managers/" +
                              std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                              "/Truststore/Certificates"}};

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_COMMON_PROPERTIES
        oem["Nvidia"]["@odata.id"] =
            "/redfish/v1/Managers/" +
            std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) + "/Oem/Nvidia";
#endif // BMCWEB_ENABLE_NVIDIA_OEM_COMMON_PROPERTIES

#endif // BMCWEB_ENABLE_HOST_OS_FEATURE

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_OBERON_PROPERTIES
        oem["Nvidia"]["UptimeSeconds"] = [asyncResp]() -> double {
            double uptime = 0;
            auto ifs = std::ifstream("/proc/uptime", std::ifstream::in);
            if (ifs.good())
            {
                ifs >> uptime;
            }
            else
            {
                BMCWEB_LOG_ERROR("Failed to get uptime from /proc/uptime.");
                messages::internalError(asyncResp->res);
            }
            return uptime;
        }();
#endif // BMCWEB_ENABLE_NVIDIA_OEM_OBERON_PROPERTIES

        if constexpr (BMCWEB_ENABLE_DEBUG_INTERFACE)
        {
            nvidia_manager_util::getOemNvidiaOpenOCD(asyncResp);
        }

        // NvidiaManager
        nlohmann::json& oemNvidia = oem["Nvidia"];
        oemNvidia["@odata.type"] = "#NvidiaManager.v1_4_0.NvidiaManager";
        nlohmann::json& oemResetToDefaults =
            asyncResp->res
                .jsonValue["Actions"]["Oem"]["#NvidiaManager.ResetToDefaults"];
        oemResetToDefaults["target"] =
            "/redfish/v1/Managers/" +
            std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
            "/Actions/Oem/NvidiaManager.ResetToDefaults";

        // build type
        nlohmann::json& buildType = oemNvidia["FirmwareBuildType"];
        std::ifstream buildDescriptionFile(buildDescriptionFilePath);
        if (buildDescriptionFile.good())
        {
            std::string line;
            const std::string prefix = "BUILD_DESC=";
            std::string descriptionContent;
            while (getline(buildDescriptionFile, line) &&
                   descriptionContent.size() == 0)
            {
                if (line.rfind(prefix, 0) == 0)
                {
                    descriptionContent = line.substr(prefix.size());
                    descriptionContent.erase(
                        std::remove(descriptionContent.begin(),
                                    descriptionContent.end(), '"'),
                        descriptionContent.end());
                }
            }
            if (descriptionContent.rfind("debug-prov", 0) == 0)
            {
                buildType = "ProvisioningDebug";
            }
            else if (descriptionContent.rfind("prod-prov", 0) == 0)
            {
                buildType = "ProvisioningProduction";
            }
            else if (descriptionContent.rfind("dev-prov", 0) == 0)
            {
                buildType = "ProvisioningDevelopment";
            }
            else if (descriptionContent.rfind("debug-platform", 0) == 0)
            {
                buildType = "PlatformDebug";
            }
            else if (descriptionContent.rfind("prod-platform", 0) == 0)
            {
                buildType = "PlatformProduction";
            }
            else if (descriptionContent.rfind("dev-platform", 0) == 0)
            {
                buildType = "PlatformDevelopment";
            }
        }

        // OTP provisioning status
        nlohmann::json& otpProvisioned = oemNvidia["OTPProvisioned"];
        std::ifstream otpStatusFile(otpProvisioningStatusFilePath);
        if (otpStatusFile.good())
        {
            std::string statusLine;
            if (getline(otpStatusFile, statusLine))
            {
                if (statusLine != "0" && statusLine != "1")
                {
                    BMCWEB_LOG_ERROR("Invalid OTP provisioning status - {}",
                                     statusLine);
                }
                otpProvisioned = (statusLine == "1");
            }
            else
            {
                BMCWEB_LOG_ERROR("Failed to read OTP provisioning status");
                otpProvisioned = false;
            }
        }
        else
        {
            BMCWEB_LOG_ERROR("Failed to open OTP provisioning status file");
            otpProvisioned = false;
        }
        getFencingPrivilege(asyncResp);

#ifdef BMCWEB_ENABLE_TLS_AUTH_OPT_IN
        oemNvidia["AuthenticationTLSRequired"] =
            persistent_data::getConfig().isTLSAuthEnabled();
#endif

        populatePersistentStorageSettingStatus(asyncResp);
#endif // BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES

        // Manager.Reset (an action) can be many values, OpenBMC only
        // supports BMC reboot.
        nlohmann::json& managerReset =
            asyncResp->res.jsonValue["Actions"]["#Manager.Reset"];
        managerReset["target"] =
            boost::urls::format("/redfish/v1/Managers/{}/Actions/Manager.Reset",
                                BMCWEB_REDFISH_MANAGER_URI_NAME);
        managerReset["@Redfish.ActionInfo"] =
            boost::urls::format("/redfish/v1/Managers/{}/ResetActionInfo",
                                BMCWEB_REDFISH_MANAGER_URI_NAME);

        // ResetToDefaults (Factory Reset) has values like
        // PreserveNetworkAndUsers and PreserveNetwork that aren't supported
        // on OpenBMC
        nlohmann::json& resetToDefaults =
            asyncResp->res.jsonValue["Actions"]["#Manager.ResetToDefaults"];
        resetToDefaults["target"] = boost::urls::format(
            "/redfish/v1/Managers/{}/Actions/Manager.ResetToDefaults",
            BMCWEB_REDFISH_MANAGER_URI_NAME);

        resetToDefaults["ResetType@Redfish.AllowableValues"] =
            nlohmann::json::array_t({"ResetAll"});

#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES

        nlohmann::json& oemActions = asyncResp->res.jsonValue["Actions"]["Oem"];

#ifdef BMCWEB_COMMAND_SMBPBI_OOB
        nlohmann::json& oemActionsNvidia = oemActions["Nvidia"];

        oemActionsNvidia["#NvidiaManager.SyncOOBRawCommand"]["target"] =
            "/redfish/v1/Managers/" +
            std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
            "/Actions/Oem/NvidiaManager.SyncOOBRawCommand";
        oemActionsNvidia["#NvidiaManager.SyncOOBRawCommand"]
                        ["@Redfish.ActionInfo"] =
                            "/redfish/v1/Managers/" +
                            std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                            "/Oem/Nvidia/SyncOOBRawCommandActionInfo";

        oemActionsNvidia["#NvidiaManager.AsyncOOBRawCommand"]["target"] =
            "/redfish/v1/Managers/" +
            std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
            "/Actions/Oem/NvidiaManager.AsyncOOBRawCommand";
        oemActionsNvidia["#NvidiaManager.AsyncOOBRawCommand"]
                        ["@Redfish.ActionInfo"] =
                            "/redfish/v1/Managers/" +
                            std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                            "/Oem/Nvidia/AsyncOOBRawCommandActionInfo";
#endif // BMCWEB_COMMAND_SMBPBI_OOB

        oemActions["#eMMC.SecureErase"]["target"] =
            "/redfish/v1/Managers/" +
            std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
            "/Actions/Oem/eMMC.SecureErase";
        oemActions["#eMMC.SecureErase"]["@Redfish.ActionInfo"] =
            "/redfish/v1/Managers/" +
            std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
            "/Oem/EmmcSecureEraseActionInfo";
#endif // BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES

        std::pair<std::string, std::string> redfishDateTimeOffset =
            redfish::time_utils::getDateTimeOffsetNow();

        asyncResp->res.jsonValue["DateTime"] = redfishDateTimeOffset.first;
        asyncResp->res.jsonValue["DateTimeLocalOffset"] =
            redfishDateTimeOffset.second;

        // TODO (Gunnar): Remove these one day since moved to ComputerSystem
        // Still used by OCP profiles
        // https://github.com/opencomputeproject/OCP-Profiles/issues/23
        // Fill in SerialConsole info
        asyncResp->res.jsonValue["SerialConsole"]["ServiceEnabled"] = true;
        asyncResp->res.jsonValue["SerialConsole"]["MaxConcurrentSessions"] = 15;
        asyncResp->res.jsonValue["SerialConsole"]["ConnectTypesSupported"] = {
            "IPMI", "SSH"};
#ifdef BMCWEB_ENABLE_KVM
        // Fill in GraphicalConsole info
        asyncResp->res.jsonValue["GraphicalConsole"]["ServiceEnabled"] = true;
        asyncResp->res.jsonValue["GraphicalConsole"]["MaxConcurrentSessions"] =
            4;
        asyncResp->res.jsonValue["GraphicalConsole"]["ConnectTypesSupported"] =
            nlohmann::json::array_t({"KVMIP"});
#endif
        if constexpr (!BMCWEB_EXPERIMENTAL_REDFISH_MULTI_COMPUTER_SYSTEM)
        {
            asyncResp->res.jsonValue["Links"]["ManagerForServers@odata.count"] =
                1;

            nlohmann::json::array_t managerForServers;
            nlohmann::json::object_t manager;
            manager["@odata.id"] = std::format("/redfish/v1/Systems/{}",
                                               BMCWEB_REDFISH_SYSTEM_URI_NAME);
            managerForServers.emplace_back(std::move(manager));

            asyncResp->res.jsonValue["Links"]["ManagerForServers"] =
                std::move(managerForServers);
        }

        asyncResp->res.jsonValue["Links"]["ManagerForServers@odata.count"] = 1;

        nlohmann::json::array_t managerForServers;
        nlohmann::json::object_t manager;
        manager["@odata.id"] = "/redfish/v1/Systems/" +
                               std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME);
        managerForServers.push_back(std::move(manager));

        asyncResp->res.jsonValue["Links"]["ManagerForServers"] =
            std::move(managerForServers);

        auto health = std::make_shared<HealthPopulate>(asyncResp);
        health->isManagersHealth = true;
        health->populate();
#ifdef BMCWEB_ENABLE_IPMI_SOL
        asyncResp->res.jsonValue["CommandShell"]["MaxConcurrentSessions"] = 1;
        asyncResp->res.jsonValue["CommandShell"]["ConnectTypesSupported"] = {
            "SSH"};
        getIsCommandShellEnable(asyncResp);
#endif
        sw_util::populateSoftwareInformation(asyncResp, sw_util::bmcPurpose,
                                             "FirmwareVersion", true);

        managerGetLastResetTime(asyncResp);

        // ManagerDiagnosticData is added for all BMCs.
        nlohmann::json& managerDiagnosticData =
            asyncResp->res.jsonValue["ManagerDiagnosticData"];
        managerDiagnosticData["@odata.id"] =
            boost::urls::format("/redfish/v1/Managers/{}/ManagerDiagnosticData",
                                BMCWEB_REDFISH_MANAGER_URI_NAME);

        if constexpr (BMCWEB_REDFISH_OEM_MANAGER_FAN_DATA)
        {
            auto pids = std::make_shared<GetPIDValues>(asyncResp);
            pids->run();
        }

        getMainChassisId(asyncResp,
                         [](const std::string& chassisId,
                            const std::shared_ptr<bmcweb::AsyncResp>& aRsp) {
            aRsp->res.jsonValue["Links"]["ManagerForChassis@odata.count"] = 1;
            nlohmann::json::array_t managerForChassis;
            nlohmann::json::object_t managerObj;
            boost::urls::url chassiUrl =
                boost::urls::format("/redfish/v1/Chassis/{}", chassisId);
            managerObj["@odata.id"] = chassiUrl;
            managerForChassis.emplace_back(std::move(managerObj));
            aRsp->res.jsonValue["Links"]["ManagerForChassis"] =
                std::move(managerForChassis);
            aRsp->res.jsonValue["Links"]["ManagerInChassis"]["@odata.id"] =
                chassiUrl;
        });

        crow::connections::systemBus->async_method_call(
            [asyncResp, managerId](
                const boost::system::error_code ec,
                const std::vector<std::pair<
                    std::string, std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>>&
                    subtree) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Error while getting manager service state");
                messages::internalError(asyncResp->res);
                return;
            }
            if (!subtree.empty())
            {
                // Iterate over all retrieved ObjectPaths.
                for (const std::pair<
                         std::string,
                         std::vector<
                             std::pair<std::string, std::vector<std::string>>>>&
                         object : subtree)
                {
                    const std::string& path = object.first;
                    if (!boost::ends_with(path,
                                          BMCWEB_REDFISH_MANAGER_URI_NAME))
                    {
                        continue;
                    }

                    // At /redfish/v1/Managers/HGX_BMC_0, we want
                    // "ManagerInChassis" to point to "root" Chassis rather than
                    // another Chassis that is contained by the "root" Chassis.
                    // we want to identify the "root" Chassis of the HMC by
                    // making only two queries and without having to attempt to
                    // parse the Topology.
                    sdbusplus::asio::getProperty<std::vector<std::string>>(
                        *crow::connections::systemBus,
                        "xyz.openbmc_project.ObjectMapper", path + "/chassis",
                        "xyz.openbmc_project.Association", "endpoints",
                        [asyncResp](const boost::system::error_code ec,
                                    const std::vector<std::string>& property) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR("DBUS response error: {}", ec);
                            return; // no chassis = no failures
                        }

                        // single entry will be present
                        for (const std::string& p : property)
                        {
                            sdbusplus::message::object_path objPath(p);
                            const std::string& chassisId = objPath.filename();
                            asyncResp->res
                                .jsonValue["Links"]["ManagerForChassis"]
                                .clear();
                            nlohmann::json::array_t managerForChassis;
                            nlohmann::json::object_t managerObj;
                            boost::urls::url chassiUrl = boost::urls::format(
                                "/redfish/v1/Chassis/{}", chassisId);
                            managerObj["@odata.id"] = chassiUrl;
                            managerForChassis.emplace_back(
                                std::move(managerObj));
                            asyncResp->res
                                .jsonValue["Links"]["ManagerForChassis"] =
                                std::move(managerForChassis);
                            asyncResp->res
                                .jsonValue["Links"]["ManagerInChassis"]
                                          ["@odata.id"] =
                                "/redfish/v1/Chassis/" + chassisId;
                        }
                        asyncResp->res
                            .jsonValue["Links"]
                                      ["ManagerInChassis@odata.count"] = 1;
                    });

                    return;
                }
                BMCWEB_LOG_ERROR(
                    "Could not find interface xyz.openbmc_project.Inventory.Item.ManagementService");
            }
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTree",
            "/xyz/openbmc_project/inventory", int32_t(0),
            std::array<const char*, 1>{"xyz.openbmc_project.Inventory."
                                       "Item.ManagementService"});

        sdbusplus::asio::getProperty<double>(
            *crow::connections::systemBus, "org.freedesktop.systemd1",
            "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager",
            "Progress",
            [asyncResp, managerId](const boost::system::error_code& ec,
                                   double val) {
            if (ec)
            {
                BMCWEB_LOG_ERROR("Error while getting progress");
                messages::internalError(asyncResp->res);
                return;
            }
            if (val < 1.0)
            {
                asyncResp->res.jsonValue["Status"]["Health"] = "OK";
                asyncResp->res.jsonValue["Status"]["State"] = "Starting";
                return;
            }
            checkForQuiesced(asyncResp, managerId);
        });

        constexpr std::array<std::string_view, 1> interfaces = {
            "xyz.openbmc_project.Inventory.Item.Bmc"};
        dbus::utility::getSubTree(
            "/xyz/openbmc_project/inventory", 0, interfaces,
            [asyncResp](
                const boost::system::error_code& ec,
                const dbus::utility::MapperGetSubTreeResponse& subtree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("D-Bus response error on GetSubTree {}", ec);
                return;
            }
            if (subtree.empty())
            {
                BMCWEB_LOG_DEBUG("Can't find bmc D-Bus object!");
                return;
            }
            // Assume only 1 bmc D-Bus object
            // Throw an error if there is more than 1
            if (subtree.size() > 1)
            {
                BMCWEB_LOG_DEBUG("Found more than 1 bmc D-Bus object!");
                messages::internalError(asyncResp->res);
                return;
            }

            if (subtree[0].first.empty() || subtree[0].second.size() != 1)
            {
                BMCWEB_LOG_DEBUG("Error getting bmc D-Bus object!");
                messages::internalError(asyncResp->res);
                return;
            }

            const std::string& path = subtree[0].first;
            const std::string& connectionName = subtree[0].second[0].first;

            for (const auto& interfaceName : subtree[0].second[0].second)
            {
                if (interfaceName ==
                    "xyz.openbmc_project.Inventory.Decorator.Asset")
                {
                    sdbusplus::asio::getAllProperties(
                        *crow::connections::systemBus, connectionName, path,
                        "xyz.openbmc_project.Inventory.Decorator.Asset",
                        [asyncResp](const boost::system::error_code& ec2,
                                    const dbus::utility::DBusPropertiesMap&
                                        propertiesList) {
                        if (ec2)
                        {
                            BMCWEB_LOG_DEBUG("Can't get bmc asset!");
                            return;
                        }

                        const std::string* partNumber = nullptr;
                        const std::string* serialNumber = nullptr;
                        const std::string* manufacturer = nullptr;
                        const std::string* model = nullptr;
                        const std::string* sparePartNumber = nullptr;

                        const bool success = sdbusplus::unpackPropertiesNoThrow(
                            dbus_utils::UnpackErrorPrinter(), propertiesList,
                            "PartNumber", partNumber, "SerialNumber",
                            serialNumber, "Manufacturer", manufacturer, "Model",
                            model, "SparePartNumber", sparePartNumber);

                        if (!success)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }

                        if (partNumber != nullptr)
                        {
                            asyncResp->res.jsonValue["PartNumber"] =
                                *partNumber;
                        }

                        if (serialNumber != nullptr)
                        {
                            asyncResp->res.jsonValue["SerialNumber"] =
                                *serialNumber;
                        }

                        if (manufacturer != nullptr)
                        {
                            asyncResp->res.jsonValue["Manufacturer"] =
                                *manufacturer;
                        }

                        if (model != nullptr)
                        {
                            asyncResp->res.jsonValue["Model"] = *model;
                        }

                        if (sparePartNumber != nullptr)
                        {
                            asyncResp->res.jsonValue["SparePartNumber"] =
                                *sparePartNumber;
                        }
                    });
                }
                else if (interfaceName ==
                         "xyz.openbmc_project.Inventory.Decorator.LocationCode")
                {
                    getLocation(asyncResp, connectionName, path);
                }
            }
        });
        getServiceIdentification(asyncResp);
    });

    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/")
        .privileges(redfish::privileges::patchManager)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   const std::string& managerId) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }

        if (managerId != BMCWEB_REDFISH_MANAGER_URI_NAME)
        {
            messages::resourceNotFound(asyncResp->res, "Manager", managerId);
            return;
        }

        std::optional<std::string> activeSoftwareImageOdataId;
        std::optional<std::string> datetime;
        std::optional<std::string> serviceIdentification;
#ifdef BMCWEB_ENABLE_REDFISH_OEM_MANAGER_FAN_DATA
        std::optional<nlohmann::json::object_t> fanControllers;
        std::optional<nlohmann::json::object_t> fanZones;
        std::optional<nlohmann::json::object_t> pidControllers;
        std::optional<std::string> profile;
        std::optional<nlohmann::json::object_t> stepwiseControllers;
#endif // BMCWEB_ENABLE_REDFISH_OEM_MANAGER_FAN_DATA
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
#ifdef BMCWEB_ENABLE_FENCING_PRIVILEGE
        std::optional<std::string> privilege;
#endif // BMCWEB_ENABLE_FENCING_PRIVILEGE
#ifdef BMCWEB_ENABLE_TLS_AUTH_OPT_IN
        std::optional<bool> tlsAuth;
#endif  // BMCWEB_ENABLE_TLS_AUTH_OPT_IN
        std::optional<bool> openocdValue;
#endif  // BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES

        // clang-format off
        if (!json_util::readJsonPatch(req, asyncResp->res,
#ifdef BMCWEB_ENABLE_REDFISH_OEM_MANAGER_FAN_DATA
              "Oem/OpenBmc/Fan/FanControllers", fanControllers,
              "Oem/OpenBmc/Fan/FanZones", fanZones,
              "Oem/OpenBmc/Fan/PidControllers", pidControllers,
              "Oem/OpenBmc/Fan/Profile", profile,
              "Oem/OpenBmc/Fan/StepwiseControllers", stepwiseControllers,
#endif // BMCWEB_ENABLE_REDFISH_OEM_MANAGER_FAN_DATA
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
#ifdef BMCWEB_ENABLE_FENCING_PRIVILEGE
              "Oem/Nvidia/SMBPBIFencingPrivilege", privilege,
#endif // BMCWEB_ENABLE_FENCING_PRIVILEGE
#ifdef BMCWEB_ENABLE_TLS_AUTH_OPT_IN
              "Oem/Nvidia/AuthenticationTLSRequired", tlsAuth,
#endif // BMCWEB_ENABLE_TLS_AUTH_OPT_IN
              "Oem/Nvidia/OpenOCD/Enable", openocdValue,
#endif // BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
              "Links/ActiveSoftwareImage/@odata.id", activeSoftwareImageOdataId,
              "DateTime", datetime,
              "ServiceIdentification", serviceIdentification
        ))
        {
            return;
        }
        // clang-format on

#ifdef BMCWEB_ENABLE_REDFISH_OEM_MANAGER_FAN_DATA
        if (pidControllers || fanControllers || fanZones ||
            stepwiseControllers || profile)
        {
            if constexpr (BMCWEB_REDFISH_OEM_MANAGER_FAN_DATA)
            {
                std::vector<std::pair<std::string,
                                      std::optional<nlohmann::json::object_t>>>
                    configuration;
                if (pidControllers)
                {
                    configuration.emplace_back("PidControllers",
                                               std::move(pidControllers));
                }
                if (fanControllers)
                {
                    configuration.emplace_back("FanControllers",
                                               std::move(fanControllers));
                }
                if (fanZones)
                {
                    configuration.emplace_back("FanZones", std::move(fanZones));
                }
                if (stepwiseControllers)
                {
                    configuration.emplace_back("StepwiseControllers",
                                               std::move(stepwiseControllers));
                }
                auto pid = std::make_shared<SetPIDValues>(
                    asyncResp, std::move(configuration), profile);
                pid->run();
            }
            else
            {
                messages::propertyUnknown(asyncResp->res, "Oem");
                return;
            }
        }
#endif
        if (activeSoftwareImageOdataId)
        {
            setActiveFirmwareImage(asyncResp, *activeSoftwareImageOdataId);
        }

        if (datetime)
        {
            setDateTime(asyncResp, *datetime);
        }

        if (serviceIdentification)
        {
            setServiceIdentification(asyncResp,
                                     std::move(*serviceIdentification));
        }
#ifdef BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
#ifdef BMCWEB_ENABLE_FENCING_PRIVILEGE
        if (privilege)
        {
            crow::connections::systemBus->async_method_call(
                [asyncResp,
                 privilege](const boost::system::error_code ec,
                            const MapperGetSubTreeResponse& subtree) {
                if (ec)
                {
                    messages::internalError(asyncResp->res);
                    return;
                }
                for (const auto& [objectPath, serviceMap] : subtree)
                {
                    if (serviceMap.size() < 1)
                    {
                        BMCWEB_LOG_ERROR("Got 0 service "
                                         "names");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                    const std::string& serviceName = serviceMap[0].first;
                    // Patch SMBPBI Fencing Privilege
                    patchFencingPrivilege(asyncResp, *privilege, serviceName,
                                          objectPath);
                }
            },
                "xyz.openbmc_project.ObjectMapper",
                "/xyz/openbmc_project/object_mapper",
                "xyz.openbmc_project.ObjectMapper", "GetSubTree", "/",
                int32_t(0),
                std::array<const char*, 1>{
                    "xyz.openbmc_project.GpuOobRecovery.Server"});
        }
#endif // BMCWEB_ENABLE_FENCING_PRIVILEGE
#ifdef BMCWEB_ENABLE_TLS_AUTH_OPT_IN
        if (tlsAuth)
        {
            if (*tlsAuth == persistent_data::getConfig().isTLSAuthEnabled())
            {
                BMCWEB_LOG_DEBUG(
                    "Ignoring redundant patch of AuthenticationTLSRequired.");
            }
            else if (!*tlsAuth)
            {
                BMCWEB_LOG_ERROR(
                    "Disabling AuthenticationTLSRequired is not allowed.");
                messages::propertyValueIncorrect(
                    asyncResp->res, "AuthenticationTLSRequired", "false");
                return;
            }
            else
            {
                enableTLSAuth();
            }
        }
#endif // BMCWEB_ENABLE_TLS_AUTH_OPT_IN
        if constexpr (BMCWEB_ENABLE_DEBUG_INTERFACE)
        {
            if (openocdValue)
            {
                nvidia_manager_util::setOemNvidiaOpenOCD(*openocdValue);
            }
        }
#endif // BMCWEB_ENABLE_NVIDIA_OEM_PROPERTIES
    });
}

inline void requestRoutesManagerCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/")
        .privileges(redfish::privileges::getManagerCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
        if (!redfish::setUpRedfishRoute(app, req, asyncResp))
        {
            return;
        }
        const std::array<const char*, 1> interface = {
            "xyz.openbmc_project.Inventory.Item.ManagementService"};
        const std::string collectionPath = "/redfish/v1/Managers";
        // Collections don't include the static data added by SubRoute
        // because it has a duplicate entry for members
        asyncResp->res.jsonValue["@odata.id"] = collectionPath;
        asyncResp->res.jsonValue["@odata.type"] =
            "#ManagerCollection.ManagerCollection";
        asyncResp->res.jsonValue["Name"] = "Manager Collection";
        nlohmann::json& members = asyncResp->res.jsonValue["Members"];
        members = nlohmann::json::array();
        // Add bmc path
        members.push_back(
            {{"@odata.id", "/redfish/v1/Managers/" +
                               std::string(BMCWEB_REDFISH_MANAGER_URI_NAME)}});
        // Collect additional management services
        crow::connections::systemBus->async_method_call(
            [collectionPath,
             asyncResp{asyncResp}](const boost::system::error_code ec,
                                   const std::vector<std::string>& objects) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("DBUS response error");
                // messages::internalError(asyncResp->res);
                return;
            }
            nlohmann::json& members = asyncResp->res.jsonValue["Members"];
            for (const auto& object : objects)
            {
                sdbusplus::message::object_path path(object);
                std::string leaf = path.filename();
                if (leaf.empty() || leaf == BMCWEB_REDFISH_MANAGER_URI_NAME)
                {
                    continue;
                }
                std::string newPath = collectionPath;
                newPath += '/';
                newPath += leaf;
                members.push_back({{"@odata.id", std::move(newPath)}});
            }
            asyncResp->res.jsonValue["Members@odata.count"] = members.size();
        },
            "xyz.openbmc_project.ObjectMapper",
            "/xyz/openbmc_project/object_mapper",
            "xyz.openbmc_project.ObjectMapper", "GetSubTreePaths",
            "/xyz/openbmc_project/inventory", 0, interface);
    });
}
} // namespace redfish
