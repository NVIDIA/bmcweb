// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.
#include "registries.hpp"
#include "registries/oem/nvidia_update_message_registry.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace redfish::registries
{
namespace
{

using Reg = NvidiaUpdate;

TEST(NvidiaUpdateRegistry, HeaderIdentity)
{
    EXPECT_STREQ(Reg::header.registryPrefix, "NvidiaUpdate");
    EXPECT_EQ(Reg::header.versionMajor, 1U);
    EXPECT_EQ(Reg::header.versionMinor, 2U);
    EXPECT_EQ(Reg::header.versionPatch, 0U);
}

TEST(NvidiaUpdateRegistry, EntriesAlphabetical)
{
    // parse_registries.py emits entries sorted by message key; the Index enum
    // is positional, so ordering is load-bearing.
    ASSERT_EQ(Reg::registry.size(), 24U);
    for (size_t i = 1; i < Reg::registry.size(); i++)
    {
        EXPECT_LT(std::string_view(Reg::registry[i - 1].first),
                  std::string_view(Reg::registry[i].first))
            << "registry not sorted at index " << i;
    }
}

TEST(NvidiaUpdateRegistry, PreUpdateValidationIndexMatchesArray)
{
    const std::array<std::pair<Reg::Index, std::string_view>, 2> expected = {{
        {Reg::Index::preUpdateValidationFailed, "PreUpdateValidationFailed"},
        {Reg::Index::firmwarePackageComponentImageMissing,
         "FirmwarePackageComponentImageMissing"},
    }};
    for (const auto& [index, key] : expected)
    {
        EXPECT_EQ(
            std::string_view(Reg::registry[static_cast<size_t>(index)].first),
            key);
    }
}

TEST(NvidiaUpdateRegistry, PreUpdateValidationArgCountsAndSeverity)
{
    const auto& summary =
        Reg::registry[static_cast<size_t>(
                          Reg::Index::preUpdateValidationFailed)]
            .second;
    EXPECT_STREQ(summary.messageSeverity, "Critical");
    EXPECT_EQ(summary.numberOfArgs, 0U);

    const auto& coverage =
        Reg::registry[static_cast<size_t>(
                          Reg::Index::firmwarePackageComponentImageMissing)]
            .second;
    EXPECT_STREQ(coverage.messageSeverity, "Critical");
    EXPECT_EQ(coverage.numberOfArgs, 1U);
}

TEST(NvidiaUpdateRegistry, RenderedMessageIdIsTwoPart)
{
    // getLogFromRegistry renders "<prefix>.<major>.<minor>.<Key>" - the exact
    // string the FW-update Task Messages[] carries and the IT plan asserts.
    const std::string name = "HGX_FW_GPU_0";
    nlohmann::json::object_t log = getLogFromRegistry(
        Reg::header, Reg::registry,
        static_cast<size_t>(Reg::Index::firmwarePackageComponentImageMissing),
        std::to_array<std::string_view>({name}));
    EXPECT_EQ(log["MessageId"],
              "NvidiaUpdate.1.2.FirmwarePackageComponentImageMissing");
    EXPECT_EQ(log["MessageSeverity"], "Critical");
    const std::string* message = log["Message"].get_ptr<const std::string*>();
    ASSERT_NE(message, nullptr);
    EXPECT_NE(message->find(name), std::string::npos);
    ASSERT_TRUE(log["MessageArgs"].is_array());
    EXPECT_EQ(log["MessageArgs"][0], name);
}

TEST(NvidiaUpdateRegistry, SummaryRendersWithNoArgs)
{
    nlohmann::json::object_t log = getLogFromRegistry(
        Reg::header, Reg::registry,
        static_cast<size_t>(Reg::Index::preUpdateValidationFailed), {});
    EXPECT_EQ(log["MessageId"], "NvidiaUpdate.1.2.PreUpdateValidationFailed");
    EXPECT_EQ(
        log["Message"],
        "The firmware update request was rejected because one or more target components failed pre-update validation.");
}

} // namespace
} // namespace redfish::registries
