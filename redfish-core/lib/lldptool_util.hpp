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

#include "logging.hpp"

#include <boost/asio.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/message.hpp>

#include <chrono>
#include <functional>
#include <iostream>
#include <string>

// Callback type definition for handling LLDP command responses
using LldpResponseCallback = std::function<void(
    const std::shared_ptr<bmcweb::AsyncResp>&, const std::string& /* stdOut*/,
    const std::string& /* stdErr*/, const boost::system::error_code& /* ec */,
    int /*errorCode */)>;

// Enumeration of LLDP TLV (Type-Length-Value) types that can be queried or set
enum class LldpTlv
{
    CHASSIS_ID,              // Chassis identifier
    CHASSIS_ID_SUBTYPE,      // Type of chassis identifier
    PORT_ID,                 // Port identifier
    PORT_ID_SUBTYPE,         // Type of port identifier
    SYSTEM_CAPABILITIES,     // System capabilities
    SYSTEM_DESCRIPTION,      // System description
    SYSTEM_NAME,             // System name
    MANAGEMENT_ADDRESS,      // IPv4 management address
    MANAGEMENT_ADDRESS_IPV6, // IPv6 management address
    MANAGEMENT_ADDRESS_MAC,  // MAC management address
    MANAGEMENT_VLAN_ID,      // Management VLAN identifier
    ADMIN_STATUS,            // Current administrative status
    ENABLE_ADMIN_STATUS,     // Enable administrative status
    DISABLE_ADMIN_STATUS,    // Disable administrative status
    ALL                      // All TLVs
};

// Enumeration of LLDP command types that can be executed
enum class LldpCommandType
{
    GET,       // Get a specific TLV value
    GET_LLDP,  // Get LLDP status
    SET_LLDP,  // Set LLDP configuration
    ENABLE_TLV // Enable specific TLV
};

/**
 * @class LldpUtil
 * @brief Utility class for handling LLDP (Link Layer Discovery Protocol)
 * operations
 *
 * This class provides methods to interact with LLDP functionality through
 * D-Bus, allowing querying and configuration of LLDP parameters.
 */
class LldpUtil
{
  public:
    /**
     * @brief Execute LLDP operations using D-Bus
     * @param ifName - The interface name (not used in D-Bus implementation)
     * @param lldpTlv - the Requested TLV type
     * @param lldpCommandType - The command type
     * @param isReceived - is the command for received TLV or for transmitted
     * TLV
     * @param asyncResp - Pointer to object holding response data
     * @param responseCallback - callback function to handle the response
     */
    static void run([[maybe_unused]] const std::string& ifName, LldpTlv lldpTlv,
                    LldpCommandType lldpCommandType, bool isReceived,
                    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                    LldpResponseCallback responseCallback);

  private:
    LldpUtil() = default;

    /**
     * @brief Get the D-Bus path for LLDP object
     * @param isReceived - is the command for received TLV or for transmitted
     * TLV
     * @return string with the D-Bus path
     */
    static std::string getLldpPath(bool isReceived)
    {
        return isReceived ? "/xyz/openbmc_project/network/lldpReceive"
                          : "/xyz/openbmc_project/network/lldpTransmit";
    }

    /**
     * @brief Get the TLV value from D-Bus
     * @param lldpTlv - the TLV type to get
     * @param isReceived - is the command for received TLV or for transmitted
     * TLV
     * @param asyncResp - Pointer to object holding response data
     * @param responseCallback - callback function to handle the response
     */
    static void getTlvValue(LldpTlv lldpTlv, bool isReceived,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            LldpResponseCallback responseCallback);

    /**
     * @brief Set the TLV value via D-Bus
     * @param lldpTlv - the TLV type to set
     * @param value - the value to set
     * @param isReceived - is the command for received TLV or for transmitted
     * TLV
     * @param asyncResp - Pointer to object holding response data
     * @param responseCallback - callback function to handle the response
     */
    static void setTlvValue(LldpTlv lldpTlv, const std::string& value,
                            bool isReceived,
                            const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            LldpResponseCallback responseCallback);
};

// Command execution
inline void LldpUtil::run([[maybe_unused]] const std::string& ifName,
                          LldpTlv lldpTlv, LldpCommandType lldpCommandType,
                          bool isReceived,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          LldpResponseCallback responseCallback)
{
    try
    {
        switch (lldpCommandType)
        {
            case LldpCommandType::GET:
            case LldpCommandType::GET_LLDP:
                getTlvValue(lldpTlv, isReceived, asyncResp, responseCallback);
                break;
            case LldpCommandType::SET_LLDP:
            case LldpCommandType::ENABLE_TLV:
                // For SET operations, we need to determine the value to set
                // This would depend on the specific TLV being set
                setTlvValue(lldpTlv, "", isReceived, asyncResp,
                            responseCallback);
                break;
        }
    }
    catch (const std::exception& e)
    {
        BMCWEB_LOG_ERROR("Error in LLDP operation: {}", e.what());
        responseCallback(asyncResp, "", e.what(),
                         boost::system::errc::make_error_code(
                             boost::system::errc::operation_canceled),
                         1);
    }
}

// Get TLV
inline void
    LldpUtil::getTlvValue(LldpTlv lldpTlv, bool isReceived,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          LldpResponseCallback responseCallback)
{
    std::string path = getLldpPath(isReceived);
    std::string interface = "xyz.openbmc_project.Network.LLDP.TLVs";
    std::string property;

    // Map TLV type to corresponding D-Bus property name
    switch (lldpTlv)
    {
        case LldpTlv::CHASSIS_ID:
            property = "ChassisId";
            break;
        case LldpTlv::CHASSIS_ID_SUBTYPE:
            property = "ChassisIdSubtype";
            break;
        case LldpTlv::PORT_ID:
            property = "PortId";
            break;
        case LldpTlv::PORT_ID_SUBTYPE:
            property = "PortIdSubtype";
            break;
        case LldpTlv::SYSTEM_CAPABILITIES:
            property = "SystemCapabilities";
            break;
        case LldpTlv::SYSTEM_DESCRIPTION:
            property = "SystemDescription";
            break;
        case LldpTlv::SYSTEM_NAME:
            property = "SystemName";
            break;
        case LldpTlv::MANAGEMENT_ADDRESS:
            property = "ManagementAddressIPv4";
            break;
        case LldpTlv::MANAGEMENT_ADDRESS_IPV6:
            property = "ManagementAddressIPv6";
            break;
        case LldpTlv::MANAGEMENT_ADDRESS_MAC:
            property = "ManagementAddressMAC";
            break;
        case LldpTlv::MANAGEMENT_VLAN_ID:
            property = "ManagementVlanId";
            break;
        case LldpTlv::ADMIN_STATUS:
            interface = "xyz.openbmc_project.Network.LLDP.Settings";
            property = "EnableLLDP";
            break;
        default:
            responseCallback(asyncResp, "", "Unsupported TLV type",
                             boost::system::errc::make_error_code(
                                 boost::system::errc::invalid_argument),
                             1);
            return;
    }

    // Get property value using a single D-Bus call
    crow::connections::systemBus->async_method_call(
        [asyncResp, responseCallback](
            const boost::system::error_code& ec,
            const std::variant<std::string, bool, uint16_t,
                               std::vector<std::string>>& variant) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Error getting LLDP property: {}", ec);
            responseCallback(asyncResp, "", "Failed to get property", ec, 1);
            return;
        }

        std::string value;
        if (const auto* strVal = std::get_if<std::string>(&variant))
        {
            value = *strVal;
        }
        else if (const auto* boolVal = std::get_if<bool>(&variant))
        {
            value = *boolVal ? "enabled" : "disabled";
        }
        else if (const auto* uintVal = std::get_if<uint16_t>(&variant))
        {
            value = std::to_string(*uintVal);
        }
        else if (const auto* vecVal =
                     std::get_if<std::vector<std::string>>(&variant))
        {
            for (const auto& cap : *vecVal)
            {
                if (!value.empty())
                {
                    value += ",";
                }
                value += cap;
            }
        }

        responseCallback(asyncResp, value, "", boost::system::error_code{}, 0);
    },
        "xyz.openbmc_project.LLDP", path, "org.freedesktop.DBus.Properties",
        "Get", interface, property);
}

// Set TLVs
inline void
    LldpUtil::setTlvValue(LldpTlv lldpTlv, const std::string& value,
                          bool isReceived,
                          const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          LldpResponseCallback responseCallback)
{
    // Cannot set values for received TLVs
    if (isReceived)
    {
        responseCallback(asyncResp, "", "Cannot set values for received TLVs",
                         boost::system::errc::make_error_code(
                             boost::system::errc::operation_not_permitted),
                         1);
        return;
    }

    std::string path = getLldpPath(isReceived);
    std::string interface = "xyz.openbmc_project.Network.LLDP.TLVs";
    std::string property;

    // Map TLV type to corresponding D-Bus property name
    switch (lldpTlv)
    {
        case LldpTlv::CHASSIS_ID:
            property = "ChassisId";
            break;
        case LldpTlv::PORT_ID:
            property = "PortId";
            break;
        case LldpTlv::SYSTEM_NAME:
            property = "SystemName";
            break;
        case LldpTlv::SYSTEM_DESCRIPTION:
            property = "SystemDescription";
            break;
        case LldpTlv::MANAGEMENT_ADDRESS:
            property = "ManagementAddressIPv4";
            break;
        default:
            responseCallback(asyncResp, "", "Unsupported TLV type",
                             boost::system::errc::make_error_code(
                                 boost::system::errc::invalid_argument),
                             1);
            return;
    }

    // Use setDbusProperty to set the property value
    redfish::setDbusProperty(asyncResp, property, "xyz.openbmc_project.LLDP",
                             sdbusplus::message::object_path(path), interface,
                             property, value);
}
