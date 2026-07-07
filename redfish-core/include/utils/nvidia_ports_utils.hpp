/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2024 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "logging.hpp"
#include "ports.hpp"
#include "utils/dbus_utils.hpp"

#include <boost/system/error_code.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace redfish
{

namespace nvidia_ports_utils
{

constexpr std::string_view lldpRawFrameDbusIntf =
    "com.nvidia.Network.LLDP.RawFrame";
constexpr std::string_view lldpTlvsDbusIntf =
    "xyz.openbmc_project.Network.LLDP.TLVs";
constexpr std::string_view nvidiaPortOdataType =
    "#NvidiaPort.v1_6_0.NvidiaPort";

inline void ensureNvidiaPortOemBlock(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["@odata.type"] =
        nvidiaPortOdataType;
}

inline std::string extractDBusEnumSuffix(std::string_view value)
{
    const size_t lastDot = value.rfind('.');
    return (lastDot == std::string_view::npos)
               ? std::string(value)
               : std::string(value.substr(lastDot + 1));
}

inline bool isMacAddressString(std::string_view value)
{
    if (value.size() != 17)
    {
        return false;
    }
    for (size_t i = 0; i < 17; ++i)
    {
        if ((i + 1) % 3 == 0)
        {
            if (value[i] != ':')
            {
                return false;
            }
        }
        else if (std::isxdigit(static_cast<unsigned char>(value[i])) == 0)
        {
            return false;
        }
    }
    return true;
}

inline std::string normalizeMacAddressString(std::string value)
{
    if (!isMacAddressString(value))
    {
        return value;
    }
    std::ranges::transform(value, value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

inline std::string pdiChassisIdSubtypeToRedfish(std::string_view suffix)
{
    // Port.v1_4_0.IEEE802IdSubtype member names (ChassisComp, MacAddr, …).
    return std::string(suffix);
}

inline std::string pdiPortIdSubtypeToRedfish(std::string_view suffix)
{
    return std::string(suffix);
}

/**
 * @brief Encode a byte buffer as a lowercase, separator-free hex string.
 *
 * Used to populate Oem.Nvidia.LLDP.{RXDataStream, TXDataStream} on the
 * Port resource per OOB Miswiring Detection work order §6.3.
 */
// Port.LLDP{Receive,Transmit}.PortId pattern requires at least one hex octet
// pair; NVIDIA validation does not accept an empty string (unlike DMTF |^$).
constexpr std::string_view lldpPortIdNotTransmitted = "00:00:00:00:00:00";

inline std::string formatLldpPortId(std::string value)
{
    if (value.empty())
    {
        return std::string(lldpPortIdNotTransmitted);
    }
    return normalizeMacAddressString(std::move(value));
}

inline void initializeMandatoryLldpTlvFields(nlohmann::json& lldpJson)
{
    lldpJson["ChassisId"] = "";
    lldpJson["ChassisIdSubtype"] = "NotTransmitted";
    lldpJson["PortId"] = lldpPortIdNotTransmitted;
    lldpJson["PortIdSubtype"] = "NotTransmitted";
    lldpJson["ManagementVlanId"] = 0;
    lldpJson["SystemName"] = "";
}

inline std::string hexEncodeLowercase(const std::vector<uint8_t>& bytes)
{
    static constexpr std::string_view hex = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (uint8_t b : bytes)
    {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0x0F]);
    }
    return out;
}

inline void getLldpTlvsFromPath(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath, const std::string& lldpType)
{
    dbus::utility::getDbusObject(
        objectPath, std::array<std::string_view, 1>{lldpTlvsDbusIntf},
        [asyncResp, objectPath,
         lldpType](const boost::system::error_code& ec,
                   const dbus::utility::MapperGetObject& serviceMap) {
            if (ec || serviceMap.empty())
            {
                BMCWEB_LOG_DEBUG("LLDP.TLVs not present at {} ({}): skipping",
                                 objectPath, lldpType);
                return;
            }
            const std::string& service = serviceMap.front().first;
            dbus::utility::getAllProperties(
                service, objectPath, std::string(lldpTlvsDbusIntf),
                [asyncResp, lldpType, objectPath](
                    const boost::system::error_code& gec,
                    const dbus::utility::DBusPropertiesMap& properties) {
                    if (gec)
                    {
                        BMCWEB_LOG_DEBUG(
                            "LLDP.TLVs GetAll failed for {} ({}): {}",
                            objectPath, lldpType, gec.message());
                        return;
                    }
                    nlohmann::json& lldpJson =
                        asyncResp->res.jsonValue["Ethernet"][lldpType];
                    for (const auto& [propName, propValue] : properties)
                    {
                        if (propName == "ManagementVlanId")
                        {
                            const auto* v = std::get_if<uint16_t>(&propValue);
                            if (v != nullptr)
                            {
                                lldpJson[propName] = *v;
                            }
                            continue;
                        }
                        if (propName == "ChassisIdSubtype" ||
                            propName == "PortIdSubtype")
                        {
                            const std::string* sv =
                                std::get_if<std::string>(&propValue);
                            if (sv == nullptr)
                            {
                                continue;
                            }
                            const std::string suffix =
                                extractDBusEnumSuffix(*sv);
                            const std::string redfish =
                                (propName == "ChassisIdSubtype")
                                    ? pdiChassisIdSubtypeToRedfish(suffix)
                                    : pdiPortIdSubtypeToRedfish(suffix);
                            lldpJson[propName] = redfish;
                            continue;
                        }
                        if (propName == "ChassisId")
                        {
                            const std::string* sv =
                                std::get_if<std::string>(&propValue);
                            if (sv == nullptr)
                            {
                                continue;
                            }
                            lldpJson[propName] = normalizeMacAddressString(*sv);
                            continue;
                        }
                        if (propName == "PortId")
                        {
                            const std::string* sv =
                                std::get_if<std::string>(&propValue);
                            if (sv == nullptr)
                            {
                                continue;
                            }
                            lldpJson[propName] = formatLldpPortId(*sv);
                            continue;
                        }
                        if (propName == "SystemName")
                        {
                            const std::string* sv =
                                std::get_if<std::string>(&propValue);
                            if (sv == nullptr)
                            {
                                continue;
                            }
                            lldpJson[propName] = *sv;
                        }
                    }
                });
        });
}

inline void getLldpRawFrame(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                            const std::string& objectPath,
                            const std::string& streamKey)
{
    dbus::utility::getDbusObject(
        objectPath, std::array<std::string_view, 1>{lldpRawFrameDbusIntf},
        [asyncResp, objectPath,
         streamKey](const boost::system::error_code& ec,
                    const dbus::utility::MapperGetObject& serviceMap) {
            if (ec || serviceMap.empty())
            {
                BMCWEB_LOG_DEBUG("RawFrame not present at {}: skipping",
                                 objectPath);
                return;
            }
            const std::string& service = serviceMap.front().first;
            dbus::utility::getProperty<std::vector<uint8_t>>(
                service, objectPath, std::string(lldpRawFrameDbusIntf), "Data",
                [asyncResp, streamKey](const boost::system::error_code& gec,
                                       const std::vector<uint8_t>& data) {
                    if (gec)
                    {
                        BMCWEB_LOG_DEBUG("RawFrame.Data read failed: {}",
                                         gec.message());
                        return;
                    }
                    asyncResp->res
                        .jsonValue["Oem"]["Nvidia"]["LLDP"][streamKey] =
                        hexEncodeLowercase(data);
                });
        });
}

inline void populateLldpDirectionFromAssociation(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& portInventoryPath, std::string_view associationSuffix,
    const std::string& lldpType, const std::string& streamKey)
{
    dbus::utility::getAssociationEndPoints(
        portInventoryPath + std::string(associationSuffix),
        [asyncResp, lldpType, streamKey,
         associationSuffix](const boost::system::error_code& ec,
                            const dbus::utility::MapperEndPoints& endpoints) {
            if (ec || endpoints.empty())
            {
                BMCWEB_LOG_DEBUG("No {} association: {}", associationSuffix,
                                 ec.message());
                return;
            }

            const std::string& packetPath = endpoints.front();
            getLldpTlvsFromPath(asyncResp, packetPath, lldpType);
            getLldpRawFrame(asyncResp, packetPath, streamKey);
        });
}

/**
 * @brief Populate NVIDIA OEM LLDP fields for a CX_NIC port (OOB Miswiring).
 *
 * Discovers per-direction LLDP packet objects via port associations
 * (lldp_rx_data / lldp_tx_data), then reads TLVs and raw frame data.
 */
inline void getLldpStatus(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const std::string& portInventoryPath)
{
    // Always expose mandatory LLDP TLV fields; async D-Bus reads overwrite
    // defaults when nsmd has cached TLV data.
    nlohmann::json& receiveJson =
        asyncResp->res.jsonValue["Ethernet"][lldpReceive];
    initializeMandatoryLldpTlvFields(receiveJson);
    nlohmann::json& transmitJson =
        asyncResp->res.jsonValue["Ethernet"][lldpTransmit];
    initializeMandatoryLldpTlvFields(transmitJson);
    ensureNvidiaPortOemBlock(asyncResp);
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["LLDP"]["RXDataStream"] = "";
    asyncResp->res.jsonValue["Oem"]["Nvidia"]["LLDP"]["TXDataStream"] = "";

    populateLldpDirectionFromAssociation(
        asyncResp, portInventoryPath, "/lldp_rx_data", lldpReceive,
        "RXDataStream");
    populateLldpDirectionFromAssociation(
        asyncResp, portInventoryPath, "/lldp_tx_data", lldpTransmit,
        "TXDataStream");
}

/**
 * @brief Compute Ethernet.LLDPEnabled from parent NetworkAdapter LLDP modes.
 */
inline void getLldpEnabledFromParent(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& networkAdapterPath)
{
    static constexpr std::string_view modesIntf =
        "com.nvidia.Network.LLDP.Modes";
    dbus::utility::getAssociationEndPoints(
        networkAdapterPath + "/lldp_mode_settings",
        [asyncResp](const boost::system::error_code& ec,
                    const dbus::utility::MapperEndPoints& endpoints) {
            if (ec || endpoints.empty())
            {
                BMCWEB_LOG_DEBUG(
                    "No lldp_mode_settings endpoints: skipping LLDPEnabled");
                return;
            }

            const std::string& lldpModesPath = endpoints.front();
            dbus::utility::getDbusObject(
                lldpModesPath, std::array<std::string_view, 1>{modesIntf},
                [asyncResp, lldpModesPath](
                    const boost::system::error_code& ec2,
                    const dbus::utility::MapperGetObject& serviceMap) {
                    if (ec2 || serviceMap.empty())
                    {
                        BMCWEB_LOG_DEBUG(
                            "LLDP.Modes not present at {}: skipping LLDPEnabled",
                            lldpModesPath);
                        return;
                    }
                    const std::string& service = serviceMap.front().first;
                    dbus::utility::getAllProperties(
                        service, lldpModesPath, std::string(modesIntf),
                        [asyncResp](
                            const boost::system::error_code& gec,
                            const dbus::utility::DBusPropertiesMap& props) {
                            if (gec)
                            {
                                BMCWEB_LOG_DEBUG("LLDP.Modes GetAll failed: {}",
                                                 gec.message());
                                return;
                            }
                            const std::string* tx = nullptr;
                            const std::string* rx = nullptr;
                            if (!sdbusplus::unpackPropertiesNoThrow(
                                    dbus_utils::UnpackErrorPrinter(), props,
                                    "TXMode", tx, "RXMode", rx))
                            {
                                return;
                            }
                            auto isOff = [](const std::string* v) {
                                if (v == nullptr)
                                {
                                    return true;
                                }
                                size_t lastDot = v->find_last_of('.');
                                std::string suffix =
                                    (lastDot == std::string::npos)
                                        ? *v
                                        : v->substr(lastDot + 1);
                                return suffix == "Off";
                            };
                            asyncResp->res
                                .jsonValue["Ethernet"]["LLDPEnabled"] =
                                !(isOff(tx) && isOff(rx));
                        });
                });
        });
}

} // namespace nvidia_ports_utils
} // namespace redfish
