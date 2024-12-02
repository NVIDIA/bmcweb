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
#include "ethernet.hpp"
#include "lldptool_util.hpp"

#include <app.hpp>

namespace redfish
{
enum class ChassisIdSubtype
{
    Reserved = 0,
    ChassisComponent = 1,
    InterfaceAlias = 2,
    PortComponent = 3,
    MACAddress = 4,
    NetworkAddress = 5,
    InterfaceName = 6,
    LocallyAssigned = 7
};

enum class PortIdSubtype
{
    Reserved = 0,
    InterfaceAlias = 1,
    PortComponent = 2,
    MACAddress = 3,
    NetworkAddress = 4,
    InterfaceName = 5,
    AgentCircuitID = 6,
    LocallyAssigned = 7
};

const std::string lldpTransmit = "LLDPTransmit";
const std::string lldpReceive = "LLDPReceive";

inline void getLldpStatus(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& ifaceId)
{
    LldpToolUtil::run(
        ifaceId, LldpTlv::ADMIN_STATUS, LldpCommandType::GET_LLDP, false,
        asyncResp,
        [ifaceId](const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                  const std::string& stdOut, const std::string&,
                  const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                messages::resourceErrorsDetectedFormatError(
                    asyncResp->res,
                    "/redfish/v1/Managers/" +
                        std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                        "/DedicatedNetworkPorts/" + ifaceId,
                    " command failure");
                BMCWEB_LOG_ERROR("Error while running lldtool get status");
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "Error while running lldtool get status, Message: {}",
                        ec.message());
                }
                return;
            }
            if (stdOut.find("adminStatus=") != std::string::npos)
            {
                if (stdOut.find("disabled") != std::string::npos)
                {
                    asyncResp->res.jsonValue["Ethernet"]["LLDPEnabled"] = false;
                }
                else
                {
                    asyncResp->res.jsonValue["Ethernet"]["LLDPEnabled"] = true;
                }
            }
            BMCWEB_LOG_DEBUG("get Lldp Status: {}", stdOut);
        });
}

inline void setLldpStatus(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& ifaceId, LldpTlv commandType)
{
    LldpToolUtil::run(
        ifaceId, commandType, LldpCommandType::SET_LLDP, false, asyncResp,
        [ifaceId](const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                  const std::string&, const std::string&,
                  const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                messages::resourceErrorsDetectedFormatError(
                    asyncResp->res,
                    "/redfish/v1/Managers/" +
                        std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                        "/DedicatedNetworkPorts/" + ifaceId,
                    " command failure");
                BMCWEB_LOG_ERROR("Error while running lldtool set status");
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "Error while running lldtool set status, Message: {}",
                        ec.message());
                }
                return;
            }
        });
}

inline ChassisIdSubtype getChassisSubType(const std::string& chassisId)
{
    if (chassisId.find("MAC") != std::string::npos)
    {
        return ChassisIdSubtype::MACAddress;
    }
    if (chassisId.find("IP") != std::string::npos)
    {
        return ChassisIdSubtype::NetworkAddress;
    }
    if (chassisId.find("Ifname") != std::string::npos)
    {
        return ChassisIdSubtype::InterfaceName;
    }

    BMCWEB_LOG_ERROR("Cannot find chassis subtype for chassis id: {}",
                     chassisId);
    return ChassisIdSubtype::Reserved;
}

inline PortIdSubtype getPortSubType(const std::string& portId)
{
    if (portId.find("MAC") != std::string::npos)
    {
        return PortIdSubtype::MACAddress;
    }
    if (portId.find("IP") != std::string::npos)
    {
        return PortIdSubtype::NetworkAddress;
    }
    if (portId.find("Ifname") != std::string::npos)
    {
        return PortIdSubtype::InterfaceName;
    }

    BMCWEB_LOG_ERROR("Cannot find port subtype for port id: {}", portId);
    return PortIdSubtype::Reserved;
}

inline std::vector<std::string> parseLldpCapabilities(std::string& systemCap)
{
    std::vector<std::string> capabilities{};
    std::string sysCapStr = "System capabilities:";
    size_t startPos = systemCap.find(sysCapStr);
    std::string systemCapStr;
    size_t nextPos = 0;

    if (startPos == std::string::npos)
    {
        return capabilities;
    }
    size_t sysCapStrSize = sysCapStr.size();
    nextPos = systemCap.find('\n', sysCapStrSize + startPos);
    systemCapStr = systemCap.substr(startPos + sysCapStrSize,
                                    nextPos - startPos - sysCapStrSize);
    std::string token;
    std::istringstream systemCapStream(systemCapStr);
    while (std::getline(systemCapStream, token, ','))
    {
        size_t pos = token.find("Only");
        if (pos != std::string::npos)
        {
            token = token.substr(0, pos);
        }
        // Remove space from start and end of token
        token.erase(std::remove_if(token.begin(), token.end(), ::isspace),
                    token.end());
        size_t spacePos = token.find(' ');
        if (spacePos != std::string::npos)
        {
            token = token.substr(0, spacePos);
        }
        if (token == "None")
        {
            return {};
        }
        capabilities.push_back(token);
    }
    return capabilities;
}

// Enable on BMC SYSTEM_CAPABILITIES, SYSTEM_DESCRIPTION, SYSTEM_NAME,
inline void
    getEnableLldpTlvs(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                      const std::string& ifaceId)
{
    LldpToolUtil::run(
        ifaceId, LldpTlv::SYSTEM_CAPABILITIES, LldpCommandType::ENABLE_TLV,
        false, asyncResp,
        [ifaceId](const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                  const std::string& stdOut, const std::string&,
                  const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                messages::resourceErrorsDetectedFormatError(
                    asyncResp->res,
                    "/redfish/v1/Managers/" +
                        std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                        "/DedicatedNetworkPorts/" + ifaceId,
                    " command failure");
                BMCWEB_LOG_ERROR("Error while running lldtool enable TLV");
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "Error while running lldtool enable TLV, Message: {}",
                        ec.message());
                }
                return;
            }
            BMCWEB_LOG_DEBUG("getEnableLldpTlvs capability enable response: {}",
                             stdOut);
        });

    LldpToolUtil::run(
        ifaceId, LldpTlv::SYSTEM_DESCRIPTION, LldpCommandType::ENABLE_TLV,
        false, asyncResp,
        [ifaceId](const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                  const std::string& stdOut, const std::string&,
                  const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                messages::resourceErrorsDetectedFormatError(
                    asyncResp->res,
                    "/redfish/v1/Managers/" +
                        std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                        "/DedicatedNetworkPorts/" + ifaceId,
                    " command failure");
                BMCWEB_LOG_ERROR("Error while running lldtool get TLV");
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "Error while running lldtool get TLV, Message: {}",
                        ec.message());
                }
                return;
            }
            BMCWEB_LOG_DEBUG("getEnableLldpTlv  enable response: {}", stdOut);
        });

    LldpToolUtil::run(
        ifaceId, LldpTlv::SYSTEM_NAME, LldpCommandType::ENABLE_TLV, false,
        asyncResp,
        [ifaceId](const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                  const std::string& stdOut, const std::string&,
                  const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                messages::resourceErrorsDetectedFormatError(
                    asyncResp->res,
                    "/redfish/v1/Managers/" +
                        std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                        "/DedicatedNetworkPorts/" + ifaceId,
                    " command failure");
                BMCWEB_LOG_ERROR("Error while running lldtool enable TLV");
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "Error while running lldtool enable TLV, message: {}",
                        ec.message());
                }
                return;
            }
            BMCWEB_LOG_DEBUG("lldptool capability enable response: {}", stdOut);
        });
}

// Returns the string that is between the tlvName and next line that contains
// "TLV"
inline std::string getTlvString(const std::string& commandOutput,
                                const std::string& tlvName)
{
    std::string result{};
    size_t pos = commandOutput.find(tlvName);
    if (pos != std::string::npos)
    {
        size_t startPosNextLine = commandOutput.find('\n', pos);
        size_t nextTlv = commandOutput.find("TLV", startPosNextLine);
        if (startPosNextLine != std::string::npos &&
            nextTlv != std::string::npos)
        {
            size_t endLinePos = commandOutput.rfind('\n', nextTlv);
            if (endLinePos != std::string::npos)
            {
                result = commandOutput.substr(startPosNextLine,
                                              endLinePos - startPosNextLine);
                // Erase spaces from the start of a string
                result.erase(result.begin(),
                             std::find_if(result.begin(), result.end(),
                                          [](unsigned char ch) {
                                              return !std::isspace(ch);
                                          }));
            }
        }
    }
    return result;
}

// Find the line that contains the searchString and return the string
// after the ":" and searchString
inline std::string findLineContaining(const std::string& commandOutput,
                                      const std::string& searchString)
{
    std::string result{};
    size_t pos = commandOutput.find(searchString);
    if (pos != std::string::npos)
    {
        size_t startPos = commandOutput.find(':', pos);
        size_t endPos = commandOutput.find('\n', startPos);
        if (endPos != std::string::npos)
        {
            result = commandOutput.substr(startPos + 1, endPos - startPos - 1);
            // Erase spaces from the start of a string
            result.erase(result.begin(),
                         std::find_if(result.begin(), result.end(),
                                      [](unsigned char ch) {
                                          return !std::isspace(ch);
                                      }));
        }
    }
    return result;
}

inline void setLldpTlvProperty(
    nlohmann::json& jsonSchema, const std::string& property,
    const std::string& propertyValue, const std::string& lldpType)
{
    if (!propertyValue.empty())
    {
        jsonSchema[property] = propertyValue;
    }
    else if (lldpType == lldpTransmit)
    {
        jsonSchema[property] = "";
    }
}

inline void getLldpTlvs(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& ifaceId, bool isReceived)
{
    LldpToolUtil::run(
        ifaceId, LldpTlv::ALL, LldpCommandType::GET, isReceived, asyncResp,
        [ifaceId,
         isReceived](const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                     const std::string& stdOut, const std::string&,
                     const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                messages::resourceErrorsDetectedFormatError(
                    asyncResp->res,
                    "/redfish/v1/Managers/" +
                        std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                        "/DedicatedNetworkPorts/" + ifaceId,
                    " command failure");
                BMCWEB_LOG_ERROR("Error while running lldtool get TLV");
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "Error while running lldtool get TLV, Message: {}",
                        ec.message());
                }
                return;
            }
            std::string idStr;
            std::string lldpType = isReceived ? lldpReceive : lldpTransmit;
            nlohmann::json& jsonSchema =
                asyncResp->res.jsonValue["Ethernet"][lldpType];
            idStr = getTlvString(stdOut, "Chassis ID TLV");
            if (!idStr.empty())
            {
                jsonSchema["ChassisId"] = idStr;
                jsonSchema["ChassisIdSubtype"] = getChassisSubType(idStr);
            }
            else if (lldpType == lldpTransmit)
            {
                jsonSchema["ChassisId"] = "";
                jsonSchema["ChassisIdSubtype"] = "NotTransmitted";
            }

            idStr = getTlvString(stdOut, "Port ID TLV");
            if (!idStr.empty())
            {
                jsonSchema["PortId"] = idStr;
                jsonSchema["PortIdSubtype"] = getPortSubType(idStr);
            }
            else if (lldpType == lldpTransmit)
            {
                jsonSchema["PortId"] = "";
                jsonSchema["PortIdSubtype"] = "NotTransmitted";
            }

            idStr = getTlvString(stdOut, "System Capabilities TLV");
            if (!idStr.empty())
            {
                std::vector<std::string> capabilities =
                    parseLldpCapabilities(idStr);
                if ((!capabilities.empty() && lldpType == lldpReceive) ||
                    (lldpType == lldpTransmit))
                {
                    jsonSchema["SystemCapabilities"] = capabilities;
                }
            }

            idStr = getTlvString(stdOut, "System Description TLV");
            setLldpTlvProperty(jsonSchema, "SystemDescription", idStr,
                               lldpType);

            idStr = getTlvString(stdOut, "System Name TLV");
            setLldpTlvProperty(jsonSchema, "SystemName", idStr, lldpType);

            idStr = getTlvString(stdOut, "Management Address TLV");
            if (!idStr.empty())
            {
                std::string managementAddress =
                    findLineContaining(idStr, "IPv4");
                setLldpTlvProperty(jsonSchema, "ManagementAddressIPv4",
                                   managementAddress, lldpType);

                std::string managementAddressMac =
                    findLineContaining(idStr, "MAC");
                setLldpTlvProperty(jsonSchema, "ManagementAddressMAC",
                                   managementAddressMac, lldpType);
            }

            idStr = getTlvString(stdOut, "VLAN Name TLV");
            // Matches all strings that represent a valid integer
            std::regex e("(\\d+)");
            std::smatch match;
            if (!idStr.empty() && std::regex_search(idStr, match, e) &&
                match.size() > 1)
            {
                jsonSchema["ManagementVlanId"] = std::stoi(match.str(1));
            }
            else if (lldpType == lldpTransmit)
            {
                jsonSchema["ManagementVlanId"] = 4095;
            }
        });
}

inline void
    getLldpInformation(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& ifaceId)
{
    static bool islldpEnable = false;
    if (!islldpEnable)
    {
        getEnableLldpTlvs(asyncResp, ifaceId);
        islldpEnable = true;
    }
    getLldpStatus(asyncResp, ifaceId);
    getLldpTlvs(asyncResp, ifaceId, true);
    getLldpTlvs(asyncResp, ifaceId, false);
}

inline void requestDedicatedPortsInterfacesRoutes(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/DedicatedNetworkPorts/")
        .privileges(redfish::privileges::getEthernetInterfaceCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& managerName) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                asyncResp->res.jsonValue["@odata.type"] =
                    "#PortCollection.PortCollection";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Managers/" +
                    std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                    "/DedicatedNetworkPorts";
                asyncResp->res.jsonValue["Name"] =
                    "Ethernet Dedicated Port Interface Collection";
                asyncResp->res.jsonValue["Description"] =
                    "The dedicated network ports of the manager";

                // Get eth interface list, and call the below callback for JSON
                // preparation
                getEthernetIfaceList(
                    [asyncResp](const bool& success,
                                const std::vector<std::string>& ifaceList) {
                        if (!success)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        int entryIdx = 1;
                        nlohmann::json& ifaceArray =
                            asyncResp->res.jsonValue["Members"];
                        ifaceArray = nlohmann::json::array();
                        std::string tag = "vlan";
                        for (const std::string& ifaceItem : ifaceList)
                        {
                            std::size_t found = ifaceItem.find(tag);
                            if (found == std::string::npos)
                            {
                                nlohmann::json::object_t iface;
                                iface["@odata.id"] =
                                    "/redfish/v1/Managers/" +
                                    std::string(
                                        BMCWEB_REDFISH_MANAGER_URI_NAME) +
                                    "/DedicatedNetworkPorts/" +
                                    std::to_string(entryIdx);
                                ifaceArray.push_back(std::move(iface));
                                ++entryIdx;
                            }
                        }
                        asyncResp->res.jsonValue["Members@odata.count"] =
                            ifaceArray.size();
                    });
            });

    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/DedicatedNetworkPorts/<str>/")
        .privileges(redfish::privileges::getEthernetInterface)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& managerName,
                   const std::string& entryIdx) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }
                asyncResp->res.jsonValue["@odata.type"] = "#Port.v1_9_0.Port";
                asyncResp->res.jsonValue["@odata.id"] =
                    "/redfish/v1/Managers/" +
                    std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                    "/DedicatedNetworkPorts/" + entryIdx;
                asyncResp->res.jsonValue["Name"] =
                    "Manager Dedicated Network Port";
                asyncResp->res.jsonValue["Id"] = entryIdx;
                getEthernetIfaceList(
                    [asyncResp,
                     entryIdx](const bool& success,
                               const std::vector<std::string>& ifaceList) {
                        if (!success)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        int entryIdxInt = std::stoi(entryIdx);
                        int count = 1;
                        std::string tag = "vlan";
                        nlohmann::json& ifaceArray =
                            asyncResp->res
                                .jsonValue["Links"]["EthernetInterfaces"];
                        for (const std::string& ifaceItem : ifaceList)
                        {
                            // take only none vlan interfaces
                            std::size_t found = ifaceItem.find(tag);
                            if (found == std::string::npos)
                            {
                                if (count == entryIdxInt)
                                {
                                    getLldpInformation(asyncResp, ifaceItem);
                                    nlohmann::json::object_t iface;
                                    iface["@odata.id"] =
                                        "/redfish/v1/Managers/" +
                                        std::string(
                                            BMCWEB_REDFISH_MANAGER_URI_NAME) +
                                        "/EthernetInterfaces/" + ifaceItem;
                                    ifaceArray.push_back(std::move(iface));
                                    return;
                                }
                                ++count;
                            }
                        }
                        BMCWEB_LOG_ERROR("No internet interface was found ");
                    });
            });

    BMCWEB_ROUTE(app, "/redfish/v1/Managers/<str>/DedicatedNetworkPorts/<str>/")
        .privileges(redfish::privileges::patchEthernetInterface)
        .methods(boost::beast::http::verb::patch)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                   [[maybe_unused]] const std::string& managerName,
                   const std::string& ifaceInx) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                std::optional<bool> lldpEnabled;
                if (!json_util::readJsonPatch(req, asyncResp->res,
                                              "LLDPEnabled", lldpEnabled))
                {
                    return;
                }
                if (lldpEnabled)
                {
                    LldpTlv commandType = LldpTlv::DISABLE_ADMIN_STATUS;
                    if (*lldpEnabled)
                    {
                        commandType = LldpTlv::ENABLE_ADMIN_STATUS;
                    }
                    getEthernetIfaceList(
                        [asyncResp, ifaceInx, commandType](
                            const bool& success,
                            const std::vector<std::string>& ifaceList) {
                            if (!success)
                            {
                                messages::internalError(asyncResp->res);
                                return;
                            }
                            int entryIdxInt = std::stoi(ifaceInx);
                            int count = 1;
                            std::string tag = "vlan";
                            for (const std::string& ifaceItem : ifaceList)
                            {
                                std::size_t found = ifaceItem.find(tag);
                                // Take only none vlan interfaces
                                if (found == std::string::npos)
                                {
                                    if (count == entryIdxInt)
                                    {
                                        setLldpStatus(asyncResp, ifaceItem,
                                                      commandType);
                                        return;
                                    }
                                    ++count;
                                }
                            }
                            BMCWEB_LOG_ERROR(
                                "No internet interface was found ");
                        });
                }
            });
}

} // namespace redfish
