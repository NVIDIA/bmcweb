/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
 * AFFILIATES. All rights reserved. SPDX-License-Identifier: Apache-2.0
 */
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "utils/fw_utils.hpp"

#include <boost/system/error_code.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <string>

#include <gtest/gtest.h>

namespace redfish::fw_util
{
namespace
{

dbus::utility::DBusPropertiesMap slotProperties(uint8_t slotId, bool isActive)
{
    dbus::utility::DBusPropertiesMap properties;
    properties.emplace_back("SlotId", slotId);
    properties.emplace_back("IsActive", isActive);
    return properties;
}

TEST(SlotMatchesType, ActiveSlot)
{
    EXPECT_TRUE(slotMatchesType(slotProperties(0, true), "ActiveFirmwareSlot"));
    EXPECT_FALSE(
        slotMatchesType(slotProperties(0, true), "InactiveFirmwareSlot"));
}

TEST(SlotMatchesType, InactiveSlot)
{
    EXPECT_FALSE(
        slotMatchesType(slotProperties(1, false), "ActiveFirmwareSlot"));
    EXPECT_TRUE(
        slotMatchesType(slotProperties(1, false), "InactiveFirmwareSlot"));
}

TEST(SlotMatchesType, IsActiveMissing)
{
    dbus::utility::DBusPropertiesMap properties;
    properties.emplace_back("SlotId", uint8_t{0});
    EXPECT_TRUE(slotMatchesType(properties, "ActiveFirmwareSlot"));
    EXPECT_TRUE(slotMatchesType(properties, "InactiveFirmwareSlot"));
}

TEST(SlotMatchesType, IsActiveNotBool)
{
    dbus::utility::DBusPropertiesMap properties;
    properties.emplace_back("IsActive", std::string("true"));
    EXPECT_TRUE(slotMatchesType(properties, "ActiveFirmwareSlot"));
    EXPECT_TRUE(slotMatchesType(properties, "InactiveFirmwareSlot"));
}

// Both slots are in both association lists and the old slot comes first.
TEST(AfterGetSlotProperties, BothSlotsInBothLists)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    const boost::system::error_code ec;

    afterGetSlotProperties(asyncResp, "ActiveFirmwareSlot", ec,
                           slotProperties(1, false));
    afterGetSlotProperties(asyncResp, "ActiveFirmwareSlot", ec,
                           slotProperties(0, true));
    afterGetSlotProperties(asyncResp, "InactiveFirmwareSlot", ec,
                           slotProperties(0, true));
    afterGetSlotProperties(asyncResp, "InactiveFirmwareSlot", ec,
                           slotProperties(1, false));

    const nlohmann::json& oemNvidia = asyncResp->res.jsonValue["Oem"]["Nvidia"];
    EXPECT_EQ(oemNvidia["ActiveFirmwareSlot"]["SlotId"], 0);
    EXPECT_EQ(oemNvidia["InactiveFirmwareSlot"]["SlotId"], 1);
}

// Two slots match the same slot type: only the first one is reported.
TEST(AfterGetSlotProperties, OneSlotPerSlotType)
{
    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    const boost::system::error_code ec;

    afterGetSlotProperties(asyncResp, "InactiveFirmwareSlot", ec,
                           slotProperties(0, false));
    afterGetSlotProperties(asyncResp, "InactiveFirmwareSlot", ec,
                           slotProperties(1, false));

    const nlohmann::json& oemNvidia = asyncResp->res.jsonValue["Oem"]["Nvidia"];
    EXPECT_EQ(oemNvidia["InactiveFirmwareSlot"]["SlotId"], 0);
    EXPECT_FALSE(oemNvidia.contains("ActiveFirmwareSlot"));
}

} // namespace
} // namespace redfish::fw_util
