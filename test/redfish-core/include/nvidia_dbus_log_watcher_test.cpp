/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION &
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
#include "dbus_utility.hpp"
#include "utils/dbus_event_log_entry.hpp"

#include <cstdint>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::ElementsAre;

TEST(DbusEventLogEntry, FillDbusEventLogEntryFromPropertyMapSuccess)
{
    std::vector<std::pair<std::string, std::string>> data;
    data.emplace_back("KEY1", "VALUE1");
    data.emplace_back("KEY2", "VALUE2");

    const dbus::utility::DBusPropertiesMap propMap = {
        {"AdditionalData", dbus::utility::DbusVariantType(data)},
        {"Id", dbus::utility::DbusVariantType(static_cast<uint32_t>(1234))},
        {"Message", dbus::utility::DbusVariantType("Test message")},
        {"Path", dbus::utility::DbusVariantType("/test/path")},
        {"Resolution", dbus::utility::DbusVariantType("Test resolution")},
        {"Resolved", dbus::utility::DbusVariantType(false)},
        {"ServiceProviderNotify",
         dbus::utility::DbusVariantType("Test notify")},
        {"Severity", dbus::utility::DbusVariantType("Warning")},
        {"Timestamp",
         dbus::utility::DbusVariantType(static_cast<uint64_t>(1234567890))},
        {"UpdateTimestamp",
         dbus::utility::DbusVariantType(static_cast<uint64_t>(9876543210))},
    };

    auto result = redfish::fillDbusEventLogEntryFromPropertyMap(propMap);
    if (result)
    {
        EXPECT_TRUE(result.has_value());

        const auto& entry = *result;

        EXPECT_EQ(entry.Id, 1234);
        EXPECT_EQ(entry.Message, "Test message");
        EXPECT_EQ(entry.Resolved, false);
        EXPECT_EQ(entry.Severity, "Warning");
        EXPECT_EQ(entry.Timestamp, 1234567890);
        EXPECT_EQ(entry.UpdateTimestamp, 9876543210);
        EXPECT_EQ(entry.ServiceProviderNotify, "Test notify");
        EXPECT_EQ(entry.AdditionalData.size(), 2);
        EXPECT_THAT(
            entry.AdditionalData,
            ElementsAre(
                std::make_pair(std::string("KEY1"), std::string("VALUE1")),
                std::make_pair(std::string("KEY2"), std::string("VALUE2"))));
        EXPECT_EQ(*entry.Path, "/test/path");
        EXPECT_EQ(*entry.Resolution, "Test resolution");
    }
}

TEST(DbusEventLogEntry, FillDbusEventLogEntryFromPropertyMapEmptyMap)
{
    const dbus::utility::DBusPropertiesMap propMap = {};

    auto result = redfish::fillDbusEventLogEntryFromPropertyMap(propMap);

    EXPECT_FALSE(result.has_value());
}

TEST(DbusEventLogEntry,
     FillDbusEventLogEntryFromPropertyMapMissingRequiredFields)
{
    std::vector<std::pair<std::string, std::string>> data;
    data.emplace_back("KEY1", "VALUE1");
    data.emplace_back("KEY2", "VALUE2");

    const dbus::utility::DBusPropertiesMap propMap = {
        {"AdditionalData", dbus::utility::DbusVariantType(data)},
        // Missing Id, Message, Resolved, Severity, Timestamp
        {"UpdateTimestamp",
         dbus::utility::DbusVariantType(static_cast<uint64_t>(9999999999))},
    };

    auto result = redfish::fillDbusEventLogEntryFromPropertyMap(propMap);

    EXPECT_FALSE(result.has_value());
}

TEST(DbusEventLogEntry, FillDbusEventLogEntryFromPropertyMapWrongTypes)
{
    const dbus::utility::DBusPropertiesMap propMap = {
        {"Id",
         dbus::utility::DbusVariantType("not_a_number")}, // Should be uint32_t
        {"Message", dbus::utility::DbusVariantType(
                        static_cast<uint32_t>(123))},     // Should be string
        {"Resolved",
         dbus::utility::DbusVariantType("not_a_bool")},   // Should be bool
        {"Severity", dbus::utility::DbusVariantType(
                         static_cast<uint64_t>(456))},    // Should be string
        {"Timestamp", dbus::utility::DbusVariantType(
                          "not_a_timestamp")},            // Should be uint64_t
    };

    auto result = redfish::fillDbusEventLogEntryFromPropertyMap(propMap);

    EXPECT_FALSE(result.has_value());
}
