// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES.
// All rights reserved.

#include "registries.hpp"
#include "registries/oem/nvidia_factory_reset_message_registry.hpp"

#include <array>
#include <cstddef>
#include <string_view>

#include <gtest/gtest.h>

namespace redfish::registries
{
namespace
{

using Reg = NvidiaFactoryReset;

TEST(NvidiaFactoryResetMessageRegistry, HeaderIdentity)
{
    EXPECT_STREQ(Reg::header.registryPrefix, "NvidiaFactoryReset");
    EXPECT_EQ(Reg::header.versionMajor, 1U);
    EXPECT_EQ(Reg::header.versionMinor, 0U);
    EXPECT_EQ(Reg::header.versionPatch, 0U);
}

TEST(NvidiaFactoryResetMessageRegistry, EntriesMatchContract)
{
    ASSERT_EQ(Reg::registry.size(), 4U);

    const Message& failed =
        Reg::registry[static_cast<size_t>(Reg::Index::attestedWipeFailed)]
            .second;
    EXPECT_STREQ(failed.message, "Attested wipe failed. Error: %1.");
    EXPECT_STREQ(failed.messageSeverity, "Critical");
    EXPECT_EQ(failed.numberOfArgs, 1U);
    EXPECT_STREQ(failed.paramTypes[0], "string");
    EXPECT_STREQ(failed.resolution,
                 "Read the error in this message, correct that condition, and "
                 "retry the attested wipe.");

    const Message& succeeded =
        Reg::registry[static_cast<size_t>(Reg::Index::attestedWipeSucceeded)]
            .second;
    EXPECT_STREQ(succeeded.message, "Attested wipe completed successfully.");
    EXPECT_STREQ(succeeded.messageSeverity, "OK");
    EXPECT_EQ(succeeded.numberOfArgs, 0U);

    const Message& ignored =
        Reg::registry[static_cast<size_t>(Reg::Index::factoryResetIgnored)]
            .second;
    EXPECT_STREQ(ignored.message,
                 "Factory reset ignored. Button released before %1 seconds.");
    EXPECT_STREQ(ignored.messageSeverity, "OK");
    EXPECT_EQ(ignored.numberOfArgs, 1U);
    EXPECT_STREQ(ignored.paramTypes[0], "string");

    const Message& scheduled =
        Reg::registry[static_cast<size_t>(Reg::Index::factoryResetScheduled)]
            .second;
    EXPECT_STREQ(scheduled.message,
                 "Factory reset scheduled. Button held for %1 seconds.");
    EXPECT_STREQ(scheduled.messageSeverity, "Warning");
    EXPECT_EQ(scheduled.numberOfArgs, 1U);
    EXPECT_STREQ(scheduled.paramTypes[0], "string");
}

TEST(NvidiaFactoryResetMessageRegistry, LookupUsesRegistryPrefix)
{
    const Message* failed =
        getMessage("NvidiaFactoryReset.1.0.AttestedWipeFailed");
    ASSERT_NE(failed, nullptr);
    EXPECT_STREQ(failed->messageSeverity, "Critical");
    EXPECT_EQ(failed->numberOfArgs, 1U);
}

TEST(NvidiaFactoryResetMessageRegistry, GenericFormattingUsesDeclaredArguments)
{
    constexpr std::array<std::string_view, 0> noArgs{};
    const Message& succeeded =
        Reg::registry[static_cast<size_t>(Reg::Index::attestedWipeSucceeded)]
            .second;
    EXPECT_EQ(fillMessageArgs(noArgs, succeeded.message),
              "Attested wipe completed successfully.");

    constexpr std::array<std::string_view, 1> failureArgs{"NIC wipe failed"};
    const Message& failed =
        Reg::registry[static_cast<size_t>(Reg::Index::attestedWipeFailed)]
            .second;
    EXPECT_EQ(fillMessageArgs(failureArgs, failed.message),
              "Attested wipe failed. Error: NIC wipe failed.");

    constexpr std::array<std::string_view, 1> holdArgs{"10"};
    const Message& ignored =
        Reg::registry[static_cast<size_t>(Reg::Index::factoryResetIgnored)]
            .second;
    EXPECT_EQ(fillMessageArgs(holdArgs, ignored.message),
              "Factory reset ignored. Button released before 10 seconds.");
    const Message& scheduled =
        Reg::registry[static_cast<size_t>(Reg::Index::factoryResetScheduled)]
            .second;
    EXPECT_EQ(fillMessageArgs(holdArgs, scheduled.message),
              "Factory reset scheduled. Button held for 10 seconds.");
}

} // namespace
} // namespace redfish::registries
