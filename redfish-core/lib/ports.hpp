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
#include "error_messages.hpp"
#include "ethernet.hpp"
#include "lldptool_util.hpp"
#include "nvidia_error_messages.hpp"
#include "utils/nvidia_utils.hpp"

#include <app.hpp>

#include <filesystem>

namespace redfish
{
// Enumeration of chassis ID subtypes as defined in IEEE 802.1AB
enum class ChassisIdSubtype
{
    Reserved = 0,         // Reserved value
    ChassisComponent = 1, // Chassis component identifier
    InterfaceAlias = 2,   // Interface alias
    PortComponent = 3,    // Port component identifier
    MACAddress = 4,       // MAC address
    NetworkAddress = 5,   // Network address
    InterfaceName = 6,    // Interface name
    LocallyAssigned = 7   // Locally assigned identifier
};

// Enumeration of port ID subtypes as defined in IEEE 802.1AB
enum class PortIdSubtype
{
    Reserved = 0,       // Reserved value
    InterfaceAlias = 1, // Interface alias
    PortComponent = 2,  // Port component identifier
    MACAddress = 3,     // MAC address
    NetworkAddress = 4, // Network address
    InterfaceName = 5,  // Interface name
    AgentCircuitID = 6, // Agent circuit ID
    LocallyAssigned = 7 // Locally assigned identifier
};

// Constants for LLDP transmit and receive types
const std::string lldpTransmit = "LLDPTransmit";
const std::string lldpReceive = "LLDPReceive";

/**
 * @brief Get the LLDP status for a network interface
 * @param asyncResp - Pointer to object holding response data
 * @param ifaceId - Network interface identifier
 */
inline void getLldpStatus(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& ifaceId)
{
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    LldpUtil::run(
        ifaceId, LldpTlv::ADMIN_STATUS, LldpCommandType::GET_LLDP, false,
        asyncResp,
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        [ifaceId](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                  const std::string& stdOut, const std::string&,
                  const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                messages::resourceErrorsDetectedFormatError(
                    aResp->res,
                    "/redfish/v1/Managers/" +
                        std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                        "/DedicatedNetworkPorts/" + ifaceId,
                    " command failure");
                BMCWEB_LOG_ERROR("Error while running lldtool get status");
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "Error while running lldtool get status, Message: {}",
                        ec);
                }
                return;
            }
            // Parse the LLDP status from the output
            if (stdOut.find("adminStatus=") != std::string::npos)
            {
                if (stdOut.find("disabled") != std::string::npos)
                {
                    aResp->res.jsonValue["Ethernet"]["LLDPEnabled"] = false;
                }
                else
                {
                    aResp->res.jsonValue["Ethernet"]["LLDPEnabled"] = true;
                }
            }
            BMCWEB_LOG_DEBUG("get Lldp Status: {}", stdOut);
        });
}

/**
 * @brief Set the LLDP status for a network interface
 * @param asyncResp - Pointer to object holding response data
 * @param ifaceId - Network interface identifier
 * @param commandType - Type of LLDP command to execute
 */
inline void setLldpStatus(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& ifaceId, LldpTlv commandType)
{
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    LldpUtil::run(
        ifaceId, commandType, LldpCommandType::SET_LLDP, false, asyncResp,
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        [ifaceId](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                  const std::string&, const std::string&,
                  const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                messages::resourceErrorsDetectedFormatError(
                    aResp->res,
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

/**
 * @brief Enable LLDP TLVs for system capabilities, description, and name
 * @param asyncResp - Pointer to object holding response data
 * @param ifaceId - Network interface identifier
 */
inline void getEnableLldpTlvs(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& ifaceId)
{
    // Enable system capabilities TLV
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    LldpUtil::run(
        ifaceId, LldpTlv::SYSTEM_CAPABILITIES, LldpCommandType::ENABLE_TLV,
        false, asyncResp,
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        [ifaceId](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                  const std::string& stdOut, const std::string&,
                  const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                messages::resourceErrorsDetectedFormatError(
                    aResp->res,
                    "/redfish/v1/Managers/" +
                        std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                        "/DedicatedNetworkPorts/" + ifaceId,
                    " command failure");
                BMCWEB_LOG_ERROR("Error while running lldtool enable TLV");
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "Error while running lldtool enable TLV, Message: {}",
                        ec);
                }
                return;
            }
            BMCWEB_LOG_DEBUG("getEnableLldpTlvs capability enable response: {}",
                             stdOut);
        });

    // Enable system description TLV
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    LldpUtil::run(
        ifaceId, LldpTlv::SYSTEM_DESCRIPTION, LldpCommandType::ENABLE_TLV,
        false, asyncResp,
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        [ifaceId](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                  const std::string& stdOut, const std::string&,
                  const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                messages::resourceErrorsDetectedFormatError(
                    aResp->res,
                    "/redfish/v1/Managers/" +
                        std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                        "/DedicatedNetworkPorts/" + ifaceId,
                    " command failure");
                BMCWEB_LOG_ERROR("Error while running lldtool get TLV");
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "Error while running lldtool get TLV, Message: {}", ec);
                }
                return;
            }
            BMCWEB_LOG_DEBUG("getEnableLldpTlv  enable response: {}", stdOut);
        });

    // Enable system name TLV
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    LldpUtil::run(
        ifaceId, LldpTlv::SYSTEM_NAME, LldpCommandType::ENABLE_TLV, false,
        asyncResp,
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        [ifaceId](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                  const std::string& stdOut, const std::string&,
                  const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                messages::resourceErrorsDetectedFormatError(
                    aResp->res,
                    "/redfish/v1/Managers/" +
                        std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                        "/DedicatedNetworkPorts/" + ifaceId,
                    " command failure");
                BMCWEB_LOG_ERROR("Error while running lldtool enable TLV");
                if (ec)
                {
                    BMCWEB_LOG_ERROR(
                        "Error while running lldtool enable TLV, message: {}",
                        ec);
                }
                return;
            }
            BMCWEB_LOG_DEBUG("lldptool capability enable response: {}", stdOut);
        });
}

/**
 * @brief Set LLDP TLV property in JSON schema
 * @param jsonSchema - The JSON schema to update
 * @param property - The property name
 * @param propertyValue - The property value
 * @param lldpType - The LLDP type (transmit/receive)
 */
inline void setLldpTlvProperty(
    nlohmann::json& jsonSchema, const std::string& property,
    const std::string& propertyValue, const std::string& lldpType)
{
    if (property == "SystemCapabilities")
    {
        // Input format examples from D-Bus:
        // 1. Single capability:
        // "xyz.openbmc_project.Network.LLDP.TLVs.SystemCapabilities.Station"
        // 2. Multiple capabilities: they are comma-separated in the input
        // string.
        //    "xyz.openbmc_project.Network.LLDP.TLVs.SystemCapabilities.Station"
        //    +
        //    ",xyz.openbmc_project.Network.LLDP.TLVs.SystemCapabilities.Bridge"
        // The format is:
        // "xyz.openbmc_project.Network.LLDP.TLVs.SystemCapabilities.<CapabilityName>"
        // We split by comma in case of multiple capabilities and then by dot,
        // and keep only the last part
        // Example output for
        // "xyz.openbmc_project.Network.LLDP.TLVs.SystemCapabilities.Station":
        // ["Station"]
        // Example output for multiple capabilities:
        // ["Station", "Bridge"]
        std::vector<std::string> caps;
        std::istringstream ss(propertyValue);
        std::string cap;
        while (std::getline(ss, cap, ','))
        {
            trim(cap);
            size_t lastDot = cap.find_last_of('.');
            if (lastDot != std::string::npos)
            {
                cap = cap.substr(lastDot + 1);
            }
            if (!cap.empty())
            {
                caps.push_back(cap);
            }
        }
        jsonSchema["Ethernet"][lldpType][property] = caps;
    }
    else if (property == "ChassisIdSubtype" || property == "PortIdSubtype")
    {
        std::string subtype = propertyValue;
        size_t lastDot = subtype.find_last_of('.');
        if (lastDot != std::string::npos)
        {
            subtype = subtype.substr(lastDot + 1);
        }
        // Map string to enum index
        int subtypeIndex = 0;
        if (property == "ChassisIdSubtype")
        {
            if (subtype == "ChassisComponent")
            {
                subtypeIndex = 1;
            }
            else if (subtype == "InterfaceAlias")
            {
                subtypeIndex = 2;
            }
            else if (subtype == "PortComponent")
            {
                subtypeIndex = 3;
            }
            else if (subtype == "MacAddr" || subtype == "MACAddress" ||
                     subtype == "MacAddress")
            {
                subtypeIndex = 4;
            }
            else if (subtype == "NetworkAddress")
            {
                subtypeIndex = 5;
            }
            else if (subtype == "InterfaceName" || subtype == "IfName")
            {
                subtypeIndex = 6;
            }
            else if (subtype == "LocallyAssigned")
            {
                subtypeIndex = 7;
            }
        }
        else // PortIdSubtype
        {
            if (subtype == "InterfaceAlias")
            {
                subtypeIndex = 1;
            }
            else if (subtype == "PortComponent")
            {
                subtypeIndex = 2;
            }
            else if (subtype == "MacAddr" || subtype == "MACAddress" ||
                     subtype == "MacAddress")
            {
                subtypeIndex = 3;
            }
            else if (subtype == "NetworkAddress")
            {
                subtypeIndex = 4;
            }
            else if (subtype == "InterfaceName" || subtype == "IfName")
            {
                subtypeIndex = 5;
            }
            else if (subtype == "AgentCircuitID" || subtype == "AgentId")
            {
                subtypeIndex = 6;
            }
            else if (subtype == "LocallyAssigned")
            {
                subtypeIndex = 7;
            }
        }
        jsonSchema["Ethernet"][lldpType][property] = subtypeIndex;
    }
    else if (property == "ManagementVlanId")
    {
        // Handle ManagementVlanId as integer
        BMCWEB_LOG_DEBUG("Processing ManagementVlanId: '{}'", propertyValue);

        try
        {
            int vlanId = std::stoi(propertyValue);
            jsonSchema["Ethernet"][lldpType][property] = vlanId;
        }
        catch (const std::exception&)
        {
            // If conversion fails, set to 0 (default)
            BMCWEB_LOG_WARNING(
                "Could not parse ManagementVlanId: '{}', setting to 0",
                propertyValue);
            jsonSchema["Ethernet"][lldpType][property] = 0;
        }
    }
    else
    {
        // Always set the property for both transmit and receive
        jsonSchema["Ethernet"][lldpType][property] = propertyValue;
    }
}

/**
 * @brief Get LLDP TLVs for a network interface
 * @param asyncResp - Pointer to object holding response data
 * @param ifaceId - Network interface identifier
 * @param isReceived - Whether to get received or transmitted TLVs
 */
inline void getLldpTlvs(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                        const std::string& ifaceId, bool isReceived)
{
    const std::string lldpType = isReceived ? lldpReceive : lldpTransmit;
    const std::string path =
        "/redfish/v1/Managers/" + std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
        "/DedicatedNetworkPorts/" + ifaceId;

    // Get Chassis ID
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    LldpUtil::run(
        ifaceId, LldpTlv::CHASSIS_ID, LldpCommandType::GET, isReceived,
        asyncResp,
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        [lldpType, path](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& stdOut, const std::string&,
                         const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                BMCWEB_LOG_ERROR("Error getting LLDP Chassis ID: {}", ec);
                return;
            }
            setLldpTlvProperty(aResp->res.jsonValue, "ChassisId", stdOut,
                               lldpType);
        });

    // Get Chassis ID Subtype
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    LldpUtil::run(
        ifaceId, LldpTlv::CHASSIS_ID_SUBTYPE, LldpCommandType::GET, isReceived,
        asyncResp,
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        [lldpType, path](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& stdOut, const std::string&,
                         const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                BMCWEB_LOG_ERROR("Error getting LLDP Chassis ID Subtype: {}",
                                 ec);
                return;
            }
            setLldpTlvProperty(aResp->res.jsonValue, "ChassisIdSubtype", stdOut,
                               lldpType);
        });

    // Get Port ID
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    LldpUtil::run(
        ifaceId, LldpTlv::PORT_ID, LldpCommandType::GET, isReceived, asyncResp,
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        [lldpType, path](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& stdOut, const std::string&,
                         const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                BMCWEB_LOG_ERROR("Error getting LLDP Port ID: {}", ec);
                return;
            }
            setLldpTlvProperty(aResp->res.jsonValue, "PortId", stdOut,
                               lldpType);
        });

    // Get Port ID Subtype
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    LldpUtil::run(
        ifaceId, LldpTlv::PORT_ID_SUBTYPE, LldpCommandType::GET, isReceived,
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        asyncResp,
        [lldpType, path](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& stdOut, const std::string&,
                         const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                BMCWEB_LOG_ERROR("Error getting LLDP Port ID Subtype: {}", ec);
                return;
            }
            setLldpTlvProperty(aResp->res.jsonValue, "PortIdSubtype", stdOut,
                               lldpType);
        });

    // Get System Name
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    LldpUtil::run(
        ifaceId, LldpTlv::SYSTEM_NAME, LldpCommandType::GET, isReceived,
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        asyncResp,
        [lldpType, path](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& stdOut, const std::string&,
                         const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                BMCWEB_LOG_ERROR("Error getting LLDP System Name: {}", ec);
                return;
            }
            setLldpTlvProperty(aResp->res.jsonValue, "SystemName", stdOut,
                               lldpType);
        });

    // Get System Description
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    LldpUtil::run(
        ifaceId, LldpTlv::SYSTEM_DESCRIPTION, LldpCommandType::GET, isReceived,
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        asyncResp,
        [lldpType, path](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& stdOut, const std::string&,
                         const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                BMCWEB_LOG_ERROR("Error getting LLDP System Description: {}",
                                 ec);
                return;
            }
            setLldpTlvProperty(aResp->res.jsonValue, "SystemDescription",
                               stdOut, lldpType);
        });

    // Get System Capabilities
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    LldpUtil::run(
        ifaceId, LldpTlv::SYSTEM_CAPABILITIES, LldpCommandType::GET, isReceived,
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        asyncResp,
        [lldpType, path](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& stdOut, const std::string&,
                         const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                BMCWEB_LOG_ERROR("Error getting LLDP System Capabilities: {}",
                                 ec);
                return;
            }
            setLldpTlvProperty(aResp->res.jsonValue, "SystemCapabilities",
                               stdOut, lldpType);
        });

    // Get Management Address IPv4
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    LldpUtil::run(
        ifaceId, LldpTlv::MANAGEMENT_ADDRESS, LldpCommandType::GET, isReceived,
        asyncResp,
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        [lldpType, path](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& stdOut, const std::string&,
                         const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                BMCWEB_LOG_ERROR(
                    "Error getting LLDP Management Address IPv4: {}", ec);
                return;
            }
            if (!stdOut.empty())
            {
                setLldpTlvProperty(aResp->res.jsonValue,
                                   "ManagementAddressIPv4", stdOut, lldpType);
            }
        });

    // Get Management Address IPv6
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    LldpUtil::run(
        ifaceId, LldpTlv::MANAGEMENT_ADDRESS_IPV6, LldpCommandType::GET,
        isReceived, asyncResp,
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        [lldpType, path](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& stdOut, const std::string&,
                         const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                BMCWEB_LOG_ERROR(
                    "Error getting LLDP Management Address IPv6: {}", ec);
                return;
            }
            if (!stdOut.empty())
            {
                setLldpTlvProperty(aResp->res.jsonValue,
                                   "ManagementAddressIPv6", stdOut, lldpType);
            }
        });

    // Get Management Address MAC
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    LldpUtil::run(
        ifaceId, LldpTlv::MANAGEMENT_ADDRESS_MAC, LldpCommandType::GET,
        isReceived, asyncResp,
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        [lldpType, path](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& stdOut, const std::string&,
                         const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                BMCWEB_LOG_ERROR(
                    "Error getting LLDP Management Address MAC: {}", ec);
                return;
            }
            setLldpTlvProperty(aResp->res.jsonValue, "ManagementAddressMAC",
                               stdOut, lldpType);
        });

    // Get Management VLAN ID
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    LldpUtil::run(
        ifaceId, LldpTlv::MANAGEMENT_VLAN_ID, LldpCommandType::GET, isReceived,
        asyncResp,
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        [lldpType, path](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                         const std::string& stdOut, const std::string&,
                         const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                BMCWEB_LOG_ERROR("Error getting LLDP Management VLAN ID: {}",
                                 ec);
                return;
            }
            setLldpTlvProperty(aResp->res.jsonValue, "ManagementVlanId", stdOut,
                               lldpType);
        });
}

/**
 * @brief Get complete LLDP information for a network interface
 * @param asyncResp - Pointer to object holding response data
 * @param ifaceId - Network interface identifier
 */
inline void getLldpInformation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& ifaceId)
{
    // Get LLDP status first
    getLldpStatus(asyncResp, ifaceId);

    // Get LLDP TLVs for transmitted data
    getLldpTlvs(asyncResp, ifaceId, false);

    // Get LLDP TLVs for received data
    getLldpTlvs(asyncResp, ifaceId, true);
}

/**
 * @brief Get LLDP information with index for a network interface
 * @param asyncResp - Pointer to object holding response data
 * @param ifaceId - Network interface identifier
 * @param entryIdx - Entry index for the interface
 */
inline void getLldpInformationWithIndex(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& ifaceId, const std::string& entryIdx)
{
    // Get LLDP status first
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    LldpUtil::run(
        ifaceId, LldpTlv::ADMIN_STATUS, LldpCommandType::GET_LLDP, false,
        asyncResp,
        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        [entryIdx](const std::shared_ptr<bmcweb::AsyncResp>& aResp,
                   const std::string& stdOut, const std::string&,
                   const boost::system::error_code& ec, int errorCode) {
            if (ec || errorCode)
            {
                // Don't report error if we can't get LLDP status
                // Just set LLDPEnabled to false and continue
                aResp->res.jsonValue["Ethernet"]["LLDPEnabled"] = false;
                BMCWEB_LOG_DEBUG(
                    "LLDP status not available, defaulting to disabled");
                return;
            }
            // The status is now directly returned as "enabled" or "disabled"
            aResp->res.jsonValue["Ethernet"]["LLDPEnabled"] =
                (stdOut == "enabled");
            BMCWEB_LOG_DEBUG("get Lldp Status: {}", stdOut);
        });

    // Get LLDP TLVs for transmitted data
    getLldpTlvs(asyncResp, ifaceId, false);

    // Get LLDP TLVs for received data
    getLldpTlvs(asyncResp, ifaceId, true);
}

/**
 * @brief Check if a network interface is physical (not virtual)
 * @param ifaceName - Name of the network interface
 * @return true if interface is physical hardware, false if virtual
 *
 * In Linux, network interfaces are symlinked in /sys/class/net/:
 * - Physical interfaces: point to /devices/platform/.../net/<name> (hardware
 * device)
 * - Virtual interfaces: point to /devices/virtual/net/<name> (no hardware)
 *
 * This function checks if the interface path contains "/virtual/" to determine
 * if it's virtual.
 */
inline bool isPhysicalInterface(const std::string& ifaceName)
{
    std::string ifacePath = "/sys/class/net/" + ifaceName;
    std::error_code ec;
    // Read the symlink to see where it points
    auto target = std::filesystem::read_symlink(ifacePath, ec);
    if (ec)
    {
        return false; // Can't read symlink, assume not physical
    }
    // Virtual interfaces have "/virtual/" in their path
    std::string targetStr = target.string();
    return targetStr.find("/virtual/") == std::string::npos;
}

/**
 * @brief Get list of physical (non-VLAN) Ethernet interfaces
 * @param callback - Function to call with the list of physical interfaces
 *
 * This function queries D-Bus to get all Ethernet interfaces and filters out
 * VLAN interfaces by checking if they expose the
 * xyz.openbmc_project.Network.VLAN D-Bus interface. Only physical interfaces
 * (without VLAN interface) are returned.
 */
template <typename CallbackFunc>
void getPhysicalEthernetIfaceList(CallbackFunc&& callback)
{
    sdbusplus::message::object_path path("/xyz/openbmc_project/network");
    dbus::utility::getManagedObjects(
        "xyz.openbmc_project.Network", path,
        [callback = std::forward<CallbackFunc>(callback)](
            const boost::system::error_code& ec,
            const dbus::utility::ManagedObjectType& resp) {
            std::vector<std::string> physicalIfaceList;
            physicalIfaceList.reserve(resp.size());

            if (ec)
            {
                callback(false, physicalIfaceList);
                return;
            }

            // Iterate over all network objects
            for (const auto& objpath : resp)
            {
                bool hasEthernetInterface = false;
                bool hasVlanInterface = false;

                // Check what interfaces this object exposes
                for (const auto& interface : objpath.second)
                {
                    if (interface.first ==
                        "xyz.openbmc_project.Network.EthernetInterface")
                    {
                        hasEthernetInterface = true;
                    }
                    else if (interface.first ==
                             "xyz.openbmc_project.Network.VLAN")
                    {
                        hasVlanInterface = true;
                    }
                }

                // Only include interfaces that are Ethernet but NOT VLAN
                if (hasEthernetInterface && !hasVlanInterface)
                {
                    std::string ifaceId = objpath.first.filename();
                    // Exclude virtual interfaces via sysfs check.
                    // Virtual: /sys/devices/virtual/net/, Physical:
                    // /sys/devices/platform/.../net/
                    if (!ifaceId.empty() && isPhysicalInterface(ifaceId))
                    {
                        physicalIfaceList.emplace_back(ifaceId);
                    }
                }
            }

            std::ranges::sort(physicalIfaceList, AlphanumLess<std::string>());
            callback(true, physicalIfaceList);
        });
}

/**
 * @brief Set up routes for dedicated network ports
 * @param app - The application instance
 */
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

                // Get physical interface list (excludes VLANs based on D-Bus
                // interface)
                getPhysicalEthernetIfaceList(
                    [asyncResp](const bool& success,
                                const std::vector<std::string>& ifaceList) {
                        if (!success)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        nlohmann::json& ifaceArray =
                            asyncResp->res.jsonValue["Members"];
                        ifaceArray = nlohmann::json::array();
                        for (size_t entryIdx = 1; entryIdx <= ifaceList.size();
                             ++entryIdx)
                        {
                            nlohmann::json::object_t iface;
                            iface["@odata.id"] =
                                "/redfish/v1/Managers/" +
                                std::string(BMCWEB_REDFISH_MANAGER_URI_NAME) +
                                "/DedicatedNetworkPorts/" +
                                std::to_string(entryIdx);
                            ifaceArray.push_back(std::move(iface));
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
                getPhysicalEthernetIfaceList(
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
                        nlohmann::json& ifaceArray =
                            asyncResp->res
                                .jsonValue["Links"]["EthernetInterfaces"];
                        for (const std::string& ifaceItem : ifaceList)
                        {
                            if (count == entryIdxInt)
                            {
                                // Get LLDP information for the dedicated
                                // network port
                                getLldpInformationWithIndex(
                                    asyncResp, ifaceItem, entryIdx);
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
                    getPhysicalEthernetIfaceList(
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
                            for (const std::string& ifaceItem : ifaceList)
                            {
                                if (count == entryIdxInt)
                                {
                                    setLldpStatus(asyncResp, ifaceItem,
                                                  commandType);
                                    return;
                                }
                                ++count;
                            }
                            BMCWEB_LOG_ERROR(
                                "No internet interface was found ");
                        });
                }
            });
}

} // namespace redfish
