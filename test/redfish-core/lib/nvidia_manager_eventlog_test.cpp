// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION &
// AFFILIATES. All rights reserved.

#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "nvidia_manager_eventlog.hpp"
#include "registries/base_message_registry.hpp"
#include "utils/time_utils.hpp"

#include <boost/beast/http/status.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

const std::string* jsonStringField(const nlohmann::json& value,
                                   std::string_view key)
{
    const nlohmann::json::object_t* object =
        value.get_ptr<const nlohmann::json::object_t*>();
    if (object == nullptr)
    {
        return nullptr;
    }
    const nlohmann::json::object_t::const_iterator it =
        object->find(std::string(key));
    if (it == object->end())
    {
        return nullptr;
    }
    return it->second.get_ptr<const std::string*>();
}

// phosphor-logging Timestamp is milliseconds. A MessageId entry overwrites
// Created inside generateMessageRegistry(). Treating that value as seconds
// clamps the year to 9999-12-31. The MessageId must resolve, or the
// registry helper returns before it overwrites Created.
TEST(ManagerEventLogEntry, MessageIdCreatedUsesMillisecondTimestamp)
{
    constexpr uint64_t timestampMs = 1638312095123;
    constexpr uint64_t updateTimestampMs = 1638312095456;
    const std::string messageId =
        std::format("{}.{}.{}.Created", registries::Base::header.registryPrefix,
                    registries::Base::header.versionMajor,
                    registries::Base::header.versionMinor);

    std::vector<std::pair<std::string, std::string>> additional;
    additional.emplace_back("REDFISH_MESSAGE_ID", messageId);

    const dbus::utility::DBusPropertiesMap properties = {
        {"AdditionalData", dbus::utility::DbusVariantType(additional)},
        {"EventId", dbus::utility::DbusVariantType("")},
        {"Id", dbus::utility::DbusVariantType(static_cast<uint32_t>(42))},
        {"Message", dbus::utility::DbusVariantType("created")},
        {"Path", dbus::utility::DbusVariantType(
                     "/xyz/openbmc_project/logging/entry/42")},
        {"Resolution", dbus::utility::DbusVariantType("")},
        {"Resolved", dbus::utility::DbusVariantType(false)},
        {"ServiceProviderNotify", dbus::utility::DbusVariantType("")},
        {"Severity",
         dbus::utility::DbusVariantType(
             "xyz.openbmc_project.Logging.Entry.Level.Informational")},
        {"Timestamp", dbus::utility::DbusVariantType(timestampMs)},
        {"UpdateTimestamp", dbus::utility::DbusVariantType(updateTimestampMs)},
    };

    std::shared_ptr<bmcweb::AsyncResp> asyncResp =
        std::make_shared<bmcweb::AsyncResp>();
    nlohmann::json entry;
    fillManagerEventLogLogEntryFromPropertyMap(asyncResp, properties, entry);

    EXPECT_EQ(asyncResp->res.result(), boost::beast::http::status::ok);

    const std::string* created = jsonStringField(entry, "Created");
    const std::string* modified = jsonStringField(entry, "Modified");
    const std::string* gotMessageId = jsonStringField(entry, "MessageId");
    ASSERT_NE(created, nullptr);
    ASSERT_NE(modified, nullptr);
    ASSERT_NE(gotMessageId, nullptr);
    EXPECT_EQ(*gotMessageId, messageId);

    const std::string expectedCreated =
        time_utils::getDateTimeUintMs(timestampMs);
    EXPECT_EQ(*created, expectedCreated);
    EXPECT_NE(expectedCreated.find(".123"), std::string::npos);
    EXPECT_EQ(*modified, time_utils::getDateTimeUintMs(updateTimestampMs));
}

} // namespace
} // namespace redfish
