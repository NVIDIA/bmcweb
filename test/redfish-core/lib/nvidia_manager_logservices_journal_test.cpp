// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.

#include "nvidia_manager_logservices_journal.hpp"

#include "async_resp.hpp"
#include "error_messages.hpp"
#include "http_response.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace redfish
{
namespace
{

constexpr std::array kDbusSeverities{
    log_service::SyslogSeverity::Emergency,
    log_service::SyslogSeverity::Alert,
    log_service::SyslogSeverity::Critical,
    log_service::SyslogSeverity::Error,
    log_service::SyslogSeverity::Warning,
    log_service::SyslogSeverity::Notice,
    log_service::SyslogSeverity::Informational,
    log_service::SyslogSeverity::Debug,
};

std::string enumName(log_service::SyslogSeverity severity)
{
    return nlohmann::json(severity).get<std::string>();
}

std::string dbusEntryLevel(std::string_view name)
{
    return std::string(entryLevelPrefix) + std::string(name);
}

TEST(JournalSyslogFilters, AllowableValues)
{
    nlohmann::json::array_t expected;
    for (auto severity : kDbusSeverities)
    {
        expected.emplace_back(enumName(severity));
    }
    expected.emplace_back("All");
    EXPECT_EQ(journalLowestSeverityAllowableValues(), expected);
}

TEST(JournalSyslogFilters, SeverityMapping)
{
    for (auto severity : kDbusSeverities)
    {
        const std::string name = enumName(severity);
        EXPECT_EQ(parseRedfishSyslogSeverity(name), severity) << name;
        EXPECT_EQ(toRedfishSyslogSeverity(dbusEntryLevel(name)), severity)
            << name;
        EXPECT_EQ(toDbusEntryLevel(severity), dbusEntryLevel(name)) << name;
    }

    EXPECT_EQ(parseRedfishSyslogSeverity("All"),
              log_service::SyslogSeverity::All);
    EXPECT_EQ(toDbusEntryLevel(log_service::SyslogSeverity::All),
              dbusEntryLevel("Debug"));
    EXPECT_FALSE(toDbusEntryLevel(log_service::SyslogSeverity::Invalid));

    for (std::string_view bad : {"Foo", "", "Invalid", "Foo.Error"})
    {
        EXPECT_FALSE(parseRedfishSyslogSeverity(bad)) << bad;
    }
    EXPECT_FALSE(toRedfishSyslogSeverity(dbusEntryLevel("Nope")));
    EXPECT_FALSE(toRedfishSyslogSeverity("Foo.Error"));
    EXPECT_FALSE(toRedfishSyslogSeverity("6"));
}

TEST(JournalSyslogFilters, PatchArraySizeTooLongUsesMaxAllowed)
{
    std::vector<nlohmann::json::object_t> filters(2);
    filters[0]["LowestSeverity"] = "Error";
    filters[1]["LowestSeverity"] = "Debug";

    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    log_service::SyslogSeverity severity{};
    bool setProperty = true;
    EXPECT_FALSE(
        parseJournalSyslogFilters(filters, asyncResp, severity, setProperty));
    EXPECT_FALSE(setProperty);

    crow::Response tooLong;
    messages::arraySizeTooLong(tooLong, "SyslogFilters", 1);
    EXPECT_EQ(asyncResp->res.result(), tooLong.result());
    EXPECT_EQ(asyncResp->res.jsonValue, tooLong.jsonValue);
}

TEST(JournalSyslogFilters, ParseOmittedLowestSeverityDoesNotSet)
{
    std::vector<nlohmann::json::object_t> filters(1);

    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    log_service::SyslogSeverity severity{};
    bool setProperty = true;
    EXPECT_TRUE(
        parseJournalSyslogFilters(filters, asyncResp, severity, setProperty));
    EXPECT_FALSE(setProperty);
}

TEST(JournalSyslogFilters, ParseEmptyArraySetsAll)
{
    std::vector<nlohmann::json::object_t> filters;

    auto asyncResp = std::make_shared<bmcweb::AsyncResp>();
    log_service::SyslogSeverity severity{};
    bool setProperty = false;
    EXPECT_TRUE(
        parseJournalSyslogFilters(filters, asyncResp, severity, setProperty));
    EXPECT_TRUE(setProperty);
    EXPECT_EQ(severity, log_service::SyslogSeverity::All);
}

} // namespace
} // namespace redfish
