/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 */
#include "ports.hpp"
#include "utils/nvidia_ports_utils.hpp"

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

namespace redfish::nvidia_ports_utils
{
namespace
{

// A port as getLldpStatus() leaves it before any LLDP data is read.
nlohmann::json portWithLldpDefaults()
{
    nlohmann::json json;
    json["Id"] = "NVLinkManagement_0";
    initializeMandatoryLldpTlvFields(json["Ethernet"][lldpReceive]);
    initializeMandatoryLldpTlvFields(json["Ethernet"][lldpTransmit]);
    json["Oem"]["Nvidia"]["@odata.type"] = "#NvidiaPort.v1_0_0.NvidiaPort";
    json["Oem"]["Nvidia"]["LLDP"]["RXDataStream"] = "";
    json["Oem"]["Nvidia"]["LLDP"]["TXDataStream"] = "";
    return json;
}

TEST(RemoveLldpDefaultsFromPortJson, NVLinkDropsAllDefaults)
{
    nlohmann::json json = portWithLldpDefaults();

    removeLldpDefaultsFromPortJson(json, "NVLink");

    EXPECT_EQ(json, nlohmann::json({{"Id", "NVLinkManagement_0"}}));
}

TEST(RemoveLldpDefaultsFromPortJson, PCIeDropsAllDefaults)
{
    nlohmann::json json = portWithLldpDefaults();

    removeLldpDefaultsFromPortJson(json, "PCIe");

    EXPECT_EQ(json, nlohmann::json({{"Id", "NVLinkManagement_0"}}));
}

TEST(RemoveLldpDefaultsFromPortJson, EthernetAndInfiniBandUnchanged)
{
    for (const char* protocol : {"Ethernet", "InfiniBand", ""})
    {
        nlohmann::json json = portWithLldpDefaults();

        removeLldpDefaultsFromPortJson(json, protocol);

        EXPECT_EQ(json, portWithLldpDefaults()) << protocol;
    }
}

TEST(RemoveLldpDefaultsFromPortJson, RealLldpDataKept)
{
    nlohmann::json json = portWithLldpDefaults();
    json["Ethernet"][lldpReceive]["ChassisId"] = "c0:ff:ee:00:00:01";
    json["Oem"]["Nvidia"]["LLDP"]["RXDataStream"] = "AQID";

    removeLldpDefaultsFromPortJson(json, "NVLink");

    // Only the transmit side still held the defaults.
    EXPECT_FALSE(json["Ethernet"].contains(lldpTransmit));
    EXPECT_EQ(json["Ethernet"][lldpReceive]["ChassisId"], "c0:ff:ee:00:00:01");
    EXPECT_EQ(json["Oem"]["Nvidia"]["LLDP"],
              nlohmann::json({{"RXDataStream", "AQID"}}));
}

TEST(RemoveLldpDefaultsFromPortJson, OneStreamFilledOtherRemoved)
{
    nlohmann::json json = portWithLldpDefaults();
    json["Oem"]["Nvidia"]["LLDP"]["TXDataStream"] = "BAUG";

    removeLldpDefaultsFromPortJson(json, "PCIe");

    EXPECT_EQ(json["Oem"]["Nvidia"]["LLDP"],
              nlohmann::json({{"TXDataStream", "BAUG"}}));
}

TEST(RemoveLldpDefaultsFromPortJson, OtherKeysKept)
{
    nlohmann::json json = portWithLldpDefaults();
    json["Ethernet"]["LLDPEnabled"] = false;
    json["Oem"]["Nvidia"]["PortConnectionState"] = "Connected";

    removeLldpDefaultsFromPortJson(json, "NVLink");

    EXPECT_EQ(json["Ethernet"], nlohmann::json({{"LLDPEnabled", false}}));
    EXPECT_FALSE(json["Oem"]["Nvidia"].contains("LLDP"));
    EXPECT_EQ(json["Oem"]["Nvidia"]["PortConnectionState"], "Connected");
}

TEST(RemoveLldpDefaultsFromPortJson, NotAnObjectNoChange)
{
    nlohmann::json json = nullptr;

    removeLldpDefaultsFromPortJson(json, "NVLink");

    EXPECT_TRUE(json.is_null());
}

} // namespace
} // namespace redfish::nvidia_ports_utils
