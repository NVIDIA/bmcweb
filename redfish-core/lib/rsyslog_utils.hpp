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
#include "app.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "query.hpp"
#include "redfish_util.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/json_utils.hpp"
#include "utils/stl_utils.hpp"

#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/asio/property.hpp>

#include <array>
#include <optional>
#include <regex>
#include <string_view>
#include <variant>

namespace redfish
{
namespace rsyslog
{
inline std::string getLastWordAfterDot(std::string str)
{
    // Find the position of the last dot
    size_t pos = str.rfind('.');
    if (pos != std::string::npos)
    {
        // Return the substring after the last dot
        return str.substr(pos + 1);
    }
    return str; // Return the whole string if no dot is found
}

inline std::optional<bool> isEnabled(const std::optional<std::string>& input)
{
    if (*input == "Enabled")
    {
        return true; // Enabled
    }
    return false;    // Not enabled
}

inline std::optional<std::string>
    getEnabledState(const std::optional<bool>& state)
{
    if (!state.has_value())
    {
        return std::nullopt;                       // No value provided
    }
    return state.value() ? "Enabled" : "Disabled"; // Map true/false to strings
}

// Function to validate IPv4 address
inline bool isValidIPv4(const std::string& ipAddress)
{
    std::vector<std::string> parts;
    bmcweb::split(parts, ipAddress, '.');
    if (parts.size() != 4)
    {
        return false; // IPv4 must have exactly 4 parts
    }

    for (const auto& part : parts)
    {
        if (part.empty() || part.length() > 3)
        {
            return false; // Each part must be 1-3 characters
        }
        for (char ch : part)
        {
            if (!isdigit(ch))
            {
                return false; // Only digits are allowed
            }
        }
        int num = std::stoi(part);
        if (num < 0 || num > 255)
        {
            return false; // Each part must be between 0 and 255
        }
    }
    return true;
}

// Function to validate IPv6 address
inline bool isValidIPv6(const std::string& ipAddress)
{
    std::vector<std::string> parts;
    bmcweb::split(parts, ipAddress, '.');
    if (parts.size() < 3 || parts.size() > 8)
    {
        return false; // IPv6 must have 3-8 parts
    }

    for (const auto& part : parts)
    {
        if (part.length() > 4)
        {
            return false; // Each part must be 1-4 hex digits
        }
        for (char ch : part)
        {
            if (!isxdigit(ch))
            {
                return false; // Only hex digits are allowed
            }
        }
    }
    return true;
}

// Function to validate IP address (IPv4 or IPv6)
inline bool isValidIpAddress(const std::string& ipAddress)
{
    if (ipAddress.find('.') != std::string::npos)
    {
        return isValidIPv4(ipAddress); // Check for IPv4
    }
    if (ipAddress.find(':') != std::string::npos)
    {
        return isValidIPv6(ipAddress); // Check for IPv6
    }
    return false;                      // Not a valid IP address
}

// Function to validate port
inline bool isValidPort(int port)
{
    return port >= 0 && port <= 65535; // Port must be in the valid range
}

// Sanity function
inline bool sanityCheck(const std::string& ipAddress,
                        std::optional<uint16_t> port)
{
    if (ipAddress.empty() && !port.has_value())
    {
        return true; // Skip checks if both are "empty".
    }

    if (!ipAddress.empty() && !isValidIpAddress(ipAddress))
    {
        return false; // Validate IP address if present.
    }

    if (port.has_value() && !isValidPort(*port))
    {
        return false; // Validate port if present.
    }

    return true; // Either valid values or skipped checks.
}

inline void populateRsyslogClientSettings(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    const std::string service = "xyz.openbmc_project.Syslog.Config";
    const std::string path = "/xyz/openbmc_project/logging/config/remote";

    asyncResp->res.jsonValue["Oem"] = nlohmann::json::object();
    asyncResp->res.jsonValue["Oem"]["Nvidia"] = nlohmann::json::object();
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["Rsyslog"] =
        nlohmann::json::object();

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, path, "",
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::DBusPropertiesMap& properties) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to get properties: {}", ec.message());
            messages::internalError(asyncResp->res);
            return;
        }

        // Declare variables to unpack properties into
        std::optional<bool> enabled;
        std::optional<std::string> address;
        std::optional<uint16_t> port;
        std::optional<bool> tls;
        std::optional<std::vector<std::string>> facility;
        std::optional<std::string> severity;
        std::optional<std::string> transportProtocol;

        // Unpack properties from the map
        const bool success = sdbusplus::unpackPropertiesNoThrow(
            dbus_utils::UnpackErrorPrinter(), properties, "Enabled", enabled,
            "Address", address, "Port", port, "Tls", tls, "Facility", facility,
            "Severity", severity, "TransportProtocol", transportProtocol);

        if (!success)
        {
            messages::internalError(asyncResp->res);
            return;
        }

        // Populate JSON response with the retrieved properties
        if (enabled)
        {
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["Rsyslog"]["State"] =
                getEnabledState(enabled);
        }
        if (address)
        {
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["Rsyslog"]["Address"] =
                *address;
        }
        if (port)
        {
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["Rsyslog"]["Port"] =
                *port;
        }
        if (tls)
        {
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["Rsyslog"]["TLS"] =
                getEnabledState(tls);
        }
        if (facility)
        {
            nlohmann::json& jsonArray =
                asyncResp->res.jsonValue["Oem"]["Nvidia"]["Rsyslog"]["Filter"]
                                        ["Facilities"];
            jsonArray = nlohmann::json::array();
            for (const auto& f : *facility)
            {
                jsonArray.push_back(getLastWordAfterDot(f));
            }
        }
        if (severity)
        {
            asyncResp->res.jsonValue["Oem"]["Nvidia"]["Rsyslog"]["Filter"]
                                    ["LowestSeverity"] =
                getLastWordAfterDot(*severity);
        }
        if (transportProtocol)
        {
            asyncResp->res
                .jsonValue["Oem"]["Nvidia"]["Rsyslog"]["TransportProtocol"] =
                getLastWordAfterDot(*transportProtocol);
        }
    });
}

inline void
    setRsyslogProperty(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                       const std::string& service, const std::string& path,
                       const std::string& interface,
                       const std::string& property, const auto& value)
{
    sdbusplus::asio::setProperty(
        *crow::connections::systemBus, service, path, interface, property,
        value, [asyncResp, property](const boost::system::error_code& ec) {
        if (ec)
        {
            BMCWEB_LOG_ERROR("Failed to set property {}: {}", property,
                             ec.message());
            messages::internalError(asyncResp->res);
            return;
        }
    });
}

inline void processRsyslogClientSettings(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::optional<std::string>& address = std::nullopt,
    const std::optional<uint16_t>& port = std::nullopt,
    const std::optional<std::string>& state = std::nullopt,
    const std::optional<std::string>& TLS = std::nullopt,
    const std::optional<std::vector<std::string>>& facilities = std::nullopt,
    const std::optional<std::string>& severity = std::nullopt,
    const std::optional<std::string>& transportProtocol = std::nullopt)
{
    const std::string path = "/xyz/openbmc_project/logging/config/remote";
    const std::string service = "xyz.openbmc_project.Syslog.Config";

    // Set State
    if (state.has_value() && !state->empty())
    {
        const std::optional<bool>& enabled = isEnabled(state);
        setRsyslogProperty(asyncResp, service, path,
                           "xyz.openbmc_project.Logging.RsyslogClient",
                           "Enabled", *enabled);
    }

    // Set Address
    if (address)
    {
        setRsyslogProperty(asyncResp, service, path,
                           "xyz.openbmc_project.Network.Client", "Address",
                           *address);
    }

    // Set Port
    if (port)
    {
        setRsyslogProperty(asyncResp, service, path,
                           "xyz.openbmc_project.Network.Client", "Port", *port);
    }

    // Set TLS
    if (TLS.has_value() && !TLS->empty())
    {
        const std::optional<bool>& tls = isEnabled(TLS);
        setRsyslogProperty(asyncResp, service, path,
                           "xyz.openbmc_project.Logging.RsyslogClient", "Tls",
                           *tls);
    }

    // Validate and Set Facility
    if (facilities)
    {
        std::vector<std::string> facilityDbus;
        for (const auto& f : *facilities)
        {
            if (f == "All")
            {
                // If "All" is found, clear the array and only add "All"
                facilityDbus.clear();
                facilityDbus.push_back(
                    "xyz.openbmc_project.Logging.RsyslogClient.FacilityType.All");
                break; // Exit the loop as "All" supersedes other values
            }
            else if (f == "Daemon" || f == "Kern")
            {
                facilityDbus.push_back(
                    "xyz.openbmc_project.Logging.RsyslogClient.FacilityType." +
                    f);
            }
            else
            {
                BMCWEB_LOG_ERROR("Invalid facility: {}", f);
                messages::propertyValueFormatError(asyncResp->res, f,
                                                   "Filter/Facilities");
                return;
            }
        }
        setRsyslogProperty(asyncResp, service, path,
                           "xyz.openbmc_project.Logging.RsyslogClient",
                           "Facility", facilityDbus);
    }

    // Validate and Set Severity
    if (severity)
    {
        if (*severity == "Error" || *severity == "Warning" ||
            *severity == "Info" || *severity == "All")
        {
            std::string severityDbus =
                "xyz.openbmc_project.Logging.RsyslogClient.SeverityType." +
                *severity;
            setRsyslogProperty(asyncResp, service, path,
                               "xyz.openbmc_project.Logging.RsyslogClient",
                               "Severity", severityDbus);
        }
        else
        {
            BMCWEB_LOG_ERROR("Invalid severity: {}", *severity);
            messages::propertyValueFormatError(asyncResp->res, *severity,
                                               "Filter/LowestSeverity");
            return;
        }
    }

    // Set Transport Protocol
    if (transportProtocol)
    {
        if (*transportProtocol == "TCP" || *transportProtocol == "UDP")
        {
            std::string protocolDbus =
                "xyz.openbmc_project.Network.Client.TransportProtocol." +
                *transportProtocol;
            setRsyslogProperty(asyncResp, service, path,
                               "xyz.openbmc_project.Network.Client",
                               "TransportProtocol", protocolDbus);
        }
        else
        {
            BMCWEB_LOG_ERROR("Invalid transport protocol: {}",
                             *transportProtocol);
            messages::propertyValueFormatError(
                asyncResp->res, *transportProtocol, "TransportProtocol");
            return;
        }
    }
    messages::success(asyncResp->res);
    asyncResp->res.result(boost::beast::http::status::ok);
}

} // namespace rsyslog
} // namespace redfish
