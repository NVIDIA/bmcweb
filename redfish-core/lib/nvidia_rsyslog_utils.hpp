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
#include "generated/enums/nvidia_network_protocol.hpp"
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
#include <string>
#include <string_view>

namespace redfish
{
namespace rsyslog
{

inline nvidia_network_protocol::ClientStatus dbusToClientStatus(
    bool clientState)
{
    if (clientState)
    {
        return nvidia_network_protocol::ClientStatus::Enabled;
    }
    if (!clientState)
    {
        return nvidia_network_protocol::ClientStatus::Disabled;
    }
    return nvidia_network_protocol::ClientStatus::Invalid;
}

inline nvidia_network_protocol::TLSStatus dbusToTLSStatus(const bool& tlsState)
{
    if (tlsState)
    {
        return nvidia_network_protocol::TLSStatus::Enabled;
    }
    if (!tlsState)
    {
        return nvidia_network_protocol::TLSStatus::Disabled;
    }
    return nvidia_network_protocol::TLSStatus::Invalid;
}

inline nvidia_network_protocol::FilterSeverity dbusToFilterSeverity(
    const std::string& sevStr)
{
    if (sevStr ==
        "xyz.openbmc_project.Logging.RsyslogClient.SeverityType.Error")
    {
        return nvidia_network_protocol::FilterSeverity::Error;
    }
    if (sevStr ==
        "xyz.openbmc_project.Logging.RsyslogClient.SeverityType.Warning")
    {
        return nvidia_network_protocol::FilterSeverity::Warning;
    }
    if (sevStr == "xyz.openbmc_project.Logging.RsyslogClient.SeverityType.Info")
    {
        return nvidia_network_protocol::FilterSeverity::Info;
    }
    if (sevStr == "xyz.openbmc_project.Logging.RsyslogClient.SeverityType.All")
    {
        return nvidia_network_protocol::FilterSeverity::All;
    }
    return nvidia_network_protocol::FilterSeverity::Invalid;
}

inline nvidia_network_protocol::FilterFacility dbusToFilterFacility(
    const std::string& facStr)
{
    if (facStr ==
        "xyz.openbmc_project.Logging.RsyslogClient.FacilityType.Daemon")
    {
        return nvidia_network_protocol::FilterFacility::Daemon;
    }
    if (facStr == "xyz.openbmc_project.Logging.RsyslogClient.FacilityType.Kern")
    {
        return nvidia_network_protocol::FilterFacility::Kern;
    }
    if (facStr == "xyz.openbmc_project.Logging.RsyslogClient.FacilityType.All")
    {
        return nvidia_network_protocol::FilterFacility::All;
    }
    return nvidia_network_protocol::FilterFacility::Invalid;
}

inline nvidia_network_protocol::Protocol dbusToProtocol(
    const std::string& protoStr)
{
    if (protoStr == "xyz.openbmc_project.Network.Client.TransportProtocol.TCP")
    {
        return nvidia_network_protocol::Protocol::TCP;
    }
    if (protoStr == "xyz.openbmc_project.Network.Client.TransportProtocol.UDP")
    {
        return nvidia_network_protocol::Protocol::UDP;
    }
    return nvidia_network_protocol::Protocol::Invalid;
}

inline nvidia_network_protocol::RFCFormat dbusToRfcFormat(
    const std::string& rfcFormatStr)
{
    if (rfcFormatStr ==
        "xyz.openbmc_project.Logging.RsyslogClient.RfcFormatType.RFC3164")
    {
        return nvidia_network_protocol::RFCFormat::RFC3164;
    }
    if (rfcFormatStr ==
        "xyz.openbmc_project.Logging.RsyslogClient.RfcFormatType.RFC5424")
    {
        return nvidia_network_protocol::RFCFormat::RFC5424;
    }
    return nvidia_network_protocol::RFCFormat::Invalid;
}

inline std::optional<bool> isEnabled(const std::optional<std::string>& input)
{
    if (!input)
    {
        return std::nullopt; // Input is nullopt, cannot determine state
    }

    if (*input == "Enabled")
    {
        return true; // Enabled
    }
    if (*input == "Disabled")
    {
        return false; // Disabled
    }

    // Invalid input case
    BMCWEB_LOG_ERROR("Invalid input value for isEnabled: {}", *input);
    return std::nullopt;
}

inline void populateRsyslogClientSettings(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    const std::string service = "xyz.openbmc_project.Syslog.Config";
    const std::string path = "/xyz/openbmc_project/logging/config/remote";

    asyncResp->res.jsonValue["Oem"] = nlohmann::json::object();
    asyncResp->res.jsonValue["Oem"]["Nvidia"] = nlohmann::json::object();
    auto& rsyslog = asyncResp->res.jsonValue["Oem"]["Nvidia"]["Rsyslog"];
    rsyslog = nlohmann::json::object();
    rsyslog["@odata.type"] = "#NvidiaNetworkProtocol.v1_1_0.Rsyslog";

    sdbusplus::asio::getAllProperties(
        *crow::connections::systemBus, service, path, "",
        [asyncResp,
         &rsyslog](const boost::system::error_code& ec,
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
            std::optional<std::string> rfcformat;
            // Unpack properties from the map
            const bool success = sdbusplus::unpackPropertiesNoThrow(
                dbus_utils::UnpackErrorPrinter(), properties, "Enabled",
                enabled, "Address", address, "Port", port, "Tls", tls,
                "Facility", facility, "Severity", severity, "TransportProtocol",
                transportProtocol, "Rfcformat", rfcformat);

            if (!success)
            {
                messages::internalError(asyncResp->res);
                return;
            }

            // Populate JSON response with the retrieved properties
            if (enabled)
            {
                auto clientState = dbusToClientStatus(*enabled);
                rsyslog["State"] = clientState;
            }
            if (address)
            {
                rsyslog["Address"] = *address;
            }
            if (port)
            {
                rsyslog["Port"] = *port;
            }
            if (tls)
            {
                auto tlsStatus = dbusToTLSStatus(*tls);
                rsyslog["TLS"] = tlsStatus;
            }
            if (facility)
            {
                nlohmann::json& jsonArray = rsyslog["Filter"]["Facilities"];
                jsonArray = nlohmann::json::array();
                for (const auto& f : *facility)
                {
                    nvidia_network_protocol::FilterFacility fac =
                        dbusToFilterFacility(f);
                    jsonArray.push_back(fac);
                }
            }
            if (severity)
            {
                nvidia_network_protocol::FilterSeverity sev =
                    dbusToFilterSeverity(*severity);
                rsyslog["Filter"]["LowestSeverity"] = sev;
            }
            if (transportProtocol)
            {
                nvidia_network_protocol::Protocol proto =
                    dbusToProtocol(*transportProtocol);
                rsyslog["TransportProtocol"] = proto;
            }
            if (rfcformat)
            {
                nvidia_network_protocol::RFCFormat rfc =
                    dbusToRfcFormat(*rfcformat);
                rsyslog["RFCFormat"] = rfc;
            }
        });
}

inline void processRsyslogClientSettings(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::optional<std::string>& address = std::nullopt,
    const std::optional<uint16_t>& port = std::nullopt,
    const std::optional<std::string>& state = std::nullopt,
    const std::optional<std::string>& tls = std::nullopt,
    const std::optional<std::vector<std::string>>& facilities = std::nullopt,
    const std::optional<std::string>& severity = std::nullopt,
    const std::optional<std::string>& transportProtocol = std::nullopt,
    const std::optional<std::string>& rfcformat = std::nullopt)
{
    const std::string path = "/xyz/openbmc_project/logging/config/remote";
    const std::string service = "xyz.openbmc_project.Syslog.Config";
    // Set State
    if (state)
    {
        auto enabled = isEnabled(state);
        if (!enabled)
        {
            BMCWEB_LOG_ERROR("Invalid State value for rsyslog");
            messages::propertyValueFormatError(asyncResp->res, *state, "State");
            return;
        }
        setDbusProperty(asyncResp, "Oem/Nvidia/Rsyslog/State", service, path,
                        "xyz.openbmc_project.Logging.RsyslogClient", "Enabled",
                        *enabled);
    }

    // Set Address
    if (address)
    {
        setDbusProperty(asyncResp, "Oem/Nvidia/Rsyslog/Address", service, path,
                        "xyz.openbmc_project.Network.Client", "Address",
                        *address);
    }

    // Set Port
    if (port)
    {
        setDbusProperty(asyncResp, "Oem/Nvidia/Rsyslog/Port", service, path,
                        "xyz.openbmc_project.Network.Client", "Port", *port);
    }

    // Set TLS
    if (tls)
    {
        auto tlsEnabled = isEnabled(tls);
        if (!tlsEnabled)
        {
            BMCWEB_LOG_ERROR("Invalid TLS value for rsyslog");
            messages::propertyValueFormatError(asyncResp->res, *tls, "TLS");
            return;
        }
        setDbusProperty(asyncResp, "Oem/Nvidia/Rsyslog/TLS", service, path,
                        "xyz.openbmc_project.Logging.RsyslogClient", "Tls",
                        *tlsEnabled);
    }

    // Validate and Set Facility
    if (facilities)
    {
        std::vector<std::string> facilityDbus;
        for (const auto& f : *facilities)
        {
            try
            {
                nvidia_network_protocol::FilterFacility facEnum =
                    nlohmann::json(f)
                        .get<nvidia_network_protocol::FilterFacility>();

                if (facEnum == nvidia_network_protocol::FilterFacility::All)
                {
                    // If "All" is found, clear the array and only add "All"
                    facilityDbus.clear();
                    facilityDbus.emplace_back(
                        "xyz.openbmc_project.Logging.RsyslogClient.FacilityType.All");
                    break; // Exit the loop as "All" supersedes other values
                }
                if (facEnum ==
                        nvidia_network_protocol::FilterFacility::Daemon ||
                    facEnum == nvidia_network_protocol::FilterFacility::Kern)
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
            catch (const nlohmann::json::exception&)
            {
                BMCWEB_LOG_ERROR("Invalid facility (parse): {}", f);
                messages::propertyValueFormatError(asyncResp->res, f,
                                                   "Filter/Facilities");
                return;
            }
        }
        setDbusProperty(asyncResp, "Oem/Nvidia/Rsyslog/Filter/Facilities",
                        service, path,
                        "xyz.openbmc_project.Logging.RsyslogClient", "Facility",
                        facilityDbus);
    }

    // Validate and Set Severity
    if (severity)
    {
        try
        {
            nvidia_network_protocol::FilterSeverity sevEnum =
                nlohmann::json(*severity)
                    .get<nvidia_network_protocol::FilterSeverity>();
            if (sevEnum == nvidia_network_protocol::FilterSeverity::Error ||
                sevEnum == nvidia_network_protocol::FilterSeverity::Warning ||
                sevEnum == nvidia_network_protocol::FilterSeverity::Info ||
                sevEnum == nvidia_network_protocol::FilterSeverity::All)
            {
                std::string severityDbus =
                    "xyz.openbmc_project.Logging.RsyslogClient.SeverityType." +
                    *severity;
                setDbusProperty(
                    asyncResp, "Oem/Nvidia/Rsyslog/Filter/LowestSeverity",
                    service, path, "xyz.openbmc_project.Logging.RsyslogClient",
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
        catch (const nlohmann::json::exception&)
        {
            BMCWEB_LOG_ERROR("Invalid severity (parse): {}", *severity);
            messages::propertyValueFormatError(asyncResp->res, *severity,
                                               "Filter/LowestSeverity");
            return;
        }
    }

    // Set Transport Protocol
    if (transportProtocol)
    {
        try
        {
            nvidia_network_protocol::Protocol protoEnum =
                nlohmann::json(*transportProtocol)
                    .get<nvidia_network_protocol::Protocol>();

            if (protoEnum == nvidia_network_protocol::Protocol::TCP ||
                protoEnum == nvidia_network_protocol::Protocol::UDP)
            {
                std::string protocolDbus =
                    "xyz.openbmc_project.Network.Client.TransportProtocol." +
                    *transportProtocol;
                setDbusProperty(asyncResp, "Oem/Nvidia/Rsyslog/Protocol",
                                service, path,
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
        catch (const nlohmann::json::exception&)
        {
            BMCWEB_LOG_ERROR("Invalid transport protocol (parse): {}",
                             *transportProtocol);
            messages::propertyValueFormatError(
                asyncResp->res, *transportProtocol, "TransportProtocol");
            return;
        }
    }
    // Add code here for RFCFROMAT 3164 and RFCFROMAT 5424
    if (rfcformat)
    {
        try
        {
            nvidia_network_protocol::RFCFormat rfcEnum =
                nlohmann::json(*rfcformat)
                    .get<nvidia_network_protocol::RFCFormat>();
            if (rfcEnum == nvidia_network_protocol::RFCFormat::RFC3164 ||
                rfcEnum == nvidia_network_protocol::RFCFormat::RFC5424)
            {
                std::string rfcDbus =
                    "xyz.openbmc_project.Logging.RsyslogClient.RfcFormatType." +
                    *rfcformat;
                setDbusProperty(asyncResp, "Oem/Nvidia/Rsyslog/RFCFORMAT",
                                service, path,
                                "xyz.openbmc_project.Logging.RsyslogClient",
                                "Rfcformat", rfcDbus);
            }
            else
            {
                BMCWEB_LOG_ERROR("Invalid RFC format: {}", *rfcformat);
                messages::propertyValueFormatError(asyncResp->res, *rfcformat,
                                                   "RFCFORMAT");
                return;
            }
        }
        catch (const nlohmann::json::exception&)
        {
            BMCWEB_LOG_ERROR("Invalid RFC format (parse): {}", *rfcformat);
            messages::propertyValueFormatError(asyncResp->res, *rfcformat,
                                               "RFCFORMAT");
            return;
        }
    }
}

} // namespace rsyslog
} // namespace redfish
