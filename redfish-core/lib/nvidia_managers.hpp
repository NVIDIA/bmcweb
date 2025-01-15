/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION &
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
#include "event_service_manager.hpp"
#include "generated/enums/action_info.hpp"
#include "generated/enums/manager.hpp"
#include "generated/enums/resource.hpp"
#include "health.hpp"
#include "nvidia_event_service_manager.hpp"
#include "persistentstorage_util.hpp"
#include "query.hpp"
#include "redfish_util.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/conditions_utils.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/json_utils.hpp"
#include "utils/nvidia_manager_utils.hpp"
#include "utils/sw_utils.hpp"
#include "utils/systemd_utils.hpp"
#include "utils/time_utils.hpp"

#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/asio/property.hpp>
#include <sdbusplus/unpack_properties.hpp>

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
        out << "ListenStream=" << std::endl; // disable port 80
        out << "ListenStream=443" << std::endl; // enable port 443
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("TLSAuthEnable drop-in socket configuration file: {}",
                         e.what());
    }
    persistent_data::AuthConfigMethods& authMethodsConfig =
        persistent_data::SessionStore::getInstance().getAuthMethodsConfig();
    authMethodsConfig.basic = true;
    authMethodsConfig.cookie = true;
    authMethodsConfig.xtoken = true;
    authMethodsConfig.sessionToken = true;
    authMethodsConfig.tls = true;
    persistent_data::nvidia::getConfig().enableTLSAuth();

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

inline void executeRawSynCommand(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& serviceName, const std::string& objPath,
    const std::string& Type, uint8_t id, uint8_t opCode, uint8_t arg1,
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
                resp->res.jsonValue["ExtDataOut"] =
                    intToHexByteArray(get<3>(res));
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
                BMCWEB_LOG_ERROR(
                    "form executeRawSynCommand failed with error ");
                BMCWEB_LOG_ERROR("{}", ec);
                messages::internalError(resp->res);
            }
            return;
        },
        serviceName, objPath, "com.nvidia.Protocol.SMBPBI.Raw", "SyncCommand",
        Type, id, opCode, arg1, arg2, dataIn, extDataIn);
}

// function to convert dataInbyte array to dataIn uint32 vector
inline std::vector<std::uint32_t>
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

inline void executeRawAsynCommand(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& serviceName, const std::string& objPath,
    const std::string& Type, uint8_t id, uint8_t argRaw,
    const std::vector<uint32_t>& asyncDataInRaw, uint32_t requestedDataOutBytes)
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
                                             dataOutHex.begin(),
                                             dataOutHex.end());
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
                BMCWEB_LOG_ERROR(
                    "form executeRawAsynCommand failed with error ");
                BMCWEB_LOG_ERROR("{}", ec);
                messages::internalError(resp->res);
            }
            return;
        },
        serviceName, objPath, "com.nvidia.Protocol.SMBPBI.Raw", "AsyncCommand",
        Type, id, argRaw, asyncDataInRaw, requestedDataOutBytes);
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

inline void setDbusSelCapacity(
    size_t capacity, const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
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
            for (const std::pair<std::string, std::variant<std::string>>&
                     property : propertiesList)
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
            for (const std::pair<std::string, std::variant<std::string>>&
                     property : propertiesList)
            {
                const std::string& propertyName = property.first;

                if ((propertyName == "PartNumber") ||
                    (propertyName == "SerialNumber") ||
                    (propertyName == "Manufacturer") ||
                    (propertyName == "Model") ||
                    (propertyName == "SparePartNumber") ||
                    (propertyName == "Name"))
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
                BMCWEB_LOG_DEBUG(
                    "DBUS response error for ServiceIdentification {}", ec);
                return;
            }
            asyncResp->res.jsonValue["ServiceIdentification"] = sysId;
        });
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
                            asyncResp->res
                                .jsonValue["Links"]["ManagerForSwitches"];
                        for (const std::pair<
                                 std::string,
                                 std::vector<std::pair<
                                     std::string, std::vector<std::string>>>>&
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
                    std::array<const char*, 2>{
                        "xyz.openbmc_project.Inventory.Item.NvSwitch",
                        "xyz.openbmc_project.Inventory.Item.Switch"});
            }
        },
        "xyz.openbmc_project.ObjectMapper", objPath + "/fabric",
        "org.freedesktop.DBus.Properties", "Get",
        "xyz.openbmc_project.Association", "endpoints");
}

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

                        for (const std::pair<
                                 std::string,
                                 std::variant<std::string, uint8_t>>& property :
                             propertiesList)
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
                                json["Oem"]["Nvidia"]
                                    ["SMBPBIFencingPrivilege"] = redfish::
                                        dbus_utils::toSMPBIPrivilegeString(
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

inline void patchFencingPrivilege(
    const std::shared_ptr<bmcweb::AsyncResp>& resp,
    const std::string& privilegeType, const std::string& serviceName,
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
        [resp, privilegeType](boost::system::error_code ec,
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

inline void getFabricManagerInfo(
    const std::shared_ptr<bmcweb::AsyncResp>& aResp,
    const std::string& connectionName, const std::string& path,
    const std::string& interfaceName, const std::string& managerId)
{
    if (interfaceName == "com.nvidia.State.FabricManager")
    {
        aResp->res.jsonValue["Name"] = managerId;
        nvidia_manager_util::getFabricManagerInformation(aResp, connectionName,
                                                         path);
    }
}

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
                BMCWEB_LOG_DEBUG(
                    "DBUS response error for getIsCommandShellEnable");
                messages::internalError(asyncResp->res);
                return;
            }
            asyncResp->res.jsonValue["CommandShell"]["ServiceEnabled"] =
                isEnable;
        });
}

inline void sendRestartEvent(const crow::Request& req, std::string& resetType)
{
    if (resetType == "GracefulRestart" || resetType == "ForceRestart" ||
        resetType == "GracefulShutdown")
    {
        if constexpr (BMCWEB_REDFISH_DBUS_EVENT)
        {
            // Send an event for Manager Reset
            DsEvent event =
                redfish::EventUtil::getInstance().createEventRebootReason(
                    "ManagerReset", "Managers");
            redfish::EventServiceManager::getInstance().sendEventWithOOC(
                std::string(req.target()), event);
        }
    }
}

inline void sendFactoryResetEvent(const crow::Request& req)
{
    if constexpr (BMCWEB_REDFISH_DBUS_EVENT)
    {
        // Send an event for reset to defaults
        DsEvent event =
            redfish::EventUtil::getInstance().createEventRebootReason(
                "FactoryReset", "Managers");
        redfish::EventServiceManager::getInstance().sendEventWithOOC(
            std::string(req.target()), event);
    }
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
                asyncResp->res.jsonValue["Name"] =
                    "Emmc Secure Erase Action Info";
                asyncResp->res.jsonValue["Id"] = "EmmcSecureEraseActionInfo";
            });
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

                if (!redfish::json_util::readJsonAction(
                        req, asyncResp->res, "ErrorInfoCap", capacity))
                {
                    BMCWEB_LOG_ERROR("Unable to set ErrorInfoCap");
                    return;
                }

                setDbusSelCapacity(capacity, asyncResp);
            });
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
                    messages::resourceNotFound(
                        asyncResp->res, "#Manager.v1_11_0.Manager", bmcId);
                    return;
                }
                asyncResp->res.jsonValue = {
                    {"@odata.type", "#ActionInfo.v1_1_2.ActionInfo"},
                    {"@odata.id",
                     "/redfish/v1/Managers/" + bmcId +
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
                      {{"Name", "Opcode"},
                       {"Required", true},
                       {"DataType", "String"}},
                      {{"Name", "Arg1"},
                       {"Required", false},
                       {"DataType", "String"}},
                      {{"Name", "Arg2"},
                       {"Required", false},
                       {"DataType", "String"}},
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
                   const std::string& bmcId) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                // Process non bmc service manager
                if (bmcId != BMCWEB_REDFISH_MANAGER_URI_NAME)
                {
                    messages::resourceNotFound(
                        asyncResp->res, "#Manager.v1_11_0.Manager", bmcId);
                    return;
                }

                asyncResp->res.jsonValue = {
                    {"@odata.type", "#ActionInfo.v1_1_2.ActionInfo"},
                    {"@odata.id",
                     "/redfish/v1/Managers/" + bmcId +
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

                uint8_t targetId = 0;
                std::string targetType;
                std::string opCode;
                std::string arg1;
                std::string arg2;
                uint8_t opCodeRaw = 0;
                uint8_t arg1Raw = 0;
                uint8_t arg2Raw = 0;
                uint32_t dataInRaw = 0;
                uint32_t extDataInRaw = 0;
                std::optional<std::vector<std::string>> dataIn;
                std::optional<std::vector<std::string>> extDataIn;

                if (!redfish::json_util::readJsonAction(
                        req, asyncResp->res, "TargetType", targetType,
                        "TargetInstanceId", targetId, "Opcode", opCode, "Arg1",
                        arg1, "Arg2", arg2, "DataIn", dataIn, "ExtDataIn",
                        extDataIn))
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
                    catch (const std::runtime_error& /*e*/)
                    {
                        BMCWEB_LOG_ERROR(
                            "formatSyncDataIn failed with runtime error ");
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
                    catch (const std::runtime_error& /*e*/)
                    {
                        BMCWEB_LOG_ERROR(
                            "formatSyncDataIn failed with runtime error ");
                        messages::internalError(asyncResp->res);
                        return;
                    }
                }

                try
                {
                    opCodeRaw =
                        static_cast<uint8_t>(std::stoul(opCode, nullptr, 16));
                    arg1Raw =
                        static_cast<uint8_t>(std::stoul(arg1, nullptr, 16));
                    arg2Raw =
                        static_cast<uint8_t>(std::stoul(arg2, nullptr, 16));
                }
                catch (...)
                {
                    BMCWEB_LOG_ERROR(
                        "raw Sync command failed : stoul exception ");
                    messages::internalError(asyncResp->res);
                    return;
                }

                crow::connections::systemBus->async_method_call(
                    [asyncResp, targetType, targetId, opCodeRaw, arg1Raw,
                     arg2Raw, dataInRaw,
                     extDataInRaw](const boost::system::error_code ec,
                                   const MapperGetSubTreeResponse& subtree) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR(
                                "unable to find SMBPBI raw interface");
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
                            const std::string& serviceName =
                                serviceMap[0].first;
                            executeRawSynCommand(
                                asyncResp, serviceName, objectPath, targetType,
                                targetId, opCodeRaw, arg1Raw, arg2Raw,
                                dataInRaw, extDataInRaw);
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                    "/xyz/openbmc_project/inventory", int32_t(0),
                    std::array<const char*, 1>{
                        "com.nvidia.Protocol.SMBPBI.Raw"});
            });
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

                uint8_t targetId = 0;
                uint8_t argRaw = 0;
                uint32_t requestedDataOutBytes = 0;
                std::string targetType;
                std::string arg;
                std::optional<std::vector<std::string>> asynDataIn;
                std::vector<uint32_t> asyncDataInRaw;

                if (!redfish::json_util::readJsonAction(
                        req, asyncResp->res, "TargetType", targetType,
                        "TargetInstanceId", targetId, "AsyncArg1", arg,
                        "RequestedDataOutBytes", requestedDataOutBytes,
                        "AsyncDataIn", asynDataIn))
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
                    catch (const std::runtime_error& /*e*/)
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
                    BMCWEB_LOG_ERROR(
                        "raw Async command failed : stoul exception ");
                    messages::internalError(asyncResp->res);
                    return;
                }

                crow::connections::systemBus->async_method_call(
                    [asyncResp, targetType, targetId, argRaw, asyncDataInRaw,
                     requestedDataOutBytes](
                        const boost::system::error_code ec,
                        const MapperGetSubTreeResponse& subtree) {
                        if (ec)
                        {
                            BMCWEB_LOG_ERROR(
                                "unable to find SMBPBI raw interface");
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
                            const std::string& serviceName =
                                serviceMap[0].first;
                            executeRawAsynCommand(
                                asyncResp, serviceName, objectPath, targetType,
                                targetId, argRaw, asyncDataInRaw,
                                requestedDataOutBytes);
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTree",
                    "/xyz/openbmc_project/inventory", int32_t(0),
                    std::array<const char*, 1>{
                        "com.nvidia.Protocol.SMBPBI.Raw"});
            });
}

inline void
    handleGenericManager(const crow::Request& /*req*/,
                         const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                         const std::string& managerId)
{
    crow::connections::systemBus->async_method_call(
        [asyncResp, managerId](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
                subtree) {
            if (ec)
            {
                BMCWEB_LOG_DEBUG("D-Bus response error on GetSubTree {}", ec);
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
                    "#Manager.v1_13_0.Manager";
                asyncResp->res.jsonValue["Id"] = managerId;
                asyncResp->res.jsonValue["Name"] = "OpenBmc Manager Service";
                asyncResp->res.jsonValue["Description"] =
                    "Software Service for Baseboard Management "
                    "Functions";
                asyncResp->res.jsonValue["ManagerType"] = "Service";
                const std::string& connectionName = connectionNames[0].first;
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
                    getFabricManagerInfo(asyncResp, connectionName, path,
                                         interfaceName, managerId);
                }
                getLinkManagerForSwitches(asyncResp, path);

                if constexpr (!BMCWEB_DISABLE_CONDITIONS_ARRAY)
                {
                    redfish::conditions_utils::populateServiceConditions(
                        asyncResp, managerId);
                }

                if constexpr (BMCWEB_HEALTH_ROLLUP_ALTERNATIVE)
                {
                    auto health = std::make_shared<HealthRollup>(
                        path, [asyncResp](const std::string& rootHealth,
                                          const std::string& healthRollup) {
                            asyncResp->res.jsonValue["Status"]["Health"] =
                                rootHealth;
                            if constexpr (!BMCWEB_DISABLE_HEALTH_ROLLUP)
                            {
                                asyncResp->res
                                    .jsonValue["Status"]["HealthRollup"] =
                                    healthRollup;
                            }
                        });
                    health->start();
                }
                return;
            }
            messages::resourceNotFound(asyncResp->res,
                                       "#Manager.v1_11_0.Manager", managerId);
        },
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetSubTree",
        "/xyz/openbmc_project/inventory", int32_t(0),
        std::array<const char*, 1>{"xyz.openbmc_project.Inventory."
                                   "Item.ManagementService"});
    return;
}

inline void
    extendManagerPatch(const crow::Request& req,
                       const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& /*managerId*/)
{
    std::optional<std::string> serviceIdentification;

    if (!json_util::readJsonPatch(req, asyncResp->res, "ServiceIdentification",
                                  serviceIdentification))
    {
        return;
    }
    if (serviceIdentification)
    {
        setServiceIdentification(asyncResp, std::move(*serviceIdentification));
    }
}

inline void
    extendManagerPatchOEM(const crow::Request& req,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& /*managerId*/)
{
    std::optional<std::string> serviceIdentification;
    std::optional<std::string> privilege;
    std::optional<bool> tlsAuth;

    std::optional<bool> openocdValue;

    if (!json_util::readJsonPatch(
            req, asyncResp->res, "Oem/Nvidia/SMBPBIFencingPrivilege", privilege,
            "Oem/Nvidia/AuthenticationTLSRequired", tlsAuth,
            "Oem/Nvidia/OpenOCD/Enable", openocdValue))
    {
        return;
    }
    if constexpr (BMCWEB_NVIDIA_OEM_PROPERTIES)
    {
        if constexpr (BMCWEB_FENCING_PRIVILEGE)
        {
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
                            const std::string& serviceName =
                                serviceMap[0].first;
                            // Patch SMBPBI Fencing Privilege
                            patchFencingPrivilege(asyncResp, *privilege,
                                                  serviceName, objectPath);
                        }
                    },
                    "xyz.openbmc_project.ObjectMapper",
                    "/xyz/openbmc_project/object_mapper",
                    "xyz.openbmc_project.ObjectMapper", "GetSubTree", "/",
                    int32_t(0),
                    std::array<const char*, 1>{
                        "xyz.openbmc_project.GpuOobRecovery.Server"});
            }
        }
        if constexpr (BMCWEB_TLS_AUTH_OPT_IN)
        {
            if (tlsAuth)
            {
                if (*tlsAuth ==
                    persistent_data::nvidia::getConfig().isTLSAuthEnabled())
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
        }
        if constexpr (BMCWEB_NVIDIA_OEM_OPENOCD)
        {
            if (openocdValue)
            {
                nvidia_manager_util::setOemNvidiaOpenOCD(*openocdValue);
            }
        }
    }
}

inline void
    extendManagerGet(const crow::Request& /*req*/,
                     const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& managerId)
{
    // Default Health State.
    asyncResp->res.jsonValue["Status"]["State"] = "Enabled";
    asyncResp->res.jsonValue["Status"]["Health"] = "OK";
    asyncResp->res.jsonValue["Status"]["HealthRollup"] = "OK";

    if constexpr (BMCWEB_LLDP_DEDICATED_PORTS)
    {
        asyncResp->res.jsonValue["DedicatedNetworkPorts"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Managers/{}/DedicatedNetworkPorts",
                                BMCWEB_REDFISH_MANAGER_URI_NAME);
    }
    if constexpr (!BMCWEB_DISABLE_CONDITIONS_ARRAY)
    {
        redfish::conditions_utils::populateServiceConditions(
            asyncResp, std::string(BMCWEB_REDFISH_MANAGER_URI_NAME));
    }

    if constexpr (BMCWEB_HOST_IFACE)
    {
        asyncResp->res.jsonValue["HostInterfaces"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Managers/{}/HostInterfaces",
                                BMCWEB_REDFISH_MANAGER_URI_NAME);
    }

    if constexpr (BMCWEB_RMEDIA)
    {
        asyncResp->res.jsonValue["VirtualMedia"]["@odata.id"] =
            boost::urls::format("/redfish/v1/Managers/{}/VirtualMedia",
                                BMCWEB_REDFISH_MANAGER_URI_NAME);
    }

    asyncResp->res.jsonValue["Links"]["ManagerForServers@odata.count"] = 1;

    nlohmann::json::array_t managerForServers;
    nlohmann::json::object_t manager;
    manager["@odata.id"] =
        "/redfish/v1/Systems/" + std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME);
    managerForServers.push_back(std::move(manager));

    asyncResp->res.jsonValue["Links"]["ManagerForServers"] =
        std::move(managerForServers);

    auto health = std::make_shared<HealthPopulate>(asyncResp);
    health->isManagersHealth = true;
    health->populate();
    if constexpr (BMCWEB_COMMAND_SHELL)
    {
        asyncResp->res.jsonValue["CommandShell"]["MaxConcurrentSessions"] = 1;
        asyncResp->res.jsonValue["CommandShell"]["ConnectTypesSupported"] = {
            "SSH"};
        getIsCommandShellEnable(asyncResp);
    }
    // Get Managers Chassis ID
    crow::connections::systemBus->async_method_call(
        [asyncResp, managerId](
            const boost::system::error_code ec,
            const std::vector<std::pair<
                std::string,
                std::vector<std::pair<std::string, std::vector<std::string>>>>>&
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
                    // "ManagerInChassis" to point to "root" Chassis
                    // rather than another Chassis that is contained by
                    // the "root" Chassis. we want to identify the
                    // "root" Chassis of the HMC by making only two
                    // queries and without having to attempt to parse
                    // the Topology.
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
                                const std::string& chassisId =
                                    objPath.filename();
                                asyncResp->res
                                    .jsonValue["Links"]["ManagerForChassis"]
                                    .clear();
                                nlohmann::json::array_t managerForChassis;
                                nlohmann::json::object_t managerObj;
                                boost::urls::url chassiUrl =
                                    boost::urls::format(
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

    getServiceIdentification(asyncResp);
}

inline void
    extendManagerOEM(const crow::Request& /*req*/,
                     const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& /*managerId*/)
{
    // default oem data
    nlohmann::json& oem = asyncResp->res.jsonValue["Oem"];
    if constexpr (BMCWEB_HOST_OS_FEATURES)
    {
        nlohmann::json& oemOpenbmc = oem["OpenBmc"];

        oemOpenbmc["@odata.type"] = "#OpenBMCManager.OpenBmc";
        oemOpenbmc["@odata.id"] =
            "/redfish/v1/Managers/" +
            std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) + "#/Oem/OpenBmc";

        oemOpenbmc["Certificates"] = {
            {"@odata.id", "/redfish/v1/Managers/" +
                              std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                              "/Truststore/Certificates"}};

        if constexpr (BMCWEB_NVIDIA_OEM_COMMON_PROPERTIES)
        {
            oem["Nvidia"]["@odata.id"] =
                "/redfish/v1/Managers/" +
                std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) + "/Oem/Nvidia";
        }
    }

    if constexpr (BMCWEB_NVIDIA_OEM_GB200NVL_PROPERTIES)
    {
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
    }

    if constexpr (BMCWEB_NVIDIA_OEM_OPENOCD)
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
        "/redfish/v1/Managers/" + std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
        "/Actions/Oem/NvidiaManager.ResetToDefaults";

    // build type
    nlohmann::json& buildType = oemNvidia["FirmwareBuildType"];
    std::ifstream buildDescriptionFile(
        BMCWEB_BUILD_DESCRIPTION_FILE_PATH.data());
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
    std::ifstream otpStatusFile(
        BMCWEB_OTP_PROVISIONING_STATUS_FILE_PATH.data());
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

    if constexpr (BMCWEB_TLS_AUTH_OPT_IN)
    {
        oemNvidia["AuthenticationTLSRequired"] =
            persistent_data::nvidia::getConfig().isTLSAuthEnabled();
    }

    populatePersistentStorageSettingStatus(asyncResp);

    nlohmann::json& oemActions = asyncResp->res.jsonValue["Actions"]["Oem"];

    if constexpr (BMCWEB_COMMAND_SMBPBI_OOB)
    {
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
    }

    oemActions["#eMMC.SecureErase"]["target"] =
        "/redfish/v1/Managers/" + std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
        "/Actions/Oem/eMMC.SecureErase";
    oemActions["#eMMC.SecureErase"]["@Redfish.ActionInfo"] =
        "/redfish/v1/Managers/" + std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
        "/Oem/EmmcSecureEraseActionInfo";

    if constexpr (BMCWEB_NSM_RAW_COMMAND_ENABLE)
    {
        nvidia_manager_util::getNSMRawCommandActions(asyncResp);
    }

    if constexpr (BMCWEB_REDFISH_SW_EINJ)
    {
        nlohmann::json& oemActionsEinj =
            oemActions["#NvidiaManager.SWErrorInjection"];

        oemActionsEinj["target"] = boost::urls::format(
            "/redfish/v1/Managers/{}/Actions/Oem/NvidiaManager.SWErrorInjection",
            BMCWEB_REDFISH_MANAGER_URI_NAME);

        oemActionsEinj["@Redfish.ActionInfo"] = boost::urls::format(
            "/redfish/v1/Managers/{}/Oem/Nvidia/SWErrorInjectionActionInfo",
            BMCWEB_REDFISH_MANAGER_URI_NAME);
    }
}

} // namespace redfish
